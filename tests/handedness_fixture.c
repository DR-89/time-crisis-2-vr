static _Bool keyboard_modal_test;
_Bool qkeyboard_modal(void){return keyboard_modal_test;}
/* Exercise the real host input path against a deterministic OpenXR runtime.
 * Render/startup entry points are excluded; no headset is required. */
#include <assert.h>
#define TCVR_INPUT_TEST
#include "../quest/quest_host.c"

static char paths[40][100];
static unsigned path_count,action_count,space_count,binding_count;
static XrActionSuggestedBinding suggested[20];
static XrActionSuggestedBinding frame_suggested[20],touch_suggested[20];
static unsigned frame_binding_count,touch_binding_count;
static XrActionType types[16];
static bool dual[16],buttons[16][3],connected[2]={true,true},tracked[2]={true,true};
static float stick_x,stick_y;
float qvr_world_scale=1.f;
float qvr_units_per_meter=2.f/3.f;
float qvr_view_distance;
int qtc2_cabinet_side=1;
static float distance_x,distance_y;
static bool distance_active=true;
static bool stick_active=true;
static Q4 controller_rotation={0,0,0,1};
static float triggers[2],grips[2];
static XrTime trigger_times[2];
static bool trigger_active[2]={true,true};
static XrPath last_haptic,last_stop;
static float last_haptic_amplitude;
static XrDuration last_haptic_duration;
static unsigned haptic_count;
static XrPath current_profile;
static XrResult sync_result=XR_SUCCESS;
uint16_t tc2_inputs;
#define arcade_bits tc2_inputs
static float reported_rate=72;
static unsigned rate_requests;
static XrResult XRAPI_CALL mock_get_rate(XrSession s,float *rate){(void)s;*rate=reported_rate;return XR_SUCCESS;}
static XrResult XRAPI_CALL mock_request_rate(XrSession s,float rate){(void)s;assert(rate==120);rate_requests++;return XR_SUCCESS;}
uint16_t g_ss22_gun_x,g_ss22_gun_y;
bool g_ss22_gun_off;
static unsigned id(XrAction a){return (unsigned)(uintptr_t)a;}
static int hand_index(XrPath p){
    if(p==XR_NULL_PATH)return 2;
    if(p==hand_paths[LEFT_HAND])return LEFT_HAND;
    assert(p==hand_paths[RIGHT_HAND]);return RIGHT_HAND;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance i,const char *s,XrPath *p){
    (void)i;for(unsigned n=1;n<=path_count;n++)if(!strcmp(paths[n],s)){*p=n;return XR_SUCCESS;}
    assert(path_count+1<40);*p=++path_count;snprintf(paths[*p],100,"%s",s);return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance i,XrResult r,char buffer[XR_MAX_RESULT_STRING_SIZE]){
    (void)i;snprintf(buffer,XR_MAX_RESULT_STRING_SIZE,"Mock result %d",r);return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance i,const XrActionSetCreateInfo *c,XrActionSet *out){
    (void)i;(void)c;*out=(XrActionSet)(uintptr_t)1;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet set,const XrActionCreateInfo *c,XrAction *out){
    (void)set;unsigned n=++action_count;assert(n<16);types[n]=c->actionType;
    dual[n]=c->countSubactionPaths==2;
    if(dual[n]){assert(c->subactionPaths[0]==hand_paths[0]);assert(c->subactionPaths[1]==hand_paths[1]);}
    else assert(c->countSubactionPaths==0);
    *out=(XrAction)(uintptr_t)n;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance i,const XrInteractionProfileSuggestedBinding *s){
    (void)i;
    binding_count=s->countSuggestedBindings;assert(binding_count<=20);
    memcpy(suggested,s->suggestedBindings,binding_count*sizeof suggested[0]);
    if(!strcmp(paths[s->interactionProfile],"/interaction_profiles/oculus/touch_controller")){
        touch_binding_count=binding_count;memcpy(touch_suggested,suggested,binding_count*sizeof suggested[0]);
    }else if(!strcmp(paths[s->interactionProfile],"/interaction_profiles/valve/frame_controller_valve")){
        frame_binding_count=binding_count;memcpy(frame_suggested,suggested,binding_count*sizeof suggested[0]);
    }else assert(!"unexpected interaction profile");
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(XrSession s,XrPath hand,XrInteractionProfileState *out){
    (void)s;assert(hand==hand_paths[RIGHT_HAND]);out->interactionProfile=current_profile;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession s,const XrSessionActionSetsAttachInfo *a){
    (void)s;assert(a->countActionSets==1&&a->actionSets[0]==action_set);return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession s,const XrActionSpaceCreateInfo *c,XrSpace *out){
    (void)s;assert(c->action==aim_action&&dual[id(c->action)]);assert(c->poseInActionSpace.orientation.w==1);
    int hand=hand_index(c->subactionPath);assert(hand<2);*out=(XrSpace)(uintptr_t)(hand+1);space_count++;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession s,const XrActionsSyncInfo *c){
    (void)s;assert(c->countActiveActionSets==1);return sync_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession s,const XrActionStateGetInfo *get,XrActionStateBoolean *out){
    (void)s;unsigned a=id(get->action);int h=hand_index(get->subactionPath);
    assert(types[a]==XR_ACTION_TYPE_BOOLEAN_INPUT);assert(dual[a]?h<2:h==2);
    out->currentState=buttons[a][h];out->isActive=h==2||connected[h];return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession s,const XrActionStateGetInfo *get,XrActionStateFloat *out){
    (void)s;assert((get->action==trigger_action||get->action==grip_action)&&dual[id(get->action)]);
    int h=hand_index(get->subactionPath);assert(h<2);bool fire=get->action==trigger_action;
    out->isActive=connected[h]&&(!fire||trigger_active[h]);out->currentState=fire?triggers[h]:grips[h];
    out->lastChangeTime=fire?trigger_times[h]:0;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession s,const XrActionStateGetInfo *get,XrActionStatePose *out){
    (void)s;assert(get->action==aim_action&&dual[id(get->action)]);int h=hand_index(get->subactionPath);assert(h<2);
    out->isActive=connected[h];return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession s,const XrActionStateGetInfo *get,XrActionStateVector2f *out){
    (void)s;assert((get->action==pitch_action||get->action==distance_action)&&get->subactionPath==XR_NULL_PATH);
    assert(types[id(get->action)]==XR_ACTION_TYPE_VECTOR2F_INPUT);
    if(get->action==distance_action){out->isActive=distance_active&&connected[LEFT_HAND];out->currentState=(XrVector2f){distance_x,distance_y};return XR_SUCCESS;}
    out->isActive=stick_active&&connected[RIGHT_HAND];out->currentState=(XrVector2f){stick_x,stick_y};return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space,XrSpace base,XrTime t,XrSpaceLocation *out){
    (void)base;(void)t;int h=(int)(uintptr_t)space-1;assert(h==0||h==1);
    out->locationFlags=tracked[h]?XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT:0;
    out->pose=(XrPosef){{controller_rotation.x,controller_rotation.y,controller_rotation.z,controller_rotation.w},{h==LEFT_HAND?-.25f:.25f,1.4f,-.5f}};return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession s,const XrHapticActionInfo *info,const XrHapticBaseHeader *v){
    (void)s;assert(info->action==haptic_action&&dual[id(info->action)]);
    assert(hand_index(info->subactionPath)<2);last_haptic=info->subactionPath;haptic_count++;
    if(v&&v->type==XR_TYPE_HAPTIC_VIBRATION){const XrHapticVibration *pulse=(const XrHapticVibration*)v;last_haptic_amplitude=pulse->amplitude;last_haptic_duration=pulse->duration;}
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession s,const XrHapticActionInfo *info){
    (void)s;assert(info->action==haptic_action);last_stop=info->subactionPath;return XR_SUCCESS;
}
void tc2_audio_volume(int v){(void)v;}
void ss22_snd_inputs(uint16_t bits,unsigned w,unsigned p1,unsigned p2){(void)w;(void)p1;(void)p2;arcade_bits=bits;}
V3 qgun_muzzle(V3 p,Q4 q){return add(p,rotate(q,v3(0,0,-.1f)));}
bool qvr_aim(V3 p,V3 d,float *x,float *y,V3 *hit){*x=p.x+.5f;*y=.5f;*hit=add(p,mul(d,2.f));return true;}
#ifdef TCVR_STEAM_FRAME
void glPixelStorei(GLenum name,GLint value){(void)name;(void)value;}
void glReadPixels(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,void *pixels){
    (void)x;(void)y;(void)format;(void)type;memset(pixels,0,(size_t)width*height*4);
}
#endif
static void tick_at(XrTime time){sync_input(time,true,1.7f);ss22_input_update();}
static void tick(void){tick_at(100);}
static void click(XrAction a,int h){buttons[id(a)][h]=true;tick();buttons[id(a)][h]=false;tick();}
static void bound(XrAction a,const char *name){
    for(unsigned i=0;i<binding_count;i++)if(suggested[i].action==a&&!strcmp(paths[suggested[i].binding],name))return;
    fprintf(stderr,"Missing binding: %s\n",name);abort();
}
#ifdef TCVR_STEAM_FRAME
static bool has_binding(const XrActionSuggestedBinding *bindings,unsigned count,XrAction action,const char *name){
    for(unsigned i=0;i<count;i++)if(bindings[i].action==action&&!strcmp(paths[bindings[i].binding],name))return true;
    return false;
}
static void require_binding(const XrActionSuggestedBinding *bindings,unsigned count,XrAction action,const char *name){
    if(!has_binding(bindings,count,action,name)){fprintf(stderr,"Missing profile binding: %s\n",name);abort();}
}
int main(void){
    get_rate=mock_get_rate;request_rate=mock_request_rate;preferred_rate=120;focused=true;
    rate_attempts=0;rate_requests=0;display_rate_request();assert(rate_requests==0&&rate_attempts==0);
    display_rate_check(10000000000LL);assert(rate_requests==0&&rate_attempts==0);
    get_rate=NULL;request_rate=NULL;
    options_path="frame-profile-test.cfg";qoptions_load(options_path,&options);
    assert(options.laser_enabled&&!options.left_handed);instance=(XrInstance)(uintptr_t)1;session=(XrSession)(uintptr_t)1;
    running=focused=origin_set=true;frame_profile_supported=true;
    assert(actions_init());assert(space_count==2&&frame_binding_count==17&&touch_binding_count==17);
    require_binding(frame_suggested,frame_binding_count,distance_action,"/user/hand/left/input/thumbstick");
    const char *hands[]={"/user/hand/left","/user/hand/right"};
    for(int h=0;h<2;h++){
        char name[100];
        snprintf(name,sizeof name,"%s/input/aim/pose",hands[h]);require_binding(frame_suggested,frame_binding_count,aim_action,name);
        snprintf(name,sizeof name,"%s/input/trigger/value",hands[h]);require_binding(frame_suggested,frame_binding_count,trigger_action,name);
        snprintf(name,sizeof name,"%s/input/squeeze/value",hands[h]);require_binding(frame_suggested,frame_binding_count,grip_action,name);
        snprintf(name,sizeof name,"%s/output/haptic",hands[h]);require_binding(frame_suggested,frame_binding_count,haptic_action,name);
    }
    require_binding(frame_suggested,frame_binding_count,lower_action,"/user/hand/right/input/a/click");
    require_binding(frame_suggested,frame_binding_count,upper_action,"/user/hand/right/input/b/click");
    require_binding(frame_suggested,frame_binding_count,cover_mode_action,"/user/hand/right/input/x/click");
    require_binding(frame_suggested,frame_binding_count,pause_action,"/user/hand/right/input/menu/click");
    require_binding(frame_suggested,frame_binding_count,view_action,"/user/hand/left/input/view/click");
    require_binding(frame_suggested,frame_binding_count,pitch_action,"/user/hand/right/input/thumbstick");
    require_binding(frame_suggested,frame_binding_count,hand_action,"/user/hand/right/input/thumbstick/click");
    require_binding(frame_suggested,frame_binding_count,pitch_reset_action,"/user/hand/left/input/thumbstick/click");
    require_binding(touch_suggested,touch_binding_count,lower_action,"/user/hand/left/input/x/click");
    require_binding(touch_suggested,touch_binding_count,lower_action,"/user/hand/right/input/a/click");
    require_binding(touch_suggested,touch_binding_count,pause_action,"/user/hand/left/input/menu/click");

    /* The original Touch profile remains active and keeps its hand/layout roles. */
    current_profile=touch_profile_path;update_frame_profile();assert(!frame_profile_active);
    tick();assert(gun_tracked&&gun_position.x==.25f);
    click(lower_action,RIGHT_HAND);assert(coin_frames>0);coin_frames=0;
    click(upper_action,RIGHT_HAND);assert(!options.laser_enabled);
    recenter_requested=false;click(lower_action,LEFT_HAND);assert(recenter_requested);

    /* Frame profile assigns trigger/grip on both hands and options to Frame controls. */
    current_profile=frame_profile_path;update_frame_profile();assert(frame_profile_active);
    options.laser_enabled=true;paused=false;coin_frames=0;recenter_requested=false;
    buttons[id(lower_action)][RIGHT_HAND]=false;buttons[id(upper_action)][RIGHT_HAND]=false;
    tick();click(lower_action,RIGHT_HAND);assert(coin_frames>0);coin_frames=0;
    click(upper_action,RIGHT_HAND);assert(!options.laser_enabled);
    click(lower_action,LEFT_HAND);click(upper_action,LEFT_HAND);assert(coin_frames==0&&!options.laser_enabled);

    /* Either trigger selects the firing hand; grips expose cover. */
    triggers[LEFT_HAND]=.9f;tick();assert(weapon_hand()==LEFT_HAND&&(arcade_bits&0x10));
    triggers[LEFT_HAND]=0;tick();grips[LEFT_HAND]=.8f;tick();assert(pedal==.8f);
    grips[LEFT_HAND]=0;tick();triggers[RIGHT_HAND]=.9f;tick();assert(weapon_hand()==RIGHT_HAND&&(arcade_bits&0x10));
    triggers[RIGHT_HAND]=0;tick();

    /* Right Menu opens options; right X switches cover mode only while paused. */
    click(pause_action,2);assert(paused);
    bool old_cover=options.physical_crouch;click(cover_mode_action,2);assert(options.physical_crouch!=old_cover);
    click(cover_mode_action,2);assert(options.physical_crouch==old_cover);

    /* A one-second View hold recentres and emits the standard two-hand pulse. */
    buttons[id(view_action)][LEFT_HAND]=false;tick_at(1000);
    unsigned before=haptic_count;buttons[id(view_action)][LEFT_HAND]=true;
    tick_at(2000);tick_at(1000002000LL);
    assert(recenter_requested&&haptic_count==before+2&&fabsf(last_haptic_amplitude-.6f)<.001f&&last_haptic_duration==80000000LL);
    buttons[id(view_action)][LEFT_HAND]=false;tick_at(1000003000LL);assert(paused);

    /* Three seconds adds upright-height calibration and a second standard pulse. */
    tick_at(1100000000LL);cover_calibration_pending=false;cover.calibrated=false;before=haptic_count;buttons[id(view_action)][LEFT_HAND]=true;
    tick_at(1100000100LL);tick_at(4100000100LL);
    assert(recenter_calibration_requested&&cover.calibrated&&fabsf(cover.upright_y-1.7f)<.001f&&haptic_count==before+4);
    buttons[id(view_action)][LEFT_HAND]=false;tick_at(4100000200LL);assert(paused);

    /* A fresh short release resumes; releasing a held gesture never short-acts. */
    tick_at(4200000000LL);buttons[id(view_action)][LEFT_HAND]=true;tick_at(4200000100LL);
    buttons[id(view_action)][LEFT_HAND]=false;tick_at(4200000200LL);assert(!paused);
    click(pause_action,2);assert(paused);tick_at(4300000000LL);
    buttons[id(view_action)][LEFT_HAND]=true;tick_at(4300000100LL);tick_at(5300000100LL);
    buttons[id(view_action)][LEFT_HAND]=false;tick_at(5300000200LL);assert(paused);

    /* The one-shot probe arms capture after game ticks, captures both eyes, then consumes the marker. */
    paused=false;FILE *config=fopen("frame-probe.cfg","w");assert(config);
    fputs("force_render=1\ncapture_once=1\ncapture_after_ticks=2\n",config);fclose(config);
    frame_probe=(FrameProbe){0};frame_probe_load();assert(frame_probe.enabled&&frame_probe.capture_once&&frame_probe.capture_after_ticks==2);
    frame_probe_game_tick();assert(frame_probe.game_ticks==1&&access("capture.request",F_OK)!=0);
    frame_probe_game_tick();assert(frame_probe.game_ticks==2&&access("capture.request",F_OK)==0);
    assert(capture_eye(0,2,2)&&access("capture.request",F_OK)==0);
    assert(capture_eye(1,2,2)&&access("capture.request",F_OK)!=0&&!frame_probe.capture_once);
    assert(access("eye-0.ppm",F_OK)==0&&access("eye-1.ppm",F_OK)==0);
    unlink("eye-0.ppm");unlink("eye-1.ppm");frame_probe.enabled=false;
    paused=true;stick_y=stick_x=distance_x=distance_y=0;options_page=1;tick();distance_y=1;tick();
    assert(options_page==2&&qlobby_visible());distance_y=0;tick();
    qlobby_input(QL_DOWN);qlobby_input(QL_DOWN);qlobby_input(QL_ACCEPT);assert(qlobby_editing());
    qlobby_replace_text("192.168.1.42");
    keyboard_modal_test=true;buttons[id(lower_action)][RIGHT_HAND]=true;triggers[RIGHT_HAND]=1;tick();
    assert(qlobby_editing()&&tc2_link_session()->state==TC2_OFF);
    keyboard_modal_test=false;tick();tick();assert(qlobby_editing()&&tc2_link_session()->state==TC2_OFF);
    buttons[id(lower_action)][RIGHT_HAND]=false;triggers[RIGHT_HAND]=0;tick();
    click(upper_action,RIGHT_HAND);assert(qlobby_visible()&&!qlobby_editing());
    click(upper_action,RIGHT_HAND);qlobby_tick(0);assert(!qlobby_visible());
    puts("PASS: production Frame OpenXR action path; both profile bindings and Touch fallback, trigger/grip behavior, Menu/options mappings, View tap/hold timing, recenter/calibration and haptics; bounded tick-delayed paired capture; runtime-owned refresh policy");
    return 0;
}
#else
int main(void){
    /* A successful request is not proof of application. Retry while focused,
     * stop on measured success, and cap retries if the runtime refuses to switch. */
    get_rate=mock_get_rate;request_rate=mock_request_rate;preferred_rate=120;focused=true;
    display_rate_check(1);assert(!rate_requests);display_rate_check(2000000001LL);assert(rate_requests==1);
    reported_rate=120;display_rate_check(4000000001LL);assert(rate_attempts==4);
    display_rate_check(6000000001LL);assert(rate_requests==1);
    reported_rate=72;rate_attempts=0;rate_check_time=0;rate_requests=0;
    for(int i=0;i<8;i++)display_rate_check(1+(int64_t)i*2000000000LL);
    assert(rate_requests==3&&rate_attempts==4);
    focused=false;rate_attempts=0;rate_check_time=0;display_rate_check(30000000000LL);assert(!rate_check_time);
    get_rate=NULL;request_rate=NULL;
    options_path="handedness-test.cfg";qoptions_load(options_path,&options);
    assert(!options.left_handed);instance=(XrInstance)(uintptr_t)1;session=(XrSession)(uintptr_t)1;
    running=focused=origin_set=true;assert(actions_init());assert(space_count==2&&binding_count==17);
    for(int h=0;h<2;h++){
        char name[100];const char *prefix=h?"/user/hand/right":"/user/hand/left";
        snprintf(name,sizeof name,"%s/input/aim/pose",prefix);bound(aim_action,name);
        snprintf(name,sizeof name,"%s/input/trigger/value",prefix);bound(trigger_action,name);
        snprintf(name,sizeof name,"%s/input/squeeze/value",prefix);bound(grip_action,name);
        snprintf(name,sizeof name,"%s/input/%s/click",prefix,h?"a":"x");bound(lower_action,name);
        snprintf(name,sizeof name,"%s/input/%s/click",prefix,h?"b":"y");bound(upper_action,name);
        snprintf(name,sizeof name,"%s/output/haptic",prefix);bound(haptic_action,name);
    }
    bound(pause_action,"/user/hand/left/input/menu/click");bound(hand_action,"/user/hand/right/input/thumbstick/click");
    tick();assert(gun_position.x==.25f&&gun_origin.x==.25f&&aim_valid);
    /* Either grip exposes; only releasing both hides. Trigger no longer opens cover. */
    grips[LEFT_HAND]=.9f;tick();assert((arcade_bits&0x30)==0x20);
    grips[RIGHT_HAND]=.8f;grips[LEFT_HAND]=0;tick();assert((arcade_bits&0x30)==0x20);
    grips[LEFT_HAND]=.9f;grips[RIGHT_HAND]=0;tick();assert((arcade_bits&0x30)==0x20);
    grips[RIGHT_HAND]=.8f;grips[LEFT_HAND]=0;tick();assert(pedal==.8f);
    grips[RIGHT_HAND]=0;triggers[RIGHT_HAND]=.9f;tick();assert((arcade_bits&0x30)==0x10);
    ss22_input_rumble(0,48000,45);assert(last_haptic==hand_paths[RIGHT_HAND]);
    /* Opposite trigger pressed while old one remains held: new arcade fire edge. */
    triggers[LEFT_HAND]=.9f;tick();assert(weapon_hand()==LEFT_HAND&&!(arcade_bits&0x10));
    assert(last_stop==hand_paths[RIGHT_HAND]&&gun_position.x==-.25f&&aim_valid);
    tick();assert(arcade_bits&0x10);ss22_input_rumble(0,48000,45);assert(last_haptic==hand_paths[LEFT_HAND]);
    /* Scripted machine-gun sections need a sustained arcade trigger level. */
    for(int n=0;n<120;n++){tick();assert(weapon_hand()==LEFT_HAND&&(arcade_bits&0x10));}
    triggers[LEFT_HAND]=0;tick();assert(!(arcade_bits&0x10)&&weapon_hand()==LEFT_HAND);
    assert(!options.left_handed); /* Handoffs do not change the saved button layout. */
    click(lower_action,RIGHT_HAND);assert(coin_frames>0);coin_frames=0;
    click(upper_action,RIGHT_HAND);assert(!options.laser_enabled&&!paused);
    QOptions saved;qoptions_load(options_path,&saved);assert(!saved.left_handed&&!saved.laser_enabled);
    triggers[RIGHT_HAND]=0;tick();triggers[RIGHT_HAND]=.9f;tick();assert(weapon_hand()==RIGHT_HAND&&(arcade_bits&0x10));
    /* A tap between simulation ticks must still produce one press. */
    triggers[0]=triggers[1]=0;tick();triggers[LEFT_HAND]=.9f;sync_input(100,true,1.7f);
    triggers[LEFT_HAND]=0;sync_input(101,true,1.7f);ss22_input_update();assert(arcade_bits&0x10);
    tick();assert(!(arcade_bits&0x10));
    /* Simultaneous samples use event time; exact ties keep the active hand. */
    triggers[0]=triggers[1]=.9f;trigger_times[0]=10;trigger_times[1]=20;tick();assert(weapon_hand()==RIGHT_HAND);
    triggers[0]=triggers[1]=0;tick();triggers[0]=triggers[1]=.9f;trigger_times[0]=trigger_times[1]=30;
    tick();assert(weapon_hand()==RIGHT_HAND);
    triggers[0]=triggers[1]=0;tick();triggers[0]=triggers[1]=.9f;trigger_times[0]=50;trigger_times[1]=40;
    tick();assert(weapon_hand()==LEFT_HAND);
    /* Keep cover operational without an aim pose; recovery requires trigger release. */
    grips[RIGHT_HAND]=.8f;tracked[LEFT_HAND]=false;tick();assert(!gun_tracked&&!aim_valid&&trigger==0&&pedal==.8f);
    tracked[LEFT_HAND]=true;tick();assert(trigger==0);triggers[LEFT_HAND]=0;tick();triggers[LEFT_HAND]=.9f;tick();assert(trigger==1);
    connected[RIGHT_HAND]=false;tick();assert(pedal==0&&gun_tracked);connected[RIGHT_HAND]=true;
    /* Action loss also cancels an unconsumed shot, even if pose remains valid. */
    triggers[LEFT_HAND]=0;tick();triggers[LEFT_HAND]=.9f;sync_input(100,true,1.7f);assert(shot_pending);
    trigger_active[LEFT_HAND]=false;tick();assert(!(arcade_bits&0x10));
    trigger_active[LEFT_HAND]=true;tick();assert(!(arcade_bits&0x10));
    /* Focus loss/pause/sync errors cannot replay a held trigger or button. */
    focused=false;tick();assert(arcade_bits==0);buttons[id(upper_action)][RIGHT_HAND]=true;
    focused=true;tick();assert(!options.laser_enabled&&trigger==0);
    buttons[id(upper_action)][RIGHT_HAND]=false;tick();click(upper_action,RIGHT_HAND);assert(options.tracer_enabled&&!options.laser_enabled);click(upper_action,RIGHT_HAND);assert(options.laser_enabled);
    sync_result=XR_ERROR_RUNTIME_FAILURE;tick();assert(trigger==0&&pedal==0);
    buttons[id(upper_action)][RIGHT_HAND]=true;sync_result=XR_SUCCESS;tick();assert(options.laser_enabled&&trigger==0);
    buttons[id(upper_action)][RIGHT_HAND]=false;tick();
    click(hand_action,2);assert(!options.left_handed&&!paused); /* Menu-only preference. */
    click(pause_action,2);assert(paused&&arcade_bits==0);recoil_started=500;click(hand_action,2);
    assert(options.left_handed&&paused&&options_saved&&!recoil_started&&weapon_hand()==LEFT_HAND);
    qoptions_load(options_path,&saved);assert(saved.left_handed&&saved.laser_enabled);
    click(pause_action,2);assert(!paused&&trigger==0);
    triggers[0]=triggers[1]=0;tick();triggers[RIGHT_HAND]=.9f;tick();assert(weapon_hand()==RIGHT_HAND&&(arcade_bits&0x10));
    click(lower_action,LEFT_HAND);assert(coin_frames>0);coin_frames=0;recenter_requested=false;
    click(lower_action,RIGHT_HAND);assert(recenter_requested&&coin_frames==0);
    click(upper_action,LEFT_HAND);assert(!options.laser_enabled&&!paused);
    click(upper_action,RIGHT_HAND);assert(!options.physical_crouch&&!paused);
    click(pause_action,2);click(upper_action,RIGHT_HAND);assert(options.physical_crouch);
    click(pause_action,2);grips[0]=grips[1]=0;tick();assert(pedal==1);
    grips[0]=grips[1]=1;sync_input(100,true,1.45f);assert(pedal==0);tick();assert(pedal==1);
    sync_input(100,false,1.7f);assert(pedal==0);tick();
    /* Held face buttons must not acquire new roles on manual layout change. */
    click(pause_action,2);buttons[id(lower_action)][LEFT_HAND]=true;buttons[id(upper_action)][LEFT_HAND]=true;tick();
    bool laser=options.laser_enabled;bool physical=options.physical_crouch;recenter_requested=false;
    buttons[id(hand_action)][2]=true;tick();assert(!options.left_handed&&paused);
    for(int n=0;n<6;n++)tick();
    assert(!recenter_requested&&coin_frames==0&&options.laser_enabled==laser&&options.physical_crouch==physical);
    memset(buttons,0,sizeof buttons);triggers[0]=triggers[1]=0;tick();
    click(lower_action,RIGHT_HAND);assert(coin_frames==48);coin_frames=0;
    click(lower_action,LEFT_HAND);assert(recenter_requested);
    click(upper_action,RIGHT_HAND);assert(options.laser_enabled!=laser);
    click(upper_action,LEFT_HAND);assert(options.physical_crouch!=physical);
    /* Angle only changes in options, after a neutral stick, at a bounded rate. */
    bound(pitch_action,"/user/hand/right/input/thumbstick");bound(pitch_reset_action,"/user/hand/left/input/thumbstick/click");
    assert(paused&&options.gun_pitch==0);stick_y=-1;sync_input(1000,true,1.7f);assert(options.gun_pitch==-1);
    sync_input(399999999,true,1.7f);assert(options.gun_pitch==-1);
    sync_input(400001000,true,1.7f);assert(options.gun_pitch==-2);
    sync_input(500001000,true,1.7f);assert(options.gun_pitch==-3);
    stick_y=0;tick();stick_y=1;tick();assert(options.gun_pitch==-2);
    stick_active=false;tick();stick_active=true;tick();assert(options.gun_pitch==-2);
    stick_y=0;tick();stick_y=1;tick();assert(options.gun_pitch==-1);
    focused=false;tick();focused=true;tick();assert(options.gun_pitch==-1);
    stick_y=NAN;tick();assert(options.gun_pitch==-1);
    click(pitch_reset_action,2);assert(options.gun_pitch==0);
    stick_y=0;tick();set_gun_pitch(100);assert(options.gun_pitch==60);set_gun_pitch(-100);assert(options.gun_pitch==-60);
    set_gun_pitch(-45);qoptions_load(options_path,&saved);assert(saved.gun_pitch==-45);
    click(pause_action,2);assert(!paused);stick_y=1;tick();click(pitch_reset_action,2);assert(options.gun_pitch==-45);
    click(pause_action,2);assert(paused);tick();assert(options.gun_pitch==-45); /* Held across menu open. */
    /* A controller pitched 45 degrees up becomes level at -45 in BOTH hands.
     * Muzzle and target use precisely the same corrected pose as the model. */
    controller_rotation=(Q4){sinf(3.14159265f/8),0,0,cosf(3.14159265f/8)};
    for(int h=0;h<2;h++){
        select_weapon_hand(h);tick();V3 direction=rotate(gun_rotation,v3(0,0,-1));
        assert(fabsf(direction.y)<1e-6f&&fabsf(direction.z+1)<1e-6f);
        assert(fabsf(gun_origin.y-gun_position.y)<1e-6f&&fabsf(gun_origin.z-gun_position.z+.1f)<1e-6f);
        assert(fabsf(gun_hit.y-gun_origin.y)<1e-6f&&fabsf(gun_hit.z-gun_origin.z+2)<1e-6f);
    }
    /* Apply pitch in controller space after yaw, not around the world's X axis. */
    controller_rotation=product((Q4){0,sinf(.6f),0,cosf(.6f)},controller_rotation);tick();
    V3 forward=rotate(gun_rotation,v3(0,0,-1));assert(fabsf(forward.y)<1e-6f&&fabsf(forward.x+sinf(1.2f))<1e-6f);
    controller_rotation=(Q4){0,0,0,1};click(pitch_reset_action,2);tick();assert(options.gun_pitch==0&&gun_rotation.w==1);
    /* TC2's US cabinet needs four coins per game. A single credit action
     * emits four separate edges, without changing trigger/grip semantics. */
    paused=false;trigger=pedal=0;shot_pending=shot_needs_release=false;coin_frames=48;
    int pulses=0,previous=0;
    for(int n=0;n<60;n++){ss22_input_update();int on=(arcade_bits&1)!=0;pulses+=on&&!previous;previous=on;}
    assert(pulses==4&&coin_frames==0);
    nx=0;ny=0;ss22_input_update();assert(g_ss22_gun_x==91&&g_ss22_gun_y==38);
    nx=1;ny=1;ss22_input_update();assert(g_ss22_gun_x==824&&g_ss22_gun_y==285);
    /* Sparse recordings retain their input until the next recorded change. */
    replay_input=true;arcade_bits=0x30;g_ss22_gun_x=456;g_ss22_gun_y=123;g_ss22_gun_off=false;
    for(int n=0;n<120;n++){ss22_input_update();ss22_input_neutral();assert(arcade_bits==0x30&&g_ss22_gun_x==456&&g_ss22_gun_y==123&&!g_ss22_gun_off);}
    replay_input=false;ss22_input_neutral();assert(arcade_bits==0);
    /* World size: neutral arm, repeat delay, dead zone, diagonal isolation,
     * focus/action loss, persistence and bounds. Physical poses stay in metres. */
    paused=true;stick_x=stick_y=0;tick();set_world_scale(100);
    V3 physical_gun=gun_position,physical_muzzle=gun_origin;
    stick_x=1;sync_input(1000,true,1.7f);assert(options.world_scale==105);
    sync_input(399999999,true,1.7f);assert(options.world_scale==105);
    sync_input(400001000,true,1.7f);assert(options.world_scale==110);
    sync_input(500001000,true,1.7f);assert(options.world_scale==115);
    qoptions_load(options_path,&saved);assert(saved.world_scale==115&&options_saved);
    assert(fabsf(qvr_world_scale-1.15f)<1e-6f);
    assert(gun_position.x==physical_gun.x&&gun_position.y==physical_gun.y&&gun_position.z==physical_gun.z);
    assert(gun_origin.x==physical_muzzle.x&&gun_origin.y==physical_muzzle.y&&gun_origin.z==physical_muzzle.z);
    stick_active=false;tick();stick_active=true;tick();assert(options.world_scale==115);
    stick_x=0;tick();stick_x=-1;tick();assert(options.world_scale==110);
    focused=false;tick();focused=true;tick();assert(options.world_scale==110);
    stick_x=NAN;tick();stick_x=-1;tick();assert(options.world_scale==110);
    stick_x=0;tick();stick_x=.5f;tick();assert(options.world_scale==110);
    stick_x=1;stick_y=.8f;tick();assert(options.world_scale==115&&options.gun_pitch==0);
    stick_x=stick_y=0;tick();stick_x=.8f;stick_y=1;tick();assert(options.world_scale==115&&options.gun_pitch==1);
    stick_x=stick_y=0;tick();click(pause_action,2);assert(!paused);
    stick_x=1;tick();assert(options.world_scale==115);
    click(pause_action,2);assert(paused);tick();assert(options.world_scale==115);
    stick_x=0;tick();stick_x=1;tick();assert(options.world_scale==120);
    set_world_scale(1000);assert(options.world_scale==400&&qvr_world_scale==4.f);
    set_world_scale(-1);assert(options.world_scale==25&&qvr_world_scale==.25f);
    qoptions_load(options_path,&saved);assert(saved.world_scale==25&&saved.gun_pitch==1);
    bound(distance_action,"/user/hand/left/input/thumbstick");
    stick_x=stick_y=0;distance_x=0;tick();set_view_distance(40);
    physical_gun=gun_position;physical_muzzle=gun_origin;
    distance_x=1;sync_input(1000,true,1.7f);assert(options.view_distance==45);
    sync_input(399999999,true,1.7f);assert(options.view_distance==45);
    sync_input(400001000,true,1.7f);assert(options.view_distance==50);
    sync_input(500001000,true,1.7f);assert(options.view_distance==55);
    qoptions_load(options_path,&saved);assert(saved.view_distance==55&&fabsf(qvr_view_distance-.55f)<1e-6f);
    assert(gun_position.z==physical_gun.z&&gun_origin.z==physical_muzzle.z);
    distance_active=false;tick();distance_active=true;tick();assert(options.view_distance==55);
    distance_x=0;tick();distance_x=-1;tick();assert(options.view_distance==50);
    focused=false;tick();focused=true;tick();assert(options.view_distance==50);
    distance_x=NAN;tick();distance_x=-1;tick();assert(options.view_distance==50);
    distance_x=0;tick();distance_x=.5f;tick();assert(options.view_distance==50);
    distance_x=.8f;distance_y=1;tick();assert(options.view_distance==50);
    distance_x=distance_y=0;tick();click(pause_action,2);assert(!paused);
    distance_x=1;tick();assert(options.view_distance==50);
    click(pause_action,2);assert(paused);tick();assert(options.view_distance==50);
    distance_x=0;tick();distance_x=1;tick();assert(options.view_distance==55);
    set_view_distance(1000);assert(options.view_distance==100&&qvr_view_distance==1);
    set_view_distance(-1);assert(options.view_distance==0&&qvr_view_distance==0);
    /* Tracer is triggered by accepted shots, frozen at that ray, and expires
     * independently of a held trigger or 72/90/120-Hz presentation. */
    paused=false;options.laser_enabled=options.tracer_enabled=false;
    cycle_aim_guide();assert(options.tracer_enabled&&!options.laser_enabled);
    gun_tracked=aim_valid=focused=true;gun_origin=v3(.1f,.2f,-.3f);gun_hit=v3(.5f,.6f,-4);
    shot_feedback(1000);assert(qtracer_opacity(&tracer,1000)==1);
    gun_hit=v3(0,0,-8);assert(tracer.end.x==.5f&&tracer.end.z==-4);
    for(int rate=72;rate<=120;rate+=24){
        for(int ms=0;ms<180;ms+=1000/rate){float a=qtracer_opacity(&tracer,1000+ms);assert(a>=0&&a<=1);if(ms>=65)assert(a==0);}
    }
    assert(qtracer_opacity(&tracer,999)==0&&qtracer_opacity(&tracer,1065)==0);
    paused=true;tracer.active=false;shot_feedback(2000);assert(!tracer.active);paused=false;
    cycle_aim_guide();assert(options.laser_enabled&&!options.tracer_enabled);shot_feedback(2000);assert(!tracer.active);
    cycle_aim_guide();assert(!options.laser_enabled&&!options.tracer_enabled);
    /* Gun-fit page, per-axis offsets and saved next-boot cabinet side. */
    paused=true;options_page=0;stick_x=stick_y=distance_x=distance_y=0;tick();
    distance_y=1;tick();assert(options_page==1);tick();assert(options_page==1);
    distance_y=0;tick();stick_y=1;sync_input(1000,true,1.7f);assert(options.gun_offset_y_mm==5);
    stick_y=0;tick();stick_x=1;sync_input(1000,true,1.7f);assert(options.gun_offset_z_mm==5);
    stick_x=0;distance_x=1;tick();assert(options.player_side==2&&qtc2_cabinet_side==1);
    qoptions_load(options_path,&saved);assert(saved.player_side==2&&saved.gun_offset_y_mm==5&&saved.gun_offset_z_mm==5);
    set_gun_offset(true,999);set_gun_offset(false,-999);assert(options.gun_offset_y_mm==200&&options.gun_offset_z_mm==-200);
    set_gun_offset(true,20);set_gun_offset(false,40);controller_rotation=(Q4){0,0,0,1};tick();
    V3 fitted=gun_position,muzzle=gun_origin;set_gun_offset(true,0);set_gun_offset(false,0);tick();
    assert(fabsf(fitted.y-gun_position.y-.02f)<1e-6f&&fabsf(fitted.z-gun_position.z-.04f)<1e-6f&&fitted.x==gun_position.x);
    assert(fabsf(muzzle.y-gun_origin.y-.02f)<1e-6f&&fabsf(muzzle.z-gun_origin.z-.04f)<1e-6f);
    /* Offsets follow the hand, not world axes, and ignore world-size settings. */
    controller_rotation=(Q4){0,sinf(.6f),0,cosf(.6f)};tick();V3 base=gun_position;
    set_gun_offset(false,40);tick();V3 delta=rotate(controller_rotation,v3(0,0,.04f));
    assert(fabsf(gun_position.x-base.x-delta.x)<1e-6f&&fabsf(gun_position.z-base.z-delta.z)<1e-6f);
    set_world_scale(400);tick();assert(fabsf(gun_position.x-base.x-delta.x)<1e-6f);
    click(pitch_reset_action,2);assert(options.gun_offset_y_mm==0&&options.gun_offset_z_mm==0);
    for(int side=1;side<=2;side++)for(int input=0;input<256;input++){
        uint8_t result=qtc2_cabinet_switch(input,side);assert((result&~4)==(input&~4));assert(!!(result&4)==(side==1));
    }
    /* Route selection is bounded, big-endian, and never changes default P1.
     * Out-of-range native state must not become a seemingly valid route. */
    static uint8_t route_ram[0x2f3fd9];route_ram[0x2f3fd8]=0xa5;
    assert(!qtc2_route_select(NULL,sizeof route_ram,2));
    for(int mode=0;mode<4;mode++)for(int route=0;route<4;route++)for(int side=1;side<=2;side++){
        route_ram[0x2f3fd3]=mode;route_ram[0x2f3fd7]=route;
        assert(!qtc2_route_select(route_ram,0x2f3fd7,side));
        bool valid=side==2&&mode>=1&&mode<=2&&route<=1;
        assert(qtc2_route_select(route_ram,sizeof route_ram,side)==valid);
        assert(route_ram[0x2f3fd7]==(valid?1:route));
        assert(route_ram[0x2f3fd3]==mode&&route_ram[0x2f3fd8]==0xa5);
    }
    for(int i=0;i<3;i++){
        memset(route_ram,0,sizeof route_ram);route_ram[0x2f3fd3]=1;route_ram[0x2f3fd4+i]=1;
        assert(!qtc2_route_select(route_ram,sizeof route_ram,2));
        memset(route_ram,0,sizeof route_ram);route_ram[0x2f3fd3]=1;route_ram[0x2f3fd0+i]=1;
        assert(!qtc2_route_select(route_ram,sizeof route_ram,2));
    }
    /* LAN play is the third options page; leaving the lobby returns to page 0. */
    paused=true;stick_x=stick_y=distance_x=distance_y=0;options_page=1;tick();
    distance_y=1;tick();assert(options_page==2&&qlobby_visible());distance_y=0;tick();
    qlobby_input(QL_DOWN);qlobby_input(QL_DOWN);qlobby_input(QL_ACCEPT);assert(qlobby_editing());
    qlobby_replace_text("192.168.1.42");
    keyboard_modal_test=true;buttons[id(lower_action)][RIGHT_HAND]=true;triggers[RIGHT_HAND]=1;tick();
    assert(qlobby_editing()&&tc2_link_session()->state==TC2_OFF);
    keyboard_modal_test=false;tick();tick();assert(qlobby_editing()&&tc2_link_session()->state==TC2_OFF);
    buttons[id(lower_action)][RIGHT_HAND]=false;triggers[RIGHT_HAND]=0;tick();
    click(upper_action,RIGHT_HAND);assert(qlobby_visible()&&!qlobby_editing());
    click(upper_action,RIGHT_HAND);qlobby_tick(0);assert(!qlobby_visible());
    tick();assert(options_page==0);
    stick_x=1;tick();assert(!qlobby_visible());stick_x=0;tick();
    puts("PASS: world-scale controls/persistence/physical poses; LAN page and lobby input; real host controls, gun pitch persistence/bounds/repeat/reset, model/muzzle/aim alignment in both hands, local-space correction, focus/action loss and existing input regressions");
    return 0;
}
#endif
