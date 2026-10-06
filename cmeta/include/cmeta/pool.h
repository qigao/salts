#ifndef CMETA_POOL_H
#define CMETA_POOL_H
#include <cmeta/generic.h>
#include <cmeta/lifecycle.h>
#include <object_pool_managed.h>

/* Optional lifecycle projection. Core owns storage, capacity, affinity, leases
 * and callback exclusion. Borrowed canonical ops remain live until destroy.
 * Callbacks may reenter the facade (BUSY) but must not mutate owner fields or
 * complete its native transaction. Borrowed values expire on release. */
typedef struct cmeta_pool_state {
    object_pool_managed owner;
    const cmeta_data_construct_ops *ops;
} cmeta_pool_state;

CMETA_INLINE cmeta_status cmeta_pool_native_status_(int status) {
    if (status == SALTS_OK) return CMETA_OK;
    if (status == SALTS_EBUSY) return CMETA_BUSY;
    if (status == SALTS_ENOSPC) return CMETA_CAPACITY_EXCEEDED;
    if (status == SALTS_ENOMEM) return CMETA_OUT_OF_MEMORY;
    return CMETA_INVALID_ARGUMENT;
}
CMETA_INLINE cmeta_status cmeta_pool_check(cmeta_pool_state *pool) {
    if (pool == NULL || pool->ops == NULL) return CMETA_INVALID_ARGUMENT;
    return cmeta_pool_native_status_(object_pool_managed_check(&pool->owner));
}
CMETA_INLINE cmeta_status cmeta_pool_init(
    cmeta_pool_state *pool, const cmeta_data_desc *data,
    size_t size, size_t align, size_t capacity) {
    const cmeta_data_construct_ops *ops;
    cmeta_status status;
    int native_status;
    if (pool == NULL || pool->ops != NULL) return CMETA_INVALID_ARGUMENT;
    native_status = object_pool_managed_validate(size, align, capacity);
    if (native_status != SALTS_OK) return cmeta_pool_native_status_(native_status);
    status = cmeta_lifecycle_bind(data, size, align, &ops);
    if (status != CMETA_OK) return status;
    native_status = object_pool_managed_init(&pool->owner, size, align, capacity);
    if (native_status != SALTS_OK) return cmeta_pool_native_status_(native_status);
    pool->ops = ops;
    return CMETA_OK;
}
CMETA_INLINE cmeta_status cmeta_pool_acquire(
    cmeta_pool_state *pool, object_pool_managed_lease *lease) {
    cmeta_status status = cmeta_pool_check(pool);
    int native_status;
    if (status != CMETA_OK) return status;
    native_status = object_pool_managed_claim(&pool->owner, lease);
    if (native_status != SALTS_OK) return cmeta_pool_native_status_(native_status);
    status = pool->ops->init_zero(lease->value);
    if (status != CMETA_OK) {
        pool->ops->restore_zero(lease->value);
        native_status = object_pool_managed_discard(&pool->owner, lease);
        return native_status == SALTS_OK ? status : cmeta_pool_native_status_(native_status);
    }
    return cmeta_pool_native_status_(object_pool_managed_publish(&pool->owner, lease));
}
CMETA_INLINE void *cmeta_pool_get(cmeta_pool_state *pool, object_pool_managed_lease *lease) {
    if (pool == NULL || pool->ops == NULL) return NULL;
    return object_pool_managed_get(&pool->owner, lease);
}
CMETA_INLINE cmeta_status cmeta_pool_release(
    cmeta_pool_state *pool, object_pool_managed_lease *lease) {
    cmeta_status status = cmeta_pool_check(pool);
    int native_status;
    if (status != CMETA_OK) return status;
    native_status = object_pool_managed_enter(&pool->owner, lease);
    if (native_status != SALTS_OK) return cmeta_pool_native_status_(native_status);
    pool->ops->restore_zero(lease->value);
    return cmeta_pool_native_status_(object_pool_managed_discard(&pool->owner, lease));
}
/* Destination is external, live semantic-zero storage. Source lease remains
 * live after the canonical move and must subsequently be released. */
CMETA_INLINE cmeta_status cmeta_pool_move_out(
    cmeta_pool_state *pool, object_pool_managed_lease *lease, void *destination) {
    cmeta_status status = cmeta_pool_check(pool);
    int native_status;
    if (status != CMETA_OK) return status;
    native_status = object_pool_managed_move_begin(&pool->owner, lease, destination);
    if (native_status != SALTS_OK) return cmeta_pool_native_status_(native_status);
    if (pool->ops->move == NULL) {
        native_status = object_pool_managed_publish(&pool->owner, lease);
        return native_status == SALTS_OK ? CMETA_TRAIT_MISSING : cmeta_pool_native_status_(native_status);
    }
    pool->ops->move(destination, lease->value);
    return cmeta_pool_native_status_(object_pool_managed_publish(&pool->owner, lease));
}
CMETA_INLINE cmeta_status cmeta_pool_destroy(cmeta_pool_state *pool) {
    cmeta_status status = cmeta_pool_check(pool);
    int native_status;
    if (status != CMETA_OK) return status;
    native_status = object_pool_managed_destroy(&pool->owner);
    if (native_status == SALTS_OK) pool->ops = NULL;
    return cmeta_pool_native_status_(native_status);
}

#define CMETA_GENERIC_KIND_Pool CMETA_GENERIC_PROBE()
#define CMETA_TYPED_Pool(name_, type_) \
    typedef struct name_ { cmeta_pool_state state; } name_; \
    typedef struct name_##_lease { object_pool_managed_lease state; } name_##_lease; \
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
