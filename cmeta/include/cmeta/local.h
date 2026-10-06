#ifndef CMETA_LOCAL_H
#define CMETA_LOCAL_H
#include <cmeta/generic.h>
#include <cmeta/lifecycle.h>
#include <salts/thread.h>
#include <stdbool.h>

/* Zero-initialize before first use. Explicit construction/destruction;
 * TLS declarations do not run callbacks. Init/destroy require quiescence.
 * Both ordinary Local instances and TLS instances are thread-affine and must
 * keep their original address. Destroy before owner thread exit. A borrow may
 * not cross suspension which can migrate execution to another thread.
 * Shard-local ownership belongs to the executor and is not emulated by TLS. */
typedef struct cmeta_local_state {
    const void *owner;
    const void *thread;
    const cmeta_data_construct_ops *ops;
    bool busy;
} cmeta_local_state;
CMETA_INLINE cmeta_status cmeta_local_check(cmeta_local_state *state, const void *owner) {
    if (state == NULL || state->owner != owner || owner == NULL ||
        state->thread != salts_thread_current_token()) return CMETA_INVALID_ARGUMENT;
    return state->busy ? CMETA_BUSY : CMETA_OK;
}
CMETA_INLINE cmeta_status cmeta_local_init(
    cmeta_local_state *state, void *owner, void *value, const cmeta_data_desc *data,
    size_t size, size_t align) {
    const cmeta_data_construct_ops *ops;
    cmeta_status status;
    if (state == NULL || owner == NULL || state->owner != NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_lifecycle_bind(data, size, align, &ops);
    if (status != CMETA_OK) return status;
    state->owner = owner; state->thread = salts_thread_current_token();
    state->ops = ops; state->busy = true;
    status = ops->init_zero(value);
    if (status != CMETA_OK) { ops->restore_zero(value); *state = (cmeta_local_state){0}; }
    else state->busy = false;
    return status;
}
CMETA_INLINE cmeta_status cmeta_local_destroy(cmeta_local_state *state, void *owner, void *value) {
    cmeta_status status = cmeta_local_check(state, owner);
    if (status != CMETA_OK) return status;
    state->busy = true;
    state->ops->restore_zero(value);
    *state = (cmeta_local_state){0};
    return CMETA_OK;
}
#define cmeta_thread_local(type_, name_) SALTS_THREAD_LOCAL type_ name_ = {0}
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
