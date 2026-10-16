/* Exercise the production adapter, with a real local socket and deterministic
 * emulated clock; no game packet generator or expected-state substitute. */
#include <assert.h>
#include "../link/tc2_link.c"
uint8_t tc2_ram[0x1000000];
static uint8_t linkram[0x4000];
static uint64_t cycle;
static int irq_level;
uint32_t rr_frame;
uint8_t *tc2_vram_ptr(uint32_t a) { assert(a==0x06200000); return linkram; }
uint64_t tc2_cycles_now(void) { return cycle; }
void tc2_irq_line(uint32_t bit,int on) { assert(bit==0x2000); irq_level=on; }
int main(void) {
    uint16_t regs[8]={0};
    initialized=1; enabled=0;
    regs[5]=0x1234; assert(tc2_link_read(5,regs)==0x1234); assert(tc2_link_side()==0);
    int sockets[2]; assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    peer=sockets[0]; nonblocking(); phase=3; enabled=1; side=1; regfile=regs; memset(regs,0,sizeof regs);
    assert(tc2_link_side()==1);
    linkram[4]=0; linkram[5]=0x5a; linkram[6]=1; linkram[7]=2;
    tc2_link_write(3,3,regs); tc2_link_write(7,2,regs); tc2_link_write(5,2,regs);
    assert(!txlen); /* START arms before DMA address and size are ready. */
    tc2_link_write(3,2,regs); assert(txlen==4 && regs[5]==2);
    linkram[5]=0xff; /* DMA snapshot must remain immutable while in flight. */
    cycle=tx_due; tc2_link_poll();
    uint8_t wire[6]; assert(recv(sockets[1],wire,6,MSG_WAITALL)==6);
    const uint8_t expected[]={0,4,0,0x5a,1,2}; assert(!memcmp(wire,expected,6));
    assert(regs[5]==0 && txptr==4 && irq_level && (tc2_link_read(0,regs)&4));
    tc2_link_write(1,0,regs); assert(!irq_level);
    /* A receive wraps only inside the high 8 KiB and does not wipe staged TX. */
    regs[6]=4095; regs[5]=17;
    assert(send(sockets[1],expected,6,0)==6); network_poll();
    assert(linkram[0x3ffe]==0 && linkram[0x3fff]==0x5a && linkram[0x2000]==1 && linkram[0x2001]==2);
    assert(regs[6]==1 && regs[5]==17 && irq_level && (status&2));
    /* A queued game record must not block a test control token behind it. */
    const uint8_t token[]={0x80,4,0,0,0,99};
    assert(send(sockets[1],expected,6,0)==6); assert(send(sockets[1],token,6,0)==6); network_poll();
    assert(peer_frame==99 && incoming_len==6 && regs[6]==1);
    tc2_link_write(0,status&~2,regs); assert(!(status&2) && !irq_level);
    network_poll(); assert(regs[6]==3 && incoming_len==0);
    /* Cable removal is nonfatal and leaves ROM link-loss handling active. */
    close(sockets[1]); network_poll(); assert(!phase && enabled);
    puts("PASS: TX setup/DMA/completion, RX wrap/ack, control queue, solo and disconnect");
}
