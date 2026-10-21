#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "../quest/quest_openvr_keyboard.h"
int main(int argc,char **argv){
 assert(argc==3);void *fake=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(fake);
 void (*mode)(int)=dlsym(fake,"fake_mode");void (*event)(unsigned,const char *)=dlsym(fake,"fake_event");
 void (*restart)(void)=dlsym(fake,"fake_restart");int (*count)(int)=dlsym(fake,"fake_count");
 const char *(*initial)(void)=dlsym(fake,"fake_initial");
 int scenario=atoi(argv[2]);mode(scenario);char text[16];
 if(scenario>=2&&scenario<=4){assert(!qovr_keyboard_show("1.2.3.4"));assert(strstr(qovr_keyboard_error(),"FAILED")||strstr(qovr_keyboard_error(),"UNAVAILABLE"));assert(count(1)==(scenario==2?0:1));if(scenario==3){assert(count(3)==1&&count(5)==0);}puts("PASS OpenVR failure cleanup");return 0;}
 assert(qovr_keyboard_show("1.2.3.4"));assert(!strcmp(initial(),"1.2.3.4"));assert(qovr_keyboard_poll(text)==0);
 event(1202,"10.20.30.40");assert(qovr_keyboard_poll(text)==1&&!strcmp(text,"10.20.30.40"));assert(count(3)==1&&count(5)==1);
 assert(qovr_keyboard_show("10.20.30.40"));event(1200,"");assert(qovr_keyboard_poll(text)==-1);assert(count(3)==2&&count(5)==1);
 assert(qovr_keyboard_show("10.20.30.40"));qovr_keyboard_cancel();assert(count(3)==3&&count(5)==2);
 assert(qovr_keyboard_show("10.20.30.40"));
 if(scenario==5){restart();assert(qovr_keyboard_poll(text)==-2);assert(count(1)==0&&count(3)==3);}
 qovr_keyboard_shutdown();assert(count(0)==(scenario==1?0:1));assert(count(1)==(scenario==0?1:0));
 dlclose(fake);puts("PASS OpenVR owned/borrowed, Done, cancel, reopen and runtime invalidation");return 0;
}
