/* The lobby drops a member after five seconds without traffic. A slow render,
 * asset upload or other main-thread stall must not evict a connected cabinet:
 * upstream desktop partners cannot reopen a running room for it to rejoin.
 * Keep the current socket alive for a bounded grace period. No game frames are
 * fabricated, and the network state machine stays entirely on the main thread. */
#include "tc2_net_keepalive.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#define KEEPALIVE_MS 1000u
#define GRACE_MS 30000u
static SDL_mutex *keepalive_lock;
static SDL_TimerID keepalive_timer;
static bool keepalive_attempted;
static eng_sock_t keepalive_socket = ENG_SOCK_BAD;
static struct sockaddr_storage keepalive_address;
static socklen_t keepalive_address_size;
static uint8_t keepalive_ping[20];      /* NMN2 header + timestamp (RRN1 uses 14 bytes) */
static int keepalive_size;
static Uint32 keepalive_main_ms, keepalive_sent_ms;
static unsigned keepalive_count;

static Uint32 SDLCALL keepalive_tick(Uint32 interval, void *unused)
{
    (void)unused;
    SDL_LockMutex(keepalive_lock);
    Uint32 now = SDL_GetTicks();
    if (keepalive_socket != ENG_SOCK_BAD && now - keepalive_main_ms <= GRACE_MS &&
        now - keepalive_sent_ms >= KEEPALIVE_MS) {
        uint8_t *stamp = keepalive_ping + keepalive_size - 4;
        stamp[0] = (uint8_t)now; stamp[1] = (uint8_t)(now >> 8);
        stamp[2] = (uint8_t)(now >> 16); stamp[3] = (uint8_t)(now >> 24);
        sendto(keepalive_socket, (const char *)keepalive_ping, keepalive_size, 0,
               (const struct sockaddr *)&keepalive_address, keepalive_address_size);
        keepalive_sent_ms = now;
        keepalive_count++;
    }
    SDL_UnlockMutex(keepalive_lock);
    return interval;
}

void tc2_net_keepalive_update(eng_sock_t socket, const struct sockaddr_storage *address,
                              socklen_t address_size, const uint8_t *ping, int size)
{
    if (socket == ENG_SOCK_BAD || (size != 14 && size != 20) ||
        address_size <= 0 || (size_t)address_size > sizeof keepalive_address) return;
    if (!keepalive_attempted) {
        keepalive_attempted = true;
        if (!keepalive_lock) keepalive_lock = SDL_CreateMutex();
        if (keepalive_lock) keepalive_timer = SDL_AddTimer(250, keepalive_tick, NULL);
        if (!keepalive_timer) fprintf(stderr, "[NET] background keepalive unavailable: %s\n", SDL_GetError());
    }
    if (!keepalive_timer) return;
    SDL_LockMutex(keepalive_lock);
    Uint32 now = SDL_GetTicks(), gap = now - keepalive_main_ms;
    unsigned sent = keepalive_count;
    keepalive_socket = socket;
    keepalive_address = *address;
    keepalive_address_size = address_size;
    memcpy(keepalive_ping, ping, (size_t)size);
    keepalive_size = size;
    keepalive_main_ms = keepalive_sent_ms = now;
    keepalive_count = 0;
    SDL_UnlockMutex(keepalive_lock);
    if (sent) fprintf(stderr, "[NET] main thread resumed after %u ms; sent %u background keepalive(s)\n", gap, sent);
}

void tc2_net_keepalive_clear(void)
{
    if (!keepalive_lock) { keepalive_attempted = false; return; }
    /* The callback holds this same lock across sendto. Once clear returns, it
     * cannot touch a closed (or recycled) descriptor or a previous server. */
    SDL_LockMutex(keepalive_lock);
    keepalive_socket = ENG_SOCK_BAD;
    keepalive_count = 0;
    SDL_UnlockMutex(keepalive_lock);
    if (keepalive_timer) SDL_RemoveTimer(keepalive_timer);
    keepalive_timer = 0;
    keepalive_attempted = false;
}
