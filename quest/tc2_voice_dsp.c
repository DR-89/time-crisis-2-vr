/* tc2_voice_dsp.c -- see tc2_voice_dsp.h.
 *
 * Measured on synthetic speech (a partner's voice played back 60-150 ms late through a small room, 250 ms filter): the echo falls
 * by 13-17 dB within the first seconds and by about 30 dB once the filter has learned the room; near speech keeps its level
 * (0.95) during double talk; fan noise and gunshot bursts score as speech in under 1% of blocks. About 0.15 ms per block on a
 * desktop core. SpeexDSP's residual-echo stage changed nothing measurable with its own denoiser off, so it is not used. */
#include "tc2_voice_dsp.h"
#include <rnnoise.h>
#include <speex/speex_echo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define REF_RING (1<<17)                 /* 2.7 s: the queue (up to 0.25 s ahead), the tail and the recorder's lag behind */
#define RESYNC   (VDSP_RATE/40)          /* an estimate 25 ms off the running position: a gap or a stall, start again there */

static SpeexEchoState *echo;static DenoiseState *noise;
static bool use_echo=true,use_noise=true;
static void (*lock_fn)(void),(*unlock_fn)(void);
static void lock(void){if(lock_fn)lock_fn();}
static void unlock(void){if(unlock_fn)unlock_fn();}

/* what was played, by its time on the shared clock; a slot whose tag is not its time is silence */
static int16_t ref[REF_RING];static int64_t tag[REF_RING];
static float test_gain;static int test_delay;

/* A running position on the shared clock. Blocks follow each other exactly; the times measured, averaged, only steer that
 * position, so their jitter (a device takes or delivers audio in bursts of ~10 ms) does not shake the echo path the filter has
 * learned: a lasting offset over `slack` (the clocks drift) is taken up a little at a time, one over 25 ms at once. The game's
 * output has gaps the microphone's has not: when the game is held (a stall, the link waiting for the partner) its queue runs dry
 * and the device plays silence. Then the next block goes out as it is queued, and the position jumps there: the delay from the
 * position to the microphone stays what the filter learned. (Measured on tests/voice_dsp_fixture.c: smoothing alone lost 10 dB
 * of echo to a stall every 7 s, and the device's bursts another 8 dB.) */
typedef struct { int64_t next;double err;bool set; } Track;
static Track out_track,mic_track;
static int64_t track(Track *t,int64_t est,bool dry,int n,int slack,double rate){
    if(!t->set||llabs(est-t->next)>RESYNC){t->next=est;t->err=0;t->set=true;}
    else if(dry&&est>t->next+VDSP_RATE/2000){t->next=est;t->err=0;}
    else{
        t->err=t->err*(1-rate)+(double)(est-t->next)*rate;
        if(fabs(t->err)>slack){const int64_t step=(int64_t)(t->err/8);t->next+=step;t->err-=(double)step;}
    }
    const int64_t at=t->next;t->next+=n;return at;
}

/* 88.2 -> 48 kHz: linear between the mono samples (only the echo filter's reference; the player hears the original) */
static double phase;static int last_mono;
void vdsp_played(const int16_t *s,int frames,int64_t play_at,bool queue_was_empty){
    if(frames<=0)return;
    const double step=88200.0/VDSP_RATE;
    int count=0;for(double p=phase;p<frames;p+=step)count++;
    lock();
    int64_t at=track(&out_track,play_at,queue_was_empty,count,VDSP_RATE*6/1000,.01);
    for(double p=phase;p<frames;p+=step,at++){
        const int i=(int)p;const double f=p-i;
        const int a=i==0?last_mono:(s[2*(i-1)]+s[2*(i-1)+1])/2,b=(s[2*i]+s[2*i+1])/2;
        ref[at&(REF_RING-1)]=(int16_t)(a+(b-a)*f);tag[at&(REF_RING-1)]=at;
    }
    unlock();
    phase+=count*step-frames;last_mono=(s[2*(frames-1)]+s[2*(frames-1)+1])/2;
}
static void played_block(int64_t at,int16_t *out,int n){
    for(int i=0;i<n;i++){const int64_t t=at+i;out[i]=tag[t&(REF_RING-1)]==t?ref[t&(REF_RING-1)]:0;}
}

static int16_t pend[VDSP_BLOCK];static int pend_n;static int64_t pend_at;
static void process(vdsp_out out){
    int16_t mic[VDSP_BLOCK],play[VDSP_BLOCK],clean[VDSP_BLOCK];
    memcpy(mic,pend,sizeof mic);
    lock();
    played_block(pend_at,play,VDSP_BLOCK);
    if(test_gain>0){
        int16_t e[VDSP_BLOCK];played_block(pend_at-test_delay,e,VDSP_BLOCK);
        for(int i=0;i<VDSP_BLOCK;i++){const int v=mic[i]+(int)lrintf(e[i]*test_gain);mic[i]=(int16_t)(v>32767?32767:v<-32768?-32768:v);}
    }
    unlock();
    if(use_echo&&echo)speex_echo_cancellation(echo,mic,play,clean);else memcpy(clean,mic,sizeof clean);
    float speech=1;
    if(use_noise&&noise){
        float in[VDSP_BLOCK],o[VDSP_BLOCK];
        for(int i=0;i<VDSP_BLOCK;i++)in[i]=clean[i];
        speech=rnnoise_process_frame(noise,o,in);
        for(int i=0;i<VDSP_BLOCK;i++){const float v=o[i];clean[i]=(int16_t)(v>32767?32767:v<-32768?-32768:lrintf(v));}
    }
    out(clean,VDSP_BLOCK,speech);
}
void vdsp_capture(const int16_t *mic,int n,int64_t now,vdsp_out out){
    if(n<=0)return;
    int64_t at=track(&mic_track,now-n,false,n,VDSP_RATE*2/1000,.05);
    for(int i=0;i<n;i++,at++){
        if(!pend_n)pend_at=at;
        pend[pend_n++]=mic[i];
        if(pend_n==VDSP_BLOCK){process(out);pend_n=0;}
    }
}

void vdsp_init(void){
    if(echo)return;
    echo=speex_echo_state_init(VDSP_BLOCK,VDSP_TAIL_MS*VDSP_RATE/1000);
    if(echo){int rate=VDSP_RATE;speex_echo_ctl(echo,SPEEX_ECHO_SET_SAMPLING_RATE,&rate);}
    noise=rnnoise_create(NULL);
}
void vdsp_reset(void){
    if(echo)speex_echo_state_reset(echo);
    if(noise)rnnoise_init(noise,NULL);
    lock();out_track.set=false;unlock();
    mic_track.set=false;pend_n=0;
}
void vdsp_set_options(bool echo_cancel,bool denoise){use_echo=echo_cancel;use_noise=denoise;}
void vdsp_set_lock(void (*l)(void),void (*u)(void)){lock_fn=l;unlock_fn=u;}
void vdsp_test_echo(float gain,int delay_ms){test_gain=gain;test_delay=delay_ms*VDSP_RATE/1000;}
