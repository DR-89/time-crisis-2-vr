#ifndef TC2_LINK_H
#define TC2_LINK_H
#include <stdint.h>
int tc2_link_enabled(void);
int tc2_link_side(void);
uint16_t tc2_link_read(unsigned k, uint16_t *regs);
void tc2_link_write(unsigned k, uint16_t value, uint16_t *regs);
void tc2_link_poll(void);
void tc2_link_vblank(uint32_t frame);
void tc2_link_report(void);
void tc2_link_observe(uint32_t pc);

/* Player-facing session. All calls run on the emulation/host thread. */
typedef enum { TC2_OFF,TC2_FINDING,TC2_WAITING,TC2_CONNECTING,TC2_LOBBY,TC2_STARTING,TC2_PLAYING,TC2_ERROR } TC2SessionState;
typedef struct {
    TC2SessionState state;
    int host,local_ready,peer_ready,compatible,restart;
    unsigned revision,found_count;
    char message[80],address[16],found[4][16];
} TC2Session;
const TC2Session *tc2_link_session(void);
int tc2_link_host(void);
int tc2_link_join(const char *ip);
void tc2_link_find(void);
void tc2_link_ready(void);
void tc2_link_start(void);
void tc2_link_leave(void);
void tc2_link_service(uint64_t milliseconds);
int tc2_link_take_restart(void);
void tc2_link_device_reset(void);
const char *tc2_link_backup(void);
#endif
