#pragma once
/* The lobby browser: asks an NMN2 server (upstream/docs/NETPLAY.md) for this game's rooms without joining any, the way the
 * server answers a stranger (DISCOVER -> ANNOUNCE, ROOMS padded to 400 bytes -> ROOMLIST). One small UDP socket, polled. */
#include <stdbool.h>
#include <stdint.h>

enum { TC2_ROOMS_MAX=32 };
typedef struct tc2_room { int id,version,state,players,max_players; char name[32],code[8]; } tc2_room;

/* Starts a query (resolves the address once per server; a refresh reuses it). False = the address does not resolve. */
bool tc2_rooms_query(const char *server,uint16_t game_version);
void tc2_rooms_poll(void);                       /* every frame while a query or the browser is open */
bool tc2_rooms_busy(void);                       /* waiting for the first answer */
bool tc2_rooms_answered(void);                   /* the server answered this query */
int  tc2_rooms_count(void);
const tc2_room *tc2_rooms_get(int i);
const char *tc2_rooms_server_name(void);
int  tc2_rooms_server_players(void);
void tc2_rooms_close(void);
void tc2_rooms_reset(void);                       /* changing servers: discard the socket, address and old room list */

/* Room codes: four characters that cannot be misread (no 0/O, 1/I, 2/Z, 5/S, 8/B). A hosted room is named "NAME CODE". */
void tc2_room_code_make(char out[5]);
bool tc2_room_code_valid(const char *code);
extern const char tc2_room_code_chars[];
