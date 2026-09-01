#ifndef CNET_CLIENT_INTERNAL_H
#define CNET_CLIENT_INTERNAL_H

#include <cnet/cnet.h>

#include <stdint.h>

/** Consumes one connected TCP socket, closing it on immediate admission failure. */
int cnet_client_adopt_tcp(cnet_client *client, uintptr_t native_socket,
                          const cnet_observer *observer, cnet_connection *out_connection);

#endif /* CNET_CLIENT_INTERNAL_H */
