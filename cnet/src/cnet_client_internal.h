#ifndef CNET_CLIENT_INTERNAL_H
#define CNET_CLIENT_INTERNAL_H

#include <cnet/cnet.h>

#include "cnet_tls.h"

#include <stdint.h>

/** Consumes one connected TCP socket, closing it on immediate admission failure. */
int cnet_client_adopt_tcp(cnet_client *client, uintptr_t native_socket,
                          const cnet_observer *observer, cnet_connection *out_connection);

/** Retains `context` and consumes the connected TCP socket on successful admission. */
int cnet_client_adopt_tls_server(cnet_client *client, uintptr_t native_socket,
                                 cnet_tls_context *context, const cnet_observer *observer,
                                 cnet_connection *out_connection);

#endif /* CNET_CLIENT_INTERNAL_H */
