#include <cflow/cflow.h>

#include "tinytest.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    REACTIVE_BENCH_DEFAULT_SAMPLES = 50,
    REACTIVE_BENCH_DEFAULT_VALUES = 4096,
    REACTIVE_BENCH_MAX_SAMPLES = 1000,
    REACTIVE_BENCH_MAX_VALUES = 1024 * 1024,
    REACTIVE_BENCH_DRIVER_STEPS = 64,
    REACTIVE_BENCH_LOOP_FACTOR = 16,
    REACTIVE_BENCH_LOOP_MARGIN = 64
};

typedef struct reactive_benchmark_fixture {
    cflow_graph surface;
    cflow_graph normalized;
    cflow_scheduler scheduler;
    cflow_publisher publisher;
    cflow_io_publisher_owner owner;
    cflow_subscription subscription;
    cflow_subscriber_callbacks subscriber_callbacks;
    cflow_subscriber subscriber;
    size_t prepared;
    size_t submitted;
    size_t encoded;
    size_t released;
    size_t subscriber_values;
    size_t drive_calls;
    size_t driver_calls;
    const char *subscriber_error;
    bool surface_initialized;
    bool normalized_initialized;
    bool scheduler_initialized;
    bool owner_initialized;
    bool subscription_initialized;
} reactive_benchmark_fixture;

static bool reactive_benchmark_env(
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

static void reactive_benchmark_release(void *user) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;
    if (fixture != NULL)
        ++fixture->released;
}

static cflow_io_publisher_prepare_status reactive_benchmark_prepare(
    void *user, cflow_io_operation *operation, const char **error) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;

    (void)error;
    if (fixture == NULL || operation == NULL)
        return CFLOW_IO_PUBLISHER_PREPARE_ERROR;
    ++fixture->prepared;
    operation->user = fixture;
    operation->release = reactive_benchmark_release;
    return CFLOW_IO_PUBLISHER_PREPARE_OPERATION;
}

static cflow_read_status reactive_benchmark_encode(
    void *user, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user,
    const cflow_io_completion *completion, void *out_value,
    const char **error) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;

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

static int reactive_benchmark_submit(
    void *user, cflow_io_actor *actor,
    cflow_io_request_id request_id, cflow_io_lease_id lease_id,
    void *operation_user) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;
    const cflow_io_completion completion = {
        CFLOW_IO_COMPLETION_OK, sizeof(int), SALTS_OK};

    (void)lease_id;
    (void)operation_user;
    if (fixture == NULL || actor == NULL)
        return SALTS_EINVAL;
    ++fixture->submitted;
    return cflow_io_actor_complete(actor, request_id, &completion) ==
                   CFLOW_IO_COMPLETE_ACCEPTED
        ? SALTS_OK : SALTS_EPROTO;
}

static void reactive_benchmark_drive(void *user) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;
    if (fixture != NULL)
        ++fixture->drive_calls;
}

static bool reactive_benchmark_subscriber_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;
    if (fixture == NULL || value == NULL ||
        !cmeta_type_equal(type, &cmeta_type_int) ||
        *(const int *)value != 1)
        return false;
    ++fixture->subscriber_values;
    return true;
}

static void reactive_benchmark_subscriber_error(
    void *user, const char *message) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;
    if (fixture != NULL)
        fixture->subscriber_error = message;
}

static void reactive_benchmark_subscriber_done(void *user) {
    reactive_benchmark_fixture *fixture =
        (reactive_benchmark_fixture *)user;
    if (fixture != NULL && fixture->subscriber_error == NULL)
        fixture->subscriber_error = "benchmark Reactive terminated unexpectedly";
}

static int reactive_benchmark_destroy(
    reactive_benchmark_fixture *fixture) {
    int status = SALTS_OK;

    if (fixture == NULL)
        return SALTS_EINVAL;
    if (fixture->subscription_initialized) {
        cflow_subscription_close(&fixture->subscription);
        fixture->subscription_initialized = false;
    } else if (cflow_publisher_valid(&fixture->publisher)) {
        cflow_publisher_destroy(&fixture->publisher);
    }
    if (fixture->owner_initialized) {
        int close_status;

        while (!cflow_io_publisher_owner_is_quiescent(&fixture->owner)) {
            size_t progressed = 0u;
            const int drive_status = cflow_io_publisher_owner_run_ready(
                &fixture->owner, REACTIVE_BENCH_DRIVER_STEPS,
                &progressed);
            if (drive_status != SALTS_OK && status == SALTS_OK)
                status = drive_status;
            if (drive_status != SALTS_OK || progressed == 0u)
                break;
        }
        close_status = cflow_io_publisher_owner_close(&fixture->owner);
        if (status == SALTS_OK)
            status = close_status;
        if (close_status == SALTS_OK)
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

static int reactive_benchmark_init(
    reactive_benchmark_fixture *fixture, size_t capacity) {
    cflow_io_publisher_config config = {0};
    int status = SALTS_ENOMEM;

    if (fixture == NULL || capacity == 0u ||
        capacity > CFLOW_IO_PUBLISHER_MAX_WINDOW)
        return SALTS_EINVAL;
    memset(fixture, 0, sizeof(*fixture));
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

    config.name = "windowed-publisher-benchmark";
    config.type = &cmeta_type_int;
    config.backend.submit = reactive_benchmark_submit;
    config.backend_user = fixture;
    config.prepare = reactive_benchmark_prepare;
    config.encode = reactive_benchmark_encode;
    config.user = fixture;
    config.drive = reactive_benchmark_drive;
    config.drive_user = fixture;
    status = cflow_publisher_from_io_actor_windowed(
        &fixture->publisher, &fixture->owner, &config, capacity);
    if (status != SALTS_OK)
        goto cleanup;
    fixture->owner_initialized = true;
    fixture->subscriber_callbacks = (cflow_subscriber_callbacks){
        reactive_benchmark_subscriber_value,
        reactive_benchmark_subscriber_error,
        reactive_benchmark_subscriber_done,
        fixture};
    fixture->subscriber = cflow_subscriber_from_callbacks(
        &fixture->subscriber_callbacks);
    if (!cflow_subscribe(
            &fixture->subscription, &fixture->normalized, &fixture->publisher,
            &fixture->scheduler, &fixture->subscriber)) {
        status = SALTS_EIO;
        goto cleanup;
    }
    fixture->subscription_initialized = true;
    return SALTS_OK;

cleanup:
    (void)reactive_benchmark_destroy(fixture);
    return status;
}

static int reactive_benchmark_run_batch(
    reactive_benchmark_fixture *fixture, size_t values) {
    size_t target;
    size_t loop_limit;

    if (fixture == NULL || values == 0u)
        return SALTS_EINVAL;
    target = fixture->subscriber_values + values;
    loop_limit = values <=
            (SIZE_MAX - REACTIVE_BENCH_LOOP_MARGIN) /
                REACTIVE_BENCH_LOOP_FACTOR
        ? values * REACTIVE_BENCH_LOOP_FACTOR +
              REACTIVE_BENCH_LOOP_MARGIN
        : SIZE_MAX;
    size_t loops = 0u;

    if (target < fixture->subscriber_values ||
        !cflow_subscription_request(&fixture->subscription, values))
        return SALTS_EINVAL;
    while (fixture->subscriber_values < target && loops < loop_limit) {
        size_t progressed = 0u;
        int status;

        (void)cflow_scheduler_run_until_idle(&fixture->scheduler, 0u);
        ++fixture->driver_calls;
        status = cflow_io_publisher_owner_run_ready(
            &fixture->owner, REACTIVE_BENCH_DRIVER_STEPS,
            &progressed);
        if (status != SALTS_OK)
            return status;
        (void)cflow_scheduler_run_until_idle(&fixture->scheduler, 0u);
        if (fixture->subscriber_error != NULL ||
            cflow_subscription_error(&fixture->subscription) != NULL)
            return SALTS_EIO;
        if (progressed == 0u && fixture->subscriber_values < target)
            return SALTS_EPROTO;
        ++loops;
    }
    return fixture->subscriber_values == target
        ? SALTS_OK : SALTS_ETIMEDOUT;
}

suite("CFlow Reactive IO benchmarks") {
bench("CFlow Reactive Subscription control path") {
    size_t capacity = 0u;
    size_t samples = 0u;
    size_t values_per_sample = 0u;
    reactive_benchmark_fixture fixture;
    cflow_io_publisher_stats stats = {0};
    cflow_io_publisher_window_stats window = {0};
    char title[96];
    int init_status;
    int run_status;
    const bool config_valid = reactive_benchmark_env(
            "CFLOW_IO_PUBLISHER_WINDOW", 1u,
            CFLOW_IO_PUBLISHER_MAX_WINDOW, &capacity) &&
        reactive_benchmark_env(
            "CFLOW_IO_PUBLISHER_SAMPLES", REACTIVE_BENCH_DEFAULT_SAMPLES,
            REACTIVE_BENCH_MAX_SAMPLES, &samples) &&
        reactive_benchmark_env(
            "CFLOW_IO_PUBLISHER_VALUES", REACTIVE_BENCH_DEFAULT_VALUES,
            REACTIVE_BENCH_MAX_VALUES, &values_per_sample);

    check_true(config_valid);
    if (!config_valid)
        return;
    memset(&fixture, 0, sizeof(fixture));
    init_status = reactive_benchmark_init(&fixture, capacity);
    check_equal(init_status, SALTS_OK);
    if (init_status != SALTS_OK)
        return;
    check_equal(reactive_benchmark_run_batch(
        &fixture, values_per_sample), SALTS_OK);
    check_equal(fixture.prepared, values_per_sample);
    check_equal(fixture.submitted, values_per_sample);
    check_equal(fixture.encoded, values_per_sample);
    check_equal(fixture.released, values_per_sample);
    check_equal(fixture.subscriber_values, values_per_sample);

    run_status = SALTS_OK;
    (void)snprintf(title, sizeof(title),
                   "window=%zu values=%zu", capacity,
                   values_per_sample);
    benchmark_ops(title, samples, values_per_sample) {
        if (run_status == SALTS_OK)
            run_status = reactive_benchmark_run_batch(
                &fixture, values_per_sample);
    }

    check_equal(run_status, SALTS_OK);
    check_true(cflow_io_publisher_owner_get_stats(&fixture.owner, &stats));
    check_true(cflow_io_publisher_owner_get_window_stats(
        &fixture.owner, &window));
    check_equal(stats.actor.rejected_request_full, (uint64_t)0u);
    check_equal(stats.actor.rejected_command_full, (uint64_t)0u);
    check_equal(stats.actor.rejected_closed, (uint64_t)0u);
    check_equal(stats.actor.rejected_lease_in_use, (uint64_t)0u);
    check_equal(stats.actor.stale_completions, (uint64_t)0u);
    check_equal(window.occupied, (size_t)0u);
    check_equal(window.demand_reserved, (size_t)0u);
    check_equal(window.results_ready, (size_t)0u);
    printf("CFLOW_IO_PUBLISHER_BENCH_JSON "
           "{\"capacity\":%zu,\"values_per_sample\":%zu,"
           "\"samples\":%zu,\"drive_calls\":%zu,"
           "\"driver_calls\":%zu,\"peak_occupied\":%zu,"
           "\"errors\":0,\"rejections\":0,"
           "\"stale_completions\":0}\n",
           capacity, values_per_sample, samples,
           fixture.drive_calls, fixture.driver_calls,
           window.peak_occupied);
    check_equal(reactive_benchmark_destroy(&fixture), SALTS_OK);
}
}
