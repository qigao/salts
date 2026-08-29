#include <cflow/io_blocking_driver.h>

#include <cflow/executor.h>

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdlib.h>
#include <string.h>

typedef enum cflow_io_blocking_slot_phase {
    CFLOW_IO_BLOCKING_SLOT_FREE = 0,
    CFLOW_IO_BLOCKING_SLOT_QUEUED,
    CFLOW_IO_BLOCKING_SLOT_RUNNING
} cflow_io_blocking_slot_phase;

typedef struct cflow_io_blocking_driver_impl cflow_io_blocking_driver_impl;

typedef struct cflow_io_blocking_slot {
    cflow_io_blocking_driver_impl *owner;
    cflow_io_actor *actor;
    cflow_io_request_id request_id;
    cflow_io_blocking_job *job;
    cflow_io_blocking_slot_phase phase;
    bool cancel_requested;
} cflow_io_blocking_slot;

struct cflow_io_blocking_driver_impl {
    turbo_mutex_t gate;
    cflow_executor executor;
    cflow_executor_control control;
    cflow_io_blocking_slot *slots;
    size_t capacity;
    size_t active;
    size_t queued;
    size_t running;
    uint64_t accepted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t rejected_closed;
    uint64_t publication_errors;
    cflow_io_blocking_driver_lifecycle lifecycle;
};

static void blocking_counter_increment(uint64_t *counter) {
    if (*counter != UINT64_MAX)
        ++*counter;
}

static bool blocking_completion_valid(
    const cflow_io_completion *completion) {
    if (completion == NULL ||
        completion->kind > CFLOW_IO_COMPLETION_FAILED)
        return false;
    switch (completion->kind) {
        case CFLOW_IO_COMPLETION_OK:
            return completion->error == TURBO_OK;
        case CFLOW_IO_COMPLETION_EOF:
        case CFLOW_IO_COMPLETION_CANCELLED:
            return completion->bytes == 0u &&
                   completion->error == TURBO_OK;
        case CFLOW_IO_COMPLETION_FAILED:
            return completion->bytes == 0u &&
                   completion->error != TURBO_OK;
    }
    return false;
}

static void blocking_publish(cflow_io_blocking_slot *slot,
                             const cflow_io_completion *completion,
                             bool cancelled) {
    cflow_io_blocking_driver_impl *impl = slot->owner;
    const cflow_io_complete_status status = cflow_io_actor_complete(
        slot->actor, slot->request_id, completion);
    turbo_mutex_lock(&impl->gate);
    if (status != CFLOW_IO_COMPLETE_ACCEPTED)
        blocking_counter_increment(&impl->publication_errors);
    else if (cancelled)
        blocking_counter_increment(&impl->cancelled);
    else
        blocking_counter_increment(&impl->completed);
    turbo_mutex_unlock(&impl->gate);
}

static void blocking_task_run(void *user) {
    cflow_io_blocking_slot *slot = (cflow_io_blocking_slot *)user;
    cflow_io_blocking_driver_impl *impl = slot->owner;
    cflow_io_blocking_job *job;
    bool cancelled;
    int status;
    cflow_io_completion completion = {
        CFLOW_IO_COMPLETION_FAILED, 0u, TURBO_EPROTO};

    turbo_mutex_lock(&impl->gate);
    cancelled = slot->cancel_requested;
    slot->phase = CFLOW_IO_BLOCKING_SLOT_RUNNING;
    --impl->queued;
    ++impl->running;
    job = slot->job;
    turbo_mutex_unlock(&impl->gate);

    if (cancelled) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
        blocking_publish(slot, &completion, true);
        return;
    }

    status = job->execute(job->user, &completion);
    if (status != TURBO_OK) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, status};
    } else if (!blocking_completion_valid(&completion)) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, TURBO_EPROTO};
    }
    blocking_publish(slot, &completion,
                     completion.kind == CFLOW_IO_COMPLETION_CANCELLED);
}

static void blocking_task_cancel(void *user) {
    cflow_io_blocking_slot *slot = (cflow_io_blocking_slot *)user;
    cflow_io_blocking_driver_impl *impl = slot->owner;
    const cflow_io_completion completion = {
        CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};

    turbo_mutex_lock(&impl->gate);
    slot->cancel_requested = true;
    turbo_mutex_unlock(&impl->gate);
    blocking_publish(slot, &completion, true);
}

static void blocking_task_finalize(void *user) {
    cflow_io_blocking_slot *slot = (cflow_io_blocking_slot *)user;
    cflow_io_blocking_driver_impl *impl = slot->owner;
    turbo_mutex_lock(&impl->gate);
    if (slot->phase == CFLOW_IO_BLOCKING_SLOT_QUEUED)
        --impl->queued;
    else if (slot->phase == CFLOW_IO_BLOCKING_SLOT_RUNNING)
        --impl->running;
    --impl->active;
    slot->actor = NULL;
    slot->request_id = 0u;
    slot->job = NULL;
    slot->cancel_requested = false;
    slot->phase = CFLOW_IO_BLOCKING_SLOT_FREE;
    turbo_mutex_unlock(&impl->gate);
}

static cflow_io_blocking_slot *blocking_reserve_slot(
    cflow_io_blocking_driver_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->capacity; ++index) {
        if (impl->slots[index].phase == CFLOW_IO_BLOCKING_SLOT_FREE) {
            impl->slots[index].phase = CFLOW_IO_BLOCKING_SLOT_QUEUED;
            ++impl->active;
            ++impl->queued;
            return &impl->slots[index];
        }
    }
    return NULL;
}

static int blocking_backend_submit(
    void *user, cflow_io_actor *actor, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user) {
    cflow_io_blocking_driver_impl *impl =
        (cflow_io_blocking_driver_impl *)user;
    cflow_io_blocking_job *job = (cflow_io_blocking_job *)operation_user;
    cflow_io_blocking_slot *slot;
    cflow_admission_status admitted;
    cflow_executor_task task;
    (void)lease_id;

    if (impl == NULL || actor == NULL || request_id == 0u || job == NULL ||
        job->execute == NULL)
        return TURBO_EINVAL;

    turbo_mutex_lock(&impl->gate);
    if (impl->lifecycle != CFLOW_IO_BLOCKING_DRIVER_OPEN) {
        blocking_counter_increment(&impl->rejected_closed);
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ESHUTDOWN;
    }
    slot = blocking_reserve_slot(impl);
    if (slot == NULL) {
        blocking_counter_increment(&impl->rejected_full);
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOBUFS;
    }
    slot->actor = actor;
    slot->request_id = request_id;
    slot->job = job;
    slot->cancel_requested = false;
    task = (cflow_executor_task){
        .run = blocking_task_run,
        .cancel = blocking_task_cancel,
        .finalize = blocking_task_finalize,
        .user = slot,
    };
    admitted = cflow_executor_try_post_task(&impl->executor, &task);
    if (admitted == CFLOW_ADMISSION_ACCEPTED) {
        blocking_counter_increment(&impl->accepted);
        turbo_mutex_unlock(&impl->gate);
        return TURBO_OK;
    }

    --impl->active;
    --impl->queued;
    slot->actor = NULL;
    slot->request_id = 0u;
    slot->job = NULL;
    slot->phase = CFLOW_IO_BLOCKING_SLOT_FREE;
    if (admitted == CFLOW_ADMISSION_FULL)
        blocking_counter_increment(&impl->rejected_full);
    else if (admitted == CFLOW_ADMISSION_CLOSED)
        blocking_counter_increment(&impl->rejected_closed);
    turbo_mutex_unlock(&impl->gate);

    if (admitted == CFLOW_ADMISSION_FULL)
        return TURBO_ENOBUFS;
    if (admitted == CFLOW_ADMISSION_CLOSED)
        return TURBO_ESHUTDOWN;
    return TURBO_EINVAL;
}

static int blocking_backend_cancel(void *user,
                                   cflow_io_request_id request_id) {
    cflow_io_blocking_driver_impl *impl =
        (cflow_io_blocking_driver_impl *)user;
    size_t index;
    if (impl == NULL || request_id == 0u)
        return TURBO_EINVAL;
    turbo_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->capacity; ++index) {
        cflow_io_blocking_slot *slot = &impl->slots[index];
        if (slot->phase != CFLOW_IO_BLOCKING_SLOT_FREE &&
            slot->request_id == request_id) {
            slot->cancel_requested = true;
            break;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static const cflow_io_backend_ops blocking_backend_ops = {
    blocking_backend_submit,
    blocking_backend_cancel,
};

int cflow_io_blocking_driver_init(
    cflow_io_blocking_driver *driver,
    const cflow_io_blocking_driver_config *config) {
    cflow_io_blocking_driver_impl *impl;
    size_t index;
    if (driver == NULL || driver->impl != NULL || config == NULL ||
        config->workers == 0u || config->capacity == 0u ||
        config->capacity > SIZE_MAX / sizeof(cflow_io_blocking_slot))
        return TURBO_EINVAL;
    impl = (cflow_io_blocking_driver_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->slots = (cflow_io_blocking_slot *)calloc(
        config->capacity, sizeof(*impl->slots));
    if (impl->slots == NULL) {
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->capacity = config->capacity;
    impl->lifecycle = CFLOW_IO_BLOCKING_DRIVER_OPEN;
    turbo_mutex_init(&impl->gate);
    for (index = 0u; index < impl->capacity; ++index)
        impl->slots[index].owner = impl;
    if (!cflow_executor_worker_init_with_capacity(
            &impl->executor, config->workers, config->capacity) ||
        !cflow_executor_as_control(&impl->executor, &impl->control)) {
        cflow_executor_destroy(&impl->executor);
        turbo_mutex_destroy(&impl->gate);
        free(impl->slots);
        free(impl);
        return TURBO_ENOMEM;
    }
    driver->impl = impl;
    return TURBO_OK;
}

bool cflow_io_blocking_driver_as_backend(
    cflow_io_blocking_driver *driver, cflow_io_backend_ops *out_ops,
    void **out_user) {
    if (driver == NULL || driver->impl == NULL || out_ops == NULL ||
        out_user == NULL)
        return false;
    *out_ops = blocking_backend_ops;
    *out_user = driver->impl;
    return true;
}

int cflow_io_blocking_driver_close(cflow_io_blocking_driver *driver) {
    cflow_io_blocking_driver_impl *impl;
    if (driver == NULL || driver->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_io_blocking_driver_impl *)driver->impl;
    turbo_mutex_lock(&impl->gate);
    if (impl->lifecycle != CFLOW_IO_BLOCKING_DRIVER_OPEN) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EALREADY;
    }
    impl->lifecycle = CFLOW_IO_BLOCKING_DRIVER_CLOSING;
    turbo_mutex_unlock(&impl->gate);
    if (!cflow_executor_control_shutdown(
            &impl->control, CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING))
        return TURBO_EINVAL;
    return TURBO_OK;
}

bool cflow_io_blocking_driver_get_stats(
    const cflow_io_blocking_driver *driver,
    cflow_io_blocking_driver_stats *out) {
    cflow_io_blocking_driver_impl *impl;
    cflow_executor_protocol_stats executor_stats = {0};
    cflow_io_blocking_driver_lifecycle lifecycle;
    if (driver == NULL || driver->impl == NULL || out == NULL)
        return false;
    impl = (cflow_io_blocking_driver_impl *)driver->impl;
    (void)cflow_executor_control_get_stats(&impl->control, &executor_stats);
    turbo_mutex_lock(&impl->gate);
    lifecycle = impl->lifecycle;
    if (impl->lifecycle == CFLOW_IO_BLOCKING_DRIVER_CLOSING &&
        impl->active == 0u &&
        executor_stats.lifecycle == CFLOW_EXECUTOR_CLOSED)
        lifecycle = CFLOW_IO_BLOCKING_DRIVER_CLOSED;
    *out = (cflow_io_blocking_driver_stats){
        .capacity = impl->capacity,
        .active = impl->active,
        .queued = impl->queued,
        .running = impl->running,
        .accepted = impl->accepted,
        .completed = impl->completed,
        .cancelled = impl->cancelled,
        .rejected_full = impl->rejected_full,
        .rejected_closed = impl->rejected_closed,
        .publication_errors = impl->publication_errors,
        .lifecycle = lifecycle,
    };
    turbo_mutex_unlock(&impl->gate);
    return true;
}

int cflow_io_blocking_driver_destroy(cflow_io_blocking_driver *driver) {
    cflow_io_blocking_driver_impl *impl;
    cflow_io_blocking_driver_stats stats;
    if (driver == NULL || driver->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_io_blocking_driver_impl *)driver->impl;
    if (!cflow_io_blocking_driver_get_stats(driver, &stats))
        return TURBO_EINVAL;
    if (stats.lifecycle != CFLOW_IO_BLOCKING_DRIVER_CLOSED ||
        stats.active != 0u)
        return TURBO_EBUSY;
    driver->impl = NULL;
    cflow_executor_destroy(&impl->executor);
    turbo_mutex_destroy(&impl->gate);
    free(impl->slots);
    free(impl);
    return TURBO_OK;
}
