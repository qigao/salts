#include <cflow/fs_watch.h>

#include "fs_watch_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_fs_watch_slot {
    cflow_fs_watch_event_kind kind;
    cflow_fs_watch_entry_type entry_type;
    char *path;
    char *old_path;
} cflow_fs_watch_slot;

struct cflow_fs_watch_impl {
    cflow_fs_watch_slot *slots;
    char *paths;
    char *delivery_path;
    char *delivery_old_path;
    size_t capacity;
    size_t path_capacity;
    size_t head;
    size_t tail;
    size_t count;
    turbo_mutex_t gate;
    cflow_fs_watch_event_fn event;
    void *event_user;
    void *backend;
    atomic_bool close_requested;
    atomic_bool backend_done;
    atomic_bool driver_active;
    size_t delivered;
    size_t suppressed;
    size_t rescan_required;
    size_t rescan_delivery_suppressed;
    bool awaiting_rescan;
    bool rescan_pending;
    bool rescan_delivered;
};

static bool watch_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static bool watch_path_length(const char *path, size_t capacity,
                              size_t *out) {
    size_t length = 0u;
    if (path == NULL) {
        *out = 0u;
        return true;
    }
    while (length < capacity && path[length] != '\0')
        ++length;
    if (length == capacity)
        return false;
    *out = length;
    return true;
}

void cflow_fs_watch_backend_set(cflow_fs_watch_impl *impl, void *backend) {
    if (impl != NULL)
        impl->backend = backend;
}

void *cflow_fs_watch_backend_get(cflow_fs_watch_impl *impl) {
    return impl != NULL ? impl->backend : NULL;
}

bool cflow_fs_watch_close_requested(const cflow_fs_watch_impl *impl) {
    return impl == NULL || atomic_load(&impl->close_requested);
}

void cflow_fs_watch_backend_mark_done(cflow_fs_watch_impl *impl) {
    if (impl != NULL)
        atomic_store(&impl->backend_done, true);
}

void cflow_fs_watch_publish_loss(cflow_fs_watch_impl *impl) {
    if (impl == NULL)
        return;
    turbo_mutex_lock(&impl->gate);
    ++impl->suppressed;
    if (!impl->awaiting_rescan) {
        impl->awaiting_rescan = true;
        impl->rescan_pending = true;
        impl->rescan_delivered = false;
        ++impl->rescan_required;
    }
    turbo_mutex_unlock(&impl->gate);
}

int cflow_fs_watch_publish(cflow_fs_watch_impl *impl,
                           cflow_fs_watch_event_kind kind,
                           const char *path,
                           const char *old_path,
                           cflow_fs_watch_entry_type entry_type) {
    size_t path_length;
    size_t old_length;
    cflow_fs_watch_slot *slot;
    if (impl == NULL || kind > CFLOW_FS_WATCH_ROOT_CHANGED ||
        !watch_path_length(path, impl->path_capacity, &path_length) ||
        !watch_path_length(old_path, impl->path_capacity, &old_length)) {
        cflow_fs_watch_publish_loss(impl);
        return TURBO_ENOBUFS;
    }
    turbo_mutex_lock(&impl->gate);
    if (atomic_load(&impl->close_requested)) {
        ++impl->suppressed;
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ESHUTDOWN;
    }
    if (impl->awaiting_rescan || impl->count == impl->capacity) {
        ++impl->suppressed;
        if (!impl->awaiting_rescan) {
            impl->awaiting_rescan = true;
            impl->rescan_pending = true;
            impl->rescan_delivered = false;
            ++impl->rescan_required;
        }
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOBUFS;
    }
    slot = &impl->slots[impl->tail];
    slot->kind = kind;
    slot->entry_type = entry_type;
    if (path != NULL)
        memcpy(slot->path, path, path_length + 1u);
    else
        slot->path[0] = '\0';
    if (old_path != NULL)
        memcpy(slot->old_path, old_path, old_length + 1u);
    else
        slot->old_path[0] = '\0';
    impl->tail = (impl->tail + 1u) % impl->capacity;
    ++impl->count;
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

int cflow_fs_watch_open(cflow_fs_watch *watch, const char *path,
                        const cflow_fs_watch_config *config) {
    cflow_fs_watch_impl *impl;
    size_t slot_bytes;
    size_t pair_capacity;
    size_t all_path_bytes;
    size_t index;
    int status;
    if (watch == NULL || watch->impl != NULL || path == NULL || path[0] == '\0' ||
        config == NULL || config->event_capacity == 0u ||
        config->watch_capacity == 0u ||
        config->path_capacity < 2u || config->native_buffer_capacity < 1024u ||
        config->native_buffer_capacity > 65536u || config->event == NULL ||
        !watch_multiply(config->event_capacity,
                        sizeof(cflow_fs_watch_slot), &slot_bytes) ||
        !watch_multiply(config->path_capacity, 2u, &pair_capacity) ||
        !watch_multiply(config->event_capacity, pair_capacity,
                        &all_path_bytes))
        return TURBO_EINVAL;
    impl = (cflow_fs_watch_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->slots = (cflow_fs_watch_slot *)calloc(1u, slot_bytes);
    impl->paths = (char *)calloc(1u, all_path_bytes);
    impl->delivery_path = (char *)calloc(1u, pair_capacity);
    if (impl->slots == NULL || impl->paths == NULL ||
        impl->delivery_path == NULL) {
        free(impl->delivery_path);
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->delivery_old_path = impl->delivery_path + config->path_capacity;
    impl->capacity = config->event_capacity;
    impl->path_capacity = config->path_capacity;
    impl->event = config->event;
    impl->event_user = config->event_user;
    turbo_mutex_init(&impl->gate);
    atomic_init(&impl->close_requested, false);
    atomic_init(&impl->backend_done, false);
    atomic_init(&impl->driver_active, false);
    for (index = 0u; index < impl->capacity; ++index) {
        impl->slots[index].path = impl->paths + index * pair_capacity;
        impl->slots[index].old_path =
            impl->slots[index].path + impl->path_capacity;
    }
    status = cflow_fs_watch_backend_open(impl, path, config);
    if (status != TURBO_OK) {
        turbo_mutex_destroy(&impl->gate);
        free(impl->delivery_path);
        free(impl->paths);
        free(impl->slots);
        free(impl);
        return status;
    }
    watch->impl = impl;
    return TURBO_OK;
}

int cflow_fs_watch_run_ready(cflow_fs_watch *watch, size_t max_events,
                             size_t *delivered) {
    cflow_fs_watch_impl *impl;
    bool expected = false;
    size_t count = 0u;
    if (watch == NULL || watch->impl == NULL ||
        max_events == 0u || delivered == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_watch_impl *)watch->impl;
    *delivered = 0u;
    if (!atomic_compare_exchange_strong(&impl->driver_active, &expected, true))
        return TURBO_EBUSY;
    while (count < max_events) {
        cflow_fs_watch_event event;
        bool have_event = false;
        turbo_mutex_lock(&impl->gate);
        if (impl->count != 0u) {
            cflow_fs_watch_slot *slot = &impl->slots[impl->head];
            memcpy(impl->delivery_path, slot->path,
                   strlen(slot->path) + 1u);
            memcpy(impl->delivery_old_path, slot->old_path,
                   strlen(slot->old_path) + 1u);
            event = (cflow_fs_watch_event){
                .kind = slot->kind,
                .path = impl->delivery_path[0] != '\0'
                    ? impl->delivery_path : NULL,
                .old_path = impl->delivery_old_path[0] != '\0'
                    ? impl->delivery_old_path : NULL,
                .entry_type = slot->entry_type,
            };
            impl->head = (impl->head + 1u) % impl->capacity;
            --impl->count;
            have_event = true;
        } else if (impl->rescan_pending) {
            impl->rescan_pending = false;
            impl->rescan_delivered = true;
            impl->rescan_delivery_suppressed = impl->suppressed;
            event = (cflow_fs_watch_event){
                .kind = CFLOW_FS_WATCH_RESCAN_REQUIRED,
                .path = NULL,
                .old_path = NULL,
                .entry_type = CFLOW_FS_WATCH_ENTRY_UNKNOWN,
            };
            have_event = true;
        }
        turbo_mutex_unlock(&impl->gate);
        if (!have_event)
            break;
        impl->event(impl->event_user, &event);
        turbo_mutex_lock(&impl->gate);
        ++impl->delivered;
        turbo_mutex_unlock(&impl->gate);
        ++count;
    }
    atomic_store(&impl->driver_active, false);
    *delivered = count;
    return TURBO_OK;
}

int cflow_fs_watch_acknowledge_rescan(cflow_fs_watch *watch) {
    cflow_fs_watch_impl *impl;
    if (watch == NULL || watch->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_watch_impl *)watch->impl;
    turbo_mutex_lock(&impl->gate);
    if (!impl->awaiting_rescan || !impl->rescan_delivered) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EALREADY;
    }
    if (impl->suppressed != impl->rescan_delivery_suppressed) {
        impl->rescan_pending = true;
        impl->rescan_delivered = false;
        ++impl->rescan_required;
    } else {
        impl->awaiting_rescan = false;
        impl->rescan_delivered = false;
    }
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

int cflow_fs_watch_close(cflow_fs_watch *watch) {
    cflow_fs_watch_impl *impl;
    bool expected = false;
    if (watch == NULL || watch->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_watch_impl *)watch->impl;
    if (!atomic_compare_exchange_strong(&impl->close_requested,
                                        &expected, true))
        return TURBO_EALREADY;
    cflow_fs_watch_backend_request_close(impl);
    return TURBO_OK;
}

bool cflow_fs_watch_is_quiescent(const cflow_fs_watch *watch) {
    const cflow_fs_watch_impl *impl;
    bool empty;
    if (watch == NULL || watch->impl == NULL)
        return false;
    impl = (const cflow_fs_watch_impl *)watch->impl;
    turbo_mutex_lock((turbo_mutex_t *)&impl->gate);
    empty = impl->count == 0u && !impl->rescan_pending;
    turbo_mutex_unlock((turbo_mutex_t *)&impl->gate);
    return atomic_load(&impl->close_requested) &&
           atomic_load(&impl->backend_done) && empty;
}

bool cflow_fs_watch_get_stats(const cflow_fs_watch *watch,
                              cflow_fs_watch_stats *out) {
    const cflow_fs_watch_impl *impl;
    if (watch == NULL || watch->impl == NULL || out == NULL)
        return false;
    impl = (const cflow_fs_watch_impl *)watch->impl;
    turbo_mutex_lock((turbo_mutex_t *)&impl->gate);
    *out = (cflow_fs_watch_stats){
        .capacity = impl->capacity,
        .queued = impl->count + (impl->rescan_pending ? 1u : 0u),
        .delivered = impl->delivered,
        .suppressed = impl->suppressed,
        .rescan_required = impl->rescan_required,
        .awaiting_rescan = impl->awaiting_rescan,
        .lifecycle = !atomic_load(&impl->close_requested)
            ? CFLOW_FS_WATCH_OPEN
            : (atomic_load(&impl->backend_done)
                   ? CFLOW_FS_WATCH_CLOSED : CFLOW_FS_WATCH_CLOSING),
    };
    turbo_mutex_unlock((turbo_mutex_t *)&impl->gate);
    return true;
}

int cflow_fs_watch_destroy(cflow_fs_watch *watch) {
    cflow_fs_watch_impl *impl;
    int status;
    if (watch == NULL || watch->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_fs_watch_impl *)watch->impl;
    if (atomic_load(&impl->driver_active) ||
        !cflow_fs_watch_is_quiescent(watch))
        return TURBO_EBUSY;
    status = cflow_fs_watch_backend_destroy(impl);
    turbo_mutex_destroy(&impl->gate);
    free(impl->delivery_path);
    free(impl->paths);
    free(impl->slots);
    free(impl);
    watch->impl = NULL;
    return status;
}
