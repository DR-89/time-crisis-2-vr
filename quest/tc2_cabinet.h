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
static inline uint8_t qtc2_cabinet_switch(uint8_t active_low,int side){
    return side==2?(uint8_t)(active_low&~4u):(uint8_t)(active_low|4u);
}
