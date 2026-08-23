#ifndef CFLOW_EVENT_H
#define CFLOW_EVENT_H

#include <cmeta/cmeta.h>

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
