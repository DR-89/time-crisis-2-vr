#pragma once
/* Online twin-cabinet play: the NMN2 lobby/relay client from upstream (engine/net.c, engine/net_host.c) carrying the C422
 * link (tc2_link.c). Host a game, find one on the LAN, join by address, or quick-match on a public server. See docs/ONLINE.md. */
#include <stdbool.h>
#include <stddef.h>

void tc2_online_init(void);              /* once at startup: online.cfg, the network client (inert until used) */
void tc2_online_frame(void);             /* once per simulated arcade frame */
void tc2_online_service(void);           /* every host loop iteration: keeps the lobby alive while the arcade is paused */
bool tc2_online_session(void);           /* linked to a partner: the arcade keeps running behind menus and focus loss */
bool tc2_online_busy(void);              /* hosting, connecting, in a lobby or in a session */
void tc2_online_toggle_mute(void);       /* the microphone, during play (left stick click / M; not with push to talk) */
bool tc2_online_push_to_talk(void);      /* voice chat is on and sends only while the talk key is held */
void tc2_online_talk_key(bool held);     /* every frame: a thumbstick click held (Quest) / V held (PC) */
bool tc2_online_may_advance(void);       /* stall-and-wait: false = hold this arcade frame (keep rendering)  */

/* The pause menu's ONLINE tab (quest_menu.c) reads this view and calls the actions below. */
enum { TC2V_OFFLINE, TC2V_JOINING, TC2V_ROOM_WAITING, TC2V_LAN_HOSTING, TC2V_LINKED, TC2V_RECONNECTING };
typedef struct tc2_online_view {
    int state;                         /* TC2V_* */
    int side,home_side;                /* the cabinet now (the lobby slot while online), and the player's own solo choice */
    char name[16],server[64];
    char code[5];                      /* the room code: our open room, the one being joined, or the linked one */
    char joining[64];                  /* TC2V_JOINING: what (a code, an address, "QUICK MATCH") */
    bool opening;                      /* TC2V_ROOM_WAITING: the server has not confirmed the room yet */
    char partner[24];int ping_ms;bool link_alive,link_lagging;int gone_s;
    char players[2][24];bool player_self[2];   /* by cabinet: [0] P1, [1] P2; "" while the place is free */
    char address[3][20];bool address_vpn[3];int address_count;
    bool voice,muted,mic_denied,partner_voice;int level,partner_level,volume;   /* levels 0..8, volume percent */
    bool push_to_talk,talking;         /* send only while the talk key is held; sending now */
    bool join_credit;                  /* a P2 joining a game gets one credit */
    bool can_rejoin;char rejoin_code[5],rejoin_partner[24];int rejoin_side;      /* the room the player left, still open */
    char join_ip[24];
    bool browse_answered,browse_busy,discovering;char server_name[64];int server_rooms,server_players;
} tc2_online_view;
enum { TC2G_JOIN, TC2G_PLAYING, TC2G_FULL, TC2G_OLD };
typedef struct tc2_online_game { char name[24],detail[40];int players,max_players,status;bool lan; } tc2_online_game;
void tc2_online_get_view(tc2_online_view *v);
int  tc2_online_games(tc2_online_game *out,int max);   /* the browser: server rooms, then games on the local network */
void tc2_online_browse(bool open);                     /* keep the browser's lists fresh while a browse / local screen shows */
void tc2_online_refresh(void);
void tc2_online_join_game(int index);                  /* an index into tc2_online_games */
void tc2_online_host_room(void);
void tc2_online_join_code(const char *code);
void tc2_online_quick(void);
void tc2_online_host_lan(void);
void tc2_online_set_join_ip(const char *address);
void tc2_online_join_ip(void);
void tc2_online_set_voice(bool on);
void tc2_online_set_push_to_talk(bool on);
void tc2_online_set_join_credit(bool on);
void tc2_online_set_volume(int percent);
void tc2_online_set_name(const char *name);
void tc2_online_leave(void);                           /* also cancels joining and closes an open room */
void tc2_online_rejoin(void);
void tc2_online_restart_game(void);                    /* the arcade's TEST restart, coins paid back; leaves first when online */
void tc2_online_menu_shown(bool online_tab);           /* the menu is open on the ONLINE tab: keep REJOIN honest */

/* A short message over the game (a partner joined / left, the link is struggling); false = nothing to show. */
bool tc2_online_toast(char *text,size_t n,unsigned *serial);

/* The host (quest_host.c): open or close the ONLINE page from the game side (the arcade's SOLO/LINKED shot, a partner linking). */
void qhost_online_menu(bool open);
bool qhost_online_menu_open(void);
