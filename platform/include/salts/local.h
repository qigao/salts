#ifndef SALTS_LOCAL_H
#define SALTS_LOCAL_H

#include <salts/thread.h>
#include <salts/error_codes.h>
#include <stddef.h>

typedef enum salts_local_phase {
    SALTS_LOCAL_ZERO = 0,
    SALTS_LOCAL_BUSY = 1,
    SALTS_LOCAL_READY = 2
} salts_local_phase;

/** One caller-owned, zero-initialized thread binding. Single owner; address
 * stable and noncopyable. Payload storage/lifecycle belong to the caller.
 * No allocation, callbacks, TLS registration or implicit thread-exit cleanup.
 * Initialization/reset require externally quiescent users. Destroy payload and
 * reset before owner thread exit; a borrow cannot cross thread migration.
 * Calls use the same Platform runtime/token instance that created the binding.
 * After begin, fields are read-only to callers; transitions use this API.
 * Foreign checks are safe while the published binding's identity stays live;
 * callers must exclude foreign calls from initialization/reset. */
typedef struct salts_local_state {
    const struct salts_local_state *self;
    const void *owner;
    const void *thread;
    salts_local_phase phase;
} salts_local_state;

/** ZERO -> BUSY. owner is a live, stable, non-NULL address. Only success binds
 * the current thread. Duplicate/invalid initialization returns SALTS_EINVAL. */
SALTS_PLATFORM_C_API int salts_local_begin(salts_local_state *state, const void *owner);
/** BUSY -> READY after the owner finishes payload construction/mutation.
 * This is a logical transition; sharing uses external synchronization. */
SALTS_PLATFORM_C_API int salts_local_publish(salts_local_state *state, const void *owner);
/** READY returns SALTS_OK; original owner in BUSY returns SALTS_EBUSY.
 * NULL, copied, unbound, wrong owner/thread or invalid phase returns EINVAL. */
SALTS_PLATFORM_C_API int salts_local_check(const salts_local_state *state, const void *owner);
/** READY -> BUSY. Blocks recursive access before the caller runs lifecycle
 * callbacks. BUSY returns EBUSY without mutation; invalid bindings return EINVAL. */
SALTS_PLATFORM_C_API int salts_local_enter(salts_local_state *state, const void *owner);
/** BUSY -> ZERO after failed construction or completed payload destruction.
 * Repeated reset or reset while READY returns EINVAL. No payload callback runs.
 * All transitions return SALTS_OK on success; failures leave state unchanged. */
SALTS_PLATFORM_C_API int salts_local_reset(salts_local_state *state, const void *owner);

#endif
