/* Only host presentation/input callbacks are replaced. CPUs, chips, sound
 * synthesis, geometry and software rendering are the production upstream. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include "tc2_link.h"
uint16_t tc2_inputs, g_ss22_gun_x=457, g_ss22_gun_y=161;
bool g_ss22_gun_off;
int tc2_window_active(void) { return getenv("TC2_SESSION")!=NULL; }
int tc2_window_open(int scale, int fs) { (void)scale; (void)fs; return 0; }
static uint64_t wall_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void session_gate(uint32_t frame){
    static int initialized,games,solo;
    const char *role=getenv("TC2_SESSION");if(!role)return;
    uint64_t now=wall_ms();tc2_link_service(now);
    if(!initialized){initialized=1;if(!strcmp(role,"host"))tc2_link_host();else tc2_link_join(role);}
    const TC2Session *s=tc2_link_session();
    if((s->state==TC2_PLAYING && frame>=4000)||(s->state==TC2_ERROR && tc2_link_enabled() && frame>=3900)){
        fprintf(stderr,"[SESSIONGATE] game %d completed at frame %u\n",++games,frame);
        tc2_link_report();tc2_link_leave();
        if(games>=2)solo=1;
        else if(!strcmp(role,"host"))tc2_link_host();else tc2_link_join(role);
    }
    uint64_t end=wall_ms()+20000;
    while(!solo && tc2_link_session()->state!=TC2_PLAYING){
        tc2_link_service(wall_ms());s=tc2_link_session();
        if(s->state==TC2_ERROR){
            /* A reconnecting client may reach the host before it has reset. */
            if(!strcmp(role,"host")){fprintf(stderr,"[SESSIONGATE] %s\n",s->message);exit(7);}
            struct timespec wait={0,200000000};nanosleep(&wait,0);tc2_link_join(role);
        }
        if(s->state==TC2_LOBBY){if(!s->local_ready)tc2_link_ready();if(s->host&&s->peer_ready)tc2_link_start();}
        if(wall_ms()>end){fprintf(stderr,"[SESSIONGATE] lobby timeout\n");exit(7);}
        struct timespec wait={0,1000000};nanosleep(&wait,0);
    }
    if(tc2_link_take_restart()){void tc2_machine_restart(void);tc2_machine_restart();}
}
void tc2_window_frame(void) {
    extern uint32_t rr_frame;
    session_gate(rr_frame);
    static uint32_t previous_frame;
    static int initialized, pace, skew, jitter;
    static double start;
    struct timespec now;
    if(!initialized) {
        initialized=1; pace=getenv("TC2_PACE")!=NULL;
        skew=getenv("TC2_SKEW_MS")?atoi(getenv("TC2_SKEW_MS")):0;
        jitter=getenv("TC2_JITTER_MS")?atoi(getenv("TC2_JITTER_MS")):0;
        clock_gettime(CLOCK_MONOTONIC,&now); start=now.tv_sec+now.tv_nsec/1e9;
    }
    if(rr_frame<previous_frame){clock_gettime(CLOCK_MONOTONIC,&now);start=now.tv_sec+now.tv_nsec/1e9;}
    previous_frame=rr_frame;
    if(pace) {
        double target=start+rr_frame/59.906+skew/1000.0+(jitter?((rr_frame*17)%7)*jitter/6000.0:0);
        clock_gettime(CLOCK_MONOTONIC,&now); double delay=target-(now.tv_sec+now.tv_nsec/1e9);
        if(delay>0) { struct timespec wait={(time_t)delay,(long)((delay-(time_t)delay)*1e9)}; while(nanosleep(&wait,&wait)&&errno==EINTR){} }
    }
    static int resets;
    const char *reset_frame=getenv("TC2_RESET_FRAME");
    if(reset_frame && rr_frame>=strtoul(reset_frame,0,10) && resets<2) {
        void tc2_machine_restart(void); resets++;
        fprintf(stderr,"[RESET] cold restart %d\n",resets); tc2_machine_restart();
    }
    const char *s=getenv("TC2_FRAMES");
    if (s && rr_frame >= strtoul(s, 0, 10) && (!getenv("TC2_SESSION") || tc2_link_session()->state==TC2_OFF)) {
        void tc2_on_exit(void); tc2_on_exit(); exit(0);
    }
}
void tc2_crash_box(const char *s) { fprintf(stderr,"%s\n",s); }
void tc2_audio_push(const int16_t *samples, int n) { (void)samples; (void)n; }
void tc2_out_poll(uint16_t v) { (void)v; }
void tc2_gl_read(uint32_t *rgba) { (void)rgba; abort(); }
