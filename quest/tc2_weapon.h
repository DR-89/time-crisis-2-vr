#pragma once
#include <stddef.h>
#include <stdint.h>
#include "quest_gun.h"

/* Read-only adapter for the pinned US TSS3 Ver. B program. The original HUD
 * at 800CE5A0..800CE5D8 tests player.flags & 8 before showing the machine gun.
 * gp = 802C7820, local player = *(gp + 7608); player stride = 0x474.
 * RAM stores the original big-endian bytes. See docs/WEAPONS.md. */
static inline uint32_t qtc2_weapon_be32(const uint8_t *p){
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static inline QGunKind qtc2_weapon_kind(const uint8_t *ram,size_t size){
    if(!ram||size<0x2cee2cu)return QGUN_PISTOL;
    uint32_t player=qtc2_weapon_be32(ram+0x2cee28);
    if(player>1)return QGUN_PISTOL;
    size_t flags=0x2f43d0u+player*0x474u;
    if(flags+4>size)return QGUN_PISTOL;
    return (qtc2_weapon_be32(ram+flags)&8)?QGUN_MACHINE_GUN:QGUN_PISTOL;
}
