#ifndef CMETA_FASTPATH_H
#define CMETA_FASTPATH_H

#include <cmeta/function.h>

#ifndef CMETA_NATIVE_FASTPATH
#define CMETA_NATIVE_FASTPATH 0
#endif

/* C owns atomic storage. C++ consumers borrow opaque C objects rather than
 * relying on std::atomic/_Atomic layout equivalence. */
typedef struct cmeta_static_key_state cmeta_static_key_state;
#ifdef __cplusplus
extern "C" {
#endif
/** key must be live and non-NULL; acquire read of its published state. */
bool cmeta_static_key_read(const cmeta_static_key_state *key);
/** Release publication; NULL returns INVALID_ARGUMENT without mutation. */
cmeta_status cmeta_static_key_set(cmeta_static_key_state *key, bool enabled);
#if CMETA_NATIVE_FASTPATH
/** Qualified native acquire read; same key lifetime contract as reference. */
bool cmeta_static_branch_native(const cmeta_static_key_state *key);
/* Backend ABI for generated typed slots; applications use the typed facade. */
typedef void (*cmeta_static_native_target_type)(void);
cmeta_static_native_target_type cmeta_static_native_target(const void *slot);
#endif
#ifdef __cplusplus
}
#define cmeta_static_branch(key_) cmeta_static_key_read(key_)
#else
#include <stdatomic.h>
struct cmeta_static_key_state { atomic_bool enabled; };
#define cmeta_static_key(name_, initial_) \
    cmeta_static_key_state name_ = {(initial_)}
CMETA_INLINE bool cmeta_static_branch(const cmeta_static_key_state *key) {
    return atomic_load_explicit(&key->enabled, memory_order_acquire);
}
#if CMETA_NATIVE_FASTPATH
#if !defined(_MSC_VER) || defined(__clang__)
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2,
               "CMeta native loads require always-lock-free atomic storage");
#endif
/* MSVC's C header reports 1 for every lock-free macro, but its supported
 * _Atomic_is_lock_free implementation guarantees power-of-two sizes <= 8.
 * The one/eight-byte size and alignment assertions qualify that ABI below. */
_Static_assert(sizeof(atomic_bool) == 1u &&
               offsetof(cmeta_static_key_state, enabled) == 0u,
               "CMeta native key requires one-byte atomic bool at offset zero");
#define CMETA_STATIC_NATIVE_SLOT_(name_) \
    _Static_assert(sizeof(name_##_target_type) == sizeof(uint64_t) && \
                   sizeof(_Atomic(name_##_target_type)) == sizeof(uint64_t) && \
                   _Alignof(_Atomic(name_##_target_type)) >= _Alignof(uint64_t) && \
                   offsetof(name_##_slot_type, target) == 0u, \
                   "CMeta native call requires aligned 64-bit atomic target"); \
    CMETA_INLINE name_##_target_type name_##_load_native(const name_##_slot_type *slot_) { \
        return (name_##_target_type)cmeta_static_native_target(slot_); \
    }
#define cmeta_static_native_invoke(name_, ...) name_##_load_native(&(name_))(__VA_ARGS__)
#define cmeta_static_native_invoke0(name_) name_##_load_native(&(name_))()
#else
#define CMETA_STATIC_NATIVE_SLOT_(name_)
#endif

/** Declare one typed atomic call slot from an existing FunctionDecl/Function0Decl.
 * The default and replacement providers must outlive every reader and in-flight
 * call. update does not drain or unload old targets. Storage is bounded, no heap.
 * ABI views are borrowed during update only; no candidate descriptor is retained.
 * Declaration/initialization is quiescent; updates and loads permit MPMC access. */
#define cmeta_static_call(name_, default_) \
    typedef default_##_function_type name_##_target_type; \
    typedef struct name_##_slot_type { _Atomic(name_##_target_type) target; } name_##_slot_type; \
    CMETA_INLINE name_##_target_type name_##_load(const name_##_slot_type *slot_) { \
        return atomic_load_explicit(&slot_->target, memory_order_acquire); \
    } \
    CMETA_INLINE cmeta_status name_##_set(name_##_slot_type *slot_, \
        name_##_target_type target_, const cmeta_function_abi_desc *abi_) { \
        const cmeta_function_abi_desc *expected_ = default_##_function_abi(); \
        if (slot_ == NULL || target_ == NULL || \
            !cmeta_function_abi_desc_valid(expected_) || \
            !cmeta_function_abi_desc_valid(abi_)) return CMETA_INVALID_ARGUMENT; \
        if (!cmeta_function_abi_contract_compatible(expected_, abi_)) return CMETA_TYPE_MISMATCH; \
        atomic_store_explicit(&slot_->target, target_, memory_order_release); \
        return CMETA_OK; \
    } \
    CMETA_STATIC_NATIVE_SLOT_(name_) \
    name_##_slot_type name_ = {default_}

/** The symbol supplies both its checked C pointer type and canonical ABI view. */
#define cmeta_static_update(name_, function_) \
    name_##_set(&(name_), _Generic((function_), name_##_target_type: (function_)), \
               function_##_function_abi())
#define cmeta_static_invoke(name_, ...) name_##_load(&(name_))(__VA_ARGS__)
#define cmeta_static_invoke0(name_) name_##_load(&(name_))()
#endif

CMETA_INLINE cmeta_status cmeta_static_enable(cmeta_static_key_state *key) {
    return cmeta_static_key_set(key, true);
}
CMETA_INLINE cmeta_status cmeta_static_disable(cmeta_static_key_state *key) {
    return cmeta_static_key_set(key, false);
}

#endif
