#include "quest_openvr_keyboard.h"
#include <stdio.h>
#include <string.h>
#if defined(TCVR_STEAM_FRAME) || defined(TCVR_PC)
#define OPENVR_API_NODLL
/* The generated header's legacy __WIN32 branch typedefs bool; use its
 * standard C99 branch under MinGW while retaining _WIN32 calling conventions. */
#pragma push_macro("__WIN32")
#undef __WIN32
#include "vendor/openvr/openvr_capi.h"
#pragma pop_macro("__WIN32")
#ifdef _WIN32
#include <windows.h>
static void *load_library(void){
    wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n||n>=32768)return NULL;
    wchar_t *end=wcsrchr(path,L'\\');if(!end||end-path+18>=32768)return NULL;
    wcscpy(end+1,L"openvr_api.dll");
    return (void *)LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}
#define symbol(h,n) ((void *)GetProcAddress((HMODULE)(h),(n)))
#define unload(h) FreeLibrary((HMODULE)(h))
#else
#include <dlfcn.h>
#ifndef QOVR_LIBRARY_PATH
#define QOVR_LIBRARY_PATH "/data/steamvr/runtime/bin/androidarm64/libopenvr_api.so"
#endif
static void *load_library(void){return dlopen(QOVR_LIBRARY_PATH,RTLD_NOW|RTLD_LOCAL);}
#define symbol(h,n) dlsym(h,n)
#define unload(h) dlclose(h)
#endif
static void *library;
static uint32_t (*init)(EVRInitError *,EVRApplicationType);
static void (*shutdown_runtime)(void);
static intptr_t (*get_interface)(const char *,EVRInitError *);
static uint32_t (*get_token)(void);
static struct VR_IVROverlay_FnTable *overlay;
static VROverlayHandle_t handle;
static uint32_t token;
static uint64_t request;
static bool owned,shown;
static char error[80];
const char *qovr_keyboard_error(void){return error;}
static bool alive(void){return library&&get_token&&get_token()==token;}
void qovr_keyboard_cancel(void){
    if(overlay&&alive()){
        if(shown)overlay->HideKeyboard();
        if(handle)overlay->DestroyOverlay(handle);
    }
    handle=0;shown=false;
}
void qovr_keyboard_shutdown(void){
    qovr_keyboard_cancel();
    if(owned&&alive())shutdown_runtime();
    if(library)unload(library);
    library=NULL;overlay=NULL;owned=false;get_token=NULL;
}
static bool fail(const char *what,int code){
    snprintf(error,sizeof error,"STEAMVR KEYBOARD: %s (%d)",what,code);
    fprintf(stderr,"[vr-keyboard] %s\n",error);qovr_keyboard_shutdown();return false;
}
static bool connect_runtime(void){
    if(overlay&&alive())return true;
    qovr_keyboard_shutdown();library=load_library();
    if(!library){
#ifdef _WIN32
        fprintf(stderr,"[vr-keyboard] LoadLibraryEx failed: %lu\n",(unsigned long)GetLastError());
#else
        fprintf(stderr,"[vr-keyboard] dlopen %s: %s\n",QOVR_LIBRARY_PATH,dlerror());
#endif
        return fail("LIBRARY UNAVAILABLE",0);
    }
    init=(void *)symbol(library,"VR_InitInternal");
    shutdown_runtime=(void *)symbol(library,"VR_ShutdownInternal");
    get_interface=(void *)symbol(library,"VR_GetGenericInterface");
    get_token=(void *)symbol(library,"VR_GetInitToken");
    if(!init||!shutdown_runtime||!get_interface||!get_token)return fail("API UNAVAILABLE",0);
    EVRInitError status=EVRInitError_VRInitError_None;
    overlay=(void *)get_interface("FnTable:IVROverlay_027",&status);
    if(!overlay&&status==EVRInitError_VRInitError_Init_NotInitialized){
        init(&status,EVRApplicationType_VRApplication_Overlay);
        if(status!=EVRInitError_VRInitError_None)return fail("INIT FAILED",status);
        owned=true;token=get_token();
        overlay=(void *)get_interface("FnTable:IVROverlay_027",&status);
    } else token=get_token();
    if(!overlay||status!=EVRInitError_VRInitError_None)return fail("OVERLAY API UNAVAILABLE",status);
    fprintf(stderr,"[vr-keyboard] IVROverlay_027 connected (%s client)\n",owned?"owned":"borrowed");
    return true;
}
bool qovr_keyboard_show(const char *initial){
    qovr_keyboard_cancel();
    if(!connect_runtime())return false;
    EVROverlayError status=overlay->CreateOverlay("org.timecrisis2.keyboard","Time Crisis 2 IP",&handle);
    if(status!=EVROverlayError_VROverlayError_None)return fail("CREATE FAILED",status);
    /* No texture/ShowOverlay: the handle only routes keyboard events to us. */
    status=overlay->ShowKeyboardForOverlay(handle,EGamepadTextInputMode_k_EGamepadTextInputModeNormal,
        EGamepadTextInputLineMode_k_EGamepadTextInputLineModeSingleLine,EKeyboardFlags_KeyboardFlag_Modal,
        "HOST IPv4 ADDRESS",15,(char *)initial,++request);
    if(status!=EVROverlayError_VROverlayError_None)return fail("SHOW FAILED",status);
    shown=true;error[0]=0;fprintf(stderr,"[vr-keyboard] ShowKeyboardForOverlay accepted\n");return true;
}
int qovr_keyboard_poll(char text[16]){
    if(!shown)return 0;
    if(!alive()){fail("RUNTIME RESTARTED",0);return -2;}
    struct VREvent_t event;
    for(int i=0;i<32&&overlay->PollNextOverlayEvent(handle,&event,sizeof event);i++){
        if(event.eventType!=EVREventType_VREvent_KeyboardDone&&event.eventType!=EVREventType_VREvent_KeyboardClosed)continue;
        if(event.data.keyboard.uUserValue!=request)continue;
        if(event.eventType==EVREventType_VREvent_KeyboardDone){
            memset(text,0,16);overlay->GetKeyboardText(text,16);text[15]=0;
            qovr_keyboard_cancel();return 1;
        }
        shown=false;qovr_keyboard_cancel();return -1;
    }
    return 0;
}
#else
bool qovr_keyboard_show(const char *initial){(void)initial;return false;}
int qovr_keyboard_poll(char text[16]){(void)text;return 0;}
void qovr_keyboard_cancel(void){}
void qovr_keyboard_shutdown(void){}
const char *qovr_keyboard_error(void){return "STEAMVR KEYBOARD UNAVAILABLE";}
#endif
