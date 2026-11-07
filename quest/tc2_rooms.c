/* tc2_rooms.c -- see tc2_rooms.h */
#include "tc2_rooms.h"
#include "net_sock.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T_DISCOVER 0x0C
#define T_ANNOUNCE 0x0D
#define T_ROOMS    0x0F
#define T_ROOMLIST 0x10

const char tc2_room_code_chars[]="ACDEFGHJKLMNPQRTUVWXY34679";

static eng_sock_t sock=ENG_SOCK_BAD;
static struct sockaddr_storage addr;static socklen_t addr_len;
static char resolved_for[64];
static uint16_t version;
static tc2_room rooms[TC2_ROOMS_MAX];static int room_count;
static char server_name[24];static int server_players;
static uint32_t sent_ms,last_send_ms;static bool answered;

static void header(uint8_t *b,int type,int len){
    memcpy(b,"NMN2",4);b[4]=(uint8_t)type;b[5]=0;b[6]=b[7]=0;b[8]=(uint8_t)len;b[9]=(uint8_t)(len>>8);
    b[10]='t';b[11]='v';b[12]=(uint8_t)version;b[13]=(uint8_t)(version>>8);b[14]=b[15]=0;
}
static void send_query(void){
    if(sock==ENG_SOCK_BAD)return;
    uint8_t b[16+400];
    header(b,T_DISCOVER,0);sendto(sock,(const char*)b,16,0,(struct sockaddr*)&addr,addr_len);
    memset(b+16,0,400);header(b,T_ROOMS,400);          /* a stranger pads ROOMS: the server's answer may not exceed 3x what it was sent */
    sendto(sock,(const char*)b,16+400,0,(struct sockaddr*)&addr,addr_len);
    last_send_ms=SDL_GetTicks();
}
static bool resolve(const char *server){
    char host[64],port[8]="27750";
    snprintf(host,sizeof host,"%s",server);
    char *colon=strrchr(host,':');if(colon&&strchr(host,':')==colon){*colon=0;snprintf(port,sizeof port,"%s",colon+1);}
    struct addrinfo hints,*res=NULL;memset(&hints,0,sizeof hints);hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
    if(getaddrinfo(host,port,&hints,&res)!=0||!res)return false;
    memcpy(&addr,res->ai_addr,res->ai_addrlen);addr_len=(socklen_t)res->ai_addrlen;freeaddrinfo(res);
    snprintf(resolved_for,sizeof resolved_for,"%s",server);
    return true;
}

bool tc2_rooms_query(const char *server,uint16_t game_version){
    eng_sock_init();
    version=game_version;
    if(strcmp(server,resolved_for)&&!resolve(server))return false;
    if(sock==ENG_SOCK_BAD){sock=socket(AF_INET,SOCK_DGRAM,0);if(sock==ENG_SOCK_BAD)return false;eng_sock_nonblock(sock);}
    answered=false;sent_ms=SDL_GetTicks();
    send_query();
    return true;
}
bool tc2_rooms_busy(void){return sock!=ENG_SOCK_BAD&&!answered&&SDL_GetTicks()-sent_ms<3000;}
bool tc2_rooms_answered(void){return answered;}

static void code_from(const char *name,char *code){
    code[0]=0;
    const char *sp=strrchr(name,' ');const char *c=sp?sp+1:NULL;
    if(c&&strlen(c)==4&&tc2_room_code_valid(c))snprintf(code,8,"%s",c);
}
void tc2_rooms_poll(void){
    if(sock==ENG_SOCK_BAD)return;
    uint8_t b[1500];
    for(int k=0;k<16;k++){
        struct sockaddr_storage from;socklen_t fl=sizeof from;
        int n=(int)recvfrom(sock,(char*)b,sizeof b,0,(struct sockaddr*)&from,&fl);
        if(n<16)break;
        if(memcmp(b,"NMN2",4)||b[10]!='t'||b[11]!='v'||(b[8]|b[9]<<8)!=n-16)continue;
        const uint8_t *p=b+16;int len=n-16;
        if(b[4]==T_ANNOUNCE&&len>=1){
            int nl=p[0];if(1+nl+2>len)continue;
            int m=nl<23?nl:23;memcpy(server_name,p+1,(size_t)m);server_name[m]=0;
            server_players=p[1+nl]|p[2+nl]<<8;
        }else if(b[4]==T_ROOMLIST&&len>=1){
            int count=p[0],off=1;room_count=0;
            for(int i=0;i<count&&room_count<TC2_ROOMS_MAX;i++){
                if(off+9>len)break;
                tc2_room *r=&rooms[room_count];
                r->id=p[off]|p[off+1]<<8;r->version=p[off+2]|p[off+3]<<8;r->state=p[off+4];r->players=p[off+5];r->max_players=p[off+6];
                int nl=p[off+8];if(off+9+nl>len)break;
                int m=nl<31?nl:31;int j=0;for(int c=0;c<m;c++){uint8_t ch=p[off+9+c];if(ch>=' '&&ch<0x7F)r->name[j++]=(char)ch;}r->name[j]=0;
                code_from(r->name,r->code);
                room_count++;off+=9+nl;
            }
            answered=true;
        }
    }
    /* UDP: ask again if nothing came back */
    if(!answered&&SDL_GetTicks()-last_send_ms>700&&SDL_GetTicks()-sent_ms<3000)send_query();
}
int tc2_rooms_count(void){return room_count;}
const tc2_room *tc2_rooms_get(int i){return i>=0&&i<room_count?&rooms[i]:NULL;}
const char *tc2_rooms_server_name(void){return server_name;}
int tc2_rooms_server_players(void){return server_players;}
void tc2_rooms_close(void){if(sock!=ENG_SOCK_BAD){closesocket(sock);sock=ENG_SOCK_BAD;}answered=false;}

void tc2_room_code_make(char out[5]){
    unsigned n=(unsigned)strlen(tc2_room_code_chars);
    uint64_t seed=SDL_GetPerformanceCounter()^((uint64_t)SDL_GetTicks()<<32);
    for(int i=0;i<4;i++){seed=seed*6364136223846793005ULL+1442695040888963407ULL;out[i]=tc2_room_code_chars[(seed>>33)%n];}
    out[4]=0;
}
bool tc2_room_code_valid(const char *code){
    if(!code||strlen(code)!=4)return false;
    for(int i=0;i<4;i++)if(!code[i]||!strchr(tc2_room_code_chars,code[i]))return false;
    return true;
}
