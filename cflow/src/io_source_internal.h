#ifndef CFLOW_IO_SOURCE_INTERNAL_H
#define CFLOW_IO_SOURCE_INTERNAL_H

#include <cflow/io_source.h>

#include <turbo/thread.h>

#include "value_storage.h"

#include <stdint.h>

typedef struct cflow_io_source_entry {
    cflow_value_slot result;
    cflow_io_request_id request_id;
    cflow_io_lease_id lease_id;
    uint64_t delivery_sequence;
    cflow_read_status result_status;
    const char *result_error;
    bool occupied;
    bool submission_in_progress;
    bool result_encoding;
    bool result_ready;
    bool completion_delivered;
    bool acknowledged;
    bool demand_reserved;
} cflow_io_source_entry;

typedef struct cflow_io_source_state {
    cflow_io_actor actor;
    cflow_executor executor;
    cflow_value_slot result;
    cflow_io_source_entry *entries;
    turbo_mutex_t gate;
    turbo_cond_t changed;
    cflow_waker source_waker;
    cflow_io_request_id request_id;
    size_t wake_inflight;
    size_t drive_inflight;
    size_t window_capacity;
    size_t window_occupied;
    size_t window_demand_reserved;
    size_t window_results_ready;
    size_t window_peak_occupied;
    uint64_t drive_generation;
    uint64_t next_delivery_sequence;
    bool source_live;
    bool owner_live;
    bool close_requested;
    bool driver_active;
    bool drive_pending;
    bool submission_in_progress;
    bool result_encoding;
    bool result_ready;
    bool completion_delivered;
    bool acknowledged;
    bool prepare_done;
    bool terminal_delivery_seen;
    cflow_read_status result_status;
    const char *result_error;
    const char *name;
    const cmeta_type_desc *type;
    cflow_io_source_prepare_fn prepare;
    cflow_io_source_encode_fn encode;
    void *user;
    cflow_io_wake_fn drive;
    void *drive_user;
    cflow_source_terminal terminal;
    const char *terminal_error;
} cflow_io_source_state;

/* Test-only seam for placing the deterministic Executor barrier in the
   adapter-driver tail window while the owner remains live. */
static inline cflow_executor *cflow_io_source_test_executor(
    cflow_io_source_owner *owner) {
    cflow_io_source_state *state;

    if (owner == NULL || owner->impl == NULL)
        return NULL;
    state = (cflow_io_source_state *)owner->impl;
    return &state->executor;
}

#endif /* CFLOW_IO_SOURCE_INTERNAL_H */
