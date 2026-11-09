/* The pause menu without a headset: every tab and online state is drawn to build/menu-test/<name>.rgba, and every pressable
 * thing must lie inside its area (the tab body, the footer or a dialog): nothing may need scrolling. Input is driven the way
 * quest_host.c drives it. Built and checked by tests/test_menu.py. */
#include "../quest/quest_menu.c"
#include "../quest/quest_canvas.c"
#include <assert.h>
#include <stdlib.h>

/* ---- a scripted online module ---- */
static tc2_online_view sv;static tc2_online_game sg[16];static int sgn;
static int calls[32];   /* which actions ran */
enum { C_HOST,C_CODE,C_QUICK,C_HOSTLAN,C_JOINIP,C_SETIP,C_VOICE,C_VOLUME,C_NAME,C_LEAVE,C_REJOIN,C_RESTART,C_JOINGAME,C_BROWSE,C_MUTE,C_PTT,C_CREDIT };
static char last_text[32];
void tc2_online_get_view(tc2_online_view *v){*v=sv;}
int tc2_online_games(tc2_online_game *out,int max){int n=sgn<max?sgn:max;memcpy(out,sg,sizeof *sg*n);return n;}
void tc2_online_browse(bool open){calls[C_BROWSE]+=open;}
void tc2_online_refresh(void){}
void tc2_online_join_game(int i){calls[C_JOINGAME]++;(void)i;}
void tc2_online_host_room(void){calls[C_HOST]++;}
void tc2_online_join_code(const char *c){calls[C_CODE]++;snprintf(last_text,sizeof last_text,"%s",c);}
void tc2_online_quick(void){calls[C_QUICK]++;}
void tc2_online_host_lan(void){calls[C_HOSTLAN]++;}
void tc2_online_set_join_ip(const char *a){calls[C_SETIP]++;snprintf(last_text,sizeof last_text,"%s",a);}
void tc2_online_join_ip(void){calls[C_JOINIP]++;}
void tc2_online_set_voice(bool on){calls[C_VOICE]++;sv.voice=on;}
void tc2_online_set_volume(int p){calls[C_VOLUME]++;sv.volume=p<0?0:p>200?200:p;}
void tc2_online_set_push_to_talk(bool on){calls[C_PTT]++;sv.push_to_talk=on;}
void tc2_online_set_join_credit(bool on){calls[C_CREDIT]++;sv.join_credit=on;}
void tc2_online_set_name(const char *n){calls[C_NAME]++;snprintf(last_text,sizeof last_text,"%s",n);}
void tc2_online_leave(void){calls[C_LEAVE]++;}
void tc2_online_rejoin(void){calls[C_REJOIN]++;}
void tc2_online_restart_game(void){calls[C_RESTART]++;}
void tc2_online_toggle_mute(void){calls[C_MUTE]++;sv.muted=!sv.muted;}
void tc2_online_menu_shown(bool t){(void)t;}

/* ---- a host ---- */
static int opt[QOPT_COUNT];static int actions[4];
static int get(int o){return opt[o];}
static void set(int o,int v){
    const RowDef *d=row_def(o);
    if(d&&d->type==T_SLIDER){if(v<d->min)v=d->min;if(v>d->max)v=d->max;}
    opt[o]=v;
}
static void action(int a){actions[a]++;}
static QMenuHost host={get,set,action,0,0,1680,1760,false,true};

static uint8_t pixels[QMENU_W*QMENU_H*4];
static double clock_s=1;
static void frame(QMenuInput in){clock_s+=1/72.;in.now=clock_s;qmenu_update(&host,&in);qmenu_render(&host,pixels,clock_s,NULL);}
static void idle(void){frame((QMenuInput){0});}
/* pressing something: find its box, point at it, pull and release the trigger */
static bool find(int id,float *x,float *y){for(int i=0;i<nhits;i++)if(hits[i].id==id){*x=hits[i].x+hits[i].w/2.f;*y=hits[i].y+hits[i].h/2.f;return true;}return false;}
static void shoot(int id){
    float x,y;idle();if(!find(id,&x,&y)){fprintf(stderr,"no widget %d:%d on screen\n",KIND(id),ARG(id));exit(1);}
    frame((QMenuInput){.pointer=true,.px=x,.py=y});
    frame((QMenuInput){.pointer=true,.px=x,.py=y,.press=true,.held=true});
    frame((QMenuInput){.pointer=true,.px=x,.py=y,.release=true});
}
static void stick(float x,float y){frame((QMenuInput){.stick_x=x,.stick_y=y});frame((QMenuInput){0});}

static int problems;
static void save(const char *name){
    /* force a redraw */
    drawn_hash^=1;qmenu_render(&host,pixels,clock_s+=1,NULL);
    char path[256];snprintf(path,sizeof path,"%s.rgba",name);
    FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}fwrite(pixels,1,sizeof pixels,f);fclose(f);
    /* every pressable thing inside its area */
    for(int i=0;i<nhits;i++){
        const Hit *h=&hits[i];const int k=KIND(h->id);
        bool ok;
        if(modal)ok=h->x>=3&&h->y>=3&&h->x+h->w<=QMENU_W-3&&h->y+h->h<=QMENU_H-3;
        else if(k==K_TAB)ok=h->y>=60&&h->y+h->h<=BODY_Y;
        else if(k==K_FOOT)ok=h->y>=FOOT_Y&&h->y+h->h<=QMENU_H-3&&h->x+h->w<=QMENU_W-20;
        else ok=h->x>=CX0-1&&h->x+h->w<=CX1+1&&h->y>=BODY_Y+8&&h->y+h->h<=FOOT_Y-8;
        if(!ok){fprintf(stderr,"%s: widget %d:%d at %d,%d %dx%d leaves its area\n",name,k,ARG(h->id),h->x,h->y,h->w,h->h);problems++;}
    }
    printf("%s\n",name);
}
static void base_view(void){
    memset(&sv,0,sizeof sv);sv.state=TC2V_OFFLINE;sv.side=sv.home_side=1;snprintf(sv.name,sizeof sv.name,"PLAYER42");
    snprintf(sv.server,sizeof sv.server,"zonesync.net");snprintf(sv.join_ip,sizeof sv.join_ip,"192.168.1.2");sv.voice=true;sv.volume=100;sv.ping_ms=-1;sv.join_credit=true;
    sgn=0;
}
static void games_list(void){
    const char *names[]={"MRSCO KQ7M","DANNY 4HXT","ZEKE CMTR","desktop-23","RAY 9PLW","LUCY 7DFM","OLDTIMER","VRDAD RQ3E","KIM HY6C","ACE TTMX","SAM-PC","QUEST-KID"};
    const int st[]={TC2G_JOIN,TC2G_PLAYING,TC2G_JOIN,TC2G_JOIN,TC2G_FULL,TC2G_JOIN,TC2G_OLD,TC2G_PLAYING,TC2G_JOIN,TC2G_JOIN,TC2G_JOIN,TC2G_JOIN};
    sgn=12;
    for(int i=0;i<sgn;i++){memset(&sg[i],0,sizeof sg[i]);snprintf(sg[i].name,sizeof sg[i].name,"%s",names[i]);sg[i].players=st[i]==TC2G_JOIN?1:2;sg[i].max_players=2;sg[i].status=st[i];sg[i].lan=i>=10;
        if(i==3)snprintf(sg[i].detail,sizeof sg[i].detail,"upstream game");if(i==6)snprintf(sg[i].detail,sizeof sg[i].detail,"older version");
        if(sg[i].lan)snprintf(sg[i].detail,sizeof sg[i].detail,"192.168.1.%d",23+i);}
    sv.browse_answered=true;sv.server_players=19;
}
static void session(int state){
    base_view();sv.state=state;snprintf(sv.code,sizeof sv.code,"KQ7M");snprintf(sv.partner,sizeof sv.partner,"SAM");
    sv.ping_ms=48;sv.link_alive=true;sv.level=5;sv.partner_level=3;sv.partner_voice=true;sv.gone_s=12;
    snprintf(sv.players[0],sizeof sv.players[0],"PLAYER42");sv.player_self[0]=true;
    if(state==TC2V_JOINING){sv.side=2;snprintf(sv.joining,sizeof sv.joining,"KQ7M");}
    if(state==TC2V_LAN_HOSTING){sv.address_count=2;snprintf(sv.address[0],20,"192.168.1.40");snprintf(sv.address[1],20,"100.101.7.12");sv.address_vpn[1]=true;}
}
static void tab_to(int t){qmenu_open(t);idle();}
int main(void){
    opt[QOPT_HAND]=1;opt[QOPT_WORLD]=100;
    base_view();
    /* the settings tabs, plain and changed */
    tab_to(QMENU_TAB_GUN);save("gun");
    opt[QOPT_PITCH]=-12;opt[QOPT_GUN_Y]=-200;opt[QOPT_GUN_Z]=200;opt[QOPT_AIM]=1;save("gun-changed");
    tab_to(QMENU_TAB_PLAYER);save("player");
    opt[QOPT_PLAYER]=1;opt[QOPT_COVER]=1;opt[QOPT_WORLD]=400;opt[QOPT_DISTANCE]=100;save("player-pending");
    tab_to(QMENU_TAB_GRAPHICS);save("graphics");
    opt[QOPT_RESOLUTION]=5;host.preset_unsupported=true;host.saved=false;save("graphics-problems");
    host.preset_unsupported=false;host.saved=true;opt[QOPT_PLAYER]=0;opt[QOPT_RESOLUTION]=0;
    /* online */
    tab_to(QMENU_TAB_ONLINE);save("online");
    base_view();sv.home_side=2;sv.mic_denied=true;idle();save("online-p2-nomic");
    base_view();sv.can_rejoin=true;snprintf(sv.rejoin_code,5,"KQ7M");snprintf(sv.rejoin_partner,24,"SAM");sv.rejoin_side=1;idle();save("online-rejoin");
    base_view();sv.voice=false;idle();save("online-voice-off");
    base_view();sv.push_to_talk=true;sv.join_credit=false;sv.home_side=2;idle();save("online-ptt");
    base_view();games_list();shoot(ID(K_ON,ON_BROWSE));save("browse-1");
    shoot(ID(K_ON,ON_NEXT));save("browse-2");shoot(ID(K_ON,ON_NEXT));save("browse-3");
    shoot(ID(K_ON,ON_BACK));shoot(ID(K_ON,ON_LAN));save("lan");
    shoot(ID(K_ON,ON_BACK));
    session(TC2V_JOINING);idle();save("joining");
    session(TC2V_ROOM_WAITING);idle();save("waiting");
    session(TC2V_LAN_HOSTING);idle();save("hostlan");
    session(TC2V_LINKED);idle();save("linked");
    session(TC2V_LINKED);sv.partner_voice=false;sv.mic_denied=true;idle();save("linked-problems");
    session(TC2V_LINKED);sv.push_to_talk=true;idle();save("linked-ptt");
    session(TC2V_LINKED);sv.push_to_talk=true;sv.talking=true;idle();save("linked-ptt-talking");
    session(TC2V_RECONNECTING);sv.mic_denied=true;idle();save("reconnecting-nomic");
    session(TC2V_RECONNECTING);idle();save("reconnecting");
    tab_to(QMENU_TAB_PLAYER);save("player-locked");
    /* dialogs */
    tab_to(QMENU_TAB_GUN);shoot(ID(K_FOOT,1));save("dialog-restart-leave");shoot(ID(K_MBTN,0));
    base_view();idle();shoot(ID(K_FOOT,1));save("dialog-restart");
    shoot(ID(K_MBTN,1));assert(actions[QACT_RESTART_GAME]==1);
    shoot(ID(K_FOOT,3));save("dialog-app");shoot(ID(K_MBTN,0));assert(actions[QACT_RESTART_APP]==0);
    tab_to(QMENU_TAB_ONLINE);shoot(ID(K_ON,ON_CODE));
    for(int i=0;i<4;i++)shoot(ID(K_KEY,i*5));save("keyboard-code");
    shoot(ID(K_MBTN,1));assert(calls[C_CODE]==1&&!strcmp(last_text,"AGMT"));
    shoot(ID(K_ON,ON_NAME));save("keyboard-name");shoot(ID(K_MBTN,0));
    shoot(ID(K_ON,ON_LAN));shoot(ID(K_ON,ON_IPEDIT));save("keyboard-ip");
    shoot(ID(K_KEY,11));shoot(ID(K_KEY,0));shoot(ID(K_MBTN,1));assert(calls[C_SETIP]==1&&!strcmp(last_text,"192.168.1.1"));
    shoot(ID(K_ON,ON_BACK));
    /* voice: open mic, push to talk, off; a credit to join with */
    base_view();tab_to(QMENU_TAB_ONLINE);
    shoot(ID(K_VOICE,1));assert(sv.voice&&sv.push_to_talk);
    shoot(ID(K_VOICE,2));assert(!sv.voice);
    shoot(ID(K_VOICE,0));assert(sv.voice&&!sv.push_to_talk);
    shoot(ID(K_CREDIT,1));assert(!sv.join_credit);shoot(ID(K_CREDIT,0));assert(sv.join_credit);
    {float x,y;assert(find(ID(K_ON,ON_MUTE),&x,&y));sv.push_to_talk=true;idle();assert(!find(ID(K_ON,ON_MUTE),&x,&y));}   /* no mute with push to talk */

    /* controls */
    tab_to(QMENU_TAB_GUN);
    opt[QOPT_PITCH]=0;shoot(ID(K_STEP,QOPT_PITCH*2+1));assert(opt[QOPT_PITCH]==1);
    shoot(ID(K_RESET,QOPT_PITCH));assert(opt[QOPT_PITCH]==0);
    {float x,y;find(ID(K_TRACK,QOPT_PITCH),&x,&y);   /* dragging the knob to the right end */
     frame((QMenuInput){.pointer=true,.px=x,.py=y,.press=true,.held=true});
     frame((QMenuInput){.pointer=true,.px=x+400,.py=y,.held=true});frame((QMenuInput){.pointer=true,.px=x+400,.py=y,.release=true});
     assert(opt[QOPT_PITCH]==QOPTIONS_GUN_PITCH_MAX);}
    {float x,y;find(ID(K_STEP,QOPT_PITCH*2),&x,&y);   /* holding - repeats */
     frame((QMenuInput){.pointer=true,.px=x,.py=y,.press=true,.held=true});
     for(int i=0;i<72;i++)frame((QMenuInput){.pointer=true,.px=x,.py=y,.held=true});
     frame((QMenuInput){.pointer=true,.px=x,.py=y,.release=true});
     assert(opt[QOPT_PITCH]<QOPTIONS_GUN_PITCH_MAX-6&&opt[QOPT_PITCH]>QOPTIONS_GUN_PITCH_MAX-14);}
    shoot(ID(K_SEG,QOPT_HAND*8+0));assert(opt[QOPT_HAND]==0);
    /* sticks: the menu opens on RESUME; up reaches the rows, left / right change them; A presses */
    qmenu_open(QMENU_TAB_GUN);idle();
    frame((QMenuInput){.select=true});assert(actions[QACT_RESUME]==1);
    qmenu_open(QMENU_TAB_GUN);idle();opt[QOPT_GUN_Z]=0;
    stick(0,1);assert(focus==ID(K_ROW,QOPT_GUN_Z));
    stick(1,0);assert(opt[QOPT_GUN_Z]==QOPTIONS_GUN_OFFSET_STEP);
    save("sticks-focus");
    for(int i=0;i<6;i++)stick(0,1);assert(focus==ID(K_TABS,0));
    stick(1,0);assert(qmenu_tab()==QMENU_TAB_PLAYER);
    frame((QMenuInput){.tab=1});assert(qmenu_tab()==QMENU_TAB_GRAPHICS);
    frame((QMenuInput){.tab=-1});frame((QMenuInput){.tab=-1});assert(qmenu_tab()==QMENU_TAB_GUN);
    /* B: closes a dialog, leaves a browse screen */
    shoot(ID(K_FOOT,1));assert(modal);frame((QMenuInput){.back=true});assert(!modal);
    tab_to(QMENU_TAB_ONLINE);base_view();games_list();shoot(ID(K_ON,ON_BROWSE));assert(screen==S_BROWSE);frame((QMenuInput){.back=true});assert(screen==S_MAIN);
    /* a locked player row ignores everything */
    session(TC2V_LINKED);tab_to(QMENU_TAB_PLAYER);idle();opt[QOPT_PLAYER]=0;
    {float x,y;assert(!find(ID(K_SEG,QOPT_PLAYER*8+1),&x,&y));}
    /* A sweeping laser and a moving highlight redraw only small boxes: the picture must match a full redraw exactly. */
    static uint8_t partial[sizeof pixels];
    for(int t=0;t<QMENU_TAB_COUNT;t++)for(int scn=0;scn<2;scn++){
        if(scn)session(TC2V_LINKED);else base_view();
        qmenu_open(t);idle();drawn_hash^=1;qmenu_render(&host,pixels,clock_s+=1,NULL);
        for(int i=0;i<300;i++){
            frame((QMenuInput){.pointer=true,.px=60+(i*37)%1000,.py=90+(i*53)%620});
            if(i%50==49){stick(0,1);stick(1,0);}
            sv.level=i%9;sv.partner_level=(i/3)%9;sv.ping_ms=40+i%30;sv.talking=i%7<3;   /* live readings */
        }
        memcpy(partial,pixels,sizeof pixels);
        drawn_hash^=1;qmenu_render(&host,pixels,clock_s,NULL);
        if(memcmp(partial,pixels,sizeof pixels)){fprintf(stderr,"tab %d (%s): partial redraws differ from a full one\n",t,scn?"linked":"offline");problems++;}
    }
    /* A trembling hand at the edge between two buttons does not flicker between them; a real move does. */
    base_view();qmenu_open(QMENU_TAB_GUN);idle();
    {
        const Hit *laser=find_hit(ID(K_SEG,QOPT_AIM*8)),*tracer=find_hit(ID(K_SEG,QOPT_AIM*8+1));assert(laser&&tracer);
        const float edge=laser->x+laser->w,y=laser->y+laser->h/2.f;
        for(int i=0;i<20;i++)frame((QMenuInput){.pointer=true,.px=edge-3,.py=y});
        assert(hover==laser->id);
        for(int i=0;i<40;i++)frame((QMenuInput){.pointer=true,.px=edge+(i%2?3:-2),.py=y+(i%3)-1});
        assert(hover==laser->id);
        for(int i=0;i<20;i++)frame((QMenuInput){.pointer=true,.px=edge+20,.py=y});
        assert(hover==tracer->id);
        (void)tracer;
    }
    if(problems){fprintf(stderr,"%d problems\n",problems);return 1;}
    printf("menu fixture: all states fit, controls work, partial redraws match full ones, the pointer holds steady at edges\n");
    return 0;
}
