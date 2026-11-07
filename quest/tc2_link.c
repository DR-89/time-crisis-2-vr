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
 * Network payload (TC2_LINK_PAYLOAD bytes, little-endian), one fragment of the newest packet:
 *   0 magic 0xC4   1 copies sent (1..15) | 0x80 deflated   2 packet id   3 fragment index << 4 | fragment count (1..8)
 *   4..5 blob length   6.. this fragment's FRAG_DATA bytes of the blob
 * blob = the packet's low bytes (bit 8 is only ever set on the last word, re-created on receive), deflated when that is smaller. */
#include "tc2_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define IP_C422     0x2000u
#define RING        0x1000u
#define MAXW        0x800u
#define RXQ         48
#define RX_BACKLOG  6                     /* received packet copies held for the game: the newest two packets */
#define TICK_INS    8000u                 /* instructions between chip ticks (~350 a frame) */
#define FRAG_HEADER 6
#define FRAG_DATA   (TC2_LINK_PAYLOAD-FRAG_HEADER)
#define FRAG_MAX    8
#define GP          0x802C7820u
static unsigned tc2_link_frame(void){extern uint32_t rr_frame;return rr_frame;}

uint8_t *tc2_c422_ram(void);              /* the 16 KB buffer RAM, big-endian (generated tc2_mem.c adapter) */
void tc2_irq_line(uint32_t bit,int on);
extern uint8_t tc2_ram[];

static bool enabled;
static uint16_t reg[8];
static bool rx_flag,tx_busy,line;
static uint16_t txp[MAXW];static unsigned txn;              /* the packet being sent, gathered across chunks */
static struct { uint16_t n;uint8_t b[MAXW]; } sent;          /* the newest complete packet sent, not yet packed */
static unsigned sent_copies;static bool sent_new;
static struct { uint16_t n,w[MAXW]; } rxq[RXQ];static int rxr,rxw;
static uint32_t ins_acc;
static tc2_link_status st;
/* the wire: fragments of the newest packet out, reassembly in */
static uint8_t blob[MAXW+64];static unsigned blob_len,frag_next,frag_count,blob_copies;static bool blob_deflated;static uint8_t msg_id;
static uint8_t rx_blob[MAXW+64];static unsigned rx_have,rx_count,rx_len;static int rx_msg=-1;static bool rx_deflated;static unsigned rx_copies;

static uint16_t ram_rd(uint32_t i){const uint8_t *m=tc2_c422_ram();i&=0x1FFF;return (uint16_t)(m[i*2]<<8|m[i*2+1]);}
static void ram_wr(uint32_t i,uint16_t v){uint8_t *m=tc2_c422_ram();i&=0x1FFF;m[i*2]=(uint8_t)(v>>8);m[i*2+1]=(uint8_t)v;}
static void set_line(bool on){if(line!=on){line=on;tc2_irq_line(IP_C422,on);}}

void tc2_link_set_enabled(bool on){
    if(on==enabled)return;
    enabled=on;rx_flag=tx_busy=false;txn=0;sent_new=false;sent_copies=0;rxr=rxw=0;ins_acc=0;
    frag_next=frag_count=0;rx_msg=-1;rx_have=0;
    if(!on)set_line(false);
    fprintf(stderr,"[LINK] C422 link %s\n",on?"CONNECTED":"disconnected (single cabinet)");
}
bool tc2_link_enabled(void){return enabled;}

/* ---- the board side ---- */
static void packet_done(void){
    /* Valid packets carry bit 8 on the last word only; anything else cannot be framed as bytes. */
    for(unsigned k=0;k+1<txn;k++)if(txp[k]&0x100){
        if(st.tx_bad++<5)fprintf(stderr,"[LINK] unframeable packet: %u words, end mark at %u (frame %u)\n",txn,k,tc2_link_frame());
        txn=0;return;}
    bool same=sent_new&&sent.n==txn;
    for(unsigned k=0;same&&k<txn;k++)same=sent.b[k]==(uint8_t)txp[k];
    if(same)sent_copies++;
    else{sent.n=(uint16_t)txn;for(unsigned k=0;k<txn;k++)sent.b[k]=(uint8_t)txp[k];sent_copies=1;sent_new=true;}
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

/* ---- the wire ---- */
/* The newest packet becomes a blob (raw bytes, or deflated when that is smaller), cut into fragments of FRAG_DATA bytes. Most
 * packets fit one network frame; gameplay packets of 380-470 bytes take two or three. A lost fragment loses that packet, as a
 * lost packet on the cabinet cable would; the next frame's packet supersedes it. */
static bool make_blob(void){
    unsigned copies=sent_copies>15?15:sent_copies;
    blob_deflated=false;blob_len=sent.n;memcpy(blob,sent.b,sent.n);
    if(sent.n>FRAG_DATA){
        uint8_t z[MAXW+64];uLongf len=sizeof z;
        if(compress2(z,&len,sent.b,sent.n,6)==Z_OK&&len<sent.n){memcpy(blob,z,len);blob_len=(unsigned)len;blob_deflated=true;}
    }
    frag_count=(blob_len+FRAG_DATA-1)/FRAG_DATA;
    if(!frag_count||frag_count>FRAG_MAX){if(st.tx_bad++<8)fprintf(stderr,"[LINK] packet of %u bytes is too large to send\n",sent.n);return false;}
    frag_next=0;blob_copies=copies;msg_id++;
    return true;
}
int tc2_link_pack(uint8_t *out){
    if(!enabled)return 0;
    if(frag_next>=frag_count){
        if(!sent_new)return 0;
        sent_new=false;
        if(!make_blob()){sent_copies=0;return 0;}
        sent_copies=0;
    }
    unsigned off=frag_next*FRAG_DATA,n=blob_len-off;if(n>FRAG_DATA)n=FRAG_DATA;
    memset(out,0,TC2_LINK_PAYLOAD);
    out[0]=0xC4;out[1]=(uint8_t)(blob_copies|(blob_deflated?0x80:0));out[2]=msg_id;out[3]=(uint8_t)(frag_next<<4|frag_count);
    out[4]=(uint8_t)blob_len;out[5]=(uint8_t)(blob_len>>8);
    memcpy(out+FRAG_HEADER,blob+off,n);
    frag_next++;
    return 1;
}
bool tc2_link_pending(void){return enabled&&frag_next<frag_count;}

bool tc2_link_unpack(const uint8_t *in){
    if(!enabled)return false;
    unsigned copies=in[1]&15,index=in[3]>>4,count=in[3]&15,len=in[4]|in[5]<<8;
    if(in[0]!=0xC4||!copies||!count||count>FRAG_MAX||index>=count||!len||len>count*FRAG_DATA||len>sizeof rx_blob){
        if(st.rx_bad++<5)fprintf(stderr,"[LINK] bad payload %02X %02X %02X %02X\n",in[0],in[1],in[2],in[3]);return false;
    }
    if(rx_msg!=in[2]){                       /* a new packet: any unfinished older one is lost */
        if(rx_msg>=0&&rx_have!=(1u<<rx_count)-1u)st.rx_dropped++;
        rx_msg=in[2];rx_have=0;rx_count=count;rx_len=len;rx_deflated=(in[1]&0x80)!=0;rx_copies=copies;
    }
    if(count!=rx_count||len!=rx_len)return false;
    unsigned off=index*FRAG_DATA,n=len-off;if(n>FRAG_DATA)n=FRAG_DATA;
    memcpy(rx_blob+off,in+FRAG_HEADER,n);rx_have|=1u<<index;
    if(rx_have!=(1u<<count)-1u)return true;
    rx_msg=-1;rx_have=0;
    uint8_t b[MAXW];uLongf words=len;
    if(rx_deflated){words=MAXW;if(uncompress(b,&words,rx_blob,len)!=Z_OK||!words){st.rx_bad++;return false;}}
    else memcpy(b,rx_blob,len);
    if(rx_copies>3)rx_copies=3;
    /* A cable never holds more than a frame or two of traffic. After a stall the network hands over seconds of backlog at once;
     * placing it all would wrap the game's 4096-word receive ring over packets it has queued but not read (they then fail its
     * sync check without a trace). Keep only the newest packets. */
    while((unsigned)((rxw-rxr+RXQ)%RXQ)+rx_copies>RX_BACKLOG){rxr=(rxr+1)%RXQ;st.rx_dropped++;}
    for(unsigned c=0;c<rx_copies;c++){
        rxq[rxw].n=(uint16_t)words;
        for(unsigned k=0;k<words;k++)rxq[rxw].w[k]=b[k];
        rxq[rxw].w[words-1]|=0x100;
        rxw=(rxw+1)%RXQ;
        if(rxw==rxr){rxr=(rxr+1)%RXQ;st.rx_dropped++;}
    }
    return true;
}

static uint16_t g16(uint32_t va){uint32_t o=va&0xFFFFFFu;return (uint16_t)(tc2_ram[o]<<8|tc2_ram[o+1]);}
void tc2_link_get_status(tc2_link_status *o){
    *o=st;o->enabled=enabled;
    o->alive=tc2_ram[0x2F3502]!=0;o->lag=g16(0x802F3504u);o->state=g16(GP+0x75BC);
    o->game_errors=(uint32_t)g16(GP+0x75C2)+g16(GP+0x75C4)+g16(GP+0x75C8);
}
