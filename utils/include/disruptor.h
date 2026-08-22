#ifndef DISRUPTOR_H
#define DISRUPTOR_H

#include "platform.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct disruptor_s disruptor_t;
typedef struct disruptor_topology_s disruptor_topology_t;

/**
 * @file disruptor.h
 * @brief Fixed-entry MPMC ring with broadcast and worker-pool consumption modes.
 *
 * Producer claim/publish operations are thread-safe. Broadcast consumer handles
 * are single-owner: register, wait, release, and unregister a handle from one
 * consumer thread. Worker-pool claims may be made by multiple worker threads.
 * Configuration, dependency, reset, and destroy operations require a quiescent
 * ring with no concurrent producer or consumer calls.
 *
 * Every successful claim must be published exactly once. Entry pointers remain
 * valid only until the corresponding consumer release permits that slot to be
 * reused. Unless documented otherwise, int functions return 1 on success and 0
 * for invalid arguments, mode mismatch, or an unavailable operation.
 */

#define DISRUPTOR_STAGE_INVALID UINT32_MAX
#define DISRUPTOR_GROUP_INVALID UINT32_MAX

typedef uint32_t disruptor_stage_t;
typedef uint32_t disruptor_group_t;

typedef struct {
  uint64_t sequence;
} disruptor_cursor_t;

typedef struct {
  uint64_t first_sequence;
  uint64_t last_sequence;
} disruptor_sequence_range_t;

typedef struct {
  uint32_t slot;
} disruptor_consumer_t;

typedef enum {
  DISRUPTOR_MODE_BROADCAST = 0,  /**< Every registered consumer sees every entry. */
  DISRUPTOR_MODE_WORKER_POOL = 1 /**< Each entry is claimed by one worker. */
} disruptor_mode_t;

/** Predicate: return non-zero to continue blocking operations. */
typedef int (*disruptor_keep_running_fn)(void *ctx);

/**
 * @deprecated Use disruptor_keep_running_fn for clearer intent.
 */
typedef disruptor_keep_running_fn disruptor_should_run_fn;

typedef struct {
  size_t entry_size;            /**< Fixed entry size in bytes; must be non-zero. */
  uint64_t capacity;            /**< Usable entry count; must be a power of two. */
  uint32_t consumer_capacity;   /**< Maximum broadcast consumers; must be non-zero. */
  disruptor_mode_t mode;        /**< Zero-initialization selects broadcast mode. */
} disruptor_config_t;

TURBO_C_API disruptor_t *disruptor_create(const disruptor_config_t *config);
TURBO_C_API void disruptor_destroy(disruptor_t *disruptor);
TURBO_C_API int disruptor_reset(disruptor_t *disruptor);

TURBO_C_API uint64_t disruptor_capacity(const disruptor_t *disruptor);
TURBO_C_API size_t disruptor_entry_size(const disruptor_t *disruptor);

TURBO_C_API void *disruptor_acquire_entry(disruptor_t *disruptor, const disruptor_cursor_t *cursor);
TURBO_C_API const void *disruptor_show_entry(const disruptor_t *disruptor,
                                           const disruptor_cursor_t *cursor);

TURBO_C_API int disruptor_publisher_try_claim(disruptor_t *disruptor, disruptor_cursor_t *cursor);
TURBO_C_API int disruptor_publisher_try_claim_n(disruptor_t *disruptor, uint32_t count,
                                              disruptor_sequence_range_t *range);
TURBO_C_API void disruptor_publisher_next_entry_blocking(disruptor_t *disruptor,
                                                       disruptor_cursor_t *cursor);
TURBO_C_API void *disruptor_publisher_next_entry_and_acquire_blocking(disruptor_t *disruptor,
                                                                    disruptor_cursor_t *cursor);
TURBO_C_API int disruptor_publisher_claim_n_blocking(disruptor_t *disruptor, uint32_t count,
                                                   disruptor_sequence_range_t *range);
/**
 * Publish a claimed entry without waiting for earlier producers.
 *
 * A zero return can mean that a valid claimed entry was committed out of order
 * but is not visible yet. The caller must not modify or republish such an entry.
 * Use disruptor_publisher_publish() when only acceptance, not immediate
 * visibility, matters.
 */
TURBO_C_API int disruptor_publisher_try_commit(disruptor_t *disruptor,
                                             const disruptor_cursor_t *cursor);
TURBO_C_API void disruptor_publisher_commit_entry_blocking(disruptor_t *disruptor,
                                                         const disruptor_cursor_t *cursor);
TURBO_C_API int disruptor_publisher_publish_range(disruptor_t *disruptor,
                                                const disruptor_sequence_range_t *range);
TURBO_C_API void disruptor_publisher_commit_range_blocking(disruptor_t *disruptor,
                                                         const disruptor_sequence_range_t *range);
TURBO_C_API int disruptor_publisher_publish(disruptor_t *disruptor, const disruptor_cursor_t *cursor);

/** Broadcast-only consumer APIs. Wrong-mode calls fail or return without effect. */
TURBO_C_API int disruptor_consumer_try_register(disruptor_t *disruptor,
                                              disruptor_consumer_t *consumer,
                                              uint64_t *next_sequence);
/** Wait until a broadcast slot is available; use try_register when cancellation is required. */
TURBO_C_API uint64_t disruptor_consumer_register(disruptor_t *disruptor,
                                               disruptor_consumer_t *consumer);
TURBO_C_API void disruptor_consumer_unregister(disruptor_t *disruptor,
                                             const disruptor_consumer_t *consumer);

TURBO_C_API int disruptor_consumer_wait_for_nonblocking(const disruptor_t *disruptor,
                                                      disruptor_cursor_t *cursor);
TURBO_C_API void disruptor_consumer_wait_for_blocking(const disruptor_t *disruptor,
                                                    disruptor_cursor_t *cursor);
TURBO_C_API int disruptor_consumer_wait_for_nonblocking_for(
    const disruptor_t *disruptor,
    const disruptor_consumer_t *consumer,
    disruptor_cursor_t *cursor);
TURBO_C_API void disruptor_consumer_wait_for_blocking_for(
    const disruptor_t *disruptor,
    const disruptor_consumer_t *consumer,
    disruptor_cursor_t *cursor);
TURBO_C_API void disruptor_consumer_release_entry(disruptor_t *disruptor,
                                                const disruptor_consumer_t *consumer,
                                                const disruptor_cursor_t *cursor);
/**
 * Replace one broadcast consumer's dependency set.
 *
 * Dependencies must be distinct registered consumers from the same ring.
 * Cycles are rejected, and failure leaves the previous dependency set intact.
 */
TURBO_C_API int disruptor_consumer_set_dependencies(
    disruptor_t *disruptor,
    const disruptor_consumer_t *consumer,
    const disruptor_consumer_t *dependencies,
    uint32_t dependency_count);

/** Broadcast-only topology builder. Commit rejects cycles and invalid consumer handles. */
TURBO_C_API disruptor_topology_t *disruptor_topology_create(disruptor_t *disruptor);
TURBO_C_API void disruptor_topology_destroy(disruptor_topology_t *topology);
TURBO_C_API disruptor_stage_t disruptor_topology_stage(disruptor_topology_t *topology,
                                                     const char *name,
                                                     const disruptor_consumer_t *consumer);
TURBO_C_API disruptor_group_t disruptor_topology_group(disruptor_topology_t *topology,
                                                     const char *name,
                                                     const disruptor_stage_t *stages,
                                                     uint32_t stage_count);
TURBO_C_API int disruptor_topology_after(disruptor_topology_t *topology,
                                       disruptor_stage_t stage,
                                       disruptor_stage_t dependency);
TURBO_C_API int disruptor_topology_after_all(disruptor_topology_t *topology,
                                           disruptor_stage_t stage,
                                           const disruptor_stage_t *dependencies,
                                           uint32_t dependency_count);
TURBO_C_API int disruptor_topology_stage_after_group(disruptor_topology_t *topology,
                                                   disruptor_stage_t stage,
                                                   disruptor_group_t dependency_group);
TURBO_C_API int disruptor_topology_group_after(disruptor_topology_t *topology,
                                             disruptor_group_t group,
                                             disruptor_stage_t dependency);
TURBO_C_API int disruptor_topology_group_after_group(disruptor_topology_t *topology,
                                                   disruptor_group_t group,
                                                   disruptor_group_t dependency_group);
TURBO_C_API int disruptor_topology_chain(disruptor_topology_t *topology,
                                       const disruptor_stage_t *stages,
                                       uint32_t stage_count);
TURBO_C_API int disruptor_topology_commit(disruptor_topology_t *topology);

/** Worker-pool-only claim and release APIs. Wrong-mode calls fail or return without effect. */
TURBO_C_API int disruptor_worker_try_claim(disruptor_t *disruptor, disruptor_cursor_t *cursor);
TURBO_C_API void disruptor_worker_claim_blocking(disruptor_t *disruptor,
                                               disruptor_cursor_t *cursor);
/**
 * Claim one published worker-pool entry, parking the thread while the ring is empty.
 * Returns 1 after claiming an entry, or 0 when keep_running returns zero or arguments are invalid.
 * Call disruptor_worker_wake_all() after changing the predicate state.
 */
TURBO_C_API int disruptor_worker_claim_wait(disruptor_t *disruptor,
                                          disruptor_cursor_t *cursor,
                                          disruptor_keep_running_fn keep_running,
                                          void *ctx);
/** Wake parked worker claim calls so they can re-evaluate their stop predicate. */
TURBO_C_API void disruptor_worker_wake_all(disruptor_t *disruptor);
TURBO_C_API void disruptor_worker_release_entry(disruptor_t *disruptor,
                                              const disruptor_cursor_t *cursor);

/**
 * @brief Callback: process entries in range [first_seq, last_seq].
 */
typedef void (*disruptor_batch_fn)(void *ctx, uint64_t first_seq, uint64_t last_seq);

/**
 * @brief Generic consumer loop.
 *
 * Handles register, poll-wait-process-release loop, shutdown drain, and
 * unregister.  Callers only supply two callbacks: whether to keep running,
 * and how to process a batch of entries.
 */
TURBO_C_API void disruptor_consumer_run(disruptor_t *disruptor,
                                      disruptor_consumer_t *consumer,
                                      disruptor_keep_running_fn keep_running,
                                      disruptor_batch_fn process_batch,
                                      void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DISRUPTOR_H */
