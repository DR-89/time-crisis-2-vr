#include "quest_lobby.h"
#include "quest_panel.h"
#include "tc2_link.h"
#include <stdio.h>
#include <string.h>
static bool visible;
static enum { HOME,IP,FIND,SESSION } page;
static int focus,resume,last_action;
static unsigned revision,keyboard_request;
static TC2SessionState previous;
static char ip[16],note[80];
static void changed(void){revision++;}
void qlobby_show(void) {
    visible=true;focus=0;note[0]=0;
    page=tc2_link_session()->state==TC2_OFF?HOME:SESSION;changed();
}
bool qlobby_visible(void){return visible;}
unsigned qlobby_revision(void){return revision+tc2_link_session()->revision;}
bool qlobby_editing(void){return visible&&page==IP;}
void qlobby_keyboard_result(bool requested){
    if(qlobby_editing()){snprintf(note,sizeof note,"%s",requested?"TYPE OR PASTE THE HOST IP":"STEAMVR KEYBOARD NOT CONNECTED");changed();}
}
unsigned qlobby_keyboard_request(void){return keyboard_request;}
const char *qlobby_text_value(void){return ip;}
void qlobby_keyboard_error(const char *message){if(qlobby_editing()){snprintf(note,sizeof note,"%s",message);changed();}}
void qlobby_replace_text(const char *text){if(qlobby_editing()){ip[0]=0;qlobby_text(text);}}
void qlobby_text(const char *text){
    if(!qlobby_editing())return;
    size_t n=strlen(ip),add=strlen(text);
    if(n+add>15){snprintf(note,sizeof note,"IPv4 address is too long");changed();return;}
    for(size_t i=0;i<add;i++)if((text[i]<'0'||text[i]>'9')&&text[i]!='.'){
        snprintf(note,sizeof note,"Use numbers and dots only");changed();return;
    }
    memcpy(ip+n,text,add+1);note[0]=0;changed();
}
void qlobby_backspace(void){if(qlobby_editing()){size_t n=strlen(ip);if(n)ip[n-1]=0;note[0]=0;changed();}}
static bool parse_ip(void){
    const char *p=ip;unsigned value[4];
    for(int group=0;group<4;group++){
        unsigned n=0,v=0;while(*p>='0'&&*p<='9'){v=v*10+(unsigned)(*p++-'0');n++;}
        if(!n||n>3||v>255||(group<3?*p++!='.':*p!=0))goto invalid;
        value[group]=v;
    }
    if((value[0]|value[1]|value[2]|value[3])==0 || (value[0]&value[1]&value[2]&value[3])==255)goto invalid;
    char normalized[16];snprintf(normalized,sizeof normalized,"%u.%u.%u.%u",value[0],value[1],value[2],value[3]);
    strcpy(ip,normalized);return true;
invalid:
    snprintf(note,sizeof note,"Enter an IPv4 like 192.168.1.42");return false;
}
static void back(void){
    if(page==HOME || tc2_link_session()->state==TC2_PLAYING){visible=false;}
    else {tc2_link_leave();page=HOME;focus=0;note[0]=0;}
}
void qlobby_input(QLobbyKey key){
    if(!visible)return;
    changed();
    if(key==QL_BACK){back();return;}
    const TC2Session *s=tc2_link_session();
    if(page==IP){
        if(key==QL_ACCEPT){if(parse_ip()){tc2_link_join(ip);page=SESSION;focus=0;}else keyboard_request++;}
        return;
    }
    int count=page==HOME?4:page==FIND?(int)s->found_count+1:3;
    if(key==QL_UP||key==QL_LEFT){focus=(focus+count-1)%count;return;}
    if(key==QL_DOWN||key==QL_RIGHT){focus=(focus+1)%count;return;}
    if(key!=QL_ACCEPT)return;
    if(page==HOME){
        if(focus==0){last_action=0;tc2_link_host();page=SESSION;focus=0;}
        else if(focus==1){last_action=1;tc2_link_find();page=FIND;focus=0;}
        else if(focus==2){last_action=2;page=IP;note[0]=0;keyboard_request++;}
        else back();
    } else if(page==FIND){
        if(focus<(int)s->found_count){last_action=2;snprintf(ip,sizeof ip,"%s",s->found[focus]);tc2_link_join(ip);page=SESSION;focus=0;}
        else back();
    } else if(focus==2){tc2_link_leave();page=HOME;focus=0;}
    else if(s->state==TC2_ERROR && focus==0){if(last_action==1){tc2_link_find();page=FIND;}else if(s->host)tc2_link_host();else tc2_link_join(ip);}
    else if(focus==0)tc2_link_ready();
    else if(s->state==TC2_PLAYING){visible=false;resume=1;}
    else tc2_link_start();
}
int qlobby_tick(uint64_t milliseconds){
    tc2_link_service(milliseconds);
    TC2SessionState now=tc2_link_session()->state;
    if(now==TC2_PLAYING && previous!=now){visible=false;resume=1;changed();}
    if(now==TC2_ERROR && previous!=now){qlobby_show();page=SESSION;previous=now;return -1;}
    previous=now;
    if(resume){resume=0;return 1;}return 0;
}
static uint8_t *canvas;
static void label(int x,int y,const char *s){qpanel_text(canvas,x,y,s,190,218,230);}
static void button(int y,const char *s,int index,bool enabled){
    bool selected=focus==index;
    qpanel_rect(canvas,24,y,592,36,selected?54:36,selected?183:53,selected?207:64);
    qpanel_rect(canvas,26,y+2,588,32,selected?22:15,selected?63:27,selected?76:39);
    qpanel_text(canvas,40,y+10,s,enabled?235:103,enabled?247:124,enabled?252:134);
}
void qlobby_render(uint8_t pixels[640*480*4]){
    canvas=pixels;const TC2Session *s=tc2_link_session();
    qpanel_rect(pixels,0,0,640,480,54,183,207);qpanel_rect(pixels,2,2,636,476,12,19,29);
    label(24,20,"LAN PLAY / TWO CABINETS");
    if(page==HOME){
        label(24,56,"Original red + blue cabinet game");
        button(94,"HOST A GAME",0,true);button(138,"FIND A GAME",1,true);button(182,"JOIN BY IP",2,true);
        label(40,246,"RED PLAYER          WAITING");label(40,282,"BLUE PLAYER         WAITING");
        label(24,330,"Use the same Wi-Fi / LAN");button(380,"BACK TO OPTIONS",3,true);
    }else if(page==IP){
        label(24,56,"JOIN BY IP / RED HOST ADDRESS");
        label(40,118,"HOST IP ADDRESS");
        qpanel_rect(pixels,24,150,592,54,54,183,207);qpanel_rect(pixels,26,152,588,50,22,63,76);
        label(40,170,ip[0]?ip:"_");
        label(24,242,"Use the platform keyboard");
        label(24,276,"ENTER / A: CONNECT   B: CANCEL");
        label(24,332,note[0]?note:"Example: 192.168.1.42");
        label(24,384,"Or go back and FIND A GAME");
    }else if(page==FIND){
        label(24,56,s->message);
        for(unsigned i=0;i<s->found_count;i++){char row[40];snprintf(row,sizeof row,"RED HOST  %s",s->found[i]);button(100+(int)i*46,row,(int)i,true);}
        button(100+(int)s->found_count*46,"BACK / JOIN BY IP",(int)s->found_count,true);
        label(24,370,"Host must be on the same LAN");
    }else{
        char row[40];snprintf(row,sizeof row,"%s  %s",s->host?"HOST IP:":"HOST:",s->address);label(24,56,row);
        /* The same stacked actions stay visible while a session is active. */
        button(94,s->state==TC2_ERROR?"RETRY CONNECTION":s->local_ready?"READY - SELECT TO CANCEL":"I AM READY",0,s->state==TC2_LOBBY||s->state==TC2_ERROR);
        bool start=s->host&&s->state==TC2_LOBBY&&s->local_ready&&s->peer_ready;
        button(138,s->state==TC2_PLAYING?"RETURN TO GAME":s->host?"START GAME":"WAIT FOR HOST TO START",1,start||s->state==TC2_PLAYING);
        button(182,"LEAVE SESSION",2,true);
        bool red=s->host?s->local_ready:s->peer_ready,blue=s->host?s->peer_ready:s->local_ready;
        snprintf(row,sizeof row,"RED  %-7s       %s",s->host?"YOU":"PARTNER",red?"READY":"WAITING");label(40,246,row);
        snprintf(row,sizeof row,"BLUE %-7s       %s",s->host?"PARTNER":"YOU",blue?"READY":"WAITING");label(40,282,row);
        const char *message=s->state==TC2_LOBBY&&s->local_ready&&s->peer_ready?(s->host?"BOTH READY - HOST CAN START":"WAITING FOR HOST TO START"):s->message;
        char first[37];snprintf(first,sizeof first,"%.36s",message);label(24,330,first);
        if(strlen(message)>36)label(24,352,message+36);
        label(24,384,"START restarts both arcade cabinets");
    }
    label(24,438,"STICK: MOVE  A: SELECT  B: BACK");
}
