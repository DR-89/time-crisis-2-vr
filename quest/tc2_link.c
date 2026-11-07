/* tc2_link.c -- the C422 twin-cabinet link chip and its network framing. See tc2_link.h and docs/ONLINE.md.
 *
 * What the game's driver (lifted program) does with the chip:
 *   8000BEAC  the C422 interrupt (IP5, acknowledged by its caller's write to 0x0D000008): status bit 1 -> receive, bit 2 -> transmit
 *   8000BB6C  transmit: reg5 still busy -> re-arm; else send <= 0xFF words (reg3 = 3 .. 2 around it). reg7 is written once per packet
 *             (its word 2, after the length) and the CHIP advances it across chunks. Nothing queued -> TX stays masked (reg1 bit 2)
 *   8000BD24  receive: reg0 &= ~2, scan back from the word AT reg6 (receive ring = words 0x1000-0x1FFF) for bit 8, the trailer
 *             [len_hi][len_lo | 0x100]; the packet starts len-1 words before it with the 0x5A sync byte; byte sum 0
 *   8000BF38  once a frame (main loop): consumes received packets, builds the next one (three copies per frame are sent)
 * Registers (16-bit at 0x06400000 + 2n): 0 status (bit 1 received, bit 2 transmitter idle; a write without bit 1 clears it),
 * 1 interrupt mask (bit 1 RX, bit 2 TX; 1 = masked; the chip masks a cause as it interrupts, the handler re-arms it), 3 control,
 * 5 tx size (write n: send n words from reg7; reads non-zero while busy), 6 rx pointer (index of the LAST word written), 7 tx pointer.
 *
 * Disabled, every access behaves as the pinned upstream stub (registers store and read back, a write of 0xFFFB / 0x000F to reg 1
 * raises / drops IP5 for the POWER ON TEST) so offline play is byte-identical.
 *
 * On the network the link travels in upstream's Time Crisis 2 payload (below), so VR and desktop cabinets link with each other. */
#include "tc2_link.h"
#include "tc2_game.h"                    /* TC2_SRC_C422 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING        0x1000u
#define MAXW        0x800u
#define RXQ         48
#define RX_BUDGET   2048u                 /* words of received packets held for the game: half its 4096-word ring */
#define TICK_INS    8000u                 /* instructions between chip ticks (~350 a frame) */
#define GP          0x802C7820u
static unsigned link_frame(void){extern uint32_t rr_frame;return rr_frame;}

uint8_t *tc2_vram_ptr(uint32_t p);       /* the core's chip RAM windows: 0x06200000 is the C422's 16 KB buffer, big-endian */
void tc2_irq_set(int src,int on);
extern uint8_t tc2_ram[];

static bool enabled;
static uint16_t reg[8];
static bool rx_flag,tx_busy,line;
static uint16_t txp[MAXW];static unsigned txn;              /* the packet being sent, gathered across chunks */
static struct { uint16_t n,w[MAXW]; } rxq[RXQ];static int rxr,rxw;
static uint32_t ins_acc;
static tc2_link_status st;
/* the wire (see below): our transmitted link frames by id, what the partner sent last */
#define HIST 64
static struct { uint16_t id,n;uint16_t w[MAXW]; } hist[HIST];
static uint16_t next_id,rx_last;static bool rx_have;static uint32_t n_sent;
static int self_state=-1;                 /* our state as the partner sees it (its trailer); -1 unknown */
static bool partner_vr;                   /* the partner's trailer says it runs this build */
static uint32_t partner_frame;
static void history_add(void);

static uint8_t *c422_ram(void){static uint8_t *m;if(!m)m=tc2_vram_ptr(0x06200000u);return m;}
static uint16_t ram_rd(uint32_t i){const uint8_t *m=c422_ram();i&=0x1FFF;return (uint16_t)(m[i*2]<<8|m[i*2+1]);}
static void ram_wr(uint32_t i,uint16_t v){uint8_t *m=c422_ram();i&=0x1FFF;m[i*2]=(uint8_t)(v>>8);m[i*2+1]=(uint8_t)v;}
static void set_line(bool on){if(line!=on){line=on;tc2_irq_set(TC2_SRC_C422,on);}}

void tc2_link_set_enabled(bool on){
    if(on==enabled)return;
    enabled=on;rx_flag=tx_busy=false;txn=0;rxr=rxw=0;ins_acc=0;
    next_id=0;n_sent=0;rx_have=false;self_state=-1;memset(hist,0,sizeof hist);   /* a new link starts its numbering afresh (as upstream) */
    if(!on)set_line(false);
    fprintf(stderr,"[LINK] C422 link %s\n",on?"CONNECTED":"disconnected (single cabinet)");
}
bool tc2_link_enabled(void){return enabled;}

/* ---- the board side ---- */
static void packet_done(void){
    /* Valid packets carry bit 8 on the last word only; anything else cannot be framed as bytes. */
    for(unsigned k=0;k+1<txn;k++)if(txp[k]&0x100){
        if(st.tx_bad++<5)fprintf(stderr,"[LINK] unframeable packet: %u words, end mark at %u (frame %u)\n",txn,k,link_frame());
        txn=0;return;}
    history_add();
    st.tx++;txn=0;
}
static void transmit(unsigned n){
    uint32_t p=reg[7]&0x1FFF;
    for(unsigned k=0;k<n;k++){
        uint32_t i=p>=RING?RING+((p-RING+k)&0xFFF):((p+k)&0x1FFF);
        uint16_t w=(uint16_t)(ram_rd(i)&0x1FF);
        if(txn<MAXW)txp[txn++]=w;
        if(w&0x100)packet_done();
    }
    reg[7]=(uint16_t)((reg[7]&0xE000)|((p+n)&0x1FFF));
    tx_busy=true;
}
uint16_t tc2_link_read(unsigned r){
    r&=7;
    if(!enabled)return reg[r];
    if(r==0)return (uint16_t)((reg[0]&~6u)|(rx_flag?2:0)|(tx_busy?0:4));
    if(r==5)return tx_busy?reg[5]:0;
    return reg[r];
}
void tc2_link_write(unsigned r,uint16_t v){
    r&=7;
    if(r==1){
        if(v==0xFFFBu)set_line(true);           /* the POWER ON TEST's interrupt check (MAME c422_irq_w) */
        else if(v==0x000Fu)set_line(false);
        reg[1]=enabled?(uint16_t)(v&0xF):v;return;
    }
    reg[r]=v;
    if(!enabled)return;
    if(r==0&&!(v&2))rx_flag=false;
    if(r==5&&(v&0xFF)&&!tx_busy)transmit(v&0x1FF);
}
void tc2_link_ack(void){if(enabled)set_line(false);}
/* The core's C422 entry points (upstream src/tc2_chips.c, src/tc2_main.c); this model replaces upstream's src/tc2_link.c. */
int tc2_c422_read(unsigned k,uint32_t *v){*v=tc2_link_read(k);return 1;}
void tc2_c422_write(unsigned k,uint16_t v){tc2_link_write(k,v);}
void tc2_c422_load(unsigned k,uint16_t v){reg[k&7]=v;}   /* a MAME snapshot's registers */
void tc2_link_init(void){}                /* the online session is quest/tc2_online.c, driven by the VR host */
void tc2_link_frame(void){}               /* its per-frame work runs from quest/tc2_window.c (tc2_online_frame) */

static void place(int q){
    uint32_t p=reg[6]&0xFFF;
    for(unsigned k=0;k<rxq[q].n;k++){p=(p+1)&0xFFF;ram_wr(RING+p,rxq[q].w[k]);}
    reg[6]=(uint16_t)((reg[6]&0xF000)|p);
    rx_flag=true;st.rx++;
}
static void tick(void){
    tx_busy=false;
    if(!rx_flag&&rxr!=rxw){place(rxr);rxr=(rxr+1)%RXQ;}
    uint16_t m=0;
    if(rx_flag&&!(reg[1]&2))m|=2;
    if(!(reg[1]&4))m|=4;
    if(m&&!line){reg[1]|=m;set_line(true);}
}
void tc2_link_step(uint32_t n){
    if(!enabled)return;
    ins_acc+=n;
    while(ins_acc>=TICK_INS){ins_acc-=TICK_INS;tick();}
}

/* ---- the wire: upstream namco22-decompile's Time Crisis 2 payload (tc2/src/tc2_link.c, 0.9.x-0.10.x, MIT), so a VR cabinet links
 * with the upstream desktop game. Byte 0 = k, then k times { BE16 id, BE16 n, n bytes } oldest first: the newest transmitted link
 * frames that fit, one byte a word, bit 8 (end of frame) being the last word's. The driver's resends of a packet are carried once;
 * repeating recent frames in every payload means a lost datagram costs nothing, and the receiver gives the game each id once.
 * The last 8 bytes are the trailer: 'T' '2' and the byte 0x802F3502 its sender sees (the partner's state: 1 attract, 2 credited,
 * 4 in a game -- which tells the partner its OWN state), then the sender's frame, BE32. ---- */

static void history_add(void){
    unsigned prev=(uint16_t)(next_id-1)%HIST;
    bool dup=n_sent&&hist[prev].n==txn&&!memcmp(hist[prev].w,txp,txn*2);
    if(dup)return;
    hist[next_id%HIST].id=next_id;hist[next_id%HIST].n=(uint16_t)txn;memcpy(hist[next_id%HIST].w,txp,txn*2);
    next_id++;n_sent++;
}
int tc2_link_pack(uint8_t *out,int limit){
    if(!enabled)return 0;
    if(limit>TC2_LINK_PAYLOAD-TC2_LINK_TRAILER)limit=TC2_LINK_PAYLOAD-TC2_LINK_TRAILER;
    int k=0,bytes=1;
    while(k<HIST&&(uint32_t)k<n_sent&&k<255){
        int sz=4+hist[(uint16_t)(next_id-1-k)%HIST].n;
        if(bytes+sz>limit)break;
        bytes+=sz;k++;
    }
    out[0]=(uint8_t)k;int o=1;
    for(int i=k-1;i>=0;i--){
        uint16_t id=(uint16_t)(next_id-1-i);int h=id%HIST;
        out[o++]=(uint8_t)(id>>8);out[o++]=(uint8_t)id;out[o++]=(uint8_t)(hist[h].n>>8);out[o++]=(uint8_t)hist[h].n;
        for(int j=0;j<hist[h].n;j++)out[o++]=(uint8_t)hist[h].w[j];
    }
    return o;
}
bool tc2_link_pack_fits_newest(int limit){
    return !n_sent||1+4+hist[(uint16_t)(next_id-1)%HIST].n<=limit;
}
void tc2_link_trailer(uint8_t *payload){
    extern uint32_t rr_frame;uint8_t *t=payload+TC2_LINK_PAYLOAD-TC2_LINK_TRAILER;
    t[0]='T';t[1]='2';t[2]=tc2_ram[0x2F3502];t[3]='V';   /* byte 3: this build (upstream writes and reads nothing there) */
    t[4]=(uint8_t)(rr_frame>>24);t[5]=(uint8_t)(rr_frame>>16);t[6]=(uint8_t)(rr_frame>>8);t[7]=(uint8_t)rr_frame;
}
static void give(const uint16_t *w,unsigned n){
    /* A cable never holds more than a frame or two of traffic. After a stall the network hands over seconds of backlog at once;
     * placing it all would wrap the game's 4096-word receive ring over packets it has queued but not read (they then fail its
     * sync check without a trace). Keep at most half a ring of the newest packets; Wi-Fi clumps of a few packets still pass whole. */
    unsigned queued=0;for(int q=rxr;q!=rxw;q=(q+1)%RXQ)queued+=rxq[q].n;
    while(rxr!=rxw&&queued+n>RX_BUDGET){queued-=rxq[rxr].n;rxr=(rxr+1)%RXQ;st.rx_dropped++;}
    rxq[rxw].n=(uint16_t)n;memcpy(rxq[rxw].w,w,n*2);
    rxw=(rxw+1)%RXQ;
    if(rxw==rxr){rxr=(rxr+1)%RXQ;st.rx_dropped++;}
}
int tc2_link_unpack(const uint8_t *in){
    if(!enabled)return -1;
    const uint8_t *t=in+TC2_LINK_PAYLOAD-TC2_LINK_TRAILER;
    if(t[0]=='T'&&t[1]=='2'){self_state=t[2];partner_vr=t[3]=='V';}
    partner_frame=(uint32_t)t[4]<<24|(uint32_t)t[5]<<16|(uint32_t)t[6]<<8|t[7];
    int k=in[0],o=1;const int end=TC2_LINK_PAYLOAD-TC2_LINK_TRAILER;
    for(int i=0;i<k;i++){
        if(o+4>end){st.rx_bad++;return -1;}
        uint16_t id=(uint16_t)(in[o]<<8|in[o+1]);int n=in[o+2]<<8|in[o+3];o+=4;
        if(n<1||n>(int)MAXW||o+n>end){if(st.rx_bad++<5)fprintf(stderr,"[LINK] bad payload entry %d of %d (n %d)\n",i,k,n);return -1;}
        if(!rx_have||(int16_t)(id-rx_last)>0){
            uint16_t w[MAXW];for(int j=0;j<n;j++)w[j]=in[o+j];
            w[n-1]|=0x100;
            give(w,(unsigned)n);rx_last=id;rx_have=true;
        }
        o+=n;
    }
    return o;
}
int tc2_link_self_state(void){return enabled?self_state:-1;}
bool tc2_link_partner_vr(void){return enabled&&partner_vr;}
/* Every session (re)start, as upstream's session_begin: both sides number their link frames afresh, so a partner who left and
 * came back is not taken for old frames. The chip and the frames already waiting for the ring are kept. */
void tc2_link_session_reset(void){next_id=0;n_sent=0;rx_have=false;self_state=-1;partner_vr=false;memset(hist,0,sizeof hist);}

static uint16_t g16(uint32_t va){uint32_t o=va&0xFFFFFFu;return (uint16_t)(tc2_ram[o]<<8|tc2_ram[o+1]);}
void tc2_link_get_status(tc2_link_status *o){
    *o=st;o->enabled=enabled;
    o->alive=tc2_ram[0x2F3502]!=0;o->lag=g16(0x802F3504u);o->state=g16(GP+0x75BC);
    o->game_errors=(uint32_t)g16(GP+0x75C2)+g16(GP+0x75C4)+g16(GP+0x75C8);
}
