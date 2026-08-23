#include <cflow/clock.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>
#include "timer_queue.h"
#include <turbo/thread.h>

#include <stdlib.h>
#include <string.h>

typedef struct worker_state {
    turbo_mutex_t mutex;
    turbo_cond_t changed;
    turbo_thread_t timer_thread;
    cflow_clock clock;
    cflow_executor executor;
    cflow_timer_queue timers;
    size_t dispatching;
    bool stopping;
} worker_state;

static void worker_timer_main(void *user) {
    worker_state *state = (worker_state *)user;

    if (!state) return;
    turbo_mutex_lock(&state->mutex);
    for (;;) {
        cflow_deadline deadline;
        cflow_instant now;
        cflow_timer_task task;

        if (state->stopping) break;
        if (!cflow_timer_queue_next_deadline(&state->timers, &deadline)) {
            turbo_cond_wait(&state->changed, &state->mutex);
            continue;
        }

        now = cflow_clock_now(&state->clock);
        if (deadline.ns > now.ns) {
            cflow_duration remaining = cflow_deadline_remaining(deadline, now);
            (void)turbo_cond_timedwait(&state->changed, &state->mutex,
                                       remaining.ns);
            continue;
        }

        if (!cflow_timer_queue_take_ready(&state->timers, now, &task))
            continue;

        ++state->dispatching;
        turbo_cond_broadcast(&state->changed);
        turbo_mutex_unlock(&state->mutex);

        (void)cflow_executor_post(&state->executor, task.fn, task.user);

        turbo_mutex_lock(&state->mutex);
        --state->dispatching;
        turbo_cond_broadcast(&state->changed);
    }
    turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->mutex);
}

static cflow_schedule_result worker_try_post_after(void *self,
                                                   uint64_t delay_ms,
                                                   cflow_task_fn fn,
                                                   void *user) {
    worker_state *state = (worker_state *)self;
    cflow_instant now;
    cflow_deadline deadline;
    cflow_schedule_result result;

    if (!state || !fn)
        return (cflow_schedule_result){CFLOW_ADMISSION_INVALID_ARGUMENT, 0u};
    turbo_mutex_lock(&state->mutex);
    if (state->stopping) {
        turbo_mutex_unlock(&state->mutex);
        return (cflow_schedule_result){CFLOW_ADMISSION_CLOSED, 0u};
    }

    now = cflow_clock_now(&state->clock);
    deadline = cflow_deadline_after(now, cflow_duration_from_ms(delay_ms));
    result = cflow_timer_queue_try_schedule(&state->timers, deadline, fn, user);
    if (result.status == CFLOW_ADMISSION_ACCEPTED)
        turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->mutex);
    return result;
}

static cflow_task_id worker_post_after(void *self,
                                       uint64_t delay_ms,
                                       cflow_task_fn fn,
                                       void *user) {
    return worker_try_post_after(self, delay_ms, fn, user).task_id;
}

static bool worker_cancel(void *self, cflow_task_id id) {
    worker_state *state = (worker_state *)self;
    bool cancelled;

    if (!state || id == 0u) return false;
    turbo_mutex_lock(&state->mutex);
    cancelled = !state->stopping &&
                cflow_timer_queue_cancel(&state->timers, id);
    if (cancelled) turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->mutex);
    return cancelled;
}

static bool worker_run_one(void *self) {
    (void)self;
    return false;
}

static size_t worker_run_ready(void *self) {
    (void)self;
    return 0u;
}

static size_t worker_advance(void *self, uint64_t ticks) {
    (void)self;
    (void)ticks;
    return 0u;
}

static size_t worker_run_until_idle(void *self, size_t max_steps) {
    (void)self;
    (void)max_steps;
    return 0u;
}

static bool worker_wait_idle(void *self) {
    worker_state *state = (worker_state *)self;

    if (!state) return false;
    for (;;) {
        bool stopping;
        bool delayed_idle;

        turbo_mutex_lock(&state->mutex);
        while (!state->stopping &&
               (cflow_timer_queue_pending(&state->timers) != 0u ||
                state->dispatching != 0u)) {
            turbo_cond_wait(&state->changed, &state->mutex);
        }
        stopping = state->stopping;
        delayed_idle = cflow_timer_queue_pending(&state->timers) == 0u &&
                       state->dispatching == 0u;
        turbo_mutex_unlock(&state->mutex);

        if (stopping) return false;
        if (!delayed_idle || !cflow_executor_wait_idle(&state->executor))
            return false;

        turbo_mutex_lock(&state->mutex);
        delayed_idle = !state->stopping &&
                       cflow_timer_queue_pending(&state->timers) == 0u &&
                       state->dispatching == 0u;
        turbo_mutex_unlock(&state->mutex);
        if (delayed_idle && cflow_executor_pending(&state->executor) == 0u)
            return true;
    }
}

static uint64_t worker_now(void *self) {
    worker_state *state = (worker_state *)self;
    return state ? cflow_instant_to_ms(cflow_clock_now(&state->clock)) : 0u;
}

static size_t worker_pending(void *self) {
    worker_state *state = (worker_state *)self;
    size_t delayed;

    if (!state) return 0u;
    turbo_mutex_lock(&state->mutex);
    delayed = cflow_timer_queue_pending(&state->timers) + state->dispatching;
    turbo_mutex_unlock(&state->mutex);
    return delayed + cflow_executor_pending(&state->executor);
}

static void worker_destroy(void *self) {
    worker_state *state = (worker_state *)self;

    if (!state) return;
    turbo_mutex_lock(&state->mutex);
    state->stopping = true;
    turbo_cond_broadcast(&state->changed);
    turbo_mutex_unlock(&state->mutex);

    if (state->timer_thread) (void)turbo_thread_join(&state->timer_thread);
    cflow_timer_queue_destroy(&state->timers);
    cflow_executor_destroy(&state->executor);
    cflow_clock_destroy(&state->clock);
    turbo_cond_destroy(&state->changed);
    turbo_mutex_destroy(&state->mutex);
    free(state);
}

CMETA_IMPLEMENTS(cflow_scheduler, worker_scheduler,
    CMETA_SCHED_CAP_DELAYED | CMETA_SCHED_CAP_CONCURRENT,
    .try_post_after = worker_try_post_after,
    .post_after = worker_post_after,
    .cancel = worker_cancel,
    .run_one = worker_run_one,
    .run_ready = worker_run_ready,
    .advance = worker_advance,
    .run_until_idle = worker_run_until_idle,
    .wait_idle = worker_wait_idle,
    .now = worker_now,
    .pending = worker_pending,
    .destroy = worker_destroy
);

bool cflow_scheduler_worker_init(cflow_scheduler *scheduler, size_t workers) {
    return cflow_scheduler_worker_init_with_capacity(
        scheduler, workers, CFLOW_EXECUTOR_DEFAULT_CAPACITY,
        CFLOW_TIMER_DEFAULT_CAPACITY);
}

bool cflow_scheduler_worker_init_with_capacity(cflow_scheduler *scheduler,
                                               size_t workers,
                                               size_t ready_capacity,
                                               size_t timer_capacity) {
    worker_state *state;

    if (!scheduler) return false;
    memset(scheduler, 0, sizeof(*scheduler));
    if (workers == 0u || ready_capacity == 0u || timer_capacity == 0u)
        return false;
    state = (worker_state *)calloc(1, sizeof(*state));
    if (!state) return false;

    turbo_mutex_init(&state->mutex);
    turbo_cond_init(&state->changed);
    if (!state->mutex || !state->changed ||
        !cflow_clock_system_init(&state->clock) ||
        !cflow_executor_worker_init_with_capacity(&state->executor, workers,
                                                  ready_capacity) ||
        !cflow_timer_queue_init_with_capacity(&state->timers,
                                              timer_capacity) ||
        turbo_thread_create(&state->timer_thread, worker_timer_main, state) != 0) {
        if (state->timer_thread) (void)turbo_thread_join(&state->timer_thread);
        cflow_timer_queue_destroy(&state->timers);
        if (cflow_executor_valid(&state->executor))
            cflow_executor_destroy(&state->executor);
        if (cflow_clock_valid(&state->clock)) cflow_clock_destroy(&state->clock);
        turbo_cond_destroy(&state->changed);
        turbo_mutex_destroy(&state->mutex);
        free(state);
        return false;
    }

    *scheduler = worker_scheduler_as_cflow_scheduler(state);
    return true;
}
