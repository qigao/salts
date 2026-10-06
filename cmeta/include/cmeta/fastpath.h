#ifndef CMETA_FASTPATH_H
#define CMETA_FASTPATH_H

#include <cmeta/function.h>
#include <salts/fastpath.h>

#ifndef SALTS_PLATFORM_NATIVE_FASTPATH
#define SALTS_PLATFORM_NATIVE_FASTPATH 0
#endif

#ifndef __cplusplus
#include <stdatomic.h>

#if SALTS_PLATFORM_NATIVE_FASTPATH
#if !defined(_MSC_VER) || defined(__clang__)
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2,
               "CMeta native static call requires always-lock-free atomic pointer storage");
#endif
#define CMETA_STATIC_NATIVE_SLOT_(name_) \
    _Static_assert(sizeof(name_##_target_type) == sizeof(uint64_t) && \
                   sizeof(_Atomic(name_##_target_type)) == sizeof(uint64_t) && \
                   _Alignof(_Atomic(name_##_target_type)) >= _Alignof(uint64_t) && \
                   offsetof(name_##_slot_type, target) == 0u, \
                   "CMeta native call requires aligned 64-bit atomic target"); \
    CMETA_INLINE name_##_target_type name_##_load_native( \
        const name_##_slot_type *slot_) { \
        return (name_##_target_type)cmeta_fast_target_load_native(slot_); \
    }
#define cmeta_static_native_invoke(name_, ...) \
    name_##_load_native(&(name_))(__VA_ARGS__)
#define cmeta_static_native_invoke0(name_) name_##_load_native(&(name_))()
#else
#define CMETA_STATIC_NATIVE_SLOT_(name_)
#endif

/*
 * Typed static-call projection over Platform-owned raw target loads.
 *
 * CMeta owns declaration/signature/ABI validation. Platform owns the native
 * acquire-load backend and architecture policy. No Reflection lookup occurs on
 * invocation.
 */
#define cmeta_static_call(name_, default_) \
    typedef default_##_function_type name_##_target_type; \
    typedef struct name_##_slot_type { \
        _Atomic(name_##_target_type) target; \
    } name_##_slot_type; \
    CMETA_INLINE name_##_target_type name_##_load( \
        const name_##_slot_type *slot_) { \
        return atomic_load_explicit(&slot_->target, memory_order_acquire); \
    } \
    CMETA_INLINE cmeta_status name_##_set( \
        name_##_slot_type *slot_, name_##_target_type target_, \
        const cmeta_function_abi_desc *abi_) { \
        const cmeta_function_abi_desc *expected_ = default_##_function_abi(); \
        if (slot_ == NULL || target_ == NULL || \
            !cmeta_function_abi_desc_valid(expected_) || \
            !cmeta_function_abi_desc_valid(abi_)) \
            return CMETA_INVALID_ARGUMENT; \
        if (!cmeta_function_abi_contract_compatible(expected_, abi_)) \
            return CMETA_TYPE_MISMATCH; \
        atomic_store_explicit(&slot_->target, target_, memory_order_release); \
        return CMETA_OK; \
    } \
    CMETA_STATIC_NATIVE_SLOT_(name_) \
    name_##_slot_type name_ = {default_}

#define cmeta_static_update(name_, function_) \
    name_##_set(&(name_), \
        _Generic((function_), name_##_target_type: (function_)), \
        function_##_function_abi())

#define cmeta_static_invoke(name_, ...) name_##_load(&(name_))(__VA_ARGS__)
#define cmeta_static_invoke0(name_) name_##_load(&(name_))()

#endif /* !__cplusplus */

#endif
