#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Physical cabinet ID, latched before boot. P1=left/red, P2=right/blue. */
extern int qtc2_cabinet_side;
void qtc2_cabinet_configure(const char *options_path);
void qtc2_cabinet_prepare_player(void);
/* US TSS3 Ver. B: the caller at 80012E8C reads this route immediately before
 * 80013058 initializes the two characters. Standalone input remains slot 0.
 * Change only a valid route in single-game modes, never a running frame. */
static inline bool qtc2_route_select(uint8_t *ram,size_t size,int side){
    if(side!=2||!ram||size<0x2f3fd8u)return false;
    const uint8_t *mode=ram+0x2f3fd0;
    uint8_t *route=ram+0x2f3fd4;
    if(mode[0]||mode[1]||mode[2]||mode[3]<1||mode[3]>2||
       route[0]||route[1]||route[2]||route[3]>1)return false;
    route[3]=1;return true;
}
/* The I/O board's Link ID switch (upstream src/tc2_h8.c, bit 2 of 0x6001, active low): 1 = the right/blue cabinet. */
static inline int qtc2_link_id_of(int side){return side==2;}
int tc2_link_id(void);                    /* qtc2_link_id_of(qtc2_cabinet_side) */
/* Online: the agreed start holds the right cabinet's coin / trigger / pedal and pays held coins in (tc2_online.c). */
int tc2_link_hold_start(void);
int tc2_link_coin_inject(void);
