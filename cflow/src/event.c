#include <cflow/event.h>

#include <turbo/thread.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_event_slot {
    size_t type_index;
} cflow_event_slot;

typedef struct cflow_mailbox_impl {
    cflow_event_type *schema;
    cflow_event_slot *slots;
    unsigned char *payloads;
    size_t schema_count;
    size_t capacity;
    size_t payload_stride;
    size_t max_payload_size;
    turbo_mutex_t lock;
} cflow_mailbox_impl;

static bool cflow_size_multiply(size_t left, size_t right, size_t *result) {
    if (result == NULL || left == 0u || right == 0u || left > SIZE_MAX / right)
        return false;
    *result = left * right;
    return true;
}

static bool cflow_alignment_valid(size_t alignment) {
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u &&
           alignment <= _Alignof(cmeta_capture_storage);
}

static bool cflow_align_size(size_t size, size_t alignment, size_t *result) {
    size_t padding;

    if (result == NULL || size == 0u || !cflow_alignment_valid(alignment))
        return false;
    padding = alignment - 1u;
    if (size > SIZE_MAX - padding) return false;
    *result = (size + padding) & ~padding;
    return true;
}

static bool cflow_schema_measure(const cflow_event_type *schema,
                                 size_t schema_count,
                                 size_t *max_payload_size,
                                 size_t *max_payload_alignment) {
    const cmeta_trait_flags required =
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
    size_t largest_size = 0u;
    size_t largest_alignment = 0u;
    size_t index;

    if (schema == NULL || schema_count == 0u || max_payload_size == NULL ||
        max_payload_alignment == NULL)
        return false;

    for (index = 0u; index < schema_count; ++index) {
        const cmeta_type_desc *type = schema[index].payload_type;
        size_t previous;

        if (schema[index].id == 0u || !cmeta_type_desc_valid(type) ||
            type->size == 0u || !cflow_alignment_valid(type->align) ||
            cmeta_type_require_traits(type, required) != CMETA_OK)
            return false;
        for (previous = 0u; previous < index; ++previous) {
            if (schema[previous].id == schema[index].id) return false;
        }
        if (type->size > largest_size) largest_size = type->size;
        if (type->align > largest_alignment) largest_alignment = type->align;
    }

    *max_payload_size = largest_size;
    *max_payload_alignment = largest_alignment;
    return true;
}

static void cflow_mailbox_impl_free(cflow_mailbox_impl *impl) {
    if (impl == NULL) return;
    if (impl->lock != NULL) turbo_mutex_destroy(&impl->lock);
    free(impl->payloads);
    free(impl->slots);
    free(impl->schema);
    free(impl);
}

cflow_mailbox_status cflow_mailbox_init(cflow_mailbox *mailbox,
                                        const cflow_event_type *schema,
                                        size_t schema_count,
                                        size_t capacity) {
    cflow_mailbox_impl *impl;
    size_t max_payload_size;
    size_t max_payload_alignment;
    size_t payload_stride;
    size_t schema_bytes;
    size_t slot_bytes;
    size_t payload_bytes;

    if (mailbox == NULL || mailbox->impl != NULL || capacity == 0u ||
        !cflow_schema_measure(schema, schema_count, &max_payload_size,
                              &max_payload_alignment) ||
        !cflow_align_size(max_payload_size, max_payload_alignment,
                          &payload_stride) ||
        !cflow_size_multiply(schema_count, sizeof(*schema), &schema_bytes) ||
        !cflow_size_multiply(capacity, sizeof(cflow_event_slot), &slot_bytes) ||
        !cflow_size_multiply(capacity, payload_stride, &payload_bytes))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;

    impl = (cflow_mailbox_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_MAILBOX_ALLOCATION_FAILED;
    impl->schema = (cflow_event_type *)malloc(schema_bytes);
    impl->slots = (cflow_event_slot *)calloc(1u, slot_bytes);
    impl->payloads = (unsigned char *)malloc(payload_bytes);
    turbo_mutex_init(&impl->lock);
    if (impl->schema == NULL || impl->slots == NULL || impl->payloads == NULL ||
        impl->lock == NULL) {
        cflow_mailbox_impl_free(impl);
        return CFLOW_MAILBOX_ALLOCATION_FAILED;
    }

    memcpy(impl->schema, schema, schema_bytes);
    impl->schema_count = schema_count;
    impl->capacity = capacity;
    impl->payload_stride = payload_stride;
    impl->max_payload_size = max_payload_size;
    mailbox->impl = impl;
    return CFLOW_MAILBOX_OK;
}

size_t cflow_mailbox_payload_capacity(const cflow_mailbox *mailbox) {
    const cflow_mailbox_impl *impl =
        mailbox != NULL ? (const cflow_mailbox_impl *)mailbox->impl : NULL;
    return impl != NULL ? impl->max_payload_size : 0u;
}

void cflow_mailbox_destroy(cflow_mailbox *mailbox) {
    cflow_mailbox_impl *impl;

    if (mailbox == NULL) return;
    impl = (cflow_mailbox_impl *)mailbox->impl;
    mailbox->impl = NULL;
    cflow_mailbox_impl_free(impl);
}
