#ifndef CNET_DISPATCHER_H
#define CNET_DISPATCHER_H

#include "cnet_callback.h"
#include "cnet_shards.h"

/**
 * @internal @incomplete
 * Client-owned bridge from shard event leases to callback lanes. The future
 * public client owns this primitive; it is not an independently exposed API.
 */
typedef struct cnet_dispatcher {
  void *impl;
} cnet_dispatcher;

typedef int (*cnet_dispatcher_keep_running_fn)(void *context);

int cnet_dispatcher_init(cnet_dispatcher *dispatcher, cnet_shards *shards,
                         cnet_callback_workers *callbacks);

/** Registers immutable callback ownership for one generation-checked connection. */
int cnet_dispatcher_register(cnet_dispatcher *dispatcher, cnet_shard_connection connection,
                             cnet_callback_fn observer, void *observer_context);

/**
 * Single-consumer, nonblocking drive for one shard. A full callback lane retains
 * exactly one event lease and returns TURBO_ENOBUFS without copying its payload.
 */
int cnet_dispatcher_drive(cnet_dispatcher *dispatcher, uint32_t shard);
/** Normal worker path: sleeps on the shard event ring instead of polling. */
int cnet_dispatcher_drive_wait(cnet_dispatcher *dispatcher, uint32_t shard,
                               cnet_dispatcher_keep_running_fn keep_running, void *context);
/** Wakes every blocked dispatcher worker to re-check its stop predicate. */
int cnet_dispatcher_wake(cnet_dispatcher *dispatcher);

/**
 * Drives events until every registered connection has completed and recycled.
 * This control-plane wait must not run from a callback worker.
 */
int cnet_dispatcher_wait_idle(cnet_dispatcher *dispatcher, uint32_t timeout_ms);

/**
 * Closes admission and live connections, then drives all terminal callbacks.
 * Retry after TURBO_ETIMEDOUT; do not call from a callback worker.
 */
int cnet_dispatcher_drain(cnet_dispatcher *dispatcher, uint32_t timeout_ms);

/** Requires a completed drain and no retained event lease. */
int cnet_dispatcher_destroy(cnet_dispatcher *dispatcher);

#endif /* CNET_DISPATCHER_H */
