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
#ifdef TCVR_STEAM_FRAME
#include "quest_frame.h"
#endif
#include "quest_ui.h"
#include "quest_tracer.h"
#include "tc2_cabinet.h"

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
#ifdef TCVR_STEAM_FRAME
static XrAction view_action,cover_mode_action;
static XrPath frame_profile_path,touch_profile_path;
static bool frame_profile_supported,frame_profile_active,recenter_calibration_requested;
typedef struct ViewHold { bool synced,down,ignored,did_recenter,did_calibrate;XrTime down_since; } ViewHold;
static ViewHold view_hold;
typedef struct FrameProbe { bool enabled,force_render,capture_once,owns_capture_marker;unsigned max_frames,max_seconds,capture_after_ticks,frames,game_ticks;uint64_t started_ms; } FrameProbe;
static FrameProbe frame_probe;
static unsigned frame_probe_captured_eyes;
#else
static bool frame_profile_active;
#endif
static XrSessionState state;
static bool running,quit,active,focused,paused,origin_set,recenter_requested;
static bool multiview;
typedef struct ButtonEdge { bool synced,was; } ButtonEdge;
static ButtonEdge coin_button,recenter_button,pause_button,laser_button,cover_button,hand_button,pitch_reset_button;
static bool pitch_armed,scale_armed,filter_armed;
static int scale_direction;
static XrTime scale_repeat_time;
static int pitch_direction;
static XrTime pitch_repeat_time;
static XrAction distance_action;
static bool distance_armed;
static int distance_direction;
static XrTime distance_repeat_time;
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
static int options_page;
static bool page_armed;
static bool replay_input;
static int coin_frames;

static struct Eye { XrSwapchain chain;uint32_t w,h,n;QSwapchainImage *images;GLuint *fb,depth; } eyes[2];
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
#ifdef TCVR_STEAM_FRAME
static uint64_t frame_probe_now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
#endif

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
#ifdef TCVR_STEAM_FRAME
    /* SteamVR owns the Frame display mode; follow predictedDisplayPeriod. */
    return;
#else
    if(!request_rate||preferred_rate<=0)return;
    rate_attempts++;
    XrResult result=request_rate(session,preferred_rate);
    fprintf(stderr,"[XR] display request %.0f Hz attempt %u: %s\n",preferred_rate,rate_attempts,XR(result)?"accepted (not yet confirmed)":"rejected");
#endif
}
static void display_rate_check(XrTime now){
#ifdef TCVR_STEAM_FRAME
    (void)now;
#else
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
#endif
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
    float actual=0;
#ifdef TCVR_STEAM_FRAME
    if(get_rate&&XR(get_rate(session,&actual)))fprintf(stderr,"[XR] Frame follows runtime display rate %.0f Hz\n",actual);
#else
    if(get_rate&&XR(get_rate(session,&actual)))fprintf(stderr,"[XR] display requested %.0f Hz, current %.0f Hz\n",preferred_rate,actual);
#endif
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
#ifdef TCVR_STEAM_FRAME
static void frame_probe_load(void){
    FILE *file=fopen("frame-probe.cfg","r");if(!file)return;
    bool has_limit=false;frame_probe.max_frames=3600;frame_probe.max_seconds=30;frame_probe.capture_after_ticks=600;
    char line[128],key[64];int value;
    while(fgets(line,sizeof line,file)){
        if(sscanf(line," %63[^=]=%d",key,&value)!=2)continue;
        if(!strcmp(key,"force_render"))frame_probe.force_render=value!=0;
        else if(!strcmp(key,"capture_once"))frame_probe.capture_once=value!=0;
        else if(!strcmp(key,"exit_after_frames")){has_limit=true;if(value<1)value=1;if(value>3600)value=3600;frame_probe.max_frames=(unsigned)value;}
        else if(!strcmp(key,"exit_after_seconds")){has_limit=true;if(value<1)value=1;if(value>120)value=120;frame_probe.max_seconds=(unsigned)value;}
        else if(!strcmp(key,"capture_after_ticks")){if(value<0)value=0;if(value>3600)value=3600;frame_probe.capture_after_ticks=(unsigned)value;}
    }
    fclose(file);unlink("frame-probe.cfg");
    frame_probe.enabled=frame_probe.force_render||frame_probe.capture_once||has_limit;
    if(!frame_probe.enabled)return;
    if(frame_probe.capture_once){
        unsigned frame_bound=frame_probe.max_frames/2, time_bound=frame_probe.max_seconds*30;
        unsigned bound=frame_bound<time_bound?frame_bound:time_bound;
        if(frame_probe.capture_after_ticks>bound){
            fprintf(stderr,"[vr] Frame probe capture tick %u cannot fit the configured bounds (conservative maximum %u); capture disabled\n",
                frame_probe.capture_after_ticks,bound);
            frame_probe.capture_once=false;
        }
    }
    frame_probe.started_ms=frame_probe_now_ms();
    fprintf(stderr,"[vr] one-shot Frame probe enabled: force_render=%d capture_once=%d capture_after_ticks=%u max_frames=%u max_seconds=%u\n",
        frame_probe.force_render,frame_probe.capture_once,frame_probe.capture_after_ticks,frame_probe.max_frames,frame_probe.max_seconds);
}
static void frame_probe_views(void){
    const float half_fov=.72f;
    origin=v3(0,0,0);origin_rotation=(Q4){0,0,0,1};origin_set=true;recenter_requested=false;
    for(int eye=0;eye<2;eye++){
        views[eye]=(XrView){XR_TYPE_VIEW};views[eye].pose.orientation.w=1;
        views[eye].pose.position=(XrVector3f){eye?.032f:-.032f,1.65f,0};
        views[eye].fov=(XrFovf){-half_fov,half_fov,half_fov,-half_fov};
    }
}
static void frame_probe_finish(const char *reason){
    fprintf(stderr,"[vr] one-shot Frame probe exited (%s) after %u XR frame steps and %u arcade updates\n",
        reason,frame_probe.frames,frame_probe.game_ticks);
    if(frame_probe.capture_once)fprintf(stderr,"[vr] Frame probe exited without paired eye capture at tick %u/%u\n",
        frame_probe.game_ticks,frame_probe.capture_after_ticks);
    if(frame_probe.owns_capture_marker)unlink("capture.request");
    frame_probe.enabled=false;quit=true;
}
static void frame_probe_game_tick(void){
    if(!frame_probe.enabled||!frame_probe.force_render||paused)return;
    frame_probe.game_ticks++;
    if(frame_probe.capture_once&&frame_probe.game_ticks>=frame_probe.capture_after_ticks&&access("capture.request",F_OK)!=0){
        FILE *marker=fopen("capture.request","w");
        if(marker){fclose(marker);frame_probe.owns_capture_marker=true;fprintf(stderr,"[vr] Frame probe capture armed at arcade tick %u\n",frame_probe.game_ticks);}
    }
}
#endif
static int weapon_hand(void){return active_weapon_hand;}
static void reset_fire_input(void){
    tracer.active=false;
    memset(fire_triggers,0,sizeof fire_triggers);shot_pending=shot_needs_release=fire_output_down=false;
}
static void reset_pitch_input(void){pitch_armed=scale_armed=false;pitch_direction=scale_direction=0;pitch_repeat_time=scale_repeat_time=0;}
static void reset_distance_input(void){page_armed=false;distance_armed=false;distance_direction=0;distance_repeat_time=0;}
static void reset_input_edges(void){
    filter_armed=false;
    coin_button=recenter_button=pause_button=laser_button=cover_button=hand_button=pitch_reset_button=(ButtonEdge){0};
#ifdef TCVR_STEAM_FRAME
    view_hold=(ViewHold){0};
#endif
    reset_pitch_input();
    reset_distance_input();
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
static void change_options_page(int direction){
    options_page=(options_page+direction+QOPTIONS_PAGE_COUNT)%QOPTIONS_PAGE_COUNT;
    reset_pitch_input();reset_distance_input();
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
static void adjust_horizontal(int direction){
    if(options_page==QOPTIONS_PAGE_GRAPHICS)set_resolution((options.resolution+direction+QRESOLUTION_COUNT)%QRESOLUTION_COUNT);
    else if(options_page==1)set_gun_offset(false,options.gun_offset_z_mm+direction*QOPTIONS_GUN_OFFSET_STEP);
    else set_world_scale(options.world_scale+direction*QOPTIONS_WORLD_SCALE_STEP);
}
static void adjust_vertical(int direction){
    if(options_page==QOPTIONS_PAGE_GRAPHICS)return;
    if(options_page==1)set_gun_offset(true,options.gun_offset_y_mm+direction*QOPTIONS_GUN_OFFSET_STEP);
    else set_gun_pitch(options.gun_pitch+direction);
}
static void reset_page_setting(void){
    if(options_page==QOPTIONS_PAGE_GRAPHICS)set_resolution(0);
    else if(options_page==1){set_gun_offset(true,0);set_gun_offset(false,0);}
    else set_gun_pitch(0);
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
#ifdef TCVR_STEAM_FRAME
    if(!action(&view_action,"view_recenter","Hold View to recenter or calibrate",XR_ACTION_TYPE_BOOLEAN_INPUT,true)||
       !action(&cover_mode_action,"cover_mode","Change cover mode in options",XR_ACTION_TYPE_BOOLEAN_INPUT,false))return false;
    frame_profile_path=path("/interaction_profiles/valve/frame_controller_valve");
    touch_profile_path=path("/interaction_profiles/oculus/touch_controller");
    if(frame_profile_supported){
        XrActionSuggestedBinding frame[]={
            {aim_action,path("/user/hand/left/input/aim/pose")},
            {aim_action,path("/user/hand/right/input/aim/pose")},
            {trigger_action,path("/user/hand/left/input/trigger/value")},
            {trigger_action,path("/user/hand/right/input/trigger/value")},
            {grip_action,path("/user/hand/left/input/squeeze/value")},
            {grip_action,path("/user/hand/right/input/squeeze/value")},
            {lower_action,path("/user/hand/right/input/a/click")},
            {upper_action,path("/user/hand/right/input/b/click")},
            {cover_mode_action,path("/user/hand/right/input/x/click")},
            {pause_action,path("/user/hand/right/input/menu/click")},
            {view_action,path("/user/hand/left/input/view/click")},
            {hand_action,path("/user/hand/right/input/thumbstick/click")},
            {pitch_action,path("/user/hand/right/input/thumbstick")},
            {pitch_reset_action,path("/user/hand/left/input/thumbstick/click")},
            {distance_action,path("/user/hand/left/input/thumbstick")},
            {haptic_action,path("/user/hand/left/output/haptic")},
            {haptic_action,path("/user/hand/right/output/haptic")}};
        XrInteractionProfileSuggestedBinding frame_suggest={XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        frame_suggest.interactionProfile=frame_profile_path;
        frame_suggest.countSuggestedBindings=(uint32_t)(sizeof frame/sizeof frame[0]);
        frame_suggest.suggestedBindings=frame;
        if(!XR(xrSuggestInteractionProfileBindings(instance,&frame_suggest)))return false;
    }
#endif
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
#ifdef TCVR_STEAM_FRAME
    suggest.interactionProfile=touch_profile_path;
#else
    suggest.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
#endif
    suggest.countSuggestedBindings=sizeof(bindings)/sizeof(bindings[0]);suggest.suggestedBindings=bindings;
    if(!XR(xrSuggestInteractionProfileBindings(instance,&suggest)))return false;
    XrSessionActionSetsAttachInfo attach={XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&action_set;
    if(!XR(xrAttachSessionActionSets(session,&attach)))return false;
    XrActionSpaceCreateInfo space={XR_TYPE_ACTION_SPACE_CREATE_INFO};space.action=aim_action;space.poseInActionSpace.orientation.w=1;
    for(int i=0;i<HAND_COUNT;i++){space.subactionPath=hand_paths[i];if(!XR(xrCreateActionSpace(session,&space,&aim_spaces[i])))return false;}
    return true;
}
#ifdef TCVR_STEAM_FRAME
static void update_frame_profile(void){
    if(!session)return;
    XrInteractionProfileState current={XR_TYPE_INTERACTION_PROFILE_STATE};
    if(XR_SUCCEEDED(xrGetCurrentInteractionProfile(session,hand_paths[RIGHT_HAND],&current))){
        bool frame=frame_profile_supported&&current.interactionProfile==frame_profile_path;
        if(frame!=frame_profile_active){
            frame_profile_active=frame;reset_input_edges();
            fprintf(stderr,"[XR] controller profile %s\n",frame?"Steam Frame":"Touch fallback");
        }
    }
}
static void frame_view_haptic(float amplitude,XrDuration duration){
    XrHapticActionInfo info={XR_TYPE_HAPTIC_ACTION_INFO};info.action=haptic_action;
    XrHapticVibration vibration={XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration=duration;vibration.frequency=XR_FREQUENCY_UNSPECIFIED;vibration.amplitude=amplitude;
    for(int hand=0;hand<HAND_COUNT;hand++){
        info.subactionPath=hand_paths[hand];
        (void)xrApplyHapticFeedback(session,&info,(XrHapticBaseHeader*)&vibration);
    }
}
static void frame_view_button(XrTime time){
    XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=view_action;get.subactionPath=hand_paths[LEFT_HAND];
    XrActionStateBoolean current={XR_TYPE_ACTION_STATE_BOOLEAN};
    if(XR_FAILED(xrGetActionStateBoolean(session,&get,&current))||!current.isActive){view_hold=(ViewHold){0};return;}
    bool down=current.currentState;
    if(!view_hold.synced){view_hold.synced=true;view_hold.down=down;view_hold.ignored=down;view_hold.down_since=down?time:0;return;}
    if(down&&!view_hold.down){view_hold.down=true;view_hold.ignored=false;view_hold.did_recenter=false;view_hold.did_calibrate=false;view_hold.down_since=time;}
    if(down&&view_hold.down&&!view_hold.ignored){
        XrTime held=time-view_hold.down_since;
        if(held>=1000000000LL&&!view_hold.did_recenter){
            recenter_requested=true;view_hold.did_recenter=true;frame_view_haptic(.6f,80000000LL);
            fprintf(stderr,"[XR] View hold: recenter requested\n");
        }
        if(held>=3000000000LL&&!view_hold.did_calibrate){
            recenter_requested=true;recenter_calibration_requested=true;cover_calibration_pending=true;view_hold.did_calibrate=true;
            frame_view_haptic(.6f,80000000LL);
            fprintf(stderr,"[XR] View hold: upright height calibration requested\n");
        }
    }
    if(!down&&view_hold.down){
        if(!view_hold.ignored&&!view_hold.did_recenter&&paused){
            paused=false;reset_fire_input();reset_pitch_input();game_deadline=0;tc2_audio_volume(100);ss22_input_neutral();
            fprintf(stderr,"[XR] View tap: resume\n");
        }
        view_hold.down=false;view_hold.ignored=false;view_hold.did_recenter=false;view_hold.did_calibrate=false;view_hold.down_since=0;
    }
}
#endif
/* Fresh edges only: a held button across focus/tracking loss cannot change a setting. */
static bool pressed(XrAction a,XrPath hand,ButtonEdge *button){
    XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO};info.action=a;info.subactionPath=hand;
    XrActionStateBoolean s={XR_TYPE_ACTION_STATE_BOOLEAN};
    if(XR_FAILED(xrGetActionStateBoolean(session,&info,&s))||!s.isActive){button->synced=false;return false;}
    bool edge=button->synced&&s.currentState&&!button->was;button->was=s.currentState;button->synced=true;return edge;
}
static bool axis_state(XrAction a,XrPath hand,XrActionStateFloat *s){XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO};info.action=a;info.subactionPath=hand;*s=(XrActionStateFloat){XR_TYPE_ACTION_STATE_FLOAT};return XR_SUCCEEDED(xrGetActionStateFloat(session,&info,s))&&s->isActive;}
static float axis(XrAction a,XrPath hand){XrActionStateFloat s;return axis_state(a,hand,&s)?s.currentState:0;}
static void adjust_texture_filter(void){
    XrActionStateFloat grip;
    /* Require release after opening the menu, losing focus or reconnecting. */
    if(!paused||!axis_state(grip_action,hand_paths[LEFT_HAND],&grip)||!isfinite(grip.currentState)){
        filter_armed=false;return;
    }
    if(grip.currentState<.2f)filter_armed=true;
    else if(grip.currentState>.55f&&filter_armed){filter_armed=false;toggle_texture_filter();}
}
static void adjust_gun_pitch(XrTime time){
    bool reset=pressed(pitch_reset_action,XR_NULL_PATH,&pitch_reset_button);
    if(!paused){reset_pitch_input();return;}
    if(reset){reset_page_setting();reset_pitch_input();return;}
    XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=pitch_action;
    XrActionStateVector2f stick={XR_TYPE_ACTION_STATE_VECTOR2F};
    if(XR_FAILED(xrGetActionStateVector2f(session,&get,&stick))||!stick.isActive||!isfinite(stick.currentState.x)||!isfinite(stick.currentState.y)){
        reset_pitch_input();return;
    }
    float x=stick.currentState.x,y=stick.currentState.y;
    if(options_page==QOPTIONS_PAGE_GRAPHICS){
        if(fabsf(x)<.25f)scale_armed=true;
        else if(scale_armed&&fabsf(x)>=.65f&&fabsf(x)>fabsf(y)){
            adjust_horizontal(x>0?1:-1);scale_armed=false;
        }
        return;
    }
    if(fabsf(x)>fabsf(y))y=0;else x=0;
    if(fabsf(x)<.25f){scale_armed=true;scale_direction=0;scale_repeat_time=0;}
    else if(scale_armed&&fabsf(x)>=.65f){
        int direction=x>0?1:-1;
        if(direction!=scale_direction){adjust_horizontal(direction);scale_direction=direction;scale_repeat_time=time+400000000LL;}
        else if(time>=scale_repeat_time){adjust_horizontal(direction);scale_repeat_time=time+100000000LL;}
    }
    if(fabsf(y)<.25f){pitch_armed=true;pitch_direction=0;pitch_repeat_time=0;return;}
    if(!pitch_armed||fabsf(y)<.65f)return;
    int direction=y>0?1:-1;
    if(direction!=pitch_direction){adjust_vertical(direction);pitch_direction=direction;pitch_repeat_time=time+400000000LL;}
    else if(time>=pitch_repeat_time){adjust_vertical(direction);pitch_repeat_time=time+100000000LL;}
}
static void adjust_view_distance(XrTime time){
    if(!paused){reset_distance_input();return;}
    XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=distance_action;
    XrActionStateVector2f stick={XR_TYPE_ACTION_STATE_VECTOR2F};
    if(XR_FAILED(xrGetActionStateVector2f(session,&get,&stick))||!stick.isActive||!isfinite(stick.currentState.x)||!isfinite(stick.currentState.y)){
        reset_distance_input();return;
    }
    float x=stick.currentState.x,y=stick.currentState.y;
    if(fabsf(y)<.25f)page_armed=true;
    if(fabsf(y)>=.65f&&fabsf(x)<.25f&&page_armed){
        change_options_page(y>0?1:-1);return;
    }
    if(options_page==QOPTIONS_PAGE_GRAPHICS)return;
    if(fabsf(x)<.25f){distance_armed=true;distance_direction=0;distance_repeat_time=0;return;}
    if(!distance_armed||fabsf(x)<.65f||fabsf(x)<fabsf(stick.currentState.y))return;
    int direction=x>0?1:-1;
    if(direction!=distance_direction){if(options_page)set_player_side(direction>0?2:1);else set_view_distance(options.view_distance+direction*QOPTIONS_VIEW_DISTANCE_STEP);distance_direction=direction;distance_repeat_time=time+400000000LL;}
    else if(time>=distance_repeat_time){if(options_page)set_player_side(direction>0?2:1);else set_view_distance(options.view_distance+direction*QOPTIONS_VIEW_DISTANCE_STEP);distance_repeat_time=time+100000000LL;}
}
static V3 vec(XrVector3f v){return v3(v.x,v.y,v.z);}
static Q4 quat(XrQuaternionf q){return (Q4){q.x,q.y,q.z,q.w};}
static V3 relative_position(XrVector3f p){return rotate(conjugate(origin_rotation),sub(vec(p),origin));}
static Q4 relative_rotation(XrQuaternionf q){return product(conjugate(origin_rotation),quat(q));}

static bool capture_eye(int eye,int w,int h){
    if(access("capture.request",F_OK))return false;
#ifdef TCVR_STEAM_FRAME
    if(frame_probe.enabled&&frame_probe.capture_once&&eye==0)frame_probe_captured_eyes=0;
#endif
    unsigned char *pixels=malloc((size_t)w*h*4);if(!pixels)return false;
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    char name[32];snprintf(name,sizeof name,"eye-%d.ppm",eye);FILE *f=fopen(name,"wb");
    bool saved=false;
    if(f){
        saved=fprintf(f,"P6\n%d %d\n255\n",w,h)>0;
        for(int y=h-1;y>=0&&saved;y--)for(int x=0;x<w;x++)if(fwrite(pixels+((size_t)y*w+x)*4,1,3,f)!=3){saved=false;break;}
        if(fclose(f))saved=false;
        if(saved)fprintf(stderr,"[XR] captured %s\n",name);else fprintf(stderr,"[XR] failed to finish %s\n",name);
    }
    free(pixels);
#ifdef TCVR_STEAM_FRAME
    if(frame_probe.enabled&&frame_probe.capture_once){
        if(saved)frame_probe_captured_eyes|=1u<<(unsigned)eye;
        if(frame_probe_captured_eyes==3u){unlink("capture.request");frame_probe.capture_once=false;}
    }else
#endif
    if(eye==1)unlink("capture.request");
    return saved;
}

static bool events(void){
    SDL_Event s;while(SDL_PollEvent(&s)){
        if(s.type==SDL_QUIT)quit=true;
#ifdef TCVR_PC
        if(s.type==SDL_KEYDOWN&&!s.key.repeat){
            if(s.key.keysym.sym==SDLK_ESCAPE){paused=!paused;filter_armed=false;reset_fire_input();reset_pitch_input();reset_distance_input();ss22_input_neutral();}
            if(s.key.keysym.sym==SDLK_c||s.key.keysym.sym==SDLK_5)coin_frames=48;
            if(s.key.keysym.sym==SDLK_l)cycle_aim_guide();
            if(s.key.keysym.sym==SDLK_TAB&&paused){change_options_page(1);}
            if(s.key.keysym.sym==SDLK_r)recenter_requested=true;
            if(s.key.keysym.sym==SDLK_f&&paused)toggle_texture_filter();
            if(s.key.keysym.sym==SDLK_h&&paused)change_weapon_hand();
            if(s.key.keysym.sym==SDLK_UP&&paused)adjust_vertical(1);
            if(s.key.keysym.sym==SDLK_DOWN&&paused)adjust_vertical(-1);
            if(s.key.keysym.sym==SDLK_HOME&&paused){reset_page_setting();}
            if(s.key.keysym.sym==SDLK_LEFT&&paused)adjust_horizontal(-1);
            if(s.key.keysym.sym==SDLK_RIGHT&&paused)adjust_horizontal(1);
            if(s.key.keysym.sym==SDLK_END&&paused&&options_page==0)set_world_scale(QOPTIONS_WORLD_SCALE_DEFAULT);
            if(s.key.keysym.sym==SDLK_PAGEUP&&paused&&options_page!=QOPTIONS_PAGE_GRAPHICS){if(options_page)set_player_side(2);else set_view_distance(options.view_distance+QOPTIONS_VIEW_DISTANCE_STEP);}
            if(s.key.keysym.sym==SDLK_PAGEDOWN&&paused&&options_page!=QOPTIONS_PAGE_GRAPHICS){if(options_page)set_player_side(1);else set_view_distance(options.view_distance-QOPTIONS_VIEW_DISTANCE_STEP);}
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
            if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo b={XR_TYPE_SESSION_BEGIN_INFO};b.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;if(!XR(xrBeginSession(session,&b)))return false;running=true;game_deadline=0;performance_start();
#ifdef TCVR_STEAM_FRAME
                update_frame_profile();
#endif
            }
            if(state==XR_SESSION_STATE_STOPPING){if(running)xrEndSession(session);running=false;game_deadline=0;}
            if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)quit=true;
            bool focus=state==XR_SESSION_STATE_FOCUSED;
            if(focused!=focus){focused=focus;reset_input_edges();game_deadline=0;ss22_input_neutral();tc2_audio_volume(focus&&!paused?100:0);
#ifndef TCVR_STEAM_FRAME
                if(focus){rate_attempts=0;rate_check_time=0;}
#endif
            }
        }else if(e.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)quit=true;
        else if(e.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){recenter_requested=true;
#ifdef TCVR_STEAM_FRAME
            recenter_calibration_requested=true;
#endif
        }
#ifdef TCVR_STEAM_FRAME
        else if(e.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)update_frame_profile();
#endif
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
#ifdef TCVR_STEAM_FRAME
    if(frame_probe.enabled&&frame_probe_now_ms()-frame_probe.started_ms>=frame_probe.max_seconds*1000ULL){
        fprintf(stderr,"[vr] one-shot Frame probe reached its %u second deadline\n",frame_probe.max_seconds);
        frame_probe_finish("time limit");
    }
#endif
    return !quit&&(result==XR_EVENT_UNAVAILABLE||XR(result));
}
static void sync_input(XrTime time,bool head_tracked,float head_y){
    trigger=pedal=0;aim_valid=gun_tracked=false;
    if(!focused){reset_input_edges();return;}
    XrActiveActionSet aset={action_set,XR_NULL_PATH};XrActionsSyncInfo sync={XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&aset;
    if(!XR(xrSyncActions(session,&sync))){reset_input_edges();return;}
    if(pressed(pause_action,XR_NULL_PATH,&pause_button)){paused=!paused;filter_armed=false;reset_fire_input();reset_pitch_input();reset_distance_input();game_deadline=0;tc2_audio_volume(paused?0:100);ss22_input_neutral();}
    adjust_texture_filter();
    bool change_hand=pressed(hand_action,XR_NULL_PATH,&hand_button);
    if(paused&&change_hand){change_weapon_hand();return;}
    adjust_gun_pitch(time);
    adjust_view_distance(time);
#ifdef TCVR_STEAM_FRAME
    if(frame_profile_active){
        frame_view_button(time);
        if(pressed(lower_action,hand_paths[RIGHT_HAND],&coin_button))coin_frames=48;
        if(pressed(upper_action,hand_paths[RIGHT_HAND],&laser_button))cycle_aim_guide();
        if(paused&&pressed(cover_mode_action,XR_NULL_PATH,&cover_button)){
            options.physical_crouch=!options.physical_crouch;options_saved=qoptions_save(options_path,&options);
            if(options.physical_crouch)cover_calibration_pending=true;
            fprintf(stderr,"[OPTIONS] cover %s; saved %d\n",options.physical_crouch?"PHYSICAL":"GRIP",options_saved);
        }
    }else
#endif
    {
    /* Touch roles follow the saved layout, including on Steam Frame fallback. */
    /* Face-button roles follow the saved layout, even during trigger handoffs. */
    int controls=options.left_handed?LEFT_HAND:RIGHT_HAND;XrPath weapon=hand_paths[controls],other=hand_paths[1-controls];
    if(pressed(lower_action,weapon,&coin_button))coin_frames=48;
    if(pressed(lower_action,other,&recenter_button))recenter_requested=true;
    if(pressed(upper_action,weapon,&laser_button))cycle_aim_guide();
    bool change_cover=pressed(upper_action,other,&cover_button);
    if(paused&&change_cover){
        options.physical_crouch=!options.physical_crouch;options_saved=qoptions_save(options_path,&options);
        if(options.physical_crouch)cover_calibration_pending=true;
        fprintf(stderr,"[OPTIONS] cover %s; saved %d\n",options.physical_crouch?"PHYSICAL":"GRIP",options_saved);
    }
    }
    if(!origin_set)return;
    if(cover_calibration_pending&&head_tracked){
        qcover_calibrate(&cover,head_y);cover_calibration_pending=false;
        fprintf(stderr,"[COVER] upright height %.3f m\n",head_y);
    }
    /* Cover must work even when the weapon controller is out of view. */
    bool exposed=qcover_pedal(&cover,head_y,head_tracked);
    if(!paused)pedal=options.physical_crouch?(!cover_calibration_pending&&exposed?1.f:0.f):fmaxf(axis(grip_action,hand_paths[LEFT_HAND]),axis(grip_action,hand_paths[RIGHT_HAND]));
    XrSpaceLocation locations[HAND_COUNT];bool tracked[HAND_COUNT]={false,false},edges[HAND_COUNT]={false,false},available[HAND_COUNT];
    XrActionStateFloat fire[HAND_COUNT];
    XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    for(int h=0;h<HAND_COUNT;h++){
        XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=aim_action;get.subactionPath=hand_paths[h];
        XrActionStatePose pose={XR_TYPE_ACTION_STATE_POSE};locations[h]=(XrSpaceLocation){XR_TYPE_SPACE_LOCATION};
        tracked[h]=XR_SUCCEEDED(xrGetActionStatePose(session,&get,&pose))&&pose.isActive&&
            XR_SUCCEEDED(xrLocateSpace(aim_spaces[h],local_space,time,&locations[h]))&&(locations[h].locationFlags&required)==required;
        available[h]=axis_state(trigger_action,hand_paths[h],&fire[h]);
        FireTrigger *button=&fire_triggers[h];
        if(paused||!tracked[h]||!available[h]){*button=(FireTrigger){0};continue;}
        if(fire[h].currentState<.2f){button->armed=true;button->pressed=false;}
        else if(fire[h].currentState>.55f&&button->armed){button->armed=false;button->pressed=true;edges[h]=true;}
    }
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
}

/* Input tests compile the real action and polling code without a window/runtime. */
#ifndef TCVR_INPUT_TEST
bool tc2_vr_open(void){
    replay_input=getenv("TC2_REPLAY")!=NULL;
    qoptions_load(options_path,&options);tc2_gl_set_filter(options.texture_filtering);qvr_world_scale=options.world_scale/100.f;active_weapon_hand=options.left_handed?LEFT_HAND:RIGHT_HAND;
    qvr_view_distance=options.view_distance/100.f;
    fprintf(stderr,"[VR SCALE] %.6f reconstructed units/metre; world size %d%%; view distance %d cm\n",qvr_units_per_meter,options.world_scale,options.view_distance);
#ifdef TCVR_STEAM_FRAME
    frame_probe_load();
#endif
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
    const char *extensions[6]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
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
#ifdef TCVR_STEAM_FRAME
    if(XR_SUCCEEDED(enumeration))for(uint32_t j=0;j<available_count;j++)if(!strcmp("XR_VALVE_frame_controller_interaction",available[j].extensionName)){
        extensions[extension_count++]="XR_VALVE_frame_controller_interaction";frame_profile_supported=true;break;
    }
    fprintf(stderr,"[XR] Steam Frame controller profile %s\n",frame_profile_supported?"available":"unavailable; Touch fallback only");
#endif
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
                /* SteamVR owns Frame's display mode; other Android/PC builds
                 * retain their existing 120 Hz request policy. */
#ifndef TCVR_STEAM_FRAME
                float wanted=120;FILE *config=fopen("refresh-rate.txt","r");if(config){if(fscanf(config,"%f",&wanted)!=1)wanted=120;fclose(config);}
#endif
                fprintf(stderr,"[XR] supported refresh rates:");
                for(uint32_t j=0;j<count;j++)fprintf(stderr," %.0f",rates[j]);
#ifdef TCVR_STEAM_FRAME
                fprintf(stderr," Hz; Frame follows SteamVR's selected display mode\n");
#else
                preferred_rate=qrefresh_choose(rates,count,wanted);
                fprintf(stderr," Hz\n");
                fprintf(stderr,"[XR] display preference %.0f Hz; supported selection %.0f Hz\n",wanted,preferred_rate);
#endif
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
#ifdef TCVR_STEAM_FRAME
        int target=2160;FILE *display=fopen("frame-display.cfg","r");
        if(display){char line[128];while(fgets(line,sizeof line,display)){int value;if(sscanf(line,"eye_resolution=%d",&value)==1)target=value;}fclose(display);}
        qframe_eye_dimensions(eye->w,eye->h,configs[i].maxImageRectWidth,configs[i].maxImageRectHeight,target,&eye->w,&eye->h);
        fprintf(stderr,"[vr] eye %d recommended %ux%u runtime max %ux%u target max %d actual %ux%u\n",
            i,configs[i].recommendedImageRectWidth,configs[i].recommendedImageRectHeight,
            configs[i].maxImageRectWidth,configs[i].maxImageRectHeight,
            target<512?512:target>2160?2160:target,eye->w,eye->h);
#elif defined(TCVR_PC)
        float factor=fminf(1,2160.f/fmaxf(eye->w,eye->h));
#else
        float factor=fminf(1,1440.f/fmaxf(eye->w,eye->h));
#endif
#ifndef TCVR_STEAM_FRAME
        eye->w=(uint32_t)(eye->w*factor);eye->h=(uint32_t)(eye->h*factor);
#endif
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
#ifdef TCVR_PC
graphics_ready:
#endif
    if(!qgl_init())goto fail;
#ifndef TCVR_PC
    multiview=false; /* TC2 priority and gamma passes currently render each eye separately. */
#endif
    if(!qgun_init("models/namco-gun.tcgun")&&!qgun_init("models/player-gun.tcgun"))goto fail;
    if(!qgun_load_weapon(QGUN_MACHINE_GUN,"models/machine-gun.tcgun"))
        fprintf(stderr,"[GUN] Machine gun asset unavailable; using pistol fallback\n");
    if(!qui_init())goto fail;
    qui_set_resolution(options.resolution,eyes[0].w,eyes[0].h);
    if(!tc2_gl_init())goto fail;
    void ss22_gl_set_gun_flash(int);ss22_gl_set_gun_flash(0);tc2_audio_open();
    active=true;fprintf(stderr,"[XR] Renderer initialized: %ux%u per eye; %s\n",eyes[0].w,eyes[0].h,glGetString(GL_RENDERER));return true;
fail:
    fprintf(stderr,"[XR] initialization failed; SDL: %s\n",SDL_GetError());ss22_host_close();return false;
}

static void hud_camera(void){float cx,cy,focal;qvr_flat_camera(&cx,&cy,&focal);qgl_flat_camera(cx,cy,focal);}
#ifdef TCVR_PC
static bool desktop_frame(void){
    static Uint64 deadline;static unsigned frame_number;bool fast=getenv("TCVR_FAST")!=NULL;
    for(;;){
        if(!events())return false;
        int w,h,mx,my;SDL_GL_GetDrawableSize(window,&w,&h);int lw,lh;SDL_GetWindowSize(window,&lw,&lh);
        if(w<=0||h<=0||lw<=0||lh<=0){SDL_Delay(20);continue;}
        Uint32 mouse=SDL_GetMouseState(&mx,&my);focused=!!(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS);
        nx=fminf(1,fmaxf(0,(float)mx/lw));ny=fminf(1,fmaxf(0,(float)my/lh));
        aim_valid=true;gun_tracked=false;trigger=focused&&!paused&&(mouse&SDL_BUTTON_LMASK)?1:0;
        pedal=focused&&!paused&&((mouse&SDL_BUTTON_RMASK)||SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SPACE])?1:0;
        /* Match the original 640x480 camera exactly for mouse aiming and tests. */
        int vw=w,vh=w*3/4;if(vh>h){vh=h;vw=h*4/3;}
        nx=((float)mx/lw*w-(w-vw)*.5f)/vw;ny=((float)my/lh*h-(h-vh)*.5f)/vh;
        aim_valid=nx>=0&&nx<=1&&ny>=0&&ny<=1;
        float v[16],p[16];view_matrix(v,v3(0,0,0),(Q4){0,0,0,1});
        projection(p,-atanf(.64f),atanf(.64f),-atanf(.48f),atanf(.48f));
        /* Keep a 4:3 image centered in a resizable window. */
        if((float)w/h>4.f/3.f){p[0]*=(float)vw/w;}else{p[5]*=(float)vh/h;}
        hud_camera();qgl_target(0,0);qgl_eye(v,p);qtc2_draw(v,p,w,h,0);qgl_flush();
        if(options.laser_enabled&&aim_valid&&!paused){qgl_pointer(v3(.24f,-.24f,-.45f),v3((nx-.5f)*3.2f,(.5f-ny)*2.4f,-2.5f));qgl_flush();}
        if(paused)qui_draw(v,p,v3(0,0,0),(Q4){0,0,0,1},&options,options_page,qtc2_cabinet_side,frame_profile_active,options_saved);
        const char *capture_frame=getenv("TCVR_CAPTURE_FRAME");
        if(capture_frame&&++frame_number==strtoul(capture_frame,NULL,10)){FILE *f=fopen("capture.request","w");if(f)fclose(f);}
        if(!access("capture.request",F_OK)){capture_eye(0,w,h);unlink("capture.request");}
        GLenum error=glGetError();if(error)fprintf(stderr,"[GL] desktop error 0x%x\n",error);
        SDL_GL_SwapWindow(window);
        if(!fast){Uint64 now=SDL_GetPerformanceCounter(),freq=SDL_GetPerformanceFrequency();if(!deadline||now>deadline+freq/4)deadline=now;deadline+=freq*1000/59906;while((now=SDL_GetPerformanceCounter())<deadline)SDL_Delay(1);}
        if(!paused){ss22_input_update();return true;}
    }
}
#endif
bool ss22_host_frame(void){
#ifdef TCVR_PC
    if(desktop)return desktop_frame();
#endif
    /* The original game runs at 59.906 Hz. Submit additional poses without advancing its CPU at headset refresh. */
    for(;;){
        if(!events())return false;
#ifdef TCVR_STEAM_FRAME
        bool probe_exit_after_frame=false;
#endif
        if(!running){
            SDL_Delay(10);continue;
        }
        if(game_deadline&&focused&&!paused&&last_tracking&&last_display_time>=game_deadline){
            if(qclock_take(&game_deadline,last_display_time)){
#ifdef TCVR_STEAM_FRAME
                frame_probe_game_tick();
#endif
                ss22_input_update();return true;
            }
        }
        XrFrameWaitInfo wi={XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame={XR_TYPE_FRAME_STATE};if(!XR(xrWaitFrame(session,&wi,&frame)))return false;
#ifdef TCVR_STEAM_FRAME
        if(frame_probe.enabled){frame_probe.frames++;probe_exit_after_frame=frame_probe.frames>=frame_probe.max_frames;}
#endif
        display_rate_check(frame.predictedDisplayTime);
        uint64_t render_start=SDL_GetPerformanceCounter();
        double cpu_start=cpu_ms(),stage_start=wall_ms(),parts[6]={0};
        XrFrameBeginInfo bi={XR_TYPE_FRAME_BEGIN_INFO};if(!XR(xrBeginFrame(session,&bi)))return false;
        XrViewState vs={XR_TYPE_VIEW_STATE};XrViewLocateInfo li={XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=frame.predictedDisplayTime;li.space=local_space;uint32_t n=0;
        bool located=XR(xrLocateViews(session,&li,&vs,2,&n,views));
        XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        bool tracking=located&&n==2&&(vs.viewStateFlags&valid)==valid;
#ifdef TCVR_STEAM_FRAME
        if(frame_probe.enabled&&frame_probe.force_render&&(!focused||!frame.shouldRender||!tracking)){frame_probe_views();tracking=true;}
#endif
        last_display_time=frame.predictedDisplayTime;last_tracking=tracking;
        if(tracking&&(!origin_set||recenter_requested)){
            origin=mul(add(vec(views[0].pose.position),vec(views[1].pose.position)),.5f);origin_rotation=yaw_only(quat(views[0].pose.orientation));origin_set=true;recenter_requested=false;
#ifdef TCVR_STEAM_FRAME
            if(!frame_profile_active||recenter_calibration_requested)cover_calibration_pending=true;
            recenter_calibration_requested=false;
#else
            cover_calibration_pending=true;
#endif
        }
        bool head_tracked=tracking&&frame.shouldRender&&(vs.viewStateFlags&XR_VIEW_STATE_POSITION_TRACKED_BIT);
#ifdef TCVR_STEAM_FRAME
        if(frame_probe.enabled&&frame_probe.force_render){trigger=pedal=0;coin_frames=0;aim_valid=gun_tracked=false;reset_input_edges();ss22_input_neutral();}
        else
#endif
        sync_input(frame.predictedDisplayTime,head_tracked,(views[0].pose.position.y+views[1].pose.position.y)*.5f);
        parts[0]+=wall_ms()-stage_start;
        uint64_t feedback_time=SDL_GetTicks64();
        float shot_age=recoil_started?(float)(feedback_time-recoil_started):-1;
        float tracer_alpha=qtracer_opacity(&tracer,feedback_time);
        float recoil=qgun_recoil(shot_age),flash=paused?0:qgun_flash(shot_age);
        XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        bool draw_frame=frame.shouldRender&&tracking;
#ifdef TCVR_STEAM_FRAME
        draw_frame=draw_frame||(frame_probe.enabled&&frame_probe.force_render);
#endif
        bool frame_ok=true;
        float v[2][16],p[2][16];
        for(int i=0;i<2;i++){
            view_matrix(v[i],relative_position(views[i].pose.position),relative_rotation(views[i].pose.orientation));
            projection(p[i],views[i].fov.angleLeft,views[i].fov.angleRight,views[i].fov.angleDown,views[i].fov.angleUp);
        }
        hud_camera();
        bool stereo_frame=false;
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
            if(focused&&paused){
                V3 head=mul(add(relative_position(views[0].pose.position),relative_position(views[1].pose.position)),.5f);
                qui_draw(v[i],p[i],head,relative_rotation(views[0].pose.orientation),&options,options_page,qtc2_cabinet_side,frame_profile_active,options_saved);
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
        const XrCompositionLayerBaseHeader *layers[]={(const XrCompositionLayerBaseHeader*)&layer};
        XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=frame.shouldRender&&tracking&&frame_ok?1:0;end.layers=layers;
        stage_start=wall_ms();
        if(!XR(xrEndFrame(session,&end))||!frame_ok)return false;
        parts[5]+=wall_ms()-stage_start;
#ifdef TCVR_STEAM_FRAME
        if(probe_exit_after_frame){frame_probe_finish("frame limit");return false;}
#endif
        if(draw_frame&&focused&&!paused){for(int j=0;j<6;j++)profile_parts[j]+=parts[j];profile_thread+=cpu_ms()-cpu_start;}
        else{memset(profile_parts,0,sizeof profile_parts);profile_thread=0;}
        frame_profile(render_start,frame.predictedDisplayPeriod,draw_frame&&focused&&!paused);
        bool probe_simulation=false;
#ifdef TCVR_STEAM_FRAME
        probe_simulation=frame_probe.enabled&&frame_probe.force_render&&!paused;
#endif
        if(paused||((!focused||!tracking)&&!probe_simulation)){ss22_input_neutral();game_deadline=0;continue;}
        if(qclock_take(&game_deadline,frame.predictedDisplayTime)){
#ifdef TCVR_STEAM_FRAME
            frame_probe_game_tick();
#endif
            ss22_input_update();return true;
        }
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
bool ss22_host_active(void){return active;}
bool ss22_host_restart_requested(void){return false;}
bool ss22_host_open_headless(void){return false;}
bool ss22_host_pointer(float *x,float *y,bool *inside){*x=nx;*y=ny;*inside=aim_valid;return active;}
void ss22_host_shot(const char *p){(void)p;}
void ss22_host_close(void){
    active=false;
#ifdef TCVR_STEAM_FRAME
    if(frame_probe.owns_capture_marker)unlink("capture.request");
#endif
    if(context&&gpu_ready){qui_shutdown();qgun_shutdown();qgl_shutdown();}
    for(int i=0;i<2;i++){if(context&&gpu_ready&&eyes[i].fb)glDeleteFramebuffers(eyes[i].n,eyes[i].fb);if(context&&gpu_ready)glDeleteRenderbuffers(1,&eyes[i].depth);if(eyes[i].chain)xrDestroySwapchain(eyes[i].chain);free(eyes[i].images);free(eyes[i].fb);memset(&eyes[i],0,sizeof eyes[i]);}
    for(int i=0;i<HAND_COUNT;i++){if(aim_spaces[i])xrDestroySpace(aim_spaces[i]);aim_spaces[i]=0;}
    if(local_space)xrDestroySpace(local_space);
    if(session)xrDestroySession(session);if(action_set)xrDestroyActionSet(action_set);if(instance)xrDestroyInstance(instance);
    local_space=0;session=0;action_set=0;instance=0;running=false;
    if(context)SDL_GL_DeleteContext(context);context=NULL;gpu_ready=false;if(window)SDL_DestroyWindow(window);window=NULL;SDL_Quit();
}
#endif
