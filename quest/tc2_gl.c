/*
 * tc2_gl.c -- THE GPU PICTURE: a built frame (src/tc2_build.h) drawn with OpenGL, organised like an ordinary PC renderer.
 *
 *   ASSETS, uploaded once (the ROM is data, nothing more):
 *     the texture tiles (tss1cg*, 32 MB of 8-bit pens)  -> an 8192 x 4096 single-channel texture
 *     the decoded tile map (tss1ccrl/h: tile, flips)     -> a 2048 x 1024 RGB texture
 *   PER FRAME, small uploads: the palette (32768 pens), the C412 stencil SRAM, the text layer's pen map, and ONE vertex buffer holding every
 *   polygon of the frame as triangles in the hardware's draw order (z-sort key, far to near).
 *   Camera-space polygons are projected separately for each OpenXR eye. The
 *   off-screen targets use the eye's resolution and OpenGL bottom-up storage.
 *   PASSES:
 *     1. clear to the C404 background colour
 *     2. the text layer's pixels (all of them sit under the polygons at first)
 *     3. the polygons: ONE draw call. GL draws the primitives of a call in order, so blending against what is already there reproduces the
 *        hardware's painter's order exactly, without a depth buffer. The fragment shader does what the board's pixel pipeline does: the
 *        tile map -> tile -> pen lookup per pixel (no texture baking), the 2/4/8-bit colour modes, the stencil, Gouraud shade, the polygon
 *        colour fade, the screen fade, alpha by colour / by pen and the 50% blend.
 *     4. the same triangles into a small priority target (MAME's priority map: which text pixels a polygon lets through again)
 *     5. the text pixels the priority target marks, over the polygons
 *   6. apply the original gamma table directly into the current eye image.
 * Requires OpenGL ES 3.0 (Quest) or OpenGL 4.3 (Windows), with 8192-texel textures.
 * Software rendering is not a VR fallback. Diagnostics explicitly read back
 * the final eye picture; normal rendering does not copy it to the CPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "quest_gpu.h"
#include "tc2_scene.h"
#include <math.h>
#include "tc2_build.h"

/* the ROM data, owned by src/tc2_render.c */
void tc2_render_assets(const uint8_t **texrom, const uint32_t **tm_decoded, const uint8_t **tattr);

static const char *vs_poly =
    "#version 300 es\nprecision highp float;precision highp int;\n"
    "in vec4 a_pos; in vec3 a_uvs; in vec4 a_par; in vec4 a_clip; in vec2 a_ext;\n"
    "out vec2 v_screen;uniform mat4 u_view,u_proj;uniform int u_flat;out vec3 v_uvs; flat out vec4 v_par; flat out vec4 v_clip; flat out vec2 v_ext;\n"
    "void main() { gl_Position = u_flat!=0?vec4(a_pos.x,-a_pos.y,a_pos.z,a_pos.w):u_proj*u_view*vec4(a_pos.xyz,1);v_screen=a_clip.xy; v_uvs = a_uvs; v_par = a_par; v_clip = a_clip; v_ext = a_ext; }\n";
static const char *fs_poly =
    "#version 300 es\nprecision highp float;precision highp int;out vec4 frag;\n"
    "uniform highp sampler2D t_rom, t_map, t_pal, t_sten, t_near, t_nearz;\n"
    "uniform int u_ex, u_scale, u_h, u_prio, u_pfade, u_ff, u_alpha, u_alpha_pen, u_near, u_depth;\n"
    "uniform float u_keep;\n"
    "uniform ivec3 u_pcol, u_fcol;\n"
    "in vec2 v_screen;in vec3 v_uvs; flat in vec4 v_par; flat in vec4 v_clip; flat in vec2 v_ext;\n"
    "uint b8(vec4 t, int c) { return uint(t[c] * 255.0 + 0.5); }\n"
    "void main() {\n"
    "  ivec2 px = ivec2(v_screen);\n"
    ""
    "  int fl = int(v_par.w);\n"
    "  uint tx = uint(int(v_uvs.x)), ty = uint(int(v_uvs.y));\n"
    "  if ((fl & 1) != 0) {\n"                                            /* the stencil: a 0 bit in the C412 SRAM hides the pixel */
    "    uint offs = ((ty << 6u) | (tx >> 4u)) & 0x1FFFFu;\n"
    "    vec4 s = texelFetch(t_sten, ivec2(int(offs & 511u), int(offs >> 9u)), 0);\n"
    "    uint w = b8(s, 0) | (b8(s, 1) << 8u);\n"
    "    if (((w >> ((tx & 15u) ^ 15u)) & 1u) == 0u) discard;\n"
    "  }\n"
    "  ty += uint(v_par.z);\n"
    "  float oz = gl_FragCoord.w;\n"                                      /* 1/z: what the software renderer interpolates */
    "  if (u_near != 0) {\n"                                              /* EXACT DEPTH pre-pass: the nearest opaque pixel, its object and band */
    "    if ((fl & 2) != 0 || (u_alpha != 255 && (fl & 4) != 0)) discard;\n"
    "    gl_FragDepth = clamp(0.5 + log2(max(oz, 1e-30)) / 256.0, 0.0, 1.0);\n"
    "    int e = int(v_ext.x);\n"
    "    frag = vec4(float(e & 255) / 255.0, float((e >> 8) & 255) / 255.0, v_ext.y / 255.0, 1.0); return;\n"
    "  }\n"
    "  if (u_depth != 0) {\n"                                             /* hidden if ANOTHER object of this band is clearly nearer here */
    "    ivec2 fp = ivec2(gl_FragCoord.xy); vec4 nn = texelFetch(t_near, fp, 0);\n"
    "    if (nn.a > 0.5) {\n"
    "      int ne = int(nn.r * 255.0 + 0.5) | (int(nn.g * 255.0 + 0.5) << 8), nb = int(nn.b * 255.0 + 0.5);\n"
    "      if (ne != int(v_ext.x) && nb == int(v_ext.y) && oz < exp2((texelFetch(t_nearz, fp, 0).r - 0.5) * 256.0) * u_keep) discard;\n"
    "    }\n"
    "  }\n"
    "  int cmode = int(v_par.y); uint pbase = uint(v_par.x); uint pmask = 0xFFu; uint pshift = 0u;\n"
    "  if ((cmode & 4) != 0) { pbase += 0xECu + uint((cmode & 8) << 1); pmask = 3u; pshift = uint(2 * (~cmode & 3)); }\n"
    "  else if ((cmode & 2) != 0) { pbase += 0xE0u + uint((cmode & 8) << 1); pmask = 15u; pshift = uint(4 * (~cmode & 1)); }\n"
    "  uint id = ((tx >> 4u) & 0xFFu) | ((ty << 4u) & 0x1FFF00u);\n"   /* the tile map: tile number and its flips */
    "  vec4 m = texelFetch(t_map, ivec2(int(id & 2047u), int(id >> 11u)), 0);\n"
    "  uint mb = b8(m, 2); uint tile = b8(m, 0) | (b8(m, 1) << 8u) | ((mb & 1u) << 16u); uint a = mb >> 1u;\n"
    "  if ((a & 1u) != 0u) ty = ~ty;\n"
    "  if ((a & 2u) != 0u) tx = ~tx;\n"
    "  if ((a & 4u) != 0u) { uint t = tx; tx = ty; ty = t; }\n"
    "  uint ri = ((tile << 8u) | ((ty << 4u) & 0xF0u) | (tx & 0x0Fu)) & 0x1FFFFFFu;\n"
    "  uint pen = b8(texelFetch(t_rom, ivec2(int(ri & 8191u), int(ri >> 13u)), 0), 0);\n"
    "  if (u_prio != 0) {\n"                                               /* the priority target: R text-or-7, G any polygon, A last was 7 */
    "    int pv = (fl >> 3) & 7;\n"
    "    frag = vec4(pv == 7 ? 1.0 : 0.0, pv != 0 ? 1.0 : 0.0, 0.0, pv == 7 ? 1.0 : 0.0); return;\n"
    "  }\n"
    "  uint ci = pbase + ((pen >> pshift) & pmask);\n"
    "  vec4 c = texelFetch(t_pal, ivec2(int(ci & 255u), int((ci >> 8u) & 127u)), 0);\n"
    "  ivec3 col = ivec3(c.rgb * 255.0 + 0.5);\n"
    "  int sh = clamp(int(v_uvs.z), 0, 63);\n"
    "  col = (col * sh) >> 6;\n"
    "  if (u_pfade != 0) col = (col * u_pcol) >> 8;\n"
    "  if (u_ff != 255) col = (col * u_ff + u_fcol * (256 - u_ff)) >> 8;\n"
    "  bool ua = u_alpha != 255 && ((fl & 4) != 0 || int(pen) == u_alpha_pen);\n"
    "  float al = ua ? float(u_alpha) / 256.0 : ((fl & 2) != 0 ? 0.5 : 1.0);\n"
    "  frag = vec4(vec3(col) / 255.0, al);\n"
    "}\n";
static const char *vs_text =
    "#version 300 es\nprecision highp float;precision highp int;\n"
    "in vec4 a_pos;uniform mat4 u_view,u_proj;uniform vec3 u_cam;uniform int u_flat;out vec2 v_screen;\n"
    "void main(){v_screen=vec2((a_pos.x+1.0)*320.0,(1.0-a_pos.y)*240.0);vec3 p=vec3((v_screen.x-u_cam.x)*2.5/u_cam.z,(u_cam.y-v_screen.y)*2.5/u_cam.z,-2.5);gl_Position=u_flat!=0?a_pos:u_proj*u_view*vec4(p,1); }\n";
static const char *vs_screen =
    "#version 300 es\nprecision highp float;in vec4 a_pos;void main(){gl_Position=a_pos;}\n";
static const char *fs_text =
    "#version 300 es\nprecision highp float;precision highp int;out vec4 frag;\n"
    "in vec2 v_screen;uniform highp sampler2D t_mix, t_pal, t_prio, t_band;\n"
    "uniform int u_ex, u_scale, u_h, u_pass, u_fade, u_ff, u_alpha, u_amask, u_c12, u_c13;\n"
    "uniform ivec3 u_fcol;\n"
    "void main() {\n"
    "  ivec2 px = ivec2(v_screen);\n"
    "  if(px.x<0||px.x>=640||px.y<0||px.y>=480)discard;uint mv;\n"
    "  if (px.x < 0 || px.x >= 640) {\n"      /* widescreen: a text row opaque all the way across (the cut-scenes' letterbox) continues
                                                   into the side strips in its commonest pen (b->band); anything else stays in the 4:3 centre */
    "    vec4 a = texelFetch(t_band, ivec2(px.y, 0), 0);\n"
    "    mv = uint(a.r * 255.0 + 0.5) | (uint(a.g * 255.0 + 0.5) << 8u);\n"
    "  } else {\n"
    "    vec4 m = texelFetch(t_mix, ivec2(px.x, px.y), 0);\n"
    "    mv = uint(m.r * 255.0 + 0.5) | (uint(m.g * 255.0 + 0.5) << 8u);\n"
    "  }\n"
    "  if ((mv & 0x8000u) == 0u) discard;\n"
    "  if (u_pass == 0) { frag = vec4(1.0, 0.0, 0.0, 0.0); return; }\n"       /* the priority target starts as 'text here' */
    "  if (u_pass == 2) { vec4 p = texelFetch(t_prio, ivec2(gl_FragCoord.xy), 0); if (!(p.r > 0.5 && p.g > 0.5 && p.a < 0.5)) discard; }\n"
    "  uint pen = mv & 0x7FFFu;\n"
    "  ivec3 col = ivec3(texelFetch(t_pal, ivec2(int(pen & 255u), int(pen >> 8u)), 0).rgb * 255.0 + 0.5);\n"
    "  if (u_fade != 0) col = (col * u_ff + u_fcol * (256 - u_ff)) >> 8;\n"
    "  bool ua = u_alpha != 255 && (int(pen & 15u) == u_amask || (int(pen) >= u_c12 && int(pen) <= u_c13));\n"
    "  frag = vec4(vec3(col) / 255.0, ua ? float(u_alpha) / 256.0 : 1.0);\n"
    "}\n";

static const char *fs_gamma =
    "#version 300 es\nprecision highp float;precision highp int;out vec4 frag;\n"
    "uniform highp sampler2D t_src, t_lut;\n"
    "void main() {\n"
    "  vec3 c = texelFetch(t_src, ivec2(gl_FragCoord.xy), 0).rgb;\n"
    "  ivec3 i = ivec3(c * 255.0 + 0.5);\n"
    "  frag = vec4(texelFetch(t_lut, ivec2(i.r, 0), 0).r, texelFetch(t_lut, ivec2(i.g, 0), 0).g, texelFetch(t_lut, ivec2(i.b, 0), 0).b, 1.0);\n"
    "}\n";
static int ok, scale = 1, ex, fbw, fbh;                  /* ex: widescreen margin each side, in board pixels (0 = 4:3) */
static GLuint vao;
static int requested_w=640,requested_h=480;
static int capture_w,capture_h;
static unsigned output_framebuffer;
static float vr_view[16],vr_projection[16];
static const tc2_built *uploaded;
static int vertex_count;
static GLuint prog_poly, prog_text, prog_gamma, vbo, quad_vbo, t_out, fbo_out, t_lut;
static GLint ug[2];
static GLuint t_band, t_rom, t_map, t_pal, t_sten, t_mix, t_col, t_prio, fbo_col, fbo_prio;
static GLint up[20], ut[16];
static GLuint t_near, t_nearz, fbo_near;          /* the exact-depth pre-pass target: nearest object / band (colour), its 1/z (depth) */
enum { A_POS, A_UVS, A_PAR, A_CLIP, A_EXT };
#define VFLOATS 17
static float *verts; static size_t vcap;

static GLuint shader(GLenum kind, const char *src)
{
    const GLuint s = glCreateShader(kind);
    qgpu_shader_source(s,src); glCompileShader(s);
    GLint good = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &good);
    if (!good) { char log[2048]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "[GPU] shader: %s\n", log); return 0; }
    return s;
}
static GLuint program(const char *vs, const char *fs)
{
    const GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    const GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, A_POS, "a_pos"); glBindAttribLocation(p, A_UVS, "a_uvs");
    glBindAttribLocation(p, A_PAR, "a_par"); glBindAttribLocation(p, A_CLIP, "a_clip"); glBindAttribLocation(p, A_EXT, "a_ext");
    glLinkProgram(p);
    GLint good = 0; glGetProgramiv(p, GL_LINK_STATUS, &good);
    if (!good) { char log[2048]; glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "[GPU] link: %s\n", log); return 0; }
    return p;
}
static GLuint texture(GLint ifmt, int w, int h, GLenum fmt, const void *data)
{
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    return t;
}
static int targets(void)
{
    fbw=requested_w;fbh=requested_h;
    if (!t_col) { glGenTextures(1, &t_col); glGenTextures(1, &t_prio);
                  glGenFramebuffers(1, &fbo_col); glGenFramebuffers(1, &fbo_prio); }
    const GLuint tt[2] = { t_col, t_prio }, ff[2] = { fbo_col, fbo_prio };
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, tt[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbw, fbh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, ff[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tt[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "[GPU] render target %dx%d incomplete\n", fbw, fbh); glBindFramebuffer(GL_FRAMEBUFFER, 0); return 0; }
    }
    if (!t_near) { glGenTextures(1, &t_near); glGenTextures(1, &t_nearz); glGenFramebuffers(1, &fbo_near); }
    glBindTexture(GL_TEXTURE_2D, t_near);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbw, fbh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, t_nearz);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, 0x884C /* GL_TEXTURE_COMPARE_MODE */, 0 /* GL_NONE: sample the value */);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x8CAC /* GL_DEPTH_COMPONENT32F */, fbw, fbh, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_near);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t_near, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, 0x8D00 /* GL_DEPTH_ATTACHMENT */, GL_TEXTURE_2D, t_nearz, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "[GPU] exact-depth target incomplete: depth off\n"); fbo_near = 0; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

int tc2_gl_ok(void) { return ok; }
int tc2_gl_scale(void) { return scale; }
int tc2_gl_wide(void) { return ex; }
/* widescreen: the picture is (640 + 2 * e) x 480 board pixels; e = 0 is the board's 4:3 */
void tc2_gl_set_wide(int e)
{
    if (e < 0) e = 0;
    if (e > 400) e = 400;
    if (e == ex) return;
    ex = e;
    if (ok && !targets()) { ex = 0; targets(); }
    fprintf(stderr, "[GPU] picture %d x %d board pixels (%s)\n", 640 + 2 * ex, 480, ex ? "widescreen" : "4:3");
}
void tc2_gl_set_scale(int s)
{
    if (s < 1) s = 1;
    if (s > 4) s = 4;
    if (s == scale) return;
    scale = s;
    if (ok && !targets()) { scale = 1; targets(); }
    fprintf(stderr, "[GPU] internal resolution %d x %d\n", fbw, fbh);
}

int tc2_gl_init(void)
{
    ok = 0;
    const char *e = getenv("TC2_GPU");
    if (e && *e == '0') { fprintf(stderr, "[GPU] off (TC2_GPU=0): the software picture\n"); return 0; }
    const char *sl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int maj = 0, min = 0; if (sl) sscanf(sl, "%d.%d", &maj, &min);
    GLint maxtex = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
    if (maxtex < 8192) {
        fprintf(stderr, "[GPU] requires 8192-texel textures (have GLSL %s, %d)\n", sl ? sl : "?", maxtex);
        return 0;
    }
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    if (!(prog_poly = program(vs_poly, fs_poly)) || !(prog_text = program(vs_text, fs_text)) || !(prog_gamma = program(vs_screen, fs_gamma))) return 0;
    ug[0] = glGetUniformLocation(prog_gamma, "t_src"); ug[1] = glGetUniformLocation(prog_gamma, "t_lut");
    static const char *pn[] = { "t_rom", "t_map", "t_pal", "t_sten", "u_scale", "u_h", "u_prio", "u_pfade", "u_ff", "u_alpha", "u_alpha_pen", "u_pcol", "u_fcol", "u_ex",
                                "t_near", "t_nearz", "u_near", "u_depth", "u_keep" };
    for (int i = 0; i < 19; i++) up[i] = glGetUniformLocation(prog_poly, pn[i]);
    static const char *tn[] = { "t_mix", "t_pal", "t_prio", "u_scale", "u_h", "u_pass", "u_fade", "u_ff", "u_alpha", "u_amask", "u_c12", "u_c13", "u_fcol", "u_ex", "t_band" };
    for (int i = 0; i < 15; i++) ut[i] = glGetUniformLocation(prog_text, tn[i]);

    const uint8_t *texrom; const uint32_t *tm; const uint8_t *tattr;
    tc2_render_assets(&texrom, &tm, &tattr);
    if (!texrom) { fprintf(stderr, "[GPU] no ROM data\n"); return 0; }
    t_rom = texture(GL_R8, 8192, 4096, GL_RED, texrom);
    uint8_t *map = malloc(0x200000u * 3);
    for (uint32_t id = 0; id < 0x200000u; id++) {
        const uint32_t tile = tm[id] >> 8;
        map[3 * id] = (uint8_t)tile; map[3 * id + 1] = (uint8_t)(tile >> 8); map[3 * id + 2] = (uint8_t)((tile >> 16 & 1) | tattr[id] << 1);
    }
    t_map = texture(GL_RGB8, 2048, 1024, GL_RGB, map);
    free(map);
    t_pal = texture(GL_RGBA8, 256, 128, GL_RGBA, NULL);
    t_sten = texture(GL_RG8, 512, 256, GL_RG, NULL);
    t_mix = texture(GL_RG8, 640, 480, GL_RG, NULL);
    t_band = texture(GL_RG8, 480, 1, GL_RG, NULL);
    t_lut = texture(GL_RGB8, 256, 1, GL_RGB, NULL);
    glGenBuffers(1, &vbo); glGenBuffers(1, &quad_vbo);
    static const float quad[] = { -1, -1, 0, 1,  1, -1, 0, 1,  1, 1, 0, 1,  -1, -1, 0, 1,  1, 1, 0, 1,  -1, 1, 0, 1 };
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo); glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    { const char *s = getenv("TC2_RES"); if (s) scale = atoi(s) < 1 ? 1 : atoi(s) > 4 ? 4 : atoi(s); }
    { const char *s = getenv("TC2_WIDE"); if (s) { const float a = (float)atof(s); ex = a > 1.34f ? (int)(480 * a / 2 + 0.5f) - 320 : 0; } }   /* tests: TC2_WIDE=1.7778 */
    if (!targets()) return 0;
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { fprintf(stderr, "[GPU] GL error %04X during setup: the software picture\n", err); return 0; }
    ok = 1;
    fprintf(stderr, "[GPU] the picture is drawn on the GPU (%s, GLSL %s), internal resolution %d x %d\n", (const char *)glGetString(GL_RENDERER), sl, fbw, fbh);
    return 1;
}

/* the frame's polygons as triangles, in draw order: per vertex its clip position (x*w, y*w, 0, w with w = z, so GL interpolates u, v and
 * the shade perspective-correct exactly as the hardware's u/z, v/z, i/z), u v shade, and the polygon's parameters and viewport clip */
static int fill_vertices(const tc2_built *b)
{
    size_t need = 0;
    for (int i = 0; i < b->npoly; i++) need += (size_t)(b->polys[i].n - 2) * 3;
    if (need * VFLOATS > vcap) { vcap = need * VFLOATS * 2 + 4096; verts = realloc(verts, vcap * sizeof *verts); }
    float *o = verts;
    for (int i = 0; i < b->npoly; i++) {
        const tc2_poly *p = &b->polys[b->order[i]];
        const int cl = (320 - p->vp_size_x) + p->vp_offset_x, cr = (320 + p->vp_size_x) + p->vp_offset_x;
        const int ct = (240 - p->vp_size_y) - p->vp_offset_y, cb = (240 + p->vp_size_y) - p->vp_offset_y;
        /* widescreen (Hor+): a viewport that fills the 4:3 screen fills the wide one -- same camera, more world at the sides */
        const int full = ex && cl <= 0 && cr >= 640;
        const int ccl = full ? -ex : cl, ccr = full ? 640 + ex : cr;
        const float flags = (float)(p->stencil | p->blend << 1 | p->alpha_enabled << 2 | (p->prioverchar & 7) << 3);
        for (int k = 1; k + 1 < p->n; k++) {
            const int tri[3] = { 0, k, k + 1 };
            for (int j = 0; j < 3; j++) {
                const tc2_pvert *v = &p->pv[tri[j]];
                const float ooz = v->p[0] != 0 ? v->p[0] : 1e-30f, w = 1.0f / ooz;
                const float sx = v->x + (float)cl, sy = v->y + (float)ct;
                if(qvr_flat_view){*o++=(sx/320.f-1)*w;*o++=(sy/240.f-1)*w;*o++=0;*o++=w;}
                else {V3 pos=qvr_tc2_vertex(b,p,tri[j]);*o++=pos.x;*o++=pos.y;*o++=pos.z;*o++=1;}
                *o++ = v->p[1] * w; *o++ = v->p[2] * w; *o++ = v->p[3] * w;
                *o++ = (float)p->color_base; *o++ = (float)p->cmode; *o++ = (float)p->tbase; *o++ = flags;
                *o++=sx;*o++=sy;*o++=0;*o++=0;
                *o++ = (float)((p->entry & 0x3FFF) + 1); *o++ = (float)(p->zkey >> 21);
            }
        }
    }
    return (int)need;
}
int tc2_depth_on(void); float tc2_depth_keep(void);   /* src/tc2_render.c: the exact-depth setting */
static void bind_tex(int unit, GLuint t) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); }
static void camera_uniforms(GLuint p){
    glUniformMatrix4fv(glGetUniformLocation(p,"u_view"),1,GL_FALSE,vr_view);
    glUniformMatrix4fv(glGetUniformLocation(p,"u_proj"),1,GL_FALSE,vr_projection);
    glUniform1i(glGetUniformLocation(p,"u_flat"),qvr_flat_view);
    float cx,cy,f; qvr_flat_camera(&cx,&cy,&f);glUniform3f(glGetUniformLocation(p,"u_cam"),cx,cy,f);
}
static void draw_quad(void)
{
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(A_POS); glVertexAttribPointer(A_POS, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(A_POS);
}
static void text_pass(const tc2_built *b, int pass)
{
    const tc2_frame_state *fs = &b->fs;
#define C4(w) (fs->c404[2 * (w) + 1])
    glUseProgram(prog_text);camera_uniforms(prog_text);
    glUniform1i(ut[0], 0); glUniform1i(ut[1], 1); glUniform1i(ut[2], 2);
    glUniform1i(ut[3], scale); glUniform1i(ut[4], fbh); glUniform1i(ut[5], pass);
    glUniform1i(ut[6], (C4(0x1A) & 2) && C4(0x19)); glUniform1i(ut[7], 0xFF - C4(0x19));
    glUniform1i(ut[8], 0xFF - C4(0x15)); glUniform1i(ut[9], C4(0x14)); glUniform1i(ut[10], C4(0x12)); glUniform1i(ut[11], C4(0x13));
    glUniform3i(ut[12], C4(0x16), C4(0x17), C4(0x18)); glUniform1i(ut[13], ex); glUniform1i(ut[14], 3);
#undef C4
    bind_tex(0, t_mix); bind_tex(1, t_pal); bind_tex(2, t_prio); bind_tex(3, t_band);
    draw_quad();
}

unsigned tc2_gl_draw(const tc2_built *b)
{
    if (!ok || !b) return t_out;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    glBindVertexArray(vao);
    const tc2_fx *f = &b->fx;
    if(uploaded!=b){
    uploaded=b;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    static uint32_t palette[32768];
    for(int i=0;i<32768;i++){uint32_t c=b->fs.pens[i];palette[i]=0xff000000u|((c&255)<<16)|(c&0xff00)|((c>>16)&255);}
    bind_tex(0, t_pal); glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,128,GL_RGBA,GL_UNSIGNED_BYTE,palette);
    bind_tex(0, t_sten); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 256, GL_RG, GL_UNSIGNED_BYTE, b->fs.sram);
    if (f->layers & 4) {
        bind_tex(0, t_mix); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480, GL_RG, GL_UNSIGNED_BYTE, b->mix);
        bind_tex(0, t_band); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 480, 1, GL_RG, GL_UNSIGNED_BYTE, b->band);
    }
    vertex_count=fill_vertices(b);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertex_count*VFLOATS*sizeof(float)),verts,GL_STREAM_DRAW);
    }
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); 
    glViewport(0, 0, fbw, fbh);
    /* 1. the background */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_col);
    glClearColor((f->bg >> 16 & 0xFF) / 255.0f, (f->bg >> 8 & 0xFF) / 255.0f, (f->bg & 0xFF) / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_prio);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    /* 2. the text layer, under everything (and 'text here' into the priority target) */
    if (f->layers & 4) {
        glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ZERO);
        text_pass(b, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_col);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        text_pass(b, 1);
    }
    /* 3. + 4. the polygons, one draw call each into the picture and into the priority target */
    const int nv=vertex_count;
    if (nv > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        const GLsizei st = VFLOATS * sizeof(float);
        glEnableVertexAttribArray(A_POS); glVertexAttribPointer(A_POS, 4, GL_FLOAT, GL_FALSE, st, (void *)0);
        glEnableVertexAttribArray(A_UVS); glVertexAttribPointer(A_UVS, 3, GL_FLOAT, GL_FALSE, st, (void *)(4 * sizeof(float)));
        glEnableVertexAttribArray(A_PAR); glVertexAttribPointer(A_PAR, 4, GL_FLOAT, GL_FALSE, st, (void *)(7 * sizeof(float)));
        glEnableVertexAttribArray(A_CLIP); glVertexAttribPointer(A_CLIP, 4, GL_FLOAT, GL_FALSE, st, (void *)(11 * sizeof(float)));
        glEnableVertexAttribArray(A_EXT); glVertexAttribPointer(A_EXT, 2, GL_FLOAT, GL_FALSE, st, (void *)(15 * sizeof(float)));
        glUseProgram(prog_poly);camera_uniforms(prog_poly);
        glUniform1i(up[0], 0); glUniform1i(up[1], 1); glUniform1i(up[2], 2); glUniform1i(up[3], 3);
        glUniform1i(up[4], scale); glUniform1i(up[5], fbh);
        glUniform1i(up[7], f->pr || f->pg || f->pb); glUniform1i(up[8], (f->fflags & 1) ? 0xFF - f->ffactor : 0xFF);
        glUniform1i(up[9], 0xFF - f->alpha); glUniform1i(up[10], f->alpha_pen);
        glUniform3i(up[11], f->pr, f->pg, f->pb); glUniform3i(up[12], f->fr, f->fg, f->fb); glUniform1i(up[13], ex);
        bind_tex(0, t_rom); bind_tex(1, t_map); bind_tex(2, t_pal); bind_tex(3, t_sten);
        glUniform1i(up[14], 4); glUniform1i(up[15], 5); glUniform1i(up[16], 0);
        const int depth = tc2_depth_on() && fbo_near;
        glUniform1i(up[17], 0); glUniform1f(up[18], tc2_depth_keep());
        if (depth) {                                     /* EXACT DEPTH pre-pass: nearest opaque object / band / 1/z per pixel */
            bind_tex(4, 0); bind_tex(5, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_near);
            glClearColor(0, 0, 0, 0); glClearDepthf(0.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GREATER); glDepthMask(GL_TRUE);
            glUniform1i(up[6], 0); glUniform1i(up[16], 1);
            glDrawArrays(GL_TRIANGLES, 0, nv);
            glDisable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClearDepthf(1.0); glEnable(GL_BLEND);
            glUniform1i(up[16], 0); glUniform1i(up[17], 1);
            bind_tex(4, t_near); bind_tex(5, t_nearz);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_col);
        glUniform1i(up[6], 0);
        glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, nv);
        if (f->layers & 4) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_prio);
            glUniform1i(up[6], 1);
            glBlendEquationSeparate(GL_MAX, GL_FUNC_ADD); glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
            glDrawArrays(GL_TRIANGLES, 0, nv);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        }
        for (int a = A_POS; a <= A_EXT; a++) glDisableVertexAttribArray((GLuint)a);
        bind_tex(4, 0); bind_tex(5, 0); glActiveTexture(GL_TEXTURE0);
    }
    /* 5. the text the polygons let through again */
    if (f->layers & 4) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_col);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        text_pass(b, 2);
    }
    /* 6. the C404's GAMMA (the game's own R / G / B curves), into the picture the window shows */
    {
        uint8_t lut[256 * 3];
        for (int i = 0; i < 256; i++) { lut[3 * i] = b->fs.gamma[0][i]; lut[3 * i + 1] = b->fs.gamma[1][i]; lut[3 * i + 2] = b->fs.gamma[2][i]; }
        bind_tex(0, t_lut); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, lut);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
        glUseProgram(prog_gamma); glUniform1i(ug[0], 0); glUniform1i(ug[1], 1);
        bind_tex(0, t_col); bind_tex(1, t_lut);
        draw_quad();
    }
    /* back to the state the window and the menu expect */
    glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
    glClearDepthf(1);glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    for (int u = 3; u >= 0; u--) bind_tex(u, 0);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glClearColor(0, 0, 0, 1);
    return t_out;
}

/* Readback uses a private target: an OpenXR eye image may already have been
 * released to the runtime. Reapply gamma to the retained colour texture only
 * when diagnostics request pixels. Return top-row-first images on both APIs. */
static uint8_t *readback_pixels(void) {
    if(!ok||!uploaded)return NULL;
    static uint8_t *pixels;static size_t capacity;
    size_t bytes=(size_t)fbw*fbh*4;
    if(bytes>capacity){uint8_t *next=realloc(pixels,bytes);if(!next)return NULL;pixels=next;capacity=bytes;}
    GLint previous,viewport[4],pack;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
    if(!fbo_out){glGenTextures(1,&t_out);glGenFramebuffers(1,&fbo_out);}
    if(capture_w!=fbw||capture_h!=fbh){
        bind_tex(0,t_out);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,fbw,fbh,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo_out);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t_out,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous);return NULL;}
        capture_w=fbw;capture_h=fbh;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_out);glViewport(0,0,fbw,fbh);glBindVertexArray(vao);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glUseProgram(prog_gamma);glUniform1i(ug[0],0);glUniform1i(ug[1],1);bind_tex(0,t_col);bind_tex(1,t_lut);draw_quad();
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,fbw,fbh,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glPixelStorei(GL_PACK_ALIGNMENT,pack);
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glUseProgram(0);glBindBuffer(GL_ARRAY_BUFFER,0);bind_tex(1,0);bind_tex(0,0);
    return pixels;
}
void tc2_gl_read(uint32_t *rgba) {
    const uint8_t *pixels=readback_pixels();
    if(!pixels){memset(rgba,0,640*480*4);return;}
    for(int y=0;y<480;y++)for(int x=0;x<640;x++){
        int sx=(int)(((int64_t)x*2+1)*fbw/1280),sy=fbh-1-(int)(((int64_t)y*2+1)*fbh/960);
        const uint8_t *c=pixels+4*((size_t)sy*fbw+sx);
        rgba[y*640+x]=(uint32_t)c[0]<<16|(uint32_t)c[1]<<8|c[2];
    }
}
int tc2_gl_read_full(uint8_t *rgb,int cap) {
    if(cap<0||(size_t)cap<(size_t)fbw*fbh*3)return 0;
    const uint8_t *pixels=readback_pixels();if(!pixels)return 0;
    for(int y=0;y<fbh;y++)for(int x=0;x<fbw;x++){
        const uint8_t *c=pixels+4*((size_t)(fbh-1-y)*fbw+x);
        memcpy(rgb+3*((size_t)y*fbw+x),c,3);
    }
    return fbw;
}

void qtc2_draw(const float view[16],const float proj[16],int w,int h,unsigned framebuffer){
    memcpy(vr_view,view,sizeof vr_view);memcpy(vr_projection,proj,sizeof vr_projection);output_framebuffer=framebuffer;
    if(w!=requested_w||h!=requested_h){requested_w=w;requested_h=h;targets();}
    const tc2_built *b=qvr_current_frame();
    if(b)tc2_gl_draw(b);else {glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,w,h);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
}
