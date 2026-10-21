#include <assert.h>
#include <sys/wait.h>
#include "../link/tc2_link.c"
#include "link_host_stubs.c"
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void step(void){tc2_link_service(now_ms());struct timespec t={0,1000000};nanosleep(&t,0);}
static void wait_state(TC2SessionState target){uint64_t end=now_ms()+3000;while(session_state.state!=target&&now_ms()<end){assert(session_state.state!=TC2_ERROR);step();}assert(session_state.state==target);}
int main(void){
    tc2_link_leave();assert(!enabled&&!phase&&!incoming_len&&!outgoing_len);
    assert(!tc2_link_join("300.1.1.1"));assert(session_state.state==TC2_ERROR&&!phase);
    /* Real listener/client handshake, readiness, host start and close, twice. */
    for(int cycle=0;cycle<2;cycle++){
        tc2_link_service(now_ms());assert(tc2_link_host());assert(!enabled);
        pid_t child=fork();assert(child>=0);
        if(!child){
            tc2_link_leave();assert(tc2_link_join("127.0.0.1"));wait_state(TC2_LOBBY);
            assert(session_state.compatible&&!enabled);tc2_link_ready();wait_state(TC2_PLAYING);
            assert(enabled&&side==1&&tc2_link_take_restart());tc2_link_device_reset();
            uint64_t end=now_ms()+3000;while(session_state.state!=TC2_ERROR&&now_ms()<end)step();
            assert(session_state.state==TC2_ERROR);tc2_link_leave();assert(!enabled&&session_state.restart);_exit(0);
        }
        wait_state(TC2_LOBBY);assert(session_state.compatible&&!enabled);
        tc2_link_start();assert(session_state.state==TC2_LOBBY); /* cannot start unready */
        tc2_link_ready();uint64_t end=now_ms()+3000;
        while(!session_state.peer_ready&&now_ms()<end)step();assert(session_state.peer_ready);
        tc2_link_start();wait_state(TC2_PLAYING);assert(enabled&&side==0&&tc2_link_take_restart());
        tc2_link_device_reset();tc2_link_leave();assert(!enabled&&session_state.restart&&!regfile&&!txlen&&!incoming_len&&!outgoing_len);
        int status_code;waitpid(child,&status_code,0);assert(WIFEXITED(status_code)&&WEXITSTATUS(status_code)==0);
    }
    /* Discovery request/reply uses real UDP on loopback, no broadcast needed. */
    assert(tc2_link_host());int udp=socket(AF_INET,SOCK_DGRAM,0);assert(udp>=0);
    struct timeval timeout={1,0};setsockopt(udp,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons(TC2_PORT);inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);
    const char query[]="TC2VR_FIND_2",reply[]="TC2VR_HOST_2";
    assert(sendto(udp,query,sizeof query,0,(struct sockaddr *)&address,sizeof address)==sizeof query);step();
    fcntl(udp,F_SETFL,O_NONBLOCK);char response[32];int got=-1;uint64_t discovery_end=now_ms()+1000;
    while(got<0&&now_ms()<discovery_end){step();got=(int)recv(udp,response,sizeof response,0);}
    assert(got==sizeof reply&&!memcmp(response,reply,sizeof reply));
    tc2_link_find();socklen_t size=sizeof address;assert(getsockname(discovery,(struct sockaddr *)&address,&size)==0);
    inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);last_discovery=now_ms();
    assert(sendto(udp,reply,sizeof reply,0,(struct sockaddr *)&address,sizeof address)==sizeof reply);step();
    discovery_end=now_ms()+1000;while(!session_state.found_count&&now_ms()<discovery_end)step();
    assert(session_state.found_count==1&&!strcmp(session_state.found[0],"127.0.0.1"));close(udp);tc2_link_leave();
    /* Untrusted peers cannot forward original records before negotiation. */
    int pair[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);peer=pair[0];nonblocking();managed=1;phase=3;
    const uint8_t game[]={0,2,0,0};assert(send(pair[1],game,sizeof game,0)==sizeof game);network_poll();
    assert(session_state.state==TC2_ERROR&&!phase);close(pair[1]);
    tc2_link_leave();assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);peer=pair[0];nonblocking();phase=3;
    uint8_t hello[68]={0x80,66,1,1};memcpy(hello+4,"different version",17);
    assert(send(pair[1],hello,sizeof hello,0)==sizeof hello);network_poll();
    assert(session_state.state==TC2_ERROR&&strstr(session_state.message,"mismatch"));close(pair[1]);
    /* Negotiation/start deadlines and a silent ready peer are visible errors. */
    for(int state=TC2_CONNECTING;state<=TC2_STARTING;state++){
        tc2_link_leave();assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);peer=pair[0];nonblocking();phase=3;
        session_state.state=(TC2SessionState)state;session_state.compatible=state!=TC2_CONNECTING;
        connection_started=last_rx=1;tc2_link_service(20000);
        assert(session_state.state==TC2_ERROR&&!phase);close(pair[1]);
    }
    tc2_link_leave();assert(!strcmp(tc2_link_backup(),"tc2_backup.nv"));
    puts("PASS: real TCP handshake, readiness/start, reconnect, leave, premature records and version mismatch");
}
