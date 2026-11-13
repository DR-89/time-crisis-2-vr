#include <SDL2/SDL.h>
#include <android/log.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "tc2_cabinet.h"
int tc_game_main(int,char**);
int SDL_main(int argc,char **argv) {
    (void)argc;(void)argv;
    const char *dir=SDL_AndroidGetInternalStoragePath();
    if(!dir||chdir(dir))return 2;
    remove("timecrisis2-vr-previous.log");rename("timecrisis2-vr.log","timecrisis2-vr-previous.log");
    freopen("timecrisis2-vr.log","w",stderr);setvbuf(stderr,NULL,_IOLBF,0);
    setenv("SDL_ACCELEROMETER_AS_JOYSTICK","0",1);
    /* A linked arcade must keep its 59.906 Hz while the headset is off or the app is behind the system UI: the partner's cabinet
     * depends on ours (docs/ONLINE.md). SDL would otherwise block this thread for as long as the activity is paused. Offline,
     * the host loop just sleeps while no XR session runs. */
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");
    char *args[]={"TimeCrisis2VR","--window",NULL};
    qtc2_cabinet_configure("quest-options.cfg");
    int result=tc_game_main(2,args);
    __android_log_print(ANDROID_LOG_ERROR,"TCVR","Game ended: %d; see files/timecrisis2-vr.log",result);
    if(result) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Time Crisis II VR","Startup failed. Log: files/timecrisis2-vr.log (adb run-as).",NULL);
    return result;
}
