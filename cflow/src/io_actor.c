#include <cflow/io_actor.h>

#include <turbo/disruptor.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "io_actor_internal.h"
#include "io_driver_internal.h"

typedef enum cflow_io_command_kind {
    CFLOW_IO_COMMAND_SUBMIT = 0,
    CFLOW_IO_COMMAND_CANCEL
} cflow_io_command_kind;

typedef struct cflow_io_command {
    cflow_io_command_kind kind;
    cflow_io_request_id request_id;
} cflow_io_command;

typedef struct cflow_io_completion_event {
    cflow_io_request_id request_id;
    size_t slot_index;
    cflow_io_completion completion;
} cflow_io_completion_event;

typedef enum cflow_io_request_phase {
    CFLOW_IO_REQUEST_FREE = 0,
    CFLOW_IO_REQUEST_ADMITTED,
    CFLOW_IO_REQUEST_READY,
    CFLOW_IO_REQUEST_BACKEND_PENDING,
    CFLOW_IO_REQUEST_COMPLETION_QUEUED,
    CFLOW_IO_REQUEST_COMPLETED,
    CFLOW_IO_REQUEST_DISPATCH_QUEUED,
    CFLOW_IO_REQUEST_DISPATCH_RUNNING,
    CFLOW_IO_REQUEST_DELIVERED,
    CFLOW_IO_REQUEST_RELEASING
} cflow_io_request_phase;

typedef enum cflow_io_runner_state {
    /* SCHEDULED owns the single advisory wake credit until a driver consumes
       it; RUNNING defers at most one replacement credit to driver exit. */
    CFLOW_IO_RUNNER_IDLE = 0,
    CFLOW_IO_RUNNER_SCHEDULED,
    CFLOW_IO_RUNNER_RUNNING
} cflow_io_runner_state;

typedef struct cflow_io_actor_impl cflow_io_actor_impl;

typedef struct cflow_io_request_slot {
    cflow_io_actor_impl *owner;
    cflow_io_request_id request_id;
    cflow_io_lease_id lease_id;
    atomic_uint phase;
    cflow_io_operation operation;
    cflow_io_completion completion;
    bool cancel_requested;
    bool cancel_dispatched;
} cflow_io_request_slot;

typedef enum cflow_io_action_kind {
    CFLOW_IO_ACTION_NONE = 0,
    CFLOW_IO_ACTION_STATE_ONLY,
    CFLOW_IO_ACTION_CANCEL_BACKEND,
    CFLOW_IO_ACTION_SUBMIT_BACKEND,
    CFLOW_IO_ACTION_DISPATCH_COMPLETION
} cflow_io_action_kind;

typedef struct cflow_io_action {
    cflow_io_action_kind kind;
    cflow_io_request_slot *slot;
    cflow_io_request_id request_id;
    cflow_io_lease_id lease_id;
    void *operation_user;
} cflow_io_action;

struct cflow_io_actor_impl {
    turbo_mutex_t gate;
    cflow_io_actor backend_actor;
    cflow_io_request_slot *requests;
    size_t request_capacity;
    size_t command_capacity;
    size_t active_requests;
    size_t command_depth;
    size_t callbacks_inflight;
    disruptor_t *commands;
    disruptor_consumer_t command_consumer;
    uint64_t next_command_sequence;
    disruptor_t *completions;
    disruptor_consumer_t completion_consumer;
    uint64_t next_completion_sequence;
    atomic_size_t completion_depth;
    atomic_size_t completion_publishers_inflight;
    cflow_executor *executor;
    cflow_io_driver driver;
    cflow_io_completion_fn completion;
    void *completion_user;
    cflow_io_wake_fn wake;
    void *wake_user;
    cflow_io_request_id next_request_id;
    cflow_io_lifecycle lifecycle;
    cflow_io_runner_state runner_state;
    bool wake_pending;
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
};

static cflow_io_request_phase io_phase_load_locked(
    const cflow_io_request_slot *slot) {
    return (cflow_io_request_phase)atomic_load_explicit(
        &slot->phase, memory_order_relaxed);
}

static void io_phase_store_locked(cflow_io_request_slot *slot,
                                  cflow_io_request_phase phase) {
    atomic_store_explicit(&slot->phase, (unsigned)phase,
                          memory_order_relaxed);
}

static void io_counter_increment(uint64_t *counter) {
    if (*counter != UINT64_MAX)
        ++*counter;
}

static uint64_t io_round_up_power_of_two(size_t value) {
    uint64_t rounded = 1u;
    if (value == 0u)
        return 0u;
    while (rounded < (uint64_t)value) {
        if (rounded > (UINT64_MAX >> 1u))
            return 0u;
        rounded <<= 1u;
    }
    return rounded;
}

static cflow_io_actor_impl *io_impl(cflow_io_actor *actor) {
    return actor != NULL ? (cflow_io_actor_impl *)actor->impl : NULL;
}

static const cflow_io_actor_impl *io_const_impl(const cflow_io_actor *actor) {
    return actor != NULL ? (const cflow_io_actor_impl *)actor->impl : NULL;
}

static cflow_io_request_slot *io_find_request_locked(
    cflow_io_actor_impl *impl, cflow_io_request_id request_id) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        cflow_io_request_slot *slot = &impl->requests[index];
        if (io_phase_load_locked(slot) != CFLOW_IO_REQUEST_FREE &&
            slot->request_id == request_id)
            return slot;
    }
    return NULL;
}

static cflow_io_request_slot *io_find_free_request_locked(
    cflow_io_actor_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (io_phase_load_locked(&impl->requests[index]) ==
            CFLOW_IO_REQUEST_FREE)
            return &impl->requests[index];
    }
    return NULL;
}

static bool io_lease_in_use_locked(const cflow_io_actor_impl *impl,
                                   cflow_io_lease_id lease_id) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        const cflow_io_request_slot *slot = &impl->requests[index];
        if (io_phase_load_locked(slot) != CFLOW_IO_REQUEST_FREE &&
            slot->lease_id == lease_id)
            return true;
    }
    return false;
}

static bool io_completion_valid(const cflow_io_completion *completion) {
    if (completion == NULL ||
        completion->kind > CFLOW_IO_COMPLETION_FAILED)
        return false;
    switch (completion->kind) {
        case CFLOW_IO_COMPLETION_OK:
            return completion->error == TURBO_OK;
        case CFLOW_IO_COMPLETION_EOF:
        case CFLOW_IO_COMPLETION_CANCELLED:
            return completion->bytes == 0u && completion->error == TURBO_OK;
        case CFLOW_IO_COMPLETION_FAILED:
            return completion->bytes == 0u && completion->error != TURBO_OK;
    }
    return false;
}

static cflow_io_request_slot *io_reserve_completion_slot(
    cflow_io_actor_impl *impl, cflow_io_request_id request_id) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        cflow_io_request_slot *slot = &impl->requests[index];
        unsigned expected = CFLOW_IO_REQUEST_BACKEND_PENDING;
        if (atomic_load_explicit(&slot->phase, memory_order_acquire) !=
                CFLOW_IO_REQUEST_BACKEND_PENDING ||
            slot->request_id != request_id)
            continue;
        if (atomic_compare_exchange_strong_explicit(
                &slot->phase, &expected,
                CFLOW_IO_REQUEST_COMPLETION_QUEUED,
                memory_order_acq_rel, memory_order_acquire))
            return slot;
    }
    return NULL;
}

static bool io_prepare_wake_locked(cflow_io_actor_impl *impl,
                                   cflow_io_wake_fn *wake_fn,
                                   void **wake_user) {
    impl->wake_pending = true;
    if (impl->runner_state != CFLOW_IO_RUNNER_IDLE ||
        impl->wake == NULL)
        return false;
    impl->runner_state = CFLOW_IO_RUNNER_SCHEDULED;
    impl->wake_pending = false;
    *wake_fn = impl->wake;
    *wake_user = impl->wake_user;
    return true;
}

static void io_notify(cflow_io_actor_impl *impl) {
    cflow_io_wake_fn wake_fn = NULL;
    void *wake_user = NULL;
    turbo_mutex_lock(&impl->gate);
    if (io_prepare_wake_locked(impl, &wake_fn, &wake_user))
        ++impl->callbacks_inflight;
    turbo_mutex_unlock(&impl->gate);
    if (wake_fn != NULL) {
        wake_fn(wake_user);
        turbo_mutex_lock(&impl->gate);
        --impl->callbacks_inflight;
        turbo_mutex_unlock(&impl->gate);
    }
}

static void io_finish_completion_publication(cflow_io_actor_impl *impl) {
    cflow_io_wake_fn wake_fn = NULL;
    void *wake_user = NULL;

    for (;;) {
        size_t publishers = atomic_load_explicit(
            &impl->completion_publishers_inflight, memory_order_acquire);
        if (publishers > 1u) {
            if (atomic_compare_exchange_weak_explicit(
                    &impl->completion_publishers_inflight, &publishers,
                    publishers - 1u, memory_order_acq_rel,
                    memory_order_acquire))
                return;
            continue;
        }
        if (publishers == 0u)
            return;

        /* The last publisher releases its credit and establishes the batch
           wake while holding gate. A consumer can therefore never observe a
           visible batch whose final publisher still has Actor work to do. */
        turbo_mutex_lock(&impl->gate);
        publishers = 1u;
        if (!atomic_compare_exchange_strong_explicit(
                &impl->completion_publishers_inflight, &publishers, 0u,
                memory_order_acq_rel, memory_order_acquire)) {
            turbo_mutex_unlock(&impl->gate);
            continue;
        }
        if (atomic_load_explicit(&impl->completion_depth,
                                 memory_order_acquire) != 0u &&
            io_prepare_wake_locked(impl, &wake_fn, &wake_user))
            ++impl->callbacks_inflight;
        turbo_mutex_unlock(&impl->gate);
        if (wake_fn != NULL) {
            wake_fn(wake_user);
            turbo_mutex_lock(&impl->gate);
            --impl->callbacks_inflight;
            turbo_mutex_unlock(&impl->gate);
        }
        return;
    }
}

static bool io_publish_command_locked(cflow_io_actor_impl *impl,
                                      cflow_io_command command) {
    disruptor_cursor_t cursor = {0};
    cflow_io_command *entry;
    if (impl->command_depth >= impl->command_capacity ||
        !disruptor_publisher_try_claim(impl->commands, &cursor))
        return false;
    entry = (cflow_io_command *)disruptor_acquire_entry(
        impl->commands, &cursor);
    *entry = command;
    disruptor_publisher_commit_entry_blocking(impl->commands, &cursor);
    ++impl->command_depth;
    return true;
}

static bool io_take_command_locked(cflow_io_actor_impl *impl,
                                   cflow_io_command *out) {
    disruptor_cursor_t available;
    disruptor_cursor_t current;
    const cflow_io_command *entry;
    if (impl->command_depth == 0u)
        return false;
    current.sequence = impl->next_command_sequence;
    available = current;
    if (!disruptor_consumer_wait_for_nonblocking_for(
            impl->commands, &impl->command_consumer, &available))
        return false;
    entry = (const cflow_io_command *)disruptor_show_entry(
        impl->commands, &current);
    if (entry == NULL)
        return false;
    *out = *entry;
    disruptor_consumer_release_entry(
        impl->commands, &impl->command_consumer, &current);
    ++impl->next_command_sequence;
    --impl->command_depth;
    return true;
}

static cflow_io_complete_status io_completion_reservation_failure(
    cflow_io_actor_impl *impl,
    cflow_io_request_id request_id) {
    cflow_io_request_slot *slot;
    turbo_mutex_lock(&impl->gate);
    slot = io_find_request_locked(impl, request_id);
    if (slot == NULL) {
        io_counter_increment(&impl->stale_completions);
        turbo_mutex_unlock(&impl->gate);
        return CFLOW_IO_COMPLETE_NOT_FOUND;
    }
    io_counter_increment(&impl->stale_completions);
    turbo_mutex_unlock(&impl->gate);
    return CFLOW_IO_COMPLETE_NOT_PENDING;
}

static cflow_io_complete_status io_complete_inline(
    cflow_io_actor_impl *impl,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion) {
    cflow_io_request_slot *slot;
    cflow_io_wake_fn wake_fn = NULL;
    void *wake_user = NULL;

    if (impl == NULL || request_id == 0u ||
        !io_completion_valid(completion))
        return CFLOW_IO_COMPLETE_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->gate);
    slot = io_find_request_locked(impl, request_id);
    if (slot == NULL) {
        io_counter_increment(&impl->stale_completions);
        turbo_mutex_unlock(&impl->gate);
        return CFLOW_IO_COMPLETE_NOT_FOUND;
    }
    if (io_phase_load_locked(slot) !=
        CFLOW_IO_REQUEST_BACKEND_PENDING) {
        io_counter_increment(&impl->stale_completions);
        turbo_mutex_unlock(&impl->gate);
        return CFLOW_IO_COMPLETE_NOT_PENDING;
    }
    slot->completion = *completion;
    io_phase_store_locked(slot, CFLOW_IO_REQUEST_COMPLETED);
    if (io_prepare_wake_locked(impl, &wake_fn, &wake_user))
        ++impl->callbacks_inflight;
    turbo_mutex_unlock(&impl->gate);
    if (wake_fn != NULL) {
        wake_fn(wake_user);
        turbo_mutex_lock(&impl->gate);
        --impl->callbacks_inflight;
        turbo_mutex_unlock(&impl->gate);
    }
    return CFLOW_IO_COMPLETE_ACCEPTED;
}

static bool io_take_completion_locked(cflow_io_actor_impl *impl) {
    disruptor_cursor_t available;
    disruptor_cursor_t current;
    const cflow_io_completion_event *entry;
    cflow_io_request_slot *slot;

    if (atomic_load_explicit(&impl->completion_depth,
                             memory_order_acquire) == 0u ||
        atomic_load_explicit(&impl->completion_publishers_inflight,
                             memory_order_acquire) != 0u)
        return false;
    current.sequence = impl->next_completion_sequence;
    available = current;
    if (!disruptor_consumer_wait_for_nonblocking_for(
            impl->completions, &impl->completion_consumer, &available))
        return false;
    entry = (const cflow_io_completion_event *)disruptor_show_entry(
        impl->completions, &current);
    if (entry == NULL)
        return false;

    slot = entry->slot_index < impl->request_capacity
        ? &impl->requests[entry->slot_index] : NULL;
    if (slot != NULL && slot->request_id == entry->request_id &&
        io_phase_load_locked(slot) ==
            CFLOW_IO_REQUEST_COMPLETION_QUEUED) {
        slot->completion = entry->completion;
        io_phase_store_locked(slot, CFLOW_IO_REQUEST_COMPLETED);
    } else {
        io_counter_increment(&impl->stale_completions);
    }
    disruptor_consumer_release_entry(
        impl->completions, &impl->completion_consumer, &current);
    ++impl->next_completion_sequence;
    (void)atomic_fetch_sub_explicit(&impl->completion_depth, 1u,
                                    memory_order_release);
    return true;
}

static bool io_drain_completions_locked(cflow_io_actor_impl *impl) {
    bool progressed = false;
    /* Admission also takes gate, so at most the already-active bounded request
       set can publish while this single consumer drains the batch. */
    while (io_take_completion_locked(impl))
        progressed = true;
    return progressed;
}

static void io_delivery_task(void *user) {
    cflow_io_request_slot *slot = (cflow_io_request_slot *)user;
    cflow_io_actor_impl *impl;
    cflow_io_completion_fn completion_fn;
    cflow_io_wake_fn wake_fn = NULL;
    void *completion_user;
    void *wake_user = NULL;
    void *operation_user;
    cflow_io_request_id request_id;
    cflow_io_lease_id lease_id;
    cflow_io_completion completion;

    if (slot == NULL || slot->owner == NULL)
        return;
    impl = slot->owner;
    turbo_mutex_lock(&impl->gate);
    if (io_phase_load_locked(slot) !=
        CFLOW_IO_REQUEST_DISPATCH_QUEUED) {
        turbo_mutex_unlock(&impl->gate);
        return;
    }
    io_phase_store_locked(slot, CFLOW_IO_REQUEST_DISPATCH_RUNNING);
    ++impl->callbacks_inflight;
    completion_fn = impl->completion;
    completion_user = impl->completion_user;
    operation_user = slot->operation.user;
    request_id = slot->request_id;
    lease_id = slot->lease_id;
    completion = slot->completion;
    turbo_mutex_unlock(&impl->gate);

    completion_fn(completion_user, request_id, lease_id,
                  operation_user, &completion);

    turbo_mutex_lock(&impl->gate);
    if (io_phase_load_locked(slot) ==
        CFLOW_IO_REQUEST_DISPATCH_RUNNING)
        io_phase_store_locked(slot, CFLOW_IO_REQUEST_DELIVERED);
    (void)io_prepare_wake_locked(impl, &wake_fn, &wake_user);
    /* Delivery linearizes here and the slot is never touched again. A wake
       keeps the callback credit until its borrowed context has returned; the
       final unlock after decrement is the wrapper's last Actor access. */
    if (wake_fn == NULL)
        --impl->callbacks_inflight;
    turbo_mutex_unlock(&impl->gate);
    if (wake_fn != NULL) {
        wake_fn(wake_user);
        turbo_mutex_lock(&impl->gate);
        --impl->callbacks_inflight;
        turbo_mutex_unlock(&impl->gate);
    }
}

static bool io_select_action_locked(cflow_io_actor_impl *impl,
                                    cflow_io_action *action) {
    cflow_io_command command;
    cflow_io_request_slot *slot = NULL;
    const bool gathered_completion = io_drain_completions_locked(impl);
    size_t index;

    if (io_take_command_locked(impl, &command)) {
        slot = io_find_request_locked(impl, command.request_id);
        if (slot != NULL) {
            if (command.kind == CFLOW_IO_COMMAND_SUBMIT &&
                io_phase_load_locked(slot) == CFLOW_IO_REQUEST_ADMITTED) {
                if (impl->lifecycle == CFLOW_IO_CLOSING) {
                    slot->completion = (cflow_io_completion){
                        CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
                    io_phase_store_locked(slot, CFLOW_IO_REQUEST_COMPLETED);
                } else {
                    io_phase_store_locked(slot, CFLOW_IO_REQUEST_READY);
                }
            } else if (command.kind == CFLOW_IO_COMMAND_CANCEL) {
                const cflow_io_request_phase phase =
                    io_phase_load_locked(slot);
                if (phase == CFLOW_IO_REQUEST_ADMITTED ||
                    phase == CFLOW_IO_REQUEST_READY) {
                    slot->completion = (cflow_io_completion){
                        CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
                    io_phase_store_locked(slot, CFLOW_IO_REQUEST_COMPLETED);
                } else if (phase == CFLOW_IO_REQUEST_BACKEND_PENDING) {
                    slot->cancel_requested = true;
                }
            }
        }
        action->kind = CFLOW_IO_ACTION_STATE_ONLY;
        return true;
    }

    for (index = 0u; index < impl->request_capacity; ++index) {
        slot = &impl->requests[index];
        if (io_phase_load_locked(slot) ==
                CFLOW_IO_REQUEST_BACKEND_PENDING &&
            slot->cancel_requested && !slot->cancel_dispatched) {
            slot->cancel_dispatched = true;
            action->kind = CFLOW_IO_ACTION_CANCEL_BACKEND;
            action->request_id = slot->request_id;
            return true;
        }
    }

    slot = NULL;
    for (index = 0u; index < impl->request_capacity; ++index) {
        cflow_io_request_slot *candidate = &impl->requests[index];
        if (io_phase_load_locked(candidate) == CFLOW_IO_REQUEST_READY &&
            (slot == NULL || candidate->request_id < slot->request_id))
            slot = candidate;
    }
    if (slot != NULL) {
        if (impl->lifecycle == CFLOW_IO_CLOSING) {
            slot->completion = (cflow_io_completion){
                CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
            io_phase_store_locked(slot, CFLOW_IO_REQUEST_COMPLETED);
            action->kind = CFLOW_IO_ACTION_STATE_ONLY;
        } else {
            /* Native completion producers acquire this transition before
               reading the stable request identity outside gate. */
            atomic_store_explicit(&slot->phase,
                                  CFLOW_IO_REQUEST_BACKEND_PENDING,
                                  memory_order_release);
            action->kind = CFLOW_IO_ACTION_SUBMIT_BACKEND;
            action->request_id = slot->request_id;
            action->lease_id = slot->lease_id;
            action->operation_user = slot->operation.user;
        }
        return true;
    }

    for (index = 0u; index < impl->request_capacity; ++index) {
        if (io_phase_load_locked(&impl->requests[index]) ==
            CFLOW_IO_REQUEST_COMPLETED) {
            action->kind = CFLOW_IO_ACTION_DISPATCH_COMPLETION;
            action->slot = &impl->requests[index];
            io_phase_store_locked(action->slot,
                                  CFLOW_IO_REQUEST_DISPATCH_QUEUED);
            return true;
        }
    }

    if (gathered_completion) {
        action->kind = CFLOW_IO_ACTION_STATE_ONLY;
        return true;
    }
    return false;
}

static bool io_execute_action(cflow_io_actor_impl *impl,
                              const cflow_io_action *action) {
    int status;

    switch (action->kind) {
        case CFLOW_IO_ACTION_STATE_ONLY:
            return true;
        case CFLOW_IO_ACTION_CANCEL_BACKEND:
            status = cflow_io_driver_cancel(
                &impl->driver, action->request_id);
            if (status != TURBO_OK) {
                turbo_mutex_lock(&impl->gate);
                io_counter_increment(&impl->backend_cancel_errors);
                turbo_mutex_unlock(&impl->gate);
            }
            return true;
        case CFLOW_IO_ACTION_SUBMIT_BACKEND:
            status = cflow_io_driver_submit(
                &impl->driver, &impl->backend_actor,
                action->request_id, action->lease_id,
                action->operation_user);
            if (status != TURBO_OK) {
                bool completed_failure = false;
                cflow_io_request_slot *slot;

                turbo_mutex_lock(&impl->gate);
                io_counter_increment(&impl->backend_submit_errors);
                slot = io_find_request_locked(impl, action->request_id);
                if (slot != NULL &&
                    io_phase_load_locked(slot) ==
                        CFLOW_IO_REQUEST_BACKEND_PENDING) {
                    slot->completion = (cflow_io_completion){
                        CFLOW_IO_COMPLETION_FAILED, 0u, status};
                    io_phase_store_locked(slot,
                                          CFLOW_IO_REQUEST_COMPLETED);
                    completed_failure = true;
                }
                turbo_mutex_unlock(&impl->gate);
                if (completed_failure)
                    io_notify(impl);
            }
            return true;
        case CFLOW_IO_ACTION_DISPATCH_COMPLETION: {
            const cflow_admission_status admitted = cflow_executor_try_post(
                impl->executor, io_delivery_task, action->slot);

            if (admitted == CFLOW_ADMISSION_ACCEPTED)
                return true;
            turbo_mutex_lock(&impl->gate);
            if (io_phase_load_locked(action->slot) ==
                CFLOW_IO_REQUEST_DISPATCH_QUEUED)
                io_phase_store_locked(action->slot,
                                      CFLOW_IO_REQUEST_COMPLETED);
            if (admitted == CFLOW_ADMISSION_FULL)
                io_counter_increment(&impl->executor_rejected_full);
            else if (admitted == CFLOW_ADMISSION_CLOSED)
                io_counter_increment(&impl->executor_rejected_closed);
            else
                io_counter_increment(&impl->executor_rejected_invalid);
            turbo_mutex_unlock(&impl->gate);
            return false;
        }
        case CFLOW_IO_ACTION_NONE:
            return false;
    }

    return false;
}

static bool io_run_step(cflow_io_actor_impl *impl) {
    cflow_io_action action = {CFLOW_IO_ACTION_NONE, NULL, 0u, 0u, NULL};
    bool selected;

    /* The single driver commits at most one action per gate acquisition.
       Selection is O(request_capacity) with O(1) borrowed action state. */
    turbo_mutex_lock(&impl->gate);
    selected = io_select_action_locked(impl, &action);
    if (!selected) {
        /* This idle observation consumes every edge published before it.
           A completion published after this point takes the same gate and
           restores one replacement wake before the driver leaves. */
        impl->wake_pending = false;
    }
    turbo_mutex_unlock(&impl->gate);
    return selected && io_execute_action(impl, &action);
}

static bool io_has_runnable_action_locked(
    const cflow_io_actor_impl *impl) {
    size_t index;

    if (impl->command_depth != 0u)
        return true;
    if (atomic_load_explicit(&impl->completion_depth,
                             memory_order_acquire) != 0u &&
        atomic_load_explicit(&impl->completion_publishers_inflight,
                             memory_order_acquire) == 0u)
        return true;
    for (index = 0u; index < impl->request_capacity; ++index) {
        const cflow_io_request_slot *slot = &impl->requests[index];
        const cflow_io_request_phase phase = io_phase_load_locked(slot);
        if (phase == CFLOW_IO_REQUEST_READY ||
            phase == CFLOW_IO_REQUEST_COMPLETED ||
            (phase == CFLOW_IO_REQUEST_BACKEND_PENDING &&
             slot->cancel_requested && !slot->cancel_dispatched))
            return true;
    }
    return false;
}

static bool io_driver_enter(cflow_io_actor_impl *impl) {
    bool entered = false;
    turbo_mutex_lock(&impl->gate);
    if (impl->runner_state != CFLOW_IO_RUNNER_RUNNING) {
        impl->runner_state = CFLOW_IO_RUNNER_RUNNING;
        impl->wake_pending = false;
        entered = true;
    }
    turbo_mutex_unlock(&impl->gate);
    return entered;
}

static void io_driver_leave(cflow_io_actor_impl *impl,
                            bool stopped_at_quantum) {
    cflow_io_wake_fn wake_fn = NULL;
    void *wake_user = NULL;
    turbo_mutex_lock(&impl->gate);
    if (stopped_at_quantum && io_has_runnable_action_locked(impl))
        impl->wake_pending = true;
    if (impl->wake_pending && impl->wake != NULL) {
        impl->runner_state = CFLOW_IO_RUNNER_SCHEDULED;
        impl->wake_pending = false;
        wake_fn = impl->wake;
        wake_user = impl->wake_user;
        ++impl->callbacks_inflight;
    } else {
        impl->runner_state = CFLOW_IO_RUNNER_IDLE;
        impl->wake_pending = false;
    }
    turbo_mutex_unlock(&impl->gate);
    if (wake_fn != NULL) {
        wake_fn(wake_user);
        turbo_mutex_lock(&impl->gate);
        --impl->callbacks_inflight;
        turbo_mutex_unlock(&impl->gate);
    }
}

int cflow_io_actor_init(cflow_io_actor *actor,
                        const cflow_io_actor_config *config) {
    cflow_io_actor_impl *impl;
    disruptor_config_t command_config;
    disruptor_config_t completion_config;
    uint64_t physical_command_capacity;
    uint64_t physical_completion_capacity;
    uint64_t next_sequence = 0u;
    uint64_t next_completion_sequence = 0u;
    size_t index;

    if (actor == NULL || actor->impl != NULL || config == NULL ||
        config->request_capacity == 0u || config->command_capacity == 0u ||
        config->executor == NULL || !cflow_executor_valid(config->executor) ||
        config->backend.submit == NULL || config->completion == NULL)
        return TURBO_EINVAL;
    if (config->request_capacity > SIZE_MAX / sizeof(cflow_io_request_slot))
        return TURBO_EINVAL;
    physical_command_capacity = io_round_up_power_of_two(
        config->command_capacity);
    physical_completion_capacity = io_round_up_power_of_two(
        config->request_capacity);
    if (physical_command_capacity == 0u ||
        physical_completion_capacity == 0u)
        return TURBO_EINVAL;

    impl = (cflow_io_actor_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->requests = (cflow_io_request_slot *)calloc(
        config->request_capacity, sizeof(*impl->requests));
    if (impl->requests == NULL) {
        free(impl);
        return TURBO_ENOMEM;
    }
    command_config = (disruptor_config_t){
        sizeof(cflow_io_command), physical_command_capacity, 1u,
        DISRUPTOR_MODE_BROADCAST};
    impl->commands = disruptor_create(&command_config);
    if (impl->commands == NULL ||
        !disruptor_consumer_try_register(
            impl->commands, &impl->command_consumer, &next_sequence)) {
        disruptor_destroy(impl->commands);
        free(impl->requests);
        free(impl);
        return TURBO_ENOMEM;
    }
    completion_config = (disruptor_config_t){
        sizeof(cflow_io_completion_event), physical_completion_capacity, 1u,
        DISRUPTOR_MODE_BROADCAST};
    impl->completions = disruptor_create(&completion_config);
    if (impl->completions == NULL ||
        !disruptor_consumer_try_register(
            impl->completions, &impl->completion_consumer,
            &next_completion_sequence)) {
        disruptor_destroy(impl->completions);
        disruptor_consumer_unregister(
            impl->commands, &impl->command_consumer);
        disruptor_destroy(impl->commands);
        free(impl->requests);
        free(impl);
        return TURBO_ENOMEM;
    }
    turbo_mutex_init(&impl->gate);
    if (impl->gate == NULL) {
        disruptor_consumer_unregister(
            impl->completions, &impl->completion_consumer);
        disruptor_destroy(impl->completions);
        disruptor_consumer_unregister(
            impl->commands, &impl->command_consumer);
        disruptor_destroy(impl->commands);
        free(impl->requests);
        free(impl);
        return TURBO_ENOMEM;
    }

    impl->backend_actor.impl = impl;
    impl->request_capacity = config->request_capacity;
    impl->command_capacity = config->command_capacity;
    impl->next_command_sequence = next_sequence;
    impl->next_completion_sequence = next_completion_sequence;
    atomic_init(&impl->completion_depth, 0u);
    atomic_init(&impl->completion_publishers_inflight, 0u);
    impl->executor = config->executor;
    if (!cflow_io_driver_init_completion_callbacks(
            &impl->driver, config->backend, config->backend_user)) {
        turbo_mutex_destroy(&impl->gate);
        disruptor_consumer_unregister(
            impl->completions, &impl->completion_consumer);
        disruptor_destroy(impl->completions);
        disruptor_consumer_unregister(
            impl->commands, &impl->command_consumer);
        disruptor_destroy(impl->commands);
        free(impl->requests);
        free(impl);
        return TURBO_EINVAL;
    }
    impl->completion = config->completion;
    impl->completion_user = config->completion_user;
    impl->wake = config->wake;
    impl->wake_user = config->wake_user;
    impl->next_request_id = 1u;
    impl->lifecycle = CFLOW_IO_RUNNING;
    for (index = 0u; index < impl->request_capacity; ++index) {
        impl->requests[index].owner = impl;
        atomic_init(&impl->requests[index].phase,
                    CFLOW_IO_REQUEST_FREE);
    }
    actor->impl = impl;
    return TURBO_OK;
}

cflow_io_submit_result cflow_io_actor_try_submit(
    cflow_io_actor *actor,
    cflow_io_lease_id lease_id,
    cflow_io_operation *operation) {
    cflow_io_actor_impl *impl = io_impl(actor);
    cflow_io_request_slot *slot;
    cflow_io_request_id request_id;
    cflow_io_submit_result result = {
        CFLOW_IO_SUBMIT_INVALID_ARGUMENT, 0u};

    if (impl == NULL || lease_id == 0u || operation == NULL ||
        operation->release == NULL)
        return result;
    turbo_mutex_lock(&impl->gate);
    if (impl->lifecycle != CFLOW_IO_RUNNING) {
        io_counter_increment(&impl->rejected_closed);
        result.status = CFLOW_IO_SUBMIT_CLOSED;
    } else if (io_lease_in_use_locked(impl, lease_id)) {
        io_counter_increment(&impl->rejected_lease_in_use);
        result.status = CFLOW_IO_SUBMIT_LEASE_IN_USE;
    } else if (impl->active_requests >= impl->request_capacity) {
        io_counter_increment(&impl->rejected_request_full);
        result.status = CFLOW_IO_SUBMIT_FULL;
    } else if (impl->command_depth >= impl->command_capacity) {
        io_counter_increment(&impl->rejected_command_full);
        result.status = CFLOW_IO_SUBMIT_FULL;
    } else if (impl->next_request_id == UINT64_MAX) {
        result.status = CFLOW_IO_SUBMIT_ID_EXHAUSTED;
    } else {
        slot = io_find_free_request_locked(impl);
        request_id = impl->next_request_id;
        if (slot == NULL || !io_publish_command_locked(
                impl, (cflow_io_command){
                    CFLOW_IO_COMMAND_SUBMIT, request_id})) {
            io_counter_increment(&impl->rejected_command_full);
            result.status = CFLOW_IO_SUBMIT_FULL;
        } else {
            slot->request_id = request_id;
            slot->lease_id = lease_id;
            io_phase_store_locked(slot, CFLOW_IO_REQUEST_ADMITTED);
            slot->operation = *operation;
            slot->completion = (cflow_io_completion){0};
            slot->cancel_requested = false;
            slot->cancel_dispatched = false;
            ++impl->next_request_id;
            ++impl->active_requests;
            io_counter_increment(&impl->accepted);
            *operation = (cflow_io_operation){0};
            result.status = CFLOW_IO_SUBMIT_ACCEPTED;
            result.request_id = request_id;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    if (result.status == CFLOW_IO_SUBMIT_ACCEPTED)
        io_notify(impl);
    return result;
}

cflow_io_cancel_status cflow_io_actor_try_cancel(
    cflow_io_actor *actor, cflow_io_request_id request_id) {
    cflow_io_actor_impl *impl = io_impl(actor);
    cflow_io_cancel_status result;
    if (impl == NULL || request_id == 0u)
        return CFLOW_IO_CANCEL_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->gate);
    if (io_find_request_locked(impl, request_id) == NULL) {
        result = CFLOW_IO_CANCEL_NOT_FOUND;
    } else if (impl->lifecycle != CFLOW_IO_RUNNING) {
        result = CFLOW_IO_CANCEL_CLOSED;
    } else if (impl->command_depth >= impl->command_capacity ||
               !io_publish_command_locked(
                   impl, (cflow_io_command){
                       CFLOW_IO_COMMAND_CANCEL, request_id})) {
        io_counter_increment(&impl->rejected_command_full);
        result = CFLOW_IO_CANCEL_FULL;
    } else {
        result = CFLOW_IO_CANCEL_ACCEPTED;
    }
    turbo_mutex_unlock(&impl->gate);
    if (result == CFLOW_IO_CANCEL_ACCEPTED)
        io_notify(impl);
    return result;
}

static cflow_io_complete_status io_publish_completion_entry(
    cflow_io_actor_impl *impl,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion) {
    cflow_io_request_slot *slot;
    disruptor_cursor_t cursor = {0};
    cflow_io_completion_event *entry;

    slot = io_reserve_completion_slot(impl, request_id);
    if (slot == NULL) {
        return io_completion_reservation_failure(impl, request_id);
    }

    disruptor_publisher_next_entry_blocking(impl->completions, &cursor);
    entry = (cflow_io_completion_event *)disruptor_acquire_entry(
        impl->completions, &cursor);
    entry->request_id = request_id;
    entry->slot_index = (size_t)(slot - impl->requests);
    entry->completion = *completion;
    disruptor_publisher_commit_entry_blocking(impl->completions, &cursor);
    (void)atomic_fetch_add_explicit(
        &impl->completion_depth, 1u, memory_order_release);
    return CFLOW_IO_COMPLETE_ACCEPTED;
}

cflow_io_complete_status cflow_io_actor_completion_batch_publish(
    cflow_io_completion_batch *batch,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion) {
    cflow_io_actor_impl *impl = io_impl(actor);

    if (batch == NULL || impl == NULL || request_id == 0u ||
        !io_completion_valid(completion))
        return CFLOW_IO_COMPLETE_INVALID_ARGUMENT;
    if (batch->impl != impl) {
        cflow_io_actor_completion_batch_end(batch);
        /* Keep Actor destruction out of the complete native publication
           scope until the gathered entries receive their coalesced wake. */
        (void)atomic_fetch_add_explicit(
            &impl->completion_publishers_inflight, 1u,
            memory_order_acq_rel);
        batch->impl = impl;
    }
    return io_publish_completion_entry(impl, request_id, completion);
}

void cflow_io_actor_completion_batch_end(
    cflow_io_completion_batch *batch) {
    cflow_io_actor_impl *impl;
    if (batch == NULL || batch->impl == NULL)
        return;
    impl = (cflow_io_actor_impl *)batch->impl;
    batch->impl = NULL;
    io_finish_completion_publication(impl);
}

cflow_io_complete_status cflow_io_actor_publish_completion(
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion) {
    cflow_io_actor_impl *impl = io_impl(actor);
    cflow_io_complete_status status;
    if (impl == NULL || request_id == 0u ||
        !io_completion_valid(completion))
        return CFLOW_IO_COMPLETE_INVALID_ARGUMENT;
    (void)atomic_fetch_add_explicit(
        &impl->completion_publishers_inflight, 1u, memory_order_acq_rel);
    status = io_publish_completion_entry(
        impl, request_id, completion);
    io_finish_completion_publication(impl);
    return status;
}

cflow_io_complete_status cflow_io_actor_complete(
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion) {
    return io_complete_inline(io_impl(actor), request_id, completion);
}

cflow_io_run_result cflow_io_actor_run_one(cflow_io_actor *actor) {
    cflow_io_actor_impl *impl = io_impl(actor);
    cflow_io_run_result result = {
        CFLOW_IO_RUN_INVALID_ARGUMENT, 0u};
    if (impl == NULL)
        return result;
    if (!io_driver_enter(impl)) {
        result.status = CFLOW_IO_RUN_BUSY;
        return result;
    }
    if (io_run_step(impl)) {
        result.status = CFLOW_IO_RUN_PROGRESSED;
        result.progressed = 1u;
    } else {
        result.status = CFLOW_IO_RUN_IDLE;
    }
    io_driver_leave(impl, result.progressed != 0u);
    return result;
}

cflow_io_run_result cflow_io_actor_run_ready(cflow_io_actor *actor,
                                             size_t max_steps) {
    cflow_io_actor_impl *impl = io_impl(actor);
    cflow_io_run_result result = {
        CFLOW_IO_RUN_INVALID_ARGUMENT, 0u};
    if (impl == NULL || max_steps == 0u)
        return result;
    if (!io_driver_enter(impl)) {
        result.status = CFLOW_IO_RUN_BUSY;
        return result;
    }
    while (result.progressed < max_steps) {
        cflow_io_action action = {
            CFLOW_IO_ACTION_NONE, NULL, 0u, 0u, NULL};
        bool selected;

        turbo_mutex_lock(&impl->gate);
        do {
            selected = io_select_action_locked(impl, &action);
            if (selected && action.kind == CFLOW_IO_ACTION_STATE_ONLY) {
                ++result.progressed;
                action = (cflow_io_action){
                    CFLOW_IO_ACTION_NONE, NULL, 0u, 0u, NULL};
            }
        } while (selected &&
                 result.progressed < max_steps &&
                 action.kind == CFLOW_IO_ACTION_NONE);
        if (!selected)
            impl->wake_pending = false;
        turbo_mutex_unlock(&impl->gate);

        if (!selected || result.progressed == max_steps)
            break;
        if (!io_execute_action(impl, &action))
            break;
        ++result.progressed;
    }
    result.status = result.progressed != 0u
                        ? CFLOW_IO_RUN_PROGRESSED : CFLOW_IO_RUN_IDLE;
    io_driver_leave(impl, result.progressed == max_steps);
    return result;
}

cflow_io_ack_status cflow_io_actor_acknowledge(
    cflow_io_actor *actor, cflow_io_request_id request_id) {
    cflow_io_actor_impl *impl = io_impl(actor);
    cflow_io_request_slot *slot;
    cflow_io_operation operation;
    if (impl == NULL || request_id == 0u)
        return CFLOW_IO_ACK_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->gate);
    slot = io_find_request_locked(impl, request_id);
    if (slot == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return CFLOW_IO_ACK_NOT_FOUND;
    }
    if (io_phase_load_locked(slot) != CFLOW_IO_REQUEST_DELIVERED) {
        turbo_mutex_unlock(&impl->gate);
        return CFLOW_IO_ACK_BUSY;
    }
    io_phase_store_locked(slot, CFLOW_IO_REQUEST_RELEASING);
    operation = slot->operation;
    turbo_mutex_unlock(&impl->gate);

    operation.release(operation.user);

    turbo_mutex_lock(&impl->gate);
    if (io_phase_load_locked(slot) == CFLOW_IO_REQUEST_RELEASING &&
        slot->request_id == request_id) {
        cflow_io_actor_impl *owner = slot->owner;
        memset(slot, 0, sizeof(*slot));
        slot->owner = owner;
        atomic_init(&slot->phase, CFLOW_IO_REQUEST_FREE);
        --impl->active_requests;
        io_counter_increment(&impl->acknowledged);
    }
    turbo_mutex_unlock(&impl->gate);
    io_notify(impl);
    return CFLOW_IO_ACK_RELEASED;
}

int cflow_io_actor_close(cflow_io_actor *actor) {
    cflow_io_actor_impl *impl = io_impl(actor);
    size_t index;
    if (impl == NULL)
        return TURBO_EINVAL;
    turbo_mutex_lock(&impl->gate);
    if (impl->lifecycle == CFLOW_IO_CLOSING) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EALREADY;
    }
    impl->lifecycle = CFLOW_IO_CLOSING;
    for (index = 0u; index < impl->request_capacity; ++index) {
        cflow_io_request_slot *slot = &impl->requests[index];
        const cflow_io_request_phase phase = io_phase_load_locked(slot);
        if (phase == CFLOW_IO_REQUEST_ADMITTED ||
            phase == CFLOW_IO_REQUEST_READY) {
            slot->completion = (cflow_io_completion){
                CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
            io_phase_store_locked(slot, CFLOW_IO_REQUEST_COMPLETED);
        } else if (phase == CFLOW_IO_REQUEST_BACKEND_PENDING) {
            slot->cancel_requested = true;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    io_notify(impl);
    return TURBO_OK;
}

bool cflow_io_actor_get_stats(const cflow_io_actor *actor,
                              cflow_io_actor_stats *out) {
    cflow_io_actor_impl *impl =
        (cflow_io_actor_impl *)io_const_impl(actor);
    size_t index;
    cflow_io_actor_stats snapshot = {0};
    if (impl == NULL || out == NULL)
        return false;
    turbo_mutex_lock(&impl->gate);
    snapshot.request_capacity = impl->request_capacity;
    snapshot.command_capacity = impl->command_capacity;
    snapshot.active_requests = impl->active_requests;
    snapshot.queued_commands = impl->command_depth;
    for (index = 0u; index < impl->request_capacity; ++index) {
        switch (io_phase_load_locked(&impl->requests[index])) {
            case CFLOW_IO_REQUEST_ADMITTED:
                ++snapshot.admitted;
                break;
            case CFLOW_IO_REQUEST_READY:
                ++snapshot.ready;
                break;
            case CFLOW_IO_REQUEST_BACKEND_PENDING:
                ++snapshot.backend_pending;
                break;
            case CFLOW_IO_REQUEST_COMPLETION_QUEUED:
            case CFLOW_IO_REQUEST_COMPLETED:
                ++snapshot.completions_ready;
                break;
            case CFLOW_IO_REQUEST_DISPATCH_QUEUED:
                ++snapshot.dispatch_queued;
                break;
            case CFLOW_IO_REQUEST_DISPATCH_RUNNING:
                ++snapshot.dispatch_running;
                break;
            case CFLOW_IO_REQUEST_DELIVERED:
            case CFLOW_IO_REQUEST_RELEASING:
                ++snapshot.delivered_unacknowledged;
                break;
            default:
                break;
        }
    }
    snapshot.accepted = impl->accepted;
    snapshot.acknowledged = impl->acknowledged;
    snapshot.rejected_request_full = impl->rejected_request_full;
    snapshot.rejected_command_full = impl->rejected_command_full;
    snapshot.rejected_closed = impl->rejected_closed;
    snapshot.rejected_lease_in_use = impl->rejected_lease_in_use;
    snapshot.stale_completions = impl->stale_completions;
    snapshot.backend_submit_errors = impl->backend_submit_errors;
    snapshot.backend_cancel_errors = impl->backend_cancel_errors;
    snapshot.executor_rejected_full = impl->executor_rejected_full;
    snapshot.executor_rejected_closed = impl->executor_rejected_closed;
    snapshot.executor_rejected_invalid = impl->executor_rejected_invalid;
    snapshot.lifecycle = impl->lifecycle;
    turbo_mutex_unlock(&impl->gate);
    *out = snapshot;
    return true;
}

bool cflow_io_actor_is_quiescent(const cflow_io_actor *actor) {
    cflow_io_actor_impl *impl =
        (cflow_io_actor_impl *)io_const_impl(actor);
    bool quiescent;
    if (impl == NULL)
        return false;
    turbo_mutex_lock(&impl->gate);
    quiescent = impl->lifecycle == CFLOW_IO_CLOSING &&
                 impl->command_depth == 0u &&
                 atomic_load_explicit(&impl->completion_depth,
                                      memory_order_acquire) == 0u &&
                 atomic_load_explicit(
                     &impl->completion_publishers_inflight,
                     memory_order_acquire) == 0u &&
                 impl->active_requests == 0u &&
                 impl->runner_state != CFLOW_IO_RUNNER_RUNNING &&
                 impl->callbacks_inflight == 0u;
    turbo_mutex_unlock(&impl->gate);
    return quiescent;
}

int cflow_io_actor_destroy(cflow_io_actor *actor) {
    cflow_io_actor_impl *impl = io_impl(actor);
    bool quiescent;
    if (impl == NULL)
        return TURBO_EINVAL;
    turbo_mutex_lock(&impl->gate);
    quiescent = impl->lifecycle == CFLOW_IO_CLOSING &&
                 impl->command_depth == 0u &&
                 atomic_load_explicit(&impl->completion_depth,
                                      memory_order_acquire) == 0u &&
                 atomic_load_explicit(
                     &impl->completion_publishers_inflight,
                     memory_order_acquire) == 0u &&
                 impl->active_requests == 0u &&
                 impl->runner_state != CFLOW_IO_RUNNER_RUNNING &&
                 impl->callbacks_inflight == 0u;
    if (quiescent)
        actor->impl = NULL;
    turbo_mutex_unlock(&impl->gate);
    if (!quiescent)
        return TURBO_EBUSY;
    impl->backend_actor.impl = NULL;
    disruptor_consumer_unregister(
        impl->completions, &impl->completion_consumer);
    disruptor_destroy(impl->completions);
    disruptor_consumer_unregister(
        impl->commands, &impl->command_consumer);
    disruptor_destroy(impl->commands);
    turbo_mutex_destroy(&impl->gate);
    free(impl->requests);
    free(impl);
    return TURBO_OK;
}
