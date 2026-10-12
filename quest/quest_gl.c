#define QGL_IMPLEMENTATION
#include <GL/gl.h>
#include "quest_gl.h"

#include "quest_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct { float p[4],c[4],uv[4]; } Vertex;
static GLuint program,vao,vbo,post_program,post_tex,post_copy_fb,lut_tex;
static GLuint atlas_texture,atlas_ids[28];
static int atlas_dimension,atlas_pages;
static bool atlas_unavailable;
enum { U_VIEW,U_PROJ,U_TEX,U_TEXTURED,U_ALPHA,U_FOG,U_REPLACE,U_SCALE,U_THRESHOLD,U_FOG_COLOR,U_FLAT_CAMERA,U_COUNT };
static float flat_camera[4]={320,240,.005f,2.5f};
static GLint uniforms[U_COUNT],post_screen,post_lut;
static GLuint mono_program,multi_program,multi_post,multi_texture,multi_fb,multi_lut;
static GLint mono_uniforms[U_COUNT],multi_uniforms[U_COUNT];
static GLint multi_post_uniforms[4];
static bool multi_active,multi_gamma,multi_lut_valid;
static int multi_w,multi_h;
static uint8_t multi_gamma_table[3][256],multi_uploaded_lut[3][256];
typedef void (GL_APIENTRY *MultiviewProc)(GLenum,GLenum,GLuint,GLint,GLint,GLsizei);
static MultiviewProc framebuffer_multiview;
static const char *world_vertex=
    "#version 300 es\nprecision highp float;layout(location=0) in vec4 aPos;layout(location=1) in vec4 aColor;layout(location=2) in vec4 aUV;uniform mat4 view[2],proj[2];uniform vec4 flatCam;\n#ifdef QGL_MULTIVIEW\nlayout(num_views=2) in;\n#define EYE gl_ViewID_OVR\n#else\n#define EYE 0\n#endif\nout vec4 c;out vec3 uv;flat out float layer;void main(){vec3 p;if(aPos.w>0.5){p=aPos.xyz;uv=vec3(aUV.xy/max(aUV.w,1e-12),1);}else{p=vec3((aPos.x-flatCam.x)*flatCam.z,(flatCam.y-aPos.y)*flatCam.z,-flatCam.w);uv=vec3(aUV.xy,aUV.w);}gl_Position=proj[EYE]*view[EYE]*vec4(p,1);c=aColor;layer=aUV.z;}";
static const char *world_fragment=
    "#version 300 es\nprecision highp float;in vec4 c;in vec3 uv;flat in float layer;uniform sampler2D tex;uniform highp sampler2DArray atlasTex;uniform int textured,alphaTest,fog,replaceAlpha;uniform float scale,threshold;uniform vec3 fogColor;out vec4 frag;void main(){vec2 st=uv.xy/max(uv.z,1e-12);vec4 t=textured==2?texture(atlasTex,vec3(st,layer)):(textured!=0?texture(tex,st):vec4(1));vec4 v=t*c;v.rgb*=textured!=0?scale:1.0;if(replaceAlpha!=0)v.a=t.a;if(fog!=0){v.rgb=mix(fogColor,v.rgb,c.a);v.a=t.a;}if(alphaTest!=0&&v.a<=threshold)discard;frag=v;}";
static const char *uniform_names[]={"view","proj","tex","textured","alphaTest","fog","replaceAlpha","scale","threshold","fogColor","flatCam"};
/* Upload each ordered group before any draw reads its vertex buffer. Updating a
 * shared buffer between draws serializes the Adreno driver. Texture writes are
 * barriers because the original atlas and sprite texture can be reused. */
typedef struct {
    GLuint texture;GLenum mode,src,dst;
    int textured,alpha,fog,replace,blend;
    GLboolean mask[4];float scale,threshold,fog_color[3];
} QDrawState;
typedef struct { QDrawState state;int first,count; } Command;
static Command *commands;static size_t command_count,command_capacity;
static Vertex *stream;static size_t stream_count,stream_capacity;
static GLuint bindings[2];static int blend_on;
static GLenum blend_src=GL_ONE,blend_dst=GL_ZERO;
static GLboolean color_mask[4]={1,1,1,1};
static int post_w,post_h;
static uint8_t cached_lut[3][256];
static bool lut_valid,lut_identity;
static struct { GLuint texture,framebuffer;int w,h; } scene_targets[2];
static int target_eye=-1;
static GLuint target_framebuffer;
static bool scene_active;
static float eye_view[16],eye_proj[16];
static Vertex *vertices;static size_t capacity;
static struct { const float *p;int size,stride; } arrays[3];
static int active,texture_on[2],alpha_on,alpha_replace,im_count;
static float rgb_scale=1,alpha_ref=.1f,fog_color[3],color[4]={1,1,1,1},uv[4]={0,0,0,1};
static Vertex immediate[4096];static GLenum im_mode;
static GLuint shader(GLenum type,const char *source) {
    GLuint s=glCreateShader(type);qgpu_shader_source(s,source);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char msg[4096];glGetShaderInfoLog(s,sizeof msg,NULL,msg);__android_log_print(ANDROID_LOG_ERROR,"TCVR","Shader: %s",msg);glDeleteShader(s);return 0;}return s;
}
static GLuint link_program(const char *vs,const char *fs) {
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);if(!v||!f)return 0;
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);return 0;}return p;
}
bool qgl_init(void) {
    lut_valid=false;post_w=post_h=0;
    program=mono_program=link_program(world_vertex,world_fragment);
    post_program=link_program(
        "#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}",
        "#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D screenTex,lut;out vec4 frag;void main(){vec4 c=texture(screenTex,uv);ivec3 i=ivec3(clamp(c.rgb,0.0,1.0)*255.0+0.5);frag=vec4(texelFetch(lut,ivec2(i.r,0),0).r,texelFetch(lut,ivec2(i.g,0),0).g,texelFetch(lut,ivec2(i.b,0),0).b,c.a);}");
    for(int i=0;i<U_COUNT;i++)uniforms[i]=mono_uniforms[i]=glGetUniformLocation(program,uniform_names[i]);
    post_screen=glGetUniformLocation(post_program,"screenTex");post_lut=glGetUniformLocation(post_program,"lut");
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
    glBindVertexArray(vao);for(int i=0;i<3;i++)glEnableVertexAttribArray(i);
    glGenTextures(1,&post_tex);glGenTextures(1,&lut_tex);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"atlasTex"),2);
    return program && post_program;
}
bool qgl_stereo_init(void *proc){
    GLint n=0;bool supported=false;glGetIntegerv(GL_NUM_EXTENSIONS,&n);
    for(int i=0;i<n;i++)if(!strcmp((const char*)glGetStringi(GL_EXTENSIONS,i),"GL_OVR_multiview2"))supported=true;
    if(!supported||!proc)return false;
    framebuffer_multiview=(MultiviewProc)proc;
    const char *prefix="#" "version 300 es\n#extension GL_OVR_multiview2 : require\n#define QGL_MULTIVIEW\n";
    const char *body=strchr(world_vertex,'\n')+1;char *source=malloc(strlen(prefix)+strlen(body)+1);if(!source)return false;
    strcpy(source,prefix);strcat(source,body);multi_program=link_program(source,world_fragment);free(source);
    multi_post=link_program(
        "#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}",
        "#version 300 es\nprecision highp float;in vec2 uv;uniform highp sampler2DArray stereoTex;uniform sampler2D lut;uniform int eye,gammaOn;out vec4 frag;void main(){vec4 c=texture(stereoTex,vec3(uv,float(eye)));if(gammaOn!=0){ivec3 i=ivec3(clamp(c.rgb,0.0,1.0)*255.0+0.5);c.rgb=vec3(texelFetch(lut,ivec2(i.r,0),0).r,texelFetch(lut,ivec2(i.g,0),0).g,texelFetch(lut,ivec2(i.b,0),0).b);}frag=c;}");
    if(!multi_program||!multi_post)return false;
    const char *names[]={"stereoTex","lut","eye","gammaOn"};for(int i=0;i<4;i++)multi_post_uniforms[i]=glGetUniformLocation(multi_post,names[i]);
    for(int i=0;i<U_COUNT;i++)multi_uniforms[i]=glGetUniformLocation(multi_program,uniform_names[i]);
    glUseProgram(multi_program);glUniform1i(glGetUniformLocation(multi_program,"atlasTex"),2);glUniform1i(multi_uniforms[U_TEX],0);
    glGenTextures(1,&multi_texture);glGenTextures(1,&multi_lut);glGenFramebuffers(1,&multi_fb);
    fprintf(stderr,"[GL] multiview world enabled; one scene traversal for both eyes\n");return true;
}
bool qgl_stereo_begin(const float v[32],const float p[32],int w,int h){
    if(!multi_program||!multi_post||!framebuffer_multiview)return false;
    qgl_flush();
    if(multi_w!=w||multi_h!=h){
        glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D_ARRAY,multi_texture);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,w,h,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);glBindFramebuffer(GL_FRAMEBUFFER,multi_fb);
        framebuffer_multiview(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,multi_texture,0,0,2);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR){
            fprintf(stderr,"[GL] multiview target unavailable; using separate eye passes\n");framebuffer_multiview=NULL;return false;
        }
        multi_w=w;multi_h=h;
    }
    program=multi_program;memcpy(uniforms,multi_uniforms,sizeof uniforms);multi_active=true;multi_gamma=false;
    glBindFramebuffer(GL_FRAMEBUFFER,multi_fb);glUseProgram(program);
    glUniformMatrix4fv(uniforms[U_VIEW],2,GL_FALSE,v);glUniformMatrix4fv(uniforms[U_PROJ],2,GL_FALSE,p);
    return true;
}
void qgl_stereo_end(void){
    qgl_flush();multi_active=false;program=mono_program;memcpy(uniforms,mono_uniforms,sizeof uniforms);
}
void qgl_stereo_blit(int eye){
    qgl_flush();glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D_ARRAY,multi_texture);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,multi_lut);
    if(multi_gamma&&(!multi_lut_valid||memcmp(multi_uploaded_lut,multi_gamma_table,sizeof multi_gamma_table))){
        uint8_t rgb[768];for(int i=0;i<256;i++)for(int j=0;j<3;j++)rgb[i*3+j]=multi_gamma_table[j][i];
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,256,1,0,GL_RGB,GL_UNSIGNED_BYTE,rgb);
        memcpy(multi_uploaded_lut,multi_gamma_table,sizeof multi_gamma_table);multi_lut_valid=true;
    }
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glColorMask(1,1,1,1);
    glUseProgram(multi_post);glUniform1i(multi_post_uniforms[0],3);glUniform1i(multi_post_uniforms[1],1);
    glUniform1i(multi_post_uniforms[2],eye);glUniform1i(multi_post_uniforms[3],multi_gamma);
    glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindTexture(GL_TEXTURE_2D,bindings[1]);glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);
}
static int atlas_layer(GLuint texture){
    if(texture)for(int i=0;i<atlas_pages;i++)if(atlas_ids[i]==texture)return i;
    return -1;
}
void qgl_atlas_texture(unsigned texture,int page,int dimension,int pages){
    if(atlas_unavailable||page<0||page>=pages||pages>28||dimension<=0)return;
    qgl_flush();
    if(!atlas_texture){
        glActiveTexture(GL_TEXTURE2);glGenTextures(1,&atlas_texture);glBindTexture(GL_TEXTURE_2D_ARRAY,atlas_texture);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY,1,GL_RGBA8,dimension,dimension,pages);
        GLenum error=glGetError();
        if(error){
            __android_log_print(ANDROID_LOG_ERROR,"TCVR","Atlas array unavailable (0x%x); keeping separate textures",error);
            glDeleteTextures(1,&atlas_texture);atlas_texture=0;atlas_unavailable=true;
            glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);return;
        }
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);
        atlas_dimension=dimension;atlas_pages=pages;
    }
    if(dimension==atlas_dimension&&pages==atlas_pages)atlas_ids[page]=texture;
}
void qgl_eye(const float *v,const float *p){
    qgl_flush();
    memcpy(eye_view,v,64);memcpy(eye_proj,p,64);
    glUseProgram(program);
    glUniformMatrix4fv(uniforms[U_VIEW],1,GL_FALSE,v);glUniformMatrix4fv(uniforms[U_PROJ],1,GL_FALSE,p);glUniform1i(uniforms[U_TEX],0);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    for(int i=0;i<3;i++)glEnableVertexAttribArray(i);
}
static bool identity_lut(const uint8_t lut[3][256]){
    if(!lut)return true;
    for(int j=0;j<3;j++)for(int i=0;i<256;i++)if(lut[j][i]!=i)return false;
    return true;
}
void qgl_target(int eye,unsigned framebuffer){
    qgl_flush();target_eye=eye;target_framebuffer=framebuffer;scene_active=false;
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
}
void qgl_scene_begin(const uint8_t lut[3][256],int w,int h){
    if(multi_active){glBindFramebuffer(GL_FRAMEBUFFER,multi_fb);return;}
    if(target_eye<0||target_eye>1)return;
    qgl_flush();scene_active=false;
    glBindFramebuffer(GL_FRAMEBUFFER,target_framebuffer);
    if(identity_lut(lut))return;
    /* Render straight into the texture sampled by gamma correction. Copying a
     * completed eye with glCopyTexSubImage2D forced an extra mobile render pass. */
    int i=target_eye;
    if(!scene_targets[i].texture){glGenTextures(1,&scene_targets[i].texture);glGenFramebuffers(1,&scene_targets[i].framebuffer);}
    if(scene_targets[i].w!=w||scene_targets[i].h!=h){
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scene_targets[i].texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindTexture(GL_TEXTURE_2D,bindings[0]);glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);
        glBindFramebuffer(GL_FRAMEBUFFER,scene_targets[i].framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,scene_targets[i].texture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,target_framebuffer);return;}
        scene_targets[i].w=w;scene_targets[i].h=h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,scene_targets[i].framebuffer);scene_active=true;
}
void qgl_flat_camera(float cx,float cy,float focal){
    if(!isfinite(cx)||!isfinite(cy)||!isfinite(focal)||focal<=0)return;
    float next[4]={cx,cy,2.5f/focal,2.5f};
    if(!memcmp(next,flat_camera,sizeof next))return;
    qgl_flush();memcpy(flat_camera,next,sizeof next);
}
void qgl_flush(void){
    if(!command_count)return;
    glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glUniform4fv(uniforms[U_FLAT_CAMERA],1,flat_camera);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)stream_count*sizeof(Vertex),stream,GL_STREAM_DRAW);
    for(int i=0;i<3;i++)glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(size_t)(i*16));
    glActiveTexture(GL_TEXTURE0);
    for(size_t i=0;i<command_count;i++){
        const Command *cmd=&commands[i];const QDrawState *s=&cmd->state,*prev=i?&commands[i-1].state:NULL;
        if(!prev||s->texture!=prev->texture)glBindTexture(GL_TEXTURE_2D,s->texture);
        if(!prev||s->blend!=prev->blend){if(s->blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);}
        if(!prev||s->src!=prev->src||s->dst!=prev->dst)glBlendFunc(s->src,s->dst);
        if(!prev||memcmp(s->mask,prev->mask,4))glColorMask(s->mask[0],s->mask[1],s->mask[2],s->mask[3]);
        if(!prev||s->textured!=prev->textured)glUniform1i(uniforms[U_TEXTURED],s->textured);
        if(!prev||s->alpha!=prev->alpha)glUniform1i(uniforms[U_ALPHA],s->alpha);
        if(!prev||s->fog!=prev->fog)glUniform1i(uniforms[U_FOG],s->fog);
        if(!prev||s->replace!=prev->replace)glUniform1i(uniforms[U_REPLACE],s->replace);
        if(!prev||s->scale!=prev->scale)glUniform1f(uniforms[U_SCALE],s->scale);
        if(!prev||s->threshold!=prev->threshold)glUniform1f(uniforms[U_THRESHOLD],s->threshold);
        if(!prev||memcmp(s->fog_color,prev->fog_color,12))glUniform3fv(uniforms[U_FOG_COLOR],1,s->fog_color);
        glDrawArrays(s->mode,cmd->first,cmd->count);
    }
    glBindTexture(GL_TEXTURE_2D,bindings[0]);glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);
    command_count=stream_count=0;
}
static void draw(const Vertex *v,int n,GLenum mode) {
    if(n<=0)return;
    if(stream_count+(size_t)n>stream_capacity){
        size_t cap=(stream_count+n)*2;void*p=realloc(stream,cap*sizeof(Vertex));if(!p)abort();stream=p;stream_capacity=cap;
    }
    QDrawState s;memset(&s,0,sizeof s);
    s.texture=bindings[0];s.mode=mode;s.src=blend_src;s.dst=blend_dst;s.blend=blend_on;
    s.textured=texture_on[0];s.alpha=alpha_on;s.fog=texture_on[1];s.replace=alpha_replace;
    int layer=s.textured?atlas_layer(s.texture):-1;
    /* Layer indices ride in the previously unused texture R coordinate. Atlas
     * page changes no longer split a painter-ordered batch. */
    if(layer>=0){s.texture=0;s.textured=2;}
    s.scale=rgb_scale;s.threshold=alpha_ref;memcpy(s.fog_color,fog_color,12);memcpy(s.mask,color_mask,4);
    if(command_count&&mode==GL_TRIANGLES&&!memcmp(&commands[command_count-1].state,&s,sizeof s))commands[command_count-1].count+=n;
    else{
        if(command_count==command_capacity){size_t cap=command_capacity?command_capacity*2:1024;void*p=realloc(commands,cap*sizeof(Command));if(!p)abort();commands=p;command_capacity=cap;}
        commands[command_count++]=(Command){s,(int)stream_count,n};
    }
    memcpy(stream+stream_count,v,(size_t)n*sizeof(Vertex));
    if(layer>=0)for(int i=0;i<n;i++)stream[stream_count+i].uv[2]=(float)layer;
    stream_count+=n;
#ifdef QGL_TEST_IMMEDIATE
    qgl_flush(); /* Regression oracle: each call draws before any subsequent state/texture change. */
#endif
}
void qglEnable(GLenum e){if(e==GL_TEXTURE_2D)texture_on[active]=1;else if(e==GL_ALPHA_TEST)alpha_on=1;else if(e==GL_BLEND)blend_on=1;else if(e!=GL_SCISSOR_TEST){qgl_flush();glEnable(e);}}
void qglDisable(GLenum e){if(e==GL_TEXTURE_2D)texture_on[active]=0;else if(e==GL_ALPHA_TEST)alpha_on=0;else if(e==GL_BLEND)blend_on=0;else if(e!=GL_SCISSOR_TEST){qgl_flush();glDisable(e);}}
void qglActiveTexture(GLenum e){active=e==GL_TEXTURE1?1:0;glActiveTexture(e);}
void qglBindTexture(GLenum target,GLuint texture){bindings[active]=texture;glBindTexture(target,texture);}
void qglBlendFunc(GLenum src,GLenum dst){blend_src=src;blend_dst=dst;}
void qglColorMask(GLboolean r,GLboolean g,GLboolean b,GLboolean a){color_mask[0]=r;color_mask[1]=g;color_mask[2]=b;color_mask[3]=a;}
void qglClear(GLbitfield mask){qgl_flush();glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);glClear(mask);}
void qglTexImage2D(GLenum target,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void*p){
    qgl_flush();int layer=atlas_layer(bindings[active]);
    if(layer>=0&&target==GL_TEXTURE_2D){
        if(p){glActiveTexture(GL_TEXTURE2);glTexSubImage3D(GL_TEXTURE_2D_ARRAY,level,0,0,layer,w,h,1,format,type,p);glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);}
    }else glTexImage2D(target,level,internal,w,h,border,format,type,p);
}
void qglTexSubImage2D(GLenum target,GLint level,GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,const void*p){
    qgl_flush();int layer=atlas_layer(bindings[active]);
    if(layer>=0&&target==GL_TEXTURE_2D){glActiveTexture(GL_TEXTURE2);glTexSubImage3D(GL_TEXTURE_2D_ARRAY,level,x,y,layer,w,h,1,format,type,p);glActiveTexture(active?GL_TEXTURE1:GL_TEXTURE0);}
    else glTexSubImage2D(target,level,x,y,w,h,format,type,p);
}
void qglTexParameteri(GLenum target,GLenum name,GLint value){qgl_flush();glTexParameteri(target,name,value);}
void qglDeleteTextures(GLsizei n,const GLuint*p){qgl_flush();glDeleteTextures(n,p);for(int i=0;i<n;i++){int layer=atlas_layer(p[i]);if(layer>=0)atlas_ids[layer]=0;for(int j=0;j<2;j++)if(bindings[j]==p[i])bindings[j]=0;}}
void qglAlphaFunc(GLenum e,float v){(void)e;alpha_ref=v;}
void qglTexEnvi(GLenum t,GLenum p,GLint v){(void)t;if(active==0){if(p==GL_TEXTURE_ENV_MODE&&v==GL_MODULATE){rgb_scale=1;alpha_replace=0;}if(p==GL_COMBINE_ALPHA)alpha_replace=v==GL_REPLACE;}}
void qglTexEnvf(GLenum t,GLenum p,GLfloat v){(void)t;if(active==0&&p==GL_RGB_SCALE)rgb_scale=v;}
void qglTexEnvfv(GLenum t,GLenum p,const GLfloat*v){(void)t;if(p==GL_TEXTURE_ENV_COLOR)memcpy(fog_color,v,12);}
void qglMatrixMode(GLenum e){(void)e;}void qglLoadIdentity(void){}
void qglOrtho(double l,double r,double b,double t,double n,double f){(void)l;(void)r;(void)b;(void)t;(void)n;(void)f;}
/* Original scissors are in the arcade camera's coordinates; the headset clips the reprojected geometry. */
void qglScissor(GLint x,GLint y,GLsizei w,GLsizei h){(void)x;(void)y;(void)w;(void)h;}
void qglEnableClientState(GLenum e){(void)e;}void qglDisableClientState(GLenum e){(void)e;}
void qglVertexPointer(GLint s,GLenum t,GLsizei stride,const void*p){(void)t;arrays[0].p=p;arrays[0].size=s;arrays[0].stride=stride?stride:s*4;}
void qglColorPointer(GLint s,GLenum t,GLsizei stride,const void*p){(void)t;arrays[1].p=p;arrays[1].size=s;arrays[1].stride=stride?stride:s*4;}
void qglTexCoordPointer(GLint s,GLenum t,GLsizei stride,const void*p){(void)t;arrays[2].p=p;arrays[2].size=s;arrays[2].stride=stride?stride:s*4;}
void qglDrawArrays(GLenum mode,GLint first,GLsizei n){
    if(n<0)return;if((size_t)n>capacity){void *p=realloc(vertices,(size_t)n*sizeof(Vertex));if(!p)abort();vertices=p;capacity=n;}
    for(int i=0;i<n;i++)for(int a=0;a<3;a++){
        float *out=a==0?vertices[i].p:a==1?vertices[i].c:vertices[i].uv;
        memset(out,0,16);if(a)out[3]=1;
        const char *src=(const char*)arrays[a].p+(first+i)*arrays[a].stride;
        if(arrays[a].p)memcpy(out,src,arrays[a].size*4);
    }draw(vertices,n,mode);
}
void qglBegin(GLenum e){im_count=0;im_mode=e;}
void qglColor4f(float r,float g,float b,float a){color[0]=r;color[1]=g;color[2]=b;color[3]=a;}
void qglTexCoord2f(float s,float t){qglTexCoord4f(s,t,0,1);}
void qglTexCoord4f(float s,float t,float r,float q){uv[0]=s;uv[1]=t;uv[2]=r;uv[3]=q;}
void qglVertex2f(float x,float y){if(im_count>=4096)abort();Vertex*v=&immediate[im_count++];v->p[0]=x;v->p[1]=y;v->p[2]=0;v->p[3]=0;memcpy(v->c,color,16);memcpy(v->uv,uv,16);}
void qglVertex4f(float x,float y,float z,float w){qglVertex2f(x,y);immediate[im_count-1].p[2]=z;immediate[im_count-1].p[3]=w;}
void qglEnd(void){if(im_mode==GL_QUADS){for(int i=0;i+3<im_count;i+=4){Vertex v[6]={immediate[i],immediate[i+1],immediate[i+2],immediate[i],immediate[i+2],immediate[i+3]};draw(v,6,GL_TRIANGLES);}}else draw(immediate,im_count,im_mode);}
static void feedback_ray(V3 origin,V3 end,float r,float g,float b,float opacity){
    Vertex v[2]={{{origin.x,origin.y,origin.z,1},{r,g,b,opacity},{0,0,0,1}},{{end.x,end.y,end.z,1},{r,g,b,opacity},{0,0,0,1}}};
    int t0=texture_on[0],t1=texture_on[1],a=alpha_on,blend=blend_on;
    GLenum src=blend_src,dst=blend_dst;GLboolean mask[4];memcpy(mask,color_mask,4);
    texture_on[0]=texture_on[1]=alpha_on=0;blend_on=opacity<1;
    blend_src=GL_SRC_ALPHA;blend_dst=GL_ONE_MINUS_SRC_ALPHA;memset(color_mask,1,4);
    draw(v,2,GL_LINES);
    texture_on[0]=t0;texture_on[1]=t1;alpha_on=a;blend_on=blend;blend_src=src;blend_dst=dst;memcpy(color_mask,mask,4);
}
void qgl_pointer(V3 origin,V3 end){feedback_ray(origin,end,.2f,.85f,1,1);}
void qgl_tracer(V3 origin,V3 end,float opacity){
    if(opacity>0&&isfinite(opacity))feedback_ray(origin,end,1,.32f,.025f,fminf(opacity,1));
}
void eng_post_lut(const uint8_t lut[3][256],int w,int h){
    if(multi_active){memcpy(multi_gamma_table,lut,sizeof multi_gamma_table);multi_gamma=!identity_lut(lut);return;}
    bool changed=!lut_valid||memcmp(cached_lut,lut,sizeof cached_lut);
    if(changed){
        memcpy(cached_lut,lut,sizeof cached_lut);lut_identity=identity_lut(lut);
    }
    /* An identity gamma table needs neither a full-eye copy nor a post pass. */
    if(lut_identity){lut_valid=true;return;}
    qgl_flush();
    GLint binding[2];glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding[0]);
    if(scene_active){
        glBindFramebuffer(GL_FRAMEBUFFER,target_framebuffer);
        glBindTexture(GL_TEXTURE_2D,scene_targets[target_eye].texture);scene_active=false;
    }else{
        glBindTexture(GL_TEXTURE_2D,post_tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        if(w!=post_w||h!=post_h){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);post_w=w;post_h=h;}
        /* GLES rejects CopyTexSubImage from an sRGB eye into an RGBA8
         * texture. With sRGB writes disabled, blitting preserves the arcade
         * bytes across these formats without a sampling-time sRGB decode. */
        if(!post_copy_fb)glGenFramebuffers(1,&post_copy_fb);
        GLint draw_fb;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,post_copy_fb);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,post_tex,0);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)draw_fb);
    }
    glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding[1]);glBindTexture(GL_TEXTURE_2D,lut_tex);
    if(changed){
        uint8_t rgb[768];for(int i=0;i<256;i++)for(int j=0;j<3;j++)rgb[i*3+j]=lut[j][i];
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,256,1,0,GL_RGB,GL_UNSIGNED_BYTE,rgb);
        lut_valid=true;
    }
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glColorMask(1,1,1,1);
    glUseProgram(post_program);glUniform1i(post_screen,0);glUniform1i(post_lut,1);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindTexture(GL_TEXTURE_2D,binding[1]);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,binding[0]);active=0;
}
void qgl_shutdown(void){
    glDeleteFramebuffers(1,&post_copy_fb);post_copy_fb=0;
    if(multi_active)qgl_stereo_end();
    glDeleteProgram(multi_program);glDeleteProgram(multi_post);glDeleteTextures(1,&multi_texture);glDeleteTextures(1,&multi_lut);glDeleteFramebuffers(1,&multi_fb);
    multi_program=multi_post=multi_texture=multi_fb=multi_lut=0;multi_w=multi_h=0;multi_lut_valid=false;framebuffer_multiview=NULL;
    glDeleteTextures(1,&atlas_texture);atlas_texture=0;atlas_dimension=atlas_pages=0;atlas_unavailable=false;memset(atlas_ids,0,sizeof atlas_ids);
    for(int i=0;i<2;i++){glDeleteFramebuffers(1,&scene_targets[i].framebuffer);glDeleteTextures(1,&scene_targets[i].texture);}memset(scene_targets,0,sizeof scene_targets);target_eye=-1;scene_active=false;
    free(vertices);vertices=NULL;capacity=0;free(stream);stream=NULL;stream_count=stream_capacity=0;free(commands);commands=NULL;command_count=command_capacity=0;glDeleteProgram(program);glDeleteProgram(post_program);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteTextures(1,&post_tex);glDeleteTextures(1,&lut_tex);
}
