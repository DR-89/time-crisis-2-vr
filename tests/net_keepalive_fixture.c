/* Drive the real keepalive sender with a deterministic timer and a loopback UDP
 * receiver: timing, packet identity, expiry and disconnect/socket reuse. */
#include "../quest/tc2_net_keepalive.c"
#include <assert.h>

struct SDL_mutex { int locked; };
static struct SDL_mutex test_mutex;
static Uint32 test_ms;
static SDL_TimerCallback test_callback;
static void *test_callback_arg;
static int timer_count;
Uint32 SDLCALL SDL_GetTicks(void) { return test_ms; }
SDL_mutex *SDLCALL SDL_CreateMutex(void) { return &test_mutex; }
int SDLCALL SDL_LockMutex(SDL_mutex *m) { assert(!m->locked); m->locked=1; return 0; }
int SDLCALL SDL_UnlockMutex(SDL_mutex *m) { assert(m->locked); m->locked=0; return 0; }
SDL_TimerID SDLCALL SDL_AddTimer(Uint32 interval, SDL_TimerCallback cb, void *arg)
{
    assert(interval <= 1000); test_callback=cb; test_callback_arg=arg;
    timer_count++; return 1;
}
SDL_bool SDLCALL SDL_RemoveTimer(SDL_TimerID id) { assert(id==1); return SDL_TRUE; }
const char *SDLCALL SDL_GetError(void) { return "test"; }

static void tick(Uint32 at)
{
    test_ms=at;
    assert(test_callback && test_callback(250,test_callback_arg)==250);
    assert(!test_mutex.locked);
}
static eng_sock_t receiver(struct sockaddr_storage *address, socklen_t *size)
{
    eng_sock_t s=socket(AF_INET,SOCK_DGRAM,0); assert(s!=ENG_SOCK_BAD);
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(s,(struct sockaddr *)&a,sizeof a)==0);
    *size=sizeof *address; assert(getsockname(s,(struct sockaddr *)address,size)==0);
    eng_sock_nonblock(s); return s;
}
static int packet(eng_sock_t s,uint8_t *out)
{
    fd_set reads; FD_ZERO(&reads); FD_SET(s,&reads);
    struct timeval wait={0,100000};
    assert(select((int)s+1,&reads,NULL,NULL,&wait)==1);
    return recvfrom(s,(char *)out,32,0,NULL,NULL);
}
static void empty(eng_sock_t s)
{
    uint8_t b[32]; assert(recvfrom(s,(char *)b,sizeof b,0,NULL,NULL)<0);
    assert(eng_sock_would_block());
}
static void check(eng_sock_t s,const uint8_t *header,int size,Uint32 stamp)
{
    uint8_t b[32]; assert(packet(s,b)==size); assert(!memcmp(b,header,(size_t)size-4));
    uint32_t got=b[size-4]|(uint32_t)b[size-3]<<8|(uint32_t)b[size-2]<<16|(uint32_t)b[size-1]<<24;
    assert(got==stamp);
}
int main(void)
{
    eng_sock_init();
    struct sockaddr_storage a,b; socklen_t an,bn;
    eng_sock_t r1=receiver(&a,&an),r2=receiver(&b,&bn);
    eng_sock_t client=socket(AF_INET,SOCK_DGRAM,0); assert(client!=ENG_SOCK_BAD);
    uint8_t ping[20]={'N','M','N','2',9,0,0,0,4,0,'t','2',1,0,7,0,0,0,0,0};
    test_ms=100;
    tc2_net_keepalive_update(client,&a,an,ping,20);
    tick(1099); empty(r1);
    tick(1100); check(r1,ping,20,1100);
    tick(1300); empty(r1);                  /* rate limit */
    test_ms=1500; tc2_net_keepalive_update(client,&a,an,ping,20);
    tick(2100); empty(r1);                  /* regular traffic postpones a heartbeat */
    tick(2500); check(r1,ping,20,2500);
    ping[14]=8;
    test_ms=3000; tc2_net_keepalive_update(client,&b,bn,ping,20);
    tick(4000); check(r2,ping,20,4000); empty(r1);  /* copied destination/room, never stale */
    assert(timer_count==1);
    tick(33000); check(r2,ping,20,33000);
    tick(34000); empty(r2);                 /* a hung application is not kept forever */
    tc2_net_keepalive_clear();
    test_ms=0xfffffe00u; tc2_net_keepalive_update(client,&b,bn,ping,20);
    tick(0x1e8u); check(r2,ping,20,0x1e8u);  /* SDL clock wrap */
    tc2_net_keepalive_clear(); closesocket(client);
    tick(0x5d0u); empty(r1); empty(r2);      /* no send on a closed/recycled socket */
    client=socket(AF_INET,SOCK_DGRAM,0); assert(client!=ENG_SOCK_BAD);
    uint8_t rrn[14]={'R','R','N','1',9,0,0,0,4,0,0,0,0,0};
    test_ms=2000; tc2_net_keepalive_update(client,&a,an,rrn,14);
    tick(3000); check(r1,rrn,14,3000);
    tc2_net_keepalive_clear(); tc2_net_keepalive_clear();
    tick(5000); empty(r1);
    closesocket(client); closesocket(r1); closesocket(r2);
    puts("PASS: keepalive packets, cadence, active traffic, server/room changes, grace expiry, clock wrap and disconnect");
    return 0;
}
