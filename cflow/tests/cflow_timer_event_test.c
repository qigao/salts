#include <cflow/clock.h>
#include <cflow/machine_runtime.h>
#include <cflow/timer_event.h>
#include <turbo/thread.h>

#include "tinytest.h"
#include "timer_event_internal.h"

#include <stdatomic.h>

typedef struct timer_event_fixture {
    cflow_clock clock;
    cflow_executor executor;
    cflow_machine machine;
    cflow_machine_instance instance;
    cflow_resumable resumable;
    cflow_timer_event_queue timers;
} timer_event_fixture;

static void timer_event_resumable_destroy(cflow_resumable *resumable) {
    if (resumable == NULL) return;
    if (resumable->ops != NULL && resumable->ops->destroy != NULL)
        resumable->ops->destroy(resumable->state);
    *resumable = (cflow_resumable){0};
}

static bool timer_event_fixture_init(timer_event_fixture *fixture,
                                     size_t timer_capacity,
                                     size_t mailbox_capacity,
                                     cflow_instant start,
                                     bool arm_runtime) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {100u, &cmeta_type_bool}
    };
    const cflow_machine_transition transitions[] = {
        {10u, 100u, 0u, 0u, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u,
        events, 1u,
        NULL, 0u,
        NULL, 0u,
        transitions, 1u
    };
    const int initial_state = 7;
    cflow_machine_instance_config machine_config;
    cflow_timer_event_queue_config timer_config;
    cflow_resume_ctx resume_context = {NULL};
    cflow_step step;
    int output = 0;

    if (fixture == NULL) return false;
    *fixture = (timer_event_fixture){0};
    if (cflow_machine_build(&fixture->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_serial_init(&fixture->executor) ||
        !cflow_clock_virtual_init(&fixture->clock, start))
        return false;

    machine_config = (cflow_machine_instance_config){
        &fixture->machine, &initial_state, &cmeta_type_int,
        NULL, 0u, NULL, 0u, mailbox_capacity, &fixture->executor
    };
    if (cflow_machine_instance_init(&fixture->instance, &machine_config) !=
        CFLOW_MACHINE_RUNTIME_OK)
        return false;
    if (arm_runtime) {
        if (!cflow_machine_instance_as_resumable(
                &fixture->instance, &fixture->resumable))
            return false;
        step = fixture->resumable.ops->resume(
            fixture->resumable.state, &resume_context, &output);
        if (step.kind != CFLOW_STEP_WAIT) return false;
    }

    timer_config = (cflow_timer_event_queue_config){
        &fixture->clock, &fixture->instance, timer_capacity
    };
    return cflow_timer_event_queue_init(&fixture->timers, &timer_config) ==
           CFLOW_TIMER_EVENT_OK;
}

static cflow_event_view timer_bool_event(const bool *payload) {
    return (cflow_event_view){100u, &cmeta_type_bool, payload};
}

static void check_timer_accounting(const cflow_timer_event_stats *stats) {
    check_equal(stats->scheduled,
                (uint64_t)stats->pending + (uint64_t)stats->in_flight +
                stats->delivered + stats->cancelled +
                stats->mailbox_rejected);
    check(stats->pending + stats->in_flight <= stats->capacity);
}

typedef struct timer_close_context {
    cflow_timer_event_queue *queue;
    atomic_int started;
    atomic_int finished;
    cflow_timer_event_status status;
} timer_close_context;

static void close_timer_queue(void *user) {
    timer_close_context *context = (timer_close_context *)user;
    atomic_store(&context->started, 1);
    context->status = cflow_timer_event_queue_close(context->queue);
    atomic_store(&context->finished, 1);
}

static void timer_event_fixture_destroy(timer_event_fixture *fixture) {
    if (fixture == NULL) return;
    (void)cflow_timer_event_queue_close(&fixture->timers);
    cflow_timer_event_queue_destroy(&fixture->timers);
    cflow_machine_instance_cancel(&fixture->instance);
    (void)cflow_executor_wait_idle(&fixture->executor);
    timer_event_resumable_destroy(&fixture->resumable);
    cflow_machine_instance_destroy(&fixture->instance);
    cflow_clock_destroy(&fixture->clock);
    cflow_executor_destroy(&fixture->executor);
    cflow_machine_destroy(&fixture->machine);
}

suite("CFlow monotonic Timer Events") {
    it("fires at the exact VirtualClock boundary through Machine Mailbox") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = {
            100u, &cmeta_type_bool, &payload
        };
        cflow_timer_event_schedule_result scheduled;
        cflow_timer_event_fire_result fired;
        cflow_machine_instance_stats machine_stats = {0};

        check_true(timer_event_fixture_init(
            &fixture, 2u, 2u, (cflow_instant){100u}, true));
        scheduled = cflow_timer_event_queue_try_schedule_after(
            &fixture.timers, (cflow_duration){10u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_OK);
        check(scheduled.timer_id != 0u);

        check_true(cflow_clock_advance(
            &fixture.clock, (cflow_duration){9u}));
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_NOT_READY);
        check_equal(fired.timer_id, (cflow_timer_event_id)0u);

        check_true(cflow_clock_advance(
            &fixture.clock, (cflow_duration){1u}));
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_DELIVERED);
        check_equal(fired.timer_id, scheduled.timer_id);
        check_equal(fired.mailbox_status, CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_true(cflow_machine_instance_get_stats(
            &fixture.instance, &machine_stats));
        check_equal(machine_stats.accepted, (uint64_t)1u);
        check_equal(machine_stats.completed, (uint64_t)1u);

        timer_event_fixture_destroy(&fixture);
    }

    it("bounds Timer storage, validates schema, and reuses cancelled slots") {
        timer_event_fixture fixture;
        const bool payload = true;
        const int wrong_payload = 1;
        const cflow_event_view valid = timer_bool_event(&payload);
        const cflow_event_view unknown = {
            999u, &cmeta_type_bool, &payload
        };
        const cflow_event_view mismatch = {
            100u, &cmeta_type_int, &wrong_payload
        };
        cflow_timer_event_schedule_result first;
        cflow_timer_event_schedule_result result;
        cflow_timer_event_stats stats = {0};

        check_true(timer_event_fixture_init(
            &fixture, 1u, 2u, (cflow_instant){0u}, false));
        result = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){1u}, &unknown);
        check_equal(result.status, CFLOW_TIMER_EVENT_INVALID_ARGUMENT);
        result = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){1u}, &mismatch);
        check_equal(result.status, CFLOW_TIMER_EVENT_TYPE_MISMATCH);

        first = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){1u}, &valid);
        check_equal(first.status, CFLOW_TIMER_EVENT_OK);
        result = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){2u}, &valid);
        check_equal(result.status, CFLOW_TIMER_EVENT_FULL);
        check_equal(cflow_timer_event_queue_cancel(
            &fixture.timers, first.timer_id), CFLOW_TIMER_EVENT_OK);
        result = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){2u}, &valid);
        check_equal(result.status, CFLOW_TIMER_EVENT_OK);

        check_true(cflow_timer_event_queue_get_stats(&fixture.timers, &stats));
        check_equal(stats.capacity, (size_t)1u);
        check_equal(stats.pending, (size_t)1u);
        check_equal(stats.peak_pending, (size_t)1u);
        check_equal(stats.scheduled, (uint64_t)2u);
        check_equal(stats.cancelled, (uint64_t)1u);
        check_equal(stats.rejected_full, (uint64_t)1u);
        check(stats.reserved_payload_bytes >= sizeof(payload));
        check(stats.reserved_bytes > stats.reserved_payload_bytes);
        check_timer_accounting(&stats);

        timer_event_fixture_destroy(&fixture);
    }

    it("saturates relative deadlines and preserves equal-deadline FIFO") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = timer_bool_event(&payload);
        cflow_timer_event_schedule_result first;
        cflow_timer_event_schedule_result second;
        cflow_timer_event_fire_result fired;

        check_true(timer_event_fixture_init(
            &fixture, 2u, 2u, (cflow_instant){UINT64_MAX - 5u}, false));
        first = cflow_timer_event_queue_try_schedule_after(
            &fixture.timers, (cflow_duration){10u}, &event);
        second = cflow_timer_event_queue_try_schedule_after(
            &fixture.timers, (cflow_duration){10u}, &event);
        check_equal(first.status, CFLOW_TIMER_EVENT_OK);
        check_equal(second.status, CFLOW_TIMER_EVENT_OK);

        check_true(cflow_clock_advance(
            &fixture.clock, (cflow_duration){4u}));
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_NOT_READY);
        check_true(cflow_clock_advance(
            &fixture.clock, (cflow_duration){1u}));
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_DELIVERED);
        check_equal(fired.timer_id, first.timer_id);
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_DELIVERED);
        check_equal(fired.timer_id, second.timer_id);
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_NOT_READY);

        timer_event_fixture_destroy(&fixture);
    }

    it("cancels before fire without emitting a stale Event") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = timer_bool_event(&payload);
        cflow_timer_event_schedule_result scheduled;
        cflow_timer_event_fire_result fired;
        cflow_machine_instance_stats machine_stats = {0};
        cflow_timer_event_stats timer_stats = {0};

        check_true(timer_event_fixture_init(
            &fixture, 1u, 1u, (cflow_instant){0u}, false));
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){0u}, &event);
        check_equal(cflow_timer_event_queue_cancel(
            &fixture.timers, scheduled.timer_id), CFLOW_TIMER_EVENT_OK);
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_NOT_READY);
        check_equal(cflow_timer_event_queue_cancel(
            &fixture.timers, scheduled.timer_id), CFLOW_TIMER_EVENT_NOT_FOUND);
        check_true(cflow_machine_instance_get_stats(
            &fixture.instance, &machine_stats));
        check_equal(machine_stats.accepted, (uint64_t)0u);
        check_true(cflow_timer_event_queue_get_stats(
            &fixture.timers, &timer_stats));
        check_timer_accounting(&timer_stats);

        timer_event_fixture_destroy(&fixture);
    }

    it("makes a claimed fire beat cancellation exactly once") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = timer_bool_event(&payload);
        cflow_timer_event_schedule_result scheduled;
        cflow_timer_event_claim claim = {0};
        cflow_timer_event_fire_result fired;
        cflow_timer_event_stats stats = {0};

        check_true(timer_event_fixture_init(
            &fixture, 1u, 1u, (cflow_instant){0u}, false));
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){0u}, &event);
        check_true(cflow_timer_event_queue_claim_one_ready(
            &fixture.timers, &claim, &fired));
        check_equal(cflow_timer_event_queue_cancel(
            &fixture.timers, scheduled.timer_id), CFLOW_TIMER_EVENT_FIRE_WON);
        fired = cflow_timer_event_queue_commit_claim(&claim);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_DELIVERED);
        check_equal(fired.timer_id, scheduled.timer_id);
        check_equal(cflow_timer_event_queue_commit_claim(&claim).status,
                    CFLOW_TIMER_EVENT_FIRE_INVALID_ARGUMENT);
        check_true(cflow_timer_event_queue_get_stats(&fixture.timers, &stats));
        check_equal(stats.delivered, (uint64_t)1u);
        check_equal(stats.cancelled, (uint64_t)0u);
        check_timer_accounting(&stats);

        timer_event_fixture_destroy(&fixture);
    }

    it("reports full Mailbox separately and never retries") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = timer_bool_event(&payload);
        cflow_timer_event_schedule_result scheduled;
        cflow_timer_event_fire_result fired;
        cflow_timer_event_stats stats = {0};
        cflow_machine_instance_stats machine_stats = {0};

        check_true(timer_event_fixture_init(
            &fixture, 2u, 1u, (cflow_instant){0u}, false));
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){0u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_OK);
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){0u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_OK);
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_DELIVERED);
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_MAILBOX_REJECTED);
        check_equal(fired.mailbox_status, CFLOW_MAILBOX_FULL);
        check_equal(cflow_timer_event_queue_run_one_ready(&fixture.timers).status,
                    CFLOW_TIMER_EVENT_FIRE_NOT_READY);

        check_true(cflow_timer_event_queue_get_stats(&fixture.timers, &stats));
        check_equal(stats.delivered, (uint64_t)1u);
        check_equal(stats.mailbox_rejected, (uint64_t)1u);
        check_equal(stats.mailbox_rejected_full, (uint64_t)1u);
        check_timer_accounting(&stats);
        check_true(cflow_machine_instance_get_stats(
            &fixture.instance, &machine_stats));
        check_equal(machine_stats.accepted, (uint64_t)1u);
        check_equal(machine_stats.pending, (size_t)1u);

        timer_event_fixture_destroy(&fixture);
    }

    it("close cancels pending timers and rejects later admission") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = timer_bool_event(&payload);
        cflow_timer_event_schedule_result scheduled;
        cflow_timer_event_fire_result fired;
        cflow_timer_event_stats stats = {0};

        check_true(timer_event_fixture_init(
            &fixture, 2u, 2u, (cflow_instant){0u}, false));
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){1u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_OK);
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){2u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_OK);
        check_equal(cflow_timer_event_queue_close(&fixture.timers),
                    CFLOW_TIMER_EVENT_OK);
        check_equal(cflow_timer_event_queue_close(&fixture.timers),
                    CFLOW_TIMER_EVENT_CLOSED);
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){0u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_CLOSED);
        fired = cflow_timer_event_queue_run_one_ready(&fixture.timers);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_CLOSED);
        check_true(cflow_timer_event_queue_get_stats(&fixture.timers, &stats));
        check_true(stats.closed);
        check_equal(stats.cancelled, (uint64_t)2u);
        check_equal(stats.cancelled_on_close, (uint64_t)2u);
        check_equal(stats.rejected_closed, (uint64_t)1u);
        check_timer_accounting(&stats);

        timer_event_fixture_destroy(&fixture);
    }

    it("close waits for an already claimed handoff") {
        timer_event_fixture fixture;
        const bool payload = true;
        const cflow_event_view event = timer_bool_event(&payload);
        cflow_timer_event_schedule_result scheduled;
        cflow_timer_event_claim claim = {0};
        cflow_timer_event_fire_result fired;
        timer_close_context close_context = {&fixture.timers};
        turbo_thread_t close_thread = 0;
        cflow_timer_event_stats stats = {0};
        size_t attempts = 0u;

        atomic_init(&close_context.started, 0);
        atomic_init(&close_context.finished, 0);
        check_true(timer_event_fixture_init(
            &fixture, 1u, 1u, (cflow_instant){0u}, false));
        scheduled = cflow_timer_event_queue_try_schedule_at(
            &fixture.timers, (cflow_deadline){0u}, &event);
        check_equal(scheduled.status, CFLOW_TIMER_EVENT_OK);
        check_true(cflow_timer_event_queue_claim_one_ready(
            &fixture.timers, &claim, &fired));
        check_equal(turbo_thread_create(
            &close_thread, close_timer_queue, &close_context), 0);
        do {
            check_true(cflow_timer_event_queue_get_stats(
                &fixture.timers, &stats));
            if (stats.closed) break;
            turbo_thread_yield();
        } while (++attempts < 100000u);
        check_true(stats.closed);
        check_equal(atomic_load(&close_context.started), 1);
        check_equal(atomic_load(&close_context.finished), 0);

        fired = cflow_timer_event_queue_commit_claim(&claim);
        check_equal(fired.status, CFLOW_TIMER_EVENT_FIRE_DELIVERED);
        check_equal(turbo_thread_join(&close_thread), 0);
        check_equal(atomic_load(&close_context.finished), 1);
        check_equal(close_context.status, CFLOW_TIMER_EVENT_OK);
        check_true(cflow_timer_event_queue_get_stats(&fixture.timers, &stats));
        check_equal(stats.in_flight, (size_t)0u);
        check_equal(stats.delivered, (uint64_t)1u);
        check_timer_accounting(&stats);

        timer_event_fixture_destroy(&fixture);
    }
}
