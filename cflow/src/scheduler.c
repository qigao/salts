#include <cflow/clock.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>
#include "timer_queue.h"

#include <stdlib.h>
#include <string.h>

typedef struct cflow_test_loop_state {
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

static void test_update_peak(cflow_test_loop_state *state) {
    size_t pending = cflow_timer_queue_pending(&state->timers) +
                     cflow_executor_pending(&state->executor);
    if (pending > state->peak_pending) state->peak_pending = pending;
}

static bool test_take_and_run_one(cflow_test_loop_state *state) {
    cflow_timer_task task;
    cflow_instant now;

    if (!state) return false;
    now = cflow_clock_now(&state->clock);
    if (!cflow_timer_queue_take_ready(&state->timers, now, &task)) return false;
    if (!cflow_executor_post(&state->executor, task.fn, task.user)) return false;
    return cflow_executor_run_one(&state->executor);
}

static cflow_schedule_result test_try_post_after(void *self,
                                                 uint64_t delay_ms,
                                                 cflow_task_fn fn,
                                                 void *user) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    cflow_instant now;
    cflow_deadline deadline;

    if (!state || !fn)
        return (cflow_schedule_result){CFLOW_ADMISSION_INVALID_ARGUMENT, 0u};
    if (state->stopping) {
        ++state->rejected_closed;
        return (cflow_schedule_result){CFLOW_ADMISSION_CLOSED, 0u};
    }
    now = cflow_clock_now(&state->clock);
    deadline = cflow_deadline_after(now, cflow_duration_from_ms(delay_ms));
    {
        cflow_schedule_result result = cflow_timer_queue_try_schedule(
            &state->timers, deadline, fn, user);
        if (result.status == CFLOW_ADMISSION_ACCEPTED)
            test_update_peak(state);
        else if (result.status == CFLOW_ADMISSION_FULL)
            ++state->rejected_full;
        return result;
    }
}

static cflow_task_id test_post_after(void *self,
                                     uint64_t delay_ms,
                                     cflow_task_fn fn,
                                     void *user) {
    return test_try_post_after(self, delay_ms, fn, user).task_id;
}

static bool test_cancel(void *self, cflow_task_id id) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    return state && cflow_timer_queue_cancel(&state->timers, id);
}

static bool test_run_one(void *self) {
    return test_take_and_run_one((cflow_test_loop_state *)self);
}

static size_t test_run_ready(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    size_t count = 0u;
    while (test_take_and_run_one(state)) ++count;
    return count;
}

static size_t test_advance(void *self, uint64_t ticks_ms) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state || !cflow_clock_advance(&state->clock,
                                       cflow_duration_from_ms(ticks_ms)))
        return 0u;
    return test_run_ready(state);
}

static size_t test_run_until_idle(void *self, size_t max_steps) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    size_t ran = 0u;

    if (!state) return 0u;
    while (cflow_timer_queue_pending(&state->timers) != 0u &&
           (max_steps == 0u || ran < max_steps)) {
        cflow_deadline deadline;
        cflow_instant now = cflow_clock_now(&state->clock);

        if (!cflow_timer_queue_next_deadline(&state->timers, &deadline)) break;
        if (deadline.ns > now.ns) {
            if (!cflow_clock_advance(
                    &state->clock,
                    cflow_deadline_remaining(deadline, now)))
                break;
        }
        if (!test_take_and_run_one(state)) break;
        ++ran;
    }
    return ran;
}

static bool test_wait_idle(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state) return false;
    (void)test_run_until_idle(state, 0u);
    return cflow_timer_queue_pending(&state->timers) == 0u &&
           cflow_executor_wait_idle(&state->executor);
}

static uint64_t test_now(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    return state ? cflow_instant_to_ms(cflow_clock_now(&state->clock)) : 0u;
}

static size_t test_pending(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state) return 0u;
    return cflow_timer_queue_pending(&state->timers) +
           cflow_executor_pending(&state->executor);
}

static bool test_shutdown(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state) return false;
    if (state->stopping) return true;
    state->stopping = true;
    state->cancelled_on_shutdown += cflow_timer_queue_pending(&state->timers);
    state->timers.count = 0u;
    return cflow_executor_shutdown(&state->executor);
}

static bool test_get_stats(void *self, cflow_scheduler_stats *out) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state || !out) return false;
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
    return true;
}

static void test_destroy(void *self) {
    cflow_test_loop_state *state = (cflow_test_loop_state *)self;
    if (!state) return;
    (void)test_shutdown(state);
    cflow_timer_queue_destroy(&state->timers);
    cflow_executor_destroy(&state->executor);
    cflow_clock_destroy(&state->clock);
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

    if (!scheduler) return false;
    memset(scheduler, 0, sizeof(*scheduler));
    if (ready_capacity == 0u || timer_capacity == 0u) return false;
    state = (cflow_test_loop_state *)calloc(1, sizeof(*state));
    if (!state) return false;

    if (!cflow_clock_virtual_init(&state->clock, (cflow_instant){0u}) ||
        !cflow_executor_manual_init_with_capacity(&state->executor,
                                                  ready_capacity) ||
        !cflow_timer_queue_init_with_capacity(&state->timers,
                                              timer_capacity)) {
        if (cflow_clock_valid(&state->clock)) cflow_clock_destroy(&state->clock);
        if (cflow_executor_valid(&state->executor))
            cflow_executor_destroy(&state->executor);
        cflow_timer_queue_destroy(&state->timers);
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
