#ifndef CFLOW_NATIVE_EXAMPLE_RUNTIME_H
#define CFLOW_NATIVE_EXAMPLE_RUNTIME_H

#include <cflow/io_native.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    CFLOW_NATIVE_EXAMPLE_CAPACITY = 2,
    CFLOW_NATIVE_EXAMPLE_MAX_STEPS = 64
};

static const uint64_t CFLOW_NATIVE_EXAMPLE_TIMEOUT_NS =
    UINT64_C(5000000000);

typedef struct cflow_native_example_completion_log {
    cflow_io_request_id request_ids[CFLOW_NATIVE_EXAMPLE_CAPACITY];
    cflow_io_completion completions[CFLOW_NATIVE_EXAMPLE_CAPACITY];
    size_t count;
    size_t acknowledged;
    bool overflow;
} cflow_native_example_completion_log;

typedef struct cflow_native_example_runtime {
    cflow_io_native_backend backend;
    cflow_executor executor;
    cflow_io_actor actor;
    cflow_native_example_completion_log log;
    bool backend_initialized;
    bool executor_initialized;
    bool actor_initialized;
} cflow_native_example_runtime;

static void cflow_native_example_completed(
    void *user, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user,
    const cflow_io_completion *completion) {
    cflow_native_example_completion_log *log =
        (cflow_native_example_completion_log *)user;
    (void)lease_id;
    (void)operation_user;
    if (log == NULL || completion == NULL)
        return;
    if (log->count >= CFLOW_NATIVE_EXAMPLE_CAPACITY) {
        log->overflow = true;
        return;
    }
    log->request_ids[log->count] = request_id;
    log->completions[log->count] = *completion;
    ++log->count;
}

static int cflow_native_example_runtime_init(
    cflow_native_example_runtime *runtime,
    cflow_io_native_backend_kind backend_kind,
    cflow_io_backend_ops backend_ops) {
    cflow_io_native_backend_config backend_config = {
        backend_kind,
        CFLOW_NATIVE_EXAMPLE_CAPACITY,
        CFLOW_NATIVE_EXAMPLE_CAPACITY};
    cflow_io_actor_config actor_config;
    int status;

    if (runtime == NULL || backend_ops.submit == NULL)
        return TURBO_EINVAL;
    memset(runtime, 0, sizeof(*runtime));
    if (!cflow_io_native_backend_supported(backend_kind))
        return TURBO_ENOTSUP;
    status = cflow_io_native_backend_init(&runtime->backend, &backend_config);
    if (status != TURBO_OK)
        return status;
    runtime->backend_initialized = true;
    if (!cflow_executor_manual_init_with_capacity(
            &runtime->executor, CFLOW_NATIVE_EXAMPLE_CAPACITY))
        return TURBO_ENOMEM;
    runtime->executor_initialized = true;

    memset(&actor_config, 0, sizeof(actor_config));
    actor_config.request_capacity = CFLOW_NATIVE_EXAMPLE_CAPACITY;
    actor_config.command_capacity = CFLOW_NATIVE_EXAMPLE_CAPACITY;
    actor_config.executor = &runtime->executor;
    actor_config.backend = backend_ops;
    actor_config.backend_user = &runtime->backend;
    actor_config.completion = cflow_native_example_completed;
    actor_config.completion_user = &runtime->log;
    status = cflow_io_actor_init(&runtime->actor, &actor_config);
    if (status == TURBO_OK)
        runtime->actor_initialized = true;
    return status;
}

static int cflow_native_example_acknowledge_ready(
    cflow_native_example_runtime *runtime) {
    while (runtime->log.acknowledged < runtime->log.count) {
        const cflow_io_request_id request_id =
            runtime->log.request_ids[runtime->log.acknowledged];
        if (cflow_io_actor_acknowledge(&runtime->actor, request_id) !=
            CFLOW_IO_ACK_RELEASED)
            return TURBO_EPROTO;
        ++runtime->log.acknowledged;
    }
    return runtime->log.overflow ? TURBO_ERANGE : TURBO_OK;
}

static int cflow_native_example_drive_once(
    cflow_native_example_runtime *runtime, size_t *progressed) {
    const cflow_io_run_result actor_result =
        cflow_io_actor_run_ready(
            &runtime->actor, CFLOW_NATIVE_EXAMPLE_MAX_STEPS);
    const size_t executor_progress =
        cflow_executor_run_ready(&runtime->executor);
    int status;

    if (actor_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT)
        return TURBO_EINVAL;
    if (actor_result.status == CFLOW_IO_RUN_BUSY)
        return TURBO_EBUSY;
    status = cflow_native_example_acknowledge_ready(runtime);
    if (status != TURBO_OK)
        return status;
    if (progressed != NULL)
        *progressed = actor_result.progressed + executor_progress;
    return TURBO_OK;
}

static int cflow_native_example_drive_until(
    cflow_native_example_runtime *runtime, size_t expected_completions) {
    const uint64_t started = turbo_hrtime();
    if (runtime == NULL || !runtime->actor_initialized ||
        expected_completions > CFLOW_NATIVE_EXAMPLE_CAPACITY)
        return TURBO_EINVAL;
    while (runtime->log.count < expected_completions ||
           runtime->log.acknowledged < runtime->log.count) {
        size_t progressed = 0u;
        const int status =
            cflow_native_example_drive_once(runtime, &progressed);
        if (status != TURBO_OK)
            return status;
        if (turbo_hrtime() - started >= CFLOW_NATIVE_EXAMPLE_TIMEOUT_NS)
            return TURBO_ETIMEDOUT;
        if (progressed == 0u)
            turbo_thread_yield();
    }
    return TURBO_OK;
}

static int cflow_native_example_close_actor(
    cflow_native_example_runtime *runtime) {
    const uint64_t started = turbo_hrtime();
    int first_error = TURBO_OK;
    int status;

    if (runtime == NULL || !runtime->actor_initialized)
        return TURBO_OK;
    status = cflow_io_actor_close(&runtime->actor);
    if (status != TURBO_OK && status != TURBO_EALREADY)
        first_error = status;
    while (!cflow_io_actor_is_quiescent(&runtime->actor)) {
        size_t progressed = 0u;
        status = cflow_native_example_drive_once(runtime, &progressed);
        if (status != TURBO_OK && first_error == TURBO_OK)
            first_error = status;
        if (status != TURBO_OK ||
            turbo_hrtime() - started >= CFLOW_NATIVE_EXAMPLE_TIMEOUT_NS) {
            if (first_error == TURBO_OK)
                first_error = TURBO_ETIMEDOUT;
            break;
        }
        if (progressed == 0u)
            turbo_thread_yield();
    }
    status = cflow_io_actor_destroy(&runtime->actor);
    if (status == TURBO_OK)
        runtime->actor_initialized = false;
    else if (first_error == TURBO_OK)
        first_error = status;
    return first_error;
}

static int cflow_native_example_destroy_runtime(
    cflow_native_example_runtime *runtime) {
    int first_error = TURBO_OK;
    int status;
    if (runtime == NULL)
        return TURBO_EINVAL;
    if (runtime->actor_initialized) {
        status = cflow_native_example_close_actor(runtime);
        if (status != TURBO_OK)
            first_error = status;
    }
    if (runtime->backend_initialized) {
        status = cflow_io_native_backend_shutdown(&runtime->backend);
        if (status != TURBO_OK && status != TURBO_EALREADY &&
            first_error == TURBO_OK)
            first_error = status;
        if (status == TURBO_OK || status == TURBO_EALREADY) {
            status = cflow_io_native_backend_destroy(&runtime->backend);
            if (status == TURBO_OK)
                runtime->backend_initialized = false;
            else if (first_error == TURBO_OK)
                first_error = status;
        }
    }
    if (runtime->executor_initialized) {
        if (!cflow_executor_shutdown(&runtime->executor) &&
            first_error == TURBO_OK)
            first_error = TURBO_EBUSY;
        cflow_executor_destroy(&runtime->executor);
        runtime->executor_initialized = false;
    }
    return first_error;
}

#endif /* CFLOW_NATIVE_EXAMPLE_RUNTIME_H */
