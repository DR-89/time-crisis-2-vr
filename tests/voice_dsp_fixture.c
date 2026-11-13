/* The microphone's clean-up (quest/tc2_voice_dsp.c) on a simulated clock, timed the way the app's audio is:
 *  - the game thread queues one arcade frame of output (~1472 frames at 88.2 kHz) every 16.7 ms, 0-4 ms late, keeping ~35 ms
 *    queued, and every 7 s stalls for 60 ms (the link waiting for a slow partner) and then catches up;
 *  - the audio device takes 1024 frames from the queue every 11.6 ms and plays them, silence where the queue ran short;
 *  - the speaker reaches the microphone `latency` later through a small room (the device's buffers, the air, the recorder's);
 *  - the recorder delivers 960 samples every 20 ms, 0-6 ms late, with a fan's hiss.
 * The game's output is the partner's voice (talking 0-17 s and 25-42 s) over game noise (a hum, a shot every 3 s); the player
 * talks 12-27 s, over the partner for 5 s.
 * Built and checked by tests/test_voice_dsp.py: voice_dsp <far.wav> <near.wav> <latency ms> <echo gain> <out.wav> [stages] [ideal]
 * (stages: 1 echo cancellation, 2 noise suppression, 3 both; ideal: 1 no game jitter, 4 no recorder jitter, 8 no stalls).
 * Prints: the echo taken off once settled (28-42 s, the partner alone), the player's voice kept (alone and over the partner), how
 * often echo, noise and the player score as speech, and the cost per 10 ms block (with the simulation: an upper bound). */
#include "../quest/tc2_voice_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static short *load(const char *p,int *n){
    FILE *f=fopen(p,"rb");if(!f){perror(p);exit(2);}
    fseek(f,0,SEEK_END);long sz=ftell(f)-44;fseek(f,44,SEEK_SET);short *b=malloc(sz);
    if(fread(b,1,sz,f)!=(size_t)sz)exit(2);fclose(f);*n=(int)(sz/2);return b;
}
static void save(const char *p,const short *b,int n){
    FILE *f=fopen(p,"wb");unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0xBB,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a'};
    unsigned bytes=n*2,r=36+bytes;memcpy(h+4,&r,4);memcpy(h+40,&bytes,4);fwrite(h,1,44,f);fwrite(b,2,n,f);fclose(f);
}
static unsigned seed=7;
static double rnd(void){seed=seed*1664525u+1013904223u;return ((seed>>8)/16777216.0)*2-1;}

#define R 48000
#define SECONDS 50
#define N (R*SECONDS)
#define OUT_RATE 88200.0
static short far_line[N],near_line[N],game_noise[N],speaker[N+R],mic[N],out[N];static float speech_at[N/480+8];
static int out_n;
static void collect(const int16_t *b,int n,float sp){if(out_n+n<=N){memcpy(out+out_n,b,(size_t)n*2);speech_at[out_n/480]=sp;out_n+=n;}}
/* the queue between the game and the device: 88.2 kHz mono (both channels alike) */
#define QSIZE (1<<16)
static int16_t queue[QSIZE];static int q_r,q_w;
static int q_count(void){return (q_w-q_r+QSIZE)%QSIZE;}

int main(int argc,char **argv){
    if(argc<6){fprintf(stderr,"usage: far.wav near.wav latency_ms gain out.wav [stages] [ideal]\n");return 2;}
    int nf,nn;short *far=load(argv[1],&nf),*near=load(argv[2],&nn);
    const int latency=atoi(argv[3])*R/1000;const double gain=atof(argv[4]);
    const int ideal=argc>7?atoi(argv[7]):0;
    for(int i=0;i<nf&&i<N;i++){far_line[i]=far[i];if(25*R+i<N)far_line[25*R+i]=far[i];}
    for(int i=0;i<nn&&12*R+i<N;i++)near_line[12*R+i]=near[i];
    for(int i=0;i<N;i++){double g=400*sin(i*2*M_PI*110/R)+200*rnd();if((i/R)%3==1&&(i%R)<2400)g+=7000*rnd()*exp(-(i%R)/600.0);game_noise[i]=(short)g;}
    vdsp_init();vdsp_reset();
    if(argc>6)vdsp_set_options(atoi(argv[6])&1,atoi(argv[6])&2);
    for(int i=0;i<1536;i++)queue[q_w++]=0;                    /* ~17 ms of silence queued at the start, plus the first frame */
    static int16_t block[1480*2];
    double src=0,next_frame=0,next_callback=R*5/1000;int64_t next_chunk=960;int mic_sent=0,frame_no=0;
    const double frame_period=R/59.906,callback_period=1024*R/OUT_RATE;
    clock_t t0=clock();
    for(int64_t now=0;now<N;now++){
        while(now>=next_frame){   /* the game queues a frame (late frames come back to back: catching up) */
            const int frames=(int)((frame_no+1)*(OUT_RATE/59.906))-(int)(frame_no*(OUT_RATE/59.906));   /* 1472 or 1473: 88.2 kHz on average */
            for(int i=0;i<frames;i++){const int at=(int)(src+i*R/OUT_RATE);const int v=at<N?far_line[at]+game_noise[at]:0;
                block[2*i]=block[2*i+1]=(int16_t)(v>32767?32767:v<-32768?-32768:v);}
            const int waiting=q_count();
            vdsp_played(block,frames,waiting?now+(int64_t)(waiting*R/OUT_RATE):now+R/200,waiting==0);   /* as tc2_voice_played */
            for(int i=0;i<frames;i++){queue[q_w]=block[2*i];q_w=(q_w+1)%QSIZE;}
            src+=frames*R/OUT_RATE;frame_no++;
            next_frame=frame_no*frame_period+(ideal&1?0:(rnd()*.5+.5)*R*4/1000)+(!(ideal&8)&&frame_no%420==200?R*60/1000:0);
        }
        if(now>=next_callback){   /* the device takes a buffer: it plays from now on, silence where the queue ran short */
            for(int k=0;k<1024;k++){
                int16_t v=0;if(q_count()){v=queue[q_r];q_r=(q_r+1)%QSIZE;}
                const int64_t t=now+(int64_t)(k*R/OUT_RATE);if(t<N+R)speaker[t]=v;
            }
            next_callback+=callback_period;
        }
        /* the room: the speaker heard `latency` later, with reflections; the player's voice; a fan */
        {double e=0;const int64_t t=now-latency;
         if(t>=0)e+=gain*speaker[t];if(t-480>=0)e+=gain*.35*speaker[t-480];if(t-1500>=0)e-=gain*.2*speaker[t-1500];if(t-3100>=0)e+=gain*.1*speaker[t-3100];
         double m=near_line[now]+e+150*rnd();mic[now]=(short)(m>32767?32767:m<-32768?-32768:m);}
        if(now>=next_chunk){const int64_t arrive=now+(ideal&4?0:(int64_t)((rnd()*.5+.5)*R*6/1000));vdsp_capture(mic+mic_sent,960,arrive,collect);mic_sent+=960;next_chunk+=960;}
    }
    const double cost=(double)(clock()-t0)/CLOCKS_PER_SEC;
    save(argv[5],out,out_n);
    double ein=0,eout=0;for(int i=28*R;i<42*R&&i<out_n;i++){ein+=(double)mic[i]*mic[i];eout+=(double)out[i]*out[i];}
    double best=-2;int lag=0;
    for(int l=0;l<=2000;l+=10){double s=0,x=0,y=0;for(int i=18*R;i<24*R;i++){s+=(double)out[i+l]*near_line[i];x+=(double)out[i+l]*out[i+l];y+=(double)near_line[i]*near_line[i];}
        const double c=s/sqrt(x*y+1);if(c>best){best=c;lag=l;}}
    double s=0,x=0,y=0;for(int i=13*R;i<17*R;i++){s+=(double)out[i+lag]*near_line[i];x+=(double)out[i+lag]*out[i+lag];y+=(double)near_line[i]*near_line[i];}
    const double dt=s/sqrt(x*y+1),level=sqrt(x/(y+1));
    int sp_echo=0,sp_noise=0,sp_near=0,n_echo=0,n_noise=0,n_near=0;
    for(int b=0;b*480<out_n;b++){const double t=b*480.0/R;
        if(t>28&&t<42){n_echo++;sp_echo+=speech_at[b]>=.5f;}
        if(t>43&&t<50){n_noise++;sp_noise+=speech_at[b]>=.5f;}
        if(t>18&&t<24){n_near++;sp_near+=speech_at[b]>=.5f;}}
    printf("echo %.1f dB | near kept %.3f double-talk %.3f level %.2f lag %d | speech: echo %.1f%% noise %.1f%% near %.1f%% | %.3f ms per block\n",
        10*log10(ein/(eout+1)),best,dt,level,lag,100.0*sp_echo/n_echo,100.0*sp_noise/n_noise,100.0*sp_near/n_near,cost*1000/(N/480.0));
    return 0;
}
