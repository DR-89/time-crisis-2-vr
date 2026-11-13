/* The pause menu's panel through the real GLES path (quest_ui.c): per-eye stereo, texture refresh on change, every tab. */
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "quest_ui.h"
#include "tc2_online.h"

/* an idle online module */
void tc2_online_get_view(tc2_online_view *v){memset(v,0,sizeof *v);v->side=v->home_side=1;v->voice=true;v->volume=100;v->ping_ms=-1;snprintf(v->name,sizeof v->name,"PLAYER");}
int tc2_online_games(tc2_online_game *out,int max){(void)out;(void)max;return 0;}
void tc2_online_browse(bool open){(void)open;}
void tc2_online_refresh(void){}
void tc2_online_join_game(int i){(void)i;}
void tc2_online_host_room(void){}
void tc2_online_join_code(const char *c){(void)c;}
void tc2_online_quick(void){}
void tc2_online_host_lan(void){}
void tc2_online_set_join_ip(const char *a){(void)a;}
void tc2_online_join_ip(void){}
void tc2_online_set_voice(bool on){(void)on;}
void tc2_online_set_volume(int p){(void)p;}
void tc2_online_set_name(const char *n){(void)n;}
const char *tc2_online_default_server(void){return "zonesync.net";}
const char *tc2_online_set_server(const char *a){(void)a;return NULL;}
void tc2_online_leave(void){}
void tc2_online_rejoin(void){}
void tc2_online_restart_game(void){}
void tc2_online_toggle_mute(void){}
bool tc2_online_push_to_talk(void){return false;}
void tc2_online_talk_key(bool held){(void)held;}
void tc2_online_set_push_to_talk(bool on){(void)on;}
void tc2_online_set_join_credit(bool on){(void)on;}
void tc2_online_menu_shown(bool t){(void)t;}

static int opt[QOPT_COUNT];
static int get(int o){return opt[o];}
static void set(int o,int v){opt[o]=v;}
static void act(int a){(void)a;}
/* 0 GUN left eye, 1 GUN right eye, 2 GUN angle 0 then -30 (one texture, refreshed), 3 GUN angle -30 fresh, 4 PLAYER,
 * 5 GRAPHICS with a preset waiting for a restart, 6 ONLINE, 7 GUN after a failed save, 8 variant 0 as an underlay (the picture's
 * alpha 0 under the panel, 1 elsewhere), 9 the layer's copy of the menu (the whole framebuffer) */
__declspec(dllexport) int options_fixture(int variant,int w,int h,uint8_t *pixels){
    if(!qui_init())return 0;
    QMenuHost host={get,set,act,0,0,1680,1760,false,variant!=7};
    memset(opt,0,sizeof opt);opt[QOPT_HAND]=1;opt[QOPT_WORLD]=100;
    if(variant==3)opt[QOPT_PITCH]=-30;
    if(variant==5)opt[QOPT_RESOLUTION]=3;
    float view[16],proj[16];view_matrix(view,v3(variant==1?.032f:-.032f,0,0),(Q4){0,0,0,1});projection(proj,-.65f,.65f,-.65f,.65f);
    glViewport(0,0,w,h);glClearColor(.08f,.12f,.16f,variant==8?.3f:1);glClear(GL_COLOR_BUFFER_BIT);
    qmenu_open(variant==4?QMENU_TAB_PLAYER:variant==5?QMENU_TAB_GRAPHICS:variant==6?QMENU_TAB_ONLINE:QMENU_TAB_GUN);
    QMenuInput in={0};in.now=variant==8||variant==9?0:variant*10.0;qmenu_update(&host,&in);
    const V3 center=v3(0,0,-1.6f);const Q4 facing={0,0,0,1};
    if(variant==2){
        qui_draw_menu(view,proj,center,facing,&host,in.now,false);
        opt[QOPT_PITCH]=-30;in.now+=1;qmenu_update(&host,&in);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    qui_draw_menu(view,proj,center,facing,&host,in.now,variant==8);
    if(variant==9){glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);if(qui_menu_version())qui_copy_menu();}
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);GLenum error=glGetError();qui_shutdown();return error?-(int)error:1;
}
