#ifndef SALTS_RCU_H
#define SALTS_RCU_H

#include <salts/concurrency.h>
#include <salts/error_codes.h>
#include <stddef.h>

typedef struct salts_rcu { void *impl; } salts_rcu;
typedef struct salts_rcu_guard {
  salts_rcu *owner;
  const struct salts_rcu_guard *self;
  const void *value;
  unsigned epoch;
} salts_rcu_guard;

/** Zero-initialized, noncopyable domain; transfers initial on success only.
 * MPMC readers/writers, O(1) short mutex sections, fixed max_readers and one
 * retired snapshot. Published payloads are immutable. Init/destroy require
 * external quiescence. No operation allocates after successful init. */
SALTS_CONCURRENCY_C_API int salts_rcu_init(salts_rcu *domain, void *initial,
                                         size_t max_readers);
/** Pins one snapshot in a zero-initialized, address-stable noncopyable guard.
 * ENOBUFS is reader exhaustion; ESHUTDOWN is closed admission. A guard may
 * survive suspension and exclusive cross-thread handoff if its storage and
 * domain survive. Cancellation must release it. No thread-local registration. */
SALTS_CONCURRENCY_C_API int salts_rcu_read_lock(salts_rcu *domain, salts_rcu_guard *guard);
SALTS_CONCURRENCY_C_API const void *salts_rcu_load(const salts_rcu_guard *guard);
/** Releases exactly one original guard. Duplicate/copy release returns EINVAL. */
SALTS_CONCURRENCY_C_API int salts_rcu_read_unlock(salts_rcu_guard *guard);
/** Transfers replacement only on success. EBUSY preserves ownership when the
 * previous retired pointer has not been reclaimed. Rejects pointer republication. */
SALTS_CONCURRENCY_C_API int salts_rcu_replace(salts_rcu *domain, void *replacement);
/** Transfers retired to the caller after its epoch readers exit; EBUSY while
 * pinned, ENOENT with no retired pointer. Clears out_retired on failure. */
SALTS_CONCURRENCY_C_API int salts_rcu_try_reclaim(salts_rcu *domain, void **out_retired);
/** Stops admission without waiting; active guards remain valid. */
SALTS_CONCURRENCY_C_API int salts_rcu_close(salts_rcu *domain);
/** Requires close, no API calls/readers and no retired pointer. Transfers
 * current to the caller; EBUSY leaves the domain intact. Idempotent at zero. */
SALTS_CONCURRENCY_C_API int salts_rcu_destroy(salts_rcu *domain, void **out_current);

#endif
