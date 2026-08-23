#ifndef CFLOW_EVENT_H
#define CFLOW_EVENT_H

#include <cmeta/cmeta.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cflow_event_id;

/** One row in a finite mailbox schema. Both fields are immutable after init. */
typedef struct cflow_event_type {
    /** Stable, non-zero identifier unique within one mailbox schema. */
    cflow_event_id id;
    /** Borrowed CMeta descriptor, valid until mailbox destruction. */
    const cmeta_type_desc *payload_type;
} cflow_event_type;

/** Borrowed event input valid only for one `try_send` call. */
typedef struct cflow_event_view {
    cflow_event_id id;
    const cmeta_type_desc *payload_type;
    const void *payload;
} cflow_event_view;

/** Exact result of a mailbox control-plane or data-path operation. */
typedef enum cflow_mailbox_status {
    CFLOW_MAILBOX_OK = 0,
    CFLOW_MAILBOX_INVALID_ARGUMENT,
    CFLOW_MAILBOX_TYPE_MISMATCH,
    CFLOW_MAILBOX_FULL,
    CFLOW_MAILBOX_EMPTY,
    CFLOW_MAILBOX_CLOSED,
    CFLOW_MAILBOX_CANCELLED,
    CFLOW_MAILBOX_ALLOCATION_FAILED,
    CFLOW_MAILBOX_BUFFER_TOO_SMALL
} cflow_mailbox_status;

/** Opaque bounded mailbox handle. Zero initialization is required. */
typedef struct cflow_mailbox {
    void *impl;
} cflow_mailbox;

/** Snapshot of bounded resources and committed data-path outcomes. */
typedef struct cflow_mailbox_stats {
    size_t schema_count;
    size_t capacity;
    size_t pending;
    size_t peak_pending;
    size_t payload_stride;
    size_t reserved_payload_bytes;
    uint64_t accepted;
    uint64_t received;
    uint64_t rejected_full;
    uint64_t rejected_closed;
    uint64_t rejected_cancelled;
    uint64_t cancelled;
} cflow_mailbox_stats;

/**
 * Initialize a finite-schema, fixed-capacity mailbox.
 *
 * Schema rows are copied. Payload descriptors remain borrowed and must outlive
 * the mailbox. Each descriptor must be valid, non-empty, no more aligned than
 * CMeta's ABI-safe capture storage, trivially copyable, and trivially
 * destructible. No allocation occurs on the data path after this call succeeds.
 *
 * @param mailbox Zero-initialized destination handle.
 * @param schema Borrowed array containing `schema_count` rows.
 * @param schema_count Number of rows; must be greater than zero.
 * @param capacity Maximum pending event count; must be greater than zero.
 * @return `CFLOW_MAILBOX_OK`, `CFLOW_MAILBOX_INVALID_ARGUMENT`, or
 *         `CFLOW_MAILBOX_ALLOCATION_FAILED`.
 */
cflow_mailbox_status cflow_mailbox_init(cflow_mailbox *mailbox,
                                        const cflow_event_type *schema,
                                        size_t schema_count,
                                        size_t capacity);

/**
 * Try to copy one typed event into the bounded mailbox.
 *
 * The payload is borrowed for the call and remains owned by the caller. On
 * success, the mailbox owns an independent trivial byte copy. Successful
 * commits are globally FIFO in mutex acquisition order.
 *
 * @param mailbox Borrowed initialized mailbox.
 * @param event Borrowed identifier, descriptor, and non-NULL payload view.
 * @return Exact admission result. Unknown identifiers are invalid arguments;
 *         a known identifier with a different descriptor is a type mismatch.
 */
cflow_mailbox_status cflow_mailbox_try_send(cflow_mailbox *mailbox,
                                            const cflow_event_view *event);

/**
 * Try to copy and remove the oldest committed event.
 *
 * `out_payload` must be aligned for the observed CMeta type. If its capacity is
 * too small, the event remains queued and caller payload bytes remain unchanged.
 * `out_id` and `out_type` are cleared on every failure and published only after
 * a successful dequeue.
 *
 * @param mailbox Borrowed initialized mailbox; one consumer is supported.
 * @param out_id Required event identifier output.
 * @param out_type Required borrowed canonical payload descriptor output.
 * @param out_payload Required caller-owned destination storage.
 * @param out_payload_capacity Available destination bytes.
 * @return `OK`, `EMPTY`, `BUFFER_TOO_SMALL`, a terminal status, or
 *         `INVALID_ARGUMENT`.
 */
cflow_mailbox_status cflow_mailbox_try_receive(
    cflow_mailbox *mailbox,
    cflow_event_id *out_id,
    const cmeta_type_desc **out_type,
    void *out_payload,
    size_t out_payload_capacity);

/**
 * Read one mutex-consistent statistics snapshot.
 *
 * @param mailbox Borrowed initialized mailbox.
 * @param out Required destination overwritten only on success.
 * @return true on success; false for invalid arguments.
 */
bool cflow_mailbox_get_stats(const cflow_mailbox *mailbox,
                             cflow_mailbox_stats *out);

/**
 * Return the largest payload size accepted by this mailbox.
 *
 * @param mailbox Borrowed initialized mailbox; destruction must not race.
 * @return Maximum payload size, or zero for an invalid mailbox.
 */
size_t cflow_mailbox_payload_capacity(const cflow_mailbox *mailbox);

/**
 * Destroy a quiescent mailbox and clear the handle.
 *
 * Producers, the consumer, and any waitable user must have stopped before this
 * control-plane operation. Pending trivial payload copies are discarded.
 *
 * @param mailbox Mailbox handle; NULL and zero handles are accepted.
 */
void cflow_mailbox_destroy(cflow_mailbox *mailbox);

#ifdef __cplusplus
}
#endif

#endif
