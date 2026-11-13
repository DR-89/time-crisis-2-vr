/* quest_menu.c -- the pause menu (see quest_menu.h).
 *
 * Immediate mode: one layout function describes the whole menu. It runs once with this frame's input (clicks, stick steps,
 * drags) to apply actions, once more to settle the hit boxes, focus order and help line for the new state, and once more to
 * draw, only when a hash of everything shown has changed. The layout is fixed in pixels and every tab fits its 428 px body:
 * there is no scrolling anywhere (tests/menu_fixture.c checks each tab and online state). */
#include "quest_menu.h"
#include "quest_canvas.h"
#include "quest_options.h"
#include "quest_resolution.h"
#include "tc2_online.h"
#include "ui_font_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- palette (the mockup's) ---- */
static const QColor PANEL={12,19,29,245},EDGE={54,183,207,255},LINE={31,51,71,255},BODY={17,27,40,255},TEXT={238,243,246,255},
    MUTED={143,166,181,255},DIM={91,115,133,255},ACCENT={255,202,110,255},INK={26,18,6,255},GOOD={140,235,150,255},
    WARN={255,150,120,255},HOVER={26,43,61,255},ITEM={14,24,36,255},WELL={10,17,26,255},RAIL={36,56,75,255},ZERO={76,98,116,255},
    GOLD={36,29,15,255},GOLDLINE={107,84,38,255},DANGER={90,43,34,255},WARNLINE={107,51,38,255},KBD={26,38,53,255},KBDLINE={46,66,87,255},
    MODAL={16,26,38,255},SHADE={4,7,11,184},GOODINK={11,36,16,255},WARNBG={58,30,24,255},WARNINK={42,13,7,255};

/* ---- layout constants (panel pixels) ---- */
enum { BODY_Y=135,BODY_H=428,CX0=29,CX1=1091,CW=CX1-CX0,CY0=BODY_Y+16,FOOT_Y=BODY_Y+BODY_H,COLW=520,COL2=CX0+COLW+22 };

/* ---- ids: kind << 8 | argument ---- */
enum { K_TAB=1,K_FOOT,K_ROW,K_SEG,K_STEP,K_TRACK,K_RESET,K_RES,K_ON,K_GAME,K_KEY,K_MBTN,K_VOICE,K_VOL,K_VOLROW,K_TABS,K_CREDIT };
#define ID(k,a) ((k)<<8|(a))
#define KIND(id) ((id)>>8)
#define ARG(id) ((id)&255)
enum { ON_HOST,ON_CODE,ON_BROWSE,ON_QUICK,ON_LAN,ON_REJOIN,ON_NAME,ON_MUTE,ON_LEAVE,ON_BACK,ON_REFRESH,ON_HOSTLAN,ON_IPEDIT,ON_JOINIP,ON_PREV,ON_NEXT,ON_SERVER };
enum { M_NONE,M_RESTART,M_RESTART_LEAVE,M_APP,M_CODE,M_NAME,M_IP,M_SERVER };
enum { S_MAIN,S_BROWSE,S_LAN };
enum { F_BUTTON,F_SEG,F_SLIDER,F_TABS };   /* what left / right does on a focused row */
enum { PASS_ACT,PASS_LAYOUT,PASS_DRAW };

/* ---- the settings rows ---- */
enum { T_SEG,T_SLIDER,T_RES };
typedef struct RowDef { int opt;const char *label,*sub,*help;int type;const char *opts[3];int nopts,min,max,step,def;const char *unit;bool sign; } RowDef;
static const RowDef GUN_ROWS[]={
    {QOPT_HAND,"Gun hand","Either trigger still swaps hands in game","The hand that starts with the gun. Pulling either trigger in game still swaps hands.",T_SEG,{"LEFT","RIGHT"},2,0,1,1,1,"",false},
    {QOPT_AIM,"Aim guide","What shows where you point","LASER shows where you point. TRACER flashes a streak on each shot. OFF is arcade-pure.",T_SEG,{"LASER","TRACER","OFF"},3,0,2,1,0,"",false},
    {QOPT_PITCH,"Gun angle","Lower it if shots land high","Tilts the barrel about your hand. + raises it.",T_SLIDER,{0},0,QOPTIONS_GUN_PITCH_MIN,QOPTIONS_GUN_PITCH_MAX,1,0,"\xC2\xB0",true},
    {QOPT_GUN_Y,"Gun height","Up or down in your hand","+ raises the gun relative to the controller.",T_SLIDER,{0},0,QOPTIONS_GUN_OFFSET_MIN,QOPTIONS_GUN_OFFSET_MAX,QOPTIONS_GUN_OFFSET_STEP,0," mm",true},
    {QOPT_GUN_Z,"Gun depth","+ pulls it back towards you","Slides the gun forward or back along the controller.",T_SLIDER,{0},0,QOPTIONS_GUN_OFFSET_MIN,QOPTIONS_GUN_OFFSET_MAX,QOPTIONS_GUN_OFFSET_STEP,0," mm",true},
};
static const RowDef PLAYER_ROWS[]={
    {QOPT_PLAYER,"Player","Left or right cabinet","Keith (P1, left cabinet) or Robert (P2, right cabinet). Applies after RESTART APP.",T_SEG,{"P1 KEITH","P2 ROBERT"},2,0,1,1,0,"",false},
    {QOPT_COVER,"Take cover","","GRIP: hold either grip to leave cover. DUCK: crouch for real to hide; RECENTER standing up to calibrate.",T_SEG,{"GRIP","DUCK"},2,0,1,1,0,"",false},
    {QOPT_WORLD,"World size","100% is life size","How big the arcade world feels around you.",T_SLIDER,{0},0,QOPTIONS_WORLD_SCALE_MIN,QOPTIONS_WORLD_SCALE_MAX,QOPTIONS_WORLD_SCALE_STEP,QOPTIONS_WORLD_SCALE_DEFAULT,"%",false},
    {QOPT_DISTANCE,"View distance","Pushes the view further away","Start at 0 cm and add distance if things feel too close.",T_SLIDER,{0},0,QOPTIONS_VIEW_DISTANCE_MIN,QOPTIONS_VIEW_DISTANCE_MAX,QOPTIONS_VIEW_DISTANCE_STEP,QOPTIONS_VIEW_DISTANCE_DEFAULT," cm",false},
    {QOPT_SKIP_STARTUP,"Skip start-up","Shoot through the memory test and logos","ON: a shot during the arcade's start-up (memory test, countdown, logos) runs through it, silent. Not while linked online.",T_SEG,{"OFF","ON"},2,0,1,1,0,"",false},
};
static const RowDef GRAPHICS_ROWS[]={
    {QOPT_RESOLUTION,"Resolution","","Pixels per eye. Higher costs more GPU time. Default follows the headset. Applies after RESTART APP.",T_RES,{0},0,0,QRESOLUTION_COUNT-1,1,0,"",false},
    {QOPT_FILTER,"Texture filtering","Smooths world textures; text stays sharp","Applies immediately.",T_SEG,{"ON","OFF"},2,0,1,1,0,"",false},
};
static const RowDef AUDIO_ROWS[]={
    {QOPT_GAME_VOLUME,"Game volume","The arcade's sound","The game's music, guns and voices. Voice chat is not affected.",T_SLIDER,{0},0,QOPTIONS_GAME_VOLUME_MIN,QOPTIONS_GAME_VOLUME_MAX,QOPTIONS_GAME_VOLUME_STEP,QOPTIONS_GAME_VOLUME_DEFAULT,"%",false},
    {QOPT_VOICE_VOLUME,"Voice chat volume","Your partner online; also on the ONLINE tab","How loud your partner is. Over 100% boosts a quiet microphone.",T_SLIDER,{0},0,0,200,10,100,"%",false},
};
static const RowDef *row_def(int opt){
    for(size_t i=0;i<sizeof GUN_ROWS/sizeof *GUN_ROWS;i++)if(GUN_ROWS[i].opt==opt)return &GUN_ROWS[i];
    for(size_t i=0;i<sizeof PLAYER_ROWS/sizeof *PLAYER_ROWS;i++)if(PLAYER_ROWS[i].opt==opt)return &PLAYER_ROWS[i];
    for(size_t i=0;i<sizeof GRAPHICS_ROWS/sizeof *GRAPHICS_ROWS;i++)if(GRAPHICS_ROWS[i].opt==opt)return &GRAPHICS_ROWS[i];
    for(size_t i=0;i<sizeof AUDIO_ROWS/sizeof *AUDIO_ROWS;i++)if(AUDIO_ROWS[i].opt==opt)return &AUDIO_ROWS[i];
    return NULL;
}

/* ---- state ---- */
static int tab,screen,page,modal;
static char kbuf[64];
static const char *kerror;
static bool klower;
static int hover,pressed,focus;
static bool stick_mode;
static bool have_pointer;static float ptr_x,ptr_y;   /* the laser on the panel, smoothed */
static double repeat_at,last_draw;
static int stick_dx,stick_dy;static double stick_next;
static const QMenuHost *H;
static tc2_online_view V;
static tc2_online_game games[32];static int game_count;
static uint32_t drawn_hash=1,drawn_live;
/* this pass */
static int pass;
static QCanvas cv;
static int ev_click,ev_adjust,ev_adjust_dir,ev_drag;static float ev_drag_x;
typedef struct Hit { int id,x,y,w,h; } Hit;
static Hit hits[200];static int nhits;
typedef struct Focus { int id,row,cx,kind; } Focus;
static Focus foci[120];static int nfoci,frow;
static char help[200];

void qmenu_open(int t){
    if(t>=0&&t<QMENU_TAB_COUNT)tab=t;
    screen=S_MAIN;modal=M_NONE;pressed=0;focus=ID(K_FOOT,0);
    if(t==QMENU_TAB_ONLINE)focus=ID(K_TABS,0);
}
int qmenu_tab(void){return tab;}
bool qmenu_pointer(float *px,float *py){if(!have_pointer)return false;*px=ptr_x;*py=ptr_y;return true;}
bool qmenu_modal(void){return modal!=M_NONE;}

/* ---- layout primitives ---- */
static bool drawing(void){return pass==PASS_DRAW;}
static bool is_hover(int id){return !stick_mode&&hover==id;}
/* the pointer is somewhere in this box (a row stays lit over its own buttons) */
static bool pointer_in(float x,float y,float w,float h){return !stick_mode&&have_pointer&&hover&&ptr_x>=x&&ptr_x<x+w&&ptr_y>=y&&ptr_y<y+h;}
static bool is_focus(int id){return stick_mode&&focus==id;}
static void hit(int id,int x,int y,int w,int h){if(nhits<(int)(sizeof hits/sizeof *hits))hits[nhits++]=(Hit){id,x,y,w,h};}
static void focusable(int id,int kind,int cx){if(nfoci<(int)(sizeof foci/sizeof *foci))foci[nfoci++]=(Focus){id,frow,cx,kind};}
static void next_row(void){frow++;}
static bool clicked(int id){return pass==PASS_ACT&&ev_click==id;}
static void say_help(int id,const char *text){if((is_hover(id)||is_focus(id))&&text)snprintf(help,sizeof help,"%s",text);}
static void rrect(float x,float y,float w,float h,float r,QColor k){if(drawing())qc_rrect(&cv,x,y,w,h,r,k);}
static void rline(float x,float y,float w,float h,float r,float t,QColor k){if(drawing())qc_rrect_line(&cv,x,y,w,h,r,t,k);}
static int text(int font,float x,float y,const char *s,QColor k){return drawing()?qc_text(&cv,font,x,y,s,k,0):qc_text_width(font,s,0);}
static void text_track(int font,float x,float y,const char *s,QColor k,int tracking){if(drawing())qc_text(&cv,font,x,y,s,k,tracking);}
static void text_fit(int font,float x,float y,float w,const char *s,QColor k,int align){if(drawing())qc_text_fit(&cv,font,x,y,w,s,k,align);}
static int width(int font,const char *s){return qc_text_width(font,s,0);}
/* the baseline that centres a face's capitals in a box */
static float mid(int font,float y,float h){return y+h/2+qc_font_size(font)*.36f;}
/* wrapped text; returns the lines used */
static int para(int font,float x,float y,float w,float line_h,const char *s,QColor k,int max_lines){
    char line[200];int lines=0;
    while(*s&&lines<max_lines){
        int n=0,last_space=-1;
        while(s[n]){
            if(s[n]==' ')last_space=n;
            char t[200];int m=n+1<(int)sizeof t-1?n+1:(int)sizeof t-1;memcpy(t,s,m);t[m]=0;
            if(width(font,t)>w&&n>0){if(last_space>0)n=last_space;break;}
            n++;
        }
        int m=n<(int)sizeof line-1?n:(int)sizeof line-1;memcpy(line,s,m);line[m]=0;
        if(lines==max_lines-1&&s[n])text_fit(font,x,y+lines*line_h,w,s,k,0);else text(font,x,y+lines*line_h,line,k);
        lines++;s+=n;while(*s==' ')s++;
    }
    return lines;
}

/* ---- icons ---- */
enum { I_PLAY,I_LEFT,I_RIGHT,I_RESTART,I_RECENTER,I_APP,I_RESET,I_LOCK,I_HOUSE,I_KEYS,I_LIST,I_BOLT,I_SWAP,I_REJOIN,I_EJECT,I_CROSS,I_CLOUD,I_REFRESH };
static void icon(int kind,float cx,float cy,float s,QColor k){
    if(!drawing())return;
    const float h=s/2,t=s*.13f<2?2:s*.13f;
    switch(kind){
    case I_PLAY:{float p[]={cx-h*.7f,cy-h*.85f,cx+h*.85f,cy,cx-h*.7f,cy+h*.85f};qc_poly(&cv,p,3,k);break;}
    case I_LEFT:{float p[]={cx+h*.5f,cy-h*.6f,cx-h*.55f,cy,cx+h*.5f,cy+h*.6f};qc_poly(&cv,p,3,k);break;}
    case I_RIGHT:{float p[]={cx-h*.5f,cy-h*.6f,cx+h*.55f,cy,cx-h*.5f,cy+h*.6f};qc_poly(&cv,p,3,k);break;}
    case I_RESTART:case I_RESET:case I_REFRESH:{   /* an anticlockwise arrow */
        float r=h*.72f;qc_arc(&cv,cx,cy,r,-2.2f,3.0f,t,k);
        float ax=cx+cosf(-2.2f)*r,ay=cy+sinf(-2.2f)*r;float p[]={ax-h*.42f,ay-h*.18f,ax+h*.22f,ay-h*.5f,ax+h*.18f,ay+h*.26f};qc_poly(&cv,p,3,k);break;}
    case I_APP:{   /* a clockwise arrow */
        float r=h*.72f;qc_arc(&cv,cx,cy,r,-.95f,4.15f,t,k);
        float ax=cx+cosf(-.95f)*r,ay=cy+sinf(-.95f)*r;float p[]={ax+h*.42f,ay-h*.18f,ax-h*.22f,ay-h*.5f,ax-h*.18f,ay+h*.26f};qc_poly(&cv,p,3,k);break;}
    case I_RECENTER:qc_arc(&cv,cx,cy,h*.62f,0,6.2832f,t*.8f,k);qc_line(&cv,cx-h,cy,cx-h*.35f,cy,t*.8f,k);qc_line(&cv,cx+h*.35f,cy,cx+h,cy,t*.8f,k);
        qc_line(&cv,cx,cy-h,cx,cy-h*.35f,t*.8f,k);qc_line(&cv,cx,cy+h*.35f,cx,cy+h,t*.8f,k);qc_circle(&cv,cx,cy,t*.9f,k);break;
    case I_LOCK:qc_rrect(&cv,cx-h*.62f,cy-h*.1f,h*1.24f,h,h*.18f,k);qc_arc(&cv,cx,cy-h*.12f,h*.38f,3.1416f,6.2832f,t,k);
        qc_line(&cv,cx-h*.38f,cy-h*.12f,cx-h*.38f,cy,t,k);qc_line(&cv,cx+h*.38f,cy-h*.12f,cx+h*.38f,cy,t,k);break;
    case I_HOUSE:{float p[]={cx-h*.9f,cy-h*.05f,cx,cy-h*.85f,cx+h*.9f,cy-h*.05f};qc_poly(&cv,p,3,k);qc_rrect(&cv,cx-h*.6f,cy-h*.1f,h*1.2f,h*.95f,1,k);
        qc_rrect(&cv,cx-h*.18f,cy+h*.3f,h*.36f,h*.55f,1,WELL);break;}
    case I_KEYS:qc_rrect_line(&cv,cx-h,cy-h*.6f,s,h*1.2f,h*.2f,t*.8f,k);
        for(int r=0;r<2;r++)for(int i=0;i<4;i++)qc_rrect(&cv,cx-h*.7f+i*h*.42f-h*.1f,cy-h*.32f+r*h*.36f,h*.22f,h*.2f,1,k);
        qc_rrect(&cv,cx-h*.45f,cy+h*.32f,h*.9f,h*.14f,1,k);break;
    case I_LIST:for(int i=0;i<3;i++)qc_line(&cv,cx-h*.8f,cy-h*.55f+i*h*.55f,cx+h*.8f,cy-h*.55f+i*h*.55f,t,k);break;
    case I_BOLT:{float p[]={cx+h*.2f,cy-h,cx-h*.6f,cy+h*.12f,cx-h*.02f,cy+h*.12f,cx-h*.2f,cy+h,cx+h*.6f,cy-h*.15f,cx+h*.02f,cy-h*.15f};qc_poly(&cv,p,6,k);break;}
    case I_SWAP:qc_line(&cv,cx-h*.8f,cy-h*.35f,cx+h*.8f,cy-h*.35f,t,k);qc_line(&cv,cx-h*.8f,cy+h*.35f,cx+h*.8f,cy+h*.35f,t,k);
        {float a[]={cx+h*.9f,cy-h*.35f,cx+h*.4f,cy-h*.7f,cx+h*.4f,cy};qc_poly(&cv,a,3,k);float b[]={cx-h*.9f,cy+h*.35f,cx-h*.4f,cy,cx-h*.4f,cy+h*.7f};qc_poly(&cv,b,3,k);}break;
    case I_REJOIN:{float r=h*.55f;qc_arc(&cv,cx+h*.1f,cy,r,-1.5708f,1.5708f,t,k);qc_line(&cv,cx+h*.1f,cy+r,cx-h*.45f,cy+r,t,k);qc_line(&cv,cx+h*.1f,cy-r,cx-h*.3f,cy-r,t,k);
        float p[]={cx-h*.85f,cy-r,cx-h*.3f,cy-r-h*.38f,cx-h*.3f,cy-r+h*.38f};qc_poly(&cv,p,3,k);break;}
    case I_EJECT:{float p[]={cx-h*.8f,cy+h*.1f,cx,cy-h*.75f,cx+h*.8f,cy+h*.1f};qc_poly(&cv,p,3,k);qc_rrect(&cv,cx-h*.8f,cy+h*.38f,h*1.6f,h*.3f,1,k);break;}
    case I_CROSS:qc_line(&cv,cx-h*.6f,cy-h*.6f,cx+h*.6f,cy+h*.6f,t,k);qc_line(&cv,cx+h*.6f,cy-h*.6f,cx-h*.6f,cy+h*.6f,t,k);break;
    case I_CLOUD:qc_circle(&cv,cx-h*.35f,cy+h*.1f,h*.42f,k);qc_circle(&cv,cx+h*.1f,cy-h*.12f,h*.55f,k);qc_circle(&cv,cx+h*.5f,cy+h*.15f,h*.38f,k);
        qc_rrect(&cv,cx-h*.75f,cy+h*.1f,h*1.6f,h*.42f,h*.2f,k);break;
    }
}

/* ---- widgets ---- */
/* a button; style 0 ghost, 1 filled accent, 2 small ghost */
static bool button(int id,float x,float y,float w,float h,const char *label,int font,int style,const char *tip){
    hit(id,(int)x,(int)y,(int)w,(int)h);say_help(id,tip);
    const bool hot=is_hover(id)||is_focus(id);
    if(style==1){rrect(x,y,w,h,12,ACCENT);if(hot)rline(x-4,y-4,w+8,h+8,15,3,TEXT);}
    else{rrect(x,y,w,h,style==2?10:12,hot?HOVER:WELL);rline(x,y,w,h,style==2?10:12,2,hot?ACCENT:LINE);}
    if(drawing())qc_text_fit(&cv,font,x+8,mid(font,y,h),w-16,label,style==1?INK:hot?ACCENT:TEXT,1);
    return clicked(id);
}
static bool step_button(int id,float x,float y,float w,float h,bool left){
    hit(id,(int)x,(int)y,(int)w,(int)h);
    const bool hot=is_hover(id)||(pressed==id);
    rrect(x,y,w,h,w>45?14:10,WELL);rline(x,y,w,h,w>45?14:10,2,hot?ACCENT:LINE);
    icon(left?I_LEFT:I_RIGHT,x+w/2,y+h/2,h*.5f,hot?ACCENT:TEXT);
    return clicked(id);
}
/* an item: icon, title, subtitle; style 0 normal, 1 gold, 2 danger */
static bool item(int id,float x,float y,float w,float h,int ic,const char *title,const char *sub,const char *tip,int style){
    hit(id,(int)x,(int)y,(int)w,(int)h);focusable(id,F_BUTTON,(int)(x+w/2));say_help(id,tip);
    const bool hot=is_hover(id)||is_focus(id);
    rrect(x,y,w,h,14,hot?HOVER:style==1?GOLD:ITEM);rline(x,y,w,h,14,2,hot?ACCENT:style==1?GOLDLINE:style==2?DANGER:LINE);
    QColor ink=style==1?ACCENT:TEXT,ic_ink=style==1?ACCENT:style==2?WARN:EDGE;
    icon(ic,x+16+17,y+h/2,24,ic_ink);
    if(h<60){text_fit(UI_B22,x+64,mid(UI_B22,y,h),w-80,title,ink,0);}
    else{text_fit(UI_B22,x+64,y+h/2-3,w-80,title,ink,0);text_fit(UI_M15,x+64,y+h/2+18,w-80,sub,MUTED,0);}
    return clicked(id);
}
static void kbd(float *x,float y,const char *key,const char *what){
    if(!drawing()){*x+=width(UI_B16,key)+16+6+width(UI_M17,what)+14;return;}
    const int kw=width(UI_B16,key)+16;
    qc_rrect(&cv,*x,y-19,kw,26,7,KBD);qc_rrect_line(&cv,*x,y-19,kw,26,7,1,KBDLINE);qc_text(&cv,UI_B16,*x+8,y,key,TEXT,0);
    *x+=kw+6;*x+=qc_text(&cv,UI_M17,*x,y,what,MUTED,0)+14;
}
static float kbd_width(const char *const *k,int n){float x=0;const int keep=pass;pass=PASS_LAYOUT;for(int i=0;i<n;i++)kbd(&x,0,k[i*2],k[i*2+1]);pass=keep;return x-14;}

/* a box with a small spaced title; returns the y below the title */
static float box(float x,float y,float w,float h,const char *title,QColor title_ink,QColor border){
    rrect(x,y,w,h,16,WELL);rline(x,y,w,h,16,2,border);
    text_track(UI_M15,x+20,y+14+16,title,title_ink,2);
    return y+14+24;
}
static void kv(float x,float y,float w,const char *label,const char *value,QColor ink){
    text(UI_M20,x,mid(UI_M20,y,38),label,MUTED);
    if(value&&drawing())qc_text_fit(&cv,UI_B20,x+w*.38f,mid(UI_B20,y,38),w*.62f,value,ink,2);
}
static void meter(float right,float y,int level,QColor on){
    for(int i=0;i<8;i++)rrect(right-8*11+i*11,y+10,8,18,2,i<level?on:RAIL);
}

/* ---- the header, tabs and footer ---- */
static const char *const KEYS_LASER[]={"TRIGGER","select","B","back","GRIPS","tabs"};
static const char *const KEYS_STICK[]={"STICK","move","A","select","B","back","GRIPS","tabs"};
static bool online_now(void){return V.state!=TC2V_OFFLINE;}
static void header(void){
    text_track(UI_B27,30,46,"PAUSED",TEXT,2);
    text_track(UI_M15,30,66,"TIME CRISIS II \xC2\xB7 VR",MUTED,3);
    /* the status, right-aligned */
    char status[64];QColor dot=DIM;
    const char *who=V.side==2?"P2 \xC2\xB7 ROBERT":"P1 \xC2\xB7 KEITH";
    switch(V.state){
    case TC2V_JOINING:snprintf(status,sizeof status,"JOINING %s\xE2\x80\xA6",V.joining);dot=ACCENT;break;
    case TC2V_ROOM_WAITING:snprintf(status,sizeof status,"ROOM %s \xC2\xB7 WAITING",V.code);dot=ACCENT;break;
    case TC2V_LAN_HOSTING:snprintf(status,sizeof status,"HOSTING \xC2\xB7 WAITING");dot=ACCENT;break;
    case TC2V_LINKED:snprintf(status,sizeof status,"%s \xC2\xB7 LINKED WITH %s",who,V.partner);dot=GOOD;break;
    case TC2V_RECONNECTING:snprintf(status,sizeof status,"%s DROPPED \xC2\xB7 RECONNECTING",V.partner);dot=WARN;break;
    default:snprintf(status,sizeof status,"%s \xC2\xB7 OFFLINE",who);
    }
    const int sw=width(UI_M17,status);
    const float sx=1092-(sw>380?380:sw);
    if(drawing()){qc_text_fit(&cv,UI_M17,sx,48,380,status,MUTED,0);qc_circle(&cv,sx-16,42,6,dot);qc_rrect(&cv,sx-38,28,2,26,1,LINE);}
    const float right=sx-50;
    if(!H->saved){
        const char *t="SAVING FAILED - settings not kept";const int w=width(UI_B16,t)+28;
        rrect(right-w,26,w,30,15,WARN);text(UI_B16,right-w+14,47,t,WARNINK);
    }else{
        const char *const *k=stick_mode?KEYS_STICK:KEYS_LASER;const int n=stick_mode?4:3;
        float x=right-kbd_width(k,n);for(int i=0;i<n;i++)kbd(&x,47,k[i*2],k[i*2+1]);
    }
}
static void tabs(void){
    static const char *const names[QMENU_TAB_COUNT]={"GUN","PLAYER","GRAPHICS","AUDIO","ONLINE"};
    rrect(3,BODY_Y-2,QMENU_W-6,2,0,LINE);
    if(drawing())qc_clip(&cv,0,0,QMENU_W,BODY_Y);
    float x=27;
    focusable(ID(K_TABS,0),F_TABS,QMENU_W/2);
    for(int i=0;i<QMENU_TAB_COUNT;i++){
        const int id=ID(K_TAB,i),w=width(UI_B24,names[i])+4*1+60;
        hit(id,(int)x,78,w,55);
        const bool active=tab==i,hot=is_hover(id)||(is_focus(ID(K_TABS,0))&&active);
        if(active){rrect(x,78,w,80,14,BODY);rline(x,78,w,80,14,2,LINE);rrect(x+2,BODY_Y-2,w-4,2,0,BODY);}
        else if(hot)rrect(x,78,w,80,14,HOVER);
        if(is_focus(ID(K_TABS,0))&&active)rline(x+1,79,w-2,80,13,3,ACCENT);
        text_track(UI_B24,x+30,mid(UI_B24,78,55),names[i],active?ACCENT:hot?TEXT:MUTED,1);
        /* a dot: something on that tab wants attention */
        QColor dot={0};
        if(i==QMENU_TAB_PLAYER&&H->get(QOPT_PLAYER)!=H->boot_player&&!online_now())dot=ACCENT;
        if(i==QMENU_TAB_GRAPHICS&&(H->get(QOPT_RESOLUTION)!=H->boot_resolution||H->preset_unsupported))dot=ACCENT;
        if(i==QMENU_TAB_ONLINE)dot=V.state==TC2V_LINKED?GOOD:V.state==TC2V_RECONNECTING?WARN:(online_now()||V.can_rejoin)?ACCENT:dot;
        if(dot.a&&drawing())qc_circle(&cv,x+30+qc_text_width(UI_B24,names[i],1)+11,mid(UI_B24,78,55)-9,5,dot);
        if(clicked(id)){tab=i;screen=S_MAIN;}
        x+=w+8;
    }
    if(drawing())qc_unclip(&cv);
    next_row();
}
static void footer(void){
    rrect(3,FOOT_Y,QMENU_W-6,2,0,LINE);
    if(drawing())qc_text_fit(&cv,UI_M17,27,FOOT_Y+30,CW,help,MUTED,0);
    const bool leave=online_now(),pending=H->get(QOPT_PLAYER)!=H->boot_player||H->get(QOPT_RESOLUTION)!=H->boot_resolution;
    const char *labels[4]={"RESUME",leave?"RESTART & LEAVE":"RESTART GAME","RECENTER","RESTART APP"};
    const char *tips[4]={"Back to the game. The menu button does the same.",
        leave?"Leaves the online game and restarts your arcade. REJOIN brings you back while the room is open.":"Back to the intro in about 10 s. Your coins are paid back in; settings stay.",
        "Sets where the cabinet is. Stand upright: it also calibrates ducking.",
        pending?"A Player or Resolution change is waiting for this.":"Closes and reopens the app. Needed for Player and Resolution changes."};
    const int icons[4]={I_PLAY,I_RESTART,I_RECENTER,I_APP};
    const float y=FOOT_Y+46,h=QMENU_H-3-16-y,unit=(CW+4-36)/4.1f;
    float x=27;
    for(int i=0;i<4;i++){
        const int id=ID(K_FOOT,i);const float w=i==0?unit*1.1f:unit;
        hit(id,(int)x,(int)y,(int)w,(int)h);focusable(id,F_BUTTON,(int)(x+w/2));say_help(id,tips[i]);
        const bool hot=is_hover(id)||is_focus(id),gold=i==0,amber=i==3&&pending;
        rrect(x,y,w,h,14,hot?HOVER:gold?GOLD:ITEM);rline(x,y,w,h,14,2,hot||amber?ACCENT:gold?GOLDLINE:LINE);
        if(is_focus(id))rline(x-1,y-1,w+2,h+2,15,3,ACCENT);
        if(amber&&drawing())qc_circle(&cv,x+w-16,y+14,5,ACCENT);
        const int tw=width(UI_B22,labels[i]);const float tx=x+(w-tw-36)/2;
        icon(icons[i],tx+12,y+h/2,24,gold||amber?ACCENT:EDGE);
        text(UI_B22,tx+36,mid(UI_B22,y,h),labels[i],gold||amber?ACCENT:TEXT);
        if(clicked(id)){
            if(i==0)H->action(QACT_RESUME);
            else if(i==1)modal=leave?M_RESTART_LEAVE:M_RESTART;
            else if(i==2)H->action(QACT_RECENTER);
            else modal=M_APP;
            if(modal)focus=ID(K_MBTN,1);
        }
        x+=w+12;
    }
    next_row();
}

/* ---- settings tabs ---- */
static void value_text(const RowDef *r,int v,char *out,size_t n){snprintf(out,n,r->sign&&v>0?"+%d%s":"%d%s",v,r->unit);}
static void settings_row(const RowDef *r,float y,float h){
    const int row=ID(K_ROW,r->opt);
    const bool locked=r->opt==QOPT_PLAYER&&online_now();
    int v=locked?V.side-1:H->get(r->opt);
    const RowDef *d=r;
    hit(row,CX0,(int)y,CW,(int)h);focusable(row,d->type==T_SLIDER?F_SLIDER:F_SEG,CX0+CW/2);
    char tip[200];snprintf(tip,sizeof tip,"%s",locked?"Online, the room decides the cabinet: the host plays P1 Keith, the joiner P2 Robert. Your own side comes back when you leave.":r->help);
    say_help(row,tip);
    const bool hot=pointer_in(CX0,y,CW,h)||is_focus(row);
    if(hot)rrect(CX0,y,CW,h,14,HOVER);
    if(is_focus(row))rline(CX0,y,CW,h,14,2,ACCENT);
    /* the label, its line and a chip */
    char sub[160];snprintf(sub,sizeof sub,"%s",r->sub);QColor sub_ink=MUTED;
    if(r->opt==QOPT_COVER)snprintf(sub,sizeof sub,"%s",v?"Crouch to hide; RECENTER standing up":"Hold a grip to leave cover");
    if(r->opt==QOPT_RESOLUTION){
        if(H->preset_unsupported){snprintf(sub,sizeof sub,"Preset not supported by this headset: using the default");sub_ink=WARN;}
        else if(!H->active_w)snprintf(sub,sizeof sub,"Per eye \xC2\xB7 desktop window now");
        else snprintf(sub,sizeof sub,"Per eye \xC2\xB7 running %u \xC3\x97 %u",H->active_w,H->active_h);
    }
    if(locked)snprintf(sub,sizeof sub,"Online: host is P1, joiner is P2%s",V.home_side==2&&V.side==1?" \xC2\xB7 your P2 returns when you leave":"");
    const float ly=y+(h-54)/2;
    const int lw=text(UI_B27,CX0+18,ly+25,r->label,TEXT);
    const bool restart=!locked&&((r->opt==QOPT_PLAYER&&v!=H->boot_player)||(r->opt==QOPT_RESOLUTION&&v!=H->boot_resolution));
    if(restart){const int cw=width(UI_B14,"AFTER RESTART")+20;rrect(CX0+18+lw+10,ly+6,cw,22,11,ACCENT);text(UI_B14,CX0+18+lw+20,ly+22,"AFTER RESTART",INK);}
    /* the control, right-aligned before the reset (or lock) */
    const float right=CX1-18-44-10;
    float widths[3],total=8+(d->nopts-1)*4;
    for(int i=0;i<d->nopts;i++){widths[i]=width(UI_B20,d->opts[i])+36;if(widths[i]<112)widths[i]=112;total+=widths[i];}
    const float control=d->type==T_SLIDER?550:d->type==T_RES?600:total;
    text_fit(UI_M15,CX0+18,ly+52,right-control-20-CX0-18,sub,sub_ink,0);
    if(d->type==T_SEG){
        float x=right-total;const float sy=y+(h-50)/2;
        rrect(x,sy,total,50,14,WELL);rline(x,sy,total,50,14,2,LINE);
        x+=4;
        for(int i=0;i<d->nopts;i++){
            const int id=ID(K_SEG,r->opt*8+i);
            if(!locked)hit(id,(int)x,(int)sy+4,(int)widths[i],42);
            const bool on=i==v,h2=!locked&&is_hover(id);
            if(on)rrect(x,sy+4,widths[i],42,10,locked?(QColor){150,122,72,255}:ACCENT);else if(h2)rrect(x,sy+4,widths[i],42,10,HOVER);
            text_fit(UI_B20,x,mid(UI_B20,sy+4,42),widths[i],d->opts[i],on?INK:h2?TEXT:locked?DIM:MUTED,1);
            if(!locked&&clicked(id))H->set(r->opt,i);
            x+=widths[i]+4;
        }
        if(!locked&&pass==PASS_ACT&&ev_adjust==row)H->set(r->opt,(v+ev_adjust_dir+d->nopts)%d->nopts);
        if(!locked&&stick_mode&&clicked(row))H->set(r->opt,(v+1)%d->nopts);
    }else if(d->type==T_SLIDER){
        const float sy=y+(h-52)/2;float x=right-550;
        const int minus=ID(K_STEP,r->opt*2),plus=ID(K_STEP,r->opt*2+1),track=ID(K_TRACK,r->opt);
        if(step_button(minus,x,sy,52,52,true))H->set(r->opt,v-d->step);
        const float TW=284;   /* the track; the value takes the rest of the 550 */
        hit(track,(int)x+62,(int)sy,(int)TW,52);
        const float p=(float)(v-d->min)/(d->max-d->min),z=(float)(d->def-d->min)/(d->max-d->min);
        rrect(x+62,sy+22,TW,8,4,RAIL);
        rrect(x+62+TW*fminf(p,z),sy+22,TW*fabsf(p-z),8,4,EDGE);
        rrect(x+62+TW*z-1,sy+14,2,24,1,ZERO);
        if(drawing()){qc_circle(&cv,x+62+TW*p,sy+26,16,EDGE);qc_circle(&cv,x+62+TW*p,sy+26,12,(is_hover(track)||pressed==track)?ACCENT:TEXT);}
        if(pass==PASS_ACT&&ev_drag==track){
            float f=(ev_drag_x-(x+62))/TW;if(f<0)f=0;if(f>1)f=1;
            int nv=(int)lroundf((d->min+f*(d->max-d->min))/d->step)*d->step;H->set(r->opt,nv);
        }
        if(step_button(plus,x+62+TW+10,sy,52,52,false))H->set(r->opt,v+d->step);
        char val[24];value_text(d,v,val,sizeof val);text_fit(UI_B27,x+62+TW+70,mid(UI_B27,sy,52),550-(62+TW+70),val,TEXT,2);
        if(pass==PASS_ACT&&ev_adjust==row)H->set(r->opt,v+ev_adjust_dir*d->step);
    }else{   /* the resolution presets: three by two */
        const float gx=right-600,gy=y+(h-96)/2;
        for(int i=0;i<QRESOLUTION_COUNT;i++){
            const int id=ID(K_RES,i);const float bx=gx+(i%3)*204,by=gy+(i/3)*52;
            hit(id,(int)bx,(int)by,196,44);
            const bool on=i==v,h2=is_hover(id);
            rrect(bx,by,196,44,12,on?ACCENT:WELL);if(!on)rline(bx,by,196,44,12,2,h2?MUTED:LINE);
            if(i==H->boot_resolution)rrect(bx+2,by+40,192,4,2,GOOD);
            char label[32];if(i)snprintf(label,sizeof label,"%u \xC3\x97 %u",qresolution_sizes[i][0],qresolution_sizes[i][1]);else snprintf(label,sizeof label,"DEFAULT");
            text_fit(UI_B20,bx,mid(UI_B20,by,44),196,label,on?INK:h2?TEXT:MUTED,1);
            if(clicked(id))H->set(r->opt,i);
        }
        if(pass==PASS_ACT&&ev_adjust==row)H->set(r->opt,(v+ev_adjust_dir+QRESOLUTION_COUNT)%QRESOLUTION_COUNT);
        if(stick_mode&&clicked(row))H->set(r->opt,(v+1)%QRESOLUTION_COUNT);
    }
    /* reset to the default, or the lock */
    const float bx=CX1-18-44,by=y+(h-44)/2;
    if(locked)icon(I_LOCK,bx+22,by+22,22,DIM);
    else if(v!=d->def){
        const int id=ID(K_RESET,r->opt);hit(id,(int)bx,(int)by,44,44);
        const bool h2=is_hover(id);rline(bx,by,44,44,12,2,h2?ACCENT:LINE);icon(I_RESET,bx+22,by+22,22,h2?ACCENT:MUTED);
        if(clicked(id))H->set(r->opt,d->def);
    }
    next_row();
}
static void settings(const RowDef *rows,int n){
    float y=CY0;
    for(int i=0;i<n;i++){const float h=rows[i].type==T_RES?108:74;settings_row(&rows[i],y,h);y+=h+4;}
}

/* ---- the ONLINE tab ---- */
static void on_item(int act,float x,float *y,int ic,const char *title,const char *sub,const char *tip,int style){
    const int id=ID(K_ON,act);
    if(item(id,x,*y,COLW,68,ic,title,sub,tip,style)){
        switch(act){
        case ON_HOST:tc2_online_host_room();break;
        case ON_CODE:modal=M_CODE;kbuf[0]=0;focus=ID(K_KEY,0);break;
        case ON_BROWSE:screen=S_BROWSE;page=0;tc2_online_browse(true);break;
        case ON_QUICK:tc2_online_quick();break;
        case ON_LAN:screen=S_LAN;tc2_online_browse(true);break;
        case ON_REJOIN:tc2_online_rejoin();break;
        case ON_LEAVE:tc2_online_leave();break;
        case ON_HOSTLAN:tc2_online_host_lan();screen=S_MAIN;break;
        }
    }
    next_row();*y+=76;
}
/* a row inside a box: its label, then a choice of n at the right; returns the option picked this frame, or -1 */
static int box_choice(int kind,float x,float cy,const char *label,const char *const *opts,int n,int sel,const char *tip){
    text(UI_M20,x+20,mid(UI_M20,cy,38),label,MUTED);
    const int row=ID(kind,9);hit(row,(int)x,(int)cy,(int)COLW,38);focusable(row,F_SEG,(int)(x+COLW/2));say_help(row,tip);
    float w[4],total=6;for(int i=0;i<n;i++){w[i]=width(UI_B16,opts[i])+28;if(w[i]<72)w[i]=72;total+=w[i];}
    const float sx=x+COLW-20-total;rrect(sx,cy+3,total,32,10,WELL);rline(sx,cy+3,total,32,10,2,LINE);
    int picked=-1;float ox=sx+3;
    for(int i=0;i<n;i++){
        const int id=ID(kind,i);hit(id,(int)ox,(int)cy+6,(int)w[i],26);
        if(i==sel)rrect(ox,cy+6,w[i],26,8,ACCENT);else if(is_hover(id))rrect(ox,cy+6,w[i],26,8,HOVER);
        text_fit(UI_B16,ox,mid(UI_B16,cy+6,26),w[i],opts[i],i==sel?INK:TEXT,1);
        if(clicked(id))picked=i;
        ox+=w[i];
    }
    if(is_focus(row))rline(x+8,cy,COLW-16,38,10,2,ACCENT);
    if(pass==PASS_ACT&&ev_adjust==row)picked=(sel+ev_adjust_dir+n)%n;
    if(pass==PASS_ACT&&stick_mode&&ev_click==row)picked=(sel+1)%n;
    next_row();
    return picked;
}
static float voice_box(float x,float y,const char *title){
    const bool on=V.voice,partner=V.state==TC2V_LINKED||V.state==TC2V_RECONNECTING;
    const int rows=on?(partner?4:3):1,notes=(on&&V.mic_denied)?1:(!on?1:0);
    const float h=52+rows*38+notes*23;
    float cy=box(x,y,COLW,h,title,MUTED,LINE);
    const char *key=H&&H->talk_key?H->talk_key:"STICK CLICK";
    /* open mic / push to talk / off */
    {static const char *const modes[]={"OPEN MIC","PUSH TO TALK","OFF"};
     char tip[200];snprintf(tip,sizeof tip,"OPEN MIC sends when you speak. PUSH TO TALK sends while you hold %s%s. Echo and noise are filtered.",
                            key,strcmp(key,"V")?" (V on a PC)":"");
     const int mode=!on?2:V.push_to_talk?1:0;
     const int m=box_choice(K_VOICE,x,cy,"Voice chat",modes,3,mode,tip);
     if(m>=0&&m!=mode){if(m==2)tc2_online_set_voice(false);else{if(m==1)tc2_online_set_push_to_talk(true);else tc2_online_set_push_to_talk(false);if(!on)tc2_online_set_voice(true);}}}
    cy+=38;
    if(on){
        text(UI_M20,x+20,mid(UI_M20,cy,38),"You",MUTED);
        const float r=x+COLW-20;
        if(V.mic_denied)text_fit(UI_B20,r-200,mid(UI_B20,cy,38),200,"NO MIC",WARN,2);
        else if(V.push_to_talk){
            char hold[48];snprintf(hold,sizeof hold,V.talking?"TALKING":"hold %s",key);
            text_fit(V.talking?UI_B20:UI_M17,r-108-230,mid(V.talking?UI_B20:UI_M17,cy,38),220,hold,V.talking?GOOD:MUTED,2);
            meter(r,cy,V.talking?V.level:0,GOOD);
        }else{
            const int id=ID(K_ON,ON_MUTE);const char *l=V.muted?"UNMUTE":"MUTE";
            if(button(id,r-96,cy+4,96,30,l,UI_B16,V.muted?1:2,"Mute or unmute your microphone. In game: left stick click."))tc2_online_toggle_mute();
            focusable(id,F_BUTTON,(int)(r-48));next_row();
            if(V.muted)text_fit(UI_B20,r-300,mid(UI_B20,cy,38),190,"MUTED",WARN,2);else meter(r-108,cy,V.level,GOOD);
        }
        cy+=38;
        if(partner){
            text(UI_M20,x+20,mid(UI_M20,cy,38),"Partner",MUTED);
            if(V.state==TC2V_LINKED&&!V.partner_voice)text_fit(UI_M17,x+COLW-320,mid(UI_M17,cy,38),300,"their game has no voice chat",MUTED,2);
            else meter(x+COLW-20,cy,V.partner_level,GOOD);
            cy+=38;
        }
        const int row=ID(K_VOLROW,0);
        text(UI_M20,x+20,mid(UI_M20,cy,38),"Partner volume",MUTED);
        hit(row,(int)x,(int)cy,(int)COLW-170,38);focusable(row,F_SLIDER,(int)(x+COLW/2));say_help(row,"How loud your partner is.");
        if(is_focus(row))rline(x+8,cy,COLW-16,38,10,2,ACCENT);
        const float sx=x+COLW-20-152;
        if(step_button(ID(K_VOL,0),sx,cy+1,40,36,true))tc2_online_set_volume(V.volume-10);
        char vol[12];snprintf(vol,sizeof vol,"%d%%",V.volume);text_fit(UI_B20,sx+40,mid(UI_B20,cy,38),72,vol,TEXT,1);
        if(step_button(ID(K_VOL,1),sx+112,cy+1,40,36,false))tc2_online_set_volume(V.volume+10);
        if(pass==PASS_ACT&&ev_adjust==row)tc2_online_set_volume(V.volume+ev_adjust_dir*10);
        next_row();
        cy+=38;
        if(V.mic_denied)para(UI_M15,x+20,cy+16,COLW-40,21,"Allow the microphone: Quest Settings > Apps > Permissions",WARN,1);
    }else para(UI_M15,x+20,cy+16,COLW-40,21,"Voice is off. Your partner can't hear you.",MUTED,1);
    return y+h+8;
}
static void online_offline(void){
    float y=CY0;
    if(V.can_rejoin){
        char t[48],s[80];snprintf(t,sizeof t,"REJOIN %s",V.rejoin_code);
        snprintf(s,sizeof s,"%s%s room, still open \xC2\xB7 you were P%d",V.rejoin_partner,V.rejoin_partner[0]?"'s":"The",V.rejoin_side);
        if(!V.rejoin_partner[0])snprintf(s,sizeof s,"The room is still open \xC2\xB7 you were P%d",V.rejoin_side);
        on_item(ON_REJOIN,CX0,&y,I_REJOIN,t,s,V.rejoin_side==2?"Back into the room as P2: your arcade restarts once, then you join their game.":
            "Back into the room as P1. If they are mid-game you start a new one, and they join yours after theirs.",1);
    }
    on_item(ON_HOST,CX0,&y,I_HOUSE,"HOST A ROOM","Get a code to share \xC2\xB7 you play P1","Opens a room on the lobby server with a 4-letter code for your friend. The host plays P1 Keith.",0);
    on_item(ON_CODE,CX0,&y,I_KEYS,"JOIN WITH CODE","Your friend's 4-letter code \xC2\xB7 you play P2","Opens a keyboard for the code. The joiner plays P2 Robert.",0);
    on_item(ON_BROWSE,CX0,&y,I_LIST,"BROWSE GAMES","Open rooms and local-network games","Lists open rooms on the server and games on your network.",0);
    on_item(ON_QUICK,CX0,&y,I_BOLT,"QUICK MATCH","Play with anyone","Joins the fullest open room on the server, or opens one.",0);
    if(!V.can_rejoin)on_item(ON_LAN,CX0,&y,I_SWAP,"LOCAL NETWORK\xE2\x80\xA6","Host here, or join by IP","Host or join on your own network, without the lobby server.",0);
    /* right column: you, voice */
    frow=1;   /* the right column's rows sit beside the left's: up / down walks each column */
    float ry=CY0;const bool p2=V.home_side==2;
    const float h=52+3*38+(p2?23:0);
    float cy=box(COL2,ry,COLW,h,p2?"YOU / SOLO P2 ROBERT":"YOU / SOLO P1 KEITH",MUTED,LINE);
    text(UI_M20,COL2+20,mid(UI_M20,cy,38),"Name",MUTED);
    {const int id=ID(K_ON,ON_NAME);const float bx=COL2+COLW-20-96;
     if(drawing())qc_text_fit(&cv,UI_B20,bx-260,mid(UI_B20,cy,38),250,V.name,TEXT,2);
     if(button(id,bx,cy+4,96,30,"CHANGE",UI_B16,2,"Your name in the lobby and on your partner's menu.")){modal=M_NAME;snprintf(kbuf,sizeof kbuf,"%s",V.name);focus=ID(K_KEY,0);}
     focusable(id,F_BUTTON,(int)bx+48);next_row();}
    text(UI_M20,COL2+20,mid(UI_M20,cy+38,38),"Server",MUTED);
    {const int id=ID(K_ON,ON_SERVER);const float bx=COL2+COLW-20-96;
     text_fit(UI_M17,COL2+100,mid(UI_M17,cy+38,38),COLW-226,V.server,TEXT,2);
     if(button(id,bx,cy+42,96,30,"CHANGE",UI_B16,2,"Choose the lobby server. Both players must use the same server. Changes apply immediately.")){
         modal=M_SERVER;snprintf(kbuf,sizeof kbuf,"%s",V.server);kerror=NULL;klower=true;focus=ID(K_KEY,0);
     }
     focusable(id,F_BUTTON,(int)bx+48);next_row();}
    {static const char *const onoff[]={"ON","OFF"};
     const int c=box_choice(K_CREDIT,COL2,cy+76,"Join with a credit",onoff,2,V.join_credit?0:1,
                            "Joining a game as P2, one credit goes in for you. Continues still take your own coins.");
     if(c>=0)tc2_online_set_join_credit(c==0);}
    if(p2)para(UI_M15,COL2+20,cy+114+16,COLW-40,21,"Hosting makes you P1 for the session.",ACCENT,1);
    ry+=h+8;
    voice_box(COL2,ry,"VOICE CHAT");
}
static void online_browse(void){
    /* the bar: back, title, pager, refresh */
    if(button(ID(K_ON,ON_BACK),CX0,CY0+8,104,34,"BACK",UI_B16,2,"Back to the ONLINE menu. B does the same.")){screen=S_MAIN;tc2_online_browse(false);}
    focusable(ID(K_ON,ON_BACK),F_BUTTON,CX0+52);
    icon(I_LEFT,CX0+16,CY0+25,14,TEXT);
    int rooms=0;for(int i=0;i<game_count;i++)rooms+=!games[i].lan;
    char title[120];
    if(V.browse_answered)snprintf(title,sizeof title,"%s \xC2\xB7 %d rooms \xC2\xB7 %d online \xC2\xB7 %d on your network",V.server_name[0]?V.server_name:V.server,rooms,V.server_players,game_count-rooms);
    else snprintf(title,sizeof title,V.browse_busy?"Asking %s\xE2\x80\xA6":"%s does not answer \xC2\xB7 %d on your network",V.server,game_count-rooms);
    const int pages=game_count>0?(game_count+4)/5:1;if(page>=pages)page=pages-1;if(page<0)page=0;
    text_fit(UI_M17,CX0+118,CY0+31,CW-118-310,title,MUTED,0);
    char pg[16];snprintf(pg,sizeof pg,"%d / %d",page+1,pages);
    const float px=CX1-140-12-150;
    if(step_button(ID(K_ON,ON_PREV),px,CY0+6,40,36,true))page=(page+pages-1)%pages;
    text_fit(UI_B20,px+40,CY0+31,70,pg,TEXT,1);
    if(step_button(ID(K_ON,ON_NEXT),px+110,CY0+6,40,36,false))page=(page+1)%pages;
    {focusable(ID(K_ON,ON_NEXT),F_SLIDER,(int)px+75);
     say_help(ID(K_ON,ON_NEXT),"Pages of five. Stick left / right turns the page.");
     if(pass==PASS_ACT&&ev_adjust==ID(K_ON,ON_NEXT))page=(page+ev_adjust_dir+pages)%pages;}
    if(button(ID(K_ON,ON_REFRESH),CX1-140,CY0+8,140,34,"REFRESH",UI_B16,2,"Asks the server again."))tc2_online_refresh();
    focusable(ID(K_ON,ON_REFRESH),F_BUTTON,CX1-70);
    next_row();
    float y=CY0+58;
    if(!game_count){para(UI_M20,CX0+20,y+40,CW-40,26,V.browse_busy||V.discovering?"Looking for games\xE2\x80\xA6":"No games yet - host one!",MUTED,1);return;}
    for(int i=page*5;i<game_count&&i<page*5+5;i++){
        const tc2_online_game *g=&games[i];const int id=ID(K_GAME,i);const bool ok=g->status==TC2G_JOIN;
        hit(id,CX0,(int)y,CW,54);
        if(ok){focusable(id,F_BUTTON,CX0+CW/2);say_help(id,"Join this game. You play P2 Robert.");}
        else say_help(id,g->status==TC2G_OLD?"That room runs another version of the game.":g->status==TC2G_FULL?"That room is full.":"They are mid-game. Try again after it.");
        const bool hot=ok&&(is_hover(id)||is_focus(id));
        rrect(CX0,y,CW,54,12,hot?HOVER:ITEM);rline(CX0,y,CW,54,12,2,hot?ACCENT:LINE);
        const QColor ink=ok?TEXT:DIM;
        icon(g->lan?I_HOUSE:I_CLOUD,CX0+30,y+27,20,ok?EDGE:DIM);
        const int nw=text(UI_B22,CX0+56,mid(UI_B22,y,54),g->name,ink);
        if(g->detail[0])text_fit(UI_M15,CX0+56+nw+12,mid(UI_M15,y,54),CW-56-nw-12-230,g->detail,MUTED,0);
        char pl[12];snprintf(pl,sizeof pl,"%d/%d",g->players,g->max_players);text_fit(UI_M20,CX1-130-90,mid(UI_M20,y,54),70,pl,MUTED,2);
        static const char *const st[]={"JOIN","PLAYING","FULL","OLD"};
        const QColor bg=g->status==TC2G_JOIN?GOOD:g->status==TC2G_OLD?WARNBG:RAIL,fg=g->status==TC2G_JOIN?GOODINK:g->status==TC2G_OLD?WARN:MUTED;
        rrect(CX1-16-110,y+14,110,26,13,bg);text_fit(UI_B16,CX1-16-110,mid(UI_B16,y+14,26),110,st[g->status],fg,1);
        if(ok&&clicked(id)){tc2_online_join_game(i);screen=S_MAIN;tc2_online_browse(false);}
        if(ok)next_row();
        y+=60;
    }
}
static void online_lan(void){
    if(button(ID(K_ON,ON_BACK),CX0,CY0+8,104,34,"BACK",UI_B16,2,"Back to the ONLINE menu. B does the same.")){screen=S_MAIN;tc2_online_browse(false);}
    focusable(ID(K_ON,ON_BACK),F_BUTTON,CX0+52);icon(I_LEFT,CX0+16,CY0+25,14,TEXT);
    text(UI_B20,CX0+118,CY0+32,"LOCAL NETWORK",TEXT);text(UI_M17,CX0+118+width(UI_B20,"LOCAL NETWORK")+10,CY0+32,"\xC2\xB7 no lobby server needed",MUTED);
    next_row();
    float y=CY0+58;
    on_item(ON_HOSTLAN,CX0,&y,I_HOUSE,"HOST ON THIS NETWORK","Friends on your Wi-Fi or VPN join you","Hosts the game on this headset. Others on your network find it here.",0);
    /* found games */
    int lan[3],n=0;for(int i=0;i<game_count&&n<3;i++)if(games[i].lan)lan[n++]=i;
    const float bh=52+(n?n*48+(n-1)*6:0)+23;
    float cy=box(CX0,y,COLW,bh,"FOUND ON YOUR NETWORK",MUTED,LINE);
    for(int k=0;k<n;k++){
        const tc2_online_game *g=&games[lan[k]];const int id=ID(K_GAME,lan[k]);const float ry=cy+k*54;
        hit(id,CX0+18,(int)ry,COLW-36,48);focusable(id,F_BUTTON,CX0+COLW/2);say_help(id,"Join this game. You play P2 Robert.");
        const bool hot=is_hover(id)||is_focus(id);
        rrect(CX0+18,ry,COLW-36,48,12,hot?HOVER:ITEM);rline(CX0+18,ry,COLW-36,48,12,2,hot?ACCENT:LINE);
        icon(I_HOUSE,CX0+44,ry+24,18,EDGE);
        const int nw=text(UI_B22,CX0+66,mid(UI_B22,ry,48),g->name,TEXT);
        text_fit(UI_M15,CX0+66+nw+10,mid(UI_M15,ry,48),COLW-36-66-nw-10-110,g->detail,MUTED,0);
        rrect(CX0+COLW-18-16-80,ry+12,80,24,12,GOOD);text_fit(UI_B16,CX0+COLW-18-16-80,mid(UI_B16,ry+12,24),80,"JOIN",GOODINK,1);
        if(clicked(id)){tc2_online_join_game(lan[k]);screen=S_MAIN;tc2_online_browse(false);}
        next_row();
    }
    para(UI_M15,CX0+20,cy+(n?n*54:0)+14,COLW-40,21,V.discovering?"Searching\xE2\x80\xA6":n?"":"Nobody found yet. Searching again every few seconds.",MUTED,1);
    /* join by address */
    frow=1;
    const float h=52+52+46+54;
    float iy=box(COL2,CY0+58,COLW,h,"JOIN BY IP",MUTED,LINE);
    {char a[40];snprintf(a,sizeof a,"%s",V.join_ip);const int w=width(UI_B44,a),pw=width(UI_M20,":27750");
     text(UI_B44,COL2+(COLW-w-pw-6)/2,iy+42,a,TEXT);text(UI_M20,COL2+(COLW-w-pw-6)/2+w+6,iy+42,":27750",MUTED);}
    para(UI_M15,COL2+20,iy+52+18,COLW-40,21,"For a friend over Tailscale / ZeroTier, or one who doesn't show up on the left.",MUTED,2);
    const float by=iy+52+46+10;
    if(button(ID(K_ON,ON_IPEDIT),COL2+20,by,200,44,"EDIT ADDRESS",UI_B20,0,"Opens a number pad for the address.")){modal=M_IP;snprintf(kbuf,sizeof kbuf,"%s",V.join_ip);focus=ID(K_KEY,0);}
    focusable(ID(K_ON,ON_IPEDIT),F_BUTTON,COL2+120);
    if(button(ID(K_ON,ON_JOINIP),COL2+232,by,110,44,"JOIN",UI_B20,1,"Connects to that address. You play P2 Robert.")){tc2_online_join_ip();screen=S_MAIN;tc2_online_browse(false);}
    focusable(ID(K_ON,ON_JOINIP),F_BUTTON,COL2+287);
    next_row();
}
static void players_box(float x,float y){
    float cy=box(x,y,COLW,52+2*38,"PLAYERS",MUTED,LINE);
    for(int i=0;i<2;i++){
        char who[40];snprintf(who,sizeof who,"%s%s",V.players[i],V.player_self[i]?" (you)":"");
        text(UI_M20,x+20,mid(UI_M20,cy+i*38,38),i?"P2 \xC2\xB7 ROBERT":"P1 \xC2\xB7 KEITH",MUTED);
        if(V.players[i][0])text_fit(UI_B20,x+200,mid(UI_B20,cy+i*38,38),COLW-220,who,TEXT,2);
        else text_fit(UI_M20,x+200,mid(UI_M20,cy+i*38,38),COLW-220,"waiting for a partner\xE2\x80\xA6",MUTED,2);
    }
}
static void online_session(void){
    float y=CY0;
    if(V.state==TC2V_ROOM_WAITING){
        float cy=box(CX0,y,COLW,52+84+46,V.opening?"OPENING YOUR ROOM\xE2\x80\xA6":"ROOM OPEN \xC2\xB7 YOUR CODE",V.opening?ACCENT:GOOD,LINE);
        {const int w=qc_text_width(UI_B76,V.code,22);text_track(UI_B76,CX0+(COLW-w)/2,cy+72,V.code,ACCENT,22);}
        para(UI_M15,CX0+20,cy+84+18,COLW-40,21,"Your friend: ONLINE, then JOIN WITH CODE, or find it in BROWSE GAMES.",MUTED,2);
        players_box(CX0,y+52+84+46+8);
    }else if(V.state==TC2V_LAN_HOSTING){
        const int rows=V.address_count>0?V.address_count:1;
        float cy=box(CX0,y,COLW,52+rows*38+46,"HOSTING ON THIS NETWORK \xC2\xB7 PORT 27750",GOOD,LINE);
        for(int i=0;i<rows;i++){char a[32];snprintf(a,sizeof a,"%s%s",V.address_count?V.address[i]:"no network",V.address_count&&V.address_vpn[i]?"  VPN":"");kv(CX0+20,cy+i*38,COLW-40,"Address",a,TEXT);}
        para(UI_M15,CX0+20,cy+rows*38+18,COLW-40,21,"Friends on your Wi-Fi find you in LOCAL NETWORK. Over a VPN they use JOIN BY IP.",MUTED,2);
        players_box(CX0,y+52+rows*38+46+8);
    }else if(V.state==TC2V_JOINING){
        float cy=box(CX0,y,COLW,52+70+23,"JOINING",ACCENT,LINE);
        {const char *what=V.joining;const int f=strlen(what)<=6?UI_B44:UI_B32;const int w=width(f,what);text(f,CX0+(COLW-w)/2,cy+52,what,TEXT);}
        para(UI_M15,CX0+20,cy+70+16,COLW-40,21,V.code[0]?"Looking for the room\xE2\x80\xA6":"Connecting\xE2\x80\xA6",ACCENT,1);
        float wy=y+52+70+23+8;
        float ny=box(CX0,wy,COLW,52+69,"WHAT HAPPENS NEXT",MUTED,LINE);
        para(UI_M15,CX0+20,ny+16,COLW-40,21,"You join as P2 Robert. Your arcade restarts once to shake hands with P1's, then you put coins in.",MUTED,3);
    }else{   /* linked, or the partner dropped */
        const bool rc=V.state==TC2V_RECONNECTING;
        float cy=box(CX0,y,COLW,52+4*38,rc?"PARTNER DROPPED \xC2\xB7 RECONNECTING":"LINKED",rc?WARN:GOOD,rc?WARNLINE:LINE);
        kv(CX0+20,cy,COLW-40,"You",V.side==2?"P2 \xC2\xB7 ROBERT":"P1 \xC2\xB7 KEITH",GOOD);
        char p[48];snprintf(p,sizeof p,"%s \xC2\xB7 %s",V.partner,V.side==2?"P1 KEITH":"P2 ROBERT");kv(CX0+20,cy+38,COLW-40,"Partner",p,TEXT);
        char ping[48];
        if(rc)snprintf(ping,sizeof ping,"%d:%02d \xC2\xB7 retrying",V.gone_s/60,V.gone_s%60);
        else if(V.ping_ms>=0)snprintf(ping,sizeof ping,"%d ms \xC2\xB7 link %s",V.ping_ms,!V.link_alive?"waiting":V.link_lagging?"lagging":"good");
        else snprintf(ping,sizeof ping,"link %s",V.link_alive?"good":"waiting");
        kv(CX0+20,cy+76,COLW-40,rc?"Gone for":"Ping",ping,rc||V.link_lagging||!V.link_alive?WARN:TEXT);
        kv(CX0+20,cy+114,COLW-40,"Room",V.code[0]?V.code:"-",ACCENT);
        float ny=box(CX0,y+52+4*38+8,COLW,52+46,"THE GAME KEEPS RUNNING",MUTED,LINE);
        char note[160];
        if(rc)snprintf(note,sizeof note,"You play on alone. %s is linked again as soon as they're back.",V.partner);
        else snprintf(note,sizeof note,"Your partner's cabinet needs yours, so the arcade doesn't freeze while you're linked.");
        para(UI_M15,CX0+20,ny+16,COLW-40,21,note,MUTED,2);
    }
    frow=0;next_row();
    float ry=voice_box(COL2,CY0,"VOICE");
    if(V.state==TC2V_JOINING)on_item(ON_LEAVE,COL2,&ry,I_CROSS,"CANCEL","Stop joining","Stops joining.",2);
    else if(V.state==TC2V_ROOM_WAITING||V.state==TC2V_LAN_HOSTING)
        on_item(ON_LEAVE,COL2,&ry,I_EJECT,V.state==TC2V_LAN_HOSTING?"STOP HOSTING":"CLOSE ROOM","Back to playing alone","Closes your room. Nobody can join until you host again.",2);
    else on_item(ON_LEAVE,COL2,&ry,I_EJECT,"LEAVE ONLINE GAME","Your partner plays on. You can rejoin.","Ends your part of the session. REJOIN brings you back while the room is open.",2);
}
static void online_tab(void){
    if(V.state!=TC2V_OFFLINE){if(screen!=S_MAIN){screen=S_MAIN;tc2_online_browse(false);}online_session();return;}
    if(screen==S_BROWSE)online_browse();else if(screen==S_LAN)online_lan();else online_offline();
}

/* ---- dialogs ---- */
static void modal_frame(float w,float h,float *x,float *y){
    if(drawing())qc_rrect(&cv,3,3,QMENU_W-6,QMENU_H-6,20,SHADE);
    *x=(QMENU_W-w)/2;*y=(QMENU_H-h)/2;
    rrect(*x,*y,w,h,20,MODAL);rline(*x,*y,w,h,20,3,ACCENT);
}
static void modal_buttons(float x,float y,float w,const char *ok){
    const float bw=180;
    if(button(ID(K_MBTN,0),x+w-32-bw*2-14,y,bw,52,"CANCEL",UI_B22,0,"Closes this without changing anything. B does the same."))modal=M_NONE;
    focusable(ID(K_MBTN,0),F_BUTTON,(int)(x+w-32-bw*1.5f-14));
    if(button(ID(K_MBTN,1),x+w-32-bw,y,bw,52,ok,UI_B22,1,NULL)){
        const int m=modal;modal=M_NONE;
        if(m==M_RESTART||m==M_RESTART_LEAVE)H->action(QACT_RESTART_GAME);
        else if(m==M_APP)H->action(QACT_RESTART_APP);
        else if(m==M_CODE){if(strlen(kbuf)==4)tc2_online_join_code(kbuf);}
        else if(m==M_NAME)tc2_online_set_name(kbuf);
        else if(m==M_IP)tc2_online_set_join_ip(kbuf);
        else if(m==M_SERVER){kerror=tc2_online_set_server(kbuf);if(kerror)modal=M_SERVER;}
    }
    focusable(ID(K_MBTN,1),F_BUTTON,(int)(x+w-32-bw/2));
    next_row();
}
static void confirm(void){
    const float w=660;float x,y;
    const char *title=modal==M_RESTART?"RESTART GAME?":modal==M_RESTART_LEAVE?"RESTART & LEAVE?":"RESTART APP?";
    char body[200],note[200];QColor note_ink=MUTED;
    if(modal==M_RESTART){snprintf(body,sizeof body,"The arcade goes back to its intro in about 10 seconds. The game in progress is lost.");
        snprintf(note,sizeof note,"Your coins are paid back in and your settings stay.");}
    else if(modal==M_RESTART_LEAVE){snprintf(body,sizeof body,"You leave the online game and your arcade restarts.%s%s",V.state==TC2V_LINKED?" ":"",V.state==TC2V_LINKED?"Your partner plays on alone.":"");
        if(V.code[0])snprintf(note,sizeof note,"While room %s stays open, REJOIN %s waits on the ONLINE tab.",V.code,V.code);else note[0]=0;}
    else{const bool pending=H->get(QOPT_PLAYER)!=H->boot_player||H->get(QOPT_RESOLUTION)!=H->boot_resolution;
        snprintf(body,sizeof body,"The app closes and opens again by itself, in about 10 seconds.%s",pending?" Your new player / resolution takes effect.":"");
        if(online_now()){snprintf(note,sizeof note,"This leaves the online game.");note_ink=WARN;}else snprintf(note,sizeof note,"Your settings and coins are kept.");}
    const float h=26+40+12+2*28+(note[0]?2*24+8:0)+20+52+26;
    modal_frame(w,h,&x,&y);
    text(UI_B32,x+32,y+26+30,title,ACCENT);
    float ty=y+26+40+12+20;
    ty+=para(UI_M20,x+32,ty,w-64,28,body,TEXT,2)*28;
    if(note[0])para(UI_M17,x+32,ty+8,w-64,24,note,note_ink,2);
    modal_buttons(x,y+h-26-52,w,modal==M_RESTART?"RESTART":modal==M_RESTART_LEAVE?"RESTART & LEAVE":"RESTART APP");
}
static void keyboard(void){
    static const char code_keys[]="ACDEFGHJKLMNPQRTUVWXY34679",name_keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_",ip_keys[]="123456789.0",
        server_keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:",server_lower_keys[]="abcdefghijklmnopqrstuvwxyz0123456789-.:";
    const bool server_edit=modal==M_SERVER;
    const char *keys=modal==M_CODE?code_keys:modal==M_NAME?name_keys:server_edit?(klower?server_lower_keys:server_keys):ip_keys;
    const int nkeys=(int)strlen(keys),cols=modal==M_IP?3:9,len=modal==M_CODE?4:modal==M_NAME?12:server_edit?63:15;
    const int cells=nkeys+(modal==M_IP?1:2),rows=(cells+cols-1)/cols;
    const float w=modal==M_CODE?720:modal==M_NAME||server_edit?860:560,keyw=modal==M_IP?132:(w-64-(cols-1)*8)/cols;
    const float slot_h=modal==M_CODE?86:modal==M_NAME?62:72;
    const float h=26+40+10+slot_h+16+rows*62+18+52+26+(server_edit?24:0);
    float x,y;modal_frame(w,h,&x,&y);
    text(UI_B32,x+32,y+26+30,modal==M_CODE?"JOIN WITH CODE":modal==M_NAME?"YOUR NAME":server_edit?"LOBBY SERVER":"JOIN BY IP",ACCENT);
    const float sy=y+26+40+10;const int n=(int)strlen(kbuf);
    if(server_edit){
        rrect(x+32,sy,w-64,slot_h,14,WELL);rline(x+32,sy,w-64,slot_h,14,3,ACCENT);
        char first[33];snprintf(first,sizeof first,"%.32s",kbuf);
        text(UI_B24,x+48,sy+29,first,TEXT);
        text(UI_B24,x+48,sy+59,n>32?kbuf+32:"",TEXT);
    }else if(modal==M_IP){
        rrect(x+32,sy,w-64,slot_h,14,WELL);rline(x+32,sy,w-64,slot_h,14,3,ACCENT);
        const int aw=width(UI_B44,kbuf),pw=width(UI_M20,":27750");
        text(UI_B44,x+(w-aw-pw-8)/2,sy+52,kbuf,TEXT);text(UI_M20,x+(w-aw-pw-8)/2+aw+8,sy+52,":27750",MUTED);
    }else{
        const float sw=modal==M_CODE?72:46,gap=modal==M_CODE?10:8,total=len*sw+(len-1)*gap;
        for(int i=0;i<len;i++){
            const float bx=x+(w-total)/2+i*(sw+gap);const bool cur=i==n;
            rrect(bx,sy,sw,slot_h,14,WELL);rline(bx,sy,sw,slot_h,14,3,cur?ACCENT:LINE);
            if(i<n){char c[2]={kbuf[i],0};const int f=modal==M_CODE?UI_B44:UI_B27;text_fit(f,bx,mid(f,sy,slot_h),sw,c,TEXT,1);}
        }
    }
    const float ky=sy+slot_h+16,kx=modal==M_IP?x+(w-3*keyw-16)/2:x+32;
    for(int i=0;i<cells;i++){
        const bool del=i>=nkeys;
        if(modal!=M_IP&&i==nkeys+1)break;   /* DEL spans two cells */
        const int col=i%cols,row=i/cols;const float bx=kx+col*(keyw+8),by=ky+row*62,bw=del&&modal!=M_IP?keyw*2+8:keyw;
        const int id=ID(K_KEY,i);
        if(col==0&&i)next_row();
        hit(id,(int)bx,(int)by,(int)bw,54);focusable(id,F_BUTTON,(int)(bx+bw/2));
        const bool hot=is_hover(id)||is_focus(id);
        rrect(bx,by,bw,54,12,hot?HOVER:WELL);rline(bx,by,bw,54,12,2,hot?ACCENT:LINE);
        char label[8];if(del)snprintf(label,sizeof label,"DEL");else{label[0]=keys[i];label[1]=0;}
        text_fit(del?UI_B20:UI_B24,bx,mid(del?UI_B20:UI_B24,by,54),bw,label,hot?ACCENT:TEXT,1);
        if(clicked(id)){
            kerror=NULL;
            if(del){if(n>0)kbuf[n-1]=0;}
            else if(n<len){kbuf[n]=keys[i];kbuf[n+1]=0;}
        }
    }
    if(server_edit){
        const float bx=kx+5*(keyw+8),by=ky+(rows-1)*62,bw=keyw*2+8;
        if(button(ID(K_MBTN,4),bx,by,bw,54,klower?"ABC":"abc",UI_B24,0,"Switch between lowercase and uppercase letters."))klower=!klower;
        focusable(ID(K_MBTN,4),F_BUTTON,(int)(bx+bw/2));
    }
    next_row();
    if(server_edit){
        text_fit(UI_M17,x+32,y+h-26-52-10,w-64,kerror?kerror:"Hostname or IPv4, optional :port. Both players use the same server.",kerror?WARN:MUTED,0);
        if(button(ID(K_MBTN,2),x+32,y+h-26-52,180,52,"DEFAULT",UI_B22,0,"Restore the community server address, then SAVE to apply it.")){
            snprintf(kbuf,sizeof kbuf,"%s",tc2_online_default_server());kerror=NULL;
        }
        focusable(ID(K_MBTN,2),F_BUTTON,(int)x+122);
        if(button(ID(K_MBTN,3),x+32+194,y+h-26-52,160,52,"CLEAR",UI_B22,0,"Clear the address so you can enter a new one.")){kbuf[0]=0;kerror=NULL;}
        focusable(ID(K_MBTN,3),F_BUTTON,(int)x+306);
    }
    modal_buttons(x,y+h-26-52,w,modal==M_CODE?"JOIN":"SAVE");
}

/* ---- the whole menu ---- */
/* The panel's background, frame and body never change: drawn once, then copied under every redraw (only inside the limit). */
static uint8_t base_px[QMENU_W*QMENU_H*4];static bool base_ready;
static void restore_base(void){
    if(!base_ready){
        QCanvas b;qc_init(&b,base_px,QMENU_W,QMENU_H);
        qc_fill(&b,qrgba(54,183,207,0));
        qc_rrect(&b,0,0,QMENU_W,QMENU_H,22,PANEL);
        qc_rrect(&b,3,BODY_Y,QMENU_W-6,BODY_H,0,BODY);
        qc_rrect_line(&b,0,0,QMENU_W,QMENU_H,22,3,EDGE);
        base_ready=true;
    }
    for(int y=cv.ly0;y<cv.ly1;y++)memcpy(cv.px+((size_t)y*QMENU_W+cv.lx0)*4,base_px+((size_t)y*QMENU_W+cv.lx0)*4,(size_t)(cv.lx1-cv.lx0)*4);
}
static void layout(int which){
    pass=which;nhits=0;nfoci=0;frow=0;
    snprintf(help,sizeof help,"%s",stick_mode?"Stick: move and change \xC2\xB7 A: select \xC2\xB7 B: back \xC2\xB7 grips: tabs":"Point at anything and pull the trigger.");
    if(drawing())restore_base();
    const bool m=modal!=M_NONE;
    const int saved_click=ev_click,saved_adjust=ev_adjust,saved_drag=ev_drag;
    if(m){ev_click=ev_adjust=ev_drag=0;}   /* the page under a dialog is inert */
    header();tabs();
    if(drawing())qc_clip(&cv,3,BODY_Y,QMENU_W-6,BODY_H);
    if(tab==QMENU_TAB_GUN)settings(GUN_ROWS,sizeof GUN_ROWS/sizeof *GUN_ROWS);
    else if(tab==QMENU_TAB_PLAYER)settings(PLAYER_ROWS,sizeof PLAYER_ROWS/sizeof *PLAYER_ROWS);
    else if(tab==QMENU_TAB_GRAPHICS)settings(GRAPHICS_ROWS,sizeof GRAPHICS_ROWS/sizeof *GRAPHICS_ROWS);
    else if(tab==QMENU_TAB_AUDIO)settings(AUDIO_ROWS,sizeof AUDIO_ROWS/sizeof *AUDIO_ROWS);
    else online_tab();
    if(drawing())qc_unclip(&cv);
    frow=1000;footer();
    if(m){
        ev_click=saved_click;ev_adjust=saved_adjust;ev_drag=saved_drag;
        nhits=0;nfoci=0;frow=0;   /* only the dialog takes input */
        if(modal==M_RESTART||modal==M_RESTART_LEAVE||modal==M_APP)confirm();else keyboard();
    }
}

/* ---- input ---- */
static const Focus *find_focus(int id){for(int i=0;i<nfoci;i++)if(foci[i].id==id)return &foci[i];return NULL;}
static void move_focus(int dx,int dy){
    const Focus *f=find_focus(focus);
    if(!f){focus=nfoci?(modal?foci[nfoci-1].id:ID(K_FOOT,0)):0;return;}
    if(dy){
        /* the next row up or down that has something, nearest column */
        int best=-1,best_row=0;
        for(int i=0;i<nfoci;i++){
            const int r=foci[i].row;
            if(dy<0?r>=f->row:r<=f->row)continue;   /* up: only rows above */
            const bool closer=best<0||(dy<0?r>best_row:r<best_row)||(r==best_row&&abs(foci[i].cx-f->cx)<abs(foci[best].cx-f->cx));
            if(closer){best=i;best_row=r;}
        }
        if(best>=0)focus=foci[best].id;
        return;
    }
    if(f->kind==F_TABS){tab=(tab+dx+QMENU_TAB_COUNT)%QMENU_TAB_COUNT;screen=S_MAIN;return;}
    if(f->kind==F_SEG||f->kind==F_SLIDER){ev_adjust=focus;ev_adjust_dir=dx;return;}
    /* a button: its neighbour in the same row */
    int best=-1;
    for(int i=0;i<nfoci;i++){
        if(foci[i].row!=f->row||foci[i].id==focus)continue;
        if(dx>0?foci[i].cx<=f->cx:foci[i].cx>=f->cx)continue;
        if(best<0||abs(foci[i].cx-f->cx)<abs(foci[best].cx-f->cx))best=i;
    }
    if(best>=0)focus=foci[best].id;
}
static int pick(float x,float y){
    for(int i=nhits-1;i>=0;i--)if(x>=hits[i].x&&x<hits[i].x+hits[i].w&&y>=hits[i].y&&y<hits[i].y+hits[i].h)return hits[i].id;
    return 0;
}
static const Hit *find_hit(int id){for(int i=nhits-1;i>=0;i--)if(hits[i].id==id)return &hits[i];return NULL;}
static bool inside_box(const Hit *in,const Hit *box){return in->x>=box->x&&in->y>=box->y&&in->x+in->w<=box->x+box->w&&in->y+in->h<=box->y+box->h;}
/* what the smoothed pointer is on; at an edge the current one holds for 6 more pixels, so a trembling hand doesn't flicker
 * between neighbours (going into something inside the current box, a button in its row, is immediate) */
static int sticky_pick(void){
    const int next=pick(ptr_x,ptr_y);
    if(!hover||next==hover)return next;
    const Hit *now=find_hit(hover),*cand=next?find_hit(next):NULL;
    if(!now||(cand&&inside_box(cand,now)))return next;
    const float m=6;
    if(ptr_x>=now->x-m&&ptr_x<now->x+now->w+m&&ptr_y>=now->y-m&&ptr_y<now->y+now->h+m)return hover;
    return next;
}
static int axis_step(float v,float other,int *held,double now,double *next){
    int d=fabsf(v)>=.65f&&fabsf(v)>=fabsf(other)?(v>0?1:-1):0;
    if(fabsf(v)<.25f||fabsf(v)<fabsf(other))*held=0;
    if(!d)return 0;
    if(d!=*held){*held=d;*next=now+.4;return d;}
    if(now>=*next){*next=now+.1;return d;}
    return 0;
}
void qmenu_update(const QMenuHost *host,const QMenuInput *in){
    H=host;tc2_online_get_view(&V);
    game_count=(tab==QMENU_TAB_ONLINE&&screen!=S_MAIN)?tc2_online_games(games,32):0;
    tc2_online_menu_shown(tab==QMENU_TAB_ONLINE);
    ev_click=ev_adjust=ev_drag=0;ev_adjust_dir=0;
    /* the laser */
    static float last_px=-1,last_py=-1;static double last_now;
    if(in->pointer){
        if(fabsf(in->px-last_px)>3||fabsf(in->py-last_py)>3||in->press)stick_mode=false;
        last_px=in->px;last_py=in->py;
        /* a hand is never still: follow the laser a little behind (~35 ms), jump on big moves */
        const double dt=in->now-last_now;
        if(!have_pointer||fabsf(in->px-ptr_x)>80||fabsf(in->py-ptr_y)>80||dt<=0||dt>.25){ptr_x=in->px;ptr_y=in->py;}
        else{const float a=(float)(1-exp(-dt/.035));ptr_x+=(in->px-ptr_x)*a;ptr_y+=(in->py-ptr_y)*a;}
        have_pointer=true;
        hover=sticky_pick();
    }else{hover=0;have_pointer=false;last_px=last_py=-1;}
    last_now=in->now;
    if(in->press&&!stick_mode){
        pressed=hover;repeat_at=in->now+.4;
        if(KIND(pressed)==K_STEP||KIND(pressed)==K_VOL||(KIND(pressed)==K_ON&&(ARG(pressed)==ON_PREV||ARG(pressed)==ON_NEXT)))ev_click=pressed;
        if(KIND(pressed)==K_TRACK){ev_drag=pressed;ev_drag_x=ptr_x;}
    }
    if(in->held&&pressed&&!in->press){
        if(KIND(pressed)==K_TRACK&&in->pointer){ev_drag=pressed;ev_drag_x=ptr_x;}
        else if((KIND(pressed)==K_STEP||KIND(pressed)==K_VOL)&&hover==pressed&&in->now>=repeat_at){ev_click=pressed;repeat_at=in->now+.09;}
    }
    if(in->release){
        if(pressed&&pressed==hover&&KIND(pressed)!=K_STEP&&KIND(pressed)!=K_VOL&&KIND(pressed)!=K_TRACK&&!(KIND(pressed)==K_ON&&(ARG(pressed)==ON_PREV||ARG(pressed)==ON_NEXT)))ev_click=pressed;
        pressed=0;
    }
    if(!in->held&&!in->release)pressed=0;
    /* the sticks, keyboard, A / B, grips */
    int dx=axis_step(in->stick_x,in->stick_y,&stick_dx,in->now,&stick_next)+in->nav_x;
    int dy=axis_step(in->stick_y,in->stick_x,&stick_dy,in->now,&stick_next)+in->nav_y;
    if(dx||dy||in->select||in->tab){if(!stick_mode&&!find_focus(focus))focus=modal?ID(K_MBTN,1):ID(K_FOOT,0);stick_mode=true;hover=0;}
    if(in->tab&&!modal){tab=(tab+in->tab+QMENU_TAB_COUNT)%QMENU_TAB_COUNT;screen=S_MAIN;tc2_online_browse(false);if(KIND(focus)!=K_FOOT)focus=ID(K_TABS,0);}
    if(dy)move_focus(0,-dy);   /* stick up = the row above */
    else if(dx)move_focus(dx,0);
    if(in->select&&stick_mode)ev_click=focus;
    if(in->back){
        if(modal){modal=M_NONE;focus=ID(K_FOOT,0);}
        else if(tab==QMENU_TAB_ONLINE&&screen!=S_MAIN){screen=S_MAIN;tc2_online_browse(false);focus=ID(K_TABS,0);}
        else{focus=ID(K_TABS,0);stick_mode=true;}
    }
    const int was_modal=modal;
    layout(PASS_ACT);
    if(modal!=was_modal&&!modal)focus=ID(K_FOOT,0);
    tc2_online_get_view(&V);
    game_count=(tab==QMENU_TAB_ONLINE&&screen!=S_MAIN)?tc2_online_games(games,32):0;
    ev_click=ev_adjust=ev_drag=0;
    layout(PASS_LAYOUT);
    if(!find_focus(focus)&&nfoci)focus=modal?ID(K_MBTN,1):ID(K_FOOT,0);
    if(in->pointer)hover=sticky_pick();
}

/* ---- drawing ---- */
static uint32_t mix(uint32_t h,const void *p,size_t n){const uint8_t *b=p;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}
/* the boxes a pointer or highlight change touches: the thing itself, grown for its outline, and any box it sits in (its row) */
static void dirty_add(int id,int *x0,int *y0,int *x1,int *y1){
    if(!id)return;
    Hit box;const Hit *h=find_hit(id);
    if(id==ID(K_TABS,0)){box=(Hit){id,0,60,QMENU_W,BODY_Y-60+4};h=&box;}
    if(!h)return;
    Hit grown={id,h->x-8,h->y-8,h->w+16,h->h+16};
    for(int i=0;i<nhits;i++)if(hits[i].id!=id&&inside_box(h,&hits[i])&&hits[i].w<QMENU_W-40){
        const Hit *o=&hits[i];
        if(o->x-8<grown.x){grown.w+=grown.x-(o->x-8);grown.x=o->x-8;}
        if(o->y-8<grown.y){grown.h+=grown.y-(o->y-8);grown.y=o->y-8;}
        if(o->x+o->w+8>grown.x+grown.w)grown.w=o->x+o->w+8-grown.x;
        if(o->y+o->h+8>grown.y+grown.h)grown.h=o->y+o->h+8-grown.y;
    }
    if(grown.x<*x0)*x0=grown.x;if(grown.y<*y0)*y0=grown.y;
    if(grown.x+grown.w>*x1)*x1=grown.x+grown.w;if(grown.y+grown.h>*y1)*y1=grown.y+grown.h;
}
bool qmenu_render(const QMenuHost *host,uint8_t *pixels,double now,int dirty[4]){
    H=host;
    /* what is shown, minus the live readings (voice levels, ping, a dropped partner's clock) and the pointer / highlight */
    struct { int tab,screen,page,modal,stick;char kbuf[64];int opt[QOPT_COUNT];int boot_p,boot_r;unsigned aw,ah;bool unsup,saved,lower; } s;
    memset(&s,0,sizeof s);
    s.tab=tab;s.screen=screen;s.page=page;s.modal=modal;s.stick=stick_mode;
    s.lower=modal==M_SERVER&&klower;
    memcpy(s.kbuf,kbuf,sizeof s.kbuf);for(int i=0;i<QOPT_COUNT;i++)s.opt[i]=H->get(i);
    s.boot_p=H->boot_player;s.boot_r=H->boot_resolution;s.aw=H->active_w;s.ah=H->active_h;s.unsup=H->preset_unsupported;s.saved=H->saved;
    tc2_online_view v=V;const int live[]={v.level,v.partner_level,v.ping_ms/10,v.gone_s,v.link_alive,v.link_lagging,v.talking};
    v.level=v.partner_level=v.ping_ms=v.gone_s=0;v.link_alive=v.link_lagging=v.talking=false;
    uint32_t h=mix(2166136261u,&s,sizeof s);h=mix(h,&v,sizeof v);h=mix(h,games,sizeof games[0]*game_count);h=mix(h,&game_count,sizeof game_count);
    if(modal==M_SERVER&&kerror)h=mix(h,kerror,strlen(kerror));
    const int point[3]={stick_mode?0:hover,pressed,stick_mode?focus:0};
    uint32_t p=mix(2166136261u,point,sizeof point);p=mix(p,help,strlen(help));
    const uint32_t l=mix(2166136261u,live,sizeof live);
    static uint32_t drawn_point;static int drawn_ids[3];
    const bool live_due=tab==QMENU_TAB_ONLINE&&l!=drawn_live&&now-last_draw>=.15;   /* only the ONLINE tab shows them */
    if(h==drawn_hash&&p==drawn_point&&!live_due)return false;
    qc_init(&cv,pixels,QMENU_W,QMENU_H);
    int x0=QMENU_W,y0=QMENU_H,x1=0,y1=0;
    if(h==drawn_hash){
        /* only the pointer or the highlight moved: the old and new boxes, and the help line; live readings: the tab's body */
        for(int i=0;i<3;i++){dirty_add(drawn_ids[i],&x0,&y0,&x1,&y1);dirty_add(point[i],&x0,&y0,&x1,&y1);}
        if(live_due){if(BODY_Y<y0)y0=BODY_Y;if(FOOT_Y>y1)y1=FOOT_Y;}
        if(x0>3)x0=3;if(x1<QMENU_W-3)x1=QMENU_W-3;   /* full width: rows and the help line span it */
        if(FOOT_Y+4<y0)y0=FOOT_Y+4;if(FOOT_Y+40>y1)y1=FOOT_Y+40;
        if(x0<0)x0=0;if(y0<0)y0=0;if(x1>QMENU_W)x1=QMENU_W;if(y1>QMENU_H)y1=QMENU_H;
        if((y1-y0)*(x1-x0)>QMENU_W*QMENU_H*3/5){x0=y0=0;x1=QMENU_W;y1=QMENU_H;}
        else{
            /* the help line and the boxes are apart: two strips are cheaper than everything between them */
            qc_limit(&cv,0,FOOT_Y+4,QMENU_W,36);layout(PASS_DRAW);
            if(y0<FOOT_Y+4){const int top=y0,bottom=y1<FOOT_Y+4?y1:FOOT_Y+4;qc_limit(&cv,x0,top,x1-x0,bottom-top);layout(PASS_DRAW);}
            if(y1>FOOT_Y+40){qc_limit(&cv,x0,FOOT_Y+40,x1-x0,y1-(FOOT_Y+40));layout(PASS_DRAW);}
        }
    }else{x0=y0=0;x1=QMENU_W;y1=QMENU_H;}
    if(x0==0&&y0==0&&x1==QMENU_W&&y1==QMENU_H)layout(PASS_DRAW);
    drawn_hash=h;drawn_point=p;memcpy(drawn_ids,point,sizeof point);
    if(live_due||y1-y0==QMENU_H){drawn_live=l;last_draw=now;}
    if(dirty){dirty[0]=x0;dirty[1]=y0;dirty[2]=x1-x0;dirty[3]=y1-y0;}
    return true;
}
