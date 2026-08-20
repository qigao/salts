#include "stream_spsc.h"

#include <stdint.h>
#include <string.h>

static bool backpressure_policy_is_valid(stream_backpressure_policy_t policy)
{
    return policy >= STREAM_BP_REJECT_NEW && policy <= STREAM_BP_LATEST_ONLY;
}

typedef struct {
    uint64_t timestamp_ns;
    uint64_t sequence;
} stream_spsc_ring_record_t;

static size_t spsc_record_size(const stream_spsc_ring_t *ring)
{
    return ring ? (ring->element_size + sizeof(stream_spsc_ring_record_t)) : 0;
}

static bool spsc_drop_one(stream_spsc_ring_t *ring)
{
    uint8_t *slot;
    size_t available = 0;
    size_t record_size;

    if (!ring || !ring->element_size) {
        return false;
    }

    record_size = spsc_record_size(ring);
    slot = ring_spsc_read_acquire(&ring->ring, &available);
    if (!slot) {
        return false;
    }
    if (available < record_size) {
        ring_spsc_read_release(&ring->ring, available);
        return false;
    }

    (void)slot;
    ring_spsc_read_release(&ring->ring, record_size);
    return true;
}

static bool spsc_discard_all(stream_spsc_ring_t *ring)
{
    bool progressed = false;

    while (stream_spsc_ring_pending(ring) > 0) {
        if (!spsc_drop_one(ring)) {
            return false;
        }
        progressed = true;
    }

    return progressed;
}

stream_result_t stream_spsc_ring_init(
    stream_spsc_ring_t *ring,
    void *data_storage,
    size_t storage_size,
    size_t element_size,
    stream_backpressure_policy_t policy)
{
    if (!ring || !data_storage || storage_size == 0 || element_size == 0 ||
        element_size > STREAM_MAX_ITEM_SIZE ||
        !backpressure_policy_is_valid(policy)) {
        return STREAM_ERROR;
    }

    memset(ring, 0, sizeof(*ring));
    if (element_size + sizeof(stream_spsc_ring_record_t) >= storage_size) {
        return STREAM_ERROR;
    }
    if (!ring_spsc_init(&ring->ring, (uint8_t *)data_storage, storage_size)) {
        return STREAM_ERROR;
    }

    ring->element_size = element_size;
    ring->policy = policy;
    return STREAM_OK;
}

stream_push_result_t stream_spsc_ring_push(
    stream_spsc_ring_t *ring,
    const void *value,
    uint64_t timestamp_ns)
{
    uint8_t *slot;
    bool dropped = false;
    size_t record_size;
    size_t pending;
    stream_spsc_ring_record_t header;

    if (!ring || !value || !ring->element_size || ring->closed) {
        return STREAM_PUSH_ERROR;
    }

    if (!backpressure_policy_is_valid(ring->policy)) {
        return STREAM_PUSH_ERROR;
    }

    record_size = spsc_record_size(ring);
    if (ring->policy == STREAM_BP_LATEST_ONLY) {
        pending = stream_spsc_ring_pending(ring);
        if (pending > 0) {
            dropped = true;
            ring->dropped += (uint64_t)pending;
            if (!spsc_discard_all(ring)) {
                ring->dropped -= (uint64_t)pending;
                return STREAM_PUSH_ERROR;
            }
        }
    }

    slot = ring_spsc_write_acquire(&ring->ring, record_size);
    while (!slot) {
        if (ring->policy == STREAM_BP_REJECT_NEW) {
            return STREAM_PUSH_FULL;
        }
        if (ring->policy == STREAM_BP_DROP_NEWEST) {
            ++ring->dropped;
            return STREAM_PUSH_DROPPED;
        }

        if (!spsc_drop_one(ring)) {
            return STREAM_PUSH_ERROR;
        }

        ++ring->dropped;
        dropped = true;
        slot = ring_spsc_write_acquire(&ring->ring, record_size);
    }

    header.timestamp_ns = timestamp_ns;
    header.sequence = ring->next_sequence++;
    memcpy(slot, &header, sizeof(header));
    memcpy(slot + sizeof(header), value, ring->element_size);
    ring_spsc_write_release(&ring->ring, record_size);

    return dropped ? STREAM_PUSH_DROPPED : STREAM_PUSH_OK;
}

void stream_spsc_ring_close_input(stream_spsc_ring_t *ring)
{
    if (ring) {
        ring->closed = true;
    }
}

size_t stream_spsc_ring_pending(const stream_spsc_ring_t *ring)
{
    size_t record_size;

    if (!ring || ring->element_size == 0) {
        return 0;
    }

    record_size = spsc_record_size(ring);
    if (record_size == 0) {
        return 0;
    }

    return ring_spsc_read_available(&ring->ring) / record_size;
}

uint64_t stream_spsc_ring_dropped(const stream_spsc_ring_t *ring)
{
    return ring ? ring->dropped : 0;
}

static stream_result_t spsc_ring_source_next(
    stream_source_t *source,
    stream_item_t *out)
{
    stream_spsc_ring_t *ring;
    uint8_t *slot;
    size_t available = 0;
    size_t record_size;
    stream_spsc_ring_record_t header;

    if (!source || !source->context || !out || !out->data) {
        return STREAM_ERROR;
    }

    ring = (stream_spsc_ring_t *)source->context;
    if (!ring || ring->element_size == 0) {
        return STREAM_ERROR;
    }

    record_size = spsc_record_size(ring);
    slot = ring_spsc_read_acquire(&ring->ring, &available);
    if (!slot) {
        return ring->closed ? STREAM_END : STREAM_AGAIN;
    }
    if (available < record_size) {
        ring_spsc_read_release(&ring->ring, available);
        return STREAM_ERROR;
    }

    memcpy(&header, slot, sizeof(header));
    memcpy(out->data, slot + sizeof(header), ring->element_size);
    out->size = ring->element_size;
    out->timestamp_ns = header.timestamp_ns;
    out->sequence = header.sequence;
    ring_spsc_read_release(&ring->ring, record_size);

    return STREAM_OK;
}

static void spsc_ring_source_close(stream_source_t *source)
{
    (void)source;
}

stream_result_t stream_from_spsc_ring(
    stream_t *stream,
    stream_spsc_ring_t *ring)
{
    stream_source_t source;

    if (!stream || !ring || ring->element_size == 0) {
        return STREAM_ERROR;
    }

    source.context = ring;
    source.element_size = ring->element_size;
    source.next = spsc_ring_source_next;
    source.reset = NULL;
    source.close = spsc_ring_source_close;
    return stream_init(stream, &source);
}
