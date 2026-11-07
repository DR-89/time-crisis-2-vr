#pragma once
/* The System 23 C422: the RS-422 twin-cabinet link (US TSS3 Ver. B driver at 8000BEAC / 8000BB6C / 8000BD24 /
 * 8000BF38). Disabled = exactly the pinned upstream register stub, so offline play is unchanged. Enabled = a real link
 * chip whose wire is carried by tc2_online.c. See docs/ONLINE.md. */
#include <stdbool.h>
#include <stdint.h>

#define TC2_LINK_PAYLOAD 256     /* bytes per network frame: one game frame's newest link packet */

void     tc2_link_set_enabled(bool on);
bool     tc2_link_enabled(void);

/* Board side (hooked into the pinned core by quest/tc2_sources.cmake). */
uint16_t tc2_link_read(unsigned reg);
void     tc2_link_write(unsigned reg,uint16_t value);
void     tc2_link_ack(void);              /* the interrupt handler's write to 0x0D000008 */
void     tc2_link_step(uint32_t instructions);

/* Wire side. pack: 1 when out holds a fragment to send (TC2_LINK_PAYLOAD bytes); unpack takes one fragment. */
int      tc2_link_pack(uint8_t *out);
bool     tc2_link_unpack(const uint8_t *in);
bool     tc2_link_pending(void);         /* more fragments of the current packet wait to be packed */

/* What the game's own link driver thinks (its variables at gp 0x802C7820). */
typedef struct tc2_link_status {
    bool     enabled,alive;               /* alive: the driver receives its partner (0x802F3502) */
    uint16_t lag;                         /* frames since the partner's newest packet, as the game counts them */
    uint16_t state;                       /* 0 linked, 2 lost */
    uint32_t tx,rx,rx_dropped,rx_bad,tx_bad,game_errors;
} tc2_link_status;
void     tc2_link_get_status(tc2_link_status *out);
