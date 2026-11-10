/* System 23 OpenXR host, adapted from Time Crisis II VR v0.8.4. */
#ifdef TCVR_PC
#include <windows.h>
#include <unknwn.h>
#include <SDL2/SDL_syswm.h>
#else
#include <jni.h>
#include <EGL/egl.h>
#endif
#include "quest_gpu.h"
#include "quest_color.h"
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include "quest_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "tc2_vr.h"
#include "quest_gl.h"
#include "tc2_scene.h"
#include "quest_gun.h"
#include "quest_options.h"
#include "quest_cover.h"
#include "quest_clock.h"
#include "quest_refresh.h"
#include "quest_ui.h"
#include "quest_tracer.h"
#include "tc2_cabinet.h"
#include "tc2_online.h"

#ifdef TCVR_PC
typedef XrSwapchainImageOpenGLKHR QSwapchainImage;
#define Q_SWAPCHAIN_IMAGE XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR
static bool desktop;
#else
typedef XrSwapchainImageOpenGLESKHR QSwapchainImage;
#define Q_SWAPCHAIN_IMAGE XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR
#endif
static SDL_Window *window;
static SDL_GLContext context;
static bool gpu_ready;
static XrInstance instance;
static XrSession session;
enum { LEFT_HAND,RIGHT_HAND,HAND_COUNT };
static XrPath hand_paths[HAND_COUNT];
static XrSpace local_space,aim_spaces[HAND_COUNT];
static XrActionSet action_set;
static XrAction aim_action,trigger_action,grip_action,lower_action,upper_action,pause_action,haptic_action,hand_action,pitch_action,pitch_reset_action;
static XrSessionState state;
static bool running,quit,active,focused,paused,origin_set,recenter_requested;
static bool multiview;
typedef struct ButtonEdge { bool synced,was; } ButtonEdge;
static ButtonEdge coin_button,recenter_button,pause_button,laser_button,cover_button,hand_button,pitch_reset_button;
static XrAction distance_action;
typedef struct FireTrigger { bool armed,pressed; } FireTrigger;
static FireTrigger fire_triggers[HAND_COUNT];
static bool shot_pending,shot_needs_release,fire_output_down,options_saved=true;
static int active_weapon_hand=RIGHT_HAND;
static QOptions options;
static QCover cover;
static bool cover_calibration_pending=true;
static const char *options_path="quest-options.cfg";
static XrTime game_deadline;
static XrTime last_display_time;
static bool last_tracking;
static PFN_xrPerfSettingsSetPerformanceLevelEXT set_performance;
#ifndef TCVR_PC
static PFN_xrSetAndroidApplicationThreadKHR set_thread;
#endif
static PFN_xrEnumerateDisplayRefreshRatesFB enumerate_rates;
static PFN_xrRequestDisplayRefreshRateFB request_rate;
static PFN_xrGetDisplayRefreshRateFB get_rate;
static float preferred_rate;
static unsigned rate_attempts;
static XrTime rate_check_time;
static XrView views[2];
static V3 origin,gun_origin,gun_hit,gun_position;
static Q4 origin_rotation={0,0,0,1},gun_rotation={0,0,0,1};
static float nx=.5f,ny=.5f,trigger,pedal;
static bool aim_valid,gun_tracked;
static uint64_t recoil_started;
static QTracer tracer;
static bool replay_input;
static int coin_frames;
static int boot_player_side=1,boot_resolution;static unsigned active_w,active_h;   /* what this run started with */

static struct Eye { XrSwapchain chain;uint32_t w,h,n;QSwapchainImage *images;GLuint *fb,depth; } eyes[2];
/* The pause menu as its own composition layer, under the game's picture: the headset draws it straight to the display, at the
 * menu's own resolution, instead of from the eye image (on a Quest that is about half the menu's resolution, and resampled once
 * more). The picture lets it through where the menu is (alpha 0) and covers it elsewhere, so the laser still crosses it. The
 * menu is still drawn into the picture too: a desktop mirror, screenshots, and the fallback if the layer fails. */
static struct MenuLayer { XrSwapchain chain;uint32_t n,mips;QSwapchainImage *images;GLuint *fb;unsigned version;bool ok; } menu_layer;
static void menu_layer_close(void){
    if(menu_layer.fb&&context&&gpu_ready)glDeleteFramebuffers(menu_layer.n,menu_layer.fb);
    if(menu_layer.chain)xrDestroySwapchain(menu_layer.chain);
    free(menu_layer.images);free(menu_layer.fb);memset(&menu_layer,0,sizeof menu_layer);
}
static void menu_layer_open(int64_t format){
    const char *off=getenv("TCVR_MENU_LAYER");if(off&&!strcmp(off,"0"))return;
    uint32_t mips=1;while((QMENU_W>>mips)>0||(QMENU_H>>mips)>0)mips++;
    for(int attempt=0;attempt<2&&!menu_layer.chain;attempt++){   /* with smaller copies (no shimmer from afar), else without */
        XrSwapchainCreateInfo cc={XR_TYPE_SWAPCHAIN_CREATE_INFO};cc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        cc.format=format;cc.sampleCount=1;cc.width=QMENU_W;cc.height=QMENU_H;cc.faceCount=1;cc.arraySize=1;cc.mipCount=attempt?1:mips;
        if(XR_SUCCEEDED(xrCreateSwapchain(session,&cc,&menu_layer.chain)))menu_layer.mips=cc.mipCount;else menu_layer.chain=XR_NULL_HANDLE;
    }
    if(!menu_layer.chain||XR_FAILED(xrEnumerateSwapchainImages(menu_layer.chain,0,&menu_layer.n,NULL))||!menu_layer.n){
        fprintf(stderr,"[XR] menu layer unavailable; the menu is drawn into the picture\n");menu_layer_close();return;
    }
    menu_layer.images=calloc(menu_layer.n,sizeof *menu_layer.images);menu_layer.fb=calloc(menu_layer.n,sizeof(GLuint));
    if(!menu_layer.images||!menu_layer.fb){menu_layer_close();return;}
    for(uint32_t j=0;j<menu_layer.n;j++)menu_layer.images[j].type=Q_SWAPCHAIN_IMAGE;
    if(XR_FAILED(xrEnumerateSwapchainImages(menu_layer.chain,menu_layer.n,&menu_layer.n,(XrSwapchainImageBaseHeader*)menu_layer.images))){menu_layer_close();return;}
    glGenFramebuffers(menu_layer.n,menu_layer.fb);
    for(uint32_t j=0;j<menu_layer.n;j++){
        glBindFramebuffer(GL_FRAMEBUFFER,menu_layer.fb[j]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,menu_layer.images[j].image,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,0);menu_layer_close();return;}
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);menu_layer.ok=true;
    fprintf(stderr,"[XR] menu layer %dx%d, %u mip levels\n",QMENU_W,QMENU_H,menu_layer.mips);
}
/* Copies the menu into the layer's next image when it changed. False if that failed: the layer is dropped for good. */
static bool menu_layer_update(void){
    const unsigned version=qui_menu_version();
    if(!version)return false;
    if(version==menu_layer.version)return true;
    uint32_t index;XrSwapchainImageAcquireInfo acq={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(xrAcquireSwapchainImage(menu_layer.chain,&acq,&index))||XR_FAILED(xrWaitSwapchainImage(menu_layer.chain,&wait))){
        fprintf(stderr,"[XR] menu layer failed; the menu is drawn into the picture\n");menu_layer.ok=false;return false;
    }
    GLint fb;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);
    glBindFramebuffer(GL_FRAMEBUFFER,menu_layer.fb[index]);glViewport(0,0,QMENU_W,QMENU_H);qui_copy_menu();
    if(menu_layer.mips>1){
        GLint bound;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,menu_layer.images[index].image);
        glGenerateMipmap(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,bound);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,fb);
    XrSwapchainImageReleaseInfo rel={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(XR_FAILED(xrReleaseSwapchainImage(menu_layer.chain,&rel))){menu_layer.ok=false;return false;}
    menu_layer.version=version;return true;
}
static double profile_parts[6],profile_thread;
static double cpu_ms(void){
#ifdef TCVR_PC
    FILETIME created,exited,kernel,user;ULARGE_INTEGER k,u;
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
    k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return (k.QuadPart+u.QuadPart)/10000.;
#else
    struct timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;
#endif
}
static double wall_ms(void){return SDL_GetPerformanceCounter()*1000.0/SDL_GetPerformanceFrequency();}

static bool xr_ok(XrResult result,const char *operation) {
    if(XR_SUCCEEDED(result))return true;
    char error[XR_MAX_RESULT_STRING_SIZE];snprintf(error,sizeof error,"%d",result);
    if(instance)xrResultToString(instance,result,error);
    fprintf(stderr,"[XR] %s: %s\n",operation,error);
    __android_log_print(ANDROID_LOG_ERROR,"TCVR","%s: %s",operation,error);return false;
}
#define XR(call) xr_ok((call),#call)
#define REQUIRE(call) do { if(!XR(call))goto fail; } while(0)
static void display_rate_request(void){
    if(!request_rate||preferred_rate<=0)return;
    rate_attempts++;
    XrResult result=request_rate(session,preferred_rate);
    fprintf(stderr,"[XR] display request %.0f Hz attempt %u: %s\n",preferred_rate,rate_attempts,XR(result)?"accepted (not yet confirmed)":"rejected");
}
static void display_rate_check(XrTime now){
    if(!focused||!get_rate||preferred_rate<=0||rate_attempts>3)return;
    if(!rate_check_time){rate_check_time=now+2000000000LL;return;}
    if(now<rate_check_time)return;
    float actual=0;
    if(XR(get_rate(session,&actual))){
        fprintf(stderr,"[XR] display verified %.0f Hz; requested %.0f Hz\n",actual,preferred_rate);
        if(fabsf(actual-preferred_rate)<.1f){rate_attempts=4;return;}
    }
    if(rate_attempts<3)display_rate_request();
    else{fprintf(stderr,"[XR] runtime retained its display rate; no further requests this focus session\n");rate_attempts=4;}
    rate_check_time=now+2000000000LL;
}
static void performance_start(void){
#ifndef TCVR_PC
    if(set_thread){
        XR(set_thread(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,(uint32_t)gettid()));
        XR(set_thread(session,XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,(uint32_t)gettid()));
    }
#endif
    if(set_performance){
        XR(set_performance(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
        XR(set_performance(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
    }
    rate_attempts=0;rate_check_time=0;display_rate_request();
    float actual=0;if(get_rate&&XR(get_rate(session,&actual)))fprintf(stderr,"[XR] display requested %.0f Hz, current %.0f Hz\n",preferred_rate,actual);
}
static void frame_profile(uint64_t start,XrDuration period,bool visible){
    static double sum,maximum;static unsigned count,over;
    if(!visible){sum=maximum=0;count=over=0;return;}
    double ms=(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency();
    sum+=ms;if(ms>maximum)maximum=ms;if(ms>period/1e6)over++;
    if(++count==240){
        fprintf(stderr,"[XRPERF] render wall mean %.2f ms max %.2f ms over-budget %u/%u period %.2f ms\n",sum/count,maximum,over,count,period/1e6);
        fprintf(stderr,"[XRPERF] runtime frame cadence %.2f Hz (display rate is logged separately)\n",period>0?1e9/period:0);
        fprintf(stderr,"[XRSTAGE] input %.2f acquire %.2f world %.2f overlays %.2f release %.2f end %.2f thread %.2f ms\n",profile_parts[0]/count,profile_parts[1]/count,profile_parts[2]/count,profile_parts[3]/count,profile_parts[4]/count,profile_parts[5]/count,profile_thread/count);
        memset(profile_parts,0,sizeof profile_parts);profile_thread=0;
        sum=maximum=0;count=over=0;
    }
}
static XrPath path(const char *s){XrPath p=0;xrStringToPath(instance,s,&p);return p;}
static int weapon_hand(void){return active_weapon_hand;}
static void reset_fire_input(void){
    tracer.active=false;
    memset(fire_triggers,0,sizeof fire_triggers);shot_pending=shot_needs_release=fire_output_down=false;
}
/* ---- the pause menu (quest_menu.c draws it, quest_ui.c hangs it in the world) ----
 * It appears where the player looks when it opens and stays there. The gun's laser points at it and the trigger presses; the
 * sticks, A / B and the grips work too (quest_menu.h). The trigger must be released once before it presses anything. */
static bool menu_placed,menu_laser,restart_app;
static V3 menu_center,menu_end;static Q4 menu_rotation={0,0,0,1};
static bool menu_trigger_armed[HAND_COUNT],menu_trigger_held,menu_grip_armed[HAND_COUNT];
static int kb_nav_x,kb_nav_y,kb_tab;static bool kb_select,kb_back;   /* the keyboard (events) */
static void reset_menu_input(void){
    memset(menu_trigger_armed,0,sizeof menu_trigger_armed);memset(menu_grip_armed,0,sizeof menu_grip_armed);menu_trigger_held=false;
    kb_nav_x=kb_nav_y=kb_tab=0;kb_select=kb_back=false;
}
/* Opens (on a tab, or -1: the last one) or closes the menu. */
/* The game's sound: silent while the menu holds an offline game or the headset app is in the background, else the player's
 * volume (AUDIO tab). Voice chat is mixed in after it and keeps its own volume. */
static bool skipping;
static void apply_game_volume(void){
    bool silent=skipping||(paused&&!tc2_online_session());
#ifdef TCVR_PC
    if(!desktop)
#endif
    if(!focused)silent=true;
    tc2_audio_volume(silent?0:options.game_volume);
}
static void set_paused(bool p,int tab){
    if(p&&(!paused||tab>=0))qmenu_open(tab);
    if(p&&!paused)menu_placed=false;
    paused=p;reset_fire_input();reset_menu_input();game_deadline=0;
    apply_game_volume();ss22_input_neutral();
}
/* SKIPPING THE START-UP (PLAYER tab, off by default): a shot during the arcade's own start-up (tc2_online_startup_left: after power-on the memory test, the link
 * countdown, the parental advisory and the logo; after a TEST restart the last two) runs the arcade as fast as the device allows to
 * its end, silent. The arcade runs the same frames with the same inputs, only sooner. The menu or the app losing focus stops it. */
static bool skip_test;                    /* the test script's "skip": the shot, without a hand on the trigger */
static int skip_left;static Uint64 skip_started,skip_until;
static void skip_cpu(bool boost){        /* the runtime's BOOST level is meant for short loads like this one */
    if(set_performance&&session!=XR_NULL_HANDLE)XR(set_performance(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,boost?XR_PERF_SETTINGS_LEVEL_BOOST_EXT:XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
}
static bool skip_update(void){            /* every host loop iteration: starts or ends the skip; true while it runs */
    const int left=tc2_online_startup_left();
    bool held=paused||!focused;
#ifdef TCVR_PC
    if(desktop)held=paused;               /* the desktop window runs on without focus */
#endif
    if(!skipping&&left>0&&!held&&(trigger>.55f||skip_test)){
        skipping=true;skip_left=left;skip_started=SDL_GetPerformanceCounter();skip_until=0;apply_game_volume();tc2_online_startup_skipping();skip_cpu(true);
        fprintf(stderr,"[BOOT] skipping the start-up: %d frames\n",left);
    }
    else if(skipping&&(left<=0||held)){
        skipping=false;apply_game_volume();skip_cpu(false);
        const double ms=(SDL_GetPerformanceCounter()-skip_started)*1000.0/SDL_GetPerformanceFrequency();
        fprintf(stderr,"[BOOT] start-up %s: %d frames in %.0f ms (%.1fx)\n",left<=0?"skipped":"skip stopped",skip_left-(left>0?left:0),ms,
                ms>0?(skip_left-(left>0?left:0))*1000.0/59.906/ms:0);
    }
    skip_test=false;
    return skipping;
}
/* The game side opens or closes the ONLINE tab (tc2_online.c): a shot at the arcade's crossed-out SOLO/LINKED opens it, the
 * partner linking closes it again. */
void qhost_online_menu(bool open){
    if(open){if(!(paused&&qmenu_tab()==QMENU_TAB_ONLINE))set_paused(true,QMENU_TAB_ONLINE);}
    else if(paused&&qmenu_tab()==QMENU_TAB_ONLINE)set_paused(false,-1);
}
bool qhost_online_menu_open(void){return paused&&qmenu_tab()==QMENU_TAB_ONLINE;}
static void reset_input_edges(void){
    coin_button=recenter_button=pause_button=laser_button=cover_button=hand_button=pitch_reset_button=(ButtonEdge){0};
    reset_menu_input();
    reset_fire_input();
}
static void select_weapon_hand(int hand){
    if(hand==active_weapon_hand)return;
    if(session&&running){
        XrHapticActionInfo info={XR_TYPE_HAPTIC_ACTION_INFO};info.action=haptic_action;info.subactionPath=hand_paths[weapon_hand()];
        xrStopHapticFeedback(session,&info);
    }
    active_weapon_hand=hand;recoil_started=0;tracer.active=false;aim_valid=gun_tracked=false;
}
static void change_weapon_hand(void){
    options.left_handed=!options.left_handed;options_saved=qoptions_save(options_path,&options);
    select_weapon_hand(options.left_handed?LEFT_HAND:RIGHT_HAND);
    reset_input_edges();recoil_started=0;coin_frames=0;aim_valid=gun_tracked=false;ss22_input_neutral();
    fprintf(stderr,"[OPTIONS] weapon hand %s; saved %d\n",options.left_handed?"LEFT":"RIGHT",options_saved);
}
static void toggle_texture_filter(void){
    options.texture_filtering=!options.texture_filtering;
    tc2_gl_set_filter(options.texture_filtering);
    options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] texture filtering %s; saved %d\n",options.texture_filtering?"ON":"OFF",options_saved);
}
static void set_resolution(int preset){
    if(preset<0||preset>=QRESOLUTION_COUNT||preset==options.resolution)return;
    options.resolution=preset;options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] resolution %s per eye; saved %d; restart to apply\n",qresolution_labels[preset],options_saved);
}
static void set_gun_pitch(int value){
    if(value<QOPTIONS_GUN_PITCH_MIN)value=QOPTIONS_GUN_PITCH_MIN;
    if(value>QOPTIONS_GUN_PITCH_MAX)value=QOPTIONS_GUN_PITCH_MAX;
    if(value==options.gun_pitch)return;
    options.gun_pitch=value;options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] gun angle %+d deg; saved %d\n",value,options_saved);
}
static void set_world_scale(int value){
    if(value<QOPTIONS_WORLD_SCALE_MIN)value=QOPTIONS_WORLD_SCALE_MIN;
    if(value>QOPTIONS_WORLD_SCALE_MAX)value=QOPTIONS_WORLD_SCALE_MAX;
    if(value==options.world_scale)return;
    options.world_scale=value;qvr_world_scale=value/100.f;
    options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] world size %d%%; saved %d\n",value,options_saved);
}
static void set_view_distance(int value){
    if(value<QOPTIONS_VIEW_DISTANCE_MIN)value=QOPTIONS_VIEW_DISTANCE_MIN;
    if(value>QOPTIONS_VIEW_DISTANCE_MAX)value=QOPTIONS_VIEW_DISTANCE_MAX;
    if(value==options.view_distance)return;
    options.view_distance=value;qvr_view_distance=value/100.f;
    options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] view distance %d cm; saved %d\n",value,options_saved);
}
static void cycle_aim_guide(void){
    if(options.laser_enabled){options.laser_enabled=false;options.tracer_enabled=false;}
    else if(options.tracer_enabled){options.tracer_enabled=false;options.laser_enabled=true;}
    else options.tracer_enabled=true;
    tracer.active=false;options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] aiming %s; saved %d\n",qoptions_aim_label(&options),options_saved);
}
static void set_gun_offset(bool vertical,int value){
    if(value<QOPTIONS_GUN_OFFSET_MIN)value=QOPTIONS_GUN_OFFSET_MIN;
    if(value>QOPTIONS_GUN_OFFSET_MAX)value=QOPTIONS_GUN_OFFSET_MAX;
    int *field=vertical?&options.gun_offset_y_mm:&options.gun_offset_z_mm;
    if(*field==value)return;*field=value;tracer.active=false;
    options_saved=qoptions_save(options_path,&options);
}
static void set_player_side(int side){
    if(side<1||side>2||side==options.player_side)return;
    options.player_side=side;options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] P%d selected for next app start; current P%d\n",side,qtc2_cabinet_side);
}
static void set_aim_guide(int mode){   /* 0 laser, 1 tracer, 2 off */
    const bool laser=mode==0,tracer_on=mode==1;
    if(options.laser_enabled==laser&&options.tracer_enabled==tracer_on)return;
    options.laser_enabled=laser;options.tracer_enabled=tracer_on;tracer.active=false;options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] aiming %s; saved %d\n",qoptions_aim_label(&options),options_saved);
}
static void set_cover_mode(bool physical){
    if(options.physical_crouch==physical)return;
    options.physical_crouch=physical;options_saved=qoptions_save(options_path,&options);
    if(physical)cover_calibration_pending=true;
    fprintf(stderr,"[OPTIONS] cover %s; saved %d\n",physical?"PHYSICAL":"GRIP",options_saved);
}
static void set_game_volume(int value){
    if(value<QOPTIONS_GAME_VOLUME_MIN)value=QOPTIONS_GAME_VOLUME_MIN;
    if(value>QOPTIONS_GAME_VOLUME_MAX)value=QOPTIONS_GAME_VOLUME_MAX;
    if(value==options.game_volume)return;
    options.game_volume=value;apply_game_volume();
    options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] game volume %d%%; saved %d\n",value,options_saved);
}
static void set_skip_startup(bool on){
    if(on==options.skip_startup)return;
    options.skip_startup=on;tc2_online_set_startup_skip(on);
    options_saved=qoptions_save(options_path,&options);
    fprintf(stderr,"[OPTIONS] skip start-up %s; saved %d\n",on?"on":"off",options_saved);
}
static int menu_get(int o){
    switch(o){
    case QOPT_HAND:return options.left_handed?0:1;
    case QOPT_AIM:return options.laser_enabled?0:options.tracer_enabled?1:2;
    case QOPT_PITCH:return options.gun_pitch;
    case QOPT_GUN_Y:return options.gun_offset_y_mm;
    case QOPT_GUN_Z:return options.gun_offset_z_mm;
    case QOPT_PLAYER:return options.player_side-1;
    case QOPT_COVER:return options.physical_crouch;
    case QOPT_WORLD:return options.world_scale;
    case QOPT_DISTANCE:return options.view_distance;
    case QOPT_RESOLUTION:return options.resolution;
    case QOPT_FILTER:return options.texture_filtering?0:1;
    case QOPT_GAME_VOLUME:return options.game_volume;
    case QOPT_SKIP_STARTUP:return options.skip_startup;
    case QOPT_VOICE_VOLUME:{tc2_online_view view;tc2_online_get_view(&view);return view.volume;}
    }
    return 0;
}
static void menu_set(int o,int v){
    switch(o){
    case QOPT_HAND:if((v==0)!=options.left_handed)change_weapon_hand();break;
    case QOPT_AIM:set_aim_guide(v);break;
    case QOPT_PITCH:set_gun_pitch(v);break;
    case QOPT_GUN_Y:set_gun_offset(true,v);break;
    case QOPT_GUN_Z:set_gun_offset(false,v);break;
    case QOPT_PLAYER:set_player_side(v+1);break;
    case QOPT_COVER:set_cover_mode(v!=0);break;
    case QOPT_WORLD:set_world_scale(v);break;
    case QOPT_DISTANCE:set_view_distance(v);break;
    case QOPT_RESOLUTION:set_resolution(v);break;
    case QOPT_FILTER:if((v==0)!=options.texture_filtering)toggle_texture_filter();break;
    case QOPT_GAME_VOLUME:set_game_volume(v);break;
    case QOPT_SKIP_STARTUP:set_skip_startup(v!=0);break;
    case QOPT_VOICE_VOLUME:tc2_online_set_volume(v);break;
    }
}
static void menu_action(int a){
    switch(a){
    case QACT_RESUME:set_paused(false,-1);break;
    case QACT_RESTART_GAME:tc2_online_restart_game();set_paused(false,-1);break;
    case QACT_RECENTER:recenter_requested=true;menu_placed=false;break;   /* the panel comes back in front, too */
    case QACT_RESTART_APP:
        fprintf(stderr,"[OPTIONS] restart app\n");
        tc2_online_leave();restart_app=true;quit=true;break;
    }
}
static QMenuHost menu_host={menu_get,menu_set,menu_action,0,0,0,0,false,true};
/* Test scripts (tc2_online.c TC2_NET_SCRIPT / online-test.txt): the menu's buttons without a player. */
void qhost_test(const char *action,const char *arg){
    if(!strcmp(action,"menu"))set_paused(true,arg?atoi(arg)%QMENU_TAB_COUNT:-1);
    else if(!strcmp(action,"resume"))menu_action(QACT_RESUME);
    else if(!strcmp(action,"restartgame"))menu_action(QACT_RESTART_GAME);
    else if(!strcmp(action,"restartapp"))menu_action(QACT_RESTART_APP);
    else if(!strcmp(action,"skip"))skip_test=true;
    else fprintf(stderr,"[TEST] unknown action %s\n",action);
}
static void menu_host_refresh(void){
    menu_host.boot_player=boot_player_side-1;menu_host.boot_resolution=boot_resolution;menu_host.saved=options_saved;
#ifdef TCVR_PC
    menu_host.talk_key=desktop?"V":"STICK CLICK";
#else
    menu_host.talk_key="STICK CLICK";
#endif
    menu_host.active_w=active_w;menu_host.active_h=active_h;
    menu_host.preset_unsupported=boot_resolution>0&&boot_resolution<QRESOLUTION_COUNT&&active_w&&
        (active_w!=qresolution_sizes[boot_resolution][0]||active_h!=qresolution_sizes[boot_resolution][1]);
}
/* One frame of menu input. The pointer: a ray from the gun (or the mouse), when it meets the panel. */
static void menu_input(double now,bool has_ray,V3 from,V3 dir,bool press,bool held,bool release,float stick_x,float stick_y,bool select,bool back,int tab){
    QMenuInput in={0};in.now=now;
    if(has_ray&&menu_placed){
        V3 point;in.pointer=qui_panel_hit(from,dir,menu_center,menu_rotation,&in.px,&in.py,&point);
        menu_laser=true;menu_end=in.pointer?point:add(from,mul(dir,2.f));
    }else menu_laser=false;
    in.press=press;in.held=held;in.release=release;in.stick_x=stick_x;in.stick_y=stick_y;
    in.nav_x=kb_nav_x;in.nav_y=kb_nav_y;in.select=select||kb_select;in.back=back||kb_back;in.tab=tab+kb_tab;
    kb_nav_x=kb_nav_y=kb_tab=0;kb_select=kb_back=false;
    menu_host_refresh();
    qmenu_update(&menu_host,&in);
    /* the laser ends where the menu's steadied pointer is, not on every tremor of the hand */
    float px,py;
    if(in.pointer&&qmenu_pointer(&px,&py))menu_end=add(menu_center,rotate(menu_rotation,v3((px/QMENU_W-.5f)*QUI_PANEL_W,(.5f-py/QMENU_H)*QUI_PANEL_H,0)));
}
static void shot_feedback(uint64_t now){
    recoil_started=now;
    if(options.tracer_enabled&&!options.laser_enabled&&focused&&!paused&&gun_tracked&&aim_valid)
        qtracer_start(&tracer,gun_origin,gun_hit,now);
}
static bool action(XrAction *out,const char *name,const char *label,XrActionType type,bool both_hands){
    XrActionCreateInfo ci={XR_TYPE_ACTION_CREATE_INFO};ci.actionType=type;
    if(both_hands){ci.countSubactionPaths=HAND_COUNT;ci.subactionPaths=hand_paths;}
    snprintf(ci.actionName,sizeof ci.actionName,"%s",name);snprintf(ci.localizedActionName,sizeof ci.localizedActionName,"%s",label);
    return XR(xrCreateAction(action_set,&ci,out));
}
static bool actions_init(void){
    XrActionSetCreateInfo ci={XR_TYPE_ACTION_SET_CREATE_INFO};strcpy(ci.actionSetName,"time_crisis");strcpy(ci.localizedActionSetName,"Time Crisis");
    if(!XR(xrCreateActionSet(instance,&ci,&action_set)))return false;
    hand_paths[LEFT_HAND]=path("/user/hand/left");hand_paths[RIGHT_HAND]=path("/user/hand/right");
    if(!action(&aim_action,"aim","Gun aim",XR_ACTION_TYPE_POSE_INPUT,true)||
       !action(&trigger_action,"trigger","Select weapon hand and fire",XR_ACTION_TYPE_FLOAT_INPUT,true)||
       !action(&grip_action,"grip_cover","Hold either grip to leave cover",XR_ACTION_TYPE_FLOAT_INPUT,true)||
       !action(&lower_action,"lower_button","Add credits / recenter",XR_ACTION_TYPE_BOOLEAN_INPUT,true)||
       !action(&upper_action,"upper_button","Toggle laser / change cover mode",XR_ACTION_TYPE_BOOLEAN_INPUT,true)||
       !action(&pause_action,"pause","Pause",XR_ACTION_TYPE_BOOLEAN_INPUT,false)||
       !action(&hand_action,"weapon_hand","Change weapon hand in options",XR_ACTION_TYPE_BOOLEAN_INPUT,false)||
       !action(&pitch_action,"gun_angle","Adjust gun angle in options",XR_ACTION_TYPE_VECTOR2F_INPUT,false)||
       !action(&pitch_reset_action,"gun_angle_reset","Reset gun angle in options",XR_ACTION_TYPE_BOOLEAN_INPUT,false)||
       !action(&distance_action,"view_distance","Adjust view distance in options",XR_ACTION_TYPE_VECTOR2F_INPUT,false)||
       !action(&haptic_action,"recoil","Gun recoil",XR_ACTION_TYPE_VIBRATION_OUTPUT,true))return false;
    XrActionSuggestedBinding bindings[]={
        {aim_action,path("/user/hand/left/input/aim/pose")},
        {aim_action,path("/user/hand/right/input/aim/pose")},
        {trigger_action,path("/user/hand/left/input/trigger/value")},
        {trigger_action,path("/user/hand/right/input/trigger/value")},
        {grip_action,path("/user/hand/left/input/squeeze/value")},
        {grip_action,path("/user/hand/right/input/squeeze/value")},
        {lower_action,path("/user/hand/left/input/x/click")},
        {lower_action,path("/user/hand/right/input/a/click")},
        {upper_action,path("/user/hand/left/input/y/click")},
        {upper_action,path("/user/hand/right/input/b/click")},
        {pause_action,path("/user/hand/left/input/menu/click")},
        {hand_action,path("/user/hand/right/input/thumbstick/click")},
        {pitch_action,path("/user/hand/right/input/thumbstick")},
        {pitch_reset_action,path("/user/hand/left/input/thumbstick/click")},
        {distance_action,path("/user/hand/left/input/thumbstick")},
        {haptic_action,path("/user/hand/left/output/haptic")},
        {haptic_action,path("/user/hand/right/output/haptic")}};
    XrInteractionProfileSuggestedBinding suggest={XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggest.interactionProfile=path("/interaction_profiles/oculus/touch_controller");suggest.countSuggestedBindings=sizeof(bindings)/sizeof(bindings[0]);suggest.suggestedBindings=bindings;
    if(!XR(xrSuggestInteractionProfileBindings(instance,&suggest)))return false;
    XrSessionActionSetsAttachInfo attach={XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&action_set;
    if(!XR(xrAttachSessionActionSets(session,&attach)))return false;
    XrActionSpaceCreateInfo space={XR_TYPE_ACTION_SPACE_CREATE_INFO};space.action=aim_action;space.poseInActionSpace.orientation.w=1;
    for(int i=0;i<HAND_COUNT;i++){space.subactionPath=hand_paths[i];if(!XR(xrCreateActionSpace(session,&space,&aim_spaces[i])))return false;}
    return true;
}
/* Fresh edges only: a held button across focus/tracking loss cannot change a setting. */
static bool pressed(XrAction a,XrPath hand,ButtonEdge *button){
    XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO};info.action=a;info.subactionPath=hand;
    XrActionStateBoolean s={XR_TYPE_ACTION_STATE_BOOLEAN};
    if(XR_FAILED(xrGetActionStateBoolean(session,&info,&s))||!s.isActive){button->synced=false;return false;}
    bool edge=button->synced&&s.currentState&&!button->was;button->was=s.currentState;button->synced=true;return edge;
}
static bool axis_state(XrAction a,XrPath hand,XrActionStateFloat *s){XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO};info.action=a;info.subactionPath=hand;*s=(XrActionStateFloat){XR_TYPE_ACTION_STATE_FLOAT};return XR_SUCCEEDED(xrGetActionStateFloat(session,&info,s))&&s->isActive;}
static float axis(XrAction a,XrPath hand){XrActionStateFloat s;return axis_state(a,hand,&s)?s.currentState:0;}
/* the larger of the two thumbsticks (the menu's stick) */
static void menu_stick(float *x,float *y){
    *x=*y=0;
    XrAction sticks[2]={pitch_action,distance_action};
    for(int i=0;i<2;i++){
        XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=sticks[i];
        XrActionStateVector2f s={XR_TYPE_ACTION_STATE_VECTOR2F};
        if(XR_FAILED(xrGetActionStateVector2f(session,&get,&s))||!s.isActive||!isfinite(s.currentState.x)||!isfinite(s.currentState.y))continue;
        if(fabsf(s.currentState.x)+fabsf(s.currentState.y)>fabsf(*x)+fabsf(*y)){*x=s.currentState.x;*y=s.currentState.y;}
    }
}
static V3 vec(XrVector3f v){return v3(v.x,v.y,v.z);}
static Q4 quat(XrQuaternionf q){return (Q4){q.x,q.y,q.z,q.w};}
static V3 relative_position(XrVector3f p){return rotate(conjugate(origin_rotation),sub(vec(p),origin));}
static Q4 relative_rotation(XrQuaternionf q){return product(conjugate(origin_rotation),quat(q));}

static void capture_eye(int eye,int w,int h){
    if(access("capture.request",F_OK))return;
    unsigned char *pixels=malloc((size_t)w*h*4);if(!pixels)return;
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    char name[32];snprintf(name,sizeof name,"eye-%d.ppm",eye);FILE *f=fopen(name,"wb");
    if(f){fprintf(f,"P6\n%d %d\n255\n",w,h);for(int y=h-1;y>=0;y--)for(int x=0;x<w;x++)fwrite(pixels+((size_t)y*w+x)*4,1,3,f);fclose(f);fprintf(stderr,"[XR] captured %s\n",name);}
    free(pixels);if(eye==1)unlink("capture.request");
}

static bool events(void){
    SDL_Event s;while(SDL_PollEvent(&s)){
        if(s.type==SDL_QUIT)quit=true;
#ifdef TCVR_PC
        if(s.type==SDL_KEYDOWN&&!s.key.repeat){
            const SDL_Keycode k=s.key.keysym.sym;
            if(k==SDLK_ESCAPE)set_paused(!paused,-1);
            else if(paused){   /* the menu: arrows move and change, Enter presses, Backspace goes back, Tab changes tab */
                if(k==SDLK_UP)kb_nav_y++;if(k==SDLK_DOWN)kb_nav_y--;if(k==SDLK_LEFT)kb_nav_x--;if(k==SDLK_RIGHT)kb_nav_x++;
                if(k==SDLK_RETURN||k==SDLK_KP_ENTER)kb_select=true;
                if(k==SDLK_BACKSPACE)kb_back=true;
                if(k==SDLK_TAB)kb_tab+=(s.key.keysym.mod&KMOD_SHIFT)?-1:1;
            }else{
                if(k==SDLK_c||k==SDLK_5)coin_frames=48;
                if(k==SDLK_m&&tc2_online_session()&&!tc2_online_push_to_talk())tc2_online_toggle_mute();
                if(k==SDLK_l)cycle_aim_guide();
                if(k==SDLK_r)recenter_requested=true;
            }
        }
#endif
    }
#ifdef TCVR_PC
    if(desktop)return !quit;
#endif
    XrEventDataBuffer e={XR_TYPE_EVENT_DATA_BUFFER};XrResult result;
    while((result=xrPollEvent(instance,&e))==XR_SUCCESS){
        if(e.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            XrEventDataSessionStateChanged *change=(void*)&e;state=change->state;
            fprintf(stderr,"[XR] session state %d\n",state);
            if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo b={XR_TYPE_SESSION_BEGIN_INFO};b.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;if(!XR(xrBeginSession(session,&b)))return false;running=true;game_deadline=0;performance_start();}
            if(state==XR_SESSION_STATE_STOPPING){if(running)xrEndSession(session);running=false;game_deadline=0;}
            if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)quit=true;
            bool focus=state==XR_SESSION_STATE_FOCUSED;
            if(focused!=focus){focused=focus;reset_input_edges();game_deadline=0;ss22_input_neutral();apply_game_volume();if(focus){rate_attempts=0;rate_check_time=0;}}
        }else if(e.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)quit=true;
        else if(e.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){recenter_requested=true;}
        else if(e.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB){
            XrEventDataDisplayRefreshRateChangedFB *rate=(void*)&e;game_deadline=0;
            fprintf(stderr,"[XR] display changed %.0f -> %.0f Hz\n",rate->fromDisplayRefreshRate,rate->toDisplayRefreshRate);
        }
        else if(e.type==XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT){
            XrEventDataPerfSettingsEXT *perf=(void*)&e;
            fprintf(stderr,"[XR] performance domain %d subdomain %d level %d\n",perf->domain,perf->subDomain,perf->toLevel);
        }
        e=(XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
    return !quit&&(result==XR_EVENT_UNAVAILABLE||XR(result));
}
static void sync_input(XrTime time,bool head_tracked,float head_y){
    trigger=pedal=0;aim_valid=gun_tracked=false;
    if(!focused){reset_input_edges();tc2_online_talk_key(false);return;}
    XrActiveActionSet aset={action_set,XR_NULL_PATH};XrActionsSyncInfo sync={XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&aset;
    if(!XR(xrSyncActions(session,&sync))){reset_input_edges();return;}
    if(pressed(pause_action,XR_NULL_PATH,&pause_button))set_paused(!paused,-1);
    /* voice chat: the left stick click mutes an open mic; with push to talk, either stick click held talks (V too on a PC) */
    const bool ptt=tc2_online_push_to_talk();
    if(pressed(pitch_reset_action,XR_NULL_PATH,&pitch_reset_button)&&!paused&&!ptt&&tc2_online_session())tc2_online_toggle_mute();
    (void)pressed(hand_action,XR_NULL_PATH,&hand_button);
    bool talk=(pitch_reset_button.synced&&pitch_reset_button.was)||(hand_button.synced&&hand_button.was);
#ifdef TCVR_PC
    talk=talk||SDL_GetKeyboardState(NULL)[SDL_SCANCODE_V];
#endif
    tc2_online_talk_key(ptt&&talk);
    /* Face-button roles follow the saved layout, even during trigger handoffs. In the menu: A / X select, B / Y go back. */
    int controls=options.left_handed?LEFT_HAND:RIGHT_HAND;XrPath weapon=hand_paths[controls],other=hand_paths[1-controls];
    const bool lower_w=pressed(lower_action,weapon,&coin_button),lower_o=pressed(lower_action,other,&recenter_button);
    const bool upper_w=pressed(upper_action,weapon,&laser_button),upper_o=pressed(upper_action,other,&cover_button);
    if(!paused){
        if(lower_w)coin_frames=48;
        if(lower_o)recenter_requested=true;
        if(upper_w)cycle_aim_guide();
    }
    int menu_tab=0;
    if(paused)for(int h=0;h<HAND_COUNT;h++){   /* grips: left the tab before, right the next */
        XrActionStateFloat g;
        if(!axis_state(grip_action,hand_paths[h],&g)||!isfinite(g.currentState)){menu_grip_armed[h]=false;continue;}
        if(g.currentState<.2f)menu_grip_armed[h]=true;
        else if(g.currentState>.55f&&menu_grip_armed[h]){menu_grip_armed[h]=false;menu_tab+=h==LEFT_HAND?-1:1;}
    }
    if(!origin_set){if(paused){float sx,sy;menu_stick(&sx,&sy);menu_input(time/1e9,false,v3(0,0,0),v3(0,0,-1),false,false,false,sx,sy,lower_w||lower_o,upper_w||upper_o,menu_tab);}return;}
    if(cover_calibration_pending&&head_tracked){
        qcover_calibrate(&cover,head_y);cover_calibration_pending=false;
        fprintf(stderr,"[COVER] upright height %.3f m\n",head_y);
    }
    /* Cover must work even when the weapon controller is out of view. */
    bool exposed=qcover_pedal(&cover,head_y,head_tracked);
    if(!paused)pedal=options.physical_crouch?(!cover_calibration_pending&&exposed?1.f:0.f):fmaxf(axis(grip_action,hand_paths[LEFT_HAND]),axis(grip_action,hand_paths[RIGHT_HAND]));
    XrSpaceLocation locations[HAND_COUNT];bool tracked[HAND_COUNT]={false,false},edges[HAND_COUNT]={false,false},available[HAND_COUNT];
    XrActionStateFloat fire[HAND_COUNT];int menu_press=-1;
    XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    for(int h=0;h<HAND_COUNT;h++){
        XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=aim_action;get.subactionPath=hand_paths[h];
        XrActionStatePose pose={XR_TYPE_ACTION_STATE_POSE};locations[h]=(XrSpaceLocation){XR_TYPE_SPACE_LOCATION};
        tracked[h]=XR_SUCCEEDED(xrGetActionStatePose(session,&get,&pose))&&pose.isActive&&
            XR_SUCCEEDED(xrLocateSpace(aim_spaces[h],local_space,time,&locations[h]))&&(locations[h].locationFlags&required)==required;
        available[h]=axis_state(trigger_action,hand_paths[h],&fire[h]);
        FireTrigger *button=&fire_triggers[h];
        if(paused&&available[h]){   /* the menu's trigger: either hand, after a release */
            if(fire[h].currentState<.2f)menu_trigger_armed[h]=true;
            else if(fire[h].currentState>.55f&&menu_trigger_armed[h]){menu_trigger_armed[h]=false;menu_press=h;}
        }
        if(paused||!tracked[h]||!available[h]){*button=(FireTrigger){0};continue;}
        if(fire[h].currentState<.2f){button->armed=true;button->pressed=false;}
        else if(fire[h].currentState>.55f&&button->armed){button->armed=false;button->pressed=true;edges[h]=true;}
    }
    if(menu_press>=0&&menu_press!=weapon_hand())select_weapon_hand(menu_press);   /* the gun goes to the hand that pressed */
    int chosen=-1;
    if(edges[LEFT_HAND]&&edges[RIGHT_HAND]){
        /* If both arrive in one XR sample, newest wins; exact ties keep the hand. */
        chosen=fire[LEFT_HAND].lastChangeTime==fire[RIGHT_HAND].lastChangeTime?weapon_hand():
            fire[LEFT_HAND].lastChangeTime>fire[RIGHT_HAND].lastChangeTime?LEFT_HAND:RIGHT_HAND;
    }else if(edges[LEFT_HAND])chosen=LEFT_HAND;else if(edges[RIGHT_HAND])chosen=RIGHT_HAND;
    if(chosen>=0){
        if(chosen!=weapon_hand())fprintf(stderr,"[INPUT] weapon hand %s (trigger)\n",chosen==LEFT_HAND?"LEFT":"RIGHT");
        select_weapon_hand(chosen);shot_pending=true;shot_needs_release=fire_output_down;
    }
    int hand=weapon_hand();
    if(!available[hand])shot_pending=shot_needs_release=false;
    if(tracked[hand]){
        gun_position=relative_position(locations[hand].pose.position);gun_rotation=relative_rotation(locations[hand].pose.orientation);gun_tracked=true;
        /* Local pitch rotates the model, muzzle and aim together about the hand.
         * Positive raises the barrel; zero preserves the runtime's aim pose. */
        float half_pitch=options.gun_pitch*(3.14159265358979323846f/360.f);
        gun_rotation=product(gun_rotation,(Q4){sinf(half_pitch),0,0,cosf(half_pitch)});
        /* Controller-local translation: +Y up, +Z back. Rotate the offset
         * before the pitch correction so fitting does not swing around the barrel. */
        Q4 hand_rotation=relative_rotation(locations[hand].pose.orientation);
        gun_position=add(gun_position,rotate(hand_rotation,v3(0,options.gun_offset_y_mm*.001f,options.gun_offset_z_mm*.001f)));
        gun_origin=qgun_muzzle(gun_position,gun_rotation);
        V3 dir=rotate(gun_rotation,v3(0,0,-1));
        aim_valid=qvr_aim(gun_origin,dir,&nx,&ny,&gun_hit);
        if(!aim_valid)gun_hit=add(gun_origin,mul(dir,2.5f));
        if(!paused&&fire_triggers[hand].pressed)trigger=1;
    }else shot_pending=shot_needs_release=false;
    if(paused){
        bool press=menu_press>=0,release=false;
        if(press)menu_trigger_held=true;
        else if(menu_trigger_held&&(!available[hand]||fire[hand].currentState<.2f)){menu_trigger_held=false;release=true;}
        float sx,sy;menu_stick(&sx,&sy);
        menu_input(time/1e9,gun_tracked,gun_origin,rotate(gun_rotation,v3(0,0,-1)),press,menu_trigger_held,release,sx,sy,lower_w||lower_o,upper_w||upper_o,menu_tab);
    }
}

/* Input tests compile the real action and polling code without a window/runtime. */
#ifndef TCVR_INPUT_TEST
bool tc2_vr_open(void){
    replay_input=getenv("TC2_REPLAY")!=NULL;
    qoptions_load(options_path,&options);tc2_gl_set_filter(options.texture_filtering);
    boot_player_side=options.player_side;boot_resolution=options.resolution;qvr_world_scale=options.world_scale/100.f;active_weapon_hand=options.left_handed?LEFT_HAND:RIGHT_HAND;
    qvr_view_distance=options.view_distance/100.f;
    tc2_online_init();tc2_online_set_startup_skip(options.skip_startup);
    fprintf(stderr,"[VR SCALE] %.6f reconstructed units/metre; world size %d%%; view distance %d cm\n",qvr_units_per_meter,options.world_scale,options.view_distance);
    fprintf(stderr,"[OPTIONS] laser %s; cover %s; default hand %s; trigger handoff enabled (left menu: options)\n",options.laser_enabled?"ON":"OFF",options.physical_crouch?"PHYSICAL":"GRIP",options.left_handed?"LEFT":"RIGHT");
    fprintf(stderr,"[OPTIONS] gun angle %+d deg\n",options.gun_pitch);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)!=0)goto fail;
#ifdef TCVR_PC
    desktop=getenv("TCVR_DESKTOP")!=NULL;qvr_flat_view=desktop&&!getenv("TCVR_SCENE_VIEW");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
#ifdef TCVR_PC
    window=SDL_CreateWindow("Time Crisis II VR",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,960,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|(getenv("TCVR_TEST_HIDDEN")?SDL_WINDOW_HIDDEN:0));
#else
    window=SDL_CreateWindow("Time Crisis II VR",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN);
#endif
    if(!window)goto fail;context=SDL_GL_CreateContext(window);if(!context)goto fail;
#ifdef TCVR_PC
    if(!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))goto fail;
    gpu_ready=true;glDisable(GL_FRAMEBUFFER_SRGB);
    SDL_GL_SetSwapInterval(0);
    if(desktop)goto graphics_ready;
    const char *extensions[5]={XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    uint32_t extension_count=1;
#else
    gpu_ready=true;
    JNIEnv *env=SDL_AndroidGetJNIEnv();JavaVM *vm=NULL;(*env)->GetJavaVM(env,&vm);jobject activity=SDL_AndroidGetActivity();
    PFN_xrInitializeLoaderKHR init=NULL;
    REQUIRE(xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",(PFN_xrVoidFunction*)&init));
    XrLoaderInitInfoAndroidKHR loader={XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};loader.applicationVM=vm;loader.applicationContext=activity;
    REQUIRE(init((XrLoaderInitInfoBaseHeaderKHR*)&loader));
    const char *extensions[5]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    uint32_t extension_count=2;
#endif
    const char *optional[]={XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
#ifdef TCVR_PC
        "",
#else
        XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,
#endif
        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
    bool enabled[3]={false,false,false};uint32_t available_count=0;
    REQUIRE(xrEnumerateInstanceExtensionProperties(NULL,0,&available_count,NULL));
    XrExtensionProperties *available=calloc(available_count,sizeof *available);if(!available)goto fail;
    for(uint32_t i=0;i<available_count;i++)available[i].type=XR_TYPE_EXTENSION_PROPERTIES;
    XrResult enumeration=xrEnumerateInstanceExtensionProperties(NULL,available_count,&available_count,available);
    if(XR_SUCCEEDED(enumeration))for(int k=0;k<3;k++)for(uint32_t j=0;j<available_count;j++)if(!strcmp(optional[k],available[j].extensionName)){
        extensions[extension_count++]=optional[k];enabled[k]=true;break;
    }
    free(available);if(!XR(enumeration))goto fail;
#ifndef TCVR_PC
    XrInstanceCreateInfoAndroidKHR android={XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};android.applicationVM=vm;android.applicationActivity=activity;
#endif
    XrInstanceCreateInfo ic={XR_TYPE_INSTANCE_CREATE_INFO};
#ifndef TCVR_PC
    ic.next=&android;
#endif
    ic.enabledExtensionCount=extension_count;ic.enabledExtensionNames=extensions;
    strcpy(ic.applicationInfo.applicationName,"Time Crisis II VR");ic.applicationInfo.applicationVersion=1;strcpy(ic.applicationInfo.engineName,"Namco23 VR");ic.applicationInfo.engineVersion=1;ic.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,34);
    REQUIRE(xrCreateInstance(&ic,&instance));
#ifndef TCVR_PC
    (*env)->DeleteLocalRef(env,activity);
#endif
    if(enabled[0])XR(xrGetInstanceProcAddr(instance,"xrPerfSettingsSetPerformanceLevelEXT",(PFN_xrVoidFunction*)&set_performance));
#ifndef TCVR_PC
    if(enabled[1])XR(xrGetInstanceProcAddr(instance,"xrSetAndroidApplicationThreadKHR",(PFN_xrVoidFunction*)&set_thread));
#endif
    if(enabled[2]){
        XR(xrGetInstanceProcAddr(instance,"xrEnumerateDisplayRefreshRatesFB",(PFN_xrVoidFunction*)&enumerate_rates));
        XR(xrGetInstanceProcAddr(instance,"xrRequestDisplayRefreshRateFB",(PFN_xrVoidFunction*)&request_rate));
        XR(xrGetInstanceProcAddr(instance,"xrGetDisplayRefreshRateFB",(PFN_xrVoidFunction*)&get_rate));
    }
    XrSystemGetInfo system_info={XR_TYPE_SYSTEM_GET_INFO};system_info.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XrSystemId system;
    REQUIRE(xrGetSystem(instance,&system_info,&system));
#ifdef TCVR_PC
    PFN_xrGetOpenGLGraphicsRequirementsKHR requirements=NULL;
    REQUIRE(xrGetInstanceProcAddr(instance,"xrGetOpenGLGraphicsRequirementsKHR",(PFN_xrVoidFunction*)&requirements));
    XrGraphicsRequirementsOpenGLKHR req={XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};REQUIRE(requirements(instance,system,&req));
    GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    XrVersion version=XR_MAKE_VERSION(major,minor,0);
    if(version<req.minApiVersionSupported||version>req.maxApiVersionSupported){fprintf(stderr,"[XR] OpenGL version outside runtime range\n");goto fail;}
    XrGraphicsBindingOpenGLWin32KHR binding={XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};binding.hDC=wglGetCurrentDC();binding.hGLRC=wglGetCurrentContext();
#else
    PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements=NULL;
    REQUIRE(xrGetInstanceProcAddr(instance,"xrGetOpenGLESGraphicsRequirementsKHR",(PFN_xrVoidFunction*)&requirements));
    XrGraphicsRequirementsOpenGLESKHR req={XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};REQUIRE(requirements(instance,system,&req));
    EGLDisplay display=eglGetCurrentDisplay();EGLContext egl_context=eglGetCurrentContext();EGLint config_id,count;
    if(!eglQueryContext(display,egl_context,EGL_CONFIG_ID,&config_id))goto fail;
    EGLint attributes[]={EGL_CONFIG_ID,config_id,EGL_NONE};EGLConfig config;
    if(!eglChooseConfig(display,attributes,&config,1,&count)||!count)goto fail;
    XrGraphicsBindingOpenGLESAndroidKHR binding={XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};binding.display=display;binding.config=config;binding.context=egl_context;
#endif
    XrSessionCreateInfo sc={XR_TYPE_SESSION_CREATE_INFO};sc.next=&binding;sc.systemId=system;REQUIRE(xrCreateSession(instance,&sc,&session));
    if(enumerate_rates){
        uint32_t count=0;
        if(XR(enumerate_rates(session,0,&count,NULL))&&count){
            float *rates=calloc(count,sizeof *rates);
            if(rates&&XR(enumerate_rates(session,count,&count,rates))){
                /* Multiview and shared sprite images leave more headroom for
                 * 120 Hz. A private override permits other benchmarks. */
                float wanted=120;FILE *config=fopen("refresh-rate.txt","r");if(config){if(fscanf(config,"%f",&wanted)!=1)wanted=120;fclose(config);}
                fprintf(stderr,"[XR] supported refresh rates:");
                for(uint32_t j=0;j<count;j++)fprintf(stderr," %.0f",rates[j]);
                preferred_rate=qrefresh_choose(rates,count,wanted);
                fprintf(stderr," Hz\n");
                fprintf(stderr,"[XR] display preference %.0f Hz; supported selection %.0f Hz\n",wanted,preferred_rate);
            }
            free(rates);
        }
    }
    XrReferenceSpaceCreateInfo rc={XR_TYPE_REFERENCE_SPACE_CREATE_INFO};rc.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;rc.poseInReferenceSpace.orientation.w=1;REQUIRE(xrCreateReferenceSpace(session,&rc,&local_space));
    if(!actions_init())goto fail;
    uint32_t format_count;REQUIRE(xrEnumerateSwapchainFormats(session,0,&format_count,NULL));int64_t *formats=calloc(format_count,sizeof *formats);if(!formats)goto fail;
    XrResult fmt_result=xrEnumerateSwapchainFormats(session,format_count,&format_count,formats);
    bool srgb_control=qcolor_srgb_write_control();
    int64_t color_format=XR_SUCCEEDED(fmt_result)?qcolor_swapchain_format(formats,format_count,srgb_control):0;
    free(formats);if(!XR(fmt_result)||!color_format){fprintf(stderr,"[XR] No supported 8-bit color swapchain\n");goto fail;}
    qcolor_raw_output(color_format);
    fprintf(stderr,"[XR] swapchain %s; sRGB write control %s\n",color_format==GL_SRGB8_ALPHA8?"sRGB (raw arcade values)":"RGBA8 (legacy fallback)",srgb_control?"available":"unavailable");
    uint32_t view_count;REQUIRE(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&view_count,NULL));if(view_count!=2)goto fail;
    XrViewConfigurationView configs[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    REQUIRE(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&view_count,configs));
    GLint texture_limit,renderbuffer_limit,viewport_limit[2];
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&texture_limit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&renderbuffer_limit);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS,viewport_limit);
    uint32_t gpu_limit=(uint32_t)(texture_limit<renderbuffer_limit?texture_limit:renderbuffer_limit);
    for(int i=0;i<2;i++){
        struct Eye *eye=&eyes[i];eye->w=configs[i].recommendedImageRectWidth;eye->h=configs[i].recommendedImageRectHeight;
#ifdef TCVR_PC
        float factor=fminf(1,2160.f/fmaxf(eye->w,eye->h));
#else
        float factor=fminf(1,1440.f/fmaxf(eye->w,eye->h));
#endif
       eye->w=(uint32_t)(eye->w*factor);eye->h=(uint32_t)(eye->h*factor);
        uint32_t max_w=configs[i].maxImageRectWidth,max_h=configs[i].maxImageRectHeight;
        if(max_w>gpu_limit)max_w=gpu_limit;if(max_h>gpu_limit)max_h=gpu_limit;
        if(max_w>(uint32_t)viewport_limit[0])max_w=(uint32_t)viewport_limit[0];
        if(max_h>(uint32_t)viewport_limit[1])max_h=(uint32_t)viewport_limit[1];
        if(!qresolution_choose(options.resolution,eye->w,eye->h,max_w,max_h,&eye->w,&eye->h))
            fprintf(stderr,"[XR] resolution preset exceeds runtime/GPU limits; using default %ux%u\n",eye->w,eye->h);
        if(!eye->w||!eye->h)goto fail;
        XrSwapchainCreateInfo cc={XR_TYPE_SWAPCHAIN_CREATE_INFO};cc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;cc.format=color_format;cc.sampleCount=1;cc.width=eye->w;cc.height=eye->h;cc.faceCount=1;cc.arraySize=1;cc.mipCount=1;
        REQUIRE(xrCreateSwapchain(session,&cc,&eye->chain));REQUIRE(xrEnumerateSwapchainImages(eye->chain,0,&eye->n,NULL));
        eye->images=calloc(eye->n,sizeof(*eye->images));eye->fb=calloc(eye->n,sizeof(GLuint));if(!eye->images||!eye->fb)goto fail;
        for(uint32_t j=0;j<eye->n;j++)eye->images[j].type=Q_SWAPCHAIN_IMAGE;
        REQUIRE(xrEnumerateSwapchainImages(eye->chain,eye->n,&eye->n,(XrSwapchainImageBaseHeader*)eye->images));
        glGenFramebuffers(eye->n,eye->fb);
        glGenRenderbuffers(1,&eye->depth);glBindRenderbuffer(GL_RENDERBUFFER,eye->depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,eye->w,eye->h);
        for(uint32_t j=0;j<eye->n;j++){glBindFramebuffer(GL_FRAMEBUFFER,eye->fb[j]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,eye->images[j].image,0);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,eye->depth);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;}
        views[i]=(XrView){XR_TYPE_VIEW};
    }
    menu_layer_open(color_format);
graphics_ready:
    if(!qgl_init())goto fail;
#ifndef TCVR_PC
    multiview=false; /* TC2 priority and gamma passes currently render each eye separately. */
#endif
    if(!qgun_init("models/namco-gun.tcgun")&&!qgun_init("models/player-gun.tcgun"))goto fail;
    if(!qgun_load_weapon(QGUN_MACHINE_GUN,"models/machine-gun.tcgun"))
        fprintf(stderr,"[GUN] Machine gun asset unavailable; using pistol fallback\n");
    if(!qui_init())goto fail;
    active_w=eyes[0].w;active_h=eyes[0].h;
    if(!tc2_gl_init())goto fail;
    void ss22_gl_set_gun_flash(int);ss22_gl_set_gun_flash(0);tc2_audio_open();tc2_audio_volume(options.game_volume);
    active=true;fprintf(stderr,"[XR] Renderer initialized: %ux%u per eye; %s\n",eyes[0].w,eyes[0].h,glGetString(GL_RENDERER));return true;
fail:
    fprintf(stderr,"[XR] initialization failed; SDL: %s\n",SDL_GetError());ss22_host_close();return false;
}

/* While linked, the partner's cabinet needs ours: the arcade keeps running behind the menu and
 * through focus or tracking loss, with neutral controls. Offline, those pause it as before. */
static bool sim_held(bool tracking){return !tc2_online_session()&&(!focused||paused||!tracking);}
static void online_overlays(const float v[16],const float p[16],V3 head,Q4 rotation){
    char toast[64];unsigned serial;
    if(!paused&&tc2_online_toast(toast,sizeof toast,&serial))qui_draw_toast(v,p,head,rotation,toast,serial);
}
static void hud_camera(void){float cx,cy,focal;qvr_flat_camera(&cx,&cy,&focal);qgl_flat_camera(cx,cy,focal);}
#ifdef TCVR_PC
static bool desktop_frame(void){
    static Uint64 deadline;static unsigned frame_number;bool fast=getenv("TCVR_FAST")!=NULL;
    for(;;){
        tc2_online_service();
        if(!events())return false;
        int w,h,mx,my;SDL_GL_GetDrawableSize(window,&w,&h);int lw,lh;SDL_GetWindowSize(window,&lw,&lh);
        if(w<=0||h<=0||lw<=0||lh<=0){SDL_Delay(20);continue;}
        Uint32 mouse=SDL_GetMouseState(&mx,&my);focused=!!(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS);
        nx=fminf(1,fmaxf(0,(float)mx/lw));ny=fminf(1,fmaxf(0,(float)my/lh));
        aim_valid=true;gun_tracked=false;trigger=focused&&!paused&&(mouse&SDL_BUTTON_LMASK)?1:0;
        pedal=focused&&!paused&&((mouse&SDL_BUTTON_RMASK)||SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SPACE])?1:0;
        tc2_online_talk_key(focused&&SDL_GetKeyboardState(NULL)[SDL_SCANCODE_V]);   /* push to talk, held */
        /* Match the original 640x480 camera exactly for mouse aiming and tests. */
        int vw=w,vh=w*3/4;if(vh>h){vh=h;vw=h*4/3;}
        nx=((float)mx/lw*w-(w-vw)*.5f)/vw;ny=((float)my/lh*h-(h-vh)*.5f)/vh;
        aim_valid=nx>=0&&nx<=1&&ny>=0&&ny<=1;
        float v[16],p[16];view_matrix(v,v3(0,0,0),(Q4){0,0,0,1});
        projection(p,-atanf(.64f),atanf(.64f),-atanf(.48f),atanf(.48f));
        /* Keep a 4:3 image centered in a resizable window. */
        if((float)w/h>4.f/3.f){p[0]*=(float)vw/w;}else{p[5]*=(float)vh/h;}
        {   /* the menu: straight ahead; the mouse points and clicks */
            static bool was_down;const bool down=focused&&(mouse&SDL_BUTTON_LMASK);
            if(paused){
                menu_center=v3(0,0,-1.6f);menu_rotation=(Q4){0,0,0,1};menu_placed=true;
                const float ndx=2.f*mx/lw-1,ndy=1-2.f*my/lh;V3 dir=v3(ndx/p[0],ndy/p[5],-1);
                menu_input(SDL_GetTicks64()/1000.0,focused,v3(0,0,0),dir,down&&!was_down,down,!down&&was_down,0,0,false,false,0);
                menu_laser=false;
            }
            was_down=down;
        }
        hud_camera();qgl_target(0,0);qgl_eye(v,p);qtc2_draw(v,p,w,h,0);qgl_flush();
        if(options.laser_enabled&&aim_valid&&!paused){qgl_pointer(v3(.24f,-.24f,-.45f),v3((nx-.5f)*3.2f,(.5f-ny)*2.4f,-2.5f));qgl_flush();}
        online_overlays(v,p,v3(0,0,0),(Q4){0,0,0,1});
        if(paused){menu_host_refresh();qui_draw_menu(v,p,menu_center,menu_rotation,&menu_host,SDL_GetTicks64()/1000.0,false);}
        const char *capture_frame=getenv("TCVR_CAPTURE_FRAME");
        const char *capture_every=getenv("TCVR_CAPTURE_EVERY");++frame_number;
        {const char *menu=getenv("TCVR_TEST_MENU");const char *at=menu?strchr(menu,'@'):NULL;   /* "tab@frame": open the menu there */
         if(menu&&frame_number==(at?strtoul(at+1,NULL,10):1))set_paused(true,atoi(menu)%QMENU_TAB_COUNT);}
        if((capture_frame&&frame_number==strtoul(capture_frame,NULL,10))||(capture_every&&atoi(capture_every)>0&&frame_number%(unsigned)atoi(capture_every)==0)){FILE *f=fopen("capture.request","w");if(f)fclose(f);}
        if(!access("capture.request",F_OK)){capture_eye(0,w,h);unlink("capture.request");
            if(capture_every){char name[32];snprintf(name,sizeof name,"cap-%06u.ppm",frame_number);remove(name);rename("eye-0.ppm",name);}}
        GLenum error=glGetError();if(error)fprintf(stderr,"[GL] desktop error 0x%x\n",error);
        SDL_GL_SwapWindow(window);
        const bool skip=skip_update();   /* skipping the start-up: no pacing */
        if(!fast&&!skip){Uint64 now=SDL_GetPerformanceCounter(),freq=SDL_GetPerformanceFrequency();if(!deadline||now>deadline+freq/4)deadline=now;deadline+=freq*1000/59906;while((now=SDL_GetPerformanceCounter())<deadline)SDL_Delay(1);}
        if((!paused||tc2_online_session())&&tc2_online_may_advance()){ss22_input_update();return true;}   /* stall-and-wait: render on, hold the arcade */
    }
}
#endif
bool ss22_host_frame(void){
#ifdef TCVR_PC
    if(desktop)return desktop_frame();
#endif
    /* The original game runs at 59.906 Hz. Submit additional poses without advancing its CPU at headset refresh. */
    for(;;){
        tc2_online_service();
        if(!events())return false;
        if(!running){
            /* No XR frames (headset off / app in the background): a linked arcade keeps its own 59.906 Hz. */
            static Uint64 wall_deadline;
            if(tc2_online_session()){
                Uint64 now=SDL_GetPerformanceCounter(),freq=SDL_GetPerformanceFrequency();
                if(!wall_deadline||now>wall_deadline+freq/4)wall_deadline=now;
                if(now>=wall_deadline&&tc2_online_may_advance()){wall_deadline+=freq*1000/59906;ss22_input_neutral();return true;}
                SDL_Delay(1);continue;
            }
            wall_deadline=0;SDL_Delay(10);continue;
        }
        /* skipping the start-up: more arcade frames between headset frames, for most of a display period after each one */
        if(skip_update()&&!sim_held(last_tracking)&&SDL_GetPerformanceCounter()<skip_until){ss22_input_update();return true;}
        if(game_deadline&&!sim_held(last_tracking)&&last_display_time>=game_deadline){
            if(tc2_online_may_advance()&&qclock_take(&game_deadline,last_display_time)){ss22_input_update();return true;}
        }
        XrFrameWaitInfo wi={XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame={XR_TYPE_FRAME_STATE};if(!XR(xrWaitFrame(session,&wi,&frame)))return false;
        display_rate_check(frame.predictedDisplayTime);
        uint64_t render_start=SDL_GetPerformanceCounter();
        double cpu_start=cpu_ms(),stage_start=wall_ms(),parts[6]={0};
        XrFrameBeginInfo bi={XR_TYPE_FRAME_BEGIN_INFO};if(!XR(xrBeginFrame(session,&bi)))return false;
        XrViewState vs={XR_TYPE_VIEW_STATE};XrViewLocateInfo li={XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=frame.predictedDisplayTime;li.space=local_space;uint32_t n=0;
        bool located=XR(xrLocateViews(session,&li,&vs,2,&n,views));
        XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        bool tracking=located&&n==2&&(vs.viewStateFlags&valid)==valid;
        last_display_time=frame.predictedDisplayTime;last_tracking=tracking;
        if(tracking&&(!origin_set||recenter_requested)){
            origin=mul(add(vec(views[0].pose.position),vec(views[1].pose.position)),.5f);origin_rotation=yaw_only(quat(views[0].pose.orientation));origin_set=true;recenter_requested=false;
            cover_calibration_pending=true;
        }
        bool head_tracked=tracking&&frame.shouldRender&&(vs.viewStateFlags&XR_VIEW_STATE_POSITION_TRACKED_BIT);
        if(paused&&!menu_placed&&tracking&&origin_set){   /* 1.5 m in front, a little below the eyes, upright, facing the player */
            const V3 eyes_mid=mul(add(relative_position(views[0].pose.position),relative_position(views[1].pose.position)),.5f);
            menu_rotation=yaw_only(relative_rotation(views[0].pose.orientation));
            menu_center=add(eyes_mid,rotate(menu_rotation,v3(0,-.12f,-1.5f)));menu_placed=true;
        }
        sync_input(frame.predictedDisplayTime,head_tracked,(views[0].pose.position.y+views[1].pose.position.y)*.5f);
        parts[0]+=wall_ms()-stage_start;
        uint64_t feedback_time=SDL_GetTicks64();
        float shot_age=recoil_started?(float)(feedback_time-recoil_started):-1;
        float tracer_alpha=qtracer_opacity(&tracer,feedback_time);
        float recoil=qgun_recoil(shot_age),flash=paused?0:qgun_flash(shot_age);
        XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        bool draw_frame=frame.shouldRender&&tracking;
        bool frame_ok=true;
        float v[2][16],p[2][16];
        for(int i=0;i<2;i++){
            view_matrix(v[i],relative_position(views[i].pose.position),relative_rotation(views[i].pose.orientation));
            projection(p[i],views[i].fov.angleLeft,views[i].fov.angleRight,views[i].fov.angleDown,views[i].fov.angleUp);
        }
        hud_camera();
        bool stereo_frame=false;
        const bool menu_shown=draw_frame&&focused&&paused&&menu_placed,menu_under=menu_shown&&menu_layer.ok;
        for(int i=0;draw_frame&&i<2;i++){
            stage_start=wall_ms();
            struct Eye *eye=&eyes[i];uint32_t index;
            XrSwapchainImageAcquireInfo acq={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(!XR(xrAcquireSwapchainImage(eye->chain,&acq,&index))){frame_ok=false;break;}
            XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
            if(!XR(xrWaitSwapchainImage(eye->chain,&wait))){frame_ok=false;break;}
            parts[1]+=wall_ms()-stage_start;stage_start=wall_ms();
            qgl_target(i,eye->fb[index]);
            glViewport(0,0,eye->w,eye->h);
            qgl_eye(v[i],p[i]);
            qtc2_draw(v[i],p[i],eye->w,eye->h,eye->fb[index]);
            if(options.laser_enabled&&focused&&aim_valid&&!paused)qgl_pointer(gun_origin,gun_hit);
            else if(options.tracer_enabled&&focused&&gun_tracked&&!paused&&tracer_alpha>0)
                qgl_tracer(tracer.origin,tracer.end,tracer_alpha);
            qgl_flush();
            parts[2]+=wall_ms()-stage_start;stage_start=wall_ms();
            if(focused&&gun_tracked)qgun_draw(v[i],p[i],gun_position,gun_rotation,recoil,trigger,flash);
            if(focused){
                V3 head=mul(add(relative_position(views[0].pose.position),relative_position(views[1].pose.position)),.5f);
                online_overlays(v[i],p[i],head,relative_rotation(views[0].pose.orientation));
                if(paused&&menu_placed){
                    menu_host_refresh();qui_draw_menu(v[i],p[i],menu_center,menu_rotation,&menu_host,SDL_GetTicks64()/1000.0,menu_under);
                    if(menu_laser&&gun_tracked){qgl_eye(v[i],p[i]);qgl_pointer(gun_origin,menu_end);qgl_flush();}
                }
            }
            capture_eye(i,eye->w,eye->h);
            parts[3]+=wall_ms()-stage_start;stage_start=wall_ms();
            const GLenum discard[]={GL_DEPTH_ATTACHMENT};glInvalidateFramebuffer(GL_FRAMEBUFFER,1,discard);
            GLenum error=glGetError();if(error)fprintf(stderr,"[XR] eye %d GL error 0x%x\n",i,error);
#ifdef TCVR_PC
            if(i==0){
                int mw,mh;SDL_GL_GetDrawableSize(window,&mw,&mh);
                glBindFramebuffer(GL_READ_FRAMEBUFFER,eye->fb[index]);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
                glBlitFramebuffer(0,0,eye->w,eye->h,0,0,mw,mh,GL_COLOR_BUFFER_BIT,GL_LINEAR);
                SDL_GL_SwapWindow(window);glBindFramebuffer(GL_FRAMEBUFFER,eye->fb[index]);
            }
#endif
            glFlush();
            XrSwapchainImageReleaseInfo rel={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};if(!XR(xrReleaseSwapchainImage(eye->chain,&rel))){frame_ok=false;break;}
            pv[i].pose=views[i].pose;pv[i].fov=views[i].fov;pv[i].subImage.swapchain=eye->chain;pv[i].subImage.imageRect.extent=(XrExtent2Di){eye->w,eye->h};
            parts[4]+=wall_ms()-stage_start;
        }
        XrCompositionLayerProjection layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=local_space;layer.viewCount=2;layer.views=pv;
        XrCompositionLayerQuad menu_quad={XR_TYPE_COMPOSITION_LAYER_QUAD};
        const XrCompositionLayerBaseHeader *layers[2]={(const XrCompositionLayerBaseHeader*)&layer};uint32_t layer_count=1;
        if(menu_under&&frame_ok&&menu_layer_update()){   /* the menu first (under), the picture over it, blended by its alpha */
            const V3 at=add(origin,rotate(origin_rotation,menu_center));const Q4 facing=product(origin_rotation,menu_rotation);
            menu_quad.space=local_space;menu_quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
            menu_quad.subImage.swapchain=menu_layer.chain;menu_quad.subImage.imageRect.extent=(XrExtent2Di){QMENU_W,QMENU_H};
            menu_quad.pose.position=(XrVector3f){at.x,at.y,at.z};menu_quad.pose.orientation=(XrQuaternionf){facing.x,facing.y,facing.z,facing.w};
            menu_quad.size=(XrExtent2Df){QUI_PANEL_W,QUI_PANEL_H};
            layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            layers[0]=(const XrCompositionLayerBaseHeader*)&menu_quad;layers[1]=(const XrCompositionLayerBaseHeader*)&layer;layer_count=2;
        }
        XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=draw_frame&&frame_ok?layer_count:0;end.layers=layers;
        stage_start=wall_ms();
        if(!XR(xrEndFrame(session,&end))||!frame_ok)return false;
        parts[5]+=wall_ms()-stage_start;
        if(draw_frame&&focused&&!paused){for(int j=0;j<6;j++)profile_parts[j]+=parts[j];profile_thread+=cpu_ms()-cpu_start;}
        else{memset(profile_parts,0,sizeof profile_parts);profile_thread=0;}
        frame_profile(render_start,frame.predictedDisplayPeriod,draw_frame&&focused&&!paused);
        if(skipping)skip_until=SDL_GetPerformanceCounter()+(Uint64)(SDL_GetPerformanceFrequency()*(frame.predictedDisplayPeriod*.75e-9));
        if(sim_held(tracking)){ss22_input_neutral();game_deadline=0;continue;}
        if(tc2_online_may_advance()&&qclock_take(&game_deadline,frame.predictedDisplayTime)){ss22_input_update();return true;}
    }
}
#endif
void ss22_input_init(const ss22_input_game *g){(void)g;replay_input=getenv("TC2_REPLAY")!=NULL;}
void ss22_input_update(void){
    /* The upstream recording stores CHANGES, not a full state every frame.
     * Live input must not overwrite the held replay state between those lines. */
    if(replay_input)return;
    uint16_t bits=0;
    if(focused&&!paused){
        bool fire=trigger>.55f;
        /* Arcade fire is edge-triggered. A handoff while the old trigger is
         * held needs one released game tick, then the queued new press.
         * Also retain taps shorter than a 59.906 Hz simulation tick. */
        if(shot_needs_release){fire=false;shot_needs_release=false;}
        else if(shot_pending){fire=true;shot_pending=false;}
        if(fire)bits|=0x10;if(pedal>.55f)bits|=0x20;if(coin_frames){if((coin_frames%12)>5)bits|=1;--coin_frames;}
    }
    fire_output_down=(bits&0x10)!=0;
    g_ss22_gun_off=!aim_valid;g_ss22_gun_x=(uint16_t)(91+fminf(1,fmaxf(0,nx))*733);g_ss22_gun_y=(uint16_t)(38+fminf(1,fmaxf(0,ny))*247);
    tc2_inputs=bits;
}
void ss22_input_neutral(void){trigger=pedal=0;shot_pending=shot_needs_release=fire_output_down=false;if(!replay_input)tc2_inputs=0;}
const eng_ui_page *ss22_input_page(void){return NULL;}
void ss22_input_event(const SDL_Event *e){(void)e;}
void ss22_input_motor(uint8_t b){(void)b;}
void ss22_input_close(void){}
bool ss22_input_aim(float *x,float *y){*x=nx;*y=ny;return aim_valid;}
void ss22_input_rumble(uint16_t lo,uint16_t hi,uint32_t ms){
    (void)lo;if(!focused||!running)return;XrHapticActionInfo info={XR_TYPE_HAPTIC_ACTION_INFO};info.action=haptic_action;
    info.subactionPath=hand_paths[weapon_hand()];
    XrHapticVibration vibration={XR_TYPE_HAPTIC_VIBRATION};vibration.duration=(XrDuration)ms*1000000;vibration.frequency=XR_FREQUENCY_UNSPECIFIED;vibration.amplitude=hi/65535.f;
    xrApplyHapticFeedback(session,&info,(XrHapticBaseHeader*)&vibration);
}
#ifndef TCVR_INPUT_TEST
void ss22_out_poll(uint16_t outputs){static uint16_t previous;uint16_t rise=outputs&~previous;previous=outputs;if(rise&2){shot_feedback(SDL_GetTicks64());ss22_input_rumble(0,48000,45);}}
void ss22_out_close(void){}
/* RESTART APP (the menu): a new copy of the program, once this one has shut the headset session down. On Quest, MainActivity
 * hands that to a small helper process that waits for this one to end, then opens the game again. */
static void relaunch(void){
#ifdef TCVR_PC
    STARTUPINFOW si={sizeof si};PROCESS_INFORMATION pi;wchar_t *line=_wcsdup(GetCommandLineW());
    if(line&&CreateProcessW(NULL,line,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);fprintf(stderr,"[OPTIONS] restarted\n");}
    else fprintf(stderr,"[OPTIONS] restart failed: %lu\n",GetLastError());
    free(line);
#else
    JNIEnv *env=SDL_AndroidGetJNIEnv();jobject activity=SDL_AndroidGetActivity();
    if(env&&activity){
        jclass cls=(*env)->GetObjectClass(env,activity);jmethodID m=(*env)->GetMethodID(env,cls,"relaunch","()V");
        if(m)(*env)->CallVoidMethod(env,activity,m);
        if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env,cls);(*env)->DeleteLocalRef(env,activity);
    }
#endif
}
bool ss22_host_active(void){return active;}
bool ss22_host_restart_requested(void){return false;}
bool ss22_host_open_headless(void){return false;}
bool ss22_host_pointer(float *x,float *y,bool *inside){*x=nx;*y=ny;*inside=aim_valid;return active;}
void ss22_host_shot(const char *p){(void)p;}
void ss22_host_close(void){
    active=false;
    if(context&&gpu_ready){qui_shutdown();qgun_shutdown();qgl_shutdown();}
    menu_layer_close();
    for(int i=0;i<2;i++){if(context&&gpu_ready&&eyes[i].fb)glDeleteFramebuffers(eyes[i].n,eyes[i].fb);if(context&&gpu_ready)glDeleteRenderbuffers(1,&eyes[i].depth);if(eyes[i].chain)xrDestroySwapchain(eyes[i].chain);free(eyes[i].images);free(eyes[i].fb);memset(&eyes[i],0,sizeof eyes[i]);}
    for(int i=0;i<HAND_COUNT;i++){if(aim_spaces[i])xrDestroySpace(aim_spaces[i]);aim_spaces[i]=0;}
    if(local_space)xrDestroySpace(local_space);
    if(session)xrDestroySession(session);if(action_set)xrDestroyActionSet(action_set);if(instance)xrDestroyInstance(instance);
    local_space=0;session=0;action_set=0;instance=0;running=false;
    if(context)SDL_GL_DeleteContext(context);context=NULL;gpu_ready=false;if(window)SDL_DestroyWindow(window);window=NULL;
    if(restart_app){restart_app=false;relaunch();}
    SDL_Quit();
}
#endif
