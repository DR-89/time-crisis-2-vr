/* tc2_voice.c -- see tc2_voice.h.
 *
 * Send: capture ring (any thread, locked) -> 20 ms frames on the game thread -> level gate (opens above about -42 dBFS, holds
 * 400 ms) -> Opus VOIP 20 kbit/s with in-band FEC -> queued, two frames per network frame.
 * Receive: frames by sequence number into a 64-slot jitter store -> decoded on the game thread into a 48 kHz playback ring kept
 * about 60 ms ahead, a missing frame rebuilt from the next frame's FEC or by Opus concealment -> read at the game's 88.2 kHz.
 *
 * On the network: a 'V' 'C' block in the link payload after the link frames, where upstream's own Time Crisis 2 ignores it (see
 * tc2_voice_pack). */
#include "tc2_voice.h"
#include <opus.h>
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#define RATE        48000
#define FRAME       960                   /* 20 ms */
#define CAP_RING    (RATE)                /* 1 s */
#define PLAY_RING   (RATE)
#define SLOTS       64
#define OUTQ        12
#define TARGET      (FRAME*3)             /* playback kept ~60 ms ahead */
#define GATE_ON     260                   /* RMS of int16 samples: ~ -42 dBFS */
#define HANGOVER    20                    /* frames the gate stays open after speech (400 ms) */

static bool inited,enabled=true,muted,active;
static int volume=100;
static OpusEncoder *enc;static OpusDecoder *dec;

/* capture ring: written by the capture thread, read by the game thread */
static SDL_mutex *cap_lock;static int16_t cap[CAP_RING];static int cap_r,cap_w;
static bool capturing,mic_denied;
#ifndef __ANDROID__
static SDL_AudioDeviceID cap_dev;
#endif

/* send side */
static struct { uint16_t seq;uint8_t n;uint8_t b[160]; } outq[OUTQ];static int out_r,out_w;
static uint16_t tx_seq;static int gate_hold;static int level;static uint32_t first_queued_ms;
/* receive side */
static struct { bool full;uint16_t seq;uint8_t n;uint8_t b[160]; } slot[SLOTS];
static bool playing;static uint16_t next_seq;static uint32_t last_rx_ms;static int partner_level;
static int16_t play[PLAY_RING];static int play_r,play_w;static uint32_t play_frac;   /* frac: 16.16 position between samples */
static uint32_t n_sent,n_received,n_lost;

/* test hooks: TC2_VOICE_IN=<48 kHz mono 16-bit WAV> replaces the microphone; TC2_VOICE_OUT=<WAV> records what is played */
static FILE *test_in,*test_out;static uint32_t test_out_samples;static double test_in_acc;

static int play_fill(void){return (play_w-play_r+PLAY_RING)%PLAY_RING;}
static int cap_fill(void){return (cap_w-cap_r+CAP_RING)%CAP_RING;}

void tc2_voice_capture(const int16_t *pcm,int n){
    if(!cap_lock)return;
    SDL_LockMutex(cap_lock);
    for(int i=0;i<n;i++){cap[cap_w]=pcm[i];cap_w=(cap_w+1)%CAP_RING;if(cap_w==cap_r)cap_r=(cap_r+1)%CAP_RING;}
    SDL_UnlockMutex(cap_lock);
}

/* ---- the microphone ---- */
#ifdef __ANDROID__
static void java_call(const char *method){
    JNIEnv *env=(JNIEnv*)SDL_AndroidGetJNIEnv();jobject activity=(jobject)SDL_AndroidGetActivity();
    if(!env||!activity)return;
    jclass cls=(*env)->GetObjectClass(env,activity);jmethodID m=(*env)->GetMethodID(env,cls,method,"()V");
    if(m)(*env)->CallVoidMethod(env,activity,m);
    if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env,cls);(*env)->DeleteLocalRef(env,activity);
}
JNIEXPORT void JNICALL Java_org_timecrisis2_arcadevr_MainActivity_nativeVoicePcm(JNIEnv *env,jclass cls,jshortArray pcm,jint n){
    (void)cls;jshort *s=(*env)->GetShortArrayElements(env,pcm,NULL);
    if(s){tc2_voice_capture((const int16_t*)s,n);(*env)->ReleaseShortArrayElements(env,pcm,s,JNI_ABORT);}
}
JNIEXPORT void JNICALL Java_org_timecrisis2_arcadevr_MainActivity_nativeVoiceState(JNIEnv *env,jclass cls,jint state){
    (void)env;(void)cls;
    /* 1 recording (echo cancellation on), 2 recording without echo cancellation, -1 microphone permission refused, -2 failed */
    mic_denied=state==-1;
    fprintf(stderr,"[VOICE] microphone %s\n",state==1?"on (echo cancellation)":state==2?"on (no echo cancellation available)":state==-1?"permission refused":"failed");
}
static void mic_open(void){if(!capturing){java_call("voiceStart");capturing=true;}}
static void mic_close(void){if(capturing){java_call("voiceStop");capturing=false;}}
#else
static void SDLCALL cap_callback(void *u,Uint8 *stream,int len){(void)u;tc2_voice_capture((const int16_t*)stream,len/2);}
static void mic_open(void){
    if(capturing||test_in)return;
    SDL_AudioSpec want,have;SDL_zero(want);want.freq=RATE;want.format=AUDIO_S16SYS;want.channels=1;want.samples=480;want.callback=cap_callback;
    cap_dev=SDL_OpenAudioDevice(NULL,1,&want,&have,0);
    if(!cap_dev){fprintf(stderr,"[VOICE] no microphone: %s\n",SDL_GetError());mic_denied=true;return;}
    SDL_PauseAudioDevice(cap_dev,0);capturing=true;mic_denied=false;
    fprintf(stderr,"[VOICE] microphone on\n");
}
static void mic_close(void){if(cap_dev){SDL_CloseAudioDevice(cap_dev);cap_dev=0;}capturing=false;}
#endif

static void wav_out_header(void){
    if(!test_out)return;
    uint32_t bytes=test_out_samples*2;uint8_t h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0};
    #define P32(o,v) do{h[o]=(uint8_t)(v);h[o+1]=(uint8_t)((v)>>8);h[o+2]=(uint8_t)((v)>>16);h[o+3]=(uint8_t)((v)>>24);}while(0)
    P32(4,36+bytes);P32(24,RATE);P32(28,RATE*2);h[32]=2;h[34]=16;memcpy(h+36,"data",4);P32(40,bytes);
    long pos=ftell(test_out);fseek(test_out,0,SEEK_SET);fwrite(h,1,44,test_out);fseek(test_out,pos,SEEK_SET);fflush(test_out);
}
static void test_close(void){if(test_out){wav_out_header();fclose(test_out);test_out=NULL;}}
void tc2_voice_test_output(const char *path){
    if(test_out||!path||!(test_out=fopen(path,"wb")))return;
    fseek(test_out,44,SEEK_SET);atexit(test_close);
}

void tc2_voice_init(void){
    if(inited)return;inited=true;
    int err=0;
    enc=opus_encoder_create(RATE,1,OPUS_APPLICATION_VOIP,&err);
    if(enc){opus_encoder_ctl(enc,OPUS_SET_BITRATE(20000));opus_encoder_ctl(enc,OPUS_SET_INBAND_FEC(1));opus_encoder_ctl(enc,OPUS_SET_PACKET_LOSS_PERC(10));
            opus_encoder_ctl(enc,OPUS_SET_COMPLEXITY(5));opus_encoder_ctl(enc,OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));}
    dec=opus_decoder_create(RATE,1,&err);
    cap_lock=SDL_CreateMutex();
    const char *in=getenv("TC2_VOICE_IN"),*out=getenv("TC2_VOICE_OUT");
    if(in&&(test_in=fopen(in,"rb")))fseek(test_in,44,SEEK_SET);
    if(out&&(test_out=fopen(out,"wb"))){fseek(test_out,44,SEEK_SET);atexit(test_close);}
    fprintf(stderr,"[VOICE] %s (Opus %s)\n",enc&&dec?"ready":"codec unavailable",opus_get_version_string());
}
void tc2_voice_set_enabled(bool on){enabled=on;}
void tc2_voice_set_muted(bool m){muted=m;}
void tc2_voice_set_volume(int v){volume=v<0?0:v>200?200:v;}
bool tc2_voice_enabled(void){return enabled;}
bool tc2_voice_muted(void){return muted;}
int  tc2_voice_volume(void){return volume;}

void tc2_voice_set_active(bool linked){
    bool want=inited&&linked&&enabled&&enc&&dec;
    if(want==active)return;
    active=want;
    if(active){mic_open();out_r=out_w=0;gate_hold=0;memset(slot,0,sizeof slot);playing=false;play_r=play_w=0;}
    else{mic_close();playing=false;play_r=play_w=0;out_r=out_w=0;}
}

/* ---- send ---- */
static void encode_frame(const int16_t *pcm){
    double sum=0;for(int i=0;i<FRAME;i++)sum+=(double)pcm[i]*pcm[i];
    level=(int)sqrt(sum/FRAME);
    /* Echo safeguard (speakerphone-style): while the partner's voice plays, our microphone must be four times louder to open
     * the gate, so their voice leaking from the headset's speakers is not sent back to them. Quest 3 offers no Android echo
     * canceller effect (measured); its voice-communication input already attenuates the speakers. */
    int threshold=active&&(SDL_GetTicks()-last_rx_ms<300||play_fill()>FRAME)?GATE_ON*4:GATE_ON;
    if(level>threshold)gate_hold=HANGOVER;else if(gate_hold>0)gate_hold--;
    uint8_t b[160];
    int n=opus_encode(enc,pcm,FRAME,b,sizeof b);          /* every frame: the encoder's state stays continuous across pauses */
    uint16_t seq=tx_seq++;
    if(n<=0||muted||gate_hold<=0)return;
    if((out_w+1)%OUTQ==out_r)out_r=(out_r+1)%OUTQ;         /* never let a stalled link build up old speech */
    if(out_r==out_w)first_queued_ms=SDL_GetTicks();
    outq[out_w].seq=seq;outq[out_w].n=(uint8_t)n;memcpy(outq[out_w].b,b,(size_t)n);out_w=(out_w+1)%OUTQ;
}
bool tc2_voice_pending(void){
    int queued=(out_w-out_r+OUTQ)%OUTQ;
    return active&&queued>0&&(queued>=2||SDL_GetTicks()-first_queued_ms>=30||gate_hold==0);
}
/* The voice block inside a link payload: 'V' 'C', length (LE16) of what follows, then flags (bit 0 talking), the first Opus
 * frame's sequence number (LE16), the frame count, and per frame its length byte and data. An empty block (no frames) still
 * tells the partner that this cabinet has voice chat. */
int tc2_voice_pack(uint8_t *out,int max){
    if(!inited||!enabled||max<9)return 0;
    out[0]='V';out[1]='C';
    int off=4;
    out[off++]=gate_hold>0&&!muted?1:0;
    uint16_t first=out_r!=out_w?outq[out_r].seq:0;
    out[off++]=(uint8_t)first;out[off++]=(uint8_t)(first>>8);
    int count_at=off++,count=0;
    if(tc2_voice_pending()){
        uint16_t expect=first;
        while(out_r!=out_w&&count<4&&outq[out_r].seq==expect&&off+1+outq[out_r].n<=max){
            out[off++]=outq[out_r].n;memcpy(out+off,outq[out_r].b,outq[out_r].n);off+=outq[out_r].n;
            out_r=(out_r+1)%OUTQ;count++;expect++;
        }
        n_sent+=(uint32_t)count;
        if(out_r!=out_w)first_queued_ms=SDL_GetTicks();
    }
    out[count_at]=(uint8_t)count;
    out[2]=(uint8_t)(off-4);out[3]=(uint8_t)((off-4)>>8);
    return off;
}
int tc2_voice_wanted_bytes(void){
    if(!inited||!enabled)return 0;
    int bytes=8;
    if(tc2_voice_pending())for(int q=out_r,c=0;q!=out_w&&c<4;q=(q+1)%OUTQ,c++)bytes+=1+outq[q].n;
    return bytes;
}

/* ---- receive ---- */
static uint32_t partner_voice_ms;
bool tc2_voice_unpack(const uint8_t *block,int avail){
    if(avail<8||block[0]!='V'||block[1]!='C')return false;
    int len=block[2]|block[3]<<8;if(len<4||4+len>avail)return false;
    partner_voice_ms=SDL_GetTicks();
    if(!active)return true;
    const uint8_t *in=block+4;
    uint16_t seq=(uint16_t)(in[1]|in[2]<<8);int count=in[3],off=4;
    for(int i=0;i<count&&off<len;i++,seq++){
        int n=in[off++];if(!n||off+n>len)break;
        int s=seq%SLOTS;slot[s].full=true;slot[s].seq=seq;slot[s].n=(uint8_t)n;memcpy(slot[s].b,in+off,(size_t)n);off+=n;
        n_received++;
        if(!playing){playing=true;next_seq=seq;}           /* a talk spurt starts: play from its first frame */
        last_rx_ms=SDL_GetTicks();
    }
    return true;
}
static void play_push(const int16_t *pcm,int n){
    long sum=0;
    for(int i=0;i<n;i++){
        play[play_w]=pcm[i];play_w=(play_w+1)%PLAY_RING;if(play_w==play_r)play_r=(play_r+1)%PLAY_RING;
        sum+=labs((long)pcm[i]);
    }
    partner_level=(int)(sum/(n?n:1));
    if(test_out){fwrite(pcm,2,(size_t)n,test_out);test_out_samples+=(uint32_t)n;if(test_out_samples%(RATE*2)<(uint32_t)n)wav_out_header();}   /* a headset app is killed, never exits: keep the header current */
}
static bool have(uint16_t seq){int s=seq%SLOTS;return slot[s].full&&slot[s].seq==seq;}
static void decode_ahead(void){
    if(!playing)return;
    int16_t pcm[FRAME];
    int guard=0;
    while(play_fill()<TARGET&&guard++<6){
        if(have(next_seq)){
            int s=next_seq%SLOTS;int n=opus_decode(dec,slot[s].b,slot[s].n,pcm,FRAME,0);slot[s].full=false;
            if(n>0)play_push(pcm,n);
            next_seq++;continue;
        }
        /* missing: is the stream still going? */
        int later=-1;for(int d=1;d<=6;d++)if(have((uint16_t)(next_seq+d))){later=d;break;}
        if(later<0){
            if(SDL_GetTicks()-last_rx_ms>250){playing=false;opus_decoder_ctl(dec,OPUS_RESET_STATE);}   /* the spurt ended */
            break;
        }
        int n;
        if(later==1){int s=(next_seq+1)%SLOTS;n=opus_decode(dec,slot[s].b,slot[s].n,pcm,FRAME,1);}   /* rebuilt from the next frame's FEC */
        else n=opus_decode(dec,NULL,0,pcm,FRAME,0);                                                  /* concealment */
        if(n>0)play_push(pcm,n);
        n_lost++;next_seq++;
    }
    if(play_fill()>RATE/4){play_r=(play_w-TARGET+PLAY_RING)%PLAY_RING;}   /* drifted far ahead (the game audio stalled): catch up */
}

void tc2_voice_tick(void){
    if(!inited)return;
    if(test_in&&active){                                     /* the test file plays at real time, one arcade frame's worth */
        test_in_acc+=RATE/59.906;int n=(int)test_in_acc;test_in_acc-=n;
        int16_t buf[1024];if(n>1024)n=1024;
        size_t got=fread(buf,2,(size_t)n,test_in);if(got<(size_t)n)memset(buf+got,0,((size_t)n-got)*2);
        tc2_voice_capture(buf,n);
    }
    if(!active){if(cap_lock){SDL_LockMutex(cap_lock);cap_r=cap_w;SDL_UnlockMutex(cap_lock);}return;}
    int16_t frame[FRAME];
    for(;;){
        SDL_LockMutex(cap_lock);
        bool ready=cap_fill()>=FRAME;
        if(ready)for(int i=0;i<FRAME;i++){frame[i]=cap[cap_r];cap_r=(cap_r+1)%CAP_RING;}
        SDL_UnlockMutex(cap_lock);
        if(!ready)break;
        encode_frame(frame);
    }
    decode_ahead();
}

int tc2_voice_mix(void){
    if(!active||play_fill()<2)return 0;
    int a=play[play_r],b=play[(play_r+1)%PLAY_RING];
    int v=a+(int)(((int64_t)(b-a)*(int64_t)(play_frac&0xFFFF))>>16);
    play_frac+=(uint32_t)((48000ull<<16)/88200ull);
    while(play_frac>=0x10000){play_frac-=0x10000;play_r=(play_r+1)%PLAY_RING;if(play_fill()<2)break;}
    return v*volume/100;
}

void tc2_voice_get_status(tc2_voice_status *o){
    memset(o,0,sizeof *o);
    o->active=active;o->capturing=capturing||test_in;o->talking=active&&!muted&&gate_hold>0;o->mic_denied=mic_denied;
    o->partner_talking=active&&SDL_GetTicks()-last_rx_ms<300;o->partner_has_voice=partner_voice_ms&&SDL_GetTicks()-partner_voice_ms<3000;o->level=level;o->partner_level=o->partner_talking?partner_level:0;
    o->sent=n_sent;o->received=n_received;o->lost=n_lost;
}
