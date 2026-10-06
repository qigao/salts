#include <object_pool_managed.h>

static int managed_lease_valid(
    const object_pool_managed *pool, const object_pool_managed_lease *lease) {
    return lease != NULL && lease->self == lease && lease->owner == pool &&
        object_pool_is_allocated(pool->storage, lease->value) ? SALTS_OK : SALTS_EINVAL;
}

int object_pool_managed_check(const object_pool_managed *pool) {
    int status;
    if (pool == NULL) return SALTS_EINVAL;
    status = salts_local_check(&pool->binding, pool);
    if (status != SALTS_OK) return status;
    return pool->storage != NULL ? SALTS_OK : SALTS_EINVAL;
}

static int managed_active_check(
    const object_pool_managed *pool, const object_pool_managed_lease *lease) {
    int status = object_pool_managed_check(pool);
    if (status != SALTS_EBUSY) return status == SALTS_OK ? SALTS_EINVAL : status;
    if (pool->storage == NULL || pool->active != lease) return SALTS_EINVAL;
    return managed_lease_valid(pool, lease);
}

static int managed_config(
    size_t size, size_t alignment, size_t capacity, object_pool_config_t *config) {
    size_t stride, slot_alignment;
    if (size == 0 || capacity == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 || alignment > object_pool_max_alignment())
        return SALTS_EINVAL;
    slot_alignment = alignment > sizeof(void *) ? alignment : sizeof(void *);
    stride = size > sizeof(void *) ? size : sizeof(void *);
    if (stride > SIZE_MAX - (slot_alignment - 1)) return SALTS_ENOSPC;
    stride = (stride + slot_alignment - 1) & ~(slot_alignment - 1);
    if (capacity > SIZE_MAX / stride) return SALTS_ENOSPC;
    config->object_size = stride;
    config->initial_capacity = capacity;
    config->max_capacity = capacity;
    config->zero_on_alloc = false;
    return SALTS_OK;
}

int object_pool_managed_validate(size_t size, size_t alignment, size_t capacity) {
    object_pool_config_t config;
    return managed_config(size, alignment, capacity, &config);
}

int object_pool_managed_init(
    object_pool_managed *pool, size_t size, size_t alignment, size_t capacity) {
    object_pool_config_t config;
    int status;
    if (pool == NULL || pool->storage != NULL || pool->active != NULL) return SALTS_EINVAL;
    status = managed_config(size, alignment, capacity, &config);
    if (status != SALTS_OK) return status;
    status = salts_local_begin(&pool->binding, pool);
    if (status != SALTS_OK) return status;
    pool->storage = object_pool_create_aligned(&config, alignment);
    if (pool->storage == NULL) {
        status = salts_local_reset(&pool->binding, pool);
        return status == SALTS_OK ? SALTS_ENOMEM : status;
    }
    return salts_local_publish(&pool->binding, pool);
}

int object_pool_managed_claim(object_pool_managed *pool, object_pool_managed_lease *lease) {
    int status = object_pool_managed_check(pool);
    void *value;
    if (status != SALTS_OK) return status;
    if (lease == NULL || lease->self != NULL || lease->owner != NULL || lease->value != NULL)
        return SALTS_EINVAL;
    if (object_pool_free_count(pool->storage) == 0) return SALTS_ENOSPC;
    status = salts_local_enter(&pool->binding, pool);
    if (status != SALTS_OK) return status;
    value = object_pool_alloc(pool->storage);
    /* Preallocated free slots cannot fail normally. Preserve BUSY on a broken
     * native allocation invariant rather than repairing untrusted state. */
    if (value == NULL) return SALTS_EINVAL;
    lease->value = value; lease->owner = pool; lease->self = lease;
    pool->active = lease;
    return SALTS_OK;
}

int object_pool_managed_publish(object_pool_managed *pool, object_pool_managed_lease *lease) {
    int status = managed_active_check(pool, lease);
    if (status != SALTS_OK) return status;
    status = salts_local_publish(&pool->binding, pool);
    if (status == SALTS_OK) pool->active = NULL;
    return status;
}

int object_pool_managed_discard(object_pool_managed *pool, object_pool_managed_lease *lease) {
    int status = managed_active_check(pool, lease);
    if (status != SALTS_OK) return status;
    object_pool_free(pool->storage, lease->value);
    *lease = (object_pool_managed_lease){0};
    pool->active = NULL;
    return salts_local_publish(&pool->binding, pool);
}

int object_pool_managed_lease_check(
    const object_pool_managed *pool, const object_pool_managed_lease *lease) {
    int status = object_pool_managed_check(pool);
    return status == SALTS_OK ? managed_lease_valid(pool, lease) : status;
}

void *object_pool_managed_get(
    const object_pool_managed *pool, const object_pool_managed_lease *lease) {
    return object_pool_managed_lease_check(pool, lease) == SALTS_OK ? lease->value : NULL;
}

int object_pool_managed_enter(object_pool_managed *pool, object_pool_managed_lease *lease) {
    int status = object_pool_managed_lease_check(pool, lease);
    if (status != SALTS_OK) return status;
    status = salts_local_enter(&pool->binding, pool);
    if (status == SALTS_OK) pool->active = lease;
    return status;
}

int object_pool_managed_move_begin(
    object_pool_managed *pool, object_pool_managed_lease *lease, const void *destination) {
    int status = object_pool_managed_lease_check(pool, lease);
    if (status != SALTS_OK) return status;
    if (destination == NULL || object_pool_contains(pool->storage, destination))
        return SALTS_EINVAL;
    return object_pool_managed_enter(pool, lease);
}

int object_pool_managed_destroy(object_pool_managed *pool) {
    int status = object_pool_managed_check(pool);
    if (status != SALTS_OK) return status;
    if (object_pool_allocated_count(pool->storage) != 0) return SALTS_EBUSY;
    status = salts_local_enter(&pool->binding, pool);
    if (status != SALTS_OK) return status;
    object_pool_destroy(pool->storage);
    pool->storage = NULL;
    return salts_local_reset(&pool->binding, pool);
}
