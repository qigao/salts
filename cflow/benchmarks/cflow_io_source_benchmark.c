#include <cflow/cflow.h>

#if defined(CFLOW_IO_SOURCE_BENCH_HAS_MINICORO)
#include <cflow/minicoro.h>
#endif

#include "tinytest.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    IO_SOURCE_BENCH_DEFAULT_SAMPLES = 50,
    IO_SOURCE_BENCH_DEFAULT_VALUES = 4096,
    IO_SOURCE_BENCH_MAX_SAMPLES = 1000,
    IO_SOURCE_BENCH_MAX_VALUES = 1024 * 1024,
    IO_SOURCE_BENCH_DRIVER_STEPS = 64,
    IO_SOURCE_BENCH_LOOP_FACTOR = 16,
    IO_SOURCE_BENCH_LOOP_MARGIN = 64
};

typedef struct io_direct_benchmark_fixture {
    size_t completed;
} io_direct_benchmark_fixture;

typedef struct io_actor_benchmark_fixture {
    cflow_executor executor;
    cflow_io_actor actor;
    cflow_io_request_id completed_ids[CFLOW_IO_SOURCE_MAX_WINDOW];
    size_t completion_count;
    size_t submitted;
    size_t completed;
    size_t delivered;
    size_t released;
    cflow_io_lease_id next_lease;
    size_t capacity;
    bool executor_initialized;
    bool actor_initialized;
} io_actor_benchmark_fixture;

typedef int (*io_direct_benchmark_step_fn)(
    io_direct_benchmark_fixture *fixture);

static int io_direct_benchmark_step(
    io_direct_benchmark_fixture *fixture) {
    if (fixture == NULL)
        return TURBO_EINVAL;
    ++fixture->completed;
    return TURBO_OK;
}

static io_direct_benchmark_step_fn volatile io_direct_benchmark_step_call =
    io_direct_benchmark_step;

static int io_direct_benchmark_run_batch(
    io_direct_benchmark_fixture *fixture, size_t values) {
    if (fixture == NULL || values == 0u)
        return TURBO_EINVAL;
    for (size_t index = 0u; index < values; ++index) {
        const int status = io_direct_benchmark_step_call(fixture);
        if (status != TURBO_OK)
            return status;
    }
    return TURBO_OK;
}

static void io_actor_benchmark_release(void *user) {
    io_actor_benchmark_fixture *fixture =
        (io_actor_benchmark_fixture *)user;
    if (fixture != NULL)
        ++fixture->released;
}

static int io_actor_benchmark_submit(
    void *user, cflow_io_actor *actor,
    cflow_io_request_id request_id, cflow_io_lease_id lease_id,
    void *operation_user) {
    io_actor_benchmark_fixture *fixture =
        (io_actor_benchmark_fixture *)user;
    const cflow_io_completion completion = {
        CFLOW_IO_COMPLETION_OK, sizeof(int), TURBO_OK};

    (void)lease_id;
    (void)operation_user;
    if (fixture == NULL || actor == NULL)
        return TURBO_EINVAL;
    ++fixture->submitted;
    if (cflow_io_actor_complete(actor, request_id, &completion) !=
        CFLOW_IO_COMPLETE_ACCEPTED)
        return TURBO_EPROTO;
    ++fixture->completed;
    return TURBO_OK;
}

static void io_actor_benchmark_completion(
    void *user, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user,
    const cflow_io_completion *completion) {
    io_actor_benchmark_fixture *fixture =
        (io_actor_benchmark_fixture *)user;

    (void)lease_id;
    (void)operation_user;
    if (fixture == NULL || completion == NULL ||
        completion->kind != CFLOW_IO_COMPLETION_OK ||
        fixture->completion_count >= fixture->capacity)
        return;
    fixture->completed_ids[fixture->completion_count++] = request_id;
    ++fixture->delivered;
}

static int io_actor_benchmark_destroy(
    io_actor_benchmark_fixture *fixture) {
    int status = TURBO_OK;
    if (fixture == NULL)
        return TURBO_EINVAL;
    if (fixture->actor_initialized) {
        status = cflow_io_actor_close(&fixture->actor);
        if (status == TURBO_OK) {
            (void)cflow_io_actor_run_ready(
                &fixture->actor, IO_SOURCE_BENCH_DRIVER_STEPS);
            (void)cflow_executor_run_ready(&fixture->executor);
            status = cflow_io_actor_destroy(&fixture->actor);
        }
        if (status == TURBO_OK)
            fixture->actor_initialized = false;
    }
    if (fixture->executor_initialized) {
        if (!cflow_executor_shutdown(&fixture->executor) &&
            status == TURBO_OK)
            status = TURBO_EBUSY;
        if (status == TURBO_OK) {
            cflow_executor_destroy(&fixture->executor);
            fixture->executor_initialized = false;
        }
    }
    return status;
}

static int io_actor_benchmark_init(
    io_actor_benchmark_fixture *fixture, size_t capacity) {
    cflow_io_actor_config config = {0};
    int status;

    if (fixture == NULL || capacity == 0u ||
        capacity > CFLOW_IO_SOURCE_MAX_WINDOW)
        return TURBO_EINVAL;
    memset(fixture, 0, sizeof(*fixture));
    fixture->capacity = capacity;
    fixture->next_lease = UINT64_C(1);
    if (!cflow_executor_manual_init_with_capacity(
            &fixture->executor, capacity))
        return TURBO_ENOMEM;
    fixture->executor_initialized = true;
    config.request_capacity = capacity;
    config.command_capacity = capacity;
    config.executor = &fixture->executor;
    config.backend.submit = io_actor_benchmark_submit;
    config.backend_user = fixture;
    config.completion = io_actor_benchmark_completion;
    config.completion_user = fixture;
    status = cflow_io_actor_init(&fixture->actor, &config);
    if (status != TURBO_OK) {
        (void)io_actor_benchmark_destroy(fixture);
        return status;
    }
    fixture->actor_initialized = true;
    return TURBO_OK;
}

static int io_actor_benchmark_run_batch(
    io_actor_benchmark_fixture *fixture, size_t values) {
    size_t remaining = values;
    if (fixture == NULL || values == 0u || !fixture->actor_initialized)
        return TURBO_EINVAL;
    while (remaining != 0u) {
        const size_t batch = remaining < fixture->capacity
            ? remaining : fixture->capacity;
        size_t loops = 0u;
        fixture->completion_count = 0u;
        if (fixture->next_lease == 0u ||
            batch - 1u > UINT64_MAX - fixture->next_lease)
            return TURBO_ERANGE;
        for (size_t index = 0u; index < batch; ++index) {
            cflow_io_operation operation = {
                fixture, io_actor_benchmark_release};
            const cflow_io_submit_result submitted =
                cflow_io_actor_try_submit(
                    &fixture->actor, fixture->next_lease++, &operation);
            if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED)
                return TURBO_EBUSY;
        }
        while (fixture->completion_count < batch &&
               loops < IO_SOURCE_BENCH_LOOP_MARGIN) {
            (void)cflow_io_actor_run_ready(
                &fixture->actor, IO_SOURCE_BENCH_DRIVER_STEPS);
            (void)cflow_executor_run_ready(&fixture->executor);
            ++loops;
        }
        if (fixture->completion_count != batch)
            return TURBO_ETIMEDOUT;
        for (size_t index = 0u; index < batch; ++index) {
            if (cflow_io_actor_acknowledge(
                    &fixture->actor, fixture->completed_ids[index]) !=
                CFLOW_IO_ACK_RELEASED)
                return TURBO_EPROTO;
        }
        remaining -= batch;
    }
    return TURBO_OK;
}

typedef struct io_source_benchmark_fixture {
    cflow_graph surface;
    cflow_graph normalized;
    cflow_scheduler scheduler;
    cflow_source source;
    cflow_io_source_owner owner;
    cflow_run run;
    cflow_sink_callbacks sink_callbacks;
    cflow_sink sink;
    size_t prepared;
    size_t submitted;
    size_t encoded;
    size_t released;
    size_t adapter_values;
#if defined(CFLOW_IO_SOURCE_BENCH_HAS_MINICORO)
    cflow_resumable coroutine;
    size_t coroutine_values;
    bool coroutine_initialized;
#endif
    size_t sink_values;
    size_t drive_calls;
    size_t driver_calls;
    bool drive_scheduled;
    const char *sink_error;
    bool surface_initialized;
    bool normalized_initialized;
    bool scheduler_initialized;
    bool owner_initialized;
    bool run_initialized;
} io_source_benchmark_fixture;

static bool io_source_benchmark_env(
    const char *name, size_t fallback, size_t maximum,
    size_t *out) {
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long long parsed;

    if (name == NULL || out == NULL || fallback == 0u ||
        fallback > maximum)
        return false;
    if (text == NULL || text[0] == '\0') {
        *out = fallback;
        return true;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed == 0u ||
        parsed > maximum)
        return false;
    *out = (size_t)parsed;
    return true;
}

static void io_source_benchmark_release(void *user) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;
    if (fixture != NULL)
        ++fixture->released;
}

static cflow_io_source_prepare_status io_source_benchmark_prepare(
    void *user, cflow_io_operation *operation, const char **error) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;

    (void)error;
    if (fixture == NULL || operation == NULL)
        return CFLOW_IO_SOURCE_PREPARE_ERROR;
    ++fixture->prepared;
    operation->user = fixture;
    operation->release = io_source_benchmark_release;
    return CFLOW_IO_SOURCE_PREPARE_OPERATION;
}

static cflow_read_status io_source_benchmark_encode(
    void *user, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user,
    const cflow_io_completion *completion, void *out_value,
    const char **error) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;

    (void)request_id;
    (void)lease_id;
    (void)operation_user;
    (void)error;
    if (fixture == NULL || completion == NULL || out_value == NULL ||
        completion->kind != CFLOW_IO_COMPLETION_OK)
        return CFLOW_READ_ERROR;
    ++fixture->encoded;
    *(int *)out_value = 1;
    return CFLOW_READ_VALUE;
}

static int io_source_benchmark_submit(
    void *user, cflow_io_actor *actor,
    cflow_io_request_id request_id, cflow_io_lease_id lease_id,
    void *operation_user) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;
    const cflow_io_completion completion = {
        CFLOW_IO_COMPLETION_OK, sizeof(int), TURBO_OK};

    (void)lease_id;
    (void)operation_user;
    if (fixture == NULL || actor == NULL)
        return TURBO_EINVAL;
    ++fixture->submitted;
    return cflow_io_actor_complete(actor, request_id, &completion) ==
                   CFLOW_IO_COMPLETE_ACCEPTED
        ? TURBO_OK : TURBO_EPROTO;
}

static void io_source_benchmark_drive(void *user) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;
    if (fixture != NULL) {
        ++fixture->drive_calls;
        fixture->drive_scheduled = true;
    }
}

static bool io_source_benchmark_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;
    if (fixture == NULL || value == NULL ||
        !cmeta_type_equal(type, &cmeta_type_int) ||
        *(const int *)value != 1)
        return false;
    ++fixture->sink_values;
    return true;
}

static void io_source_benchmark_sink_error(
    void *user, const char *message) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;
    if (fixture != NULL)
        fixture->sink_error = message;
}

static void io_source_benchmark_sink_done(void *user) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;
    if (fixture != NULL && fixture->sink_error == NULL)
        fixture->sink_error = "benchmark Source terminated unexpectedly";
}

static int io_source_benchmark_destroy(
    io_source_benchmark_fixture *fixture) {
    int status = TURBO_OK;

    if (fixture == NULL)
        return TURBO_EINVAL;
#if defined(CFLOW_IO_SOURCE_BENCH_HAS_MINICORO)
    if (fixture->coroutine_initialized) {
        fixture->coroutine.ops->destroy(fixture->coroutine.state);
        memset(&fixture->coroutine, 0, sizeof(fixture->coroutine));
        fixture->coroutine_initialized = false;
    }
#endif
    if (fixture->run_initialized) {
        cflow_run_close(&fixture->run);
        fixture->run_initialized = false;
    } else if (cflow_source_valid(&fixture->source)) {
        cflow_source_destroy(&fixture->source);
    }
    if (fixture->owner_initialized) {
        int close_status;

        while (!cflow_io_source_owner_is_quiescent(&fixture->owner)) {
            size_t progressed = 0u;
            const int drive_status = cflow_io_source_owner_run_ready(
                &fixture->owner, IO_SOURCE_BENCH_DRIVER_STEPS,
                &progressed);
            if (drive_status != TURBO_OK && status == TURBO_OK)
                status = drive_status;
            if (drive_status != TURBO_OK || progressed == 0u)
                break;
        }
        close_status = cflow_io_source_owner_close(&fixture->owner);
        if (status == TURBO_OK)
            status = close_status;
        if (close_status == TURBO_OK)
            fixture->owner_initialized = false;
    }
    if (fixture->scheduler_initialized) {
        cflow_scheduler_destroy(&fixture->scheduler);
        fixture->scheduler_initialized = false;
    }
    if (fixture->normalized_initialized) {
        cflow_graph_destroy(&fixture->normalized);
        fixture->normalized_initialized = false;
    }
    if (fixture->surface_initialized) {
        cflow_graph_destroy(&fixture->surface);
        fixture->surface_initialized = false;
    }
    return status;
}

static cflow_io_source_config io_source_benchmark_config(
    io_source_benchmark_fixture *fixture) {
    cflow_io_source_config config = {0};

    config.name = "windowed-source-benchmark";
    config.type = &cmeta_type_int;
    config.backend.submit = io_source_benchmark_submit;
    config.backend_user = fixture;
    config.prepare = io_source_benchmark_prepare;
    config.encode = io_source_benchmark_encode;
    config.user = fixture;
    config.drive = io_source_benchmark_drive;
    config.drive_user = fixture;
    return config;
}

static int io_source_adapter_benchmark_init(
    io_source_benchmark_fixture *fixture, size_t capacity) {
    cflow_io_source_config config;
    int status;

    if (fixture == NULL || capacity == 0u ||
        capacity > CFLOW_IO_SOURCE_MAX_WINDOW)
        return TURBO_EINVAL;
    memset(fixture, 0, sizeof(*fixture));
    config = io_source_benchmark_config(fixture);
    status = cflow_source_from_io_actor_windowed(
        &fixture->source, &fixture->owner, &config, capacity);
    if (status != TURBO_OK)
        return status;
    fixture->owner_initialized = true;
    return TURBO_OK;
}

static int io_source_benchmark_init(
    io_source_benchmark_fixture *fixture, size_t capacity) {
    int status = io_source_adapter_benchmark_init(fixture, capacity);

    if (status != TURBO_OK)
        return status;
    fixture->normalized.root = CMETA_INVALID_ID;
    cflow_graph_init(&fixture->surface, &cmeta_type_int);
    fixture->surface_initialized = true;
    if (!cflow_graph_normalize(
            &fixture->normalized, &fixture->surface))
        goto cleanup;
    fixture->normalized_initialized = true;
    if (!cflow_scheduler_test_init(&fixture->scheduler))
        goto cleanup;
    fixture->scheduler_initialized = true;
    fixture->sink_callbacks = (cflow_sink_callbacks){
        io_source_benchmark_sink_value,
        io_source_benchmark_sink_error,
        io_source_benchmark_sink_done,
        fixture};
    fixture->sink = cflow_sink_from_callbacks(
        &fixture->sink_callbacks);
    if (!cflow_run_open(
            &fixture->run, &fixture->normalized, &fixture->source,
            &fixture->scheduler, &fixture->sink)) {
        status = TURBO_EIO;
        goto cleanup;
    }
    fixture->run_initialized = true;
    return TURBO_OK;

cleanup:
    (void)io_source_benchmark_destroy(fixture);
    return status;
}

static int io_source_adapter_benchmark_run_batch(
    io_source_benchmark_fixture *fixture, size_t values) {
    size_t target_values;
    size_t target_released;
    size_t loop_limit;
    size_t loops = 0u;

    if (fixture == NULL || values == 0u ||
        !cflow_source_valid(&fixture->source))
        return TURBO_EINVAL;
    target_values = fixture->adapter_values + values;
    target_released = fixture->released + values;
    loop_limit = values <=
            (SIZE_MAX - IO_SOURCE_BENCH_LOOP_MARGIN) /
                IO_SOURCE_BENCH_LOOP_FACTOR
        ? values * IO_SOURCE_BENCH_LOOP_FACTOR +
              IO_SOURCE_BENCH_LOOP_MARGIN
        : SIZE_MAX;
    if (target_values < fixture->adapter_values ||
        target_released < fixture->released)
        return TURBO_ERANGE;

    while (fixture->adapter_values < target_values && loops < loop_limit) {
        cflow_resume_ctx context = {
            NULL, target_values - fixture->adapter_values};
        int output = 0;
        const cflow_step step = cflow_source_resume(
            &fixture->source, &context, &output);

        if (step.kind == CFLOW_STEP_VALUE ||
            step.kind == CFLOW_STEP_VALUE_AND_DONE) {
            if (output != 1 || step.kind == CFLOW_STEP_VALUE_AND_DONE)
                return TURBO_EPROTO;
            ++fixture->adapter_values;
        } else if (step.kind == CFLOW_STEP_WAIT) {
            size_t progressed = 0u;
            int status;

            if (!fixture->drive_scheduled)
                return TURBO_EPROTO;
            fixture->drive_scheduled = false;
            ++fixture->driver_calls;
            status = cflow_io_source_owner_run_ready(
                &fixture->owner, IO_SOURCE_BENCH_DRIVER_STEPS,
                &progressed);
            if (status != TURBO_OK)
                return status;
            if (progressed == 0u)
                return TURBO_EPROTO;
        } else {
            return step.kind == CFLOW_STEP_ERROR
                ? TURBO_EIO : TURBO_EPROTO;
        }
        ++loops;
    }
    while (fixture->released < target_released && loops < loop_limit) {
        size_t progressed = 0u;
        int status;

        if (!fixture->drive_scheduled)
            return TURBO_EPROTO;
        fixture->drive_scheduled = false;
        ++fixture->driver_calls;
        status = cflow_io_source_owner_run_ready(
            &fixture->owner, IO_SOURCE_BENCH_DRIVER_STEPS,
            &progressed);
        if (status != TURBO_OK)
            return status;
        if (progressed == 0u)
            return TURBO_EPROTO;
        ++loops;
    }
    return fixture->adapter_values == target_values &&
            fixture->released == target_released
        ? TURBO_OK : TURBO_ETIMEDOUT;
}

#if defined(CFLOW_IO_SOURCE_BENCH_HAS_MINICORO)
static void io_source_coroutine_benchmark_entry(
    cflow_minicoro *coroutine, void *user) {
    io_source_benchmark_fixture *fixture =
        (io_source_benchmark_fixture *)user;

    if (fixture == NULL) {
        (void)cflow_minicoro_fail(
            coroutine, "coroutine Source benchmark fixture is null");
        return;
    }
    for (;;) {
        int value = 0;
        const cflow_step step = cflow_minicoro_await_source(
            coroutine, &fixture->source, &value);

        if (step.kind == CFLOW_STEP_VALUE) {
            if (value != 1 ||
                !cflow_minicoro_yield_value(coroutine, &value))
                return;
            continue;
        }
        if (step.kind == CFLOW_STEP_VALUE_AND_DONE) {
            if (value != 1)
                (void)cflow_minicoro_fail(
                    coroutine, "coroutine Source benchmark value is invalid");
            else
                (void)cflow_minicoro_return_value(coroutine, &value);
            return;
        }
        if (step.kind == CFLOW_STEP_ERROR) {
            (void)cflow_minicoro_fail(
                coroutine,
                step.error != NULL
                    ? step.error
                    : "coroutine Source benchmark Source failed");
            return;
        }
        if (step.kind == CFLOW_STEP_DONE)
            return;
        (void)cflow_minicoro_fail(
            coroutine, "coroutine Source benchmark await returned WAIT");
        return;
    }
}

static int io_source_coroutine_benchmark_init(
    io_source_benchmark_fixture *fixture, size_t capacity) {
    cflow_minicoro_config config = {0};
    int status = io_source_adapter_benchmark_init(fixture, capacity);

    if (status != TURBO_OK)
        return status;
    config.name = "coroutine-source-adapter";
    config.output_type = &cmeta_type_int;
    config.entry = io_source_coroutine_benchmark_entry;
    config.user = fixture;
    if (!cflow_resumable_from_minicoro(&fixture->coroutine, &config)) {
        (void)io_source_benchmark_destroy(fixture);
        return TURBO_ENOMEM;
    }
    fixture->coroutine_initialized = true;
    return TURBO_OK;
}

static int io_source_coroutine_benchmark_run_batch(
    io_source_benchmark_fixture *fixture, size_t values) {
    size_t target_values;
    size_t target_released;
    size_t loop_limit;
    size_t loops = 0u;

    if (fixture == NULL || values == 0u ||
        !fixture->coroutine_initialized)
        return TURBO_EINVAL;
    target_values = fixture->coroutine_values + values;
    target_released = fixture->released + values;
    loop_limit = values <=
            (SIZE_MAX - IO_SOURCE_BENCH_LOOP_MARGIN) /
                IO_SOURCE_BENCH_LOOP_FACTOR
        ? values * IO_SOURCE_BENCH_LOOP_FACTOR +
              IO_SOURCE_BENCH_LOOP_MARGIN
        : SIZE_MAX;
    if (target_values < fixture->coroutine_values ||
        target_released < fixture->released)
        return TURBO_ERANGE;

    while (fixture->coroutine_values < target_values &&
           loops < loop_limit) {
        cflow_resume_ctx context = {
            NULL, target_values - fixture->coroutine_values};
        int output = 0;
        const cflow_step step = fixture->coroutine.ops->resume(
            fixture->coroutine.state, &context, &output);

        if (step.kind == CFLOW_STEP_VALUE) {
            if (output != 1)
                return TURBO_EPROTO;
            ++fixture->coroutine_values;
        } else if (step.kind == CFLOW_STEP_WAIT) {
            size_t progressed = 0u;
            int status;

            if (!fixture->drive_scheduled)
                return TURBO_EPROTO;
            fixture->drive_scheduled = false;
            ++fixture->driver_calls;
            status = cflow_io_source_owner_run_ready(
                &fixture->owner, IO_SOURCE_BENCH_DRIVER_STEPS,
                &progressed);
            if (status != TURBO_OK)
                return status;
            if (progressed == 0u)
                return TURBO_EPROTO;
        } else {
            return step.kind == CFLOW_STEP_ERROR
                ? TURBO_EIO : TURBO_EPROTO;
        }
        ++loops;
    }
    while (fixture->released < target_released && loops < loop_limit) {
        size_t progressed = 0u;
        int status;

        if (!fixture->drive_scheduled)
            return TURBO_EPROTO;
        fixture->drive_scheduled = false;
        ++fixture->driver_calls;
        status = cflow_io_source_owner_run_ready(
            &fixture->owner, IO_SOURCE_BENCH_DRIVER_STEPS,
            &progressed);
        if (status != TURBO_OK)
            return status;
        if (progressed == 0u)
            return TURBO_EPROTO;
        ++loops;
    }
    return fixture->coroutine_values == target_values &&
            fixture->released == target_released
        ? TURBO_OK : TURBO_ETIMEDOUT;
}
#endif

static int io_source_benchmark_run_batch(
    io_source_benchmark_fixture *fixture, size_t values) {
    size_t target;
    size_t loop_limit;

    if (fixture == NULL || values == 0u)
        return TURBO_EINVAL;
    target = fixture->sink_values + values;
    loop_limit = values <=
            (SIZE_MAX - IO_SOURCE_BENCH_LOOP_MARGIN) /
                IO_SOURCE_BENCH_LOOP_FACTOR
        ? values * IO_SOURCE_BENCH_LOOP_FACTOR +
              IO_SOURCE_BENCH_LOOP_MARGIN
        : SIZE_MAX;
    size_t loops = 0u;

    if (target < fixture->sink_values ||
        !cflow_run_request(&fixture->run, values))
        return TURBO_EINVAL;
    while (fixture->sink_values < target && loops < loop_limit) {
        size_t progressed = 0u;
        int status;

        (void)cflow_scheduler_run_until_idle(&fixture->scheduler, 0u);
        if (fixture->sink_values >= target)
            break;
        if (!fixture->drive_scheduled)
            return TURBO_EPROTO;
        fixture->drive_scheduled = false;
        ++fixture->driver_calls;
        status = cflow_io_source_owner_run_ready(
            &fixture->owner, IO_SOURCE_BENCH_DRIVER_STEPS,
            &progressed);
        if (status != TURBO_OK)
            return status;
        (void)cflow_scheduler_run_until_idle(&fixture->scheduler, 0u);
        if (fixture->sink_error != NULL ||
            cflow_run_error(&fixture->run) != NULL)
            return TURBO_EIO;
        if (progressed == 0u && fixture->sink_values < target)
            return TURBO_EPROTO;
        ++loops;
    }
    return fixture->sink_values == target
        ? TURBO_OK : TURBO_ETIMEDOUT;
}

suite("CFlow mock IO model benchmarks") {
bench("mock direct-control completion path") {
    size_t capacity = 0u;
    size_t samples = 0u;
    size_t values_per_sample = 0u;
    io_direct_benchmark_fixture fixture = {0};
    int run_status = TURBO_OK;
    const bool config_valid = io_source_benchmark_env(
            "CFLOW_IO_SOURCE_WINDOW", 1u,
            CFLOW_IO_SOURCE_MAX_WINDOW, &capacity) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_SAMPLES", IO_SOURCE_BENCH_DEFAULT_SAMPLES,
            IO_SOURCE_BENCH_MAX_SAMPLES, &samples) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_VALUES", IO_SOURCE_BENCH_DEFAULT_VALUES,
            IO_SOURCE_BENCH_MAX_VALUES, &values_per_sample);

    check_true(config_valid);
    if (!config_valid)
        return;
    check_equal(io_direct_benchmark_run_batch(
        &fixture, values_per_sample), TURBO_OK);
    benchmark_ops("mock direct-control", samples, values_per_sample) {
        if (run_status == TURBO_OK)
            run_status = io_direct_benchmark_run_batch(
                &fixture, values_per_sample);
    }
    check_equal(run_status, TURBO_OK);
    check_equal(fixture.completed,
                (samples + 1u) * values_per_sample);
    printf("CFLOW_IO_MODEL_BENCH_JSON "
           "{\"schema\":\"cflow-io-model-benchmark/v1\","
           "\"model\":\"direct-control\",\"capacity\":%zu,"
           "\"values_per_sample\":%zu,\"samples\":%zu,"
           "\"timed_values\":%" PRIu64 ",\"processed_values\":%zu,"
           "\"errors\":0,\"rejections\":0,\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           (uint64_t)samples * (uint64_t)values_per_sample,
           fixture.completed);
}

bench("mock IO Actor completion path") {
    size_t capacity = 0u;
    size_t samples = 0u;
    size_t values_per_sample = 0u;
    io_actor_benchmark_fixture fixture;
    cflow_io_actor_stats stats = {0};
    int run_status = TURBO_OK;
    const bool config_valid = io_source_benchmark_env(
            "CFLOW_IO_SOURCE_WINDOW", 1u,
            CFLOW_IO_SOURCE_MAX_WINDOW, &capacity) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_SAMPLES", IO_SOURCE_BENCH_DEFAULT_SAMPLES,
            IO_SOURCE_BENCH_MAX_SAMPLES, &samples) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_VALUES", IO_SOURCE_BENCH_DEFAULT_VALUES,
            IO_SOURCE_BENCH_MAX_VALUES, &values_per_sample);

    check_true(config_valid);
    if (!config_valid)
        return;
    memset(&fixture, 0, sizeof(fixture));
    check_equal(io_actor_benchmark_init(&fixture, capacity), TURBO_OK);
    if (!fixture.actor_initialized)
        return;
    check_equal(io_actor_benchmark_run_batch(
        &fixture, values_per_sample), TURBO_OK);
    benchmark_ops("mock Actor", samples, values_per_sample) {
        if (run_status == TURBO_OK)
            run_status = io_actor_benchmark_run_batch(
                &fixture, values_per_sample);
    }
    check_equal(run_status, TURBO_OK);
    check_true(cflow_io_actor_get_stats(&fixture.actor, &stats));
    check_equal(stats.active_requests, (size_t)0u);
    check_equal(stats.accepted, stats.acknowledged);
    check_equal(stats.rejected_request_full, (uint64_t)0u);
    check_equal(stats.rejected_command_full, (uint64_t)0u);
    check_equal(stats.stale_completions, (uint64_t)0u);
    check_equal(fixture.submitted, fixture.completed);
    check_equal(fixture.completed, fixture.delivered);
    check_equal(fixture.delivered, fixture.released);
    printf("CFLOW_IO_MODEL_BENCH_JSON "
           "{\"schema\":\"cflow-io-model-benchmark/v1\","
           "\"model\":\"actor\",\"capacity\":%zu,"
           "\"values_per_sample\":%zu,\"samples\":%zu,"
           "\"timed_values\":%" PRIu64 ",\"processed_values\":%zu,"
           "\"accepted\":%" PRIu64 ",\"acknowledged\":%" PRIu64 ","
           "\"errors\":0,\"rejections\":0,\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           (uint64_t)samples * (uint64_t)values_per_sample,
           fixture.delivered, stats.accepted, stats.acknowledged);
    check_equal(io_actor_benchmark_destroy(&fixture), TURBO_OK);
}

bench("mock IO Source adapter control path") {
    size_t capacity = 0u;
    size_t samples = 0u;
    size_t values_per_sample = 0u;
    io_source_benchmark_fixture fixture;
    cflow_io_source_stats stats = {0};
    cflow_io_source_window_stats window = {0};
    int init_status;
    int run_status = TURBO_OK;
    const bool config_valid = io_source_benchmark_env(
            "CFLOW_IO_SOURCE_WINDOW", 1u,
            CFLOW_IO_SOURCE_MAX_WINDOW, &capacity) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_SAMPLES", IO_SOURCE_BENCH_DEFAULT_SAMPLES,
            IO_SOURCE_BENCH_MAX_SAMPLES, &samples) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_VALUES", IO_SOURCE_BENCH_DEFAULT_VALUES,
            IO_SOURCE_BENCH_MAX_VALUES, &values_per_sample);

    check_true(config_valid);
    if (!config_valid)
        return;
    memset(&fixture, 0, sizeof(fixture));
    init_status = io_source_adapter_benchmark_init(&fixture, capacity);
    check_equal(init_status, TURBO_OK);
    if (init_status != TURBO_OK)
        return;
    check_equal(io_source_adapter_benchmark_run_batch(
        &fixture, values_per_sample), TURBO_OK);
    check_equal(fixture.prepared, values_per_sample);
    check_equal(fixture.submitted, values_per_sample);
    check_equal(fixture.encoded, values_per_sample);
    check_equal(fixture.released, values_per_sample);
    check_equal(fixture.adapter_values, values_per_sample);

    benchmark_ops("mock IO Source adapter", samples, values_per_sample) {
        if (run_status == TURBO_OK)
            run_status = io_source_adapter_benchmark_run_batch(
                &fixture, values_per_sample);
    }
    check_equal(run_status, TURBO_OK);
    check_true(cflow_io_source_owner_get_stats(&fixture.owner, &stats));
    check_true(cflow_io_source_owner_get_window_stats(
        &fixture.owner, &window));
    check_equal(stats.actor.active_requests, (size_t)0u);
    check_equal(stats.actor.accepted, stats.actor.acknowledged);
    check_equal(stats.actor.rejected_request_full, (uint64_t)0u);
    check_equal(stats.actor.rejected_command_full, (uint64_t)0u);
    check_equal(stats.actor.stale_completions, (uint64_t)0u);
    check_equal(window.occupied, (size_t)0u);
    check_equal(window.demand_reserved, (size_t)0u);
    check_equal(window.results_ready, (size_t)0u);
    check_equal(fixture.prepared, fixture.submitted);
    check_equal(fixture.submitted, fixture.encoded);
    check_equal(fixture.encoded, fixture.adapter_values);
    check_equal(fixture.adapter_values, fixture.released);
    printf("CFLOW_IO_MODEL_BENCH_JSON "
           "{\"schema\":\"cflow-io-model-benchmark/v1\","
           "\"model\":\"io-source-adapter\",\"capacity\":%zu,"
           "\"values_per_sample\":%zu,\"samples\":%zu,"
           "\"timed_values\":%" PRIu64 ",\"processed_values\":%zu,"
           "\"accepted\":%" PRIu64 ",\"acknowledged\":%" PRIu64 ","
           "\"drive_calls\":%zu,\"driver_calls\":%zu,"
           "\"peak_occupied\":%zu,\"errors\":0,\"rejections\":0,"
           "\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           (uint64_t)samples * (uint64_t)values_per_sample,
           fixture.adapter_values, stats.actor.accepted,
           stats.actor.acknowledged, fixture.drive_calls,
           fixture.driver_calls, window.peak_occupied);
    check_equal(io_source_benchmark_destroy(&fixture), TURBO_OK);
}

#if defined(CFLOW_IO_SOURCE_BENCH_HAS_MINICORO)
bench("mock coroutine IO Source adapter control path") {
    size_t capacity = 0u;
    size_t samples = 0u;
    size_t values_per_sample = 0u;
    io_source_benchmark_fixture fixture;
    cflow_io_source_stats stats = {0};
    cflow_io_source_window_stats window = {0};
    int init_status;
    int run_status = TURBO_OK;
    const bool config_valid = io_source_benchmark_env(
            "CFLOW_IO_SOURCE_WINDOW", 1u,
            CFLOW_IO_SOURCE_MAX_WINDOW, &capacity) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_SAMPLES", IO_SOURCE_BENCH_DEFAULT_SAMPLES,
            IO_SOURCE_BENCH_MAX_SAMPLES, &samples) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_VALUES", IO_SOURCE_BENCH_DEFAULT_VALUES,
            IO_SOURCE_BENCH_MAX_VALUES, &values_per_sample);

    check_true(config_valid);
    if (!config_valid)
        return;
    memset(&fixture, 0, sizeof(fixture));
    init_status = io_source_coroutine_benchmark_init(&fixture, capacity);
    check_equal(init_status, TURBO_OK);
    if (init_status != TURBO_OK)
        return;
    check_equal(io_source_coroutine_benchmark_run_batch(
        &fixture, values_per_sample), TURBO_OK);
    check_equal(fixture.prepared, values_per_sample);
    check_equal(fixture.submitted, values_per_sample);
    check_equal(fixture.encoded, values_per_sample);
    check_equal(fixture.released, values_per_sample);
    check_equal(fixture.coroutine_values, values_per_sample);

    benchmark_ops(
        "mock coroutine Source adapter", samples, values_per_sample) {
        if (run_status == TURBO_OK)
            run_status = io_source_coroutine_benchmark_run_batch(
                &fixture, values_per_sample);
    }
    check_equal(run_status, TURBO_OK);
    check_true(cflow_io_source_owner_get_stats(&fixture.owner, &stats));
    check_true(cflow_io_source_owner_get_window_stats(
        &fixture.owner, &window));
    check_equal(stats.actor.active_requests, (size_t)0u);
    check_equal(stats.actor.accepted, stats.actor.acknowledged);
    check_equal(stats.actor.rejected_request_full, (uint64_t)0u);
    check_equal(stats.actor.rejected_command_full, (uint64_t)0u);
    check_equal(stats.actor.stale_completions, (uint64_t)0u);
    check_equal(window.occupied, (size_t)0u);
    check_equal(window.demand_reserved, (size_t)0u);
    check_equal(window.results_ready, (size_t)0u);
    check_equal(fixture.prepared, fixture.submitted);
    check_equal(fixture.submitted, fixture.encoded);
    check_equal(fixture.encoded, fixture.coroutine_values);
    check_equal(fixture.coroutine_values, fixture.released);
    printf("CFLOW_IO_MODEL_BENCH_JSON "
           "{\"schema\":\"cflow-io-model-benchmark/v1\","
           "\"model\":\"coroutine-source-adapter\",\"capacity\":%zu,"
           "\"values_per_sample\":%zu,\"samples\":%zu,"
           "\"timed_values\":%" PRIu64 ",\"processed_values\":%zu,"
           "\"accepted\":%" PRIu64 ",\"acknowledged\":%" PRIu64 ","
           "\"drive_calls\":%zu,\"driver_calls\":%zu,"
           "\"peak_occupied\":%zu,\"added_worker_threads\":0,"
           "\"errors\":0,\"rejections\":0,"
           "\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           (uint64_t)samples * (uint64_t)values_per_sample,
           fixture.coroutine_values, stats.actor.accepted,
           stats.actor.acknowledged, fixture.drive_calls,
           fixture.driver_calls, window.peak_occupied);
    check_equal(io_source_benchmark_destroy(&fixture), TURBO_OK);
}
#endif

bench("CFlow windowed IO source control path") {
    size_t capacity = 0u;
    size_t samples = 0u;
    size_t values_per_sample = 0u;
    io_source_benchmark_fixture fixture;
    cflow_io_source_stats stats = {0};
    cflow_io_source_window_stats window = {0};
    char title[96];
    int init_status;
    int run_status;
    const bool config_valid = io_source_benchmark_env(
            "CFLOW_IO_SOURCE_WINDOW", 1u,
            CFLOW_IO_SOURCE_MAX_WINDOW, &capacity) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_SAMPLES", IO_SOURCE_BENCH_DEFAULT_SAMPLES,
            IO_SOURCE_BENCH_MAX_SAMPLES, &samples) &&
        io_source_benchmark_env(
            "CFLOW_IO_SOURCE_VALUES", IO_SOURCE_BENCH_DEFAULT_VALUES,
            IO_SOURCE_BENCH_MAX_VALUES, &values_per_sample);

    check_true(config_valid);
    if (!config_valid)
        return;
    memset(&fixture, 0, sizeof(fixture));
    init_status = io_source_benchmark_init(&fixture, capacity);
    check_equal(init_status, TURBO_OK);
    if (init_status != TURBO_OK)
        return;
    check_equal(io_source_benchmark_run_batch(
        &fixture, values_per_sample), TURBO_OK);
    check_equal(fixture.prepared, values_per_sample);
    check_equal(fixture.submitted, values_per_sample);
    check_equal(fixture.encoded, values_per_sample);
    check_equal(fixture.released, values_per_sample);
    check_equal(fixture.sink_values, values_per_sample);

    run_status = TURBO_OK;
    (void)snprintf(title, sizeof(title),
                   "mock Source runtime window=%zu values=%zu", capacity,
                   values_per_sample);
    benchmark_ops(title, samples, values_per_sample) {
        if (run_status == TURBO_OK)
            run_status = io_source_benchmark_run_batch(
                &fixture, values_per_sample);
    }

    check_equal(run_status, TURBO_OK);
    check_true(cflow_io_source_owner_get_stats(&fixture.owner, &stats));
    check_true(cflow_io_source_owner_get_window_stats(
        &fixture.owner, &window));
    check_equal(stats.actor.rejected_request_full, (uint64_t)0u);
    check_equal(stats.actor.rejected_command_full, (uint64_t)0u);
    check_equal(stats.actor.rejected_closed, (uint64_t)0u);
    check_equal(stats.actor.rejected_lease_in_use, (uint64_t)0u);
    check_equal(stats.actor.stale_completions, (uint64_t)0u);
    check_equal(window.occupied, (size_t)0u);
    check_equal(window.demand_reserved, (size_t)0u);
    check_equal(window.results_ready, (size_t)0u);
    check_equal(fixture.driver_calls,
                fixture.drive_calls -
                    (fixture.drive_scheduled ? 1u : 0u));
    check_equal(fixture.sink_values,
                (samples + 1u) * values_per_sample);
    printf("CFLOW_IO_SOURCE_BENCH_JSON "
           "{\"schema\":\"cflow-io-source-benchmark/v1\","
           "\"capacity\":%zu,\"values_per_sample\":%zu,"
           "\"samples\":%zu,\"drive_calls\":%zu,"
           "\"driver_calls\":%zu,\"pending_drive_credit\":%u,"
           "\"peak_occupied\":%zu,"
           "\"processed_values\":%zu,"
           "\"errors\":0,\"rejections\":0,"
           "\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           fixture.drive_calls, fixture.driver_calls,
           fixture.drive_scheduled ? 1u : 0u,
           window.peak_occupied, fixture.sink_values);
    printf("CFLOW_IO_MODEL_BENCH_JSON "
           "{\"schema\":\"cflow-io-model-benchmark/v1\","
           "\"model\":\"source-runtime\",\"capacity\":%zu,"
           "\"values_per_sample\":%zu,\"samples\":%zu,"
           "\"timed_values\":%" PRIu64 ",\"processed_values\":%zu,"
           "\"accepted\":%" PRIu64 ",\"acknowledged\":%" PRIu64 ","
           "\"drive_calls\":%zu,\"driver_calls\":%zu,"
           "\"peak_occupied\":%zu,\"errors\":0,\"rejections\":0,"
           "\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           (uint64_t)samples * (uint64_t)values_per_sample,
           fixture.sink_values, stats.actor.accepted,
           stats.actor.acknowledged, fixture.drive_calls,
           fixture.driver_calls, window.peak_occupied);
    check_equal(io_source_benchmark_destroy(&fixture), TURBO_OK);
}
}
