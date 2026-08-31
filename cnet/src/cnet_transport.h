#ifndef CNET_TRANSPORT_H
#define CNET_TRANSPORT_H

#include <turbo/native_io.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cnet_transport {
  uintptr_t native_handle;
  native_io_endpoint endpoint;
  bool native_open;
  bool attached;
} cnet_transport;

/**
 * Creates a CNet-owned stream socket, attaches it to NativeIO, and submits one
 * asynchronous connect. `address` stays borrowed until the request completion.
 */
int cnet_transport_tcp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request);

/** Creates, connects, and attaches one CNet-owned datagram socket. */
int cnet_transport_udp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length);

/** Closes the owned socket, then releases drained NativeIO endpoint metadata. */
int cnet_transport_close(cnet_transport *transport, native_io_backend *backend);

#endif /* CNET_TRANSPORT_H */
