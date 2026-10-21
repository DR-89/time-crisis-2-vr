/* The System 23 frame callback is the boundary between the original game and
 * OpenXR. Both eyes reuse one completed geometry snapshot. Only the original
 * CPU advances at 25.6 MHz / (814 * 525); tracking remains at headset cadence. */
#include "tc2_vr.h"
#include "tc2_scene.h"
#include "tc2_host.h"
#include "tc2_weapon.h"
#include "tc2_link.h"
#include "tc2_cover_spot.h"
#include "tc2_coop.h"
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
static int opened;
void tc2_crash_box(const char *reason) {
    fprintf(stderr,"[TC2VR] fatal core error: %s\n",reason);fflush(stderr);
#ifdef TCVR_PC
    if(opened&&!getenv("TC2_NO_DIALOG"))SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Time Crisis II VR",reason,NULL);
#endif
}
int tc2_window_open(int scale,int full) {
    (void)scale;(void)full;
    void tc2_render_soft(int);
    tc2_render_soft(0);
    opened=tc2_vr_open();return opened;
}
int tc2_window_active(void) { return opened; }
void tc2_window_frame(void) {
    if(!opened)return;
    extern uint8_t tc2_ram[0x1000000];
    QGunKind weapon=qtc2_weapon_kind(tc2_ram,sizeof tc2_ram);
    if(weapon!=qgun_selected()){
        qgun_select(weapon);
        fprintf(stderr,"[WEAPON] frame %u: %s (arcade player state)\n",rr_frame,weapon==QGUN_MACHINE_GUN?"MACHINE GUN":"PISTOL");
    }
    qvr_scene_prepare(tc2_built_latest_wait());
    if(rr_frame&&rr_frame%300==0)fprintf(stderr,"[PLAYER] frame %u: local slot %u route %u mode %u\n",rr_frame,qtc2_weapon_be32(tc2_ram+0x2cee28),qtc2_weapon_be32(tc2_ram+0x2f3fd4),qtc2_weapon_be32(tc2_ram+0x2f3fd0));
    {   /* RAM read at vblank F describes the camera rendered at F+1: publish
         * the previous read with the frame it belongs to. */
        extern uint8_t tc2_data[0x2000000];static QCoverSpot pending;
        qvr_set_cover_spot(&pending);qtc2_cover_spot(tc2_ram,sizeof tc2_ram,tc2_data,sizeof tc2_data,&pending);
    }
    static uint64_t started;
    if(!started)started=SDL_GetPerformanceCounter();
    if(rr_frame && rr_frame%300==0)fprintf(stderr,"[TC2VR] frame %u elapsed %.3f s\n",rr_frame,(SDL_GetPerformanceCounter()-started)/(double)SDL_GetPerformanceFrequency());
    const char *limit=getenv("TC2_MAX_FRAMES");
    if((limit && rr_frame>=strtoul(limit,NULL,10)) || !ss22_host_frame()) {
        fprintf(stderr,"[TC2VR] normal shutdown at frame %u\n",rr_frame);
#ifdef TCVR_STEAM_FRAME
        fprintf(stderr,"[TC2VR] host requested shutdown; unwinding lifted core\n");
        extern void tc2_frame_request_shutdown(void);
        tc2_frame_request_shutdown();
#else
        tc2_on_exit();ss22_host_close();exit(0);
#endif
    }
    {   /* Shooting the crossed-out SOLO/LINKED on the arcade's mode screen opens
         * LAN Play; the ROM itself ignores that shot while no partner is linked. */
        static uint16_t previous;uint16_t inputs=tc2_inputs;
        if(!tc2_link_enabled()&&qtc2_coop_chosen(tc2_ram,sizeof tc2_ram,inputs,previous)){
            fprintf(stderr,"[LAN] SOLO/LINKED chosen on the arcade mode screen: opening LAN Play\n");
            qvr_open_lan_from_arcade();
        }
        previous=inputs;
    }
    if(tc2_link_take_restart()){void tc2_machine_restart(void);ss22_input_neutral();tc2_machine_restart();}
}
void tc2_out_poll(uint16_t value) {
    /* TC2's solenoid is bit zero (TC1 used bit one). */
    void ss22_out_poll(uint16_t);
    ss22_out_poll((value&1)?2:0);
}
