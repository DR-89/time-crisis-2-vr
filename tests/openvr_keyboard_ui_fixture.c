#define main ordinary_lobby_fixture
#include "quest_lobby_fixture.c"
#undef main
#include <dlfcn.h>
int main(int argc,char **argv){
 assert(argc==3);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(lib);
 void (*event)(unsigned,const char *)=dlsym(lib,"fake_event");
 tc2_link_leave();qlobby_show();qlobby_input(QL_DOWN);qlobby_input(QL_DOWN);qlobby_input(QL_ACCEPT);
 qlobby_text("1.2.3.4");qkeyboard_update();assert(qkeyboard_modal());
 SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_RETURN;assert(qkeyboard_event(&key));
 qlobby_input(QL_LEFT);assert(page==IP&&tc2_link_session()->state==TC2_OFF);
 key.type=SDL_TEXTINPUT;strcpy(key.text.text,"9");assert(qkeyboard_event(&key));assert(!strcmp(ip,"1.2.3.4"));
 event(1202,"10.20.30.40");qkeyboard_update();assert(!qkeyboard_modal());
 assert(page==IP&&!strcmp(ip,"10.20.30.40")&&tc2_link_session()->state==TC2_OFF);capture(argv[2],"keyboard-done");
 /* Invalid input can reopen the native editor; no custom keyboard fallback. */
 qlobby_replace_text("999.2.3.4");qlobby_input(QL_ACCEPT);qkeyboard_update();assert(qkeyboard_modal());
 event(1200,"");qkeyboard_update();assert(!qkeyboard_modal()&&page==HOME&&!strcmp(ip,"999.2.3.4"));
 qkeyboard_update();qlobby_input(QL_DOWN);qlobby_input(QL_DOWN);qlobby_input(QL_ACCEPT);qkeyboard_update();assert(qkeyboard_modal());
 key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_ESCAPE;assert(qkeyboard_event(&key));assert(page==HOME&&!qkeyboard_modal());
 qkeyboard_shutdown();dlclose(lib);puts("PASS production keyboard/lobby: modal input, replacement Done without connect, cancel preserving text, retry and Escape");
}
