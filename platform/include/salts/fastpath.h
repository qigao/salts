#ifndef SALTS_FASTPATH_H
#define SALTS_FASTPATH_H

#include <salts/platform.h>
#include <salts/error_codes.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef SALTS_NATIVE_FASTPATH
#define SALTS_NATIVE_FASTPATH 0
#endif

typedef struct salts_static_key_state salts_static_key_state;
/** Acquire read; key must be live and non-NULL. C++ borrows C-owned storage. */
SALTS_PLATFORM_C_API bool salts_static_key_read(const salts_static_key_state *key);
/** Release publication. NULL returns SALTS_EINVAL without mutation. */
SALTS_PLATFORM_C_API int salts_static_key_set(salts_static_key_state *key, bool enabled);
/** Consume one armed permission. Live/non-NULL key; concurrent arms coalesce. */
SALTS_PLATFORM_C_API bool salts_fault_consume(salts_static_key_state *key);
#if SALTS_NATIVE_FASTPATH
SALTS_PLATFORM_C_API bool salts_static_branch_native(const salts_static_key_state *key);
/* Raw acquire load for qualified typed adapters: live/non-NULL slot,
 * aligned 64-bit atomic function pointer at offset zero. No lifetime retention. */
typedef void (*salts_static_native_target_type)(void);
SALTS_PLATFORM_C_API salts_static_native_target_type salts_static_native_target(const void *slot);
#endif

#ifdef __cplusplus
#define salts_static_branch(key_) salts_static_key_read(key_)
#define salts_fault_hit(key_) salts_fault_consume(key_)
#else
#include <stdatomic.h>
struct salts_static_key_state { atomic_bool enabled; };
/** One bounded key. Initialization/destruction require quiescent users;
 * publication/reads/consumption permit MPMC access. Disable does not drain. */
#define salts_static_key(name_, initial_) salts_static_key_state name_ = {(initial_)}
static inline bool salts_static_branch(const salts_static_key_state *key) {
    return atomic_load_explicit(&key->enabled, memory_order_acquire);
}
static inline bool salts_fault_hit(salts_static_key_state *key) {
    return salts_static_branch(key) &&
        atomic_exchange_explicit(&key->enabled, false, memory_order_acq_rel);
}
#if SALTS_NATIVE_FASTPATH
#if !defined(_MSC_VER) || defined(__clang__)
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2,
               "Platform native loads require always-lock-free atomic storage");
#endif
/* MSVC reports 1, but its qualified implementation supports sizes <= 8.
 * The backend key ABI still requires an atomic byte at offset zero. */
_Static_assert(sizeof(atomic_bool) == 1u &&
               offsetof(salts_static_key_state, enabled) == 0u,
               "Platform native key requires one-byte atomic bool at offset zero");
#endif
#define salts_fault_point(name_) salts_static_key(name_, false)
#define salts_fault_arm(name_) salts_static_enable(&(name_))
#define salts_fault_disarm(name_) salts_static_disable(&(name_))
#endif

static inline int salts_static_enable(salts_static_key_state *key) {
    return salts_static_key_set(key, true);
}
static inline int salts_static_disable(salts_static_key_state *key) {
    return salts_static_key_set(key, false);
}

#endif
