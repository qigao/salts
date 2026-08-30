#ifndef CFLOW_TEMPORAL_H
#define CFLOW_TEMPORAL_H

#include <cflow/reactive.h>
#include <cflow/time.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Move `inner` into a bounded monotonic delay Publisher. Success clears `inner`;
 * failure preserves caller ownership. One value is retained until its deadline.
 * Durations round up to scheduler milliseconds and never fire early.
 *
 * @param out Empty Publisher destination.
 * @param inner Valid Publisher moved only on success.
 * @param delay Nonnegative monotonic duration; zero schedules at current time.
 * @return true on success; false for invalid handles/types or allocation
 * failure. Managed input requires CONSTRUCTS_VALUES and COPY/MOVE/DESTROY.
 */
bool cflow_publisher_delay(cflow_publisher *out,
                        cflow_publisher *inner,
                        cflow_duration delay);

/**
 * Move `inner` into a latest-value Publisher with a monotonic quiet period.
 * The adapter owns one retained slot and one scratch slot, resets the deadline
 * after each replacement, and emits a retained final value immediately when
 * upstream completes. Failure preserves `inner`.
 */
bool cflow_publisher_debounce(cflow_publisher *out,
                           cflow_publisher *inner,
                           cflow_duration quiet_period);

/**
 * Move `inner` into a Publisher that errors when an upstream WAIT times out.
 * The timer is armed only for WAIT. On resume after both sides became ready,
 * upstream is observed first; timeout is reported only if upstream still
 * returns WAIT. Failure preserves `inner`.
 */
bool cflow_publisher_timeout(cflow_publisher *out,
                          cflow_publisher *inner,
                          cflow_duration timeout);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_TEMPORAL_H */
