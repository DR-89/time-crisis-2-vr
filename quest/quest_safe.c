/* SAFE signpost for physical cover. The art is painted once at init from the
 * upstream font and simple shapes: no image assets, one texture, three quads. */
#ifndef QSAFE_PAINT_ONLY
#include "quest_gpu.h"
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "quest_safe.h"
#include "font_data.h"

enum { ATLAS_W=256,ATLAS_H=192,ROW=64 };
#ifndef QSAFE_PAINT_ONLY
static GLuint program,vao,vbo,texture;
static GLint u_view,u_projection,u_texture,u_tint;
#endif

static void put(uint8_t *rgba,int x,int y,uint8_t r,uint8_t g,uint8_t b,uint8_t a){
    if(x<0||y<0||x>=ATLAS_W||y>=ATLAS_H)return;
    uint8_t *p=rgba+(y*ATLAS_W+x)*4;p[0]=r;p[1]=g;p[2]=b;p[3]=a;
}
/* SAFE at three times the 16 px upstream font, inside a black outline. */
static bool glyph_on(const char *text,int x,int y){
    if(x<0||y<0||y>=48)return false;
    int index=x/48;if(index>=(int)strlen(text))return false;
    const uint8_t *glyph=builtin_font+((unsigned char)text[index]-32)*128;
    int col=(x%48)/3,row=y/3;uint8_t packed=glyph[row*8+col/2];
    return ((col&1)?packed&15:packed>>4)<8;
}
/* Distance (pixels) inside the right-pointing arrow; negative outside. */
static float arrow_inside(float x,float y){
    float shaft=fminf(fminf(x-14,166-x),fminf(y-21,43-y));
    /* Head: (160,4) (242,32) (160,60). */
    float left=x-158,upper=((242-160)*(y-4)-(32-4)*(x-160))/sqrtf(82.f*82+28*28),lower=((32-60)*(x-160)+(242-160)*(60-y))/sqrtf(82.f*82+28*28);
    float head=fminf(left,fminf(upper,lower));
    return fmaxf(shaft,head);
}
void qsafe_paint(unsigned char *rgba){
    memset(rgba,0,ATLAS_W*ATLAS_H*4);
    for(int y=0;y<ROW;y++)for(int x=0;x<ATLAS_W;x++){
        /* Row 0: the red plate with a black rim and a lighter top bevel. */
        bool rim=x<6||x>=ATLAS_W-6||y<6||y>=ROW-6;
        if(rim)put(rgba,x,y,20,16,16,255);else if(y<12)put(rgba,x,y,255,92,80,255);else put(rgba,x,y,224,20,27,255);
        /* Row 1: SAFE, outlined, on transparent. */
        int tx=x-32,ty=y-8;bool on=glyph_on("SAFE",tx,ty),edge=false;
        for(int dy=-3;dy<=3&&!on&&!edge;dy++)for(int dx=-3;dx<=3&&!edge;dx++)edge=glyph_on("SAFE",tx+dx,ty+dy);
        if(on)put(rgba,x,ROW+y,255,212,0,255);else if(edge)put(rgba,x,ROW+y,20,16,16,255);
        /* Row 2: a yellow arrow with a black outline. */
        float inside=arrow_inside(x+.5f,y+.5f);
        if(inside>=5)put(rgba,x,2*ROW+y,255,212,0,255);else if(inside>=0)put(rgba,x,2*ROW+y,20,16,16,255);
    }
}
#ifndef QSAFE_PAINT_ONLY /* host tests paint the atlas without GL */
static GLuint shader(GLenum type,const char *source){
    GLuint s=glCreateShader(type);qgpu_shader_source(s,source);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char message[1024];glGetShaderInfoLog(s,sizeof message,NULL,message);fprintf(stderr,"[SAFE] shader failed: %s\n",message);glDeleteShader(s);return 0;}return s;
}
bool qsafe_init(void){
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nprecision highp float;layout(location=0) in vec3 position;layout(location=1) in vec2 texcoord;uniform mat4 view,projection;out vec2 uv;void main(){gl_Position=projection*view*vec4(position,1);uv=texcoord;}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D signTex;uniform vec4 tint;out vec4 frag;void main(){frag=texture(signTex,uv)*tint;}");
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked){qsafe_shutdown();return false;}
    u_view=glGetUniformLocation(program,"view");u_projection=glGetUniformLocation(program,"projection");
    u_texture=glGetUniformLocation(program,"signTex");u_tint=glGetUniformLocation(program,"tint");
    static uint8_t pixels[ATLAS_W*ATLAS_H*4];qsafe_paint(pixels);
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenTextures(1,&texture);
    GLint bound,unit,row_length;glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,ATLAS_W,ATLAS_H,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture(unit);
    return true;
}
/* One quad: centre, half extents along right/up, atlas row. */
static void quad(float out[6][5],V3 centre,V3 right,V3 up,int row){
    const float corners[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}},uv[4][2]={{0,0},{1,0},{1,1},{0,1}};const int order[6]={0,1,2,0,2,3};
    float v0=(float)row*ROW/ATLAS_H,v1=(float)(row+1)*ROW/ATLAS_H;
    for(int i=0;i<6;i++){int j=order[i];V3 p=add(centre,add(mul(right,corners[j][0]),mul(up,corners[j][1])));
        out[i][0]=p.x;out[i][1]=p.y;out[i][2]=p.z;out[i][3]=uv[j][0];out[i][4]=uv[j][1]?v1:v0;}
}
void qsafe_draw(const float view[16],const float projection[16],V3 centre,float arrow_radians,float approach,bool text_on){
    if(!program)return;
    /* About 18 x 4.5 cm, read at roughly half a metre. */
    float vertices[18][5];
    quad(vertices,centre,v3(.09f,0,0),v3(0,.0225f,0),0);
    quad(vertices+6,centre,v3(.09f,0,0),v3(0,.0225f,0),1);
    float c=cosf(arrow_radians),s=sinf(arrow_radians);
    /* The arrow starts just under the plate and points along the slide. */
    V3 arrow_centre=add(centre,v3(.07f*c,-.055f+.07f*s,0));
    quad(vertices+12,arrow_centre,v3(.07f*c,.07f*s,0),v3(-.0175f*s,.0175f*c,0),2);
    GLint bound,unit;glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,texture);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);glUniformMatrix4fv(u_view,1,GL_FALSE,view);glUniformMatrix4fv(u_projection,1,GL_FALSE,projection);glUniform1i(u_texture,0);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),0);glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glUniform4f(u_tint,1,1,1,1);glDrawArrays(GL_TRIANGLES,0,6);
    if(text_on)glDrawArrays(GL_TRIANGLES,6,6);
    /* The arrow warms from yellow to green as the head nears the mark. */
    float a=fminf(1,fmaxf(0,approach));glUniform4f(u_tint,1-.83f*a,1,1,1);glDrawArrays(GL_TRIANGLES,12,6);
    glDisable(GL_BLEND);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture(unit);
}
void qsafe_shutdown(void){glDeleteProgram(program);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);glDeleteTextures(1,&texture);program=vao=vbo=texture=0;}
#endif
