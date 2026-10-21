/* The arcade SOLO/LINKED shot that opens LAN Play, with synthetic big-endian RAM. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../quest/tc2_coop.h"

static uint8_t ram[0x2F4000];
static void be32(uint32_t at,uint32_t v){ram[at]=v>>24;ram[at+1]=v>>16;ram[at+2]=v>>8;ram[at+3]=v;}
static void screen(int up,int state,uint32_t x,uint32_t y){
    memset(ram,0,sizeof ram);ram[QTC2_MODE_SCREEN]=(uint8_t)up;ram[QTC2_MODE_STATE]=(uint8_t)state;be32(QTC2_GUN_X,x);be32(QTC2_GUN_Y,y);
}
int main(void){
    /* Waiting screen, aimed at SOLO/LINKED, a fresh trigger press. */
    screen(1,1,320,180);assert(qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0x10));      /* held, not a new press */
    assert(!qtc2_coop_chosen(ram,sizeof ram,0x20,0));         /* pedal is not a shot */
    assert(qtc2_coop_chosen(ram,sizeof ram,0x31,0x21));       /* other bits don't matter */
    /* The box sits between the ROM's measured hits (30..632, 108..255) and misses (20, 98, 265). */
    screen(1,1,30,108);assert(qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    screen(1,1,632,255);assert(qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    screen(1,1,20,180);assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    screen(1,1,320,98);assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    screen(1,1,320,265);assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    /* SOLO ONLY (y 292..376) and an off-screen gun are not the co-op choice. */
    screen(1,1,320,330);assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    screen(1,1,0xFFFFFFFFu,0xFFFFFFFFu);assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    /* Only while the screen is up and still waiting for a choice. */
    screen(0,1,320,180);assert(!qtc2_coop_chosen(ram,sizeof ram,0x10,0));
    for(int state=0;state<8;state++){screen(1,state,320,180);assert(qtc2_coop_chosen(ram,sizeof ram,0x10,0)==(state==1));}
    assert(!qtc2_coop_chosen(ram,QTC2_GUN_Y+3,0x10,0)&&!qtc2_coop_chosen(NULL,sizeof ram,0x10,0));
    printf("PASS: SOLO/LINKED shot detection: press edge, ROM hit-box edges, SOLO ONLY and off-screen rejected, screen state and bounds\n");
    return 0;
}
