/* Small stereo options panel. Reuses the upstream font; one draw and texture
 * uploads only when the displayed setting changes. No per-frame font rasterizing. */
#include "quest_gpu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "quest_ui.h"
#include "quest_options.h"
#include "quest_panel.h"
#include "quest_lobby.h"
static unsigned last_lobby_revision;
static bool last_lobby;

enum { WIDTH=640,HEIGHT=480 };
static GLuint program,vao,vbo,texture;
static GLint u_view,u_projection,u_texture;
static int last_state=-1;
static int last_pitch,last_scale,last_distance;
static int last_y,last_z,last_player,last_boot;
static uint8_t pixels[WIDTH*HEIGHT*4];

static void text(int x,int y,const char *s,uint8_t r,uint8_t g,uint8_t b){qpanel_text(pixels,x,y,s,r,g,b);}
/* frame_controls: the Steam Frame controller profile is active, so hints
 * name its buttons (right-hand A/B/X/Menu, View) instead of Touch's. */
static void panel(const QOptions *options,int page,int boot_side,bool frame_controls,bool saved){
    bool laser=options->laser_enabled,physical_crouch=options->physical_crouch,left_handed=options->left_handed;
    int gun_pitch=options->gun_pitch,world_scale=options->world_scale,view_distance=options->view_distance;
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        uint8_t *p=pixels+(y*WIDTH+x)*4;bool border=x<2||x>=WIDTH-2||y<2||y>=HEIGHT-2;
        p[0]=border?54:12;p[1]=border?183:19;p[2]=border?207:29;p[3]=border?255:240;
    }
    char label[40];
    if(page){
        text(24,20,"GUN FIT / PLAYER",181,207,220);
        snprintf(label,sizeof label,"GUN UP: %+d MM",options->gun_offset_y_mm);text(24,64,label,255,255,255);
        text(24,90,"RIGHT STICK UP / DOWN: ADJUST",181,207,220);
        snprintf(label,sizeof label,"GUN BACK: %+d MM",options->gun_offset_z_mm);text(24,132,label,255,255,255);
        text(24,158,"RIGHT STICK LEFT / RIGHT: ADJUST",181,207,220);
        text(24,190,"LEFT CLICK: RESET BOTH OFFSETS",181,207,220);
        text(24,222,"+UP RAISES / +BACK PULLS TOWARDS YOU",181,207,220);
        snprintf(label,sizeof label,"PLAYER: P%d %s",options->player_side,options->player_side==2?"ROBERT BAXTER":"KEITH MARTIN");text(24,264,label,255,255,255);
        text(24,290,"LEFT STICK LEFT: P1 / RIGHT: P2",181,207,220);
        text(24,322,options->player_side!=boot_side?"RESTART APP TO CHANGE PLAYER":"PLAYER APPLIES AT APP START",255,202,110);
        text(24,364,frame_controls?"RIGHT A: ADD CREDITS":left_handed?"X LEFT: ADD CREDITS":"A RIGHT: ADD CREDITS",181,207,220);
        text(24,426,"LEFT UP: LAN PLAY / DOWN: OPTIONS",181,207,220);
        text(24,454,saved?(frame_controls?"RIGHT MENU: RESUME / VIEW: BACK":"LEFT MENU: RESUME"):"SAVING FAILED",saved?170:255,saved?193:150,saved?206:150);
        return;
    }
    text(24,20,"PAUSE / OPTIONS",181,207,220);
    text(24,54,left_handed?"DEFAULT HAND: LEFT":"DEFAULT HAND: RIGHT",255,255,255);
    text(24,78,"RIGHT STICK CLICK: CHANGE DEFAULT",181,207,220);
    snprintf(label,sizeof label,"GUN ANGLE: %+d DEG",gun_pitch);
    text(24,110,label,255,255,255);
    text(24,134,"RIGHT UP/DOWN: ANGLE | LEFT CLICK: 0",181,207,220);
    snprintf(label,sizeof label,"WORLD SIZE: %d%%",world_scale);text(24,168,label,255,255,255);
    text(24,192,"RIGHT STICK LEFT / RIGHT: ADJUST",181,207,220);
    int knob=28+(world_scale-QOPTIONS_WORLD_SCALE_MIN)*583/(QOPTIONS_WORLD_SCALE_MAX-QOPTIONS_WORLD_SCALE_MIN);
    for(int y=216;y<232;y++)for(int x=24;x<616;x++){
        bool handle=x>=knob-4&&x<=knob+4;
        if(!handle&&(y<221||y>226))continue;
        uint8_t *p=pixels+(y*WIDTH+x)*4;
        p[0]=handle?255:54;p[1]=handle?255:183;p[2]=handle?255:207;p[3]=255;
    }
    snprintf(label,sizeof label,"VIEW DISTANCE: %d CM",view_distance);text(24,242,label,255,255,255);
    text(24,266,"LEFT STICK LEFT / RIGHT: ADJUST",181,207,220);
    snprintf(label,sizeof label,"AIM: %s",qoptions_aim_label(options));text(24,298,label,255,202,110);
    text(336,298,frame_controls?"RIGHT B: CHANGE":left_handed?"Y: CHANGE":"B: CHANGE",181,207,220);
    text(24,328,physical_crouch?"COVER: PHYSICAL DUCKING":"COVER: GRIP BUTTONS",255,255,255);
    text(24,352,frame_controls?"RIGHT X: CHANGE MODE":left_handed?"B RIGHT: CHANGE MODE":"Y LEFT: CHANGE MODE",181,207,220);
    text(24,378,physical_crouch?"FOLLOW THE SAFE SIGN TO HIDE":"HOLD EITHER GRIP: LEAVE COVER",181,207,220);
    text(24,402,frame_controls?"VIEW HOLD: 1S RECENTER, 3S +HEIGHT":left_handed?"A RIGHT: RECENTER / CALIBRATE":"X LEFT: RECENTER / CALIBRATE",181,207,220);
    text(24,426,"LEFT UP: GUN FIT / DOWN: LAN PLAY",181,207,220);
    text(24,454,saved?(frame_controls?"RIGHT MENU: RESUME / VIEW: BACK":"LEFT MENU: RESUME"):"SAVING FAILED",saved?170:255,saved?193:150,saved?206:150);
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
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenTextures(1,&texture);last_state=-1;
    return true;
}
void qui_draw(const float view[16],const float projection[16],V3 head,Q4 rotation,const QOptions *options,int page,int boot_side,bool frame_controls,bool saved){
    if(!program)return;
    GLint bound,unit;glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,texture);
    int state=(options->physical_crouch?1:0)|(options->laser_enabled?2:0)|(saved?4:0)|(options->left_handed?8:0)|(options->tracer_enabled?16:0)|(page?32:0)|(frame_controls?64:0);
    bool lobby=qlobby_visible(); unsigned lobby_revision=qlobby_revision();
    if(last_state!=state||last_lobby!=lobby||(lobby&&last_lobby_revision!=lobby_revision)||last_pitch!=options->gun_pitch||last_scale!=options->world_scale||last_distance!=options->view_distance||last_y!=options->gun_offset_y_mm||last_z!=options->gun_offset_z_mm||last_player!=options->player_side||last_boot!=boot_side){
        if(lobby)qlobby_render(pixels);else panel(options,page,boot_side,frame_controls,saved);
        last_lobby=lobby;last_lobby_revision=lobby_revision;last_pitch=options->gun_pitch;last_scale=options->world_scale;last_distance=options->view_distance;
        last_y=options->gun_offset_y_mm;last_z=options->gun_offset_z_mm;last_player=options->player_side;last_boot=boot_side;
        GLint row_length;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,WIDTH,HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);last_state=state;
    }
    /* One panel 1.6 m from the head, with each eye's real view/projection. */
    const float xy[4][2]={{-.65f,.4875f},{.65f,.4875f},{.65f,-.4875f},{-.65f,-.4875f}};
    const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};const int order[6]={0,1,2,0,2,3};float vertices[6][5];
    for(int i=0;i<6;i++){int j=order[i];V3 p=add(head,rotate(rotation,v3(xy[j][0],xy[j][1],-1.6f)));vertices[i][0]=p.x;vertices[i][1]=p.y;vertices[i][2]=p.z;vertices[i][3]=uv[j][0];vertices[i][4]=uv[j][1];}
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);glUniformMatrix4fv(u_view,1,GL_FALSE,view);glUniformMatrix4fv(u_projection,1,GL_FALSE,projection);glUniform1i(u_texture,0);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),0);glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glDrawArrays(GL_TRIANGLES,0,6);glDisable(GL_BLEND);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture(unit);
}
void qui_shutdown(void){glDeleteProgram(program);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);glDeleteTextures(1,&texture);program=vao=vbo=texture=0;last_state=-1;}
