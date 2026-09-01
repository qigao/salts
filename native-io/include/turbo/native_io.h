#ifndef TURBO_NATIVE_IO_H
#define TURBO_NATIVE_IO_H

#include <turbo/error_codes.h>
#include <turbo/native_io_module.h>

#ifndef __cplusplus
  #include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>

typedef struct native_io_backend {
  void *impl;
} native_io_backend;

typedef enum native_io_model {
  NATIVE_IO_MODEL_NONE = 0,
  NATIVE_IO_MODEL_COMPLETION = 1,
  NATIVE_IO_MODEL_READINESS = 2
} native_io_model;

typedef enum native_io_backend_kind {
  NATIVE_IO_BACKEND_IOCP = 1,
  NATIVE_IO_BACKEND_EPOLL,
  NATIVE_IO_BACKEND_IO_URING,
  NATIVE_IO_BACKEND_KQUEUE
} native_io_backend_kind;

typedef struct native_io_endpoint {
  uint32_t slot;
  uint32_t generation;
} native_io_endpoint;

typedef struct native_io_request {
  uint32_t slot;
  uint32_t generation;
} native_io_request;

typedef struct native_io_coroutine native_io_coroutine;

typedef struct native_io_coroutine_task {
  uint32_t slot;
  uint32_t generation;
} native_io_coroutine_task;

typedef void (*native_io_coroutine_entry_fn)(native_io_coroutine *coroutine, void *user_data);

typedef enum native_io_operation_kind {
  NATIVE_IO_OPERATION_TCP_RECV = 1,
  NATIVE_IO_OPERATION_TCP_SEND = 2,
  NATIVE_IO_OPERATION_UDP_RECV_FROM = 3,
  NATIVE_IO_OPERATION_UDP_SEND_TO = 4,
  NATIVE_IO_OPERATION_PIPE_READ = 5,
  NATIVE_IO_OPERATION_PIPE_WRITE = 6,
  NATIVE_IO_OPERATION_TCP_CONNECT = 7
} native_io_operation_kind;

typedef enum native_io_pipe_endpoint_flags {
  NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE = 1u << 0
} native_io_pipe_endpoint_flags;

/**
 * One caller-owned operation descriptor copied by submit.
 *
 * endpoint is a live handle returned by attach_socket or attach_pipe. buffer
 * is borrowed from successful submit until the matching completion is
 * returned by observe.
 * Send storage is immutable during the borrow; receive storage is exclusively
 * mutable by NativeIO. user_data is copied verbatim into the completion.
 *
 * TCP_CONNECT and UDP_SEND_TO read address[0..address_length) as a native sockaddr.
 * TCP_CONNECT requires buffer == NULL and length == 0; its address storage is
 * borrowed until observe returns the matching terminal completion.
 * UDP_RECV_FROM writes at most address_capacity bytes and publishes the actual
 * length in its completion. For an OS-connected datagram socket, UDP_RECV_FROM
 * and UDP_SEND_TO accept all address fields as zero and use connected recv/send
 * semantics. TCP send/receive and pipe operations require all address fields
 * to be zero. Address storage has the same borrow as payload storage.
 */
typedef struct native_io_operation {
  native_io_operation_kind kind;
  native_io_endpoint endpoint;
  void *buffer;
  size_t length;
  uintptr_t user_data;
  void *address;
  size_t address_capacity;
  size_t address_length;
} native_io_operation;

typedef enum native_io_completion_kind {
  NATIVE_IO_COMPLETION_OK = 1,
  NATIVE_IO_COMPLETION_EOF,
  NATIVE_IO_COMPLETION_CANCELLED,
  NATIVE_IO_COMPLETION_FAILED
} native_io_completion_kind;

typedef struct native_io_completion {
  native_io_request request;
  native_io_endpoint endpoint;
  native_io_completion_kind kind;
  size_t bytes;
  int status;
  uint32_t native_status;
  uintptr_t user_data;
  size_t address_length;
} native_io_completion;

typedef struct native_io_backend_config {
  native_io_backend_kind kind;
  /** Hard cap for socket and pipe endpoints retained by this backend. */
  size_t endpoint_capacity;
  /** Hard cap for operations whose terminal completion is not yet observed. */
  size_t request_capacity;
  /** Maximum completions returned by one observe call. */
  size_t completion_batch_capacity;
} native_io_backend_config;

typedef struct native_io_backend_stats {
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
} native_io_backend_stats;

enum { NATIVE_IO_COROUTINE_STATS_ABI_V1 = 1u };

typedef struct native_io_coroutine_stats {
  uint32_t abi_version;
  size_t struct_size;
  /** Hard cap shared by coroutine task slots and in-flight requests. */
  size_t capacity;
  /** Coroutine entries that have not returned yet. */
  size_t active;
  /** Lazily allocated frames retained for bounded reuse. */
  size_t retained_frames;
} native_io_coroutine_stats;

#define NATIVE_IO_COROUTINE_STATS_V1_INITIALIZER                                              \
  {NATIVE_IO_COROUTINE_STATS_ABI_V1, sizeof(native_io_coroutine_stats), 0u, 0u, 0u}

/** Returns the communication model represented by kind, or NONE if invalid. */
TURBO_NATIVE_IO_C_API native_io_model native_io_backend_kind_model(native_io_backend_kind kind);

/** Returns compile-time availability; no fallback backend is selected. */
TURBO_NATIVE_IO_C_API bool native_io_backend_kind_supported(native_io_backend_kind kind);

/** Returns whether kind supports async-capable connected byte pipes. */
TURBO_NATIVE_IO_C_API bool native_io_backend_kind_supports_pipe(native_io_backend_kind kind);

TURBO_NATIVE_IO_C_API bool native_io_endpoint_valid(native_io_endpoint endpoint);
TURBO_NATIVE_IO_C_API bool native_io_request_valid(native_io_request request);
TURBO_NATIVE_IO_C_API bool native_io_coroutine_task_valid(native_io_coroutine_task task);
TURBO_NATIVE_IO_C_API bool native_io_operation_valid(const native_io_operation *operation);

/**
 * Initializes a fixed-capacity backend selected by config.kind.
 *
 * The zero-state backend is owned by the caller. All methods on one backend
 * except native_io_backend_wake are single-owner-thread operations and must
 * not execute concurrently. No backend creates a worker thread: the owner
 * submits and observes directly.
 *
 * @return TURBO_OK; TURBO_EINVAL for malformed handles or capacities;
 *         TURBO_ERANGE for allocation overflow; TURBO_ENOMEM; TURBO_ENOTSUP
 *         when kind is unavailable; otherwise a negative native error code.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_init(native_io_backend *backend,
                                                 const native_io_backend_config *config);

/**
 * Associates one native socket with the backend and returns a generation
 * checked endpoint. SOCK_STREAM endpoints admit only TCP operations;
 * SOCK_DGRAM endpoints admit only UDP operations. IPv4 and IPv6 use the same
 * endpoint type because the address family is independent of transport
 * admission. The backend borrows the socket and never closes it. IOCP requires
 * an overlapped socket; readiness drivers use per-call nonblocking operations
 * and do not change the socket's blocking mode. A socket used with TCP_CONNECT
 * must already be nonblocking when attached to a readiness backend.
 *
 * The caller must retain the returned endpoint, stop submitting before close,
 * drain every request, close the native socket, and then call release_socket.
 *
 * @return TURBO_OK; TURBO_EINVAL; TURBO_ESHUTDOWN; TURBO_EALREADY if the
 *         socket is already attached; TURBO_ENOBUFS at endpoint capacity;
 *         TURBO_ENOTSUP for another socket type; or a negative native query or
 *         association error.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_attach_socket(native_io_backend *backend,
                                                          uintptr_t native_socket,
                                                          native_io_endpoint *out_endpoint);

/**
 * Releases backend metadata after the caller closed the native socket.
 * TURBO_EBUSY preserves the endpoint while any request remains unobserved.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_release_socket(native_io_backend *backend,
                                                           native_io_endpoint endpoint);

/**
 * Associates one connected byte-pipe handle with the backend. The backend
 * borrows the handle and never closes it. flags must be exactly
 * NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE; no worker fallback is selected.
 * IOCP accepts byte-mode named-pipe handles opened with FILE_FLAG_OVERLAPPED
 * and rejects synchronous handles. epoll and kqueue accept nonblocking byte
 * pipe descriptors. The selected backend remains the only progress owner.
 * out_endpoint is cleared on failure.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_attach_pipe(native_io_backend *backend,
                                                        uintptr_t native_handle, uint32_t flags,
                                                        native_io_endpoint *out_endpoint);

/** Releases metadata for a drained pipe endpoint without closing its handle. */
TURBO_NATIVE_IO_C_API int native_io_backend_release_pipe(native_io_backend *backend,
                                                         native_io_endpoint endpoint);

/**
 * Starts one operation without allocating or copying payload messages. For one
 * endpoint, read operations and write operations are admitted in independent
 * FIFO lanes. A backend may retain the copied descriptor in its fixed request
 * storage until that operation reaches the head of its lane.
 * The descriptor is copied; its payload remains borrowed until observe returns
 * the matching terminal completion. out_request is cleared on failure.
 *
 * @return TURBO_OK; TURBO_EINVAL for an invalid operation or an endpoint whose
 *         socket transport does not match it; TURBO_ENOENT for a stale
 *         endpoint; TURBO_EALREADY for duplicate connect; TURBO_EBUSY when
 *         connect conflicts with retained stream operations; TURBO_ESHUTDOWN
 *         after close; TURBO_ENOBUFS when all request slots are retained; or a
 *         negative native submission error.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_submit(native_io_backend *backend,
                                                   const native_io_operation *operation,
                                                   native_io_request *out_request);

/**
 * Starts one bounded coroutine on the backend owner thread.
 *
 * The entry runs immediately until it returns or calls
 * native_io_coroutine_await. A suspended coroutine is resumed only after the
 * matching terminal NativeIO completion is observed. The backend owns and
 * reuses the coroutine frame; entry must not retain coroutine after returning.
 * The task handle becomes stale when entry returns.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_spawn_coroutine(native_io_backend *backend,
                                                            native_io_coroutine_entry_fn entry,
                                                            void *user_data,
                                                            native_io_coroutine_task *out_task);

/**
 * Submits one operation and suspends the currently running NativeIO coroutine.
 *
 * The operation descriptor is copied by the backend. Its borrowed payload and
 * address storage must remain valid across suspension until this function
 * returns. TURBO_OK means out_completion contains the terminal result; native
 * I/O failure remains encoded in completion.kind/status. Submission failures
 * are returned before suspension and leave out_completion cleared.
 */
TURBO_NATIVE_IO_C_API int native_io_coroutine_await(native_io_coroutine *coroutine,
                                                    const native_io_operation *operation,
                                                    native_io_completion *out_completion);

/** Requests cancellation of the operation currently awaited by task. */
TURBO_NATIVE_IO_C_API int native_io_backend_cancel_coroutine(native_io_backend *backend,
                                                             native_io_coroutine_task task);

/**
 * Requests cancellation of an active operation. TURBO_OK means cancellation
 * was marked, not that the operation is terminal. Only an observed CANCELLED
 * completion proves cancellation. TURBO_EALREADY means the native operation
 * was already completing; its terminal completion must still be observed.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_cancel(native_io_backend *backend,
                                                   native_io_request request);

/**
 * Directly dequeues up to min(event_capacity, configured batch capacity)
 * completion packets on the owner thread. Completions owned by a coroutine
 * await resume that coroutine and are not copied to events; processing only
 * such completions returns TURBO_OK with out_count == 0. timeout_ms == 0 polls and
 * UINT32_MAX waits indefinitely. No packet before the deadline returns
 * TURBO_ETIMEDOUT with out_count == 0. Failed I/O is a successfully observed
 * event whose kind/status carry the terminal error. A returned event ends the
 * payload borrow and invalidates that request handle.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_observe(native_io_backend *backend,
                                                    native_io_completion *events,
                                                    size_t event_capacity, uint32_t timeout_ms,
                                                    size_t *out_count);

/**
 * Wakes an owner blocked in observe without publishing a completion.
 *
 * This is the only backend operation that may be called from a non-owner
 * thread. Concurrent wake calls are coalesced into one bounded control signal.
 * A pure control wake makes observe return TURBO_OK with out_count == 0. The
 * caller must publish its command before wake and must stop all wake callers
 * before close/destroy; wake after close returns TURBO_ESHUTDOWN.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_wake(native_io_backend *backend);

/**
 * Closes admission. Accepted direct and coroutine requests must still be
 * cancelled or drained.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_close(native_io_backend *backend);

/**
 * Destroys a closed, fully drained backend with no retained endpoints or
 * active coroutine entries. Retained inactive coroutine frames are released.
 * TURBO_EBUSY preserves ownership when those preconditions are not satisfied.
 */
TURBO_NATIVE_IO_C_API int native_io_backend_destroy(native_io_backend *backend);

TURBO_NATIVE_IO_C_API bool native_io_backend_get_stats(const native_io_backend *backend,
                                                       native_io_backend_stats *out_stats);

/** Queries versioned coroutine-owner capacity and retention statistics. */
TURBO_NATIVE_IO_C_API bool
native_io_backend_get_coroutine_stats(const native_io_backend *backend,
                                      native_io_coroutine_stats *out_stats);

#endif /* TURBO_NATIVE_IO_H */
