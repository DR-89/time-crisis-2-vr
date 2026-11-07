#pragma once
/* The System 23 C422: the RS-422 twin-cabinet link (US TSS3 Ver. B driver at 8000BEAC / 8000BB6C / 8000BD24 /
 * 8000BF38). Disabled = exactly the pinned upstream register stub, so offline play is unchanged. Enabled = a real link
 * chip whose wire is carried by tc2_online.c in upstream namco22-decompile's Time Crisis 2 payload. See docs/ONLINE.md. */
#include <stdbool.h>
#include <stdint.h>

#define TC2_LINK_PAYLOAD 1024    /* bytes per network frame (upstream's "t2" frame size) */
#define TC2_LINK_TRAILER 8       /* its last bytes: 'T' '2', the sender's view of its partner, the sender's frame */

void     tc2_link_set_enabled(bool on);
bool     tc2_link_enabled(void);

/* Board side (hooked into the pinned core by quest/tc2_sources.cmake). */
uint16_t tc2_link_read(unsigned reg);
void     tc2_link_write(unsigned reg,uint16_t value);
void     tc2_link_ack(void);              /* the interrupt handler's write to 0x0D000008 */
void     tc2_link_step(uint32_t instructions);

/* Wire side, one payload a game frame. pack: the link block (k, then k frames) into out[0..limit), returns its length (0 = no
 * link); fits_newest: the newest frame fits in limit bytes; trailer: writes the last TC2_LINK_TRAILER bytes. unpack: reads a
 * whole payload, returns the offset just after the link block (where an extension may follow), -1 if malformed. */
int      tc2_link_pack(uint8_t *out,int limit);
bool     tc2_link_pack_fits_newest(int limit);
void     tc2_link_trailer(uint8_t *payload);
int      tc2_link_unpack(const uint8_t *in);
void     tc2_link_session_reset(void);    /* a session (re)starts: frame numbering afresh on both sides */
int      tc2_link_self_state(void);       /* our cabinet's state as the partner reports it: 1 attract, 2 credited, 4 in a game; -1 unknown */
bool     tc2_link_partner_vr(void);       /* the partner runs this build (its trailer's byte 3 is 'V'; upstream leaves it 0) */
void     tc2_link_set_ghost(bool on);      /* a stand-in partner: answers as an idle right cabinet, no second game running */
bool     tc2_link_ghost(void);
int      tc2_link_partner_mark(void);     /* the partner's trailer byte 3: 'V' this build, 'J' its game takes a second player, 'L' its game is SOLO ONLY; 0 upstream */
void     tc2_link_set_mark(int mark);     /* ours: 'V', 'J' or 'L' (tc2_online.c standin_tick) */

/* What the game's own link driver thinks (its variables at gp 0x802C7820). */
typedef struct tc2_link_status {
    bool     enabled,alive;               /* alive: the driver sees its partner (0x802F3502, the partner's state) */
    uint16_t lag;                         /* frames since the partner's newest packet, as the game counts them */
    uint16_t state;                       /* 0 linked, 2 lost */
    uint32_t tx,rx,rx_dropped,rx_bad,tx_bad,game_errors;
} tc2_link_status;
void     tc2_link_get_status(tc2_link_status *out);
