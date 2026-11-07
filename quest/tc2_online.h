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
void tc2_online_toggle_mute(void);       /* the microphone, during play (left stick click / M) */
bool tc2_online_may_advance(void);       /* stall-and-wait: false = hold this arcade frame (keep rendering)  */

/* The ONLINE options page (a small menu model; quest_ui.c draws it, quest_host.c feeds it input). */
void tc2_online_menu_move(int direction);       /* up / down */
void tc2_online_menu_adjust(int direction);     /* left / right: inside an address or name field */
void tc2_online_menu_select(void);
void tc2_online_menu_back(void);
bool tc2_online_menu_editing(void);
enum { TC2_ONLINE_LINES=15, TC2_ONLINE_COLUMNS=40 };
enum { TC2_LINE_TEXT, TC2_LINE_TITLE, TC2_LINE_ITEM, TC2_LINE_SELECTED, TC2_LINE_GOOD, TC2_LINE_WARN, TC2_LINE_HINT };
/* Fills the page; returns a serial that changes whenever the page content changes (the panel re-uploads only then). */
unsigned tc2_online_menu_lines(char text[TC2_ONLINE_LINES][TC2_ONLINE_COLUMNS],int style[TC2_ONLINE_LINES]);

/* A short message over the game (a partner joined / left, the link is struggling); false = nothing to show. */
bool tc2_online_toast(char *text,size_t n,unsigned *serial);

/* The host (quest_host.c): open or close the ONLINE page from the game side (the arcade's SOLO/LINKED shot, a partner linking). */
void qhost_online_menu(bool open);
bool qhost_online_menu_open(void);
