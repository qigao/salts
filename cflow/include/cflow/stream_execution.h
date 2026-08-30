#ifndef CFLOW_STREAM_EXECUTION_H
#define CFLOW_STREAM_EXECUTION_H

#include <cflow/scheduler.h>
#include <cflow/stream.h>
#include <cmeta/collector.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_stream_execution {
    /* Initialize every handle as `cflow_stream_execution execution = {0};`. */
    void *impl;
} cflow_stream_execution;

typedef enum cflow_stream_execution_state {
    CFLOW_STREAM_EXECUTION_ZERO = 0,
    CFLOW_STREAM_EXECUTION_RUNNING,
    CFLOW_STREAM_EXECUTION_COMPLETED,
    CFLOW_STREAM_EXECUTION_FAILED,
    CFLOW_STREAM_EXECUTION_CANCELLED
} cflow_stream_execution_state;

typedef enum cflow_stream_execution_status {
    CFLOW_STREAM_EXECUTION_OK = 0,
    CFLOW_STREAM_EXECUTION_INVALID_ARGUMENT,
    CFLOW_STREAM_EXECUTION_INVALID_SCHEDULER,
    CFLOW_STREAM_EXECUTION_STREAM_REJECTED,
    CFLOW_STREAM_EXECUTION_COLLECTOR_REJECTED,
    CFLOW_STREAM_EXECUTION_GRAPH_REJECTED,
    CFLOW_STREAM_EXECUTION_PUBLISHER_REJECTED,
    CFLOW_STREAM_EXECUTION_RUN_REJECTED,
    CFLOW_STREAM_EXECUTION_DEMAND_REJECTED,
    CFLOW_STREAM_EXECUTION_ALLOCATION_FAILED,
    CFLOW_STREAM_EXECUTION_ALREADY_STARTED,
    CFLOW_STREAM_EXECUTION_WOULD_BLOCK,
    CFLOW_STREAM_EXECUTION_TERMINATED
} cflow_stream_execution_status;

typedef struct cflow_stream_execution_snapshot {
    cflow_stream_execution_state state;
    cmeta_status collector_status;
    size_t count;
    /* Borrowed from the execution and valid until destroy. */
    const char *error;
} cflow_stream_execution_snapshot;

/* Start one asynchronous collection terminal.
 *
 * The execution owns its normalized Graph, Subscription, and Collector state.
 * It borrows scheduler, the Range's backing object, collector.context, and
 * collector.zero_output until destroy returns. scheduler must advertise
 * CMETA_SCHED_CAP_CONCURRENT. Admission failure restores execution to ZERO and
 * releases every acquired resource.
 *
 * One external control owner must serialize cancel/wait/destroy. Snapshot may
 * run concurrently with worker execution. Control calls from any Range,
 * operator, or Collector callback on this execution's active Subscription fail with
 * WOULD_BLOCK. */
cflow_stream_execution_status cflow_stream_execution_start(
    cflow_stream_execution *execution,
    const cflow_stream *stream,
    cflow_scheduler *scheduler,
    cmeta_collector collector);

/* Synchronously close the Subscription and abort an uncommitted Collector transaction. */
cflow_stream_execution_status cflow_stream_execution_cancel(
    cflow_stream_execution *execution);

/* Wait for COMPLETED, FAILED, or CANCELLED. */
cflow_stream_execution_status cflow_stream_execution_wait(
    cflow_stream_execution *execution);

/* Copy a race-free status view. out->error remains borrowed from execution. */
bool cflow_stream_execution_get_snapshot(
    const cflow_stream_execution *execution,
    cflow_stream_execution_snapshot *out);

/* ZERO destruction is idempotent. RUNNING destruction synchronously cancels. */
cflow_stream_execution_status cflow_stream_execution_destroy(
    cflow_stream_execution *execution);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_STREAM_EXECUTION_H */
