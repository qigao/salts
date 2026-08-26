#include <cflow/clock.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>
#include "scheduler_internal.h"
#include "timer_queue.h"
#include <turbo/thread.h>

#include <stdlib.h>
#include <string.h>

typedef struct cflow_test_loop_state {
    turbo_mutex_t mutex;
    cflow_clock clock;
    cflow_executor executor;
    cflow_timer_queue timers;
    size_t ready_capacity;
    size_t timer_capacity;
    size_t peak_pending;
    size_t rejected_full;
    size_t rejected_closed;
    size_t cancelled_on_shutdown;
    bool stopping;
} cflow_test_loop_state;

static void test_update_peak_locked(cflow_test_loop_state *state) {
    size_t pending = cflow_timer_queue_pending(&state->timers);
    if (pending > state->peak_pending) state->peak_pending = pending;
}

static bool test_take_and_run_one(cflow_test_loop_state *state,
                                  bool advance_to_next) {
    cflow_timer_task task;
    cflow_executor_task descriptor;
    cflow_instant now;
    cflow_admission_status admitted;

    if (!state) return false;
    turbo_mutex_lock(&state->mutex);
    now = cflow_clock_now(&state->clock);
    if (advance_to_next) {
        cflow_deadline deadline;
        if (!cflow_timer_queue_next_deadline(&state->timers, &deadline)) {
            turbo_mutex_unlock(&state->mutex);
            return false;
        }
        if (deadline.ns > now.ns &&
            !cflow_clock_advance(&state->clock,
                                 cflow_deadline_remaining(deadline, now))) {
            turbo_mutex_unlock(&state->mutex);
            return false;
        }
        now = cflow_clock_now(&state->clock);
    }
    if (!cflow_timer_queue_take_ready(&state->timers, now, &task)) {
        turbo_mutex_unlock(&state->mutex);
        return false;
    }
    turbo_mutex_unlock(&state->mutex);
    descriptor = cflow_timer_task_descriptor(&task);
    admitted = cflow_executor_try_post_task(&state->executor, &descriptor);
    if (admitted != CFLOW_ADMISSION_ACCEPTED) {
        cflow_scheduler_settle_cancelled_task_internal(&descriptor);
        return false;
    }

    /* The task can re-enter the scheduler, so callbacks run without its lock. */
    return cflow_executor_run_one(&state->executor);
}

static cflow_schedule_result test_try_post_task_after(
    cflow_test_loop_state *state, uint64_t delay_ms,
    const cflow_executor_task *task) {
    cflow_instant now;
    cflow_deadline deadline;
    cflow_schedule_result result;

    if (!state || !task || !task->run)
        return (cflow_schedule_result){CFLOW_ADMISSION_INVALID_ARGUMENT, 0u};
    turbo_mutex_lock(&state->mutex);
    if (state->stopping) {
        ++state->rejected_closed;
        turbo_mutex_unlock(&state->mutex);
        return (cflow_schedule_result){CFLOW_ADMISSION_CLOSED, 0u};
    }
    now = cflow_clock_now(&state->clock);
    deadline = cflow_deadline_after(now, cflow_duration_from_ms(delay_ms));
    result = cflow_timer_queue_try_schedule_task(
        &state->timers, deadline, task);
    if (result.status == CFLOW_ADMISSION_ACCEPTED)
        test_update_peak_locked(state);
    else if (result.status == CFLOW_ADMISSION_FULL)
        ++state->rejected_full;
    turbo_mutex_unlock(&state->mutex);
    return result;
}

static cflow_schedule_result test_try_post_after(void *self,
                                                 uint64_t delay_ms,
                                                 cflow_task_fn fn,
                                                 void *user) {
    const cflow_executor_task task = {
        .run = fn,
        .cancel = NULL,
        .finalize = NULL,
        .user = user
    };
    return test_try_post_task_after(
        (cflow_test_loop_state *)self, delay_ms, &task);
}

static cflow_task_id test_post_after(void *self,
                                     uint64_t delay_ms,
                                     cflow_task_fn fn,
                                     void *user) {
    return test_try_post_after(self, delay_ms, fn, user).task_id;
}

static bool test_cancel(void *self, cflow_task_id id) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    cflow_timer_task timer_task = {0};
    cflow_executor_task task;
    bool cancelled;

    if (!state || id == 0u) return false;
    turbo_mutex_lock(&state->mutex);
    cancelled = !state->stopping &&
                cflow_timer_queue_take(&state->timers, id, &timer_task);
    turbo_mutex_unlock(&state->mutex);
    if (cancelled) {
        task = cflow_timer_task_descriptor(&timer_task);
        cflow_scheduler_settle_cancelled_task_internal(&task);
    }
    return cancelled;
}

static bool test_run_one(void *self) {
    return test_take_and_run_one((cflow_test_loop_state *)self, false);
}

static size_t test_run_ready(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    size_t count = 0u;
    while (test_take_and_run_one(state, false)) ++count;
    return count;
}

static size_t test_advance(void *self, uint64_t ticks_ms) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    bool advanced;

    if (!state) return 0u;
    turbo_mutex_lock(&state->mutex);
    advanced = cflow_clock_advance(&state->clock,
                                   cflow_duration_from_ms(ticks_ms));
    turbo_mutex_unlock(&state->mutex);
    if (!advanced) return 0u;
    return test_run_ready(state);
}

static size_t test_run_until_idle(void *self, size_t max_steps) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    size_t ran = 0u;

    if (!state) return 0u;
    while (max_steps == 0u || ran < max_steps) {
        if (!test_take_and_run_one(state, true)) break;
        ++ran;
    }
    return ran;
}

static bool test_wait_idle(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    bool timers_idle;

    if (!state) return false;
    (void)test_run_until_idle(state, 0u);
    turbo_mutex_lock(&state->mutex);
    timers_idle = cflow_timer_queue_pending(&state->timers) == 0u;
    turbo_mutex_unlock(&state->mutex);
    return timers_idle && cflow_executor_wait_idle(&state->executor);
}

static uint64_t test_now(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    uint64_t now;

    if (!state) return 0u;
    turbo_mutex_lock(&state->mutex);
    now = cflow_instant_to_ms(cflow_clock_now(&state->clock));
    turbo_mutex_unlock(&state->mutex);
    return now;
}

static size_t test_pending(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    size_t pending;

    if (!state) return 0u;
    turbo_mutex_lock(&state->mutex);
    pending = cflow_timer_queue_pending(&state->timers) +
              cflow_executor_pending(&state->executor);
    turbo_mutex_unlock(&state->mutex);
    return pending;
}

static bool test_shutdown(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    cflow_timer_task timer_task;
    bool should_shutdown = false;

    if (!state) return false;
    turbo_mutex_lock(&state->mutex);
    if (!state->stopping) {
        state->stopping = true;
        should_shutdown = true;
    }
    while (cflow_timer_queue_take_any(&state->timers, &timer_task)) {
        const cflow_executor_task task =
            cflow_timer_task_descriptor(&timer_task);
        ++state->cancelled_on_shutdown;
        turbo_mutex_unlock(&state->mutex);
        cflow_scheduler_settle_cancelled_task_internal(&task);
        turbo_mutex_lock(&state->mutex);
    }
    turbo_mutex_unlock(&state->mutex);
    return !should_shutdown || cflow_executor_shutdown(&state->executor);
}

static bool test_get_stats(void *self, cflow_scheduler_stats *out) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state || !out) return false;
    turbo_mutex_lock(&state->mutex);
    *out = (cflow_scheduler_stats){
        .ready_capacity = state->ready_capacity,
        .timer_capacity = state->timer_capacity,
        .ready_pending = cflow_executor_pending(&state->executor),
        .timer_pending = cflow_timer_queue_pending(&state->timers),
        .dispatching = 0u,
        .peak_pending = state->peak_pending,
        .rejected_full = state->rejected_full,
        .rejected_closed = state->rejected_closed,
        .cancelled_on_shutdown = state->cancelled_on_shutdown
    };
    turbo_mutex_unlock(&state->mutex);
    return true;
}

static void test_destroy(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state) return;
    (void)test_shutdown(state);
    cflow_timer_queue_destroy(&state->timers);
    cflow_executor_destroy(&state->executor);
    cflow_clock_destroy(&state->clock);
    turbo_mutex_destroy(&state->mutex);
    free(state);
}

CMETA_IMPLEMENTS(cflow_scheduler, test_loop,
    CMETA_SCHED_CAP_DELAYED | CMETA_SCHED_CAP_MANUAL_CLOCK,
    .try_post_after = test_try_post_after,
    .post_after = test_post_after,
    .cancel = test_cancel,
    .run_one = test_run_one,
    .run_ready = test_run_ready,
    .advance = test_advance,
    .run_until_idle = test_run_until_idle,
    .wait_idle = test_wait_idle,
    .now = test_now,
    .pending = test_pending,
    .shutdown = test_shutdown,
    .get_stats = test_get_stats,
    .destroy = test_destroy
);

bool cflow_scheduler_test_init(cflow_scheduler *scheduler) {
    return cflow_scheduler_test_init_with_capacity(
        scheduler, CFLOW_EXECUTOR_DEFAULT_CAPACITY,
        CFLOW_TIMER_DEFAULT_CAPACITY);
}

bool cflow_scheduler_test_init_with_capacity(cflow_scheduler *scheduler,
                                             size_t ready_capacity,
                                             size_t timer_capacity) {
    cflow_test_loop_state *state;

    if (!scheduler || scheduler->self || scheduler->vtable) return false;
    if (ready_capacity == 0u || timer_capacity == 0u) return false;
    state = (cflow_test_loop_state *)calloc(1, sizeof(*state));
    if (!state) return false;

    turbo_mutex_init(&state->mutex);
    if (!state->mutex ||
        !cflow_clock_virtual_init(&state->clock, (cflow_instant){0u}) ||
        !cflow_executor_manual_init_with_capacity(&state->executor,
                                                  ready_capacity) ||
        !cflow_timer_queue_init_with_capacity(&state->timers,
                                              timer_capacity)) {
        if (cflow_clock_valid(&state->clock)) cflow_clock_destroy(&state->clock);
        if (cflow_executor_valid(&state->executor))
            cflow_executor_destroy(&state->executor);
        cflow_timer_queue_destroy(&state->timers);
        turbo_mutex_destroy(&state->mutex);
        free(state);
        return false;
    }

    state->ready_capacity = ready_capacity;
    state->timer_capacity = timer_capacity;
    *scheduler = test_loop_as_cflow_scheduler(state);
    return true;
}

cflow_task_id cflow_scheduler_post(cflow_scheduler *scheduler,
                                   cflow_task_fn fn,
                                   void *user) {
    return cflow_scheduler_post_after(scheduler, 0u, fn, user);
}

const char *cflow_scheduler_name(const cflow_scheduler *scheduler) {
    return cflow_scheduler_implementation(scheduler);
}

bool cflow_scheduler_try_post_task_after_internal(
    cflow_scheduler *scheduler, uint64_t delay_ms,
    const cflow_executor_task *task, cflow_schedule_result *out) {
    if (!scheduler || !task || !task->run || !out) return false;
    if (scheduler->vtable == &test_loop_vtable) {
        *out = test_try_post_task_after(
            (cflow_test_loop_state *)scheduler->self, delay_ms, task);
        return true;
    }
    return cflow_scheduler_worker_try_post_task_after_internal(
        scheduler, delay_ms, task, out);
}
