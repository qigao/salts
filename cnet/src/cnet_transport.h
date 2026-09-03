#ifndef CNET_TRANSPORT_H
#define CNET_TRANSPORT_H

#include <salts/native_io.h>

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
 * Converts an IPv4 or IPv6 literal plus host-order port into native sockaddr
 * storage. Returns `SALTS_ENOENT` for a hostname so the caller can route it to
 * the resolver. `out_address_length` is cleared on every failure.
 */
int cnet_transport_parse_numeric_address(const char *host, uint16_t port, void *out_address,
                                         size_t address_capacity, size_t *out_address_length);

/** Listener variant that additionally accepts port zero for ephemeral bind. */
int cnet_transport_parse_bind_address(const char *host, uint16_t port, void *out_address,
                                      size_t address_capacity, size_t *out_address_length);

/** Creates and attaches a TCP socket, then describes its connect operation. */
int cnet_transport_tcp_prepare_connect(cnet_transport *transport, native_io_backend *backend,
                                       native_io_backend_kind backend_kind, const void *address,
                                       size_t address_length, uintptr_t user_data,
                                       native_io_operation *out_operation);

/**
 * Creates a CNet-owned stream socket, attaches it to NativeIO, and submits one
 * asynchronous connect. `address` stays borrowed until the request completion.
 */
int cnet_transport_tcp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request);

/**
 * Takes ownership of one connected TCP socket and attaches it to NativeIO.
 * The handle is closed on every failure after argument validation.
 */
int cnet_transport_adopt_tcp(cnet_transport *transport, native_io_backend *backend,
                             uintptr_t native_socket);

/** Closes one unattached native socket transferred through a failed command. */
void cnet_transport_close_socket(uintptr_t native_socket);

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

/**
 * Opens one platform byte-pipe client endpoint and attaches it to NativeIO.
 * Windows maps `name` to one duplex overlapped named pipe. POSIX maps `name`
 * to the nonblocking FIFO pair `name.rx` (read) and `name.tx` (write).
 */
int cnet_transport_pipe_connect(cnet_transport *transport, native_io_backend *backend,
                                native_io_backend_kind backend_kind, const char *name);

native_io_endpoint cnet_transport_read_endpoint(const cnet_transport *transport);
native_io_endpoint cnet_transport_write_endpoint(const cnet_transport *transport);
bool cnet_transport_active(const cnet_transport *transport);

/** Closes owned native resources, then releases drained NativeIO metadata. */
int cnet_transport_close(cnet_transport *transport, native_io_backend *backend);

#endif /* CNET_TRANSPORT_H */
