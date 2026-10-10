#include "tc2_weapon.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t ram[0x1000000];
static void be32(unsigned a,uint32_t v){for(int i=0;i<4;i++)ram[a+i]=(uint8_t)(v>>(24-8*i));}
int main(void){
    assert(qtc2_weapon_kind(NULL,sizeof ram)==QGUN_PISTOL);
    assert(qtc2_weapon_kind(ram,4)==QGUN_PISTOL);
    assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_PISTOL);
    be32(0x2f43d0,8);assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_MACHINE_GUN);
    be32(0x2f43d0,0xffffffF7);assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_PISTOL);
    be32(0x2cee28,1);be32(0x2f43d0+0x474,8);
    assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_MACHINE_GUN);
    assert(qtc2_weapon_kind(ram,0x2f43d0+0x474+3)==QGUN_PISTOL);
    be32(0x2f43d0+0x474,0);assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_PISTOL);
    be32(0x2cee28,0x01000000);assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_PISTOL);
    be32(0x2cee28,2);assert(qtc2_weapon_kind(ram,sizeof ram)==QGUN_PISTOL);
    puts("PASS: US arcade weapon flags, both player slots, big-endian decoding and safe fallback");
    return 0;
}
