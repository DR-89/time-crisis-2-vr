/* Exercise the generated Frame shutdown and cabinet-restart checkpoints. */
#define main tc2_generated_game_main
#include "tc2_main.c"
#undef main

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "tc2_build.h"
#include "tc2_link.h"
#include "quest_gun.h"
#include "tc2_scene.h"
#include "tc2_vr.h"
int SDL_main(int argc,char **argv);

uint8_t R[RR_REGSPACE];
uint8_t tc2_ram[0x1000000];
uint32_t rr_frame,rr_n_traps,rr_n_fpe;
uint64_t rr_n_irq;
unsigned long tc2_render_frames,tc2_render_models,tc2_render_immediate,tc2_c435_unknown,tc2_render_polys;
unsigned long tc2_env_hits,tc2_env_miss;
static int backup_saves,io_flushes,link_reports,h8_reports;
static int host_frame_calls,host_frame_closes;
static int wrapper_game_calls;
int tc2_window_open(int scale,int fullscreen);
int tc2_window_active(void);
void tc2_window_frame(void);

void tc2_backup_save(int force){(void)force;backup_saves++;}
void tc2_io_flush(void){io_flushes++;}
void tc2_link_report(void){link_reports++;}
void tc2_h8_report(void){h8_reports++;}
void tc2_render_soft(int full){(void)full;}
bool tc2_vr_open(void){return true;}
bool ss22_host_frame(void){host_frame_calls++;return false;}
void ss22_host_close(void){host_frame_closes++;}
const tc2_built *tc2_built_latest_wait(void){return NULL;}
void qvr_scene_prepare(const tc2_built *frame){(void)frame;}
void qtc2_cabinet_configure(const char *path){(void)path;}
void qvr_set_cover_spot(const struct QCoverSpot *spot){(void)spot;}
void qvr_open_lan_from_arcade(void){}
uint16_t tc2_inputs; /* the window hook reads the arcade inputs for the SOLO/LINKED shot */
int tc2_link_enabled(void){return 0;}
uint8_t tc2_data[0x2000000]; /* the cover-slide reader maps ROM tables here */
void qgun_select(QGunKind kind){(void)kind;}
QGunKind qgun_selected(void){return QGUN_PISTOL;}
int tc2_link_take_restart(void){return 0;}
void ss22_input_neutral(void){}
void ss22_out_poll(uint16_t value){(void)value;}
Uint64 SDL_GetPerformanceCounter(void){return 1;}
Uint64 SDL_GetPerformanceFrequency(void){return 1;}
int tc2_test_chdir(const char *path){assert(!strcmp(path,"/sdcard/Documents/TimeCrisis2VR"));return 0;}
int __android_log_print(int priority,const char *tag,const char *format,...){(void)priority;(void)tag;(void)format;return 0;}
int tc_game_main(int argc,char **argv){assert(argc==2&&argv&&!strcmp(argv[0],"TimeCrisis2VR")&&!strcmp(argv[1],"--window"));wrapper_game_calls++;return 0;}
int SDL_ShowSimpleMessageBox(Uint32 flags,const char *title,const char *message,SDL_Window *window){(void)flags;(void)title;(void)message;(void)window;return -1;}

static int run_shutdown_checkpoint(void){
    if(TC2_FRAME_SHUTDOWN_CHECKPOINT())TC2_FRAME_SHUTDOWN_CATCH();
    frame_shutdown_armed=1;
    assert(tc2_window_open(0,0));
    tc2_window_frame();
    return -1;
}
static int run_restart_checkpoint(void){
    const int restart=setjmp(power_on_return);
    if(restart)return restart;
    restart_armed=1;
    const int shutdown=TC2_FRAME_SHUTDOWN_CHECKPOINT();
    if(shutdown){frame_shutdown_armed=0;return 100+shutdown;}
    frame_shutdown_armed=1;
    tc2_machine_restart();
    return -1;
}

int main(void){
    unsetenv("TC2_MAX_FRAMES");
    assert(run_shutdown_checkpoint()==0);
    assert(host_frame_calls==1&&host_frame_closes==0);
    assert(tc2_window_active());
    assert(backup_saves==1&&io_flushes==1&&link_reports==1&&h8_reports==1);
    assert(!frame_shutdown_armed);
    assert(SDL_main(0,NULL)==0);
    assert(wrapper_game_calls==1&&host_frame_closes==1);
    assert(run_restart_checkpoint()==1);
    assert(!frame_shutdown_armed);
    assert(backup_saves==1&&io_flushes==1&&link_reports==1&&h8_reports==1);
    puts("PASS: production Frame host-false path unwinds through generated core, SDL_main closes host once and returns; cabinet restart uses its separate power-on checkpoint");
    return 0;
}
