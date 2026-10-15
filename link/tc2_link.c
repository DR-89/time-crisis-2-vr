/* Port-owned TC2 C139/C422 bridge. No game RAM repairs or synthesized packets.
 * Register/RX-ring research: MAME (Angelo Salese, BSD-3-Clause), SailorSat
 * PR 13421 and pocketjazzy/MAME-TC2-Public @5ff99b9. See NOTICE.md.
 * Each TCP record is a BE byte length followed by the unchanged C422 words.
 * 0x8004 records carry an optional test-only frame barrier, never game data.
 */
#include "tc2_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define WAIT_ERR WSAGetLastError()
#define WOULD_BLOCK WSAEWOULDBLOCK
#define IN_PROGRESS WSAEWOULDBLOCK
typedef int socklen_t;
static SOCKET peer=INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define WAIT_ERR errno
#define WOULD_BLOCK EWOULDBLOCK
#define IN_PROGRESS EINPROGRESS
static int peer=-1;
#endif
extern uint8_t *tc2_vram_ptr(uint32_t address);
extern uint64_t tc2_cycles_now(void);
extern void tc2_irq_line(uint32_t bit, int on);
extern uint32_t rr_frame;
extern uint8_t tc2_ram[];
static int initialized, enabled, side, synchronize, finishing, phase; /* 1 listen, 2 connect, 3 connected */
static uint16_t *regfile, txptr, status=4;
static uint64_t tx_due, next_poll;
static uint8_t tx[8192], incoming[65536], outgoing[65536];
static size_t txlen, incoming_len, outgoing_len;
static uint32_t peer_frame;
static unsigned long sent, received, accepted, checksums;
static FILE *trace;
static int managed;
static TC2Session session_state;
static uint64_t service_now,last_rx,connection_started,last_ping;
static void session_connected(void);
static void session_control(const uint8_t *,unsigned);
static void session_error(const char *);

static void fail(const char *why) { fprintf(stderr,"[LINK] %s (socket error %d)\n",why,WAIT_ERR); exit(6); }
static int wait_socket(int writing, int seconds) {
    fd_set f; FD_ZERO(&f); FD_SET(peer,&f); struct timeval t={seconds,0};
    return select((int)peer+1,writing?NULL:&f,writing?&f:NULL,NULL,&t);
}
static void disconnected(const char *why) {
    fprintf(stderr,"[LINK] disconnected: %s; the cabinet ROM handles link loss\n",why);
    if((intptr_t)peer>=0) close(peer);
    peer=-1; phase=0; incoming_len=outgoing_len=0;
    if(managed)session_error(why);
}
static int nonblocking(void) {
#ifdef _WIN32
    u_long nb=1; if(ioctlsocket(peer,FIONBIO,&nb)) return 0;
#else
    if(fcntl(peer,F_SETFL,fcntl(peer,F_GETFL,0)|O_NONBLOCK)<0) return 0;
#endif
    return 1;
}
static void connected(void) {
    if(!nonblocking()) { disconnected("Socket setup failed"); return; }
    int one=1; setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,(const char *)&one,sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
    phase=3; fprintf(stderr,"[LINK] connected side=%d sync=%d\n",side,synchronize);
    if(managed)session_connected();
}
static void flush_output(void) {
    /* Bounded nonblocking work on the emulation thread. Rendering never waits
     * for a slow or absent peer. Overflow models a lost cable, not stale replay. */
    if(phase!=3 || !outgoing_len) return;
#ifdef MSG_NOSIGNAL
    const int flags=MSG_NOSIGNAL;
#else
    const int flags=0;
#endif
    int n=(int)send(peer,(const char *)outgoing,(int)outgoing_len,flags);
    if(n>0) { memmove(outgoing,outgoing+n,outgoing_len-n); outgoing_len-=n; }
    else if(n<0 && WAIT_ERR!=WOULD_BLOCK) disconnected("send failed");
}
static void send_bytes(const uint8_t *p,size_t n) {
    if(phase!=3) return;
    if(n>sizeof outgoing-outgoing_len) { disconnected("TX queue overflow"); return; }
    memcpy(outgoing+outgoing_len,p,n); outgoing_len+=n; flush_output();
}
static void record(const char *kind,const uint8_t *p,size_t n) {
    if(!trace) return;
    fprintf(trace,"{\"event\":\"%s\",\"frame\":%u,\"bytes\":%zu,\"data\":\"",kind,rr_frame,n);
    for(size_t i=0;i<n;i++) fprintf(trace,"%02x",p[i]);
    fprintf(trace,"\"}\n"); fflush(trace);
}
static void init(void) {
    if(initialized) return;
    initialized=1;
    const char *config=getenv("TC2_LINK");
    if(!config || !*config || !strcmp(config,"0")) return;
    const char *s=getenv("TC2_LINK_SIDE");
    if(!s || (strcmp(s,"0") && strcmp(s,"1"))) fail("TC2_LINK_SIDE must be 0 (red) or 1 (blue)");
    side=atoi(s); synchronize=getenv("TC2_LINK_SYNC") && !strcmp(getenv("TC2_LINK_SYNC"),"1");
#ifdef _WIN32
    WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w)) fail("WSAStartup failed");
#endif
    unsigned port=0; char host[64]; int used=0;
    int listener=sscanf(config,"listen:%u%n",&port,&used)==1 && config[used]==0;
    if(!listener && !(sscanf(config,"connect:%63[^:]:%u%n",host,&port,&used)==2 && config[used]==0)) fail("TC2_LINK must be listen:port or connect:IPv4:port");
    if(port<1 || port>65535) fail("invalid link port");
    peer=socket(AF_INET,SOCK_STREAM,0); if((intptr_t)peer<0) fail("socket failed");
    struct sockaddr_in addr; memset(&addr,0,sizeof addr); addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)port);
    nonblocking(); enabled=1;
    if(listener) {
        addr.sin_addr.s_addr=htonl(INADDR_ANY); int yes=1;
        setsockopt(peer,SOL_SOCKET,SO_REUSEADDR,(const char *)&yes,sizeof yes);
        if(bind(peer,(struct sockaddr *)&addr,sizeof addr) || listen(peer,1)) fail("listen failed");
        phase=1; fprintf(stderr,"[LINK] waiting on %u side=%d\n",port,side);
    } else {
        if(inet_pton(AF_INET,host,&addr.sin_addr)!=1) fail("invalid peer IPv4 address");
        if(connect(peer,(struct sockaddr *)&addr,sizeof addr)==0) connected();
        else if(WAIT_ERR==IN_PROGRESS) phase=2;
        else disconnected("connect failed; start the listening cabinet first");
    }
    const char *path=getenv("TC2_LINK_TRACE"); if(path) { trace=fopen(path,"w"); if(!trace) fail("cannot open trace"); }

}
int tc2_link_enabled(void) { init(); return enabled; }
int tc2_link_side(void) { init(); return enabled?side:0; }
static void irq(void) { tc2_irq_line(0x2000,1); }
static void receive_records(void) {
    size_t at=0;
    while(incoming_len-at>=2) {
        uint8_t *record_data=incoming+at;
        unsigned n=(record_data[0]<<8)|record_data[1];
        int control=(n&0x8000)!=0; if(control)n&=0x7fff;
        if(!n || n>8192 || n%2) { disconnected("invalid record length"); return; }
        if(incoming_len-at<n+2) break;
        if(control) {
            if(managed) {
                session_control(record_data+2,n);
                if(phase!=3)return;
            } else if(n==4) peer_frame=((uint32_t)record_data[2]<<24)|((uint32_t)record_data[3]<<16)|(record_data[4]<<8)|record_data[5];
            else { disconnected("Unexpected control record"); return; }
        } else {
            if(managed && session_state.state!=TC2_PLAYING) { disconnected("Peer sent game data before start"); return; }
            if(!regfile || (status&2)) { at+=n+2; continue; } /* ROM must consume the prior RX notification. */
            uint8_t *ram=tc2_vram_ptr(0x06200000);
            for(unsigned i=0;i<n;i++) ram[0x2000+(((regfile[6]*2)+i)&0x1fff)]=record_data[i+2];
            regfile[6]=(regfile[6]+n/2)&0xfff;
            regfile[4]=(regfile[4]+n/2)&0xfff;
            status|=2; received++; record("rx",record_data+2,n); irq();
        }
        memmove(record_data,record_data+n+2,incoming_len-at-n-2); incoming_len-=n+2;
    }
}
static void network_poll(void) {
    if(phase==1) {
        if(wait_socket(0,0)<=0) return;
        intptr_t accepted_socket=accept(peer,NULL,NULL);
        if(accepted_socket<0) return;
        close(peer); peer=accepted_socket; connected();
    } else if(phase==2) {
        if(wait_socket(1,0)<=0) return;
        int error=0; socklen_t len=sizeof error;
        if(getsockopt(peer,SOL_SOCKET,SO_ERROR,(char *)&error,&len) || error) {
            disconnected("connect failed; start the listening cabinet first"); return;
        }
        connected();
    }
    if(phase!=3)return;
    flush_output();
    if(phase!=3)return;
    receive_records();
    if(phase!=3)return;
    /* One bounded receive per poll; the kernel supplies backpressure. */
    if(incoming_len==sizeof incoming) { disconnected("RX queue overflow"); return; }
    int n=(int)recv(peer,(char *)incoming+incoming_len,(int)(sizeof incoming-incoming_len),0);
    if(n>0) { last_rx=service_now; incoming_len+=n; receive_records(); }
    else if(n==0) disconnected("peer closed");
    else if(WAIT_ERR!=WOULD_BLOCK) disconnected("receive failed");
}

void tc2_link_poll(void) {
    if(!tc2_link_enabled()) return;
    uint64_t now=tc2_cycles_now();
    if(txlen && now>=tx_due) {
        uint8_t header[2]={(uint8_t)(txlen>>8),(uint8_t)txlen}; send_bytes(header,2); send_bytes(tx,txlen);
        if(phase==3) { record("tx",tx,txlen); sent++; }
        txlen=0; regfile[5]=0; regfile[3]&=~1; status|=4; irq();
    }
    if(now<next_poll)return;
    next_poll=now+16934; network_poll();
}
uint16_t tc2_link_read(unsigned k,uint16_t *regs) {
    if(!tc2_link_enabled()) return regs[k];
    regfile=regs; tc2_link_poll();
    return k==0 ? status : regs[k];
}
void tc2_link_write(unsigned k,uint16_t v,uint16_t *regs) {
    regfile=regs;
    unsigned old=regs[k]; regs[k]=v;
    if(k==0) { status=v&6; tc2_irq_line(0x2000,0); }
    if(k==1) { tc2_irq_line(0x2000,v==0xfffb); }
    if(k==7) txptr=v;
    /* The ROM arms START=3, sets OFFSET/SIZE, clears prior IRQ flags and then
     * closes setup with START=2. DMA begins after that programming sequence. */
    if(k==3 && (old&1) && !(v&1) && regs[5]) {
        if(txlen) fail("TX programmed while busy");
        unsigned words=regs[5]; if(words>4096) fail("TX size exceeds RAM");
        uint8_t *ram=tc2_vram_ptr(0x06200000);
        txlen=words*2;
        for(size_t i=0;i<txlen;i++) tx[i]=ram[((txptr*2)+i)&0x3fff];
        txptr=(txptr+words)&0x1fff;
        status&=~4;
        tx_due=tc2_cycles_now()+169344; /* bounded asynchronous serializer, 1 ms */
    }
}
static uint32_t ram32(unsigned p) { return ((uint32_t)tc2_ram[p]<<24)|(tc2_ram[p+1]<<16)|(tc2_ram[p+2]<<8)|tc2_ram[p+3]; }
void tc2_link_observe(uint32_t pc) {
    if(pc==0x8000B8F0) accepted++;
    if(pc==0x8000BF38) checksums++;

}
void tc2_link_vblank(uint32_t frame) {
    if(tc2_link_enabled() && synchronize) {
        time_t connected_start=time(NULL);
        while(phase!=3) {
            network_poll();
            if(!phase || time(NULL)-connected_start>10) fail("test peer connection timeout");
            if(phase!=3) wait_socket(phase==2,1);
        }
        uint8_t token[6]={0x80,4,(uint8_t)(frame>>24),(uint8_t)(frame>>16),(uint8_t)(frame>>8),(uint8_t)frame};
        send_bytes(token,sizeof token);
        time_t start=time(NULL);
        while(peer_frame<frame-1) { network_poll(); if(time(NULL)-start>10) fail("frame barrier timeout"); if(peer_frame<frame-1) { if(phase!=3) fail("test peer disconnected"); wait_socket(0,1); } }
    }
    if((enabled || getenv("TC2_LINK_DIAG")) && frame%60==0) {
        fprintf(stderr,"[LINKSTATE] frame=%u side=%d tx=%lu rx=%lu accepted=%lu fresh=%u drift=%u mode=%u\n",frame,side,sent,received,accepted,tc2_ram[0x2f3502],(tc2_ram[0x2f3504]<<8)|tc2_ram[0x2f3505],ram32(0x2f3fd0));
    }
}
void tc2_link_report(void) {
    fprintf(stderr,"[LINK] summary tx=%lu rx=%lu accepted=%lu validators=%lu\n",sent,received,accepted,checksums);
    if(trace) fflush(trace);
    if(enabled && phase==3 && synchronize && !finishing) {
        finishing=1;
        const uint8_t done[6]={0x80,4,255,255,255,255}; send_bytes(done,6);
        time_t start=time(NULL);
        while(peer_frame!=UINT32_MAX) { network_poll(); if(time(NULL)-start>10) fail("finish timeout"); if(peer_frame!=UINT32_MAX) { if(phase!=3) fail("test peer disconnected"); wait_socket(0,1); } }
#ifdef _WIN32
        shutdown(peer,SD_SEND);
#else
        shutdown(peer,SHUT_WR);
#endif
        /* Drain the peer's half-close so queued bytes cannot cause TCP RST. */
        char discard[8192];
        while(wait_socket(0,2)>0) { int n=(int)recv(peer,discard,sizeof discard,0); if(n<=0)break; }
    }
}

/* Device reset preserves a negotiated cable; session close owns sockets. */
void tc2_link_device_reset(void) {
    regfile=NULL; txptr=0; status=4; tx_due=next_poll=0; txlen=0;
    peer_frame=0; sent=received=accepted=checksums=0;
    tc2_irq_line(0x2000,0);
}
const char *tc2_link_backup(void) {
    return tc2_link_enabled() ? (side ? "tc2_link_blue.nv" : "tc2_link_red.nv") : "tc2_backup.nv";
}

/* Compatibility changes whenever wire rules or emulated link behavior change.
 * tc2_rom_setup validates the pinned 22-chip timecrs2 set before the host opens. */
#define TC2_SESSION_ID "TC2VR-LAN2/core0837a397/timecrs2-tss3verb/port1"
#define TC2_PORT 32123
#ifdef _WIN32
static SOCKET discovery=INVALID_SOCKET;
#else
static int discovery=-1;
#endif
static uint64_t last_discovery;
static void session_message(TC2SessionState state,const char *message) {
    session_state.state=state; snprintf(session_state.message,sizeof session_state.message,"%s",message); session_state.revision++;
}
static void session_error(const char *message) {
    /* Preserve the current cabinet until the player chooses Leave: no surprise
     * reset in the middle of a shot. The lobby owns the visible recovery path. */
    session_state.compatible=session_state.local_ready=session_state.peer_ready=0;
    session_message(TC2_ERROR,message);
}
static void discovery_close(void) { if((intptr_t)discovery>=0)close(discovery); discovery=-1; }
static void session_close(void) {
    if((intptr_t)peer>=0)close(peer);
    peer=-1; phase=0;
    discovery_close(); if(trace)fclose(trace); trace=NULL;
    incoming_len=outgoing_len=0; memset(incoming,0,sizeof incoming); memset(outgoing,0,sizeof outgoing);
    synchronize=finishing=0; last_rx=last_ping=connection_started=0;
    tc2_link_device_reset();
}
void tc2_link_leave(void) {
    int reboot=enabled; session_close(); initialized=managed=1; enabled=side=0;
    unsigned revision=session_state.revision+1;
    memset(&session_state,0,sizeof session_state); session_state.revision=revision; session_state.restart=reboot;
    session_message(TC2_OFF,"Choose Host, Find or Join by IP");
}
const TC2Session *tc2_link_session(void) { return &session_state; }
int tc2_link_take_restart(void) { int value=session_state.restart;session_state.restart=0;return value; }
static void control_send(unsigned op,unsigned value) {
    uint8_t record_data[68]={0x80,66}; record_data[2]=(uint8_t)op;record_data[3]=(uint8_t)value;
    unsigned n=6;
    if(op==1) { snprintf((char *)record_data+4,64,"%s",TC2_SESSION_ID); n=66; }
    record_data[1]=(uint8_t)n;send_bytes(record_data,n+2);
}
static void session_connected(void) {
    last_rx=connection_started=service_now; discovery_close();
    session_message(TC2_CONNECTING,"Checking game compatibility..."); control_send(1,(unsigned)side);
}
static void play(void) {
    enabled=1; session_state.restart=1; session_message(TC2_PLAYING,"Linked game - leave to return solo");
}
static void session_control(const uint8_t *p,unsigned n) {
    if(n<6) { disconnected("Invalid session message"); return; }
    unsigned op=p[0];
    if(op==1) {
        char expected[64]={0};snprintf(expected,sizeof expected,"%s",TC2_SESSION_ID);
        if(n!=66 || memcmp(p+2,expected,64) || p[1]!=(unsigned)(1-side) || session_state.compatible) {
            disconnected("Game/version mismatch - update both"); return;
        }
        session_state.compatible=1;session_message(TC2_LOBBY,"Both players must select Ready");
    } else if(!session_state.compatible || n!=6) { disconnected("Invalid session handshake"); }
    else if(op==2 && session_state.state==TC2_LOBBY && p[1]<=1) {
        session_state.peer_ready=p[1];session_state.revision++;
    } else if(op==3 && !session_state.host && session_state.state==TC2_LOBBY && session_state.local_ready && session_state.peer_ready) {
        control_send(4,0); play();
    } else if(op==4 && session_state.host && session_state.state==TC2_STARTING) play();
    else if(op!=5) disconnected("Invalid session transition");
}
static int socket_runtime(void) {
#ifdef _WIN32
    static int ready;
    if(!ready){WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)){session_error("Network unavailable");return 0;}ready=1;}
#endif
    return 1;
}
static int discovery_open(int host) {
    if(!socket_runtime())return 0;
    discovery=socket(AF_INET,SOCK_DGRAM,0);if((intptr_t)discovery<0)return 0;
    int one=1;setsockopt(discovery,SOL_SOCKET,SO_BROADCAST,(const char *)&one,sizeof one);
#ifdef _WIN32
    u_long nb=1; if(ioctlsocket(discovery,FIONBIO,&nb)) { discovery_close();return 0; }
#else
    if(fcntl(discovery,F_SETFL,O_NONBLOCK)<0){discovery_close();return 0;}
#endif
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons(host?TC2_PORT:0);addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(discovery,(struct sockaddr *)&addr,sizeof addr)){discovery_close();return 0;}
    return 1;
}
static void local_address(void) {
    /* A UDP route lookup sends no traffic. Manual entry remains available when
     * a machine has multiple interfaces; discovery uses the responding source. */
#ifdef _WIN32
    SOCKET s=socket(AF_INET,SOCK_DGRAM,0);
#else
    int s=socket(AF_INET,SOCK_DGRAM,0);
#endif
    if((intptr_t)s<0)return;
    struct sockaddr_in addr={0};socklen_t n=sizeof addr;
    addr.sin_family=AF_INET;addr.sin_port=htons(TC2_PORT);addr.sin_addr.s_addr=htonl(0xffffffff);
    int one=1;setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char *)&one,sizeof one);
    if(connect(s,(struct sockaddr *)&addr,sizeof addr)==0 && getsockname(s,(struct sockaddr *)&addr,&n)==0)
        inet_ntop(AF_INET,&addr.sin_addr,session_state.address,sizeof session_state.address);
    close(s);
}
static int session_open(const char *ip) {
    tc2_link_leave();session_state.host=ip==NULL;side=ip?1:0;
    if(!socket_runtime())return 0;
    peer=socket(AF_INET,SOCK_STREAM,0);
    if((intptr_t)peer<0 || !nonblocking()){session_close();session_error("Network unavailable");return 0;}
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons(TC2_PORT);
    connection_started=last_rx=service_now;
    if(!ip) {
        int one=1;setsockopt(peer,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof one);
        if(bind(peer,(struct sockaddr *)&addr,sizeof addr)||listen(peer,1)) {session_close();session_error("Port busy - leave and retry");return 0;}
        phase=1;local_address();discovery_open(1);
        session_message(TC2_WAITING,"Waiting for the blue player...");
    } else {
        if(inet_pton(AF_INET,ip,&addr.sin_addr)!=1 || !addr.sin_addr.s_addr || addr.sin_addr.s_addr==0xffffffffu) {
            session_close();session_error("Enter a valid IPv4 address");return 0;
        }
        snprintf(session_state.address,sizeof session_state.address,"%s",ip);
        session_message(TC2_CONNECTING,"Connecting to red player...");
        if(connect(peer,(struct sockaddr *)&addr,sizeof addr)==0)connected();
        else if(WAIT_ERR==IN_PROGRESS)phase=2;
        else {session_close();session_error("Cannot connect - check IP and Wi-Fi");return 0;}
    }
    return 1;
}
int tc2_link_host(void) { return session_open(NULL); }
int tc2_link_join(const char *ip) { return session_open(ip); }
void tc2_link_find(void) {
    tc2_link_leave();
    if(!discovery_open(0)){session_error("Search unavailable - use Join by IP");return;}
    last_discovery=0;connection_started=service_now;session_message(TC2_FINDING,"Searching local Wi-Fi...");
}
void tc2_link_ready(void) {
    if(session_state.state!=TC2_LOBBY)return;
    session_state.local_ready=!session_state.local_ready;session_state.revision++;
    control_send(2,(unsigned)session_state.local_ready);
}
void tc2_link_start(void) {
    if(!session_state.host || session_state.state!=TC2_LOBBY || !session_state.local_ready || !session_state.peer_ready)return;
    session_message(TC2_STARTING,"Starting both cabinets...");connection_started=service_now;control_send(3,0);
}
void tc2_link_service(uint64_t milliseconds) {
    service_now=milliseconds;
    if(!managed)return;
    if(phase)network_poll();
    if((session_state.state==TC2_CONNECTING || session_state.state==TC2_STARTING) && service_now-connection_started>15000)disconnected("Connection timed out - retry");
    if(session_state.compatible && phase==3) {
        if(service_now-last_rx>10000){disconnected("Partner disconnected - leave/retry");return;}
        if(service_now-last_ping>=1000){last_ping=service_now;control_send(5,0);}
    }
    if((intptr_t)discovery<0)return;
    static const char query[]="TC2VR_FIND_2",reply[]="TC2VR_HOST_2";
    if(session_state.state==TC2_FINDING && (!last_discovery || service_now-last_discovery>=1000)) {
        last_discovery=service_now;struct sockaddr_in dest={0};dest.sin_family=AF_INET;dest.sin_port=htons(TC2_PORT);dest.sin_addr.s_addr=htonl(INADDR_BROADCAST);
        sendto(discovery,query,sizeof query,0,(struct sockaddr *)&dest,sizeof dest);
        if(service_now-connection_started>5000 && !session_state.found_count)session_message(TC2_FINDING,"No host found - try Join by IP");
    }
    char message[32];struct sockaddr_in from={0};socklen_t size=sizeof from;
    int n=(int)recvfrom(discovery,message,sizeof message,0,(struct sockaddr *)&from,&size);
    if(session_state.state==TC2_WAITING && n==(int)sizeof query && !memcmp(message,query,sizeof query))
        sendto(discovery,reply,sizeof reply,0,(struct sockaddr *)&from,size);
    if(session_state.state==TC2_FINDING && n==(int)sizeof reply && !memcmp(message,reply,sizeof reply)) {
        char ip[16];inet_ntop(AF_INET,&from.sin_addr,ip,sizeof ip);
        for(unsigned i=0;i<session_state.found_count;i++)if(!strcmp(ip,session_state.found[i]))return;
        if(session_state.found_count<4){snprintf(session_state.found[session_state.found_count++],16,"%s",ip);session_message(TC2_FINDING,"Select a host to join");}
    }
}
