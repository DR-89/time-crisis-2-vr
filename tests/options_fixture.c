#include <GLES3/gl3.h>
#include <stdint.h>
#include "quest_ui.h"
static void legacy_view(const float *v,const float *p,V3 head,Q4 rotation,bool laser,bool physical,bool left,int pitch,int scale,int distance,bool saved){
    QOptions o={.laser_enabled=laser,.physical_crouch=physical,.left_handed=left,.gun_pitch=pitch,.world_scale=scale,.view_distance=distance,.player_side=1};
    qui_draw(v,p,head,rotation,&o,0,1,saved);
}
#define qui_draw legacy_view
__declspec(dllexport) int options_fixture(int variant,int w,int h,uint8_t *pixels){
    if(!qui_init())return 0;
    float view[16],proj[16];view_matrix(view,v3(variant==3||variant==7?.032f:-.032f,0,0),(Q4){0,0,0,1});projection(proj,-.65f,.65f,-.65f,.65f);
    glViewport(0,0,w,h);glClearColor(.08f,.12f,.16f,1);glClear(GL_COLOR_BUFFER_BIT);
    bool physical=(variant>=2&&variant<=4)||variant==6||variant==7;
    qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},variant>=1,physical,variant>=5,variant==9?-45:variant==10?60:0,variant==12?25:variant==13?400:100,variant==15?100:40,variant!=4);
    if(variant==8)qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},true,false,false,0,100,40,true);
    if(variant==11){glClear(GL_COLOR_BUFFER_BIT);qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},true,false,true,-45,100,40,true);}
    if(variant==14){glClear(GL_COLOR_BUFFER_BIT);qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},true,false,true,0,400,40,true);}
    if(variant==16){glClear(GL_COLOR_BUFFER_BIT);qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},true,false,true,0,100,100,true);}
#undef qui_draw
    if(variant>=17){
        QOptions o={.world_scale=100,.view_distance=0,.player_side=1,.tracer_enabled=true};
        int page=variant>=18;
        if(variant>=19){o.gun_offset_y_mm=20;o.gun_offset_z_mm=40;o.player_side=2;}
        glClear(GL_COLOR_BUFFER_BIT);qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},&o,page,1,true);
        if(variant==20){o.gun_offset_y_mm=-200;o.gun_offset_z_mm=200;}
        glClear(GL_COLOR_BUFFER_BIT);qui_draw(view,proj,v3(0,0,0),(Q4){0,0,0,1},&o,page,1,true);
    }
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);GLenum error=glGetError();qui_shutdown();return error?-(int)error:1;
}
