/* Exercise ordered draws and texture-write barriers against the immediate path. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include RENDERER_SOURCE
#include "quest_tracer.h"
__declspec(dllexport) int tracer_fixture(int age,int eye,int w,int h,uint8_t *pixels){
    if(!qgl_init())return 0;
    float v[16],p[16];view_matrix(v,v3(eye? .032f:-.032f,0,0),(Q4){0,0,0,1});projection(p,-.4f,.4f,-.4f,.4f);
    qgl_target(eye,0);qgl_eye(v,p);glViewport(0,0,w,h);glClearColor(.08f,.12f,.16f,1);glClear(GL_COLOR_BUFFER_BIT);
    QTracer tracer={0};qtracer_start(&tracer,v3(.10f,-.06f,-.40f),v3(.30f,.18f,-2.5f),1000);
    qgl_tracer(tracer.origin,tracer.end,qtracer_opacity(&tracer,1000+age));qgl_flush();
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);GLenum error=glGetError();qgl_shutdown();return error?-(int)error:1;
}
#ifdef TEST_SRGB_TARGET
#include "quest_color.h"
#endif
static int fixture_gl_error(const char *stage){
    GLenum error=glGetError();if(error)fprintf(stderr,"Render fixture %s: GLES error 0x%x\n",stage,error);return error;
}
#ifdef IMMEDIATE_REFERENCE
#define qglBindTexture glBindTexture
#define qglBlendFunc glBlendFunc
#define qglColorMask glColorMask
#define qglClear glClear
#define qglTexImage2D glTexImage2D
#define qglTexSubImage2D glTexSubImage2D
#define qglTexParameteri glTexParameteri
#define qgl_flush() ((void)0)
#endif
#ifdef TEST_MULTIVIEW
static void *multiview_proc;
__declspec(dllexport) void render_fixture_proc(void *p){multiview_proc=p;}
#endif
static void rect(float x,float y,float w,float h){
    qglBegin(GL_QUADS);
    qglTexCoord2f(0,0);qglVertex2f(x,y);
    qglTexCoord2f(1,0);qglVertex2f(x+w,y);
    qglTexCoord2f(1,1);qglVertex2f(x+w,y+h);
    qglTexCoord2f(0,1);qglVertex2f(x,y+h);qglEnd();
}
/* Pixel-level oracle: flat sprites must lie on the reconstructed camera rays. */
__declspec(dllexport) int projection_fixture(int variant,int w,int h,uint8_t *out){
    if(!qgl_init())return 0;
    float view[16],proj[16];view_matrix(view,v3(0,0,0),(Q4){0,0,0,1});projection(proj,-.65f,.65f,-.65f,.65f);
    qgl_eye(view,proj);qgl_flat_camera(312,236,variant==0?500:772.6f);
    glViewport(0,0,w,h);glClearColor(0,0,0,1);qglColorMask(1,1,1,1);qglClear(GL_COLOR_BUFFER_BIT);
    qglActiveTexture(GL_TEXTURE1);qglDisable(GL_TEXTURE_2D);qglActiveTexture(GL_TEXTURE0);qglDisable(GL_TEXTURE_2D);qglDisable(GL_BLEND);qglDisable(GL_ALPHA_TEST);qglColor4f(1,1,1,1);
    for(int y=77;y<440;y+=113)for(int x=57;x<620;x+=119){
        if(variant<2)rect((float)x,(float)y,17,19);
        else{
            const int offset[4][2]={{0,0},{17,0},{17,19},{0,19}};qglBegin(GL_QUADS);
            for(int j=0;j<4;j++)qglVertex4f((x+offset[j][0]-312)*30.f/772.6f,(236-y-offset[j][1])*30.f/772.6f,-30,1);
            qglEnd();
        }
    }
    qgl_flush();glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,out);int result=glGetError()?0:1;qgl_shutdown();return result;
}
__declspec(dllexport) int render_fixture(int variant,int w,int h,uint8_t*out){
    post_w=post_h=0;active=0;texture_on[0]=texture_on[1]=0;
    if(!qgl_init())return 0;
    GLuint output_fb=0;
#ifdef TEST_SRGB_TARGET
    if(!qcolor_srgb_write_control())return -12;
    GLuint output_tex;glGenTextures(1,&output_tex);glBindTexture(GL_TEXTURE_2D,output_tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&output_fb);glBindFramebuffer(GL_FRAMEBUFFER,output_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output_tex,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return -13;
    qcolor_raw_output(GL_SRGB8_ALPHA8);
    if(fixture_gl_error("sRGB target setup"))return -20;
#endif
    float view[16],proj[16];view_matrix(view,v3((variant&1)?.06f:0,0,0),(Q4){0,0,0,1});projection(proj,-.65f,.65f,-.65f,.65f);
    uint8_t lut[3][256];for(int i=0;i<256;i++){lut[0][i]=(uint8_t)(i*.8f);lut[1][i]=(uint8_t)i;lut[2][i]=(uint8_t)(255-i);}
    if(variant&2)for(int j=0;j<3;j++)for(int i=0;i<256;i++)lut[j][i]=(uint8_t)i;
#ifdef TEST_SCENE_TARGET
#ifdef TEST_MULTIVIEW
    if(!qgl_stereo_init(multiview_proc))return -10;
    float vv[2][16],pp[2][16];
    for(int i=0;i<2;i++){view_matrix(vv[i],v3(i?.06f:0,0,0),(Q4){0,0,0,1});memcpy(pp[i],proj,64);}
    if(!qgl_stereo_begin(vv[0],pp[0],w,h))return -11;
#endif
    qgl_target(variant&1,output_fb);qgl_scene_begin(lut,w,h);
#endif
#ifndef TEST_MULTIVIEW
    qgl_eye(view,proj);glViewport(0,0,w,h);qglColorMask(1,1,1,1);glClearColor(.07f,.11f,.13f,1);qglClear(GL_COLOR_BUFFER_BIT);
#else
    glViewport(0,0,w,h);qglColorMask(1,1,1,1);glClearColor(.07f,.11f,.13f,1);qglClear(GL_COLOR_BUFFER_BIT);
#endif
    GLuint tex[2];glGenTextures(2,tex);
    uint8_t pixels[16]={255,32,16,255, 16,240,64,255, 32,64,255,128, 240,220,180,0};
    for(int i=0;i<2;i++){
#ifdef TEST_ATLAS_ARRAY
        qgl_atlas_texture(tex[i],i,2,2);
#endif
        qglBindTexture(GL_TEXTURE_2D,tex[i]);qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        qglTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    }
    qglEnable(GL_TEXTURE_2D);qglEnable(GL_ALPHA_TEST);qglAlphaFunc(GL_GREATER,.1f);
    qgl_flat_camera(318,244,772.6f);
    for(int i=0;i<70;i++){
        qglBindTexture(GL_TEXTURE_2D,tex[i%2]);qglColor4f(.6f+(i%3)*.2f,.8f,1,1);
        rect(40+(i%10)*40,40+(i/10)*40,85,75);
    }
    /* The first texture's queued draws must observe its OLD texels. */
    /* Camera changes are also barriers: queued sprites retain their camera. */
    qgl_flat_camera(320,240,500);
    memset(pixels,180,sizeof pixels);qglBindTexture(GL_TEXTURE_2D,tex[0]);
    uint8_t strided[24];memset(strided,99,sizeof strided);memcpy(strided,pixels,8);memcpy(strided+12,pixels+8,8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,3);
    qglTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,strided);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    qglEnable(GL_BLEND);qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);qglColor4f(1,.3f,.1f,.4f);rect(120,80,260,260);
    qglColorMask(1,0,1,1);rect(200,140,80,180);qglColorMask(1,1,1,1);
    /* Ordinary sprite/text textures must coexist with atlas-array sampling. */
    GLuint overlay;glGenTextures(1,&overlay);qglBindTexture(GL_TEXTURE_2D,overlay);
    qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);qglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    qglTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    rect(530,320,50,80);qglBindTexture(GL_TEXTURE_2D,tex[0]);
    /* Fog uses unit 1 without sampling its bound texture. */
    qglActiveTexture(GL_TEXTURE1);qglEnable(GL_TEXTURE_2D);float fog[]={.1f,.7f,.3f,1};qglTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,fog);
    qglActiveTexture(GL_TEXTURE0);qglDisable(GL_BLEND);qglColor4f(1,1,1,.25f);rect(360,80,150,200);
    qglActiveTexture(GL_TEXTURE1);qglDisable(GL_TEXTURE_2D);qglActiveTexture(GL_TEXTURE0);
    qglDisable(GL_TEXTURE_2D);qglDisable(GL_ALPHA_TEST);qglColor4f(.2f,.4f,.9f,1);rect(20,380,520,30);
    /* Reconstructed world coordinates and projective UVs, at different depths. */
    const float pos[]={-.6f,-.1f,-1.7f,1, .3f,.35f,-2.8f,1, .4f,-.35f,-2.1f,1};
    const float col[]={1,1,1,1, .7f,.9f,1,1, 1,.6f,.8f,1};
    const float texcoord[]={0,0,0,1, 1,0,0,2, 1,1,0,1};
    qglBindTexture(GL_TEXTURE_2D,tex[1]);qglEnable(GL_TEXTURE_2D);
    qglVertexPointer(4,GL_FLOAT,0,pos);qglColorPointer(4,GL_FLOAT,0,col);qglTexCoordPointer(4,GL_FLOAT,0,texcoord);
    qglDrawArrays(GL_TRIANGLES,0,3);
    if(fixture_gl_error("scene rendering"))return -21;
    eng_post_lut(lut,w,h);
    if(fixture_gl_error("scene gamma"))return -22;
#ifdef TEST_MULTIVIEW
    qgl_stereo_end();qgl_target(variant&1,output_fb);qgl_eye(view,proj);qgl_stereo_blit(variant&1);
#endif
    eng_post_lut(lut,w,h); /* Unchanged non-identity LUT must still apply each time. */
    if(fixture_gl_error("repeated gamma"))return -23;
    for(int j=0;j<3;j++)for(int i=0;i<256;i++)lut[j][i]=(uint8_t)i;
    eng_post_lut(lut,w,h); /* Identity fast path preserves the framebuffer. */
    lut[0][128]=42;eng_post_lut(lut,w,h); /* Return from identity to a changed LUT. */
    qgl_pointer(v3(-.3f,-.2f,-1),v3(.1f,.2f,-2.5f));qgl_flush();
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,out);GLenum error=glGetError();int ok=error?-(int)error:1;
    qglDeleteTextures(2,tex);qglDeleteTextures(1,&overlay);qgl_shutdown();
#ifdef TEST_SRGB_TARGET
    glDeleteFramebuffers(1,&output_fb);glDeleteTextures(1,&output_tex);
#endif
    return ok;
}
