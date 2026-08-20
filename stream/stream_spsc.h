#ifndef C11_STREAM_SPSC_H
#define C11_STREAM_SPSC_H

#include "stream.h"
#include "ring_buffer_spsc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stream_spsc_ring {
    ring_spsc_t ring;
    size_t element_size;
    uint64_t next_sequence;
    uint64_t dropped;
    bool closed;

    stream_backpressure_policy_t policy;
} stream_spsc_ring_t;

stream_result_t stream_spsc_ring_init(
    stream_spsc_ring_t *ring,
    void *data_storage,
    size_t storage_size,
    size_t element_size,
    stream_backpressure_policy_t policy);

stream_push_result_t stream_spsc_ring_push(
    stream_spsc_ring_t *ring,
    const void *value,
    uint64_t timestamp_ns);

void stream_spsc_ring_close_input(stream_spsc_ring_t *ring);

size_t stream_spsc_ring_pending(const stream_spsc_ring_t *ring);
uint64_t stream_spsc_ring_dropped(const stream_spsc_ring_t *ring);

/*
 * SPSC source semantics:
 *   empty + open   -> STREAM_AGAIN
 *   empty + closed -> STREAM_END
 */
stream_result_t stream_from_spsc_ring(
    stream_t *stream,
    stream_spsc_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif
