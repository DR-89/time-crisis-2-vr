/* Exercise the real host input path against a deterministic OpenXR runtime.
 * Render/startup entry points are excluded; no headset is required. */
#include <assert.h>
#define TCVR_INPUT_TEST
#include "../quest/quest_host.c"

static char paths[40][100];
static unsigned path_count,action_count,space_count,binding_count;
static XrActionSuggestedBinding suggested[20];
static XrActionType types[16];
static bool dual[16],buttons[16][3],connected[2]={true,true},tracked[2]={true,true};
static float stick_y;
static bool stick_active=true;
static Q4 controller_rotation={0,0,0,1};
static float triggers[2],grips[2];
static XrTime trigger_times[2];
static bool trigger_active[2]={true,true};
static XrPath last_haptic,last_stop;
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
    (void)i;assert(!strcmp(paths[s->interactionProfile],"/interaction_profiles/oculus/touch_controller"));
    binding_count=s->countSuggestedBindings;assert(binding_count<=20);
    memcpy(suggested,s->suggestedBindings,binding_count*sizeof suggested[0]);return XR_SUCCESS;
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
    (void)s;assert(get->action==pitch_action&&get->subactionPath==XR_NULL_PATH);
    assert(types[id(get->action)]==XR_ACTION_TYPE_VECTOR2F_INPUT);
    out->isActive=stick_active&&connected[RIGHT_HAND];out->currentState=(XrVector2f){0,stick_y};return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space,XrSpace base,XrTime t,XrSpaceLocation *out){
    (void)base;(void)t;int h=(int)(uintptr_t)space-1;assert(h==0||h==1);
    out->locationFlags=tracked[h]?XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT:0;
    out->pose=(XrPosef){{controller_rotation.x,controller_rotation.y,controller_rotation.z,controller_rotation.w},{h==LEFT_HAND?-.25f:.25f,1.4f,-.5f}};return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession s,const XrHapticActionInfo *info,const XrHapticBaseHeader *v){
    (void)s;(void)v;assert(info->action==haptic_action&&dual[id(info->action)]);
    assert(hand_index(info->subactionPath)<2);last_haptic=info->subactionPath;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession s,const XrHapticActionInfo *info){
    (void)s;assert(info->action==haptic_action);last_stop=info->subactionPath;return XR_SUCCESS;
}
void tc2_audio_volume(int v){(void)v;}
void ss22_snd_inputs(uint16_t bits,unsigned w,unsigned p1,unsigned p2){(void)w;(void)p1;(void)p2;arcade_bits=bits;}
V3 qgun_muzzle(V3 p,Q4 q){return add(p,rotate(q,v3(0,0,-.1f)));}
bool qvr_aim(V3 p,V3 d,float *x,float *y,V3 *hit){*x=p.x+.5f;*y=.5f;*hit=add(p,mul(d,2.f));return true;}
static void tick(void){sync_input(100,true,1.7f);ss22_input_update();}
static void click(XrAction a,int h){buttons[id(a)][h]=true;tick();buttons[id(a)][h]=false;tick();}
static void bound(XrAction a,const char *name){
    for(unsigned i=0;i<binding_count;i++)if(suggested[i].action==a&&!strcmp(paths[suggested[i].binding],name))return;
    fprintf(stderr,"Missing binding: %s\n",name);abort();
}
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
    running=focused=origin_set=true;assert(actions_init());assert(space_count==2&&binding_count==16);
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
    for(int n=0;n<8;n++){tick();assert(weapon_hand()==LEFT_HAND);}
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
    buttons[id(upper_action)][RIGHT_HAND]=false;tick();click(upper_action,RIGHT_HAND);assert(options.laser_enabled);
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
    puts("PASS: TC2 four-credit sequence and gun ADC bounds; real host controls, gun pitch persistence/bounds/repeat/reset, model/muzzle/aim alignment in both hands, local-space correction, focus/action loss and existing input regressions");
    return 0;
}
