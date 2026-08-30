#ifndef CFLOW_IO_ACTOR_H
#define CFLOW_IO_ACTOR_H

#include <cflow/executor.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cflow_io_request_id;
typedef uint64_t cflow_io_lease_id;

typedef struct cflow_io_actor {
    void *impl;
} cflow_io_actor;

typedef void (*cflow_io_operation_release_fn)(void *operation_user);

/**
 * Move-only operation token. A successful submit clears both fields and moves
 * ownership into the Actor. A rejected submit leaves both fields unchanged.
 */
typedef struct cflow_io_operation {
    void *user;
    cflow_io_operation_release_fn release;
} cflow_io_operation;

typedef enum cflow_io_completion_kind {
    CFLOW_IO_COMPLETION_OK = 0,
    CFLOW_IO_COMPLETION_EOF,
    CFLOW_IO_COMPLETION_CANCELLED,
    CFLOW_IO_COMPLETION_FAILED
} cflow_io_completion_kind;

typedef struct cflow_io_completion {
    cflow_io_completion_kind kind;
    size_t bytes;
    int error;
} cflow_io_completion;

typedef enum cflow_io_lifecycle {
    CFLOW_IO_RUNNING = 0,
    CFLOW_IO_CLOSING
} cflow_io_lifecycle;

typedef enum cflow_io_submit_status {
    CFLOW_IO_SUBMIT_ACCEPTED = 0,
    CFLOW_IO_SUBMIT_INVALID_ARGUMENT,
    CFLOW_IO_SUBMIT_FULL,
    CFLOW_IO_SUBMIT_CLOSED,
    CFLOW_IO_SUBMIT_LEASE_IN_USE,
    CFLOW_IO_SUBMIT_ID_EXHAUSTED
} cflow_io_submit_status;

typedef struct cflow_io_submit_result {
    cflow_io_submit_status status;
    cflow_io_request_id request_id;
} cflow_io_submit_result;

typedef enum cflow_io_cancel_status {
    CFLOW_IO_CANCEL_ACCEPTED = 0,
    CFLOW_IO_CANCEL_INVALID_ARGUMENT,
    CFLOW_IO_CANCEL_FULL,
    CFLOW_IO_CANCEL_CLOSED,
    CFLOW_IO_CANCEL_NOT_FOUND
} cflow_io_cancel_status;

typedef enum cflow_io_complete_status {
    CFLOW_IO_COMPLETE_ACCEPTED = 0,
    CFLOW_IO_COMPLETE_INVALID_ARGUMENT,
    CFLOW_IO_COMPLETE_NOT_FOUND,
    CFLOW_IO_COMPLETE_NOT_PENDING
} cflow_io_complete_status;

typedef enum cflow_io_ack_status {
    CFLOW_IO_ACK_RELEASED = 0,
    CFLOW_IO_ACK_INVALID_ARGUMENT,
    CFLOW_IO_ACK_NOT_FOUND,
    CFLOW_IO_ACK_BUSY
} cflow_io_ack_status;

typedef enum cflow_io_run_status {
    CFLOW_IO_RUN_PROGRESSED = 0,
    CFLOW_IO_RUN_IDLE,
    CFLOW_IO_RUN_BUSY,
    CFLOW_IO_RUN_INVALID_ARGUMENT
} cflow_io_run_status;

typedef struct cflow_io_run_result {
    cflow_io_run_status status;
    size_t progressed;
} cflow_io_run_result;

/**
 * submit() starts one native operation and must eventually call
 * cflow_io_actor_complete() exactly once after returning TURBO_OK. The Actor
 * argument is a borrowed, stable completion handle valid until that request's
 * terminal completion. A nonzero return is converted to FAILED unless the
 * backend completed synchronously first.
 * cancel() is optional and best-effort; its return is not terminal evidence.
 * submit/cancel are serialized by the single Actor driver and run without the
 * Actor mutex held. Either callback may reenter complete(); neither may destroy
 * the Actor.
 */
typedef struct cflow_io_backend_ops {
    int (*submit)(void *backend_user,
                  cflow_io_actor *actor,
                  cflow_io_request_id request_id,
                  cflow_io_lease_id lease_id,
                  void *operation_user);
    int (*cancel)(void *backend_user, cflow_io_request_id request_id);
} cflow_io_backend_ops;

/*
 * The completion callback runs outside the Actor mutex. Its return is the
 * delivery-finish boundary; acknowledge from inside the callback returns BUSY.
 * A concurrent Executor may invoke this callback concurrently for different
 * requests. operation_user and completion are borrowed for callback duration.
 */
typedef void (*cflow_io_completion_fn)(
    void *completion_user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion);

/*
 * Advisory edge notification. Calls may be concurrent and reentrant; the
 * callback should schedule/drive the Actor and must tolerate coalescing. Its
 * context remains borrowed until return, so synchronous destroy is rejected as
 * busy; schedule destruction after the callback returns.
 */
typedef void (*cflow_io_wake_fn)(void *wake_user);

typedef struct cflow_io_actor_config {
    size_t request_capacity;
    size_t command_capacity;
    cflow_executor *executor;
    cflow_io_backend_ops backend;
    void *backend_user;
    cflow_io_completion_fn completion;
    void *completion_user;
    cflow_io_wake_fn wake;
    void *wake_user;
} cflow_io_actor_config;

typedef struct cflow_io_actor_stats {
    size_t request_capacity;
    size_t command_capacity;
    size_t active_requests;
    size_t queued_commands;
    size_t admitted;
    size_t ready;
    size_t backend_pending;
    size_t completions_ready;
    size_t dispatch_queued;
    size_t dispatch_running;
    size_t delivered_unacknowledged;
    uint64_t accepted;
    uint64_t acknowledged;
    uint64_t rejected_request_full;
    uint64_t rejected_command_full;
    uint64_t rejected_closed;
    uint64_t rejected_lease_in_use;
    uint64_t stale_completions;
    uint64_t backend_submit_errors;
    uint64_t backend_cancel_errors;
    uint64_t executor_rejected_full;
    uint64_t executor_rejected_closed;
    uint64_t executor_rejected_invalid;
    cflow_io_lifecycle lifecycle;
} cflow_io_actor_stats;

/**
 * Initializes a bounded IO Actor.
 *
 * The Actor borrows config->executor and backend/callback contexts until a
 * successful destroy. request_capacity and command_capacity are hard logical
 * limits and must be nonzero. The backend submit and completion callbacks are
 * required. Returns TURBO_OK, TURBO_EINVAL, or TURBO_ENOMEM.
 *
 * Thread safety: initialization requires exclusive access to actor.
 */
int cflow_io_actor_init(cflow_io_actor *actor,
                        const cflow_io_actor_config *config);

/**
 * Attempts transactional admission of one move-only operation.
 *
 * ACCEPTED assigns a nonzero request_id, clears operation, and transfers its
 * release obligation to the Actor. Every other status preserves operation
 * unchanged. A nonzero lease_id must be unique among live requests.
 *
 * Thread safety: MPSC; may be called concurrently with other public execution
 * operations except init/destroy.
 */
cflow_io_submit_result cflow_io_actor_try_submit(
    cflow_io_actor *actor,
    cflow_io_lease_id lease_id,
    cflow_io_operation *operation);

/**
 * Publishes a bounded cancellation command for a live request.
 *
 * ACCEPTED means the request was recorded, not that native cancellation has
 * completed. Native completion remains the sole terminal evidence after
 * backend submission.
 */
cflow_io_cancel_status cflow_io_actor_try_cancel(
    cflow_io_actor *actor, cflow_io_request_id request_id);

/**
 * Records the backend's single authoritative terminal completion.
 *
 * The completion value is copied. Duplicate or late completion returns
 * NOT_PENDING and increments stale_completions. This entry is thread-safe and
 * may be called synchronously from backend.submit().
 */
cflow_io_complete_status cflow_io_actor_complete(
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion);

/** Drives at most one state transition; only one driver may run at a time. */
cflow_io_run_result cflow_io_actor_run_one(cflow_io_actor *actor);

/**
 * Drives up to max_steps transitions. max_steps must be nonzero. User/backend
 * callbacks and Executor posting are performed without the Actor mutex held.
 */
cflow_io_run_result cflow_io_actor_run_ready(cflow_io_actor *actor,
                                             size_t max_steps);

/**
 * Releases a delivered operation exactly once. Before delivery this returns
 * BUSY and retains the operation; unknown/already released IDs return
 * NOT_FOUND. The release callback executes outside the Actor mutex.
 */
cflow_io_ack_status cflow_io_actor_acknowledge(
    cflow_io_actor *actor, cflow_io_request_id request_id);

/**
 * Atomically closes admission. Work not yet submitted to the backend becomes
 * CANCELLED; pending native work receives a best-effort cancel request and
 * still waits for authoritative backend completion.
 */
int cflow_io_actor_close(cflow_io_actor *actor);

/**
 * Copies a consistent state snapshot. Before counter saturation,
 * accepted == acknowledged + active_requests. Phase counts partition active
 * requests; RELEASING is included in delivered_unacknowledged. Request-table
 * scans are O(request_capacity); storage is fixed after init.
 */
bool cflow_io_actor_get_stats(const cflow_io_actor *actor,
                              cflow_io_actor_stats *out);

/** True only after close and complete command/request/injected-callback drain. */
bool cflow_io_actor_is_quiescent(const cflow_io_actor *actor);

/**
 * Returns TURBO_EBUSY until the closed Actor is quiescent. The injected backend
 * and Executor must remain alive, and public API entry must itself be
 * quiescent, until destroy returns TURBO_OK. The Executor must remain OPEN and
 * drain-capable until every Actor completion is accepted, delivered and
 * acknowledged; shut it down only after Actor destroy succeeds. Closing it
 * first leaves retained completions visible through executor_rejected_closed
 * and intentionally keeps Actor shutdown busy because cflow_executor has no
 * delivery-cancellation callback.
 */
int cflow_io_actor_destroy(cflow_io_actor *actor);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_ACTOR_H */
