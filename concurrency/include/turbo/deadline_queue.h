#ifndef TURBO_DEADLINE_QUEUE_H
#define TURBO_DEADLINE_QUEUE_H

#include <turbo/concurrency.h>
#include <turbo/error_codes.h>

#include <stddef.h>
#include <stdint.h>

typedef uint64_t turbo_deadline_id;

typedef struct turbo_deadline_event {
  turbo_deadline_id id;
  uint64_t deadline_ms;
  uint64_t token;
} turbo_deadline_event;

typedef struct turbo_deadline_queue {
  void *impl;
} turbo_deadline_queue;

/**
 * Initializes fixed-capacity single-owner storage for absolute millisecond
 * deadlines. The caller supplies one monotonic clock domain consistently.
 * This type provides ordering only: it starts no thread, reads no clock, and
 * invokes no callback. Init, schedule, cancel, take, and destroy must all run
 * on the same owner thread after external synchronization has quiesced users.
 */
TURBO_CONCURRENCY_C_API int turbo_deadline_queue_init(turbo_deadline_queue *queue, size_t capacity);

/**
 * Adds one absolute millisecond deadline. Equal deadlines preserve successful
 * schedule order. The returned id is unique only for the live queue instance.
 * Full storage returns TURBO_ENOBUFS. Failure clears out_id; token is copied
 * integer metadata and never transfers resource ownership.
 */
TURBO_CONCURRENCY_C_API int turbo_deadline_queue_schedule(turbo_deadline_queue *queue,
                                                          uint64_t deadline_ms, uint64_t token,
                                                          turbo_deadline_id *out_id);

/** Removes one live generation-checked id and returns its copied event. O(log n). */
TURBO_CONCURRENCY_C_API int turbo_deadline_queue_cancel(turbo_deadline_queue *queue,
                                                        turbo_deadline_id id,
                                                        turbo_deadline_event *out_event);

/** Copies the earliest event without removing it; empty returns TURBO_ETIMEDOUT. O(1). */
TURBO_CONCURRENCY_C_API int turbo_deadline_queue_peek(const turbo_deadline_queue *queue,
                                                      turbo_deadline_event *out_event);

/** Removes the earliest due event; no event due returns TURBO_ETIMEDOUT. O(log n). */
TURBO_CONCURRENCY_C_API int turbo_deadline_queue_take_ready(turbo_deadline_queue *queue,
                                                            uint64_t now_ms,
                                                            turbo_deadline_event *out_event);

TURBO_CONCURRENCY_C_API size_t turbo_deadline_queue_size(const turbo_deadline_queue *queue);

/** Destroys storage and invalidates every id; pending integer events are discarded. */
TURBO_CONCURRENCY_C_API int turbo_deadline_queue_destroy(turbo_deadline_queue *queue);

#endif /* TURBO_DEADLINE_QUEUE_H */
