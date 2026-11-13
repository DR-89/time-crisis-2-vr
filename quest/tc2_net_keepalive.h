#ifndef TC2_NET_KEEPALIVE_H
#define TC2_NET_KEEPALIVE_H
#include "net_sock.h"

/* Only the main thread updates/clears this snapshot. The timer sends PINGs, never
 * reads game/network state or receives packets. Clear BEFORE closing the socket. */
void tc2_net_keepalive_update(eng_sock_t socket, const struct sockaddr_storage *address,
                              socklen_t address_size, const uint8_t *ping, int size);
void tc2_net_keepalive_clear(void);
#endif
