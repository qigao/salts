#ifndef CFLOW_IO_NATIVE_H
#define CFLOW_IO_NATIVE_H

#include <cflow/io_actor.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_io_native_backend {
    void *impl;
} cflow_io_native_backend;

typedef enum cflow_io_native_backend_kind {
    CFLOW_IO_NATIVE_EPOLL = 1,
    CFLOW_IO_NATIVE_KQUEUE,
    CFLOW_IO_NATIVE_IOCP,
    CFLOW_IO_NATIVE_IO_URING,
    CFLOW_IO_NATIVE_POLL
} cflow_io_native_backend_kind;

typedef enum cflow_io_native_operation_kind {
    CFLOW_IO_NATIVE_TCP_RECV = 0,
    CFLOW_IO_NATIVE_TCP_SEND,
    CFLOW_IO_NATIVE_UDP_RECV_FROM,
    CFLOW_IO_NATIVE_UDP_SEND_TO,
    CFLOW_IO_NATIVE_TCP_ACCEPT,
    CFLOW_IO_NATIVE_TCP_CONNECT
} cflow_io_native_operation_kind;

#define CFLOW_IO_NATIVE_INVALID_SOCKET UINTPTR_MAX

/**
 * Caller-owned native socket operation borrowed from successful Actor submit
 * until its completion callback returns. buffer is immutable for send and is
 * backend-exclusive mutable storage for recv. The backend never closes socket.
 *
 * UDP send and TCP connect consume address[0..address_length). UDP recv and
 * TCP accept write at most address_capacity bytes and publish address_length
 * before completion. address bytes use the host OS sockaddr representation.
 * TCP recv/send ignore address.
 *
 * TCP accept requires result_socket == CFLOW_IO_NATIVE_INVALID_SOCKET on
 * submit. The backend owns a provisional accepted socket and closes it on
 * failure, cancellation, or stale Actor completion. Successful Actor terminal
 * publication transfers a nonblocking accepted socket to result_socket; the
 * caller must close it. TCP connect never transfers or closes a socket.
 */
typedef struct cflow_io_native_operation {
    cflow_io_native_operation_kind kind;
    uintptr_t socket;
    void *buffer;
    size_t length;
    void *address;
    size_t address_capacity;
    size_t address_length;
    uintptr_t result_socket;
} cflow_io_native_operation;

typedef struct cflow_io_native_backend_config {
    cflow_io_native_backend_kind kind;
    /** Hard cap for in-flight requests and retained native socket identities. */
    size_t request_capacity;
    /**
     * Maximum normal I/O completions attempted per native event. A terminal
     * backend error drains the affected bounded lane, up to request_capacity,
     * because failed readiness cannot be rearmed and every accepted request
     * still requires one authoritative completion.
     */
    size_t completion_batch_capacity;
} cflow_io_native_backend_config;

typedef struct cflow_io_native_backend_stats {
    size_t request_capacity;
    size_t active_requests;
    uint64_t submitted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t stale_native_completions;
    uint64_t native_submit_errors;
    uint64_t native_cancel_errors;
    bool admission_open;
    bool worker_running;
    bool shutdown_complete;
} cflow_io_native_backend_stats;

/** Returns compile-time availability only; runtime kernel policy may still reject init. */
bool cflow_io_native_backend_supported(cflow_io_native_backend_kind kind);

/**
 * Initializes one explicitly selected bounded backend. Unsupported kinds return
 * TURBO_ENOTSUP without fallback. The backend handle must be zero-initialized.
 */
int cflow_io_native_backend_init(
    cflow_io_native_backend *backend,
    const cflow_io_native_backend_config *config);

/** Ops are used with backend_user pointing at cflow_io_native_backend. */
cflow_io_backend_ops cflow_io_native_backend_actor_ops(void);

bool cflow_io_native_backend_get_stats(
    const cflow_io_native_backend *backend,
    cflow_io_native_backend_stats *out);

/**
 * Releases backend-side identity retained for a socket after the caller has
 * closed it and all operations using it have completed. IOCP association is
 * permanent for a live handle. Readiness backends retain at most one read lane
 * and one write lane per identity so duplicate descriptors and registrations
 * scale with live sockets instead of operations. This explicit boundary makes
 * each bounded socket table reusable without guessing whether the OS recycled
 * a handle. TURBO_EBUSY means this socket is not quiescent; TURBO_ENOENT means
 * the identity is unknown or was already forgotten. io_uring retains no
 * identity and only validates its existing global quiescence contract.
 */
int cflow_io_native_backend_forget_socket(
    cflow_io_native_backend *backend, uintptr_t closed_socket);

/**
 * Closes admission. Returns TURBO_EBUSY while native requests remain active;
 * retry after Actor cancellation/completion drain. TURBO_OK joins any backend
 * worker; readiness adapters rely only on the Platform reactor worker.
 */
int cflow_io_native_backend_shutdown(cflow_io_native_backend *backend);

/** Returns TURBO_EBUSY until shutdown succeeds; TURBO_OK clears the handle. */
int cflow_io_native_backend_destroy(cflow_io_native_backend *backend);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_NATIVE_H */
