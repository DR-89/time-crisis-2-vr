/* The System 23 frame callback is the boundary between the original game and
 * OpenXR. Both eyes reuse one completed geometry snapshot. Only the original
 * CPU advances at 25.6 MHz / (814 * 525); tracking remains at headset cadence. */
#include "tc2_vr.h"
#include "tc2_scene.h"
#include "tc2_host.h"
#include "tc2_weapon.h"
#include "tc2_online.h"
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
    {   /* $TCVR_RAMWATCH=addr[,addr...] (hex, main RAM offsets): log each byte when it changes -- reading the game's state */
        static int n=-1;static uint32_t at[16];static int last[16];
        if(n<0){n=0;const char *e=getenv("TCVR_RAMWATCH");
            while(e&&*e&&n<16){char *end;at[n]=(uint32_t)strtoul(e,&end,16)&0xFFFFFFu;last[n++]=-1;e=*end==','?end+1:end;if(end==e&&*e!=',')break;}}
        for(int i=0;i<n;i++)if(tc2_ram[at[i]]!=last[i]){fprintf(stderr,"[RAM] frame %u %06X: %d -> %d\n",rr_frame,at[i],last[i],tc2_ram[at[i]]);last[i]=tc2_ram[at[i]];}
    }
    {   /* $TCVR_RAMDUMP=<frame>[,<frame>...]: main RAM to ram-<frame>.bin at those frames -- finding the game's state by comparison */
        const char *e=getenv("TCVR_RAMDUMP");
        for(const char *p=e;p&&*p;){char *end;unsigned long f=strtoul(p,&end,10);if(end==p)break;
            if(f==rr_frame){char name[32];snprintf(name,sizeof name,"ram-%u.bin",rr_frame);FILE *o=fopen(name,"wb");
                if(o){fwrite(tc2_ram,1,sizeof tc2_ram,o);fclose(o);}}
            p=*end==','?end+1:end;}
    }
    qvr_scene_prepare(tc2_built_latest_wait());
    if(rr_frame&&rr_frame%300==0)fprintf(stderr,"[PLAYER] frame %u: local slot %u route %u mode %u\n",rr_frame,qtc2_weapon_be32(tc2_ram+0x2cee28),qtc2_weapon_be32(tc2_ram+0x2f3fd4),qtc2_weapon_be32(tc2_ram+0x2f3fd0));
    static uint64_t started;
    if(!started)started=SDL_GetPerformanceCounter();
    if(rr_frame && rr_frame%300==0)fprintf(stderr,"[TC2VR] frame %u elapsed %.3f s\n",rr_frame,(SDL_GetPerformanceCounter()-started)/(double)SDL_GetPerformanceFrequency());
    const char *limit=getenv("TC2_MAX_FRAMES");
    if((limit && rr_frame>=strtoul(limit,NULL,10)) || !ss22_host_frame()) {
        fprintf(stderr,"[TC2VR] normal shutdown at frame %u\n",rr_frame);
        tc2_on_exit();ss22_host_close();exit(0);
    }
    tc2_online_frame();   /* the link's network side, once per arcade frame (docs/ONLINE.md) */
}
/* The desktop host's hooks the core calls (upstream src/tc2_window.c, tc2_out.c, tc2_subs.c, eng/eng_cfg.c). The VR host has no
 * mid-slice present tick, no output listeners, no subtitle table (Angler King only) and no tc2.cfg: every setting is its default. */
void (*tc2_present_hook)(void);
void tc2_out_close(void) {}
void tc2_subs_frame(uint32_t frame) { (void)frame; }
int eng_cfg_int(const char *key,int fallback) { (void)key; return fallback; }
void tc2_out_poll(uint16_t value) {
    /* TC2's solenoid is bit zero (TC1 used bit one). */
    void ss22_out_poll(uint16_t);
    ss22_out_poll((value&1)?2:0);
}
