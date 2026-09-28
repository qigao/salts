#include <salts/plugin.h>

#include "plugin_loader_internal.h"

#include <salts/thread.h>
#include <vstr.h>

#include <limits.h>
#include <stdlib.h>

typedef struct salts_plugin_registry_slot {
    salts_plugin_library library;
    const salts_plugin_manifest *manifest;
    uint32_t generation;
    salts_plugin_lifecycle_state state;
    salts_plugin_status failure;
    size_t callbacks_inflight;
    size_t active_leases;
    uint64_t lease_active_mask;
    uint32_t lease_generations[SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN];
    bool occupied;
    bool unloading;
    bool destroy_called;
} salts_plugin_registry_slot;

typedef struct salts_plugin_registry_impl {
    salts_plugin_registry_slot *slots;
    size_t capacity;
    size_t count;
    size_t loads_inflight;
    salts_mutex_t lock;
    bool destroying;
} salts_plugin_registry_impl;

static bool bounded_cstr_valid(const char *value, size_t max_bytes) {
    size_t index;

    if (value == NULL || max_bytes == 0u)
        return false;

    for (index = 0u; index <= max_bytes; ++index) {
        if (value[index] == '\0')
            return index != 0u;
    }
    return false;
}

static bool bounded_utf8_path_valid(const char *path) {
    size_t length;

    if (path == NULL)
        return false;

    for (length = 0u; length <= SALTS_PLUGIN_PATH_MAX; ++length) {
        if (path[length] == '\0') {
            if (length == 0u)
                return false;
            return vstr_utf8_valid(vstr_from_buf(path, length)) != 0;
        }
    }
    return false;
}

static bool plugin_id_equal(const char *left, const char *right) {
    size_t index;

    if (!bounded_cstr_valid(left, SALTS_PLUGIN_ID_MAX) ||
        !bounded_cstr_valid(right, SALTS_PLUGIN_ID_MAX))
        return false;

    for (index = 0u; index <= SALTS_PLUGIN_ID_MAX; ++index) {
        if (left[index] != right[index])
            return false;
        if (left[index] == '\0')
            return true;
    }
    return false;
}

static uint32_t next_generation(uint32_t current) {
    uint32_t next = current + 1u;
    return next == 0u ? 1u : next;
}

static salts_plugin_status close_rejected_library(
    salts_plugin_library *library,
    salts_plugin_status rejection) {
    salts_plugin_status close_status = salts_plugin_platform_close(library);
    return close_status == SALTS_PLUGIN_OK ? rejection : close_status;
}

static void release_load_reservation(
    salts_plugin_registry_impl *impl) {
    salts_mutex_lock(&impl->lock);
    --impl->loads_inflight;
    salts_mutex_unlock(&impl->lock);
}

static salts_plugin_status close_rejected_load(
    salts_plugin_registry_impl *impl,
    salts_plugin_library *library,
    salts_plugin_status rejection) {
    salts_plugin_status status =
        close_rejected_library(library, rejection);
    release_load_reservation(impl);
    return status;
}

static salts_plugin_registry_impl *registry_impl(
    const salts_plugin_registry *registry) {
    return registry == NULL
        ? NULL
        : (salts_plugin_registry_impl *)registry->impl;
}

static salts_plugin_status slot_for_ref_locked(
    salts_plugin_registry_impl *impl,
    salts_plugin_ref ref,
    salts_plugin_registry_slot **out_slot,
    size_t *out_index) {
    size_t index;
    salts_plugin_registry_slot *slot;

    if (out_slot != NULL)
        *out_slot = NULL;
    if (out_index != NULL)
        *out_index = SIZE_MAX;

    if (impl == NULL || !salts_plugin_ref_valid(ref))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    index = (size_t)ref.slot - 1u;
    if (index >= impl->capacity)
        return SALTS_PLUGIN_STALE;

    slot = &impl->slots[index];
    if (!slot->occupied || slot->generation != ref.generation)
        return SALTS_PLUGIN_STALE;

    if (out_slot != NULL)
        *out_slot = slot;
    if (out_index != NULL)
        *out_index = index;
    return SALTS_PLUGIN_OK;
}

static void clear_slot_locked(
    salts_plugin_registry_impl *impl,
    salts_plugin_registry_slot *slot) {
    size_t index;

    slot->manifest = NULL;
    slot->occupied = false;
    slot->unloading = false;
    slot->destroy_called = false;
    slot->state = (salts_plugin_lifecycle_state)0;
    slot->failure = SALTS_PLUGIN_OK;
    slot->callbacks_inflight = 0u;
    slot->active_leases = 0u;
    slot->lease_active_mask = 0u;
    slot->generation = next_generation(slot->generation);
    for (index = 0u; index < SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN; ++index)
        slot->lease_generations[index] =
            next_generation(slot->lease_generations[index]);
    if (impl->count != 0u)
        --impl->count;
}

salts_plugin_status salts_plugin_registry_init(
    salts_plugin_registry *registry,
    const salts_plugin_registry_config *config) {
    salts_plugin_registry_impl *impl;
    size_t slot_index;
    size_t lease_index;

    if (registry == NULL || config == NULL || registry->impl != NULL ||
        config->capacity == 0u)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (config->capacity > (size_t)UINT32_MAX ||
        config->capacity > SIZE_MAX / sizeof(salts_plugin_registry_slot))
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;

    impl = (salts_plugin_registry_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return SALTS_PLUGIN_ALLOCATION_FAILED;

    impl->slots = (salts_plugin_registry_slot *)calloc(
        config->capacity, sizeof(*impl->slots));
    if (impl->slots == NULL) {
        free(impl);
        return SALTS_PLUGIN_ALLOCATION_FAILED;
    }

    salts_mutex_init(&impl->lock);
    if (impl->lock == NULL) {
        free(impl->slots);
        free(impl);
        return SALTS_PLUGIN_ALLOCATION_FAILED;
    }

    impl->capacity = config->capacity;
    for (slot_index = 0u; slot_index < impl->capacity; ++slot_index) {
        impl->slots[slot_index].generation = 1u;
        for (lease_index = 0u;
             lease_index < SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN;
             ++lease_index)
            impl->slots[slot_index].lease_generations[lease_index] = 1u;
    }

    registry->impl = impl;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_registry_load(
    salts_plugin_registry *registry,
    const char *path,
    salts_plugin_ref *out_ref) {
    salts_plugin_registry_impl *impl;
    salts_plugin_registry_slot *slot = NULL;
    salts_plugin_library library = {0};
    salts_plugin_query_fn query = NULL;
    const salts_plugin_manifest *manifest;
    salts_plugin_status status;
    size_t slot_index = 0u;
    size_t index;

    if (out_ref == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_ref = (salts_plugin_ref){0};

    impl = registry_impl(registry);
    if (impl == NULL || !bounded_utf8_path_valid(path))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    /*
     * Reserve bounded admission capacity under the registry lock, then execute
     * platform loader and plugin-owned query code without that lock held.
     * destroy() observes loads_inflight and cannot free impl while the
     * reservation exists.
     */
    salts_mutex_lock(&impl->lock);
    if (impl->destroying) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (impl->count >= impl->capacity ||
        impl->loads_inflight >= impl->capacity - impl->count) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;
    }
    ++impl->loads_inflight;
    salts_mutex_unlock(&impl->lock);

    status = salts_plugin_platform_open(path, &library, &query);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_lock(&impl->lock);
        --impl->loads_inflight;
        salts_mutex_unlock(&impl->lock);
        return status;
    }

    manifest = query(SALTS_PLUGIN_ABI_VERSION);
    if (manifest == NULL) {
        return close_rejected_load(
            impl, &library, SALTS_PLUGIN_QUERY_REJECTED);
    }

    status = salts_plugin_manifest_validate(manifest);
    if (status != SALTS_PLUGIN_OK)
        return close_rejected_load(impl, &library, status);

    /*
     * Publication is the only second locked phase. Re-check registry state and
     * duplicate identity because another concurrent load may have published
     * while this candidate was open/querying.
     */
    salts_mutex_lock(&impl->lock);

    if (impl->destroying) {
        status = SALTS_PLUGIN_BUSY;
        goto reject_locked;
    }

    for (index = 0u; index < impl->capacity; ++index) {
        const salts_plugin_registry_slot *existing = &impl->slots[index];
        if (existing->occupied &&
            plugin_id_equal(existing->manifest->plugin_id,
                            manifest->plugin_id)) {
            status = SALTS_PLUGIN_DUPLICATE_PLUGIN_ID;
            goto reject_locked;
        }
    }

    for (slot_index = 0u; slot_index < impl->capacity; ++slot_index) {
        if (!impl->slots[slot_index].occupied) {
            slot = &impl->slots[slot_index];
            break;
        }
    }
    if (slot == NULL) {
        status = SALTS_PLUGIN_CAPACITY_EXCEEDED;
        goto reject_locked;
    }

    slot->library = library;
    slot->manifest = manifest;
    slot->state = SALTS_PLUGIN_LIFECYCLE_LOADED;
    slot->failure = SALTS_PLUGIN_OK;
    slot->occupied = true;
    slot->unloading = false;
    slot->destroy_called = false;
    slot->callbacks_inflight = 0u;
    slot->active_leases = 0u;
    slot->lease_active_mask = 0u;
    ++impl->count;
    --impl->loads_inflight;

    out_ref->slot = (uint32_t)(slot_index + 1u);
    out_ref->generation = slot->generation;
    salts_mutex_unlock(&impl->lock);
    return SALTS_PLUGIN_OK;

reject_locked:
    salts_mutex_unlock(&impl->lock);
    return close_rejected_load(impl, &library, status);
}

salts_plugin_status salts_plugin_registry_find(
    const salts_plugin_registry *registry,
    const char *plugin_id,
    salts_plugin_ref *out_ref) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    size_t index;

    if (out_ref == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_ref = (salts_plugin_ref){0};

    if (impl == NULL ||
        !bounded_cstr_valid(plugin_id, SALTS_PLUGIN_ID_MAX))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    for (index = 0u; index < impl->capacity; ++index) {
        const salts_plugin_registry_slot *slot = &impl->slots[index];
        if (slot->occupied &&
            plugin_id_equal(slot->manifest->plugin_id, plugin_id)) {
            out_ref->slot = (uint32_t)(index + 1u);
            out_ref->generation = slot->generation;
            salts_mutex_unlock(&impl->lock);
            return SALTS_PLUGIN_OK;
        }
    }
    salts_mutex_unlock(&impl->lock);
    return SALTS_PLUGIN_UNKNOWN_PLUGIN;
}

salts_plugin_status salts_plugin_registry_start(
    salts_plugin_registry *registry,
    salts_plugin_ref ref) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_start_fn callback;
    void *self;
    salts_plugin_status status;

    if (impl == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, ref, &slot, NULL);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_unlock(&impl->lock);
        return status;
    }
    if (impl->destroying || slot->unloading) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (slot->state == SALTS_PLUGIN_LIFECYCLE_STARTED) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_ALREADY;
    }
    if (slot->state != SALTS_PLUGIN_LIFECYCLE_LOADED) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_INVALID_STATE;
    }
    if (slot->active_leases != 0u || slot->callbacks_inflight != 0u) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }

    callback = slot->manifest->start;
    self = slot->manifest->self;
    if (callback == NULL) {
        slot->state = SALTS_PLUGIN_LIFECYCLE_STARTED;
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_OK;
    }

    slot->state = SALTS_PLUGIN_LIFECYCLE_STARTING;
    ++slot->callbacks_inflight;
    salts_mutex_unlock(&impl->lock);

    status = callback(self);

    salts_mutex_lock(&impl->lock);
    --slot->callbacks_inflight;
    if (status == SALTS_PLUGIN_OK) {
        slot->state = SALTS_PLUGIN_LIFECYCLE_STARTED;
    } else {
        /*
         * start() is failure-atomic by contract: a failed start has not
         * published service work and must remain destroyable/unloadable.
         */
        slot->state = SALTS_PLUGIN_LIFECYCLE_QUIESCENT;
        if (slot->failure == SALTS_PLUGIN_OK)
            slot->failure = status;
    }
    salts_mutex_unlock(&impl->lock);
    return status;
}

salts_plugin_status salts_plugin_registry_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref ref,
    salts_plugin_lease *out_lease,
    const salts_plugin_manifest **out_manifest) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_status status;
    size_t index;

    if (out_lease == NULL || out_manifest == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_lease = (salts_plugin_lease){0};
    *out_manifest = NULL;

    if (impl == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, ref, &slot, NULL);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_unlock(&impl->lock);
        return status;
    }
    if (impl->destroying || slot->unloading) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (slot->state != SALTS_PLUGIN_LIFECYCLE_STARTED) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_INVALID_STATE;
    }

    for (index = 0u; index < SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN; ++index) {
        const uint64_t bit = UINT64_C(1) << index;
        if ((slot->lease_active_mask & bit) == 0u) {
            slot->lease_active_mask |= bit;
            ++slot->active_leases;
            out_lease->plugin = ref;
            out_lease->slot = (uint32_t)(index + 1u);
            out_lease->generation = slot->lease_generations[index];
            *out_manifest = slot->manifest;
            salts_mutex_unlock(&impl->lock);
            return SALTS_PLUGIN_OK;
        }
    }

    salts_mutex_unlock(&impl->lock);
    return SALTS_PLUGIN_CAPACITY_EXCEEDED;
}

salts_plugin_status salts_plugin_registry_release(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_status status;
    size_t index;
    uint64_t bit;

    if (impl == NULL || lease == NULL || !salts_plugin_lease_valid(*lease))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, lease->plugin, &slot, NULL);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_unlock(&impl->lock);
        return status;
    }

    index = (size_t)lease->slot - 1u;
    if (index >= SALTS_PLUGIN_MAX_LEASES_PER_PLUGIN) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_STALE;
    }

    bit = UINT64_C(1) << index;
    if ((slot->lease_active_mask & bit) == 0u ||
        slot->lease_generations[index] != lease->generation) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_STALE;
    }

    slot->lease_active_mask &= ~bit;
    slot->lease_generations[index] =
        next_generation(slot->lease_generations[index]);
    if (slot->active_leases != 0u)
        --slot->active_leases;
    *lease = (salts_plugin_lease){0};
    salts_mutex_unlock(&impl->lock);
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_registry_request_stop(
    salts_plugin_registry *registry,
    salts_plugin_ref ref) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_request_stop_fn callback;
    void *self;
    salts_plugin_status status;

    if (impl == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, ref, &slot, NULL);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_unlock(&impl->lock);
        return status;
    }
    if (impl->destroying || slot->unloading) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (slot->state == SALTS_PLUGIN_LIFECYCLE_STOPPING ||
        slot->state == SALTS_PLUGIN_LIFECYCLE_QUIESCENT) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_ALREADY;
    }
    if (slot->state != SALTS_PLUGIN_LIFECYCLE_STARTED) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_INVALID_STATE;
    }

    slot->state = SALTS_PLUGIN_LIFECYCLE_STOPPING;
    callback = slot->manifest->request_stop;
    self = slot->manifest->self;
    if (callback == NULL) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_OK;
    }

    ++slot->callbacks_inflight;
    salts_mutex_unlock(&impl->lock);

    status = callback(self);

    salts_mutex_lock(&impl->lock);
    --slot->callbacks_inflight;
    if (status != SALTS_PLUGIN_OK && slot->failure == SALTS_PLUGIN_OK)
        slot->failure = status;
    salts_mutex_unlock(&impl->lock);
    return status;
}

salts_plugin_status salts_plugin_registry_poll_quiescent(
    salts_plugin_registry *registry,
    salts_plugin_ref ref,
    bool *out_quiescent) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_is_quiescent_fn callback;
    const void *self;
    salts_plugin_status status;
    bool quiescent;

    if (out_quiescent == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_quiescent = false;
    if (impl == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, ref, &slot, NULL);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_unlock(&impl->lock);
        return status;
    }
    if (impl->destroying || slot->unloading) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (slot->state == SALTS_PLUGIN_LIFECYCLE_QUIESCENT) {
        *out_quiescent = true;
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_OK;
    }
    if (slot->state != SALTS_PLUGIN_LIFECYCLE_STOPPING) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_INVALID_STATE;
    }
    if (slot->active_leases != 0u || slot->callbacks_inflight != 0u) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_OK;
    }

    callback = slot->manifest->is_quiescent;
    self = slot->manifest->self;
    if (callback == NULL) {
        slot->state = SALTS_PLUGIN_LIFECYCLE_QUIESCENT;
        *out_quiescent = true;
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_OK;
    }

    ++slot->callbacks_inflight;
    salts_mutex_unlock(&impl->lock);

    quiescent = callback(self);

    salts_mutex_lock(&impl->lock);
    --slot->callbacks_inflight;
    if (quiescent)
        slot->state = SALTS_PLUGIN_LIFECYCLE_QUIESCENT;
    *out_quiescent = quiescent;
    salts_mutex_unlock(&impl->lock);
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_registry_get_lifecycle(
    const salts_plugin_registry *registry,
    salts_plugin_ref ref,
    salts_plugin_lifecycle_info *out_info) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_status status;

    if (out_info == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_info = (salts_plugin_lifecycle_info){0};
    if (impl == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, ref, &slot, NULL);
    if (status == SALTS_PLUGIN_OK) {
        out_info->state = slot->state;
        out_info->active_leases = slot->active_leases;
        out_info->callbacks_inflight = slot->callbacks_inflight;
        out_info->failure = slot->failure;
    }
    salts_mutex_unlock(&impl->lock);
    return status;
}

salts_plugin_status salts_plugin_registry_unload(
    salts_plugin_registry *registry,
    salts_plugin_ref ref) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    salts_plugin_destroy_fn destroy_callback = NULL;
    void *self = NULL;
    salts_plugin_status status;

    if (impl == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    salts_mutex_lock(&impl->lock);
    status = slot_for_ref_locked(impl, ref, &slot, NULL);
    if (status != SALTS_PLUGIN_OK) {
        salts_mutex_unlock(&impl->lock);
        return status;
    }
    if (impl->destroying || slot->unloading) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (slot->state != SALTS_PLUGIN_LIFECYCLE_LOADED &&
        slot->state != SALTS_PLUGIN_LIFECYCLE_QUIESCENT) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }
    if (slot->active_leases != 0u || slot->callbacks_inflight != 0u) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }

    slot->unloading = true;
    if (!slot->destroy_called && slot->manifest->destroy != NULL) {
        destroy_callback = slot->manifest->destroy;
        self = slot->manifest->self;
        ++slot->callbacks_inflight;
    }
    salts_mutex_unlock(&impl->lock);

    if (destroy_callback != NULL)
        destroy_callback(self);

    salts_mutex_lock(&impl->lock);
    if (destroy_callback != NULL) {
        --slot->callbacks_inflight;
        slot->destroy_called = true;
        /*
         * destroy() is irreversible even if the subsequent native close fails.
         * Keep the slot quiescent so retry may only close the DSO; never permit
         * start/acquire against already-destroyed plugin state.
         */
        slot->state = SALTS_PLUGIN_LIFECYCLE_QUIESCENT;
    }
    salts_mutex_unlock(&impl->lock);

    status = salts_plugin_platform_close(&slot->library);

    salts_mutex_lock(&impl->lock);
    if (status == SALTS_PLUGIN_OK) {
        clear_slot_locked(impl, slot);
    } else {
        slot->unloading = false;
    }
    salts_mutex_unlock(&impl->lock);
    return status;
}

size_t salts_plugin_registry_count(const salts_plugin_registry *registry) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    size_t count;

    if (impl == NULL)
        return 0u;

    salts_mutex_lock(&impl->lock);
    count = impl->count;
    salts_mutex_unlock(&impl->lock);
    return count;
}

salts_plugin_status salts_plugin_registry_destroy(
    salts_plugin_registry *registry) {
    salts_plugin_registry_impl *impl;
    size_t index;

    if (registry == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    if (registry->impl == NULL)
        return SALTS_PLUGIN_OK;

    impl = (salts_plugin_registry_impl *)registry->impl;
    salts_mutex_lock(&impl->lock);
    if (impl->destroying || impl->loads_inflight != 0u) {
        salts_mutex_unlock(&impl->lock);
        return SALTS_PLUGIN_BUSY;
    }

    for (index = 0u; index < impl->capacity; ++index) {
        const salts_plugin_registry_slot *slot = &impl->slots[index];
        if (!slot->occupied)
            continue;
        if (slot->unloading ||
            slot->active_leases != 0u ||
            slot->callbacks_inflight != 0u ||
            (slot->state != SALTS_PLUGIN_LIFECYCLE_LOADED &&
             slot->state != SALTS_PLUGIN_LIFECYCLE_QUIESCENT)) {
            salts_mutex_unlock(&impl->lock);
            return SALTS_PLUGIN_BUSY;
        }
    }

    impl->destroying = true;
    salts_mutex_unlock(&impl->lock);

    for (index = 0u; index < impl->capacity; ++index) {
        salts_plugin_registry_slot *slot = &impl->slots[index];
        salts_plugin_destroy_fn destroy_callback = NULL;
        void *self = NULL;
        salts_plugin_status status;

        salts_mutex_lock(&impl->lock);
        if (!slot->occupied) {
            salts_mutex_unlock(&impl->lock);
            continue;
        }
        slot->unloading = true;
        if (!slot->destroy_called && slot->manifest->destroy != NULL) {
            destroy_callback = slot->manifest->destroy;
            self = slot->manifest->self;
            ++slot->callbacks_inflight;
        }
        salts_mutex_unlock(&impl->lock);

        if (destroy_callback != NULL)
            destroy_callback(self);

        salts_mutex_lock(&impl->lock);
        if (destroy_callback != NULL) {
            --slot->callbacks_inflight;
            slot->destroy_called = true;
            slot->state = SALTS_PLUGIN_LIFECYCLE_QUIESCENT;
        }
        salts_mutex_unlock(&impl->lock);

        status = salts_plugin_platform_close(&slot->library);

        salts_mutex_lock(&impl->lock);
        if (status != SALTS_PLUGIN_OK) {
            slot->unloading = false;
            impl->destroying = false;
            salts_mutex_unlock(&impl->lock);
            return status;
        }
        clear_slot_locked(impl, slot);
        salts_mutex_unlock(&impl->lock);
    }

    salts_mutex_lock(&impl->lock);
    registry->impl = NULL;
    salts_mutex_unlock(&impl->lock);

    salts_mutex_destroy(&impl->lock);
    free(impl->slots);
    free(impl);
    return SALTS_PLUGIN_OK;
}
