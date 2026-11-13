/* The pause menu's panel (quest_menu.c draws its pixels) and the online toast. The panel's texture is uploaded only when the
 * menu redraws; the toast is rasterized with the upstream font only when its text changes. */
#include "quest_gpu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "quest_ui.h"
#include "font_data.h"

static GLuint program,vao,vbo,texture,toast_texture;
static GLint u_view,u_projection,u_texture;
static bool texture_ready;static unsigned menu_version;
static uint8_t pixels[QMENU_W*QMENU_H*4];
enum { TOAST_W=640,TOAST_H=40,TOAST_H2=64,TOAST_CHARS=38 };   /* one line of 38 characters, or two (a long message) */
static uint8_t toast_pixels[TOAST_W*TOAST_H2*4];
static unsigned toast_serial_drawn=~0u;static int toast_h=TOAST_H;

static void toast_text(int x,int y,const char *s){
    for(;*s;s++,x+=16){
        unsigned c=(unsigned char)*s;if(c<32||c>127)continue;
        const uint8_t *glyph=builtin_font+(c-32)*128;
        for(int row=0;row<16;row++)for(int col=0;col<16;col++){
            int px=x+col,py=y+row;if(px<0||px>=TOAST_W||py<0||py>=toast_h)continue;
            uint8_t packed=glyph[row*8+col/2];int shade=col&1?packed&15:packed>>4;
            if(shade==15)continue;
            uint8_t *p=toast_pixels+(py*TOAST_W+px)*4;int a=15-shade;
            p[0]=(255*a+p[0]*shade)/15;p[1]=(255*a+p[1]*shade)/15;p[2]=(255*a+p[2]*shade)/15;p[3]=255;
        }
    }
}
static GLuint shader(GLenum type,const char *source){
    GLuint s=glCreateShader(type);qgpu_shader_source(s,source);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char message[1024];glGetShaderInfoLog(s,sizeof message,NULL,message);fprintf(stderr,"[UI] shader failed: %s\n",message);glDeleteShader(s);return 0;}return s;
}
bool qui_init(void){
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nprecision highp float;layout(location=0) in vec3 position;layout(location=1) in vec2 texcoord;uniform mat4 view,projection;out vec2 uv;void main(){gl_Position=projection*view*vec4(position,1);uv=texcoord;}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D panelTex;out vec4 frag;void main(){frag=texture(panelTex,uv);}");
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked){qui_shutdown();return false;}
    u_view=glGetUniformLocation(program,"view");u_projection=glGetUniformLocation(program,"projection");u_texture=glGetUniformLocation(program,"panelTex");
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenTextures(1,&texture);glGenTextures(1,&toast_texture);texture_ready=false;toast_serial_drawn=~0u;
    return true;
}
void qui_shutdown(void){glDeleteProgram(program);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&toast_texture);program=vao=vbo=texture=toast_texture=0;texture_ready=false;toast_serial_drawn=~0u;}
static void upload(GLuint tex,int w,int h,const uint8_t *data,bool fresh){
    GLint bound;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,tex);
    GLint row_length;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    GLint align;glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    if(fresh){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length);glPixelStorei(GL_UNPACK_ALIGNMENT,align);glBindTexture(GL_TEXTURE_2D,bound);
}
/* How a quad meets what is under it: blended; copied as is; or blended with its cover taken out of the alpha (an underlay: the
 * headset shows its own copy of the menu through there). */
enum { BLEND,COPY,UNDERLAY };
/* one textured quad: centre, orientation, half sizes, at a distance along the orientation's -Z */
static void quad_mode(GLuint tex,const float view[16],const float projection[16],V3 center,Q4 rotation,float half_w,float half_h,float y,float z,int mode){
    const float xy[4][2]={{-half_w,y+half_h},{half_w,y+half_h},{half_w,y-half_h},{-half_w,y-half_h}};
    const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};const int order[6]={0,1,2,0,2,3};float vertices[6][5];
    for(int i=0;i<6;i++){int j=order[i];V3 p=add(center,rotate(rotation,v3(xy[j][0],xy[j][1],z)));vertices[i][0]=p.x;vertices[i][1]=p.y;vertices[i][2]=p.z;vertices[i][3]=uv[j][0];vertices[i][4]=uv[j][1];}
    GLint bound,unit;glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,tex);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);
    if(mode==COPY)glDisable(GL_BLEND);
    else{glEnable(GL_BLEND);if(mode==UNDERLAY)glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE_MINUS_SRC_ALPHA);else glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    glUseProgram(program);glUniformMatrix4fv(u_view,1,GL_FALSE,view);glUniformMatrix4fv(u_projection,1,GL_FALSE,projection);glUniform1i(u_texture,0);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),0);glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glDrawArrays(GL_TRIANGLES,0,6);glDisable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture(unit);
}
static void quad(GLuint tex,const float view[16],const float projection[16],V3 center,Q4 rotation,float half_w,float half_h,float y,float z){
    quad_mode(tex,view,projection,center,rotation,half_w,half_h,y,z,BLEND);
}
/* The menu's texture: only the box that changed goes up, then its smaller copies (mipmaps) are rebuilt on the GPU, so the text
 * stays steady instead of shimmering as the head moves. */
static void upload_menu(const int box[4]){
    GLint bound;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,texture);
    GLint row_length;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);
    GLint align;glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    if(!texture_ready){
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,QMENU_W,QMENU_H,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        texture_ready=true;
    }else if(box[2]>0&&box[3]>0){
        glPixelStorei(GL_UNPACK_ROW_LENGTH,QMENU_W);
        glTexSubImage2D(GL_TEXTURE_2D,0,box[0],box[1],box[2],box[3],GL_RGBA,GL_UNSIGNED_BYTE,pixels+((size_t)box[1]*QMENU_W+box[0])*4);
    }
    glGenerateMipmap(GL_TEXTURE_2D);menu_version++;
    glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length);glPixelStorei(GL_UNPACK_ALIGNMENT,align);glBindTexture(GL_TEXTURE_2D,bound);
}
void qui_draw_menu(const float view[16],const float projection[16],V3 center,Q4 rotation,const QMenuHost *host,double now,bool underlay){
    if(!program)return;
    /* both eyes draw the same frame: the second finds nothing new */
    int box[4]={0,0,QMENU_W,QMENU_H};
    struct timespec t0,t1;timespec_get(&t0,TIME_UTC);
    const bool drew=qmenu_render(host,pixels,now,box);
    timespec_get(&t1,TIME_UTC);
    const double ms=(t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6;
    if(drew&&ms>6){static unsigned slow;if(slow++<200)fprintf(stderr,"[MENU] slow redraw %.1f ms (%dx%d)\n",ms,box[2],box[3]);}
    if(drew||!texture_ready)upload_menu(box);
    if(underlay){   /* the picture's alpha: 1 everywhere, then 0 under the menu (the laser drawn after puts its own back) */
        GLboolean mask[4];glGetBooleanv(GL_COLOR_WRITEMASK,mask);GLfloat clear[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
        glDisable(GL_SCISSOR_TEST);glColorMask(0,0,0,1);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(clear[0],clear[1],clear[2],clear[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);
    }
    quad_mode(texture,view,projection,center,rotation,QUI_PANEL_W/2,QUI_PANEL_H/2,0,0,underlay?UNDERLAY:BLEND);
}
unsigned qui_menu_version(void){return texture_ready?menu_version:0;}
void qui_copy_menu(void){
    static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    if(program&&texture_ready)quad_mode(texture,identity,identity,v3(0,0,0),(Q4){0,0,0,1},1,1,0,0,COPY);
}
void qui_draw_toast(const float view[16],const float projection[16],V3 head,Q4 rotation,const char *message,unsigned serial){
    if(!program||!message||!message[0])return;
    if(serial!=toast_serial_drawn){
        /* A message longer than a line wraps once: at " - " when both halves fit, else at the last space that fits. */
        char lines[2][TOAST_CHARS+1]={{0},{0}};
        const size_t total=strlen(message);
        if(total<=TOAST_CHARS)snprintf(lines[0],sizeof lines[0],"%s",message);
        else{
            size_t cut=0,skip=1;
            const char *dash=strstr(message," - ");
            if(dash&&(size_t)(dash-message)<=TOAST_CHARS&&strlen(dash+3)<=TOAST_CHARS){cut=(size_t)(dash-message);skip=3;}
            else for(size_t i=TOAST_CHARS;i>0;i--)if(message[i]==' '){cut=i;break;}
            if(!cut){cut=TOAST_CHARS;skip=0;}
            snprintf(lines[0],sizeof lines[0],"%.*s",(int)cut,message);
            snprintf(lines[1],sizeof lines[1],"%s",message+cut+skip);
        }
        toast_h=lines[1][0]?TOAST_H2:TOAST_H;
        for(int y=0;y<toast_h;y++)for(int x=0;x<TOAST_W;x++){
            uint8_t *p=toast_pixels+(y*TOAST_W+x)*4;bool border=y<2||y>=toast_h-2||x<2||x>=TOAST_W-2;
            p[0]=border?255:10;p[1]=border?202:16;p[2]=border?110:24;p[3]=border?255:225;
        }
        for(int i=0;i<2;i++)if(lines[i][0])toast_text((TOAST_W-(int)strlen(lines[i])*16)/2,12+i*24,lines[i]);
        upload(toast_texture,TOAST_W,toast_h,toast_pixels,true);toast_serial_drawn=serial;
    }
    /* 1.3 m wide, 1.6 m ahead, below the centre of view so it does not cover targets. */
    quad(toast_texture,view,projection,head,rotation,.65f,.04f*toast_h/TOAST_H,-.42f,-1.6f);
}
