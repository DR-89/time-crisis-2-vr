/* tc2_online.c -- online twin-cabinet sessions for Time Crisis II VR. See tc2_online.h and docs/ONLINE.md.
 *
 * Transport: upstream's NMN2 client (engine/net.c) and built-in host (engine/net_host.c), game id "t2". One FRAME a game frame
 * carries the newest C422 packet (tc2_link.c), 256 bytes, so stock nmn-server rooms and the built-in host carry it unchanged.
 * The lobby slot is the cabinet: slot 0 = P1 Keith (left/red), slot 1 = P2 Robert (right/blue). Joining is Ready; the lowest slot
 * starts the session as soon as both are in. Nothing is lockstepped: the arcade's own link protocol (it declares the partner lost
 * after 16 frames without a packet) copes with loss and latency, as the real cabinet cable did. */
#include "tc2_online.h"
#include "tc2_link.h"
#include "tc2_cabinet.h"
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
#define LINK_VERSION    1

static void session_begin(int slot,int players);
static void session_end(void);
static int  frame_out(uint8_t *buf);
static void frame_in(int slot,const uint8_t *buf,uint32_t seq);
static void net_hint(const char *text,int frames);
static const eng_net_game game={"t2",LINK_VERSION,2,TC2_LINK_PAYLOAD,"TC2_NET","game",0,
    session_begin,session_end,frame_out,frame_in,net_hint};

static char name[NAME_MAX_LEN+1]="PLAYER";
static char server[64]=DEFAULT_SERVER;
static uint8_t join_ip[4]={192,168,1,2};
static int join_port=27750;
static bool inited;
static int home_side=1;                    /* the cabinet the player chose offline (restored after a session) */
static uint32_t sim_frames,last_poll_ms;
static uint32_t start_sent_ms;
static bool ready_sent;
static enum { MODE_NONE,MODE_HOST,MODE_JOIN,MODE_SERVER } mode;

/* toast */
static char toast[64];static uint32_t toast_until,toast_serial;
static void say(const char *text,uint32_t ms){
    snprintf(toast,sizeof toast,"%s",text);for(char *c=toast;*c;c++)*c=(char)toupper((unsigned char)*c);
    toast_until=SDL_GetTicks()+ms;toast_serial++;
    fprintf(stderr,"[ONLINE] %s\n",toast);
}

/* ---- config ---- */
static void save_cfg(void){
    FILE *f=fopen(CFG_PATH,"w");if(!f)return;
    fprintf(f,"name=%s\nserver=%s\njoin=%u.%u.%u.%u:%d\n",name,server,join_ip[0],join_ip[1],join_ip[2],join_ip[3],join_port);fclose(f);
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
    }
    fclose(f);
}

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
static char target[64];                   /* the address joined (reconnects go back there) */
static uint32_t partner_gone_ms,lost_ms,retry_ms,join_started_ms;

static void partner_name(char *out,size_t n){
    snprintf(out,n,"PARTNER");
    for(int i=0;i<eng_net_roster_count();i++){int ready,self;char nm[24];if(eng_net_roster(i,nm,sizeof nm,&ready,&self)&&!self){snprintf(out,n,"%s",nm);return;}}
}
static void session_begin(int slot,int players){
    (void)players;
    int side=slot==1?2:1;char partner[24],line[64];partner_name(partner,sizeof partner);
    if(episode&&tc2_link_enabled()&&side==qtc2_cabinet_side){snprintf(line,sizeof line,"%s IS BACK - LINKED AGAIN",partner);say(line,4000);}
    else{
        if(episode&&side!=qtc2_cabinet_side)fprintf(stderr,"[ONLINE] the lobby moved this cabinet from P%d to P%d\n",qtc2_cabinet_side,side);
        qtc2_cabinet_side=side;tc2_link_set_enabled(true);
        snprintf(line,sizeof line,"LINKED WITH %s - P%d %s",partner,side,side==2?"ROBERT":"KEITH");say(line,6000);
    }
    if(!eng_net_hosting())snprintf(target,sizeof target,"%s",eng_net_server());   /* also a LAN host found by discovery */
    episode=true;partner_gone_ms=lost_ms=0;
}
static void session_end(void){ready_sent=false;start_sent_ms=0;}   /* the link stays up; the lobby re-links (see lobby_tick) */
static int frame_out(uint8_t *buf){return tc2_link_pack(buf);}
static void frame_in(int slot,const uint8_t *buf,uint32_t seq){(void)slot;(void)seq;tc2_link_unpack(buf);}
static void net_hint(const char *text,int frames){
    /* the library's own lobby notes ("back in the lobby", "rejoining") are internal while an episode recovers by itself */
    if(episode)fprintf(stderr,"[ONLINE] (net) %s\n",text);else say(text,(uint32_t)(frames>0?frames*17:4000));
}

void tc2_online_init(void){
    if(inited)return;inited=true;
    home_side=qtc2_cabinet_side;
    load_cfg();
    eng_net_init(&game);
    eng_net_set_name(name);
    eng_net_preset_server(server);
    fprintf(stderr,"[ONLINE] ready: name %s, server %s, link version %d\n",name,server,LINK_VERSION);
}
bool tc2_online_session(void){return inited&&(eng_net_session_active()||episode);}
bool tc2_online_busy(void){return inited&&(eng_net_hosting()||eng_net_connected()||eng_net_discovering()||mode!=MODE_NONE||episode);}

static void end_episode(void){
    if(tc2_link_enabled())tc2_link_set_enabled(false);
    qtc2_cabinet_side=home_side;episode=false;partner_gone_ms=lost_ms=retry_ms=0;
}
static void leave(void){
    bool hosting=eng_net_hosting();
    if(hosting)eng_net_host_stop();else eng_net_disconnect();
    mode=MODE_NONE;ready_sent=false;start_sent_ms=0;join_started_ms=0;
    bool was=episode;end_episode();
    say(was?"LEFT THE ONLINE GAME - SINGLE PLAYER":hosting?"STOPPED HOSTING":"OFFLINE",3500);
}
static void host(void){
    eng_net_set_name(name);
    if(eng_net_host_start()){mode=MODE_HOST;find_addresses();say("HOSTING - WAITING FOR A PARTNER",4000);}
    else say("CANNOT HOST: PORT 27750 IS IN USE",5000);
}
/* Re-using the resolved address matters: a reconnect every few seconds must not block the headset's frame on DNS. Connecting
 * while a connection attempt runs is a no-op (the library retransmits its HELLO itself). */
static bool connect_to(const char *address){
    eng_net_set_name(name);
    if(!address||!address[0])return false;
    if(strcmp(eng_net_server(),address)!=0&&!eng_net_set_server(address))return false;
    return eng_net_connect();
}
static void join_address(const char *address){
    snprintf(target,sizeof target,"%s",address);
    if(connect_to(address)){mode=MODE_JOIN;join_started_ms=SDL_GetTicks();char s[64];snprintf(s,sizeof s,"CONNECTING TO %s",address);say(s,3000);}
    else say("CANNOT REACH THAT ADDRESS",5000);
}
static void quick_match(void){
    snprintf(target,sizeof target,"%s",server);
    if(connect_to(server)){mode=MODE_SERVER;join_started_ms=SDL_GetTicks();say("QUICK MATCH: LOOKING FOR A PARTNER",4000);}
    else say("CANNOT REACH THE SERVER",5000);
}

/* lobby automation: joining means ready; the lowest slot starts once both are in */
static void lobby_tick(void){
    if(!eng_net_connected()||eng_net_session_active())return;
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
        bool was_server=mode==MODE_SERVER;
        mode=MODE_NONE;join_started_ms=0;eng_net_disconnect();
        say(was_server?"NO ANSWER FROM THE SERVER":"NO ANSWER - CHECK ADDRESS / PORT 27750",6000);return;
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
        if(now-lost_ms>60000){eng_net_disconnect();mode=MODE_NONE;end_episode();say("COULD NOT RECONNECT - SINGLE PLAYER",6000);}
    }else lost_ms=0;
}

/* ---- test switches (desktop automation; see tests/test_online.py) ---- */
/* TC2_NET_SCRIPT="<frame>:<action>[=arg];..." actions: host, join=<addr>, discover, quick, leave, freeze=<ms> (the whole app
 * stalls, as a backgrounded headset does). The menu path runs exactly as a player's selection would. */
static void test_switches(void){
    static int status=-1;static const char *script;
    if(status<0){status=getenv("TC2_NET_STATUS")!=NULL;script=getenv("TC2_NET_SCRIPT");}
    for(const char *p=script;p&&*p;){
        unsigned at=(unsigned)strtoul(p,(char**)&p,10);
        if(*p!=':')break;
        p++;
        char action[64];size_t n=strcspn(p,";");if(n>=sizeof action)n=sizeof action-1;memcpy(action,p,n);action[n]=0;p+=n;if(*p==';')p++;
        if(at!=sim_frames)continue;
        char *arg=strchr(action,'=');if(arg)*arg++=0;
        fprintf(stderr,"[ONLINE] test frame %u: %s%s%s\n",sim_frames,action,arg?" ":"",arg?arg:"");
        if(!strcmp(action,"host"))host();
        else if(!strcmp(action,"join")&&arg)join_address(arg);
        else if(!strcmp(action,"discover")){eng_net_discover();eng_net_discover_autojoin(1);mode=MODE_JOIN;}
        else if(!strcmp(action,"quick"))quick_match();
        else if(!strcmp(action,"leave"))leave();
        else if(!strcmp(action,"freeze")&&arg)SDL_Delay((Uint32)atoi(arg));
    }
    if(status&&sim_frames%300==0){
        tc2_link_status s;tc2_link_get_status(&s);char net[96];eng_net_status(net,sizeof net);
        fprintf(stderr,"[ONLINE] frame %u side P%d link %s alive %d lag %u state %u tx %u rx %u drop %u bad %u/%u game-errors %u ping %d | %s\n",
            sim_frames,qtc2_cabinet_side,s.enabled?"on":"off",s.alive,s.lag,s.state,s.tx,s.rx,s.rx_dropped,s.tx_bad,s.rx_bad,s.game_errors,eng_net_ping_ms(),net);
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
}

void tc2_online_frame(void){
    if(!inited)return;
    sim_frames++;
    test_switches();
    lobby_tick();
    eng_net_poll();
    /* a packet larger than one network frame: its other fragments go out now (each poll sends one FRAME) */
    for(int extra=0;extra<7&&tc2_link_pending();extra++)eng_net_poll();
    episode_tick();
    health_tick();
    last_poll_ms=SDL_GetTicks();
}
void tc2_online_service(void){
    if(!inited)return;
    uint32_t now=SDL_GetTicks();
    if(now-last_poll_ms<30)return;
    lobby_tick();
    eng_net_poll_paused();
    episode_tick();
    last_poll_ms=now;
}

/* ---- the ONLINE page ---- */
enum { ITEM_HOST,ITEM_FIND,ITEM_JOIN,ITEM_QUICK,ITEM_NAME,ITEM_FOUND0,ITEM_LEAVE=100 };
static int cursor,edit_item=-1,edit_pos;
static char edit_buf[24];
static const char name_chars[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";

static int offline_items(int *items){
    int n=0;items[n++]=ITEM_HOST;items[n++]=ITEM_FIND;
    for(int i=0;i<eng_net_found_count()&&i<4;i++)items[n++]=ITEM_FOUND0+i;
    items[n++]=ITEM_JOIN;items[n++]=ITEM_QUICK;items[n++]=ITEM_NAME;return n;
}
static int current_items(int *items){
    if(eng_net_hosting()||eng_net_connected()||mode!=MODE_NONE||episode){items[0]=ITEM_LEAVE;return 1;}
    return offline_items(items);
}
bool tc2_online_menu_editing(void){return edit_item>=0;}
void tc2_online_menu_move(int d){
    if(edit_item>=0){
        if(edit_item==ITEM_JOIN){
            int octet=edit_pos/3,digit=edit_pos%3,v=join_ip[octet];int scale=digit==0?100:digit==1?10:1;
            int cur=(v/scale)%10,next=(cur+(d>0?1:9))%10;v+= (next-cur)*scale;if(v>255)v=255;if(v<0)v=0;join_ip[octet]=(uint8_t)v;
        }else{
            const char *p=strchr(name_chars,edit_buf[edit_pos]?edit_buf[edit_pos]:' ');int i=p?(int)(p-name_chars):0,n=(int)strlen(name_chars);
            i=(i+(d>0?1:n-1))%n;edit_buf[edit_pos]=name_chars[i];
        }
        return;
    }
    int items[16],n=current_items(items);cursor=(cursor-d+n)%n;   /* stick up = previous item */
}
void tc2_online_menu_adjust(int d){
    if(edit_item<0)return;
    int max=edit_item==ITEM_JOIN?11:NAME_MAX_LEN-1;
    edit_pos+=d;if(edit_pos<0)edit_pos=0;if(edit_pos>max)edit_pos=max;
}
void tc2_online_menu_select(void){
    if(edit_item==ITEM_JOIN){
        edit_item=-1;save_cfg();char a[32];snprintf(a,sizeof a,"%u.%u.%u.%u:%d",join_ip[0],join_ip[1],join_ip[2],join_ip[3],join_port);join_address(a);return;
    }
    if(edit_item==ITEM_NAME){
        edit_item=-1;edit_buf[NAME_MAX_LEN]=0;clean_name(edit_buf);save_cfg();eng_net_set_name(name);return;
    }
    int items[16],n=current_items(items);if(cursor>=n)cursor=n-1;
    switch(items[cursor]){
    case ITEM_HOST:host();break;
    case ITEM_FIND:eng_net_discover();say("SEARCHING THE LOCAL NETWORK",3000);break;
    case ITEM_JOIN:edit_item=ITEM_JOIN;edit_pos=0;break;
    case ITEM_QUICK:quick_match();break;
    case ITEM_NAME:edit_item=ITEM_NAME;edit_pos=0;memset(edit_buf,0,sizeof edit_buf);snprintf(edit_buf,sizeof edit_buf,"%-12s",name);break;
    case ITEM_LEAVE:leave();cursor=0;break;
    default:{
        char addr[64];
        if(items[cursor]>=ITEM_FOUND0&&eng_net_found(items[cursor]-ITEM_FOUND0,NULL,0,addr,sizeof addr))join_address(addr);
    }}
}
void tc2_online_menu_back(void){if(edit_item>=0){edit_item=-1;load_cfg();}}

static void upper(char *s){for(;*s;s++)*s=(char)toupper((unsigned char)*s);}
unsigned tc2_online_menu_lines(char text[TC2_ONLINE_LINES][TC2_ONLINE_COLUMNS],int style[TC2_ONLINE_LINES]){
    int n=0;
    #define LINE(st,...) do{if(n<TC2_ONLINE_LINES){snprintf(text[n],TC2_ONLINE_COLUMNS,__VA_ARGS__);upper(text[n]);style[n++]=(st);}}while(0)
    for(int i=0;i<TC2_ONLINE_LINES;i++){text[i][0]=0;style[i]=TC2_LINE_TEXT;}
    LINE(TC2_LINE_TITLE,"ONLINE 2-PLAYER  4/4");
    int items[16],count=current_items(items);if(cursor>=count)cursor=count-1;if(cursor<0)cursor=0;
    if(tc2_online_session()){
        tc2_link_status s;tc2_link_get_status(&s);
        if(eng_net_session_active())LINE(TC2_LINE_GOOD,"LINKED - YOU ARE P%d %s",qtc2_cabinet_side,qtc2_cabinet_side==2?"ROBERT":"KEITH");
        else LINE(TC2_LINE_WARN,"%s - YOU ARE P%d",eng_net_connected()||eng_net_hosting()?"WAITING FOR PARTNER":"RECONNECTING",qtc2_cabinet_side);
        for(int i=0;i<eng_net_roster_count();i++){int r,self;char nm[24];if(eng_net_roster(i,nm,sizeof nm,&r,&self)&&!self)LINE(TC2_LINE_TEXT,"PARTNER: %s (P%d)",nm,eng_net_roster_slot(i)==1?2:1);}
        int ping=eng_net_ping_ms();
        if(ping>=0)LINE(s.alive?TC2_LINE_TEXT:TC2_LINE_WARN,"PING %d MS  LINK %s",ping,s.alive?(s.lag>6?"LAGGING":"GOOD"):"WAITING");
        else LINE(s.alive?TC2_LINE_TEXT:TC2_LINE_WARN,"LINK %s",s.alive?"GOOD":"WAITING");
        LINE(TC2_LINE_HINT,"THE GAME RUNS ON WHILE THIS IS OPEN");
        LINE(TC2_LINE_HINT,"BOTH: ADD CREDITS, PULL TRIGGER");
    }else if(eng_net_connected()||eng_net_hosting()||mode!=MODE_NONE){
        if(eng_net_hosting()){
            LINE(TC2_LINE_GOOD,"HOSTING ON PORT 27750");
            if(!address_count)find_addresses();
            for(int i=0;i<address_count;i++)LINE(TC2_LINE_TEXT,"ADDRESS: %s%s",addresses[i],vpn_address(addresses[i])?" (VPN)":"");
        }else if(eng_net_connected())LINE(TC2_LINE_GOOD,"CONNECTED TO %s",mode==MODE_SERVER?server:"HOST");
        else LINE(TC2_LINE_WARN,eng_net_discovering()?"SEARCHING...":"CONNECTING...");
        int rc=eng_net_roster_count();
        for(int i=0;i<rc;i++){int r,self;char nm[24];if(eng_net_roster(i,nm,sizeof nm,&r,&self))LINE(TC2_LINE_TEXT,"P%d %s%s",eng_net_roster_slot(i)==1?2:1,nm,self?" (YOU)":"");}
        if(eng_net_connected())LINE(TC2_LINE_HINT,rc<2?"WAITING FOR A PARTNER...":"STARTING LINK...");
    }else{
        LINE(TC2_LINE_HINT,"NAME: %s",name);
    }
    LINE(TC2_LINE_TEXT,"");
    for(int i=0;i<count;i++){
        char label[TC2_ONLINE_COLUMNS];int it=items[i];
        switch(it){
        case ITEM_HOST:snprintf(label,sizeof label,"HOST A GAME");break;
        case ITEM_FIND:snprintf(label,sizeof label,eng_net_discovering()?"FIND LAN GAMES (SEARCHING)":"FIND LAN GAMES");break;
        case ITEM_JOIN:
            if(edit_item==ITEM_JOIN){char ip[16];snprintf(ip,sizeof ip,"%03u.%03u.%03u.%03u",join_ip[0],join_ip[1],join_ip[2],join_ip[3]);
                char marked[20];int k=0;for(int j=0;ip[j];j++){int dpos=j-j/4;if(ip[j]!='.'&&dpos==edit_pos){marked[k++]='[';marked[k++]=ip[j];marked[k++]=']';}else marked[k++]=ip[j];}marked[k]=0;
                snprintf(label,sizeof label,"JOIN %s",marked);}
            else snprintf(label,sizeof label,"JOIN IP %u.%u.%u.%u",join_ip[0],join_ip[1],join_ip[2],join_ip[3]);
            break;
        case ITEM_QUICK:snprintf(label,sizeof label,"QUICK MATCH %s",server);break;
        case ITEM_NAME:
            if(edit_item==ITEM_NAME){char m[40];int k=0;for(int j=0;j<NAME_MAX_LEN;j++){char c=edit_buf[j]?edit_buf[j]:' ';if(j==edit_pos){m[k++]='[';m[k++]=c;m[k++]=']';}else m[k++]=c;}m[k]=0;snprintf(label,sizeof label,"NAME %s",m);}
            else snprintf(label,sizeof label,"CHANGE NAME");
            break;
        case ITEM_LEAVE:snprintf(label,sizeof label,episode?"LEAVE ONLINE GAME":eng_net_hosting()?"STOP HOSTING":"CANCEL");break;
        default:{char l[96];eng_net_found(it-ITEM_FOUND0,l,sizeof l,NULL,0);snprintf(label,sizeof label,"JOIN %.30s",l);}
        }
        LINE(i==cursor?TC2_LINE_SELECTED:TC2_LINE_ITEM,"%c %s",i==cursor?'>':' ',label);
    }
    if(edit_item>=0)LINE(TC2_LINE_HINT,"STICK: EDIT TRIGGER: OK L-CLICK: BACK");
    else LINE(TC2_LINE_HINT,"R-STICK UP/DOWN + TRIGGER: SELECT");
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
