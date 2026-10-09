#ifndef CNET_SG_HOST_H
#define CNET_SG_HOST_H

#include <cnet/cnet.h>
#include <salts/native_io_sharded.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CNET_SG_HOST_ROUTING_VERSION 1u

/* Owner-only dispatch for a SINGLE already-observed SG completion batch.
 * Never invokes observe, blocks, queues, creates threads or copies payload.
 * The embedding host owns one native_io_sharded_host_lease and drives short
 * progress tasks on the same shard. All events in a batch are examined, even
 * if an earlier route fails. SG-owned events were settled by SG itself and
 * MUST NOT be submitted again to CNet clients/listeners. */
typedef struct cnet_sg_host_routes {
  size_t size;
  uint32_t version;
  cnet_listener *listener; /* Optional one inbound listener on this Owner. */
  cnet_client *const *clients; /* Borrowed array, independent external clients. */
  size_t client_count;
} cnet_sg_host_routes;

/* Returns the first error after routing all *remaining* events. Unclaimed
 * non-SG completions are EPROTO, never silently discarded. A failing router
 * is not probed against other clients because it may have partially consumed
 * its own completion; subsequent events in the batch still progress.
 * out_accepts counts completed detached accepts; caller must accept/adopt.
 * out_sharded counts SG-owned completions already finally settled. */
int cnet_sg_host_route_batch(const native_io_sharded_completion *events,
                             size_t count, const cnet_sg_host_routes *routes,
                             size_t *out_accepts, size_t *out_sharded);

#ifdef __cplusplus
}
#endif
#endif
