#include "stream_live.h"

#include <stdint.h>
#include <string.h>

static bool backpressure_policy_is_valid(stream_backpressure_policy_t policy)
{
    return policy >= STREAM_BP_REJECT_NEW && policy <= STREAM_BP_LATEST_ONLY;
}

stream_result_t stream_live_ring_init(
    stream_live_ring_t *ring,
    void *data_storage,
    uint64_t *timestamp_storage,
    uint64_t *sequence_storage,
    size_t capacity,
    size_t element_size,
    stream_backpressure_policy_t policy)
{
    if (!ring || !data_storage || !timestamp_storage || !sequence_storage ||
        capacity == 0 || element_size == 0 || element_size > STREAM_MAX_ITEM_SIZE ||
        capacity > SIZE_MAX / element_size ||
        capacity > SIZE_MAX / sizeof(*timestamp_storage) ||
        capacity > SIZE_MAX / sizeof(*sequence_storage) ||
        !backpressure_policy_is_valid(policy)) {
        return STREAM_ERROR;
    }

    memset(ring, 0, sizeof(*ring));
    ring->data = (unsigned char *)data_storage;
    ring->timestamps_ns = timestamp_storage;
    ring->sequences = sequence_storage;
    ring->capacity = capacity;
    ring->element_size = element_size;
    ring->policy = policy;
    return STREAM_OK;
}

static void write_slot(
    stream_live_ring_t *ring,
    size_t index,
    const void *value,
    uint64_t timestamp_ns)
{
    memcpy(ring->data + index * ring->element_size,
           value,
           ring->element_size);
    ring->timestamps_ns[index] = timestamp_ns;
    ring->sequences[index] = ring->next_sequence++;
}

stream_push_result_t stream_live_ring_push(
    stream_live_ring_t *ring,
    const void *value,
    uint64_t timestamp_ns)
{
    size_t index;
    bool dropped = false;

    if (!ring || !value || !ring->data || ring->capacity == 0 || ring->closed) {
        return STREAM_PUSH_ERROR;
    }

    if (ring->policy == STREAM_BP_LATEST_ONLY && ring->size != 0) {
        ring->dropped += ring->size;
        dropped = true;
        /* Keep one pending slot and overwrite it with the newest value. */
        ring->size = 1;
        write_slot(ring, ring->head, value, timestamp_ns);
        return STREAM_PUSH_DROPPED;
    }

    if (ring->size == ring->capacity) {
        switch (ring->policy) {
        case STREAM_BP_REJECT_NEW:
            return STREAM_PUSH_FULL;

        case STREAM_BP_DROP_NEWEST:
            ++ring->dropped;
            return STREAM_PUSH_DROPPED;

        case STREAM_BP_DROP_OLDEST:
            ring->head = (ring->head + 1) % ring->capacity;
            --ring->size;
            ++ring->dropped;
            dropped = true;
            break;

        case STREAM_BP_LATEST_ONLY:
            /* Handled above whenever size != 0. */
            break;

        default:
            return STREAM_PUSH_ERROR;
        }
    }

    index = (ring->head + ring->size) % ring->capacity;
    write_slot(ring, index, value, timestamp_ns);
    ++ring->size;

    return dropped ? STREAM_PUSH_DROPPED : STREAM_PUSH_OK;
}

void stream_live_ring_close_input(stream_live_ring_t *ring)
{
    if (ring) {
        ring->closed = true;
    }
}

size_t stream_live_ring_pending(const stream_live_ring_t *ring)
{
    return ring ? ring->size : 0;
}

uint64_t stream_live_ring_dropped(const stream_live_ring_t *ring)
{
    return ring ? ring->dropped : 0;
}

static stream_result_t live_ring_source_next(
    stream_source_t *source,
    stream_item_t *out)
{
    stream_live_ring_t *ring;
    size_t index;

    if (!source || !source->context || !out || !out->data) {
        return STREAM_ERROR;
    }

    ring = (stream_live_ring_t *)source->context;

    if (ring->size == 0) {
        return ring->closed ? STREAM_END : STREAM_AGAIN;
    }

    index = ring->head;
    memcpy(out->data,
           ring->data + index * ring->element_size,
           ring->element_size);
    out->size = ring->element_size;
    out->timestamp_ns = ring->timestamps_ns[index];
    out->sequence = ring->sequences[index];

    ring->head = (ring->head + 1) % ring->capacity;
    --ring->size;
    return STREAM_OK;
}

static void live_ring_source_close(stream_source_t *source)
{
    (void)source;
}

stream_result_t stream_from_live_ring(
    stream_t *stream,
    stream_live_ring_t *ring)
{
    stream_source_t source;

    if (!stream || !ring || !ring->data || ring->element_size == 0) {
        return STREAM_ERROR;
    }

    source.context = ring;
    source.element_size = ring->element_size;
    source.next = live_ring_source_next;
    source.reset = NULL;
    source.close = live_ring_source_close;
    return stream_init(stream, &source);
}
