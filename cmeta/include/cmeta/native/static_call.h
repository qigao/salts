#ifndef CMETA_NATIVE_STATIC_CALL_H
#define CMETA_NATIVE_STATIC_CALL_H
#include <cmeta/fastpath.h>
#include <cmeta/native/thunk.h>

#ifndef __cplusplus
/* Explicit int(int) opt-in over the existing atomic slot. The slot borrows a
 * thunk entry; update to an ordinary target and quiesce all old readers before
 * rebind/destroy. Atomic publication alone does not reclaim executable code.
 * Normal cmeta_static_update/invoke remain available on this declaration. */
#define cmeta_static_thunk_call(name_, default_) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&(default_), cmeta_native_i32_fn), \
        "CMeta native static call requires exact int(int)"); \
    cmeta_static_call(name_, default_); \
    CMETA_INLINE cmeta_status name_##_set_thunk(name_##_slot_type *slot_, \
        const cmeta_native_thunk *thunk_) { \
        cmeta_native_i32_fn entry_ = cmeta_native_thunk_entry(thunk_); \
        if (entry_ == NULL) return CMETA_INVALID_ARGUMENT; \
        return name_##_set(slot_, entry_, thunk_->abi); \
    } \
    typedef char name_##_thunk_slot_complete[1]

/* Evaluates thunk_ once. ABI/shape errors leave the published target unchanged. */
#define cmeta_static_thunk_update(name_, thunk_) name_##_set_thunk(&(name_), (thunk_))
#endif
#endif
