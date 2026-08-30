#ifndef CFLOW_IO_NATIVE_ADAPTER_H
#define CFLOW_IO_NATIVE_ADAPTER_H

#include <cflow/io_actor.h>
#include <cflow/io_publisher.h>
#include <turbo/native_io.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_io_native_adapter {
    void *impl;
} cflow_io_native_adapter;

typedef struct cflow_io_native_adapter_config {
    turbo_io_backend_config backend;
} cflow_io_native_adapter_config;

typedef struct cflow_io_native_adapter_stats {
    turbo_io_backend_stats native;
    size_t active_bridges;
    uint64_t actor_completions;
    uint64_t stale_actor_completions;
} cflow_io_native_adapter_stats;

/**
 * Initializes one caller-driven adapter and its fixed-capacity NativeIO
 * backend. The zero-state adapter is caller-owned. All adapter operations must
 * execute on one fixed owner thread and must not execute concurrently.
 */
int cflow_io_native_adapter_init(
    cflow_io_native_adapter *adapter,
    const cflow_io_native_adapter_config *config);

/**
 * Returns Actor backend operations for caller-owned turbo_io_operation values.
 * Use the adapter as backend_user. The first valid submit attempt binds one
 * Actor for the adapter lifetime, even if NativeIO rejects that operation; a
 * different Actor fails fast.
 */
cflow_io_backend_ops cflow_io_native_adapter_actor_ops(void);

/** Associates a caller-owned native socket with the adapter backend. */
int cflow_io_native_adapter_attach_socket(
    cflow_io_native_adapter *adapter,
    uintptr_t native_socket,
    turbo_io_endpoint *out_endpoint);

/** Associates one async-capable connected byte-pipe endpoint. */
int cflow_io_native_adapter_attach_pipe(
    cflow_io_native_adapter *adapter,
    uintptr_t native_handle,
    uint32_t flags,
    turbo_io_endpoint *out_endpoint);

/** Releases metadata after terminal drain and caller-side socket close. */
int cflow_io_native_adapter_release_socket(
    cflow_io_native_adapter *adapter,
    turbo_io_endpoint endpoint);

/** Releases metadata after terminal drain and caller-side pipe close. */
int cflow_io_native_adapter_release_pipe(
    cflow_io_native_adapter *adapter,
    turbo_io_endpoint endpoint);

/**
 * Observes one fixed completion batch and offers each terminal to the bound
 * Actor exactly once. A returned completion ends the NativeIO payload borrow;
 * the Actor may retain its operation token until acknowledgement.
 */
int cflow_io_native_adapter_observe(
    cflow_io_native_adapter *adapter,
    uint32_t timeout_ms,
    size_t *out_completed);

/**
 * Runs ready Reactive work, drives one Publisher owner submission phase,
 * observes one fixed NativeIO completion batch, drives completion/acknowledge,
 * then runs newly ready Reactive work. max_phase_steps independently bounds
 * each Scheduler and Publisher-owner phase. scheduler must advertise
 * CMETA_SCHED_CAP_CALLER_DRIVEN_ZERO_DELAY and must not be concurrent. This
 * composition adds no queue, thread, or state mirror; the Publisher config may
 * leave drive NULL when all progress and shutdown use this function. The
 * post-observe phases still run when NativeIO reports an error so already
 * offered Actor completions can settle. Returns the first NativeIO error,
 * otherwise the owner status. out_completed is zeroed before validation.
 */
int cflow_io_native_adapter_drive_reactive(
    cflow_io_native_adapter *adapter,
    cflow_io_publisher_owner *owner,
    cflow_scheduler *scheduler,
    uint32_t timeout_ms,
    size_t max_phase_steps,
    size_t *out_completed);

/** Closes NativeIO admission; accepted operations still require observation. */
int cflow_io_native_adapter_close(cflow_io_native_adapter *adapter);

/** Destroys a closed, fully drained adapter and clears its public handle. */
int cflow_io_native_adapter_destroy(cflow_io_native_adapter *adapter);

/** Copies a consistent owner-thread snapshot without advancing I/O. */
bool cflow_io_native_adapter_get_stats(
    const cflow_io_native_adapter *adapter,
    cflow_io_native_adapter_stats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_NATIVE_ADAPTER_H */
