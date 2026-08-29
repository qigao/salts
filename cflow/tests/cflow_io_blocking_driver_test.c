#include <cflow/io_blocking_driver.h>

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include "tinytest.h"

#include <stdatomic.h>
#include <string.h>

enum { BLOCKING_TEST_SPIN_LIMIT = 2000 };

typedef struct blocking_job_probe {
    cflow_io_blocking_job job;
    atomic_bool started;
    atomic_bool release_gate;
    atomic_int run_count;
    atomic_int release_count;
    cflow_io_completion result;
} blocking_job_probe;

typedef struct blocking_completion_probe {
    cflow_io_request_id request_ids[4];
    cflow_io_completion values[4];
    size_t count;
} blocking_completion_probe;

typedef struct blocking_fixture {
    cflow_io_blocking_driver driver;
    cflow_executor delivery_executor;
    cflow_io_actor actor;
    blocking_completion_probe completions;
} blocking_fixture;

static int blocking_job_execute(void *user, cflow_io_completion *out) {
    blocking_job_probe *probe = (blocking_job_probe *)user;
    atomic_store(&probe->started, true);
    while (!atomic_load(&probe->release_gate))
        turbo_thread_yield();
    atomic_fetch_add(&probe->run_count, 1);
    *out = probe->result;
    return TURBO_OK;
}

static void blocking_operation_release(void *operation_user) {
    blocking_job_probe *probe = (blocking_job_probe *)operation_user;
    atomic_fetch_add(&probe->release_count, 1);
}

static void blocking_completion_record(
    void *user, cflow_io_request_id request_id, cflow_io_lease_id lease_id,
    void *operation_user, const cflow_io_completion *completion) {
    blocking_completion_probe *probe = (blocking_completion_probe *)user;
    (void)lease_id;
    (void)operation_user;
    if (probe->count >= sizeof(probe->values) / sizeof(probe->values[0]))
        return;
    probe->request_ids[probe->count] = request_id;
    probe->values[probe->count] = *completion;
    ++probe->count;
}

static void blocking_job_probe_init(
    blocking_job_probe *probe, cflow_io_completion completion,
    bool initially_released) {
    memset(probe, 0, sizeof(*probe));
    probe->job.execute = blocking_job_execute;
    probe->job.user = probe;
    probe->result = completion;
    atomic_init(&probe->started, false);
    atomic_init(&probe->release_gate, initially_released);
    atomic_init(&probe->run_count, 0);
    atomic_init(&probe->release_count, 0);
}

static bool blocking_fixture_init(blocking_fixture *fixture,
                                  size_t capacity) {
    cflow_io_backend_ops backend = {0};
    void *backend_user = NULL;
    cflow_io_actor_config actor_config = {0};
    const cflow_io_blocking_driver_config driver_config = {
        .workers = 1u,
        .capacity = capacity,
    };
    memset(fixture, 0, sizeof(*fixture));
    if (cflow_io_blocking_driver_init(&fixture->driver, &driver_config) !=
        TURBO_OK)
        return false;
    if (!cflow_io_blocking_driver_as_backend(
            &fixture->driver, &backend, &backend_user))
        return false;
    if (!cflow_executor_manual_init_with_capacity(
            &fixture->delivery_executor, capacity + 1u))
        return false;
    actor_config.request_capacity = capacity + 1u;
    actor_config.command_capacity = capacity + 1u;
    actor_config.executor = &fixture->delivery_executor;
    actor_config.backend = backend;
    actor_config.backend_user = backend_user;
    actor_config.completion = blocking_completion_record;
    actor_config.completion_user = &fixture->completions;
    return cflow_io_actor_init(&fixture->actor, &actor_config) == TURBO_OK;
}

static cflow_io_submit_result blocking_submit(
    blocking_fixture *fixture, cflow_io_lease_id lease,
    blocking_job_probe *probe) {
    cflow_io_operation operation = {
        .user = &probe->job,
        .release = blocking_operation_release,
    };
    return cflow_io_actor_try_submit(&fixture->actor, lease, &operation);
}

static bool blocking_wait_started(blocking_job_probe *probe) {
    int attempts = 0;
    while (!atomic_load(&probe->started) && attempts++ < BLOCKING_TEST_SPIN_LIMIT)
        turbo_sleep_ms(1);
    return atomic_load(&probe->started);
}

static bool blocking_drive_until(blocking_fixture *fixture,
                                 size_t completion_count) {
    int attempts = 0;
    while (fixture->completions.count < completion_count &&
           attempts++ < BLOCKING_TEST_SPIN_LIMIT) {
        (void)cflow_io_actor_run_ready(&fixture->actor, 32u);
        (void)cflow_executor_run_ready(&fixture->delivery_executor);
        turbo_sleep_ms(1);
    }
    return fixture->completions.count >= completion_count;
}

static void blocking_fixture_destroy(blocking_fixture *fixture) {
    const int actor_close = cflow_io_actor_close(&fixture->actor);
    const int driver_close = cflow_io_blocking_driver_close(&fixture->driver);
    check_true(actor_close == TURBO_OK || actor_close == TURBO_EALREADY);
    check_true(driver_close == TURBO_OK || driver_close == TURBO_EALREADY);
    (void)cflow_io_actor_run_ready(&fixture->actor, 64u);
    (void)cflow_executor_run_ready(&fixture->delivery_executor);
    check_true(cflow_io_actor_is_quiescent(&fixture->actor));
    check_equal(cflow_io_actor_destroy(&fixture->actor), TURBO_OK);
    check_true(cflow_executor_shutdown(&fixture->delivery_executor));
    cflow_executor_destroy(&fixture->delivery_executor);
    check_equal(cflow_io_blocking_driver_destroy(&fixture->driver), TURBO_OK);
}

spec("CFlow blocking IO driver") {
    it("rejects invalid unbounded configuration") {
        cflow_io_blocking_driver driver = {0};
        cflow_io_blocking_driver_config config = {0};

        check_equal(cflow_io_blocking_driver_init(&driver, &config),
                    TURBO_EINVAL);
        config.workers = 1u;
        check_equal(cflow_io_blocking_driver_init(&driver, &config),
                    TURBO_EINVAL);
        check_null(driver.impl);
    }

    it("executes a borrowed job and publishes its exact terminal result") {
        blocking_fixture fixture;
        blocking_job_probe job;
        cflow_io_submit_result submitted;
        cflow_io_blocking_driver_stats stats = {0};

        blocking_job_probe_init(
            &job,
            (cflow_io_completion){CFLOW_IO_COMPLETION_OK, 4096u, TURBO_OK},
            true);
        check_true(blocking_fixture_init(&fixture, 2u));
        submitted = blocking_submit(&fixture, 11u, &job);
        check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&fixture.actor, 16u);
        check_true(blocking_wait_started(&job));
        check_true(blocking_drive_until(&fixture, 1u));
        check_equal(fixture.completions.request_ids[0], submitted.request_id);
        check_equal(fixture.completions.values[0].kind,
                    CFLOW_IO_COMPLETION_OK);
        check_equal(fixture.completions.values[0].bytes, (size_t)4096u);
        check_equal(atomic_load(&job.run_count), 1);
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, submitted.request_id),
                    CFLOW_IO_ACK_RELEASED);
        check_equal(atomic_load(&job.release_count), 1);
        check_true(cflow_io_blocking_driver_get_stats(&fixture.driver, &stats));
        check_equal(stats.completed, (uint64_t)1u);
        check_equal(stats.cancelled, (uint64_t)0u);
        check_equal(stats.active, (size_t)0u);
        blocking_fixture_destroy(&fixture);
    }

    it("fails fast when the fixed blocking slot capacity is exhausted") {
        blocking_fixture fixture;
        blocking_job_probe running;
        blocking_job_probe rejected;
        cflow_io_submit_result running_submit;
        cflow_io_submit_result rejected_submit;
        cflow_io_blocking_driver_stats stats = {0};

        blocking_job_probe_init(
            &running,
            (cflow_io_completion){CFLOW_IO_COMPLETION_OK, 32u, TURBO_OK},
            false);
        blocking_job_probe_init(
            &rejected,
            (cflow_io_completion){CFLOW_IO_COMPLETION_OK, 64u, TURBO_OK},
            true);
        check_true(blocking_fixture_init(&fixture, 1u));
        running_submit = blocking_submit(&fixture, 31u, &running);
        rejected_submit = blocking_submit(&fixture, 32u, &rejected);
        check_equal(running_submit.status, CFLOW_IO_SUBMIT_ACCEPTED);
        check_equal(rejected_submit.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&fixture.actor, 32u);
        check_true(blocking_wait_started(&running));
        check_true(blocking_drive_until(&fixture, 1u));
        check_equal(fixture.completions.request_ids[0],
                    rejected_submit.request_id);
        check_equal(fixture.completions.values[0].kind,
                    CFLOW_IO_COMPLETION_FAILED);
        check_equal(fixture.completions.values[0].error, TURBO_ENOBUFS);
        check_equal(atomic_load(&rejected.run_count), 0);

        atomic_store(&running.release_gate, true);
        check_true(blocking_drive_until(&fixture, 2u));
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, running_submit.request_id),
                    CFLOW_IO_ACK_RELEASED);
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, rejected_submit.request_id),
                    CFLOW_IO_ACK_RELEASED);
        check_true(cflow_io_blocking_driver_get_stats(&fixture.driver, &stats));
        check_equal(stats.accepted, (uint64_t)1u);
        check_equal(stats.rejected_full, (uint64_t)1u);
        check_equal(stats.completed, (uint64_t)1u);
        check_equal(stats.active, (size_t)0u);
        blocking_fixture_destroy(&fixture);
    }

    it("cancels queued work on close without entering its callback") {
        blocking_fixture fixture;
        blocking_job_probe running;
        blocking_job_probe queued;
        cflow_io_submit_result running_submit;
        cflow_io_submit_result queued_submit;
        size_t index;
        bool saw_ok = false;
        bool saw_cancelled = false;

        blocking_job_probe_init(
            &running,
            (cflow_io_completion){CFLOW_IO_COMPLETION_OK, 8u, TURBO_OK},
            false);
        blocking_job_probe_init(
            &queued,
            (cflow_io_completion){CFLOW_IO_COMPLETION_OK, 16u, TURBO_OK},
            true);
        check_true(blocking_fixture_init(&fixture, 2u));
        running_submit = blocking_submit(&fixture, 21u, &running);
        queued_submit = blocking_submit(&fixture, 22u, &queued);
        check_equal(running_submit.status, CFLOW_IO_SUBMIT_ACCEPTED);
        check_equal(queued_submit.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&fixture.actor, 32u);
        check_true(blocking_wait_started(&running));
        check_equal(cflow_io_actor_close(&fixture.actor), TURBO_OK);
        (void)cflow_io_actor_run_ready(&fixture.actor, 32u);
        check_equal(cflow_io_blocking_driver_close(&fixture.driver), TURBO_OK);
        atomic_store(&running.release_gate, true);
        check_true(blocking_drive_until(&fixture, 2u));

        for (index = 0u; index < fixture.completions.count; ++index) {
            const cflow_io_completion *completion =
                &fixture.completions.values[index];
            if (fixture.completions.request_ids[index] ==
                running_submit.request_id)
                saw_ok = completion->kind == CFLOW_IO_COMPLETION_OK;
            if (fixture.completions.request_ids[index] ==
                queued_submit.request_id)
                saw_cancelled =
                    completion->kind == CFLOW_IO_COMPLETION_CANCELLED;
        }
        check_true(saw_ok);
        check_true(saw_cancelled);
        check_equal(atomic_load(&running.run_count), 1);
        check_equal(atomic_load(&queued.run_count), 0);
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, running_submit.request_id),
                    CFLOW_IO_ACK_RELEASED);
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, queued_submit.request_id),
                    CFLOW_IO_ACK_RELEASED);
        blocking_fixture_destroy(&fixture);
    }
}
