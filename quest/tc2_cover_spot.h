#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vr_math.h"

/* Read-only adapter for the pinned US TSS3 Ver. B program's cover slide.
 * The local cabinet's camera struct is at RAM 0x2F4060 (KSEG0 802F4060):
 *   +0x00 int32 x,y,z        +0x0C int16 pitch,yaw,roll (65536 = 360 deg)
 *   +0x5C u32 state (bit0: +0xE0 holds a cover table, bit1: fully exposed)
 *   +0x68 int16 index (n-1 = exposed .. 0 = hidden)
 *   +0xE0 descriptor: int16 -1, int16 n, then six u32 pointers to the
 *         x/y/z int32 and pitch/yaw/roll int16 tables, n entries each.
 * The game evaluates it at 80034B84; every Stage 1 slide is 18 entries.
 * The descriptor is installed before the player hides, so the hidden pose is
 * known while exposed. RAM and ROM hold the original big-endian bytes; the
 * tables are halfword aligned. Offsets come out as raw/16384; the host scales
 * them like the scene (qvr_world_scale / qvr_units_per_meter). */
enum { QTC2_COVER_STRUCT=0x2F4060u };

typedef struct QCoverSpot {
    bool valid;       /* a well-formed cover table is installed */
    bool exposed;     /* at the exposed end of the slide */
    float progress;   /* 0 exposed .. 1 hidden, from the table index */
    V3 hide;          /* hidden minus exposed camera, raw/16384, in the exposed
                         camera's VR frame (x right, y up, -z forward) */
    float turn_deg;   /* hidden minus exposed yaw */
    uint32_t table;   /* first table pointer, identifies the spot */
} QCoverSpot;

static inline uint32_t qtc2_cover_be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static inline uint16_t qtc2_cover_be16(const uint8_t *p){return (uint16_t)(p[0]<<8|p[1]);}

/* A CPU address in main RAM or the data ROM, as tc2_mem.c maps it. */
static inline const uint8_t *qtc2_cover_bytes(uint32_t address,size_t count,const uint8_t *ram,size_t ram_size,
                                              const uint8_t *data,size_t data_size){
    uint32_t p=address&0x0FFFFFFFu;
    if(p<0x1000000u)return ram&&p+count<=ram_size?ram+p:NULL;
    if(p>=0x08000000u&&p<0x0C000000u){
        size_t offset=(p>=0x0A000000u?0x1000000u:0u)+(p&0x00FFFFFFu);
        return data&&offset+count<=data_size?data+offset:NULL;
    }
    return NULL;
}

/* VR-frame direction (x right, y up, -z forward) of a world vector seen from a
 * camera with this pitch and yaw; matches the C435 camera the port draws. */
static inline V3 qtc2_cover_camera_vector(float dx,float dy,float dz,float pitch,float yaw){
    float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
    float right=cy*dx-sy*dz,up=sy*sp*dx+cp*dy+cy*sp*dz,forward=-sy*cp*dx+sp*dy-cy*cp*dz;
    return v3(right,up,-forward);
}

static inline bool qtc2_cover_spot(const uint8_t *ram,size_t ram_size,const uint8_t *data,size_t data_size,QCoverSpot *out){
    *out=(QCoverSpot){0};
    if(!ram||ram_size<QTC2_COVER_STRUCT+0xFCu)return false;
    const uint8_t *camera=ram+QTC2_COVER_STRUCT,*descriptor=camera+0xE0;
    uint32_t state=qtc2_cover_be32(camera+0x5C);
    int index=(int16_t)qtc2_cover_be16(camera+0x68);
    int header=(int16_t)qtc2_cover_be16(descriptor),n=(int16_t)qtc2_cover_be16(descriptor+2);
    if(!(state&1u)||header!=-1||n<2||n>64||index<0||index>=n)return false;
    int32_t hidden[3],exposed[3];int16_t angle_hidden[3],angle_exposed[3];
    for(int k=0;k<6;k++){
        uint32_t table=qtc2_cover_be32(descriptor+4+4*k);
        size_t size=k<3?4:2;
        const uint8_t *first=qtc2_cover_bytes(table,size,ram,ram_size,data,data_size);
        const uint8_t *last=qtc2_cover_bytes(table+(uint32_t)(size*(size_t)(n-1)),size,ram,ram_size,data,data_size);
        if(!first||!last)return false;
        if(k<3){hidden[k]=(int32_t)qtc2_cover_be32(first);exposed[k]=(int32_t)qtc2_cover_be32(last);}
        else{angle_hidden[k-3]=(int16_t)qtc2_cover_be16(first);angle_exposed[k-3]=(int16_t)qtc2_cover_be16(last);}
        if(k==0)out->table=table;
    }
    const float raw_to_radians=6.28318530717958647692f/65536.f,raw_to_metres=1.f/16384.f;
    out->hide=mul(qtc2_cover_camera_vector((float)(hidden[0]-exposed[0]),(float)(hidden[1]-exposed[1]),(float)(hidden[2]-exposed[2]),
                                           angle_exposed[0]*raw_to_radians,angle_exposed[1]*raw_to_radians),raw_to_metres);
    float length=sqrtf(dot(out->hide,out->hide));
    if(!(length>=.05f&&length<=4.f))return false;
    out->turn_deg=(int16_t)(uint16_t)(angle_hidden[1]-angle_exposed[1])*(360.f/65536.f);
    out->progress=(float)(n-1-index)/(float)(n-1);
    out->exposed=index==n-1&&(state&2u);
    out->valid=true;
    return true;
}
