#ifndef CNET_CLIENT_INTERNAL_H
#define CNET_CLIENT_INTERNAL_H

#include <cnet/cnet.h>

#include "cnet_tls.h"

#if defined(CNET_INTERNAL_PROFILING)
#include "cnet_owner.h"
#endif

#include <stdbool.h>
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
typedef struct cnet_client_poll_profile {
  cnet_owner_profile owner;
  uint64_t client_poll_ns;
  uint64_t client_poll_calls;
  uint64_t dispatcher_prepare_ns;
  uint64_t dispatcher_prepare_calls;
  uint64_t dispatcher_invoke_ns;
  uint64_t dispatcher_invoke_calls;
  uint64_t dispatcher_observer_ns;
  uint64_t dispatcher_observer_calls;
  uint64_t dispatcher_release_ns;
  uint64_t dispatcher_release_calls;
} cnet_client_poll_profile;

/** Selects the private benchmark-only dispatcher-bypass mode before admission. */
int cnet_client_set_diagnostic_direct_dispatch(cnet_client *client, bool enabled);

/** Samples internal poll-owner/client/dispatcher stages in a private diagnostic build. */
int cnet_client_profile_begin(cnet_client *client);
int cnet_client_profile_take(cnet_client *client, cnet_client_poll_profile *out_profile);
#endif

#endif /* CNET_CLIENT_INTERNAL_H */
