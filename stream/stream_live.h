#ifndef C11_STREAM_LIVE_H
#define C11_STREAM_LIVE_H

#include "stream.h"
/* Stream SPSC bridge is now isolated in stream_spsc.h; include it explicitly
 * when you need push-based ring ingestion APIs.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stream_live_ring {
    unsigned char *data;
    uint64_t *timestamps_ns;
    uint64_t *sequences;

    size_t capacity;
    size_t element_size;
    size_t head;
    size_t size;

    uint64_t next_sequence;
    uint64_t dropped;
    bool closed;

    stream_backpressure_policy_t policy;
} stream_live_ring_t;

stream_result_t stream_live_ring_init(
    stream_live_ring_t *ring,
    void *data_storage,
    uint64_t *timestamp_storage,
    uint64_t *sequence_storage,
    size_t capacity,
    size_t element_size,
    stream_backpressure_policy_t policy);

stream_push_result_t stream_live_ring_push(
    stream_live_ring_t *ring,
    const void *value,
    uint64_t timestamp_ns);

void stream_live_ring_close_input(stream_live_ring_t *ring);

size_t stream_live_ring_pending(const stream_live_ring_t *ring);
uint64_t stream_live_ring_dropped(const stream_live_ring_t *ring);

/*
 * Live source semantics:
 *   empty + open   -> STREAM_AGAIN
 *   empty + closed -> STREAM_END
 * This source is intentionally not resettable.
 */
stream_result_t stream_from_live_ring(
    stream_t *stream,
    stream_live_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif
