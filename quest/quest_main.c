#include <SDL2/SDL.h>
#include <android/log.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "tc2_cabinet.h"
int tc_game_main(int,char**);
int SDL_main(int argc,char **argv) {
    (void)argc;(void)argv;
#ifdef TCVR_STEAM_FRAME
    const char *dir="/sdcard/Documents/TimeCrisis2VR";
#else
    const char *dir=SDL_AndroidGetInternalStoragePath();
#endif
    if(!dir||chdir(dir))return 2;
    remove("timecrisis2-vr-previous.log");rename("timecrisis2-vr.log","timecrisis2-vr-previous.log");
    freopen("timecrisis2-vr.log","w",stderr);setvbuf(stderr,NULL,_IOLBF,0);
    setenv("SDL_ACCELEROMETER_AS_JOYSTICK","0",1);
    char *args[]={"TimeCrisis2VR","--window",NULL};
    qtc2_cabinet_configure("quest-options.cfg");
    int result=tc_game_main(2,args);
    __android_log_print(ANDROID_LOG_ERROR,"TCVR","Game ended: %d; see files/timecrisis2-vr.log",result);
    if(result) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Time Crisis II VR","Startup failed. Log: files/timecrisis2-vr.log (adb run-as).",NULL);
    return result;
}
