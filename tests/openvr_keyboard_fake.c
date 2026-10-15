#include <stdio.h>

#define OPENVR_API_NODLL
#include "../quest/vendor/openvr/openvr_capi.h"
#include <assert.h>
#include <string.h>
static uint32_t token=7;
static bool initialized;
static uint64_t user;
static unsigned event_type;
static char entered[16],initial_text[16];
static int mode,init_count,shutdown_count,create_count,destroy_count,show_count,hide_count;
void fake_mode(int value){mode=value;initialized=value==1;}
void fake_event(unsigned type,const char *text){event_type=type;snprintf(entered,sizeof entered,"%s",text);}
void fake_restart(void){token++;}
int fake_count(int which){int counters[]={init_count,shutdown_count,create_count,destroy_count,show_count,hide_count};return counters[which];}
const char *fake_initial(void){return initial_text;}
uint32_t VR_GetInitToken(void){return token;}
intptr_t VR_InitInternal(EVRInitError *error,EVRApplicationType type){assert(type==EVRApplicationType_VRApplication_Overlay);init_count++;*error=mode==2?EVRInitError_VRInitError_Init_HmdNotFound:0;initialized=!(*error);return ++token;}
void VR_ShutdownInternal(void){shutdown_count++;initialized=false;token++;}
static EVROverlayError OPENVR_FNTABLE_CALLTYPE create(char *key,char *name,VROverlayHandle_t *handle){assert(strstr(key,"timecrisis2")&&name);create_count++;*handle=42;return 0;}
static EVROverlayError OPENVR_FNTABLE_CALLTYPE destroy(VROverlayHandle_t handle){assert(handle==42);destroy_count++;return 0;}
static EVROverlayError OPENVR_FNTABLE_CALLTYPE show(VROverlayHandle_t handle,EGamepadTextInputMode input,EGamepadTextInputLineMode line,uint32_t flags,char *desc,uint32_t max,char *text,uint64_t value){
 assert(handle==42&&input==0&&line==0&&flags==EKeyboardFlags_KeyboardFlag_Modal&&desc&&max==15);
 show_count++;user=value;strcpy(initial_text,text);return mode==3?EVROverlayError_VROverlayError_KeyboardAlreadyInUse:0;
}
static void OPENVR_FNTABLE_CALLTYPE hide(void){hide_count++;}
static uint32_t OPENVR_FNTABLE_CALLTYPE get_text(char *text,uint32_t max){assert(max==16);strcpy(text,entered);return (uint32_t)strlen(text)+1;}
static bool OPENVR_FNTABLE_CALLTYPE poll(VROverlayHandle_t handle,struct VREvent_t *event,uint32_t size){
 assert(handle==42&&size==sizeof *event);if(!event_type)return false;
 memset(event,0,sizeof *event);event->eventType=event_type;event->data.keyboard.uUserValue=user;event->data.keyboard.overlayHandle=42;event_type=0;return true;
}
static struct VR_IVROverlay_FnTable table={.CreateOverlay=create,.DestroyOverlay=destroy,.ShowKeyboardForOverlay=show,.HideKeyboard=hide,.GetKeyboardText=get_text,.PollNextOverlayEvent=poll};
intptr_t VR_GetGenericInterface(const char *version,EVRInitError *error){
 assert(!strcmp(version,"FnTable:IVROverlay_027"));
 *error=!initialized?EVRInitError_VRInitError_Init_NotInitialized:mode==4?EVRInitError_VRInitError_Init_InterfaceNotFound:0;
 return *error?0:(intptr_t)&table;
}
