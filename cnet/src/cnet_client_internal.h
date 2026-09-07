#ifndef CNET_CLIENT_INTERNAL_H
#define CNET_CLIENT_INTERNAL_H

#include <cnet/cnet.h>

#include "cnet_tls.h"

#if defined(CNET_INTERNAL_PROFILING)
#include "cnet_owner.h"
#endif

#include <stdint.h>

/** Consumes one connected TCP socket, closing it on immediate admission failure. */
int cnet_client_adopt_tcp(cnet_client *client, uintptr_t native_socket,
                          const cnet_observer *observer, cnet_connection *out_connection);

/** Consumes one connected VSOCK stream, closing it on immediate admission failure. */
int cnet_client_adopt_vsock(cnet_client *client, uintptr_t native_socket,
                            const cnet_observer *observer, cnet_connection *out_connection);

/** Retains `context` and consumes the connected TCP socket on successful admission. */
int cnet_client_adopt_tls_server(cnet_client *client, uintptr_t native_socket,
                                 cnet_tls_context *context, const cnet_observer *observer,
                                 cnet_connection *out_connection);

#if defined(CNET_INTERNAL_PROFILING)
typedef cnet_owner_profile cnet_client_poll_profile;

/** Samples internal poll-owner stages in a private diagnostic build. */
int cnet_client_profile_begin(cnet_client *client);
int cnet_client_profile_take(cnet_client *client, cnet_client_poll_profile *out_profile);
#endif

#endif /* CNET_CLIENT_INTERNAL_H */
