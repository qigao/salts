#include <cflow/io_file.h>

#include "io_native_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#if defined(interface)
#undef interface
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

typedef struct cflow_io_file_impl cflow_io_file_impl;

typedef struct cflow_io_file_slot {
    cflow_io_native_file_operation operation;
    cflow_io_file_impl *owner;
    cflow_io_request_id request_id;
    bool in_use;
    bool delivered;
} cflow_io_file_slot;

_Static_assert(offsetof(cflow_io_file_slot, operation) == 0u,
               "native operation must remain the slot's first member");

struct cflow_io_file_impl {
    cflow_io_native_backend backend;
    cflow_executor executor;
    cflow_io_actor actor;
    cflow_io_file_slot *slots;
    size_t slot_capacity;
    turbo_mutex_t gate;
    cflow_io_native_backend_kind backend_kind;
    uintptr_t native_handle;
    uint32_t open_flags;
    cflow_io_file_completion_fn completion;
    void *completion_user;
    bool close_requested;
    bool driver_active;
};

static const uint32_t CFLOW_IO_FILE_OPEN_FLAGS =
    CFLOW_IO_FILE_READ | CFLOW_IO_FILE_WRITE |
    CFLOW_IO_FILE_CREATE | CFLOW_IO_FILE_TRUNCATE;

static cflow_io_file_impl *file_impl(cflow_io_file *file) {
    return file != NULL ? (cflow_io_file_impl *)file->impl : NULL;
}

static const cflow_io_file_impl *file_const_impl(const cflow_io_file *file) {
    return file != NULL ? (const cflow_io_file_impl *)file->impl : NULL;
}

static bool file_config_valid(const cflow_io_file *file,
                              const char *path,
                              const cflow_io_file_config *config) {
    uint32_t access;
    if (file == NULL || file->impl != NULL || path == NULL || path[0] == '\0' ||
        config == NULL || config->request_capacity == 0u ||
        config->command_capacity == 0u ||
        config->completion_batch_capacity == 0u ||
        config->completion == NULL ||
        (config->open_flags & ~CFLOW_IO_FILE_OPEN_FLAGS) != 0u)
        return false;
    access = config->open_flags & (CFLOW_IO_FILE_READ | CFLOW_IO_FILE_WRITE);
    if (access == 0u)
        return false;
    if ((config->open_flags & (CFLOW_IO_FILE_CREATE |
                               CFLOW_IO_FILE_TRUNCATE)) != 0u &&
        (access & CFLOW_IO_FILE_WRITE) == 0u)
        return false;
    if ((config->open_flags & CFLOW_IO_FILE_CREATE) == 0u)
        return config->create_mode == 0u;
    return config->create_mode <= 0777u;
}

static bool file_access_supported(const cflow_io_file_config *config) {
    if ((config->open_flags & CFLOW_IO_FILE_READ) != 0u &&
        !cflow_io_native_backend_file_operation_supported(
            config->backend_kind, CFLOW_IO_NATIVE_FILE_READ_AT))
        return false;
    if ((config->open_flags & CFLOW_IO_FILE_WRITE) != 0u &&
        !cflow_io_native_backend_file_operation_supported(
            config->backend_kind, CFLOW_IO_NATIVE_FILE_WRITE_AT))
        return false;
    return true;
}

static int file_native_open(const char *path,
                            const cflow_io_file_config *config,
                            uintptr_t *handle_out) {
#if defined(_WIN32)
    DWORD access = 0u;
    DWORD disposition;
    HANDLE handle;
    if ((config->open_flags & CFLOW_IO_FILE_READ) != 0u)
        access |= GENERIC_READ;
    if ((config->open_flags & CFLOW_IO_FILE_WRITE) != 0u)
        access |= GENERIC_WRITE;
    if ((config->open_flags & CFLOW_IO_FILE_CREATE) != 0u)
        disposition = (config->open_flags & CFLOW_IO_FILE_TRUNCATE) != 0u
            ? CREATE_ALWAYS : OPEN_ALWAYS;
    else
        disposition = (config->open_flags & CFLOW_IO_FILE_TRUNCATE) != 0u
            ? TRUNCATE_EXISTING : OPEN_EXISTING;
    handle = CreateFileA(
        path, access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return -(int)GetLastError();
    *handle_out = (uintptr_t)handle;
    return TURBO_OK;
#else
    int access_flags;
    int flags;
    int descriptor;
    if ((config->open_flags & CFLOW_IO_FILE_READ) != 0u &&
        (config->open_flags & CFLOW_IO_FILE_WRITE) != 0u)
        access_flags = O_RDWR;
    else if ((config->open_flags & CFLOW_IO_FILE_WRITE) != 0u)
        access_flags = O_WRONLY;
    else
        access_flags = O_RDONLY;
    flags = access_flags;
    if ((config->open_flags & CFLOW_IO_FILE_CREATE) != 0u)
        flags |= O_CREAT;
    if ((config->open_flags & CFLOW_IO_FILE_TRUNCATE) != 0u)
        flags |= O_TRUNC;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    do {
        descriptor = open(path, flags, (mode_t)config->create_mode);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return -errno;
#if !defined(O_CLOEXEC)
    if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
        const int status = -errno;
        (void)close(descriptor);
        return status;
    }
#endif
    *handle_out = (uintptr_t)descriptor;
    return TURBO_OK;
#endif
}

static int file_native_close(uintptr_t handle) {
#if defined(_WIN32)
    return CloseHandle((HANDLE)handle) ? TURBO_OK : -(int)GetLastError();
#else
    return close((int)handle) == 0 ? TURBO_OK : -errno;
#endif
}

static void file_slot_release(void *operation_user) {
    cflow_io_file_slot *slot = (cflow_io_file_slot *)operation_user;
    cflow_io_file_impl *impl;
    if (slot == NULL || slot->owner == NULL)
        return;
    impl = slot->owner;
    turbo_mutex_lock(&impl->gate);
    memset(&slot->operation, 0, sizeof(slot->operation));
    slot->request_id = 0u;
    slot->in_use = false;
    slot->delivered = false;
    turbo_mutex_unlock(&impl->gate);
}

static void file_actor_completion(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion) {
    cflow_io_file_impl *impl = (cflow_io_file_impl *)user;
    cflow_io_file_slot *slot = (cflow_io_file_slot *)operation_user;
    if (impl == NULL || slot == NULL || completion == NULL)
        return;
    impl->completion(impl->completion_user, request_id, lease_id,
                     slot->operation.kind, completion);
    turbo_mutex_lock(&impl->gate);
    slot->delivered = true;
    turbo_mutex_unlock(&impl->gate);
}

static void file_init_cleanup(cflow_io_file_impl *impl,
                              bool backend_initialized,
                              bool executor_initialized,
                              bool actor_initialized) {
    if (impl == NULL)
        return;
    if (actor_initialized) {
        (void)cflow_io_actor_close(&impl->actor);
        (void)cflow_io_actor_destroy(&impl->actor);
    }
    if (backend_initialized) {
        const int status = cflow_io_native_backend_shutdown(&impl->backend);
        if (status == TURBO_OK || status == TURBO_EALREADY)
            (void)cflow_io_native_backend_destroy(&impl->backend);
    }
    if (executor_initialized) {
        (void)cflow_executor_shutdown(&impl->executor);
        cflow_executor_destroy(&impl->executor);
    }
    turbo_mutex_destroy(&impl->gate);
    free(impl->slots);
    free(impl);
}

int cflow_io_file_open(cflow_io_file *file,
                       const char *path,
                       const cflow_io_file_config *config) {
    cflow_io_file_impl *impl;
    cflow_io_native_backend_config backend_config;
    cflow_io_actor_config actor_config;
    bool backend_initialized = false;
    bool executor_initialized = false;
    bool actor_initialized = false;
    int status;
    size_t index;

    if (!file_config_valid(file, path, config))
        return TURBO_EINVAL;
    if (!file_access_supported(config))
        return TURBO_ENOTSUP;
    if (config->request_capacity > SIZE_MAX / sizeof(cflow_io_file_slot))
        return TURBO_EINVAL;

    impl = (cflow_io_file_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->slots = (cflow_io_file_slot *)calloc(
        config->request_capacity, sizeof(*impl->slots));
    if (impl->slots == NULL) {
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->slot_capacity = config->request_capacity;
    impl->backend_kind = config->backend_kind;
    impl->native_handle = UINTPTR_MAX;
    impl->open_flags = config->open_flags;
    impl->completion = config->completion;
    impl->completion_user = config->completion_user;
    turbo_mutex_init(&impl->gate);
    if (impl->gate == NULL) {
        free(impl->slots);
        free(impl);
        return TURBO_ENOMEM;
    }
    for (index = 0u; index < impl->slot_capacity; ++index)
        impl->slots[index].owner = impl;

    backend_config = (cflow_io_native_backend_config){
        config->backend_kind,
        config->request_capacity,
        config->completion_batch_capacity};
    status = cflow_io_native_backend_init(&impl->backend, &backend_config);
    if (status != TURBO_OK)
        goto failed;
    backend_initialized = true;

    if (!cflow_executor_manual_init_with_capacity(
            &impl->executor, config->request_capacity)) {
        status = TURBO_ENOMEM;
        goto failed;
    }
    executor_initialized = true;

    memset(&actor_config, 0, sizeof(actor_config));
    actor_config.request_capacity = config->request_capacity;
    actor_config.command_capacity = config->command_capacity;
    actor_config.executor = &impl->executor;
    actor_config.backend = cflow_io_native_backend_file_actor_ops();
    actor_config.backend_user = &impl->backend;
    actor_config.completion = file_actor_completion;
    actor_config.completion_user = impl;
    status = cflow_io_actor_init(&impl->actor, &actor_config);
    if (status != TURBO_OK)
        goto failed;
    actor_initialized = true;

    status = file_native_open(path, config, &impl->native_handle);
    if (status != TURBO_OK)
        goto failed;

    file->impl = impl;
    return TURBO_OK;

failed:
    file_init_cleanup(impl, backend_initialized, executor_initialized,
                      actor_initialized);
    return status;
}

bool cflow_io_file_operation_supported(
    const cflow_io_file *file,
    cflow_io_native_file_operation_kind operation_kind) {
    const cflow_io_file_impl *impl = file_const_impl(file);
    if (impl == NULL)
        return false;
    if (operation_kind == CFLOW_IO_NATIVE_FILE_READ_AT &&
        (impl->open_flags & CFLOW_IO_FILE_READ) == 0u)
        return false;
    if ((operation_kind == CFLOW_IO_NATIVE_FILE_WRITE_AT ||
         operation_kind == CFLOW_IO_NATIVE_FILE_FLUSH) &&
        (impl->open_flags & CFLOW_IO_FILE_WRITE) == 0u)
        return false;
    return cflow_io_native_backend_file_operation_supported(
        impl->backend_kind, operation_kind);
}

static cflow_io_file_submit_result file_submit_result(
    cflow_io_file_submit_status status,
    cflow_io_request_id request_id) {
    cflow_io_file_submit_result result = {status, request_id};
    return result;
}

static cflow_io_file_submit_status file_map_submit_status(
    cflow_io_submit_status status) {
    switch (status) {
        case CFLOW_IO_SUBMIT_ACCEPTED:
            return CFLOW_IO_FILE_SUBMIT_ACCEPTED;
        case CFLOW_IO_SUBMIT_INVALID_ARGUMENT:
            return CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT;
        case CFLOW_IO_SUBMIT_FULL:
            return CFLOW_IO_FILE_SUBMIT_FULL;
        case CFLOW_IO_SUBMIT_CLOSED:
            return CFLOW_IO_FILE_SUBMIT_CLOSED;
        case CFLOW_IO_SUBMIT_LEASE_IN_USE:
            return CFLOW_IO_FILE_SUBMIT_LEASE_IN_USE;
        case CFLOW_IO_SUBMIT_ID_EXHAUSTED:
            return CFLOW_IO_FILE_SUBMIT_ID_EXHAUSTED;
    }
    return CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT;
}

static cflow_io_file_submit_result file_try_submit(
    cflow_io_file *file,
    cflow_io_lease_id lease_id,
    cflow_io_native_file_operation_kind operation_kind,
    void *buffer,
    size_t length,
    uint64_t offset) {
    cflow_io_file_impl *impl = file_impl(file);
    cflow_io_native_file_operation operation;
    cflow_io_file_slot *slot = NULL;
    cflow_io_operation actor_operation;
    cflow_io_submit_result submitted;
    size_t index;

    if (impl == NULL)
        return file_submit_result(
            CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT, 0u);
    if (operation_kind == CFLOW_IO_NATIVE_FILE_READ_AT &&
        (impl->open_flags & CFLOW_IO_FILE_READ) == 0u)
        return file_submit_result(CFLOW_IO_FILE_SUBMIT_ACCESS_DENIED, 0u);
    if ((operation_kind == CFLOW_IO_NATIVE_FILE_WRITE_AT ||
         operation_kind == CFLOW_IO_NATIVE_FILE_FLUSH) &&
        (impl->open_flags & CFLOW_IO_FILE_WRITE) == 0u)
        return file_submit_result(CFLOW_IO_FILE_SUBMIT_ACCESS_DENIED, 0u);
    if (!cflow_io_native_backend_file_operation_supported(
            impl->backend_kind, operation_kind))
        return file_submit_result(CFLOW_IO_FILE_SUBMIT_UNSUPPORTED, 0u);

    operation = (cflow_io_native_file_operation){
        operation_kind,
        impl->native_handle,
        buffer,
        length,
        offset,
        CFLOW_IO_NATIVE_FILE_ASYNC_CAPABLE};
    if (!cflow_io_native_file_operation_valid(&operation))
        return file_submit_result(
            CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT, 0u);

    turbo_mutex_lock(&impl->gate);
    if (impl->close_requested) {
        turbo_mutex_unlock(&impl->gate);
        return file_submit_result(CFLOW_IO_FILE_SUBMIT_CLOSED, 0u);
    }
    for (index = 0u; index < impl->slot_capacity; ++index) {
        if (!impl->slots[index].in_use) {
            slot = &impl->slots[index];
            slot->operation = operation;
            slot->request_id = 0u;
            slot->in_use = true;
            slot->delivered = false;
            break;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    if (slot == NULL)
        return file_submit_result(CFLOW_IO_FILE_SUBMIT_FULL, 0u);

    actor_operation = (cflow_io_operation){
        &slot->operation, file_slot_release};
    submitted = cflow_io_actor_try_submit(
        &impl->actor, lease_id, &actor_operation);
    if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
        file_slot_release(&slot->operation);
        return file_submit_result(
            file_map_submit_status(submitted.status), 0u);
    }
    turbo_mutex_lock(&impl->gate);
    slot->request_id = submitted.request_id;
    turbo_mutex_unlock(&impl->gate);
    return file_submit_result(
        CFLOW_IO_FILE_SUBMIT_ACCEPTED, submitted.request_id);
}

cflow_io_file_submit_result cflow_io_file_try_read_at(
    cflow_io_file *file,
    cflow_io_lease_id lease_id,
    void *buffer,
    size_t length,
    uint64_t offset) {
    return file_try_submit(file, lease_id, CFLOW_IO_NATIVE_FILE_READ_AT,
                           buffer, length, offset);
}

cflow_io_file_submit_result cflow_io_file_try_write_at(
    cflow_io_file *file,
    cflow_io_lease_id lease_id,
    const void *buffer,
    size_t length,
    uint64_t offset) {
    return file_try_submit(file, lease_id, CFLOW_IO_NATIVE_FILE_WRITE_AT,
                           (void *)buffer, length, offset);
}

cflow_io_file_submit_result cflow_io_file_try_flush(
    cflow_io_file *file,
    cflow_io_lease_id lease_id) {
    return file_try_submit(file, lease_id, CFLOW_IO_NATIVE_FILE_FLUSH,
                           NULL, 0u, 0u);
}

cflow_io_cancel_status cflow_io_file_try_cancel(
    cflow_io_file *file,
    cflow_io_request_id request_id) {
    cflow_io_file_impl *impl = file_impl(file);
    if (impl == NULL)
        return CFLOW_IO_CANCEL_INVALID_ARGUMENT;
    return cflow_io_actor_try_cancel(&impl->actor, request_id);
}

static cflow_io_request_id file_delivered_request(
    cflow_io_file_impl *impl) {
    cflow_io_request_id request_id = 0u;
    size_t index;
    turbo_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->slot_capacity; ++index) {
        if (impl->slots[index].in_use && impl->slots[index].delivered) {
            request_id = impl->slots[index].request_id;
            break;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    return request_id;
}

int cflow_io_file_run_ready(cflow_io_file *file,
                            size_t max_steps,
                            size_t *progressed) {
    cflow_io_file_impl *impl = file_impl(file);
    size_t count = 0u;
    int status = TURBO_OK;
    if (impl == NULL || max_steps == 0u || progressed == NULL)
        return TURBO_EINVAL;
    turbo_mutex_lock(&impl->gate);
    if (impl->driver_active) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    impl->driver_active = true;
    turbo_mutex_unlock(&impl->gate);

    while (count < max_steps) {
        const cflow_io_request_id request_id =
            file_delivered_request(impl);
        cflow_io_run_result actor_result;
        if (request_id != 0u) {
            const cflow_io_ack_status ack_status =
                cflow_io_actor_acknowledge(&impl->actor, request_id);
            if (ack_status == CFLOW_IO_ACK_RELEASED) {
                ++count;
                continue;
            }
            status = ack_status == CFLOW_IO_ACK_BUSY
                ? TURBO_EBUSY : TURBO_EPROTO;
            break;
        }

        actor_result = cflow_io_actor_run_one(&impl->actor);
        if (actor_result.status == CFLOW_IO_RUN_PROGRESSED) {
            ++count;
            continue;
        }
        if (actor_result.status == CFLOW_IO_RUN_BUSY) {
            status = TURBO_EBUSY;
            break;
        }
        if (actor_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT) {
            status = TURBO_EINVAL;
            break;
        }
        if (cflow_executor_run_one(&impl->executor)) {
            ++count;
            continue;
        }
        break;
    }

    turbo_mutex_lock(&impl->gate);
    impl->driver_active = false;
    turbo_mutex_unlock(&impl->gate);
    *progressed = count;
    return status;
}

int cflow_io_file_close(cflow_io_file *file) {
    cflow_io_file_impl *impl = file_impl(file);
    int status;
    if (impl == NULL)
        return TURBO_EINVAL;
    status = cflow_io_actor_close(&impl->actor);
    if (status == TURBO_OK || status == TURBO_EALREADY) {
        turbo_mutex_lock(&impl->gate);
        impl->close_requested = true;
        turbo_mutex_unlock(&impl->gate);
    }
    return status;
}

bool cflow_io_file_is_quiescent(const cflow_io_file *file) {
    const cflow_io_file_impl *impl = file_const_impl(file);
    return impl != NULL && cflow_io_actor_is_quiescent(&impl->actor);
}

bool cflow_io_file_get_stats(const cflow_io_file *file,
                             cflow_io_file_stats *out) {
    cflow_io_file_impl *impl = (cflow_io_file_impl *)file_const_impl(file);
    cflow_io_file_stats snapshot = {0};
    size_t index;
    if (impl == NULL || out == NULL ||
        !cflow_io_actor_get_stats(&impl->actor, &snapshot.actor) ||
        !cflow_io_native_backend_get_stats(&impl->backend,
                                           &snapshot.backend))
        return false;
    turbo_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->slot_capacity; ++index) {
        if (impl->slots[index].in_use)
            ++snapshot.operation_slots_in_use;
    }
    snapshot.close_requested = impl->close_requested;
    turbo_mutex_unlock(&impl->gate);
    *out = snapshot;
    return true;
}

int cflow_io_file_destroy(cflow_io_file *file) {
    cflow_io_file_impl *impl = file_impl(file);
    uintptr_t closed_handle;
    int result = TURBO_OK;
    int status;
    if (impl == NULL)
        return TURBO_EINVAL;
    if (!cflow_io_actor_is_quiescent(&impl->actor))
        return TURBO_EBUSY;

    status = cflow_io_native_backend_shutdown(&impl->backend);
    if (status != TURBO_OK && status != TURBO_EALREADY)
        return status;
    status = cflow_io_actor_destroy(&impl->actor);
    if (status != TURBO_OK)
        return status;
    closed_handle = impl->native_handle;
    impl->native_handle = UINTPTR_MAX;
    status = file_native_close(closed_handle);
    if (status != TURBO_OK)
        result = status;
    status = cflow_io_native_backend_forget_file(
        &impl->backend, closed_handle);
    if (status != TURBO_OK && status != TURBO_ENOENT && result == TURBO_OK)
        result = status;
    status = cflow_io_native_backend_destroy(&impl->backend);
    if (status != TURBO_OK && result == TURBO_OK)
        result = status;
    if (!cflow_executor_shutdown(&impl->executor) && result == TURBO_OK)
        result = TURBO_EBUSY;
    cflow_executor_destroy(&impl->executor);
    turbo_mutex_destroy(&impl->gate);
    free(impl->slots);
    free(impl);
    file->impl = NULL;
    return result;
}
