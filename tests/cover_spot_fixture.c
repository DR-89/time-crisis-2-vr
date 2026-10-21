/* Physical cover at the arcade's own slide: the RAM/ROM decoder, the
 * directional pedal and the SAFE atlas, with synthetic big-endian memory. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../quest/tc2_cover_spot.h"
#include "../quest/quest_cover.h"
#include "../quest/quest_safe.h"

static uint8_t ram[0x400000],data[0x100];
static void be32(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void be16(uint8_t *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static bool near(float a,float b){return fabsf(a-b)<.002f;}
static int16_t degrees(float d){return (int16_t)lrintf(d*65536.f/360.f);}
/* Tables at RAM 0x100000 (KSEG0 80100000), n entries; entry 0 hidden, n-1 exposed. */
static void install(int n,int index,uint32_t state,const int32_t exposed[3],const int32_t hidden[3],int16_t pitch,int16_t yaw,int16_t hidden_yaw){
    memset(ram,0,sizeof ram);
    uint8_t *camera=ram+QTC2_COVER_STRUCT,*descriptor=camera+0xE0;
    be32(camera+0x5C,state);be16(camera+0x68,(uint16_t)index);be16(descriptor,0xFFFF);be16(descriptor+2,(uint16_t)n);
    uint32_t base=0x80100000u;
    for(int k=0;k<6;k++){
        /* Halfword-aligned tables, as in the data ROM. */
        uint32_t table=base+k*0x102u;be32(descriptor+4+4*k,table);uint8_t *t=ram+(table&0x0FFFFFFF);
        for(int e=0;e<n;e++){
            float f=(float)e/(n-1);
            if(k<3)be32(t+4*e,(uint32_t)(int32_t)lrintf(hidden[k]+(exposed[k]-hidden[k])*f));
            else{int16_t value=k==3?pitch:k==4?(int16_t)lrintf(hidden_yaw+(yaw-hidden_yaw)*f):0;be16(t+2*e,(uint16_t)value);}
        }
    }
}
int main(void){
    QCoverSpot spot;
    /* Measured red Stage 1 spot 2: straight down 7168 raw with the camera
     * pitched -27 deg reads as up -0.390 / forward +0.199 m in camera space. */
    int32_t exposed[3]={1000,16000,-5000},hidden[3]={1000,16000-7168,-5000};
    install(18,17,3,exposed,hidden,degrees(-27),degrees(-9),degrees(-9));
    assert(qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot)&&spot.exposed&&spot.progress==0);
    assert(near(spot.hide.x,0)&&near(spot.hide.y,-.390f)&&near(spot.hide.z,-.199f)&&near(spot.turn_deg,0));
    /* Halfway through the slide and fully hidden. */
    install(18,8,1,exposed,hidden,degrees(-27),degrees(-9),degrees(-9));
    assert(qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot)&&!spot.exposed&&near(spot.progress,9.f/17));
    install(18,0,1,exposed,hidden,degrees(-27),degrees(-9),degrees(-9));
    assert(qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot)&&spot.progress==1&&near(spot.hide.y,-.390f));
    /* Yaw 90 deg: world -z is the camera's right; a turning slide reports its turn. */
    int32_t side[3]={1000,16000,-5000-16384};
    install(18,17,3,exposed,side,0,degrees(90),degrees(170));
    assert(qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot)&&near(spot.hide.x,1)&&near(spot.hide.y,0)&&near(spot.hide.z,0));
    assert(fabsf(spot.turn_deg-80)<.1f);
    /* Not a cover table, malformed, out of range, or a degenerate slide. */
    install(18,17,2,exposed,hidden,0,0,0);assert(!qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot)&&!spot.valid);
    install(18,18,3,exposed,hidden,0,0,0);assert(!qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot));
    install(18,17,3,exposed,exposed,0,0,0);assert(!qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot));
    install(18,17,3,exposed,hidden,0,0,0);be16(ram+QTC2_COVER_STRUCT+0xE0,0);assert(!qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot));
    install(18,17,3,exposed,hidden,0,0,0);be32(ram+QTC2_COVER_STRUCT+0xE4,0x8FFFFFFEu);assert(!qtc2_cover_spot(ram,sizeof ram,data,sizeof data,&spot));
    assert(!qtc2_cover_spot(ram,0x100000,data,sizeof data,&spot));
    /* Data ROM addresses map as tc2_mem.c does: KSEG1 A8xxxxxx -> tc2_data. */
    data[0x10]=0x12;assert(qtc2_cover_bytes(0xA8000010u,1,ram,sizeof ram,data,sizeof data)==data+0x10);
    assert(!qtc2_cover_bytes(0xA80000FFu,2,ram,sizeof ram,data,sizeof data));

    /* The mark: a comfortable lean, never longer than the slide. */
    assert(near(qcover_reach(v3(0,-.23f,0)),.23f)&&near(qcover_reach(v3(-1.2f,0,0)),.35f)&&near(qcover_reach(v3(0,-.08f,0)),.15f));
    QCover cover={0};float approach;
    assert(!qcover_spot_pedal(&cover,v3(-.6f,0,0),v3(0,0,0),true,&approach)); /* uncalibrated: hidden */
    qcover_calibrate(&cover,1.7f);
    V3 left=v3(-.6f,0,0);
    assert(qcover_spot_pedal(&cover,left,v3(0,0,0),true,&approach)&&approach==0);
    assert(qcover_spot_pedal(&cover,left,v3(-.2f,-.1f,0),true,&approach)&&near(approach,.2f/.35f));
    /* Ducking does not count at a sideways spot; leaning the wrong way neither. */
    assert(qcover_spot_pedal(&cover,left,v3(0,-.5f,0),true,&approach));
    assert(qcover_spot_pedal(&cover,left,v3(.5f,0,0),true,&approach)&&approach==0);
    assert(!qcover_spot_pedal(&cover,left,v3(-.36f,0,0),true,&approach)&&approach==1&&cover.ducked);
    assert(!qcover_spot_pedal(&cover,left,v3(-.2f,0,0),true,NULL));           /* inside the halfway band */
    assert(qcover_spot_pedal(&cover,left,v3(-.17f,0,0),true,NULL)&&!cover.ducked);
    assert(!qcover_spot_pedal(&cover,left,v3(0,0,0),false,NULL));             /* lost tracking hides */
    assert(!qcover_spot_pedal(&cover,v3(0,0,0),v3(0,0,0),true,NULL));

    /* The SAFE atlas: an opaque plate, yellow text, a yellow arrow with a rim. */
    static unsigned char atlas[256*192*4];qsafe_paint(atlas);
    int plate=0,text=0,arrow=0,rim=0;
    for(int y=0;y<64;y++)for(int x=0;x<256;x++){
        const unsigned char *a=atlas+(y*256+x)*4,*b=atlas+((64+y)*256+x)*4,*c=atlas+((128+y)*256+x)*4;
        plate+=a[3]==255;text+=b[3]&&b[0]==255&&b[1]==212;arrow+=c[3]&&c[0]==255&&c[1]==212;rim+=c[3]&&c[0]==20;
    }
    assert(plate==256*64&&text>1500&&arrow>3000&&rim>500);
    FILE *f=fopen(getenv("SAFE_ATLAS")?getenv("SAFE_ATLAS"):"/dev/null","wb");
    if(f){fprintf(f,"P6\n256 192\n255\n");for(int i=0;i<256*192;i++){unsigned char *p=atlas+i*4;int a=p[3];
        unsigned char rgb[3]={(unsigned char)((p[0]*a+64*(255-a))/255),(unsigned char)((p[1]*a+96*(255-a))/255),(unsigned char)((p[2]*a+64*(255-a))/255)};fwrite(rgb,1,3,f);}fclose(f);}
    printf("PASS: cover slide decode (pitch, yaw, turn, progress, ROM mapping, rejects), directional pedal with hysteresis, SAFE atlas\n");
    return 0;
}
