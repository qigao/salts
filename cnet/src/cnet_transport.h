#ifndef CNET_TRANSPORT_H
#define CNET_TRANSPORT_H

#include <turbo/native_io.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum cnet_transport_resource_kind {
  CNET_TRANSPORT_RESOURCE_NONE = 0,
  CNET_TRANSPORT_RESOURCE_SOCKET,
  CNET_TRANSPORT_RESOURCE_PIPE
} cnet_transport_resource_kind;

typedef struct cnet_transport {
  uintptr_t native_handle;
  native_io_endpoint endpoint;
  uintptr_t write_native_handle;
  native_io_endpoint write_endpoint;
  cnet_transport_resource_kind resource_kind;
  bool native_open;
  bool attached;
  bool write_native_open;
  bool write_attached;
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

/**
 * Adopts one connected byte-pipe connection. On success CNet owns both native
 * handles; the read and write handles may be identical for a duplex pipe. On
 * failure ownership remains with the caller.
 */
int cnet_transport_adopt_pipe(cnet_transport *transport, native_io_backend *backend,
                              uintptr_t read_handle, uintptr_t write_handle);

native_io_endpoint cnet_transport_read_endpoint(const cnet_transport *transport);
native_io_endpoint cnet_transport_write_endpoint(const cnet_transport *transport);
bool cnet_transport_active(const cnet_transport *transport);

/** Closes owned native resources, then releases drained NativeIO metadata. */
int cnet_transport_close(cnet_transport *transport, native_io_backend *backend);

#endif /* CNET_TRANSPORT_H */
