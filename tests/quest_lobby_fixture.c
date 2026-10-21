/* Render the actual UI rasterizer; synthetic peers are only presentation data. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../quest/quest_keyboard.h"
static int keyboard_requests,keyboard_stops,text_events;
#ifdef TCVR_PC
static int clipboard_requests,clipboard_frees;
char *SDL_GetClipboardText(void){
    clipboard_requests++;char *text=malloc(12);assert(text);memcpy(text,"10.20.30.40",12);return text;
}
void SDL_free(void *text){clipboard_frees++;free(text);}
#endif
void SDL_StartTextInput(void){keyboard_requests++;}
void SDL_StopTextInput(void){keyboard_stops++;}
void SDL_SetTextInputRect(const SDL_Rect *rect){assert(rect->w>0);}
Uint8 SDL_EventState(Uint32 type,int state){assert(type==SDL_TEXTINPUT&&state==SDL_ENABLE);text_events++;return SDL_ENABLE;}
#include "../quest/quest_lobby.c"
static uint8_t pixels[640*480*4];
static void capture(const char *dir,const char *name){
    qlobby_render(pixels);char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n640 480\n255\n");
    for(int i=0;i<640*480;i++)fwrite(pixels+i*4,1,3,f);
    fclose(f);
}
int main(int argc,char **argv){
    assert(argc==2);tc2_link_leave();qlobby_show();assert(qlobby_visible());capture(argv[1],"home");
    qlobby_input(QL_DOWN);qlobby_input(QL_DOWN);qlobby_input(QL_ACCEPT);assert(page==IP);
    qkeyboard_update();
#ifdef TCVR_STEAM_FRAME
    assert(text_events==1&&keyboard_requests==0);capture(argv[1],"frame-keyboard-pending");
#else
    assert(keyboard_requests==1);capture(argv[1],"ip-entry");
#endif
    SDL_Event event={0};event.type=SDL_TEXTINPUT;strcpy(event.text.text,"256.168.1.42");assert(qkeyboard_event(&event));
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_RETURN;assert(qkeyboard_event(&event));
    assert(page==IP&&strstr(note,"IPv4"));capture(argv[1],"ip-invalid");
    ip[0]=0;event.type=SDL_TEXTINPUT;strcpy(event.text.text,"192.168.1.42");assert(qkeyboard_event(&event));
    assert(parse_ip()&&!strcmp(ip,"192.168.1.42"));capture(argv[1],"ip-filled");
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_BACKSPACE;assert(qkeyboard_event(&event));assert(!strcmp(ip,"192.168.1.4"));
    qlobby_text("A");assert(!strcmp(ip,"192.168.1.4"));
    strcpy(ip,"255.255.255.254");assert(parse_ip());qlobby_text("123");assert(!strcmp(ip,"255.255.255.254"));
    strcpy(ip,"0.0.0.0");assert(!parse_ip());strcpy(ip,"255.255.255.255");assert(!parse_ip());
    strcpy(ip,"1.2.3");assert(!parse_ip());strcpy(ip,"1..2.3");assert(!parse_ip());
    strcpy(ip,"001.002.003.004");assert(parse_ip()&&!strcmp(ip,"1.2.3.4"));
#ifdef TCVR_PC
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_v;
    ip[0]=0;event.key.keysym.mod=KMOD_CTRL;assert(qkeyboard_event(&event));
    assert(!strcmp(ip,"10.20.30.40")&&clipboard_requests==1&&clipboard_frees==1);
    ip[0]=0;event.key.keysym.mod=KMOD_GUI;assert(qkeyboard_event(&event));
    assert(!strcmp(ip,"10.20.30.40")&&clipboard_requests==2&&clipboard_frees==2);
    event.key.keysym.mod=KMOD_NONE;assert(qkeyboard_event(&event));assert(clipboard_requests==2);
#endif
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_ESCAPE;assert(qkeyboard_event(&event));
    qkeyboard_update();assert(keyboard_stops==1&&page==HOME&&qlobby_visible());
    qlobby_input(QL_BACK);assert(!qlobby_visible());assert(qlobby_tick(0)==0);
    TC2Session *s=(TC2Session *)tc2_link_session();
    s->state=TC2_LOBBY;s->host=1;s->compatible=1;strcpy(s->address,"192.168.1.42");strcpy(s->message,"Both players must select Ready");
    qlobby_show();capture(argv[1],"waiting");
    s->local_ready=s->peer_ready=1;focus=1;capture(argv[1],"ready");
    s->state=TC2_ERROR;s->local_ready=s->peer_ready=0;strcpy(s->message,"Game/version mismatch - update both");focus=0;capture(argv[1],"mismatch");
    strcpy(s->message,"Partner disconnected - leave/retry");capture(argv[1],"disconnect");
    qlobby_input(QL_DOWN);qlobby_input(QL_DOWN);qlobby_input(QL_ACCEPT);assert(s->state==TC2_OFF&&page==HOME);
    s->state=TC2_FINDING;s->found_count=2;strcpy(s->found[0],"192.168.1.42");strcpy(s->found[1],"192.168.1.84");strcpy(s->message,"Select a host to join");page=FIND;focus=0;capture(argv[1],"find");
    tc2_link_leave();puts("PASS: production lobby raster, text/IP validation, back/leave and ready/error states");
#ifdef TCVR_PC
    puts("PASS: desktop Ctrl+V/Cmd+V clipboard key events and buffer release");
#endif
    return 0;
}
