/* Small stereo options panel. Reuses the upstream font; one draw and texture
 * uploads only when the displayed setting changes. No per-frame font rasterizing. */
#include "quest_gpu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "quest_ui.h"
#include "quest_options.h"
#include "font_data.h"

enum { WIDTH=640,HEIGHT=480 };
static GLuint program,vao,vbo,texture;
static GLint u_view,u_projection,u_texture;
static int last_state=-1;
static int last_pitch,last_scale,last_distance;
static int last_y,last_z,last_player,last_boot;
static uint8_t pixels[WIDTH*HEIGHT*4];

static void text(int x,int y,const char *s,uint8_t r,uint8_t g,uint8_t b){
    for(;*s;s++,x+=16){
        unsigned c=(unsigned char)*s;if(c<32||c>127)continue;
        const uint8_t *glyph=builtin_font+(c-32)*128;
        for(int row=0;row<16;row++)for(int col=0;col<16;col++){
            int px=x+col,py=y+row;if(px<0||px>=WIDTH||py<0||py>=HEIGHT)continue;
            uint8_t packed=glyph[row*8+col/2];int shade=col&1?packed&15:packed>>4;
            if(shade==15)continue;
            uint8_t *p=pixels+(py*WIDTH+px)*4;int a=15-shade;
            p[0]=(r*a+p[0]*shade)/15;p[1]=(g*a+p[1]*shade)/15;p[2]=(b*a+p[2]*shade)/15;p[3]=255;
        }
    }
}
static int boot_resolution,last_resolution;
static unsigned active_width,active_height;
void qui_set_resolution(int preset,unsigned width,unsigned height){
    boot_resolution=preset;active_width=width;active_height=height;last_state=-1;
}
static void graphics_panel(const QOptions *options,bool saved){
    char label[40];
    text(24,20,"GRAPHICS",181,207,220);
    text(24,54,"RENDER RESOLUTION / PER EYE",255,255,255);
    for(int i=0;i<QRESOLUTION_COUNT;i++){
        bool selected=i==options->resolution;
        snprintf(label,sizeof label,"%c %s",selected?'>':' ',qresolution_labels[i]);
        text(56,86+i*26,label,selected?255:181,selected?202:207,selected?110:220);
    }
    text(24,250,"RIGHT STICK LEFT / RIGHT: SELECT",181,207,220);
    text(24,276,"LEFT CLICK: DEFAULT",181,207,220);
    if(active_width&&active_height)snprintf(label,sizeof label,"ACTIVE: %u X %u",active_width,active_height);
    else snprintf(label,sizeof label,"ACTIVE: DESKTOP WINDOW");
    text(24,308,label,255,255,255);
    text(24,338,options->texture_filtering?"TEXTURE FILTERING: ON":"TEXTURE FILTERING: OFF",255,202,110);
    text(448,338,"LEFT GRIP",181,207,220);
    const char *status="RESOLUTION APPLIES AT APP START";
    if(options->resolution!=boot_resolution)status="RESTART APP TO APPLY RESOLUTION";
    else if(boot_resolution>0&&boot_resolution<QRESOLUTION_COUNT&&active_width&&
            (active_width!=qresolution_sizes[boot_resolution][0]||active_height!=qresolution_sizes[boot_resolution][1]))
        status="PRESET UNSUPPORTED: USING DEFAULT";
    text(24,366,status,255,202,110);
    text(24,394,options->left_handed?"X LEFT: ADD CREDITS":"A RIGHT: ADD CREDITS",181,207,220);
    text(24,426,"LEFT UP/DOWN: CHANGE PAGE",181,207,220);
    text(24,454,saved?"LEFT MENU: RESUME":"SAVING FAILED",saved?170:255,saved?193:150,saved?206:150);
}
static void panel(const QOptions *options,int page,int boot_side,bool saved){
    bool laser=options->laser_enabled,physical_crouch=options->physical_crouch,left_handed=options->left_handed;
    int gun_pitch=options->gun_pitch,world_scale=options->world_scale,view_distance=options->view_distance;
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        uint8_t *p=pixels+(y*WIDTH+x)*4;bool border=x<2||x>=WIDTH-2||y<2||y>=HEIGHT-2;
        p[0]=border?54:12;p[1]=border?183:19;p[2]=border?207:29;p[3]=border?255:240;
    }
    char label[40];
    if(page==QOPTIONS_PAGE_GRAPHICS){graphics_panel(options,saved);return;}
    if(page==1){
        text(24,20,"GUN FIT / PLAYER  2/3",181,207,220);
        snprintf(label,sizeof label,"GUN UP: %+d MM",options->gun_offset_y_mm);text(24,64,label,255,255,255);
        text(24,90,"RIGHT STICK UP / DOWN: ADJUST",181,207,220);
        snprintf(label,sizeof label,"GUN BACK: %+d MM",options->gun_offset_z_mm);text(24,132,label,255,255,255);
        text(24,158,"RIGHT STICK LEFT / RIGHT: ADJUST",181,207,220);
        text(24,190,"LEFT CLICK: RESET BOTH OFFSETS",181,207,220);
        text(24,222,"+UP RAISES / +BACK PULLS TOWARDS YOU",181,207,220);
        snprintf(label,sizeof label,"PLAYER: P%d %s",options->player_side,options->player_side==2?"ROBERT BAXTER":"KEITH MARTIN");text(24,264,label,255,255,255);
        text(24,290,"LEFT STICK LEFT: P1 / RIGHT: P2",181,207,220);
        text(24,322,options->player_side!=boot_side?"RESTART APP TO CHANGE PLAYER":"PLAYER APPLIES AT APP START",255,202,110);
        text(24,364,left_handed?"X LEFT: ADD CREDITS":"A RIGHT: ADD CREDITS",181,207,220);
        text(24,394,options->texture_filtering?"TEXTURE FILTERING: ON":"TEXTURE FILTERING: OFF",255,202,110);text(448,394,"LEFT GRIP",181,207,220);
        text(24,426,"LEFT UP/DOWN: CHANGE PAGE",181,207,220);
        text(24,454,saved?"LEFT MENU: RESUME":"SAVING FAILED",saved?170:255,saved?193:150,saved?206:150);
        return;
    }
    text(24,20,"WORLD / OPTIONS  1/3",181,207,220);
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
    text(336,298,left_handed?"Y: CHANGE":"B: CHANGE",181,207,220);
    text(24,328,physical_crouch?"COVER: PHYSICAL DUCKING":"COVER: GRIP BUTTONS",255,255,255);
    text(24,352,left_handed?"B RIGHT: CHANGE MODE":"Y LEFT: CHANGE MODE",181,207,220);
    text(24,378,options->texture_filtering?"TEXTURE FILTERING: ON":"TEXTURE FILTERING: OFF",255,202,110);text(448,378,"LEFT GRIP",181,207,220);
    text(24,402,left_handed?"A RIGHT: RECENTER / CALIBRATE":"X LEFT: RECENTER / CALIBRATE",181,207,220);
    text(24,426,"LEFT UP/DOWN: CHANGE PAGE",181,207,220);
    text(24,454,saved?"LEFT MENU: RESUME":"SAVING FAILED",saved?170:255,saved?193:150,saved?206:150);
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
void qui_draw(const float view[16],const float projection[16],V3 head,Q4 rotation,const QOptions *options,int page,int boot_side,bool saved){
    if(!program)return;
    GLint bound,unit;glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,texture);
    int state=(options->physical_crouch?1:0)|(options->laser_enabled?2:0)|(saved?4:0)|(options->left_handed?8:0)|(options->tracer_enabled?16:0)|(page<<8)|(options->texture_filtering?64:0);
    if(last_state!=state||last_pitch!=options->gun_pitch||last_scale!=options->world_scale||last_distance!=options->view_distance||last_y!=options->gun_offset_y_mm||last_z!=options->gun_offset_z_mm||last_player!=options->player_side||last_boot!=boot_side||last_resolution!=options->resolution){
        panel(options,page,boot_side,saved);last_resolution=options->resolution;last_pitch=options->gun_pitch;last_scale=options->world_scale;last_distance=options->view_distance;
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
