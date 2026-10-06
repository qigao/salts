#ifndef SALTS_OBJECT_POOL_MANAGED_H
#define SALTS_OBJECT_POOL_MANAGED_H

#include <object_pool.h>
#include <salts/local.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct object_pool_managed_lease object_pool_managed_lease;
/** Caller-owned, zero-initialized, address-stable single-threaded pool.
 * Fields are read-only to callers. Slot allocation/counts belong to storage;
 * address/thread/phase belong to binding; active names the sole BUSY lease.
 * No callbacks, metadata, locks, automatic drain or thread-exit cleanup.
 * Initialization/destroy require quiescence; use one Platform token instance.
 * Foreign calls are excluded from init/destroy, otherwise reject before reading
 * mutable storage/lease state. Destroy all payloads before owner thread exit. */
typedef struct object_pool_managed {
    object_pool_t *storage;
    salts_local_state binding;
    object_pool_managed_lease *active;
} object_pool_managed;

/** Zero-initialize before claim. A live lease is address-stable/noncopyable;
 * fields are read-only. value is borrowed until discard; BUSY payload access
 * is reserved for the transaction's caller. No borrow crosses thread migration. */
struct object_pool_managed_lease {
    void *value;
    object_pool_managed *owner;
    const object_pool_managed_lease *self;
};

/** Validate size/alignment/capacity without allocating or binding state.
 * EINVAL rejects configuration; ENOSPC rejects stride/capacity overflow.
 * Uses the same checks as init. */
SALTS_C_API int object_pool_managed_validate(size_t size, size_t alignment, size_t capacity);
/** Fixed capacity > 0, concrete size > 0, supported power-of-two alignment.
 * Preallocates every slot; no growth after init. Validation errors as above;
 * ENOMEM reports allocation failure, leaving the zero-initialized pool unbound. */
SALTS_C_API int object_pool_managed_init(
    object_pool_managed *pool, size_t size, size_t alignment, size_t capacity);
/** READY -> OK; original owner during a transaction -> EBUSY;
 * NULL/unbound/copied/wrong-thread -> EINVAL. */
SALTS_C_API int object_pool_managed_check(const object_pool_managed *pool);
/** Reserve one slot and enter BUSY with lease as the active transaction.
 * Caller initializes value before publish, or cleans it before discard.
 * ENOSPC means full; invalid/nonzero lease -> EINVAL; reentry -> EBUSY.
 * Normal rejection preserves state; no payload initialization runs.
 * A broken native free-slot invariant reports EINVAL and retains BUSY. */
SALTS_C_API int object_pool_managed_claim(
    object_pool_managed *pool, object_pool_managed_lease *lease);
/** End the active BUSY transaction -> READY, preserving its live lease.
 * Used after construction/mutation. Wrong active lease/phase -> EINVAL.
 * Logical publication only; external synchronization governs sharing. */
SALTS_C_API int object_pool_managed_publish(
    object_pool_managed *pool, object_pool_managed_lease *lease);
/** Return the active BUSY slot after caller cleanup, zero lease, enter READY.
 * Covers failed construction and release; no destructor runs. */
SALTS_C_API int object_pool_managed_discard(
    object_pool_managed *pool, object_pool_managed_lease *lease);
/** Validate a READY live lease including self/owner/native allocation.
 * EBUSY during any transaction; EINVAL for copied/wrong/released/NULL leases. */
SALTS_C_API int object_pool_managed_lease_check(
    const object_pool_managed *pool, const object_pool_managed_lease *lease);
/** Borrow a READY value; NULL on failed lease_check. */
SALTS_C_API void *object_pool_managed_get(
    const object_pool_managed *pool, const object_pool_managed_lease *lease);
/** Enter BUSY on a checked READY lease before caller mutation/destruction.
 * Finish exactly once through publish or discard. */
SALTS_C_API int object_pool_managed_enter(
    object_pool_managed *pool, object_pool_managed_lease *lease);
/** Like enter, also requires non-NULL destination outside all pool storage.
 * Caller owns a live semantic-zero destination; native owner cannot validate
 * its layout/lifecycle. After move, publish keeps the source lease live. */
SALTS_C_API int object_pool_managed_move_begin(
    object_pool_managed *pool, object_pool_managed_lease *lease, const void *destination);
/** Reject transactions/outstanding slots with EBUSY. Destroy READY empty
 * storage and return pool to zero. Repeated destroy -> EINVAL. */
SALTS_C_API int object_pool_managed_destroy(object_pool_managed *pool);

#ifdef __cplusplus
}
#endif
#endif
