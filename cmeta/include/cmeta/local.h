#ifndef CMETA_LOCAL_H
#define CMETA_LOCAL_H
#include <cmeta/generic.h>
#include <cmeta/lifecycle.h>
#include <salts/local.h>

/* Zero-initialize before first use. Explicit construction/destruction;
 * TLS declarations do not run callbacks. Init/destroy require quiescence.
 * Both ordinary Local instances and TLS instances are thread-affine and must
 * keep their original address. Destroy before owner thread exit. A borrow may
 * not cross suspension which can migrate execution to another thread.
 * Shard-local ownership belongs to the executor and is not emulated by TLS. */
/* The only CMeta-owned state is the borrowed canonical lifecycle binding.
 * Platform is the sole authority for address/thread/phase transitions. */
typedef struct cmeta_local_state {
    salts_local_state binding;
    const cmeta_data_construct_ops *ops;
} cmeta_local_state;
CMETA_INLINE cmeta_status cmeta_local_native_status_(int status) {
    if (status == SALTS_OK) return CMETA_OK;
    return status == SALTS_EBUSY ? CMETA_BUSY : CMETA_INVALID_ARGUMENT;
}
CMETA_INLINE cmeta_status cmeta_local_check(cmeta_local_state *state, const void *owner) {
    return state != NULL && state->ops != NULL ?
        cmeta_local_native_status_(salts_local_check(&state->binding, owner)) : CMETA_INVALID_ARGUMENT;
}
CMETA_INLINE cmeta_status cmeta_local_init(
    cmeta_local_state *state, void *owner, void *value, const cmeta_data_desc *data,
    size_t size, size_t align) {
    const cmeta_data_construct_ops *ops;
    if (state == NULL || owner == NULL || value == NULL || state->ops != NULL)
        return CMETA_INVALID_ARGUMENT;
    cmeta_status status = cmeta_lifecycle_bind(data, size, align, &ops);
    if (status != CMETA_OK) return status;
    status = cmeta_local_native_status_(salts_local_begin(&state->binding, owner));
    if (status != CMETA_OK) return status;
    state->ops = ops;
    status = ops->init_zero(value);
    if (status != CMETA_OK) {
        ops->restore_zero(value);
        cmeta_status reset = cmeta_local_native_status_(salts_local_reset(&state->binding, owner));
        if (reset != CMETA_OK) return reset;
        state->ops = NULL;
        return status;
    }
    return cmeta_local_native_status_(salts_local_publish(&state->binding, owner));
}
CMETA_INLINE cmeta_status cmeta_local_destroy(cmeta_local_state *state, void *owner, void *value) {
    if (state == NULL || value == NULL || state->ops == NULL) return CMETA_INVALID_ARGUMENT;
    cmeta_status status = cmeta_local_native_status_(salts_local_enter(&state->binding, owner));
    if (status != CMETA_OK) return status;
    state->ops->restore_zero(value);
    status = cmeta_local_native_status_(salts_local_reset(&state->binding, owner));
    if (status == CMETA_OK) state->ops = NULL;
    return status;
}
#define CMETA_GENERIC_KIND_Local CMETA_GENERIC_PROBE()
#define CMETA_TYPED_Local(name_, type_) \
    typedef struct name_ { cmeta_local_state state; type_ value; } name_; \
    CMETA_INLINE cmeta_status name_##_init(name_ *p) { \
        if (p == NULL) return CMETA_INVALID_ARGUMENT; \
        return cmeta_local_init(&p->state, p, &p->value, CMETA_DATA_ACCESSOR_(type_)(), \
                                sizeof(type_), CMETA_ALIGNOF(type_)); \
    } \
    CMETA_INLINE type_ *name_##_get(name_ *p) { \
        return p != NULL && cmeta_local_check(&p->state, p) == CMETA_OK ? &p->value : NULL; \
    } \
    CMETA_INLINE cmeta_status name_##_destroy(name_ *p) { \
        return p != NULL ? cmeta_local_destroy(&p->state, p, &p->value) : CMETA_INVALID_ARGUMENT; \
    } \
    typedef type_ name_##_value_type
#endif
