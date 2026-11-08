/* tc2_online.c -- online twin-cabinet sessions for Time Crisis II VR. See tc2_online.h and docs/ONLINE.md.
 *
 * Transport: upstream's NMN2 client (engine/net.c) and built-in host (engine/net_host.c), game id "tv". Network frames of 256
 * bytes carry the newest C422 packet (tc2_link.c, magic 0xC4) and voice (tc2_voice.c, magic 0xC5), so stock nmn-server rooms
 * and the built-in host carry them unchanged. The lobby slot is the cabinet: slot 0 = P1 Keith (left/red), slot 1 = P2 Robert
 * (right/blue). Joining is Ready; the lowest slot starts the session as soon as both are in. Nothing is lockstepped: the arcade's
 * own link protocol (it declares the partner lost after 16 frames without a packet) copes with loss and latency, as the cable did.
 *
 * Ways to meet: a room on the lobby server with a four-character code (host / join code / browse), the built-in host on the
 * local network (found by broadcast, or joined by address), and quick match (the server's fullest open room). */
#include "tc2_online.h"
#include "tc2_link.h"
#include "tc2_voice.h"
#include "tc2_rooms.h"
#include "tc2_cabinet.h"
#include "tc2_game.h"
#include "net.h"
#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

#define CFG_PATH        "online.cfg"
#define DEFAULT_SERVER  "zonesync.net"
#define NAME_MAX_LEN    12
#define LINK_VERSION    1           /* upstream namco22-decompile's Time Crisis 2 link, version 1 */

/* the room the next connect asks for (generated net.c adapter, quest/tc2_sources.cmake): 0 the server picks, -1 a new room
 * named eng_net_room_request_name, n room n. It stays set, so the library's own rejoins go back to the same room. */
extern int eng_net_room_request;extern char eng_net_room_request_name[32];
int eng_net_my_room(void);

static void session_begin(int slot,int players);
static bool menu_from_mode;              /* the ONLINE page was opened by the SOLO/LINKED shot (mode_screen_tick) */
#define MODE_RESTART_FRAMES 10
static uint32_t init_frame;               /* our arcade's last power-on or TEST restart (sim_frames) */
static bool p2_init_pending;              /* P2 linked after its power-on handshake: it restarts between games (link_contact_tick) */
static uint32_t game_start_frame;         /* when our current game began (standin_tick) */
static int mode_restart;                 /* frames of TEST still to press: the cabinet restarts (link_contact_tick) */
static int coin_cost=4;                   /* coins a credit takes, as last seen (coins_tick); 4 on a fresh cabinet */
static int repay;                         /* coins to pay back after a restart the SOLO/LINKED shot asked for (repay_tick) */
#define REPAY_AFTER_FRAMES 720             /* attract is back ~600 frames after a TEST restart; coins at +900 counted (modescreen) */
static bool await_trigger;                /* the repay waits for the trigger: SELECT GAME MODE ran out (menu_timeout_tick) */
#define COINS_AFTER_RESTART 40             /* coins 30 frames after a TEST restart counted (measured 2026-11-08) */
#define MENU_TIMEOUT_LEAD 30               /* frames before SELECT GAME MODE would choose by itself that the restart begins */
#define LINK_INIT_FRAMES 880               /* the game shakes hands on its link ~920 frames after power-on (measured) */
static void session_end(void);
static int  frame_out(uint8_t *buf);
static void frame_in(int slot,const uint8_t *buf,uint32_t seq);
static void net_hint(const char *text,int frames);
/* Game id "t2" with upstream namco22-decompile's own Time Crisis 2 payload (tc2_link.c): a VR cabinet and the upstream desktop
 * game share rooms and link with each other. Voice rides in a block upstream's parser never reads (tc2_voice.c). */
#define GAME_ID         "t2"
static const char *slot_label(int slot){return slot==0?"Left / Red (Keith)":slot==1?"Right / Blue (Robert)":"";}   /* the slot IS the side */
static const eng_net_game game={GAME_ID,LINK_VERSION,2,TC2_LINK_PAYLOAD,"TC2_NET","game",0,
    session_begin,session_end,frame_out,frame_in,net_hint,slot_label};

static char name[NAME_MAX_LEN+1]="PLAYER";
static char server[64]=DEFAULT_SERVER;
static uint8_t join_ip[4]={192,168,1,2};
static int join_port=27750;
static bool inited;
static int home_side=1;                    /* the cabinet the player chose offline (restored after a session) */
static uint32_t sim_frames,last_poll_ms;
static uint32_t start_sent_ms;
static bool ready_sent;
static enum { MODE_NONE,MODE_LAN_HOST,MODE_JOIN,MODE_ROOM,MODE_QUICK } mode;
static char room_code[5];                  /* the code of the server room we host or joined */
static int room_id;
static bool room_owner;                    /* we opened that room (host_room): P1, waiting for a partner */
static char pending_code[5];               /* JOIN ROOM CODE waits for the room list */
static uint32_t pending_code_ms;

/* toast */
static char toast[64];static uint32_t toast_until,toast_serial;
static void say(const char *text,uint32_t ms){
    snprintf(toast,sizeof toast,"%s",text);for(char *c=toast;*c;c++)*c=(char)toupper((unsigned char)*c);
    toast_until=SDL_GetTicks()+ms;toast_serial++;
    fprintf(stderr,"[ONLINE] %s\n",toast);
}

/* ---- config ---- */
static bool cfg_voice=true,cfg_muted=false;static int cfg_volume=100;
static void save_cfg(void){
    FILE *f=fopen(CFG_PATH,"w");if(!f)return;
    fprintf(f,"name=%s\nserver=%s\njoin=%u.%u.%u.%u:%d\nvoice=%d\nmic_muted=%d\nvoice_volume=%d\n",name,server,
            join_ip[0],join_ip[1],join_ip[2],join_ip[3],join_port,cfg_voice?1:0,cfg_muted?1:0,cfg_volume);
    fclose(f);
}
static void clean_name(char *s){
    char out[NAME_MAX_LEN+1];int n=0;
    for(;*s&&n<NAME_MAX_LEN;s++){char c=(char)toupper((unsigned char)*s);if(isalnum((unsigned char)c)||c=='-'||c=='_'||c==' ')out[n++]=c;}
    while(n&&out[n-1]==' ')n--;out[n]=0;
    if(!n)snprintf(out,sizeof out,"PLAYER");
    memcpy(name,out,sizeof out);   /* the cleaned text becomes the player's name */
}
static void load_cfg(void){
    FILE *f=fopen(CFG_PATH,"r");
    if(!f){snprintf(name,sizeof name,"PLAYER%02u",(unsigned)(SDL_GetPerformanceCounter()%100));save_cfg();return;}
    char line[128];
    while(fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"name=",5)){char tmp[64];snprintf(tmp,sizeof tmp,"%s",line+5);clean_name(tmp);}
        else if(!strncmp(line,"server=",7))snprintf(server,sizeof server,"%s",line+7);
        else if(!strncmp(line,"join=",5)){unsigned a,b,c,d;int p=27750;
            if(sscanf(line+5,"%u.%u.%u.%u:%d",&a,&b,&c,&d,&p)>=4&&a<256&&b<256&&c<256&&d<256){join_ip[0]=a;join_ip[1]=b;join_ip[2]=c;join_ip[3]=d;join_port=p>0&&p<65536?p:27750;}}
        else if(!strncmp(line,"voice=",6))cfg_voice=atoi(line+6)!=0;
        else if(!strncmp(line,"mic_muted=",10))cfg_muted=atoi(line+10)!=0;
        else if(!strncmp(line,"voice_volume=",13)){cfg_volume=atoi(line+13);if(cfg_volume<0)cfg_volume=0;if(cfg_volume>200)cfg_volume=200;}
    }
    fclose(f);
}
static void apply_voice_cfg(void){tc2_voice_set_enabled(cfg_voice);tc2_voice_set_muted(cfg_muted);tc2_voice_set_volume(cfg_volume);}

/* ---- local addresses (shown while hosting so the partner knows what to type) ---- */
static char addresses[3][20];static int address_count;
static void find_addresses(void){
    address_count=0;
#ifdef _WIN32
    char host[256];struct addrinfo hints={0},*res=NULL;hints.ai_family=AF_INET;
    if(gethostname(host,sizeof host)==0&&getaddrinfo(host,NULL,&hints,&res)==0){
        for(struct addrinfo *a=res;a&&address_count<3;a=a->ai_next){
            const uint8_t *b=(const uint8_t*)&((struct sockaddr_in*)a->ai_addr)->sin_addr;
            if(b[0]==127||(b[0]==169&&b[1]==254))continue;
            snprintf(addresses[address_count++],20,"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);
        }
        freeaddrinfo(res);
    }
#else
    struct ifaddrs *list=NULL;
    if(getifaddrs(&list)==0){
        for(struct ifaddrs *i=list;i&&address_count<3;i=i->ifa_next){
            if(!i->ifa_addr||i->ifa_addr->sa_family!=AF_INET||(i->ifa_flags&IFF_LOOPBACK))continue;
            const uint8_t *b=(const uint8_t*)&((struct sockaddr_in*)i->ifa_addr)->sin_addr;
            if(b[0]==169&&b[1]==254)continue;
            snprintf(addresses[address_count++],20,"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);
        }
        freeifaddrs(list);
    }
#endif
}
/* 100.64.0.0/10: carrier-grade NAT space, where Tailscale and similar VPNs put their addresses */
static bool vpn_address(const char *a){unsigned x,y;return sscanf(a,"%u.%u",&x,&y)==2&&x==100&&y>=64&&y<128;}

/* ---- the online episode ----
 * From the first link until the player leaves, the C422 stays connected and the cabinet keeps the side the lobby gave it. The
 * session underneath may drop and come back (the partner rejoins, a server's session limit, a Wi-Fi gap): the arcade sees what a
 * real cabinet sees when the other one is switched off and on again, never a cabinet changing sides mid-game. */
static bool episode;                      /* linked at least once, not left */
static int pending_slot=-1;               /* a partner arrived while we choose on SELECT GAME MODE: the stand-in hands over after */
static char target[64];                   /* the address joined (reconnects go back there) */
static uint32_t partner_gone_ms,lost_ms,retry_ms,join_started_ms;

static void partner_name(char *out,size_t n){
    snprintf(out,n,"PARTNER");
    for(int i=0;i<eng_net_roster_count();i++){int ready,self;char nm[24];if(eng_net_roster(i,nm,sizeof nm,&ready,&self)&&!self){snprintf(out,n,"%s",nm);return;}}
}
static void session_begin(int slot,int players){
    (void)players;
    if(tc2_link_ghost()){
        extern uint8_t tc2_ram[];
        if(slot==0&&tc2_ram[0x2CE92B]==1&&tc2_ram[0x2F3930]==1){   /* choosing now: a link change would cross SOLO/LINKED out under the player */
            if(pending_slot<0){fprintf(stderr,"[ONLINE] frame %u: partner arrived during SELECT GAME MODE: the stand-in stays until the choice\n",sim_frames);
                say("PARTNER CONNECTED - CHOOSE YOUR MODE",4000);}
            pending_slot=slot;return;
        }
        fprintf(stderr,"[ONLINE] frame %u: the partner replaces the stand-in\n",sim_frames);
        tc2_link_set_ghost(false);
    }
    pending_slot=-1;
    int side=slot==1?2:1;char partner[24],line[64];partner_name(partner,sizeof partner);
    if(tc2_link_ghost())tc2_link_set_ghost(false);   /* a real partner replaces the stand-in */
    tc2_link_session_reset();
    if(episode&&tc2_link_enabled()&&side==qtc2_cabinet_side){snprintf(line,sizeof line,"%s IS BACK - LINKED AGAIN",partner);say(line,4000);}
    else{
        if(episode&&side!=qtc2_cabinet_side)fprintf(stderr,"[ONLINE] the lobby moved this cabinet from P%d to P%d\n",qtc2_cabinet_side,side);
        qtc2_cabinet_side=side;tc2_link_set_enabled(true);
        if(side==2&&sim_frames-init_frame>LINK_INIT_FRAMES)p2_init_pending=true;
        snprintf(line,sizeof line,"LINKED WITH %s - P%d %s",partner,side,side==2?"ROBERT":"KEITH");say(line,6000);
    }
    if(menu_from_mode){menu_from_mode=false;qhost_online_menu(false);}   /* opened by the SOLO/LINKED shot: back to the arcade */
    if(!eng_net_hosting())snprintf(target,sizeof target,"%s",eng_net_server());   /* also a LAN host found by discovery */
    episode=true;partner_gone_ms=lost_ms=0;
}
static void session_end(void){ready_sent=false;start_sent_ms=0;}   /* the link stays up; the lobby re-links (see lobby_tick) */
/* Each poll sends one frame: link fragments first (the arcade must not wait), voice when the link has nothing new. */
/* One payload a frame, as upstream: the newest link frames that fit, then (ours only) the voice block, then the trailer. Voice
 * takes room from the repeated older link frames, never from the newest. */
static int frame_out(uint8_t *buf){
    if(!tc2_link_enabled())return 0;
    memset(buf,0,TC2_LINK_PAYLOAD);
    const int end=TC2_LINK_PAYLOAD-TC2_LINK_TRAILER;
    int want=tc2_voice_wanted_bytes(),limit=end-want;
    if(want&&!tc2_link_pack_fits_newest(limit)){limit=end;want=0;}
    int o=tc2_link_pack(buf,limit);
    if(want)tc2_voice_pack(buf+o,end-o);
    tc2_link_trailer(buf);
    return 1;
}
static void frame_in(int slot,const uint8_t *buf,uint32_t seq){
    (void)slot;(void)seq;
    int o=tc2_link_unpack(buf);
    if(o>0)tc2_voice_unpack(buf+o,TC2_LINK_PAYLOAD-TC2_LINK_TRAILER-o);
}
static void net_hint(const char *text,int frames){
    /* the library's own lobby notes ("back in the lobby", "rejoining") are internal while an episode recovers by itself */
    if(episode)fprintf(stderr,"[ONLINE] (net) %s\n",text);else say(text,(uint32_t)(frames>0?frames*17:4000));
}

void tc2_online_init(void){
    if(inited)return;inited=true;
    home_side=qtc2_cabinet_side;
    load_cfg();
    tc2_voice_init();apply_voice_cfg();
    eng_net_init(&game);
    eng_net_set_name(name);
    eng_net_preset_server(server);
    fprintf(stderr,"[ONLINE] ready: name %s, server %s, link version %d, voice %s\n",name,server,LINK_VERSION,cfg_voice?"on":"off");
}
bool tc2_online_session(void){return inited&&(eng_net_session_active()||episode);}
bool tc2_online_busy(void){return inited&&(eng_net_hosting()||eng_net_connected()||eng_net_discovering()||mode!=MODE_NONE||episode);}

static void end_episode(void){
    if(tc2_link_enabled()&&!tc2_link_ghost())tc2_link_set_enabled(false);   /* the stand-in stays (standin_tick) */
    tc2_voice_set_active(false);
    qtc2_cabinet_side=home_side;episode=false;partner_gone_ms=lost_ms=retry_ms=0;
}
static void leave(void){
    bool hosting=eng_net_hosting();
    if(hosting)eng_net_host_stop();else eng_net_disconnect();
    mode=MODE_NONE;ready_sent=false;start_sent_ms=0;join_started_ms=0;pending_code[0]=0;room_code[0]=0;room_id=0;room_owner=false;
    eng_net_room_request=0;eng_net_room_request_name[0]=0;
    bool was=episode;end_episode();
    say(was?"LEFT THE ONLINE GAME - SINGLE PLAYER":hosting?"STOPPED HOSTING":"OFFLINE",3500);
}
/* Re-using the resolved address matters: a reconnect every few seconds must not block the headset's frame on DNS. Connecting
 * while a connection attempt runs is a no-op (the library retransmits its HELLO itself). */
static bool connect_to(const char *address){
    eng_net_set_name(name);
    if(!address||!address[0])return false;
    if(strcmp(eng_net_server(),address)!=0&&!eng_net_set_server(address))return false;
    return eng_net_connect();
}
static void host_lan(void){
    eng_net_set_name(name);eng_net_room_request=0;
    if(eng_net_host_start()){mode=MODE_LAN_HOST;find_addresses();say("HOSTING - WAITING FOR A PARTNER",4000);}
    else say("CANNOT HOST: PORT 27750 IS IN USE",5000);
}
static void join_address(const char *address){
    snprintf(target,sizeof target,"%s",address);eng_net_room_request=0;
    if(connect_to(address)){mode=MODE_JOIN;join_started_ms=SDL_GetTicks();char s[64];snprintf(s,sizeof s,"CONNECTING TO %s",address);say(s,3000);}
    else say("CANNOT REACH THAT ADDRESS",5000);
}
static void quick_match(void){
    snprintf(target,sizeof target,"%s",server);eng_net_room_request=0;
    if(connect_to(server)){mode=MODE_QUICK;join_started_ms=SDL_GetTicks();say("QUICK MATCH: LOOKING FOR A PARTNER",4000);}
    else say("CANNOT REACH THE SERVER",5000);
}
static void host_room(void){
    const char *forced=getenv("TC2_NET_ROOM_CODE");   /* tests: a known code */
    if(forced&&tc2_room_code_valid(forced))snprintf(room_code,sizeof room_code,"%s",forced);else tc2_room_code_make(room_code);
    snprintf(target,sizeof target,"%s",server);
    eng_net_room_request=-1;snprintf(eng_net_room_request_name,sizeof eng_net_room_request_name,"%s %s",name,room_code);
    room_id=0;room_owner=true;
    if(connect_to(server)){mode=MODE_ROOM;join_started_ms=SDL_GetTicks();char s[48];snprintf(s,sizeof s,"ROOM CODE %s - OPENING ROOM",room_code);say(s,4000);}
    else{room_code[0]=0;eng_net_room_request=0;say("CANNOT REACH THE SERVER",5000);}
}
static void join_room(int id,const char *code){
    snprintf(target,sizeof target,"%s",server);
    eng_net_room_request=id;eng_net_room_request_name[0]=0;room_id=id;room_owner=false;
    snprintf(room_code,sizeof room_code,"%s",code?code:"");
    if(connect_to(server)){mode=MODE_ROOM;join_started_ms=SDL_GetTicks();say("JOINING THE ROOM",3000);}
    else{eng_net_room_request=0;say("CANNOT REACH THE SERVER",5000);}
}
static void join_code(const char *code){
    if(!tc2_room_code_valid(code)){say("A ROOM CODE IS 4 LETTERS / DIGITS",4000);return;}
    if(!tc2_rooms_query(server,LINK_VERSION)){say("CANNOT REACH THE SERVER",5000);return;}
    snprintf(pending_code,sizeof pending_code,"%s",code);pending_code_ms=SDL_GetTicks();
    say("LOOKING FOR THE ROOM",3000);
}
static void code_join_tick(void){
    if(!pending_code[0])return;
    tc2_rooms_poll();
    if(tc2_rooms_answered()){
        for(int i=0;i<tc2_rooms_count();i++){
            const tc2_room *r=tc2_rooms_get(i);
            if(strcmp(r->code,pending_code))continue;
            pending_code[0]=0;
            if(r->version!=LINK_VERSION){say("THAT ROOM RUNS ANOTHER VERSION",5000);return;}
            if(r->state){say("THAT ROOM IS ALREADY PLAYING",5000);return;}
            if(r->players>=r->max_players){say("THAT ROOM IS FULL",5000);return;}
            char code[5];snprintf(code,sizeof code,"%s",r->code);join_room(r->id,code);return;
        }
        pending_code[0]=0;say("NO ROOM WITH THAT CODE",5000);
    }else if(SDL_GetTicks()-pending_code_ms>3500){pending_code[0]=0;say("NO ANSWER FROM THE SERVER",5000);}
}

/* lobby automation: joining means ready; the lowest slot starts once both are in */
static void lobby_tick(void){
    if(!eng_net_connected()||eng_net_session_active())return;
    if(mode==MODE_ROOM&&eng_net_room_request<=0&&eng_net_my_room()>0){
        /* our room exists now: every later (re)connect goes back to it, never to a new one */
        room_id=eng_net_my_room();eng_net_room_request=room_id;eng_net_room_request_name[0]=0;
        fprintf(stderr,"[ONLINE] in room %d (code %s)\n",room_id,room_code);
    }
    if(!ready_sent||!eng_net_self_ready()){eng_net_set_ready(1);ready_sent=true;}
    if(eng_net_roster_count()>=2&&eng_net_all_ready()){
        int lowest=99;for(int i=0;i<eng_net_roster_count();i++){int s=eng_net_roster_slot(i);if(s<lowest)lowest=s;}
        uint32_t now=SDL_GetTicks();
        if(eng_net_slot()==lowest&&(!start_sent_ms||now-start_sent_ms>1500)){eng_net_request_start();start_sent_ms=now;}
    }
}

/* recovery: keeps the episode's room joinable and the connection up */
static void episode_tick(void){
    uint32_t now=SDL_GetTicks();
    if(mode!=MODE_NONE&&!episode&&join_started_ms&&!eng_net_connected()&&!eng_net_hosting()&&!eng_net_discovering()&&now-join_started_ms>8000){
        int was=mode;char why[96];eng_net_status(why,sizeof why);
        mode=MODE_NONE;join_started_ms=0;eng_net_disconnect();eng_net_room_request=0;room_code[0]=0;room_owner=false;
        if(strstr(why,"room full")||strstr(why,"full"))say("THAT ROOM IS FULL",6000);
        else if(strstr(why,"no such room"))say("THAT ROOM IS GONE",6000);
        else if(strstr(why,"in progress"))say("THAT ROOM IS ALREADY PLAYING",6000);
        else say(was==MODE_JOIN?"NO ANSWER - CHECK ADDRESS / PORT 27750":"NO ANSWER FROM THE SERVER",6000);
        return;
    }
    if(eng_net_connected())join_started_ms=0;
    if(!episode)return;
    /* the partner left the room mid-session: a running session turns rejoins away, so reopen the room */
    if(eng_net_session_active()&&eng_net_roster_count()<2){
        if(!partner_gone_ms)partner_gone_ms=now;
        else if(now-partner_gone_ms>1200){
            partner_gone_ms=0;say("PARTNER LEFT - WAITING FOR REJOIN",6000);
            if(eng_net_hosting()){eng_net_host_stop();eng_net_host_start();}
            else{eng_net_disconnect();connect_to(target);}
            ready_sent=false;start_sent_ms=0;
        }
    }else partner_gone_ms=0;
    /* our own connection is gone (the host vanished, or a long stall): keep trying the same place for a minute */
    if(!eng_net_connected()&&!eng_net_hosting()){
        if(!lost_ms){lost_ms=now;retry_ms=0;say("CONNECTION LOST - RECONNECTING",5000);}
        if(now-retry_ms>3000){retry_ms=now;connect_to(target);}
        if(now-lost_ms>60000){eng_net_disconnect();mode=MODE_NONE;eng_net_room_request=0;room_code[0]=0;room_owner=false;end_episode();say("COULD NOT RECONNECT - SINGLE PLAYER",6000);}
    }else lost_ms=0;
}

void tc2_online_toggle_mute(void){
    if(!inited)return;
    cfg_muted=!cfg_muted;tc2_voice_set_muted(cfg_muted);save_cfg();
    say(cfg_muted?"MICROPHONE MUTED":"MICROPHONE ON",2000);
}

/* ---- the browser ---- */
static enum { SCREEN_MAIN,SCREEN_BROWSE } screen;
static uint32_t browse_ms;
static void browse_open(void){
    screen=SCREEN_BROWSE;browse_ms=SDL_GetTicks();
    if(!tc2_rooms_query(server,LINK_VERSION))say("CANNOT REACH THE SERVER - LAN ONLY",4000);
    eng_net_discover();
}
static void browse_tick(void){
    if(screen!=SCREEN_BROWSE)return;
    tc2_rooms_poll();
    if(SDL_GetTicks()-browse_ms>4000){browse_ms=SDL_GetTicks();tc2_rooms_query(server,LINK_VERSION);if(!eng_net_discovering())eng_net_discover();}
}

/* ---- the ONLINE page's items (the page itself is below) ---- */
enum { ITEM_HOST_ROOM,ITEM_JOIN_CODE,ITEM_BROWSE,ITEM_HOST_LAN,ITEM_JOIN_IP,ITEM_QUICK,ITEM_VOICE,ITEM_MIC,ITEM_VOLUME,ITEM_NAME,
       ITEM_LEAVE,ITEM_REFRESH,ITEM_BACK,ITEM_ROOM0=100,ITEM_LAN0=200 };
static int cursor,edit_item=-1,edit_pos;

/* ---- the arcade's own way in: SOLO/LINKED on SELECT GAME MODE (measured on US TSS3 Ver. B, 2026-11-07) ----
 * The screen comes up when the first credit goes in; it waits with 0x2CE92B = 1 and 0x2F3930 = 1 (a choice moves 0x2F3930 on to 2,
 * 3, 255, 0 for SOLO ONLY, 4, 5, ... for LINK PLAY) and times out to SOLO ONLY after ~15 s. Without a partner (0x802F3502 = 0)
 * SOLO/LINKED is crossed out and the game ignores a shot at it (three shots, nothing happened). The stand-in (below) answers from
 * power-on, so this only happens without one (a P2 cabinet, TC2_NO_STANDIN). Such a shot opens the ONLINE page instead: it hosts a room (P1 restarts so the screen comes up again with the stand-in there, and the coins are paid back), or,
 * when we are joining someone, opens the ONLINE page (the arcade is held while it is open). The panel is x 20-632, y 95-258 of
 * the 640x480 picture; the game's own gun position is used, so a replayed shot behaves like a real one.
 * The coins (measured 2026-11-08, coins 0..9 against RAM): 0x2CE7B7 holds the coins in the machine and 0x2F3D27 counts every
 * coin. A fresh cabinet takes 4 coins a credit; SELECT GAME MODE comes up as the first credit is complete and takes it then
 * (3 coins: 3; 4: 0; 5: 1; 8: 4). A TEST restart clears them. */
static bool connected_any(void);
static bool host_waiting(void);
static void host_room(void);
static void coins_tick(void){
    extern uint8_t tc2_ram[];
    static int prev_held=-1,prev_total;
    const int held=tc2_ram[0x2CE7B7],total=tc2_ram[0x2F3D27];
    if(prev_held>=0){const int used=prev_held+(uint8_t)(total-prev_total)-held;   /* a credit taken: a game or a continue */
        if(used>0&&used<=16&&(!init_frame||sim_frames-init_frame>300))coin_cost=used;}   /* not a restart clearing them */
    prev_held=held;prev_total=total;
}
static void restart_for_dropin(void){
    extern uint8_t tc2_ram[];
    repay=coin_cost+tc2_ram[0x2CE7B7];          /* the credit SELECT GAME MODE took, and the coins left over */
    mode_restart=MODE_RESTART_FRAMES;init_frame=sim_frames;
    fprintf(stderr,"[ONLINE] frame %u: SELECT GAME MODE came up without a partner: P1 restarts so SOLO/LINKED opens (TEST), %d coins paid back after\n",sim_frames,repay);
    char s[64];snprintf(s,sizeof s,"ROOM %s OPEN - THE ARCADE RESTARTS, COINS KEPT",room_code);
    say(room_code[0]?s:"THE ARCADE RESTARTS - YOUR COINS ARE KEPT",9000);
}
/* SELECT GAME MODE's timeout (measured 2026-11-08): the 16-bit word at 0x2F3933 counts the screen's 900 frames down, one a frame,
 * and at 0 the game chooses for the player (SOLO/LINKED when it is open). Instead, just before, the arcade restarts into its intro
 * (TEST) with the coins kept; the trigger pays them back (repay_tick) and the screen comes up again with its full 15 s. Coins count
 * from 30 frames after a restart, during the warning screen, so a pull at any time brings the screen back within a second. After a
 * restart 0x2CE92B stays 1 and the screen waits with 0x2F3930 = 0, so "counting" is the word going down by one a frame. */
static void menu_timeout_tick(void){
    extern uint8_t tc2_ram[];
    static int off=-1;if(off<0)off=getenv("TC2_MENU_TIMEOUT")!=NULL;   /* tests: the game's own timeout */
    static int prev=-1;
    const int left=tc2_ram[0x2F3933]<<8|tc2_ram[0x2F3934];
    const bool counting=tc2_ram[0x2CE92B]==1&&tc2_ram[0x2F3930]<=1&&left==prev-1;
    prev=left;
    if(off||!counting||left>MENU_TIMEOUT_LEAD||mode_restart||repay)return;
    repay=coin_cost+tc2_ram[0x2CE7B7];await_trigger=true;   /* the credit the screen took, and the coins left over */
    mode_restart=MODE_RESTART_FRAMES;init_frame=sim_frames;
    fprintf(stderr,"[ONLINE] frame %u: SELECT GAME MODE ran out: back to the intro (TEST), %d coins kept for the trigger\n",sim_frames,repay);
    say("COINS KEPT - PULL THE TRIGGER TO PLAY",8000);
}
static void mode_screen_tick(void){
    extern uint8_t tc2_ram[];extern uint16_t tc2_inputs,g_ss22_gun_x,g_ss22_gun_y;
    static bool prev_trigger;
    const bool trigger=(tc2_inputs&0x0010)!=0,fresh=trigger&&!prev_trigger;prev_trigger=trigger;
    if(menu_from_mode&&!qhost_online_menu_open())menu_from_mode=false;   /* the player left the page without linking */
    if(tc2_ram[0x2CE92B]!=1||tc2_ram[0x2F3930]!=1)return;
    static int off=-1;if(off<0)off=getenv("TC2_NO_MODE_MENU")!=NULL;   /* tests that link later on purpose */
    if(off||!fresh||tc2_ram[0x2F3502]!=0||eng_net_session_active()||mode_restart||repay)return;   /* LINKED is crossed out: no partner */
    const int x=((int)g_ss22_gun_x-tc2_game.gun_x0)*640/tc2_game.gun_xw,y=((int)g_ss22_gun_y-tc2_game.gun_y0)*480/tc2_game.gun_yh;
    if(x<20||x>632||y<95||y>258)return;
    if(!connected_any()||host_waiting()){
        fprintf(stderr,"[ONLINE] frame %u: SOLO/LINKED shot without a partner (%d,%d): %s\n",sim_frames,x,y,connected_any()?"hosting already":"hosting a room");
        if(!connected_any())host_room();                 /* the stand-in answers from now on (room_owner): standin_tick */
        if(host_waiting())restart_for_dropin();           /* else the server is out of reach: host_room said so */
        return;
    }
    fprintf(stderr,"[ONLINE] frame %u: SOLO/LINKED shot without a partner (%d,%d): the ONLINE page opens\n",sim_frames,x,y);
    menu_from_mode=true;
    if(!connected_any()){screen=SCREEN_MAIN;cursor=0;edit_item=-1;}
    qhost_online_menu(true);
    say(connected_any()?"WAITING FOR YOUR PARTNER":"HOST A ROOM - YOUR PARTNER JOINS YOU",5000);
}
/* ---- drop-in: the stand-in partner (measured 2026-11-07, docs/ONLINE.md "Drop in at any time") ----
 * The game decides whether a second player may join when the LEFT cabinet's SELECT GAME MODE comes up: with a partner linked, it
 * offers SOLO/LINKED, "SOLO START, BUT PERMITS 2P JOIN-IN" (mode 2 at 0x802F3FD0); without, only SOLO ONLY. Once a game runs in
 * mode 2 its partner may vanish, and a freshly booted right cabinet that links later still takes LINK PLAY and plays alongside.
 * So from power-on, whenever nobody is linked, tc2_link.c's stand-in answers as an idle right cabinet (no second game runs): the
 * player always gets SOLO/LINKED, taking it opens a room, and a partner can join at any time (~10 us a frame, docs/ONLINE.md). In an episode (a partner linked before) it returns only between games:
 * mid-game the arcade still expects that partner back. A partner arriving while we choose waits for the choice (session_begin). */
static bool host_waiting(void){return (mode==MODE_LAN_HOST&&eng_net_hosting())||(mode==MODE_ROOM&&room_owner);}
static void standin_tick(void){
    extern uint8_t tc2_ram[];
    static int off=-1;if(off<0)off=getenv("TC2_NO_STANDIN")!=NULL;
    const bool choosing=tc2_ram[0x2CE92B]==1&&tc2_ram[0x2F3930]==1;
    const int game_mode=tc2_ram[0x2F3FD3];     /* 0x802F3FD0, a 32-bit word: 1 SOLO ONLY, 2 SOLO/LINKED, 0 between games */
    static int prev_mode;
    if(game_mode&&!prev_mode)game_start_frame=sim_frames;
    prev_mode=game_mode;
    tc2_link_set_mark(game_mode==2&&sim_frames-game_start_frame>=1500?'J':game_mode==1?'L':'V');   /* P2's agreed start reads 'J' */
    if(pending_slot>=0){
        if(!eng_net_session_active()){pending_slot=-1;}
        else if(!choosing){int slot=pending_slot;session_begin(slot,2);}
        return;
    }
    const bool partner=eng_net_session_active();
    /* from power-on, and after a partner left once a game is over (mid-game the arcade still expects that partner back) */
    const bool want=!off&&qtc2_cabinet_side==1&&!partner&&(game_mode==0||(!episode&&host_waiting()));
    if(want&&!tc2_link_ghost()){tc2_link_set_ghost(true);fprintf(stderr,"[ONLINE] frame %u: drop-in open: the stand-in answers until a partner links\n",sim_frames);}
    else if(tc2_link_ghost()&&!partner&&(off||qtc2_cabinet_side!=1))tc2_link_set_ghost(false);
    /* SOLO/LINKED taken with nobody online yet: a room opens for this game, and a friend joins it any time */
    static int prev_choice;
    if(game_mode==2&&prev_choice==0&&tc2_link_ghost()&&!connected_any()){
        fprintf(stderr,"[ONLINE] frame %u: SOLO/LINKED taken: hosting a room\n",sim_frames);
        host_room();
        if(room_code[0]){char s[48];snprintf(s,sizeof s,"ROOM %s - FRIENDS JOIN ANY TIME",room_code);say(s,8000);}
    }
    prev_choice=game_mode;
    /* opened by the SOLO/LINKED shot: once the room is open, back to the arcade (held meanwhile), where SOLO/LINKED now opens */
    if(menu_from_mode&&tc2_link_ghost()&&(mode==MODE_LAN_HOST||eng_net_my_room()>0)){
        menu_from_mode=false;qhost_online_menu(false);
        fprintf(stderr,"[ONLINE] frame %u: room open: back to SELECT GAME MODE\n",sim_frames);
        /* the game sets SOLO/LINKED as SELECT GAME MODE comes up: a partner appearing later does not open it (measured: the screen
         * re-armed, shots on the panel did nothing, it ran out into SOLO ONLY). So the arcade restarts (TEST) and the next credit
         * finds the stand-in there. A partner who links meanwhile waits for the restart (session_begin). */
        if(choosing){
            mode_restart=MODE_RESTART_FRAMES;init_frame=sim_frames;
            fprintf(stderr,"[ONLINE] frame %u: SELECT GAME MODE came up without a partner: P1 restarts so SOLO/LINKED opens (TEST)\n",sim_frames);
            say("ROOM OPEN - THE ARCADE RESTARTS, THEN COIN IN AND SHOOT SOLO/LINKED",8000);
        }else say("ROOM OPEN - SHOOT SOLO/LINKED",6000);
    }
}
/* ---- the right cabinet's handshake (measured 2026-11-07, build/online-test/dropin) ----
 * The game links its two cabinets when it powers on (~920 frames in: each side shows its partner state 5, then 1). A LEFT cabinet
 * linked later is fine: hosting 50 s after power-on, the stand-in answered at once and SOLO/LINKED opened. A RIGHT cabinet linked
 * later is not: it sees the left's game (state 4) but its own SOLO/LINKED stays crossed out, while a right cabinet powered on with
 * the link up joined the same game. So P2, linked after its power-on, restarts the game's program once: the TEST switch (10
 * frames) gives the Parental Advisory, then attract, credits cleared; its handshake then runs with the link up. Only between
 * games: a P2 in a game of its own finishes it first. The emulator is not reset; the game does it. */
static void link_contact_tick(void){
    extern uint8_t tc2_ram[];
    if(!p2_init_pending)return;
    if(!eng_net_session_active()||qtc2_cabinet_side!=2){p2_init_pending=false;return;}
    static uint32_t told;
    if(tc2_ram[0x2F3FD3]!=0){if(!told||sim_frames-told>1800){told=sim_frames;say("FINISH YOUR GAME - THEN YOU JOIN P1",6000);}return;}
    p2_init_pending=false;told=0;
    mode_restart=MODE_RESTART_FRAMES;init_frame=sim_frames;
    fprintf(stderr,"[ONLINE] frame %u: P2 linked after its power-on handshake: the cabinet restarts (TEST)\n",sim_frames);
    say("JOINING P1 - THE ARCADE RESTARTS, THEN COIN IN",8000);
}
static void mode_restart_tick(void){
    extern uint16_t tc2_inputs;static bool pressing;
    if(mode_restart>0){tc2_inputs|=0x0008;pressing=true;mode_restart--;}
    else if(pressing){tc2_inputs&=(uint16_t)~0x0008;pressing=false;}
}
static int current_items(int *items);
static bool holding;                       /* the agreed start (below) */

/* ---- test switches (desktop automation; see tests/test_online.py) ----
 * TC2_NET_SCRIPT="<frame>:<action>[=arg];..." (or t<ms>: milliseconds after start, for a headset nobody wears). Actions: host
 * (LAN), join=<addr>, discover, quick, hostroom, joincode=<code>, browse, mute, voice=0|1, leave, freeze=<ms> (the whole app
 * stalls, as a backgrounded headset does). The menu path runs exactly as a player's selection would.
 * On a headset there is no environment: online-test.txt beside the settings (adb run-as on this debuggable build) may hold
 * "script=<as TC2_NET_SCRIPT>" and "status=1". */
static void test_file(int *status,const char **script){
    static char from_file[256];
    FILE *f=fopen("online-test.txt","r");if(!f)return;
    char line[300];
    while(fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"script=",7)){snprintf(from_file,sizeof from_file,"%s",line+7);*script=from_file;}
        else if(!strcmp(line,"status=1"))*status=1;
        else if(!strncmp(line,"voice_out=",10))tc2_voice_test_output(line+10);
    }
    fclose(f);fprintf(stderr,"[ONLINE] online-test.txt: script '%s'\n",*script?*script:"");
}
static int test_status=-1;static const char *test_script;static uint32_t test_t0,test_last_ms;
static void test_switches_at(bool timed){
    if(test_status<0){test_status=getenv("TC2_NET_STATUS")!=NULL;test_script=getenv("TC2_NET_SCRIPT");test_file(&test_status,&test_script);test_t0=SDL_GetTicks();}
    uint32_t now=SDL_GetTicks()-test_t0,since=test_last_ms;if(timed)test_last_ms=now;
    for(const char *p=test_script;p&&*p;){
        bool ms=*p=='t';if(ms)p++;
        unsigned at=(unsigned)strtoul(p,(char**)&p,10);
        if(*p!=':')break;
        p++;
        char action[64];size_t n=strcspn(p,";");if(n>=sizeof action)n=sizeof action-1;memcpy(action,p,n);action[n]=0;p+=n;if(*p==';')p++;
        if(ms!=timed)continue;
        if(timed?!(at>since&&at<=now):at!=sim_frames)continue;
        char *arg=strchr(action,'=');if(arg)*arg++=0;
        fprintf(stderr,"[ONLINE] test frame %u: %s%s%s\n",sim_frames,action,arg?" ":"",arg?arg:"");
        if(!strcmp(action,"host"))host_lan();
        else if(!strcmp(action,"join")&&arg)join_address(arg);
        else if(!strcmp(action,"discover")){eng_net_discover();eng_net_discover_autojoin(1);mode=MODE_JOIN;eng_net_room_request=0;}
        else if(!strcmp(action,"quick"))quick_match();
        else if(!strcmp(action,"hostroom"))host_room();
        else if(!strcmp(action,"joincode")&&arg)join_code(arg);
        else if(!strcmp(action,"browse"))browse_open();
        else if(!strcmp(action,"pickroom")){   /* the browser's first joinable room, chosen the way the menu does */
            int items[40],count=current_items(items);
            for(int i=0;i<count;i++)if(items[i]>=ITEM_ROOM0&&items[i]<ITEM_LAN0){const tc2_room *r=tc2_rooms_get(items[i]-ITEM_ROOM0);
                if(r&&r->version==LINK_VERSION&&!r->state&&r->players<r->max_players){cursor=i;tc2_online_menu_select();break;}}
        }
        else if(!strcmp(action,"mute"))tc2_online_toggle_mute();
        else if(!strcmp(action,"voice")&&arg){cfg_voice=atoi(arg)!=0;apply_voice_cfg();}
        else if(!strcmp(action,"leave"))leave();
        else if(!strcmp(action,"freeze")&&arg)SDL_Delay((Uint32)atoi(arg));
    }
    if(!timed&&test_status&&sim_frames%300==0){
        tc2_link_status s;tc2_link_get_status(&s);char net[96];eng_net_status(net,sizeof net);
        tc2_voice_status v;tc2_voice_get_status(&v);extern uint8_t tc2_ram[];
        fprintf(stderr,"[ONLINE] frame %u side P%d link %s alive %d lag %u state %u tx %u rx %u drop %u bad %u/%u game-errors %u ping %d"
                " | voice %s sent %u received %u lost %u | room %d %s | partner-state %d self %d hold %d partner-voice %d | %s\n",
            sim_frames,qtc2_cabinet_side,s.enabled?(tc2_link_ghost()?"stand-in":"on"):"off",s.alive,s.lag,s.state,s.tx,s.rx,s.rx_dropped,s.tx_bad,s.rx_bad,s.game_errors,eng_net_ping_ms(),
            v.active?"on":"off",v.sent,v.received,v.lost,room_id,room_code[0]?room_code:"-",tc2_ram[0x2F3502],tc2_link_self_state(),holding?1:0,v.partner_has_voice?1:0,net);
        if(screen==SCREEN_BROWSE)for(int i=0;i<tc2_rooms_count();i++){const tc2_room *r=tc2_rooms_get(i);
            fprintf(stderr,"[ONLINE] room %d '%s' code %s %d/%d %s v%d\n",r->id,r->name,r->code[0]?r->code:"-",r->players,r->max_players,r->state?"playing":"lobby",r->version);}
    }
}

/* link health warnings over the game */
static void health_tick(void){
    static uint32_t bad_since,warned;
    if(!eng_net_session_active())return;
    tc2_link_status s;tc2_link_get_status(&s);
    uint32_t now=SDL_GetTicks();
    if(s.lag>6&&s.alive){if(!bad_since)bad_since=now;}else bad_since=0;
    if(bad_since&&now-bad_since>1500&&now-warned>8000){say("PARTNER CONNECTION IS LAGGING",3000);warned=now;}
    static bool denied_said;tc2_voice_status v;tc2_voice_get_status(&v);
    if(v.mic_denied&&!denied_said){denied_said=true;say("NO MICROPHONE ACCESS - LISTEN ONLY",5000);}
}

/* ---- the agreed start (from upstream namco22-decompile 0.9.3+, tc2/src/tc2_link.c, MIT): the game's byte 0x802F3502 is the
 * PARTNER's state as this cabinet sees it (1 attract, 2 credited / selecting, 4 in a game). Upstream measured (LAN and zonesync):
 * the right cabinet's kills only count when the LEFT starts its game first and the right is credited once that game is under
 * way; credited first, or within ~6 s, the right shows its partner as "COM" and its score freezes. So the right cabinet's coin,
 * trigger and pedal are held until it is in a game, while the left has not been in its game for TC2_JOIN_WAIT frames (1500 = 25 s,
 * or the same in wall-clock time over a slow link). Coins put in meanwhile are counted and paid in afterwards, 4 frames on and 36
 * off each. The right cabinet learns its own state from the left's trailer; without one (unknown), nothing is held. ---- */
static bool holding;static int coin_pending,coin_pulse,coin_fast,prev_coin;static uint32_t p_since,p_since_ms;static int p_prev=-1;
static void start_hold_tick(void){
    extern uint8_t tc2_ram[];extern uint16_t tc2_inputs;extern uint32_t rr_frame;
    static long wait=-1;if(wait<0){const char *e=getenv("TC2_JOIN_WAIT");wait=e?atol(e):1500;}
    const int pst=tc2_ram[0x2F3502],self=tc2_link_self_state();
    if(pst!=p_prev){p_prev=pst;p_since=rr_frame;p_since_ms=SDL_GetTicks();}
    const uint32_t left_ms=(uint32_t)(wait*1000/60),in_ms=SDL_GetTicks()-p_since_ms;
    const bool left_done=pst==4&&(rr_frame-p_since>=(uint32_t)wait||in_ms>=left_ms||tc2_link_partner_mark()=='J');
    bool h=eng_net_session_active()&&eng_net_slot()==1&&self>=0&&self!=4&&!left_done;
    const int c=tc2_inputs&0x0001;                       /* the raw coin, before the hold (tc2_h8.c reads it through tc2_link_hold_start) */
    if(h&&c&&!prev_coin)coin_pending++;
    prev_coin=c;
    static int gap;
    if(coin_pulse>0){if(--coin_pulse==0)coin_pulse=gap;}
    else if(coin_pulse<0)coin_pulse++;
    else if(!h&&coin_pending>0){   /* coins kept over a restart go in as a coin mech's (6 on, 6 off): the game counts them all */
        const bool fast=coin_fast>0;coin_pending--;if(fast)coin_fast--;
        coin_pulse=fast?6:4;gap=fast?-6:-36;}
    if(h!=holding){
        holding=h;
        fprintf(stderr,"[ONLINE] start %s (we are %d, partner %d)\n",h?"HELD":"free",self,pst);
        if(h||self!=4)say(h?(pst==4?"P1 IS STARTING - YOU JOIN IN A MOMENT":"P1 STARTS FIRST, THEN YOU JOIN"):"PULL THE TRIGGER TO JOIN YOUR PARTNER",h?6000:4000);
    }
    if(h&&pst==4&&rr_frame%120==0){
        char t[48];uint32_t r=in_ms<left_ms?(left_ms-in_ms+999)/1000:0;
        snprintf(t,sizeof t,"P1 IS STARTING - YOU JOIN IN %u S",(unsigned)r);say(t,2500);
    }
}
/* The coins a drop-in restart cleared, paid in as the held ones are once attract is back and the room is open (or could not
 * open): the last one completes the credit and SELECT GAME MODE comes up by itself, now with SOLO/LINKED. */
static void repay_tick(void){
    if(repay&&await_trigger){   /* SELECT GAME MODE ran out (menu_timeout_tick): the intro plays until the trigger is pulled */
        extern uint16_t tc2_inputs;static bool prev,pulled;static uint32_t told;
        const bool t=(tc2_inputs&0x0010)!=0;if(t&&!prev&&!mode_restart)pulled=true;prev=t;
        if(mode_restart||sim_frames-init_frame<COINS_AFTER_RESTART)return;
        if(!pulled){if(sim_frames-told>=1800){told=sim_frames;say("COINS KEPT - PULL THE TRIGGER TO PLAY",6000);}return;}
        fprintf(stderr,"[ONLINE] frame %u: trigger: %d coins paid back, SELECT GAME MODE again\n",sim_frames,repay);
        coin_pending+=repay;coin_fast+=repay;repay=0;await_trigger=false;told=0;pulled=false;
        return;
    }
    if(!repay||mode_restart||sim_frames-init_frame<REPAY_AFTER_FRAMES)return;
    const bool open=eng_net_my_room()>0||eng_net_session_active()||(mode==MODE_LAN_HOST&&eng_net_hosting());
    if(!open&&host_waiting()&&sim_frames-init_frame<REPAY_AFTER_FRAMES+900)return;   /* the server is slow: wait up to 15 s more */
    fprintf(stderr,"[ONLINE] frame %u: %d coins paid back (%s)\n",sim_frames,repay,open?"room open":"no room");
    coin_pending+=repay;repay=0;
    char s[64];snprintf(s,sizeof s,"ROOM %s: SHOOT SOLO/LINKED - FRIENDS JOIN ANY TIME",room_code);
    say(!open?"COULD NOT OPEN A ROOM - SOLO ONLY THIS TIME":room_code[0]?s:"SHOOT SOLO/LINKED - YOUR PARTNER JOINS ANY TIME",8000);
}
/* The I/O board's reads (upstream src/tc2_h8.c io_switches): the coin, trigger and pedal held, and a held coin paid in. */
int tc2_link_hold_start(void){return holding;}
int tc2_link_coin_inject(void){return coin_pulse>0;}
/* stall-and-wait, as upstream: neither cabinet runs more than a few frames ahead of the other. The VR host asks before each
 * arcade frame and keeps rendering the headset meanwhile (upstream blocks in eng_net_wait; a headset must not). */
bool tc2_online_may_advance(void){
    if(!inited||!eng_net_session_active())return true;
    static int lead=-2;if(lead==-2){const char *e=getenv("TC2_NET_LEAD");lead=e?atoi(e):-1;}
    if(lead>=1000)return true;
    if(eng_net_may_advance(lead<0?eng_net_auto_lead():lead,1000))return true;
    eng_net_poll_paused();
    return false;
}

void tc2_online_frame(void){
    if(!inited)return;
    sim_frames++;
    test_switches_at(false);
    test_switches_at(true);
    lobby_tick();
    tc2_voice_set_active(eng_net_session_active());
    tc2_voice_tick();
    eng_net_poll();
    start_hold_tick();
    coins_tick();
    menu_timeout_tick();
    standin_tick();
    link_contact_tick();
    mode_restart_tick();
    repay_tick();
    mode_screen_tick();
    episode_tick();
    code_join_tick();
    browse_tick();
    health_tick();
    last_poll_ms=SDL_GetTicks();
}
void tc2_online_service(void){
    if(!inited)return;
    uint32_t now=SDL_GetTicks();
    if(now-last_poll_ms<30)return;
    test_switches_at(true);
    lobby_tick();
    eng_net_poll_paused();
    standin_tick();
    episode_tick();
    code_join_tick();
    browse_tick();
    last_poll_ms=now;
}

/* ---- the ONLINE page ---- */
static char edit_buf[24];
static const char name_chars[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";

static bool connected_any(void){return eng_net_hosting()||eng_net_connected()||mode!=MODE_NONE||episode;}
static int current_items(int *items){
    int n=0;
    if(connected_any()){
        items[n++]=ITEM_VOICE;if(cfg_voice){items[n++]=ITEM_MIC;items[n++]=ITEM_VOLUME;}
        items[n++]=ITEM_LEAVE;return n;
    }
    if(screen==SCREEN_BROWSE){
        for(int i=0;i<tc2_rooms_count()&&n<20;i++)items[n++]=ITEM_ROOM0+i;
        for(int i=0;i<eng_net_found_count()&&i<6;i++)items[n++]=ITEM_LAN0+i;
        items[n++]=ITEM_REFRESH;items[n++]=ITEM_BACK;return n;
    }
    items[n++]=ITEM_HOST_ROOM;items[n++]=ITEM_JOIN_CODE;items[n++]=ITEM_BROWSE;items[n++]=ITEM_HOST_LAN;items[n++]=ITEM_JOIN_IP;
    items[n++]=ITEM_QUICK;items[n++]=ITEM_VOICE;items[n++]=ITEM_NAME;return n;
}
bool tc2_online_menu_editing(void){return edit_item>=0;}
static void set_voice(bool on){cfg_voice=on;apply_voice_cfg();save_cfg();say(on?"VOICE CHAT ON":"VOICE CHAT OFF",2000);}
static void set_volume(int v){cfg_volume=v<0?0:v>200?200:v;tc2_voice_set_volume(cfg_volume);save_cfg();}
void tc2_online_menu_move(int d){
    if(edit_item>=0){
        if(edit_item==ITEM_JOIN_IP){
            int octet=edit_pos/3,digit=edit_pos%3,v=join_ip[octet];int scale=digit==0?100:digit==1?10:1;
            int cur=(v/scale)%10,next=(cur+(d>0?1:9))%10;v+= (next-cur)*scale;if(v>255)v=255;if(v<0)v=0;join_ip[octet]=(uint8_t)v;
        }else{
            const char *set=edit_item==ITEM_JOIN_CODE?tc2_room_code_chars:name_chars;
            const char *p=edit_buf[edit_pos]?strchr(set,edit_buf[edit_pos]):NULL;int i=p?(int)(p-set):0,n=(int)strlen(set);
            i=(i+(d>0?1:n-1))%n;edit_buf[edit_pos]=set[i];
        }
        return;
    }
    int items[40],n=current_items(items);cursor=(cursor-d+n)%n;   /* stick up = previous item */
}
void tc2_online_menu_adjust(int d){
    if(edit_item<0){
        int items[40],n=current_items(items);if(cursor>=n)return;
        if(items[cursor]==ITEM_VOLUME)set_volume(cfg_volume+d*10);
        return;
    }
    int max=edit_item==ITEM_JOIN_IP?11:edit_item==ITEM_JOIN_CODE?3:NAME_MAX_LEN-1;
    edit_pos+=d;if(edit_pos<0)edit_pos=0;if(edit_pos>max)edit_pos=max;
}
void tc2_online_menu_select(void){
    if(edit_item==ITEM_JOIN_IP){
        edit_item=-1;save_cfg();char a[32];snprintf(a,sizeof a,"%u.%u.%u.%u:%d",join_ip[0],join_ip[1],join_ip[2],join_ip[3],join_port);join_address(a);return;
    }
    if(edit_item==ITEM_NAME){
        edit_item=-1;edit_buf[NAME_MAX_LEN]=0;clean_name(edit_buf);save_cfg();eng_net_set_name(name);return;
    }
    if(edit_item==ITEM_JOIN_CODE){edit_item=-1;edit_buf[4]=0;join_code(edit_buf);return;}
    int items[40],n=current_items(items);if(cursor>=n)cursor=n-1;
    int it=items[cursor];
    switch(it){
    case ITEM_HOST_ROOM:host_room();break;
    case ITEM_JOIN_CODE:edit_item=ITEM_JOIN_CODE;edit_pos=0;memset(edit_buf,0,sizeof edit_buf);memcpy(edit_buf,"AAAA",4);break;
    case ITEM_BROWSE:browse_open();cursor=0;break;
    case ITEM_HOST_LAN:host_lan();break;
    case ITEM_JOIN_IP:edit_item=ITEM_JOIN_IP;edit_pos=0;break;
    case ITEM_QUICK:quick_match();break;
    case ITEM_VOICE:set_voice(!cfg_voice);break;
    case ITEM_MIC:tc2_online_toggle_mute();break;
    case ITEM_VOLUME:set_volume(cfg_volume>=200?50:cfg_volume+25);break;
    case ITEM_NAME:edit_item=ITEM_NAME;edit_pos=0;memset(edit_buf,0,sizeof edit_buf);snprintf(edit_buf,sizeof edit_buf,"%-12s",name);break;
    case ITEM_LEAVE:leave();cursor=0;screen=SCREEN_MAIN;break;
    case ITEM_REFRESH:browse_open();break;
    case ITEM_BACK:screen=SCREEN_MAIN;cursor=2;tc2_rooms_close();break;
    default:
        if(it>=ITEM_LAN0){char addr[64];if(eng_net_found(it-ITEM_LAN0,NULL,0,addr,sizeof addr)){screen=SCREEN_MAIN;cursor=0;join_address(addr);}}
        else if(it>=ITEM_ROOM0){
            const tc2_room *r=tc2_rooms_get(it-ITEM_ROOM0);if(!r)break;
            if(r->version!=LINK_VERSION)say("THAT ROOM RUNS ANOTHER VERSION",4000);
            else if(r->state)say("THAT ROOM IS ALREADY PLAYING",4000);
            else if(r->players>=r->max_players)say("THAT ROOM IS FULL",4000);
            else{char code[5];snprintf(code,sizeof code,"%s",r->code);screen=SCREEN_MAIN;cursor=0;tc2_rooms_close();join_room(r->id,code);}
        }
    }
}
void tc2_online_menu_back(void){
    if(edit_item>=0){edit_item=-1;load_cfg();return;}
    if(screen==SCREEN_BROWSE){screen=SCREEN_MAIN;cursor=2;tc2_rooms_close();}
}

static void upper(char *s){for(;*s;s++)*s=(char)toupper((unsigned char)*s);}
static void meter(char *out,int level){int bars=level/350;if(bars>8)bars=8;for(int i=0;i<8;i++)out[i]=i<bars?'|':'.';out[8]=0;}
unsigned tc2_online_menu_lines(char text[TC2_ONLINE_LINES][TC2_ONLINE_COLUMNS],int style[TC2_ONLINE_LINES]){
    char status[10][TC2_ONLINE_COLUMNS];int status_style[10],ns=0;
    #define STATUS(st,...) do{if(ns<10){snprintf(status[ns],TC2_ONLINE_COLUMNS,__VA_ARGS__);upper(status[ns]);status_style[ns++]=(st);}}while(0)
    int items[40],count=current_items(items);if(cursor>=count)cursor=count-1;if(cursor<0)cursor=0;
    tc2_voice_status v;tc2_voice_get_status(&v);
    if(tc2_online_session()){
        tc2_link_status s;tc2_link_get_status(&s);
        if(eng_net_session_active())STATUS(TC2_LINE_GOOD,"LINKED - YOU ARE P%d %s",qtc2_cabinet_side,qtc2_cabinet_side==2?"ROBERT":"KEITH");
        else STATUS(TC2_LINE_WARN,"%s - YOU ARE P%d",eng_net_connected()||eng_net_hosting()?"WAITING FOR PARTNER":"RECONNECTING",qtc2_cabinet_side);
        for(int i=0;i<eng_net_roster_count();i++){int r,self;char nm[24];if(eng_net_roster(i,nm,sizeof nm,&r,&self)&&!self)STATUS(TC2_LINE_TEXT,"PARTNER: %s (P%d)",nm,eng_net_roster_slot(i)==1?2:1);}
        int ping=eng_net_ping_ms();
        if(ping>=0)STATUS(s.alive?TC2_LINE_TEXT:TC2_LINE_WARN,"PING %d MS  LINK %s",ping,s.alive?(s.lag>6?"LAGGING":"GOOD"):"WAITING");
        else STATUS(s.alive?TC2_LINE_TEXT:TC2_LINE_WARN,"LINK %s",s.alive?"GOOD":"WAITING");
        if(room_code[0])STATUS(TC2_LINE_TEXT,"ROOM CODE %s",room_code);
        if(cfg_voice&&v.active&&!v.partner_has_voice&&eng_net_session_active())STATUS(TC2_LINE_HINT,"PARTNER'S GAME HAS NO VOICE CHAT");
        else if(cfg_voice&&v.active){char a[10],b[10];meter(a,cfg_muted?0:v.level);meter(b,v.partner_level*3);
            STATUS(v.mic_denied?TC2_LINE_WARN:TC2_LINE_TEXT,"YOU %s  PARTNER %s",v.mic_denied?"NO MIC  ":cfg_muted?"MUTED   ":a,b);}
        STATUS(TC2_LINE_HINT,"THE GAME RUNS ON WHILE THIS IS OPEN");
    }else if(connected_any()){
        if(eng_net_hosting()){
            STATUS(TC2_LINE_GOOD,"HOSTING ON PORT 27750");
            if(!address_count)find_addresses();
            for(int i=0;i<address_count;i++)STATUS(TC2_LINE_TEXT,"ADDRESS: %s%s",addresses[i],vpn_address(addresses[i])?" (VPN)":"");
        }else if(mode==MODE_ROOM&&room_code[0]){
            STATUS(eng_net_connected()?TC2_LINE_GOOD:TC2_LINE_WARN,"ROOM CODE:  %s",room_code);
            STATUS(TC2_LINE_HINT,eng_net_connected()?"YOUR FRIEND: JOIN ROOM CODE":"OPENING THE ROOM...");
            if(menu_from_mode&&eng_net_connected())STATUS(TC2_LINE_HINT,"THEN THEY PUT COINS IN");
        }else if(eng_net_connected())STATUS(TC2_LINE_GOOD,"CONNECTED TO %s",mode==MODE_QUICK?server:"HOST");
        else STATUS(TC2_LINE_WARN,eng_net_discovering()?"SEARCHING...":"CONNECTING...");
        int rc=eng_net_roster_count();
        for(int i=0;i<rc;i++){int r,self;char nm[24];if(eng_net_roster(i,nm,sizeof nm,&r,&self))STATUS(TC2_LINE_TEXT,"P%d %s%s",eng_net_roster_slot(i)==1?2:1,nm,self?" (YOU)":"");}
        if(eng_net_connected())STATUS(TC2_LINE_HINT,rc<2?"WAITING FOR A PARTNER...":"STARTING LINK...");
    }else if(screen==SCREEN_BROWSE){
        const char *sn=tc2_rooms_server_name();
        if(tc2_rooms_answered())STATUS(TC2_LINE_TEXT,"%s: %d ROOMS, %d ONLINE",sn[0]?sn:server,tc2_rooms_count(),tc2_rooms_server_players());
        else STATUS(TC2_LINE_WARN,tc2_rooms_busy()?"ASKING %s...":"%s DOES NOT ANSWER",server);
        if(eng_net_discovering())STATUS(TC2_LINE_HINT,"SEARCHING THE LOCAL NETWORK...");
        if(!tc2_rooms_count()&&!eng_net_found_count()&&!tc2_rooms_busy())STATUS(TC2_LINE_HINT,"NO GAMES YET - HOST ONE!");
    }else{
        STATUS(TC2_LINE_HINT,"NAME: %s",name);
        if(menu_from_mode)STATUS(TC2_LINE_GOOD,"HOST: YOU ARE P1, YOUR PARTNER JOINS");
        if(pending_code[0])STATUS(TC2_LINE_WARN,"LOOKING FOR ROOM %s...",pending_code);
    }
    #undef STATUS

    for(int i=0;i<TC2_ONLINE_LINES;i++){text[i][0]=0;style[i]=TC2_LINE_TEXT;}
    int n=0;
    #define LINE(st,...) do{if(n<TC2_ONLINE_LINES){snprintf(text[n],TC2_ONLINE_COLUMNS,__VA_ARGS__);upper(text[n]);style[n++]=(st);}}while(0)
    LINE(TC2_LINE_TITLE,screen==SCREEN_BROWSE&&!connected_any()?"ONLINE 4/4 - FIND A GAME":"ONLINE 2-PLAYER  4/4");
    for(int i=0;i<ns;i++)LINE(status_style[i],"%s",status[i]);
    LINE(TC2_LINE_TEXT,"%s","");
    /* the items, scrolled so the cursor stays visible above the hint line */
    int room=TC2_ONLINE_LINES-n-1;if(room<3)room=3;
    int first=cursor-room/2;if(first>count-room)first=count-room;if(first<0)first=0;
    for(int i=first;i<count&&i<first+room;i++){
        char label[TC2_ONLINE_COLUMNS];int it=items[i];
        switch(it){
        case ITEM_HOST_ROOM:snprintf(label,sizeof label,"HOST ONLINE ROOM (CODE)");break;
        case ITEM_JOIN_CODE:
            if(edit_item==ITEM_JOIN_CODE){char m[16];int k=0;for(int j=0;j<4;j++){char c=edit_buf[j]?edit_buf[j]:'A';if(j==edit_pos){m[k++]='[';m[k++]=c;m[k++]=']';}else{m[k++]=' ';m[k++]=c;m[k++]=' ';}}m[k]=0;snprintf(label,sizeof label,"ROOM CODE %s",m);}
            else snprintf(label,sizeof label,"JOIN ROOM CODE");
            break;
        case ITEM_BROWSE:snprintf(label,sizeof label,"BROWSE GAMES");break;
        case ITEM_HOST_LAN:snprintf(label,sizeof label,"HOST ON THIS NETWORK");break;
        case ITEM_JOIN_IP:
            if(edit_item==ITEM_JOIN_IP){char ip[16];snprintf(ip,sizeof ip,"%03u.%03u.%03u.%03u",join_ip[0],join_ip[1],join_ip[2],join_ip[3]);
                char marked[24];int k=0;for(int j=0;ip[j];j++){int dpos=j-j/4;if(ip[j]!='.'&&dpos==edit_pos){marked[k++]='[';marked[k++]=ip[j];marked[k++]=']';}else marked[k++]=ip[j];}marked[k]=0;
                snprintf(label,sizeof label,"JOIN %s",marked);}
            else snprintf(label,sizeof label,"JOIN IP %u.%u.%u.%u",join_ip[0],join_ip[1],join_ip[2],join_ip[3]);
            break;
        case ITEM_QUICK:snprintf(label,sizeof label,"QUICK MATCH (ANYONE)");break;
        case ITEM_VOICE:snprintf(label,sizeof label,"VOICE CHAT: %s",cfg_voice?"ON":"OFF");break;
        case ITEM_MIC:snprintf(label,sizeof label,"MICROPHONE: %s",cfg_muted?"MUTED":"ON");break;
        case ITEM_VOLUME:snprintf(label,sizeof label,"PARTNER VOLUME: %d%%  < >",cfg_volume);break;
        case ITEM_NAME:
            if(edit_item==ITEM_NAME){char m[40];int k=0;for(int j=0;j<NAME_MAX_LEN;j++){char c=edit_buf[j]?edit_buf[j]:' ';if(j==edit_pos){m[k++]='[';m[k++]=c;m[k++]=']';}else m[k++]=c;}m[k]=0;snprintf(label,sizeof label,"NAME %s",m);}
            else snprintf(label,sizeof label,"CHANGE NAME");
            break;
        case ITEM_LEAVE:snprintf(label,sizeof label,episode?"LEAVE ONLINE GAME":eng_net_hosting()||mode==MODE_ROOM?"CLOSE ROOM":"CANCEL");break;
        case ITEM_REFRESH:snprintf(label,sizeof label,"REFRESH");break;
        case ITEM_BACK:snprintf(label,sizeof label,"BACK");break;
        default:
            if(it>=ITEM_LAN0){char l[96],a[64];eng_net_found(it-ITEM_LAN0,l,sizeof l,a,sizeof a);
                char nm[20];sscanf(l,"%19s",nm);snprintf(label,sizeof label,"LAN %.12s %.18s",nm,a);}
            else{const tc2_room *r=tc2_rooms_get(it-ITEM_ROOM0);
                if(r)snprintf(label,sizeof label,"%-17.17s %d/%d %s",r->name,r->players,r->max_players,
                              r->version!=LINK_VERSION?"OLD":r->state?"PLAYING":r->players>=r->max_players?"FULL":"JOIN");
                else snprintf(label,sizeof label,"-");}
        }
        LINE(i==cursor?TC2_LINE_SELECTED:TC2_LINE_ITEM,"%c %s",i==cursor?'>':' ',label);
    }
    if(edit_item>=0)LINE(TC2_LINE_HINT,"STICK: EDIT TRIGGER: OK L-CLICK: BACK");
    else LINE(TC2_LINE_HINT,count>room?"R-STICK: MORE  TRIGGER: SELECT":"R-STICK UP/DOWN + TRIGGER: SELECT");
    #undef LINE
    /* a content hash: the panel uploads only on change */
    unsigned h=2166136261u;
    for(int i=0;i<TC2_ONLINE_LINES;i++){for(const char *c=text[i];*c;c++)h=(h^(unsigned char)*c)*16777619u;h=(h^(unsigned)style[i])*16777619u;}
    return h;
}

bool tc2_online_toast(char *text,size_t n,unsigned *serial){
    if(!toast[0]||(int32_t)(SDL_GetTicks()-toast_until)>0)return false;
    snprintf(text,n,"%s",toast);if(serial)*serial=toast_serial;return true;
}
