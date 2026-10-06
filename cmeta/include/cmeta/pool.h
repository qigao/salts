#ifndef CMETA_POOL_H
#define CMETA_POOL_H
#include <cmeta/generic.h>
#include <cmeta/lifecycle.h>
#include <object_pool.h>
#include <salts/thread.h>

/* Optional Salts::Core facade. Pools and leases must remain at their original
 * addresses on their initializing thread. Zero-initialize before first use.
 * Borrowed values expire on release. Destroy rejects outstanding leases.
 * Canonical callbacks may not reenter the same pool. Slot storage never grows
 * after init; payload allocation follows the concrete type's own contract.
 */
typedef struct cmeta_pool_state {
    object_pool_t *storage;
    const struct cmeta_pool_state *self;
    const void *thread;
    const cmeta_data_construct_ops *ops;
    bool busy;
} cmeta_pool_state;
typedef struct cmeta_pool_lease {
    void *value;
    cmeta_pool_state *owner;
    const struct cmeta_pool_lease *self;
} cmeta_pool_lease;

CMETA_INLINE cmeta_status cmeta_pool_check(cmeta_pool_state *pool) {
    if (pool == NULL || pool->self != pool || pool->storage == NULL ||
        pool->thread != salts_thread_current_token()) return CMETA_INVALID_ARGUMENT;
    return pool->busy ? CMETA_BUSY : CMETA_OK;
}
CMETA_INLINE cmeta_status cmeta_pool_init(
    cmeta_pool_state *pool, const cmeta_data_desc *data,
    size_t size, size_t align, size_t capacity) {
    object_pool_config_t config;
    const cmeta_data_construct_ops *ops;
    cmeta_status status;
    size_t stride, slot_align;
    if (pool == NULL || pool->self != NULL || pool->storage != NULL || capacity == 0 ||
        align == 0 || (align & (align - 1)) != 0 || align > object_pool_max_alignment())
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_lifecycle_bind(data, size, align, &ops);
    if (status != CMETA_OK) return status;
    slot_align = align > sizeof(void *) ? align : sizeof(void *);
    stride = size > sizeof(void *) ? size : sizeof(void *);
    if (stride > SIZE_MAX - (slot_align - 1)) return CMETA_CAPACITY_EXCEEDED;
    stride = (stride + slot_align - 1) & ~(slot_align - 1);
    if (capacity > SIZE_MAX / stride) return CMETA_CAPACITY_EXCEEDED;
    config.object_size = stride;
    config.initial_capacity = capacity;
    config.max_capacity = capacity;
    config.zero_on_alloc = false;
    pool->storage = object_pool_create_aligned(&config, align);
    if (pool->storage == NULL) return CMETA_OUT_OF_MEMORY;
    pool->self = pool; pool->thread = salts_thread_current_token(); pool->ops = ops;
    return CMETA_OK;
}
CMETA_INLINE cmeta_status cmeta_pool_acquire(cmeta_pool_state *pool, cmeta_pool_lease *lease) {
    cmeta_status status = cmeta_pool_check(pool);
    void *value;
    if (status != CMETA_OK) return status;
    if (lease == NULL || lease->self != NULL || lease->owner != NULL || lease->value != NULL)
        return CMETA_INVALID_ARGUMENT;
    value = object_pool_alloc(pool->storage);
    if (value == NULL) return CMETA_CAPACITY_EXCEEDED;
    pool->busy = true;
    status = pool->ops->init_zero(value);
    if (status != CMETA_OK) {
        pool->ops->restore_zero(value);
        object_pool_free(pool->storage, value);
    } else {
        lease->owner = pool; lease->self = lease; lease->value = value;
    }
    pool->busy = false;
    return status;
}
CMETA_INLINE cmeta_status cmeta_pool_lease_check(cmeta_pool_state *pool, cmeta_pool_lease *lease) {
    cmeta_status status = cmeta_pool_check(pool);
    if (status != CMETA_OK) return status;
    if (lease == NULL || lease->self != lease || lease->owner != pool ||
        !object_pool_is_allocated(pool->storage, lease->value)) return CMETA_INVALID_ARGUMENT;
    return CMETA_OK;
}
CMETA_INLINE void *cmeta_pool_get(cmeta_pool_state *pool, cmeta_pool_lease *lease) {
    return cmeta_pool_lease_check(pool, lease) == CMETA_OK ? lease->value : NULL;
}
CMETA_INLINE cmeta_status cmeta_pool_release(cmeta_pool_state *pool, cmeta_pool_lease *lease) {
    cmeta_status status = cmeta_pool_lease_check(pool, lease);
    if (status != CMETA_OK) return status;
    pool->busy = true;
    pool->ops->restore_zero(lease->value);
    object_pool_free(pool->storage, lease->value);
    *lease = (cmeta_pool_lease){0};
    pool->busy = false;
    return CMETA_OK;
}
/* Destination must be external, live semantic-zero storage. The moved-from
 * lease remains live and must still be released. */
CMETA_INLINE cmeta_status cmeta_pool_move_out(
    cmeta_pool_state *pool, cmeta_pool_lease *lease, void *destination) {
    cmeta_status status = cmeta_pool_lease_check(pool, lease);
    if (status != CMETA_OK) return status;
    if (destination == NULL || destination == lease->value ||
        object_pool_is_allocated(pool->storage, destination)) return CMETA_INVALID_ARGUMENT;
    if (pool->ops->move == NULL) return CMETA_TRAIT_MISSING;
    pool->busy = true;
    pool->ops->move(destination, lease->value);
    pool->busy = false;
    return CMETA_OK;
}
CMETA_INLINE cmeta_status cmeta_pool_destroy(cmeta_pool_state *pool) {
    cmeta_status status = cmeta_pool_check(pool);
    if (status != CMETA_OK) return status;
    if (object_pool_allocated_count(pool->storage) != 0) return CMETA_BUSY;
    object_pool_destroy(pool->storage);
    *pool = (cmeta_pool_state){0};
    return CMETA_OK;
}

#define CMETA_GENERIC_KIND_Pool CMETA_GENERIC_PROBE()
#define CMETA_TYPED_Pool(name_, type_) \
    typedef struct name_ { cmeta_pool_state state; } name_; \
    typedef struct name_##_lease { cmeta_pool_lease state; } name_##_lease; \
    CMETA_INLINE cmeta_status name_##_init(name_ *p, size_t capacity) { \
        return cmeta_pool_init(p != NULL ? &p->state : NULL, CMETA_DATA_ACCESSOR_(type_)(), \
                               sizeof(type_), CMETA_ALIGNOF(type_), capacity); \
    } \
    CMETA_INLINE cmeta_status name_##_acquire(name_ *p, name_##_lease *l) { \
        return cmeta_pool_acquire(p != NULL ? &p->state : NULL, l != NULL ? &l->state : NULL); \
    } \
    CMETA_INLINE type_ *name_##_get(name_ *p, name_##_lease *l) { \
        return (type_ *)cmeta_pool_get(p != NULL ? &p->state : NULL, l != NULL ? &l->state : NULL); \
    } \
    CMETA_INLINE cmeta_status name_##_release(name_ *p, name_##_lease *l) { \
        return cmeta_pool_release(p != NULL ? &p->state : NULL, l != NULL ? &l->state : NULL); \
    } \
    CMETA_INLINE cmeta_status name_##_move_out(name_ *p, name_##_lease *l, type_ *dst) { \
        return cmeta_pool_move_out(p != NULL ? &p->state : NULL, l != NULL ? &l->state : NULL, dst); \
    } \
    CMETA_INLINE cmeta_status name_##_destroy(name_ *p) { \
        return cmeta_pool_destroy(p != NULL ? &p->state : NULL); \
    } \
    typedef type_ name_##_value_type
#endif
