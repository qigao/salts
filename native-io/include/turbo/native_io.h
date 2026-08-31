#ifndef TURBO_NATIVE_IO_H
#define TURBO_NATIVE_IO_H

#include <turbo/error_codes.h>
#include <turbo/native_io_module.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct turbo_io_backend {
  void *impl;
} turbo_io_backend;

typedef enum turbo_io_model {
  TURBO_IO_MODEL_NONE = 0,
  TURBO_IO_MODEL_COMPLETION = 1,
  TURBO_IO_MODEL_READINESS = 2
} turbo_io_model;

typedef enum turbo_io_backend_kind {
  TURBO_IO_BACKEND_IOCP = 1,
  TURBO_IO_BACKEND_EPOLL,
  TURBO_IO_BACKEND_IO_URING,
  TURBO_IO_BACKEND_KQUEUE
} turbo_io_backend_kind;

typedef struct turbo_io_endpoint {
  uint32_t slot;
  uint32_t generation;
} turbo_io_endpoint;

typedef struct turbo_io_request {
  uint32_t slot;
  uint32_t generation;
} turbo_io_request;

typedef enum turbo_io_operation_kind {
  TURBO_IO_TCP_RECV = 1,
  TURBO_IO_TCP_SEND = 2,
  TURBO_IO_UDP_RECV_FROM = 3,
  TURBO_IO_UDP_SEND_TO = 4,
  TURBO_IO_PIPE_READ = 5,
  TURBO_IO_PIPE_WRITE = 6
} turbo_io_operation_kind;

typedef enum turbo_io_pipe_endpoint_flags {
  TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE = 1u << 0
} turbo_io_pipe_endpoint_flags;

/**
 * One caller-owned operation descriptor copied by submit.
 *
 * endpoint is a live handle returned by attach_socket or attach_pipe. buffer
 * is borrowed from successful submit until the matching completion is
 * returned by observe.
 * Send storage is immutable during the borrow; receive storage is exclusively
 * mutable by NativeIO. user_data is copied verbatim into the completion.
 *
 * UDP_SEND_TO reads address[0..address_length) as a native sockaddr.
 * UDP_RECV_FROM writes at most address_capacity bytes and publishes the actual
 * length in its completion. TCP and pipe operations require all address fields
 * to be zero. Address storage has the same borrow as payload storage.
 */
typedef struct turbo_io_operation {
  turbo_io_operation_kind kind;
  turbo_io_endpoint endpoint;
  void *buffer;
  size_t length;
  uintptr_t user_data;
  void *address;
  size_t address_capacity;
  size_t address_length;
} turbo_io_operation;

typedef enum turbo_io_completion_kind {
  TURBO_IO_COMPLETION_OK = 1,
  TURBO_IO_COMPLETION_EOF,
  TURBO_IO_COMPLETION_CANCELLED,
  TURBO_IO_COMPLETION_FAILED
} turbo_io_completion_kind;

typedef struct turbo_io_completion {
  turbo_io_request request;
  turbo_io_endpoint endpoint;
  turbo_io_completion_kind kind;
  size_t bytes;
  int status;
  uint32_t native_status;
  uintptr_t user_data;
  size_t address_length;
} turbo_io_completion;

typedef struct turbo_io_backend_config {
  turbo_io_backend_kind kind;
  /** Hard cap for sockets retained by this backend. */
  size_t endpoint_capacity;
  /** Hard cap for operations whose terminal completion is not yet observed. */
  size_t request_capacity;
  /** Maximum completions returned by one observe call. */
  size_t completion_batch_capacity;
} turbo_io_backend_config;

typedef struct turbo_io_backend_stats {
  size_t endpoint_capacity;
  size_t endpoint_count;
  size_t request_capacity;
  size_t active_requests;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t failed;
  uint64_t rejected_full;
  uint64_t native_submit_errors;
  uint64_t native_cancel_errors;
  bool admission_open;
} turbo_io_backend_stats;

/** Returns the communication model represented by kind, or NONE if invalid. */
TURBO_NATIVE_IO_C_API turbo_io_model native_io_get_model(turbo_io_backend_kind kind);

/** Returns compile-time availability; no fallback backend is selected. */
TURBO_NATIVE_IO_C_API bool native_io_supported(turbo_io_backend_kind kind);

/** Returns whether kind supports async-capable connected byte pipes. */
TURBO_NATIVE_IO_C_API bool native_io_pipe_supported(turbo_io_backend_kind kind);

TURBO_NATIVE_IO_C_API bool turbo_io_endpoint_valid(turbo_io_endpoint endpoint);
TURBO_NATIVE_IO_C_API bool turbo_io_request_valid(turbo_io_request request);
TURBO_NATIVE_IO_C_API bool turbo_io_operation_valid(const turbo_io_operation *operation);

/**
 * Initializes a fixed-capacity backend selected by config.kind.
 *
 * The zero-state backend is owned by the caller. All methods on one backend
 * are single-owner-thread operations and must not execute concurrently. No
 * backend creates a worker thread: the owner submits and observes directly.
 *
 * @return TURBO_OK; TURBO_EINVAL for malformed handles or capacities;
 *         TURBO_ERANGE for allocation overflow; TURBO_ENOMEM; TURBO_ENOTSUP
 *         when kind is unavailable; otherwise a negative native error code.
 */
TURBO_NATIVE_IO_C_API int native_io_init(turbo_io_backend *backend,
                                                const turbo_io_backend_config *config);

/**
 * Associates one native socket with the backend and returns a generation
 * checked endpoint. The backend borrows the socket and never closes it. IOCP
 * requires an overlapped socket; readiness drivers use per-call nonblocking
 * operations and do not change the socket's blocking mode.
 *
 * The caller must retain the returned endpoint, stop submitting before close,
 * drain every request, close the native socket, and then call release_socket.
 *
 * @return TURBO_OK; TURBO_EINVAL; TURBO_ESHUTDOWN; TURBO_EALREADY if the
 *         socket is already attached; TURBO_ENOBUFS at endpoint capacity; or
 *         a negative native association error.
 */
TURBO_NATIVE_IO_C_API int native_io_attach_socket(turbo_io_backend *backend,
                                                         uintptr_t native_socket,
                                                         turbo_io_endpoint *out_endpoint);

/**
 * Releases backend metadata after the caller closed the native socket.
 * TURBO_EBUSY preserves the endpoint while any request remains unobserved.
 */
TURBO_NATIVE_IO_C_API int native_io_release_socket(turbo_io_backend *backend,
                                                          turbo_io_endpoint endpoint);

/**
 * Associates one connected byte-pipe handle with the backend. The backend
 * borrows the handle and never closes it. flags must be exactly
 * TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE; no worker fallback is selected.
 * IOCP accepts byte-mode named-pipe handles opened with FILE_FLAG_OVERLAPPED
 * and rejects synchronous handles. epoll and kqueue accept nonblocking byte
 * pipe descriptors. The selected backend remains the only progress owner.
 * out_endpoint is cleared on failure.
 */
TURBO_NATIVE_IO_C_API int native_io_attach_pipe(turbo_io_backend *backend,
                                                       uintptr_t native_handle,
                                                       uint32_t flags,
                                                       turbo_io_endpoint *out_endpoint);

/** Releases metadata for a drained pipe endpoint without closing its handle. */
TURBO_NATIVE_IO_C_API int native_io_release_pipe(turbo_io_backend *backend,
                                                        turbo_io_endpoint endpoint);

/**
 * Starts one operation without allocating or copying payload messages. For one
 * endpoint, read operations and write operations are admitted in independent
 * FIFO lanes. A backend may retain the copied descriptor in its fixed request
 * storage until that operation reaches the head of its lane.
 * The descriptor is copied; its payload remains borrowed until observe returns
 * the matching terminal completion. out_request is cleared on failure.
 *
 * @return TURBO_OK; TURBO_EINVAL for an invalid operation; TURBO_ENOENT for a
 *         stale endpoint; TURBO_ESHUTDOWN after close; TURBO_ENOBUFS when all
 *         request slots are retained; or a negative native submission error.
 */
TURBO_NATIVE_IO_C_API int native_io_submit(turbo_io_backend *backend,
                                                  const turbo_io_operation *operation,
                                                  turbo_io_request *out_request);

/**
 * Requests cancellation of an active operation. TURBO_OK means cancellation
 * was marked, not that the operation is terminal. Only an observed CANCELLED
 * completion proves cancellation. TURBO_EALREADY means the native operation
 * was already completing; its terminal completion must still be observed.
 */
TURBO_NATIVE_IO_C_API int native_io_cancel(turbo_io_backend *backend,
                                                  turbo_io_request request);

/**
 * Directly dequeues up to min(event_capacity, configured batch capacity)
 * completion packets on the owner thread. timeout_ms == 0 polls and
 * UINT32_MAX waits indefinitely. No packet before the deadline returns
 * TURBO_ETIMEDOUT with out_count == 0. Failed I/O is a successfully observed
 * event whose kind/status carry the terminal error. A returned event ends the
 * payload borrow and invalidates that request handle.
 */
TURBO_NATIVE_IO_C_API int native_io_observe(turbo_io_backend *backend,
                                                   turbo_io_completion *events,
                                                   size_t event_capacity, uint32_t timeout_ms,
                                                   size_t *out_count);

/** Closes admission. Accepted requests must still be cancelled or drained. */
TURBO_NATIVE_IO_C_API int native_io_close(turbo_io_backend *backend);

/**
 * Destroys a closed, fully drained backend with no retained endpoints.
 * TURBO_EBUSY preserves ownership when those preconditions are not satisfied.
 */
TURBO_NATIVE_IO_C_API int native_io_destroy(turbo_io_backend *backend);

TURBO_NATIVE_IO_C_API bool native_io_get_stats(const turbo_io_backend *backend,
                                                      turbo_io_backend_stats *out_stats);

#endif /* TURBO_NATIVE_IO_H */
