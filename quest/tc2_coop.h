#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The arcade's SELECT GAME MODE screen (US TSS3 Ver. B), shown after every credit.
 * SOLO/LINKED stays crossed out until a partner cabinet is linked, and the ROM
 * ignores a shot at it; the host uses that shot to open LAN Play instead.
 *   0x2CE92B  1 while the screen is up
 *   0x2F3930  1 while it waits for a choice (2/4 at the confirm frame)
 *   0x2CEA68, 0x2CEA6C  u32 gun x / y in 640x480 screen pixels (FFFFFFFF off screen)
 * The ROM's own SOLO/LINKED hit box is x 30..632, y 108..255 (x 20 and y 98 / 265
 * miss); the box below sits between the measured hits and misses. RAM holds the
 * original big-endian bytes. */
enum { QTC2_MODE_SCREEN=0x2CE92Bu,QTC2_MODE_STATE=0x2F3930u,QTC2_GUN_X=0x2CEA68u,QTC2_GUN_Y=0x2CEA6Cu };

static inline uint32_t qtc2_coop_be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

static inline bool qtc2_mode_select_waiting(const uint8_t *ram,size_t size){
    return ram&&size>QTC2_GUN_Y+3&&ram[QTC2_MODE_SCREEN]==1&&ram[QTC2_MODE_STATE]==1;
}
static inline bool qtc2_coop_aimed(const uint8_t *ram,size_t size){
    if(!ram||size<QTC2_GUN_Y+4)return false;
    uint32_t x=qtc2_coop_be32(ram+QTC2_GUN_X),y=qtc2_coop_be32(ram+QTC2_GUN_Y);
    return x>=25&&x<=636&&y>=103&&y<=260;
}
/* A fresh trigger press at SOLO/LINKED while the screen waits for a choice. */
static inline bool qtc2_coop_chosen(const uint8_t *ram,size_t size,uint16_t inputs,uint16_t previous_inputs){
    bool fire=(inputs&0x10)&&!(previous_inputs&0x10);
    return fire&&qtc2_mode_select_waiting(ram,size)&&qtc2_coop_aimed(ram,size);
}
