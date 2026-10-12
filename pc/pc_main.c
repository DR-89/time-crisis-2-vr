#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "tc2_cabinet.h"

int tc_game_main(int,char **);
/* SDL supplies the discrete-GPU selection exports for hybrid laptops. */

int main(int argc,char **argv){
    SDL_SetMainReady();
    int headless=0,no_dialog=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--headless"))headless=1;
        if(!strcmp(argv[i],"--no-dialog"))no_dialog=1;
    }
    if(no_dialog)setenv("TC2_NO_DIALOG","1",1);
    char *base=SDL_GetBasePath();if(base){_chdir(base);SDL_free(base);}
    remove("tc2-previous.log");rename("tc2.log","tc2-previous.log");
    setenv("TC2_OUT_PORT","0",1);
    char **args=calloc((size_t)argc+4,sizeof *args);if(!args)return 2;
    int n=0;args[n++]=argv[0];args[n++]=headless?"reset":"--window";
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--desktop"))setenv("TCVR_DESKTOP","1",1);
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)setenv("TC2_MAX_FRAMES",argv[++i],1);
        else if(!strcmp(argv[i],"--replay")&&i+1<argc)setenv("TC2_REPLAY",argv[++i],1);
        else if(!strcmp(argv[i],"--headless")||!strcmp(argv[i],"--no-dialog"))continue;
        else args[n++]=argv[i];
    }
    qtc2_cabinet_configure("quest-options.cfg");
    int result=tc_game_main(n,args);free(args);
    if(result&&!headless&&!no_dialog)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Time Crisis II VR",
        "Startup failed. See tc2.log next to the executable.\nFor VR, connect your headset and select an OpenXR runtime with OpenGL support (such as SteamVR).\nUse Play Desktop.cmd to play without a headset.",NULL);
    return result;
}
