#include <cflow/cflow.h>

#include "tinytest.h"

#include <string.h>

typedef struct io_source_fixture {
    cflow_io_source_prepare_status prepare_status;
    const char *prepare_error;
    size_t prepare_calls;
    size_t encode_calls;
    size_t backend_submit_calls;
    size_t backend_cancel_calls;
    size_t drive_calls;
    size_t release_calls;
    cflow_io_source_owner *stats_owner;
    cflow_source *cancel_source_during_prepare;
    cflow_io_source_stats drive_stats;
    bool prepare_valid_operation;
    bool capture_stats_on_drive;
    bool drive_stats_captured;
} io_source_fixture;

typedef struct io_source_sink_probe {
    size_t values;
    size_t errors;
    size_t done;
    const char *error;
} io_source_sink_probe;

typedef struct io_source_run_fixture {
    cflow_graph surface;
    cflow_graph normalized;
    cflow_scheduler scheduler;
    cflow_source source;
    cflow_io_source_owner owner;
    cflow_run run;
    io_source_sink_probe sink_probe;
    cflow_sink_callbacks sink_callbacks;
    cflow_sink sink;
} io_source_run_fixture;

static void io_source_operation_release(void *user) {
    io_source_fixture *fixture = (io_source_fixture *)user;
    if (fixture != NULL)
        ++fixture->release_calls;
}

static cflow_io_source_prepare_status io_source_prepare(
    void *user, cflow_io_operation *operation, const char **error) {
    io_source_fixture *fixture = (io_source_fixture *)user;

    ++fixture->prepare_calls;
    if (fixture->prepare_status == CFLOW_IO_SOURCE_PREPARE_OPERATION) {
        operation->user = fixture;
        operation->release = fixture->prepare_valid_operation
            ? io_source_operation_release : NULL;
        if (fixture->cancel_source_during_prepare != NULL)
            cflow_source_cancel(fixture->cancel_source_during_prepare);
    } else if (fixture->prepare_status == CFLOW_IO_SOURCE_PREPARE_ERROR) {
        *error = fixture->prepare_error;
    }
    return fixture->prepare_status;
}

static cflow_read_status io_source_encode(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion,
    void *out_value,
    const char **error) {
    io_source_fixture *fixture = (io_source_fixture *)user;

    (void)request_id;
    (void)lease_id;
    (void)operation_user;
    (void)completion;
    (void)out_value;
    (void)error;
    ++fixture->encode_calls;
    return CFLOW_READ_DONE;
}

static int io_source_backend_submit(
    void *user,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user) {
    io_source_fixture *fixture = (io_source_fixture *)user;

    (void)actor;
    (void)request_id;
    (void)lease_id;
    (void)operation_user;
    ++fixture->backend_submit_calls;
    return TURBO_OK;
}

static int io_source_backend_cancel(
    void *user, cflow_io_request_id request_id) {
    io_source_fixture *fixture = (io_source_fixture *)user;

    (void)request_id;
    ++fixture->backend_cancel_calls;
    return TURBO_OK;
}

static void io_source_drive(void *user) {
    io_source_fixture *fixture = (io_source_fixture *)user;
    ++fixture->drive_calls;
    if (fixture->capture_stats_on_drive &&
        fixture->stats_owner != NULL) {
        fixture->drive_stats_captured =
            cflow_io_source_owner_get_stats(
                fixture->stats_owner, &fixture->drive_stats);
    }
}

static cflow_io_source_config io_source_config(
    io_source_fixture *fixture) {
    cflow_io_source_config config = {0};

    config.name = "test-io-source";
    config.type = &cmeta_type_int;
    config.backend.submit = io_source_backend_submit;
    config.backend.cancel = io_source_backend_cancel;
    config.backend_user = fixture;
    config.prepare = io_source_prepare;
    config.encode = io_source_encode;
    config.user = fixture;
    config.drive = io_source_drive;
    config.drive_user = fixture;
    return config;
}

static bool io_source_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    io_source_sink_probe *probe = (io_source_sink_probe *)user;

    (void)type;
    (void)value;
    ++probe->values;
    return true;
}

static void io_source_sink_error(void *user, const char *message) {
    io_source_sink_probe *probe = (io_source_sink_probe *)user;

    ++probe->errors;
    probe->error = message;
}

static void io_source_sink_done(void *user) {
    io_source_sink_probe *probe = (io_source_sink_probe *)user;
    ++probe->done;
}

static bool io_source_run_fixture_init(
    io_source_run_fixture *run_fixture, io_source_fixture *fixture) {
    cflow_io_source_config config = io_source_config(fixture);
    bool surface_initialized = false;
    bool normalized_initialized = false;
    bool scheduler_initialized = false;

    memset(run_fixture, 0, sizeof(*run_fixture));
    run_fixture->normalized.root = CMETA_INVALID_ID;
    cflow_graph_init(&run_fixture->surface, &cmeta_type_int);
    surface_initialized = true;
    if (!cflow_graph_normalize(
            &run_fixture->normalized, &run_fixture->surface))
        goto cleanup;
    normalized_initialized = true;
    if (!cflow_scheduler_test_init(&run_fixture->scheduler))
        goto cleanup;
    scheduler_initialized = true;
    if (cflow_source_from_io_actor(
            &run_fixture->source, &run_fixture->owner,
            &config) != TURBO_OK)
        goto cleanup;

    run_fixture->sink_callbacks = (cflow_sink_callbacks){
        io_source_sink_value,
        io_source_sink_error,
        io_source_sink_done,
        &run_fixture->sink_probe
    };
    run_fixture->sink = cflow_sink_from_callbacks(
        &run_fixture->sink_callbacks);
    return true;

cleanup:
    if (cflow_source_valid(&run_fixture->source)) {
        cflow_source_destroy(&run_fixture->source);
        (void)cflow_io_source_owner_close(&run_fixture->owner);
    }
    if (scheduler_initialized)
        cflow_scheduler_destroy(&run_fixture->scheduler);
    if (normalized_initialized)
        cflow_graph_destroy(&run_fixture->normalized);
    if (surface_initialized)
        cflow_graph_destroy(&run_fixture->surface);
    return false;
}

static void io_source_run_fixture_close(
    io_source_run_fixture *run_fixture) {
    cflow_run_close(&run_fixture->run);
    check_equal(cflow_io_source_owner_close(
                    &run_fixture->owner), TURBO_OK);
    cflow_scheduler_destroy(&run_fixture->scheduler);
    cflow_graph_destroy(&run_fixture->normalized);
    cflow_graph_destroy(&run_fixture->surface);
}

spec("CFlow reactive IO source") {
    it("rejects an empty configuration without mutating outputs") {
        cflow_source source = {0};
        cflow_io_source_owner owner = {0};
        cflow_io_source_config config = {0};
        cflow_io_source_stats stats = {0};

        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(owner.impl);
        check_false(stats.source_live);
    }

    it("requires only the callbacks needed by the adapter protocol") {
        io_source_fixture fixture = {0};
        cflow_io_source_config config = io_source_config(&fixture);
        cflow_source source = {0};
        cflow_io_source_owner owner = {0};

        config.backend.submit = NULL;
        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(owner.impl);

        config = io_source_config(&fixture);
        config.prepare = NULL;
        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(owner.impl);

        config = io_source_config(&fixture);
        config.encode = NULL;
        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(owner.impl);

        config = io_source_config(&fixture);
        config.drive = NULL;
        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(owner.impl);

        config = io_source_config(&fixture);
        config.backend.cancel = NULL;
        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_OK);
        cflow_source_destroy(&source);
        check_equal(cflow_io_source_owner_close(&owner), TURBO_OK);
    }

    it("does not prepare or submit without downstream demand") {
        io_source_fixture fixture = {
            .prepare_status = CFLOW_IO_SOURCE_PREPARE_OPERATION,
            .prepare_valid_operation = true
        };
        io_source_run_fixture run_fixture;
        cflow_io_source_stats stats = {0};

        check_true(io_source_run_fixture_init(&run_fixture, &fixture));
        check_true(cflow_run_open(
            &run_fixture.run, &run_fixture.normalized,
            &run_fixture.source, &run_fixture.scheduler,
            &run_fixture.sink));
        check_equal(fixture.prepare_calls, (size_t)0u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_equal(cflow_scheduler_run_until_idle(
                        &run_fixture.scheduler, 0u), (size_t)0u);
        check_equal(fixture.prepare_calls, (size_t)0u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_true(cflow_io_source_owner_get_stats(
            &run_fixture.owner, &stats));
        check_equal(stats.actor.request_capacity, (size_t)1u);
        check_equal(stats.actor.command_capacity, (size_t)1u);
        check_true(stats.source_live);
        check_false(stats.request_active);
        check_false(stats.result_ready);
        check_false(stats.close_requested);
        check_equal(cflow_io_source_owner_close(
                        &run_fixture.owner), TURBO_EBUSY);
        check_not_null(run_fixture.owner.impl);

        io_source_run_fixture_close(&run_fixture);
        check_equal(fixture.release_calls, (size_t)0u);
    }

    it("reports an accepted operation active during Actor admission transfer") {
        io_source_fixture fixture = {
            .prepare_status = CFLOW_IO_SOURCE_PREPARE_OPERATION,
            .prepare_valid_operation = true,
            .capture_stats_on_drive = true
        };
        io_source_run_fixture run_fixture;
        cflow_io_source_stats stats = {0};

        check_true(io_source_run_fixture_init(&run_fixture, &fixture));
        fixture.stats_owner = &run_fixture.owner;
        check_true(cflow_run_open(
            &run_fixture.run, &run_fixture.normalized,
            &run_fixture.source, &run_fixture.scheduler,
            &run_fixture.sink));
        check_true(cflow_run_request(&run_fixture.run, 1u));
        (void)cflow_scheduler_run_until_idle(
            &run_fixture.scheduler, 0u);

        check_true(fixture.drive_stats_captured);
        check_equal(fixture.drive_stats.actor.accepted, (uint64_t)1u);
        check_equal(fixture.drive_stats.actor.active_requests, (size_t)1u);
        check_true(fixture.drive_stats.request_active);
        check_equal(fixture.prepare_calls, (size_t)1u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_equal(fixture.release_calls, (size_t)0u);
        check_true(cflow_io_source_owner_get_stats(
            &run_fixture.owner, &stats));
        check_equal(stats.actor.accepted, (uint64_t)1u);
        check_equal(stats.actor.active_requests, (size_t)1u);
        check_true(stats.request_active);

        cflow_run_close(&run_fixture.run);
        check_equal(fixture.release_calls, (size_t)0u);
        /* Task 2 deliberately stops before delivery/ack, so this accepted
         * owner remains busy until the Task 3 lifecycle is available. */
        check_equal(cflow_io_source_owner_close(
                        &run_fixture.owner), TURBO_EBUSY);
        cflow_scheduler_destroy(&run_fixture.scheduler);
        cflow_graph_destroy(&run_fixture.normalized);
        cflow_graph_destroy(&run_fixture.surface);
    }

    it("releases an adapter-owned operation once when Actor admission rejects") {
        io_source_fixture fixture = {
            .prepare_status = CFLOW_IO_SOURCE_PREPARE_OPERATION,
            .prepare_valid_operation = true
        };
        cflow_io_source_config config = io_source_config(&fixture);
        cflow_source source = {0};
        cflow_io_source_owner owner = {0};
        cflow_io_source_stats stats = {0};
        cflow_step step;
        int output = 0;

        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_OK);
        fixture.cancel_source_during_prepare = &source;
        step = cflow_source_resume(&source, NULL, &output);

        check_equal(step.kind, CFLOW_STEP_ERROR);
        check_equal(fixture.prepare_calls, (size_t)1u);
        check_equal(fixture.release_calls, (size_t)1u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_true(cflow_io_source_owner_get_stats(&owner, &stats));
        check_equal(stats.actor.accepted, (uint64_t)0u);
        check_equal(stats.actor.rejected_closed, (uint64_t)1u);
        check_false(stats.request_active);
        check_true(stats.close_requested);

        cflow_source_destroy(&source);
        check_equal(fixture.release_calls, (size_t)1u);
        check_equal(cflow_io_source_owner_close(&owner), TURBO_OK);
        check_equal(fixture.release_calls, (size_t)1u);
    }

    it("maps prepare DONE to one terminal sink notification") {
        io_source_fixture fixture = {
            .prepare_status = CFLOW_IO_SOURCE_PREPARE_DONE
        };
        io_source_run_fixture run_fixture;

        check_true(io_source_run_fixture_init(&run_fixture, &fixture));
        check_true(cflow_run_open(
            &run_fixture.run, &run_fixture.normalized,
            &run_fixture.source, &run_fixture.scheduler,
            &run_fixture.sink));
        check_true(cflow_run_request(&run_fixture.run, 1u));
        (void)cflow_scheduler_run_until_idle(
            &run_fixture.scheduler, 0u);

        check_equal(fixture.prepare_calls, (size_t)1u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_equal(run_fixture.sink_probe.values, (size_t)0u);
        check_equal(run_fixture.sink_probe.errors, (size_t)0u);
        check_equal(run_fixture.sink_probe.done, (size_t)1u);
        check_true(cflow_run_is_done(&run_fixture.run));
        {
            cflow_io_source_stats stats = {0};
            check_true(cflow_io_source_owner_get_stats(
                &run_fixture.owner, &stats));
            check_equal(stats.actor.accepted, (uint64_t)0u);
            check_false(stats.request_active);
        }

        io_source_run_fixture_close(&run_fixture);
    }

    it("maps prepare ERROR to the stable callback error") {
        static const char prepare_error[] = "literal prepare failure";
        io_source_fixture fixture = {
            .prepare_status = CFLOW_IO_SOURCE_PREPARE_ERROR,
            .prepare_error = prepare_error
        };
        io_source_run_fixture run_fixture;

        check_true(io_source_run_fixture_init(&run_fixture, &fixture));
        check_true(cflow_run_open(
            &run_fixture.run, &run_fixture.normalized,
            &run_fixture.source, &run_fixture.scheduler,
            &run_fixture.sink));
        check_true(cflow_run_request(&run_fixture.run, 1u));
        (void)cflow_scheduler_run_until_idle(
            &run_fixture.scheduler, 0u);

        check_equal(fixture.prepare_calls, (size_t)1u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_equal(run_fixture.sink_probe.values, (size_t)0u);
        check_equal(run_fixture.sink_probe.errors, (size_t)1u);
        check_equal(run_fixture.sink_probe.done, (size_t)0u);
        check_true(run_fixture.sink_probe.error == prepare_error);
        check_true(cflow_run_error(&run_fixture.run) == prepare_error);
        {
            cflow_io_source_stats stats = {0};
            check_true(cflow_io_source_owner_get_stats(
                &run_fixture.owner, &stats));
            check_equal(stats.actor.accepted, (uint64_t)0u);
            check_false(stats.request_active);
        }

        io_source_run_fixture_close(&run_fixture);
    }

    it("rejects an operation without a release token") {
        io_source_fixture fixture = {
            .prepare_status = CFLOW_IO_SOURCE_PREPARE_OPERATION,
            .prepare_valid_operation = false
        };
        io_source_run_fixture run_fixture;

        check_true(io_source_run_fixture_init(&run_fixture, &fixture));
        check_true(cflow_run_open(
            &run_fixture.run, &run_fixture.normalized,
            &run_fixture.source, &run_fixture.scheduler,
            &run_fixture.sink));
        check_true(cflow_run_request(&run_fixture.run, 1u));
        (void)cflow_scheduler_run_until_idle(
            &run_fixture.scheduler, 0u);

        check_equal(fixture.prepare_calls, (size_t)1u);
        check_equal(fixture.backend_submit_calls, (size_t)0u);
        check_equal(run_fixture.sink_probe.errors, (size_t)1u);

        io_source_run_fixture_close(&run_fixture);
    }
}
