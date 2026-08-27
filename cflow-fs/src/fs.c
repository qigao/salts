#include <cflow/fs.h>

#include <cflow/executor.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef enum cflow_fs_slot_phase {
    CFLOW_FS_SLOT_FREE = 0,
    CFLOW_FS_SLOT_QUEUED,
    CFLOW_FS_SLOT_RUNNING,
    CFLOW_FS_SLOT_COMPLETE,
    CFLOW_FS_SLOT_DELIVERING
} cflow_fs_slot_phase;

typedef struct cflow_fs_impl cflow_fs_impl;

typedef struct cflow_fs_slot {
    cflow_fs_impl *owner;
    char *path;
    char *second_path;
    atomic_int phase;
    atomic_bool cancel_requested;
    uint64_t request_id;
    cflow_fs_operation_kind operation;
    union {
        turbo_fs_stat_t *stat_out;
        cflow_fs_dir_buffer *directory_out;
        int mode;
    } arguments;
    int result;
} cflow_fs_slot;

struct cflow_fs_impl {
    cflow_executor executor;
    cflow_executor_control control;
    cflow_fs_slot *slots;
    char *paths;
    size_t capacity;
    size_t path_capacity;
    turbo_mutex_t gate;
    uint64_t next_request_id;
    cflow_fs_completion_fn completion;
    void *completion_user;
    atomic_size_t accepted;
    atomic_size_t completed;
    atomic_size_t cancelled;
    atomic_size_t in_use;
    atomic_size_t rejected_full;
    atomic_size_t rejected_closed;
    atomic_bool close_requested;
    atomic_bool driver_active;
};

static cflow_fs_submit_result fs_submit_result(cflow_fs_submit_status status,
                                                uint64_t request_id) {
    cflow_fs_submit_result result = {
        .status = status,
        .request_id = request_id,
    };
    return result;
}

static bool fs_checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static bool fs_path_fits(const char *path, size_t capacity,
                         size_t *length_out) {
    size_t length = 0u;
    if (path == NULL || capacity == 0u)
        return false;
    while (length < capacity && path[length] != '\0')
        ++length;
    if (length == 0u || length == capacity)
        return false;
    if (length_out != NULL)
        *length_out = length;
    return true;
}

static void fs_slot_reset(cflow_fs_slot *slot) {
    slot->request_id = 0u;
    slot->operation = CFLOW_FS_STAT;
    memset(&slot->arguments, 0, sizeof(slot->arguments));
    slot->result = TURBO_OK;
    slot->path[0] = '\0';
    slot->second_path[0] = '\0';
    atomic_store(&slot->cancel_requested, false);
    atomic_store(&slot->phase, CFLOW_FS_SLOT_FREE);
}

static int fs_read_directory(cflow_fs_slot *slot) {
    cflow_fs_dir_buffer *out = slot->arguments.directory_out;
    turbo_fs_dir_t *directory = NULL;
    turbo_fs_dirent_t entry;
    size_t entry_count = 0u;
    size_t names_used = 0u;
    int status;

    out->entry_count = 0u;
    out->names_used = 0u;
    status = turbo_fs_opendir(slot->path, &directory);
    if (status != TURBO_OK)
        return status;
    for (;;) {
        size_t name_length;
        status = turbo_fs_readdir(directory, &entry);
        if (status <= 0)
            break;
        name_length = strlen(entry.name) + 1u;
        if (entry_count == out->entry_capacity ||
            name_length > out->names_capacity - names_used) {
            status = TURBO_ENOBUFS;
            break;
        }
        memcpy(out->names + names_used, entry.name, name_length);
        out->entries[entry_count].name = out->names + names_used;
        out->entries[entry_count].type = entry.type;
        names_used += name_length;
        ++entry_count;
    }
    {
        const int close_status = turbo_fs_closedir(directory);
        if (status == TURBO_OK && close_status != TURBO_OK)
            status = close_status;
    }
    if (status != TURBO_OK) {
        out->entry_count = 0u;
        out->names_used = 0u;
        return status;
    }
    out->entry_count = entry_count;
    out->names_used = names_used;
    return TURBO_OK;
}

static int fs_execute(cflow_fs_slot *slot) {
    switch (slot->operation) {
        case CFLOW_FS_STAT:
            return turbo_fs_stat(slot->path, slot->arguments.stat_out);
        case CFLOW_FS_LSTAT:
            return turbo_fs_lstat(slot->path, slot->arguments.stat_out);
        case CFLOW_FS_READ_DIRECTORY:
            return fs_read_directory(slot);
        case CFLOW_FS_MKDIR:
            return turbo_fs_mkdir(slot->path, slot->arguments.mode);
        case CFLOW_FS_RMDIR:
            return turbo_fs_rmdir(slot->path);
        case CFLOW_FS_RENAME:
            return turbo_fs_rename(slot->path, slot->second_path);
        case CFLOW_FS_UNLINK:
            return turbo_fs_unlink(slot->path);
        default:
            return TURBO_EINVAL;
    }
}

static void fs_worker_run(void *user) {
    cflow_fs_slot *slot = (cflow_fs_slot *)user;
    int expected = CFLOW_FS_SLOT_QUEUED;
    if (slot == NULL ||
        !atomic_compare_exchange_strong(&slot->phase, &expected,
                                        CFLOW_FS_SLOT_RUNNING))
        return;
    if (atomic_load(&slot->cancel_requested))
        slot->result = TURBO_ECANCELED;
    else
        slot->result = fs_execute(slot);
    atomic_store(&slot->phase, CFLOW_FS_SLOT_COMPLETE);
}

static void fs_worker_cancel(void *user) {
    cflow_fs_slot *slot = (cflow_fs_slot *)user;
    int expected = CFLOW_FS_SLOT_QUEUED;
    if (slot == NULL ||
        !atomic_compare_exchange_strong(&slot->phase, &expected,
                                        CFLOW_FS_SLOT_RUNNING))
        return;
    slot->result = TURBO_ECANCELED;
    atomic_store(&slot->phase, CFLOW_FS_SLOT_COMPLETE);
}

static cflow_fs_submit_result fs_submit(
    cflow_fs_service *service, cflow_fs_operation_kind operation,
    const char *path, const char *second_path, void *output, int mode) {
    cflow_fs_impl *impl;
    cflow_fs_slot *slot = NULL;
    cflow_executor_task task;
    cflow_admission_status admission;
    size_t path_length;
    size_t second_length = 0u;
    size_t index;
    uint64_t request_id;

    if (service == NULL || service->impl == NULL)
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    impl = (cflow_fs_impl *)service->impl;
    if (!fs_path_fits(path, impl->path_capacity, &path_length) ||
        (second_path != NULL &&
         !fs_path_fits(second_path, impl->path_capacity, &second_length)))
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    if (atomic_load(&impl->close_requested)) {
        atomic_fetch_add(&impl->rejected_closed, 1u);
        return fs_submit_result(CFLOW_FS_SUBMIT_CLOSED, 0u);
    }

    turbo_mutex_lock(&impl->gate);
    if (atomic_load(&impl->close_requested)) {
        turbo_mutex_unlock(&impl->gate);
        atomic_fetch_add(&impl->rejected_closed, 1u);
        return fs_submit_result(CFLOW_FS_SUBMIT_CLOSED, 0u);
    }
    if (impl->next_request_id == UINT64_MAX) {
        turbo_mutex_unlock(&impl->gate);
        return fs_submit_result(CFLOW_FS_SUBMIT_ID_EXHAUSTED, 0u);
    }
    for (index = 0u; index < impl->capacity; ++index) {
        if (atomic_load(&impl->slots[index].phase) == CFLOW_FS_SLOT_FREE) {
            slot = &impl->slots[index];
            break;
        }
    }
    if (slot == NULL) {
        turbo_mutex_unlock(&impl->gate);
        atomic_fetch_add(&impl->rejected_full, 1u);
        return fs_submit_result(CFLOW_FS_SUBMIT_FULL, 0u);
    }
    request_id = ++impl->next_request_id;
    memcpy(slot->path, path, path_length + 1u);
    if (second_path != NULL)
        memcpy(slot->second_path, second_path, second_length + 1u);
    else
        slot->second_path[0] = '\0';
    slot->request_id = request_id;
    slot->operation = operation;
    slot->result = TURBO_OK;
    atomic_store(&slot->cancel_requested, false);
    switch (operation) {
        case CFLOW_FS_STAT:
        case CFLOW_FS_LSTAT:
            slot->arguments.stat_out = (turbo_fs_stat_t *)output;
            break;
        case CFLOW_FS_READ_DIRECTORY:
            slot->arguments.directory_out = (cflow_fs_dir_buffer *)output;
            break;
        case CFLOW_FS_MKDIR:
            slot->arguments.mode = mode;
            break;
        default:
            memset(&slot->arguments, 0, sizeof(slot->arguments));
            break;
    }
    atomic_store(&slot->phase, CFLOW_FS_SLOT_QUEUED);
    atomic_fetch_add(&impl->in_use, 1u);
    turbo_mutex_unlock(&impl->gate);

    task = (cflow_executor_task){
        .run = fs_worker_run,
        .cancel = fs_worker_cancel,
        .finalize = NULL,
        .user = slot,
    };
    admission = cflow_executor_try_post_task(&impl->executor, &task);
    if (admission != CFLOW_ADMISSION_ACCEPTED) {
        turbo_mutex_lock(&impl->gate);
        fs_slot_reset(slot);
        atomic_fetch_sub(&impl->in_use, 1u);
        turbo_mutex_unlock(&impl->gate);
        if (admission == CFLOW_ADMISSION_FULL) {
            atomic_fetch_add(&impl->rejected_full, 1u);
            return fs_submit_result(CFLOW_FS_SUBMIT_FULL, 0u);
        }
        if (admission == CFLOW_ADMISSION_CLOSED) {
            atomic_fetch_add(&impl->rejected_closed, 1u);
            return fs_submit_result(CFLOW_FS_SUBMIT_CLOSED, 0u);
        }
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    }
    atomic_fetch_add(&impl->accepted, 1u);
    return fs_submit_result(CFLOW_FS_SUBMIT_ACCEPTED, request_id);
}

int cflow_fs_service_init(cflow_fs_service *service,
                          const cflow_fs_config *config) {
    cflow_fs_impl *impl;
    size_t slot_bytes;
    size_t paths_per_slot;
    size_t path_bytes;
    size_t index;
    bool executor_initialized;

    if (service == NULL || service->impl != NULL || config == NULL ||
        config->worker_count == 0u ||
        config->worker_count > (size_t)INT_MAX ||
        config->request_capacity == 0u || config->path_capacity < 2u ||
        config->completion == NULL ||
        !fs_checked_multiply(config->request_capacity,
                             sizeof(cflow_fs_slot), &slot_bytes) ||
        !fs_checked_multiply(config->path_capacity, 2u, &paths_per_slot) ||
        !fs_checked_multiply(config->request_capacity, paths_per_slot,
                             &path_bytes))
        return TURBO_EINVAL;
    impl = (cflow_fs_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->slots = (cflow_fs_slot *)calloc(1u, slot_bytes);
    impl->paths = (char *)calloc(1u, path_bytes);
    if (impl->slots == NULL || impl->paths == NULL) {
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return TURBO_ENOMEM;
    }
    turbo_mutex_init(&impl->gate);
    if (impl->gate == NULL) {
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->capacity = config->request_capacity;
    impl->path_capacity = config->path_capacity;
    impl->completion = config->completion;
    impl->completion_user = config->completion_user;
    for (index = 0u; index < impl->capacity; ++index) {
        impl->slots[index].owner = impl;
        impl->slots[index].path =
            impl->paths + index * paths_per_slot;
        impl->slots[index].second_path =
            impl->slots[index].path + impl->path_capacity;
        atomic_init(&impl->slots[index].phase, CFLOW_FS_SLOT_FREE);
        atomic_init(&impl->slots[index].cancel_requested, false);
    }
    atomic_init(&impl->close_requested, false);
    atomic_init(&impl->driver_active, false);
    atomic_init(&impl->accepted, 0u);
    atomic_init(&impl->completed, 0u);
    atomic_init(&impl->cancelled, 0u);
    atomic_init(&impl->in_use, 0u);
    atomic_init(&impl->rejected_full, 0u);
    atomic_init(&impl->rejected_closed, 0u);
    executor_initialized = cflow_executor_worker_init_with_capacity(
        &impl->executor, config->worker_count, config->request_capacity);
    if (!executor_initialized ||
        !cflow_executor_as_control(&impl->executor, &impl->control)) {
        if (executor_initialized)
            cflow_executor_destroy(&impl->executor);
        turbo_mutex_destroy(&impl->gate);
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return TURBO_ENOMEM;
    }
    service->impl = impl;
    return TURBO_OK;
}

cflow_fs_submit_result cflow_fs_try_stat(
    cflow_fs_service *service, const char *path, turbo_fs_stat_t *out) {
    if (out == NULL)
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, CFLOW_FS_STAT, path, NULL, out, 0);
}

cflow_fs_submit_result cflow_fs_try_lstat(
    cflow_fs_service *service, const char *path, turbo_fs_stat_t *out) {
    if (out == NULL)
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, CFLOW_FS_LSTAT, path, NULL, out, 0);
}

cflow_fs_submit_result cflow_fs_try_read_directory(
    cflow_fs_service *service, const char *path, cflow_fs_dir_buffer *out) {
    if (out == NULL || out->entries == NULL || out->entry_capacity == 0u ||
        out->names == NULL || out->names_capacity == 0u)
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, CFLOW_FS_READ_DIRECTORY, path, NULL, out, 0);
}

cflow_fs_submit_result cflow_fs_try_mkdir(
    cflow_fs_service *service, const char *path, int mode) {
    if (mode < 0 || mode > 0777)
        return fs_submit_result(CFLOW_FS_SUBMIT_INVALID_ARGUMENT, 0u);
    return fs_submit(service, CFLOW_FS_MKDIR, path, NULL, NULL, mode);
}

cflow_fs_submit_result cflow_fs_try_rmdir(
    cflow_fs_service *service, const char *path) {
    return fs_submit(service, CFLOW_FS_RMDIR, path, NULL, NULL, 0);
}

cflow_fs_submit_result cflow_fs_try_rename(
    cflow_fs_service *service, const char *old_path,
    const char *new_path) {
    return fs_submit(service, CFLOW_FS_RENAME, old_path, new_path, NULL, 0);
}

cflow_fs_submit_result cflow_fs_try_unlink(
    cflow_fs_service *service, const char *path) {
    return fs_submit(service, CFLOW_FS_UNLINK, path, NULL, NULL, 0);
}

cflow_fs_cancel_status cflow_fs_try_cancel(
    cflow_fs_service *service, uint64_t request_id) {
    cflow_fs_impl *impl;
    size_t index;
    if (service == NULL || service->impl == NULL || request_id == 0u)
        return CFLOW_FS_CANCEL_INVALID_ARGUMENT;
    impl = (cflow_fs_impl *)service->impl;
    turbo_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->capacity; ++index) {
        cflow_fs_slot *slot = &impl->slots[index];
        const int phase = atomic_load(&slot->phase);
        if (slot->request_id != request_id || phase == CFLOW_FS_SLOT_FREE)
            continue;
        if (phase == CFLOW_FS_SLOT_QUEUED) {
            atomic_store(&slot->cancel_requested, true);
            if (atomic_load(&slot->phase) != CFLOW_FS_SLOT_QUEUED) {
                turbo_mutex_unlock(&impl->gate);
                return CFLOW_FS_CANCEL_ALREADY_RUNNING;
            }
            turbo_mutex_unlock(&impl->gate);
            return CFLOW_FS_CANCEL_REQUESTED;
        }
        if (phase == CFLOW_FS_SLOT_RUNNING) {
            turbo_mutex_unlock(&impl->gate);
            return CFLOW_FS_CANCEL_ALREADY_RUNNING;
        }
        turbo_mutex_unlock(&impl->gate);
        return CFLOW_FS_CANCEL_NOT_FOUND;
    }
    turbo_mutex_unlock(&impl->gate);
    return atomic_load(&impl->close_requested)
        ? CFLOW_FS_CANCEL_CLOSED : CFLOW_FS_CANCEL_NOT_FOUND;
}

int cflow_fs_run_ready(cflow_fs_service *service, size_t max_completions,
                       size_t *completed) {
    cflow_fs_impl *impl;
    bool expected = false;
    size_t delivered = 0u;
    size_t index;
    if (service == NULL || service->impl == NULL ||
        max_completions == 0u || completed == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_impl *)service->impl;
    *completed = 0u;
    if (!atomic_compare_exchange_strong(&impl->driver_active, &expected, true))
        return TURBO_EBUSY;
    for (index = 0u; index < impl->capacity && delivered < max_completions;
         ++index) {
        cflow_fs_slot *slot = &impl->slots[index];
        int phase = CFLOW_FS_SLOT_COMPLETE;
        uint64_t request_id;
        cflow_fs_operation_kind operation;
        int result;
        if (!atomic_compare_exchange_strong(&slot->phase, &phase,
                                            CFLOW_FS_SLOT_DELIVERING))
            continue;
        request_id = slot->request_id;
        operation = slot->operation;
        result = slot->result;
        impl->completion(impl->completion_user, request_id, operation, result);
        if (result == TURBO_ECANCELED)
            atomic_fetch_add(&impl->cancelled, 1u);
        else
            atomic_fetch_add(&impl->completed, 1u);
        turbo_mutex_lock(&impl->gate);
        fs_slot_reset(slot);
        atomic_fetch_sub(&impl->in_use, 1u);
        turbo_mutex_unlock(&impl->gate);
        ++delivered;
    }
    atomic_store(&impl->driver_active, false);
    *completed = delivered;
    return TURBO_OK;
}

int cflow_fs_close(cflow_fs_service *service) {
    cflow_fs_impl *impl;
    bool expected = false;
    if (service == NULL || service->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_impl *)service->impl;
    if (!atomic_compare_exchange_strong(&impl->close_requested,
                                        &expected, true))
        return TURBO_EALREADY;
    if (!cflow_executor_control_shutdown(
            &impl->control, CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING)) {
        atomic_store(&impl->close_requested, false);
        return TURBO_EBUSY;
    }
    return TURBO_OK;
}

bool cflow_fs_is_quiescent(const cflow_fs_service *service) {
    const cflow_fs_impl *impl;
    cflow_executor_protocol_stats executor_stats = {0};
    if (service == NULL || service->impl == NULL)
        return false;
    impl = (const cflow_fs_impl *)service->impl;
    if (!atomic_load(&impl->close_requested) ||
        !cflow_executor_control_get_stats(
            (cflow_executor_control *)&impl->control, &executor_stats))
        return false;
    return executor_stats.lifecycle == CFLOW_EXECUTOR_CLOSED &&
           atomic_load(&impl->in_use) == 0u;
}

bool cflow_fs_get_stats(const cflow_fs_service *service,
                        cflow_fs_stats *out) {
    const cflow_fs_impl *impl;
    cflow_executor_protocol_stats executor_stats = {0};
    if (service == NULL || service->impl == NULL || out == NULL)
        return false;
    impl = (const cflow_fs_impl *)service->impl;
    if (!cflow_executor_control_get_stats(
            (cflow_executor_control *)&impl->control, &executor_stats))
        return false;
    *out = (cflow_fs_stats){
        .capacity = impl->capacity,
        .accepted = atomic_load(&impl->accepted),
        .running = executor_stats.running,
        .completed = atomic_load(&impl->completed),
        .cancelled = atomic_load(&impl->cancelled),
        .in_use = atomic_load(&impl->in_use),
        .rejected_full = atomic_load(&impl->rejected_full),
        .rejected_closed = atomic_load(&impl->rejected_closed),
        .lifecycle = !atomic_load(&impl->close_requested)
            ? CFLOW_FS_OPEN
            : (executor_stats.lifecycle == CFLOW_EXECUTOR_CLOSED &&
               atomic_load(&impl->in_use) == 0u
                   ? CFLOW_FS_CLOSED : CFLOW_FS_CLOSING),
    };
    return true;
}

int cflow_fs_destroy(cflow_fs_service *service) {
    cflow_fs_impl *impl;
    if (service == NULL || service->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_impl *)service->impl;
    if (atomic_load(&impl->driver_active) || !cflow_fs_is_quiescent(service))
        return TURBO_EBUSY;
    cflow_executor_destroy(&impl->executor);
    turbo_mutex_destroy(&impl->gate);
    free(impl->paths);
    free(impl->slots);
    free(impl);
    service->impl = NULL;
    return TURBO_OK;
}
