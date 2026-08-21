#ifndef CMETA_RANGE_H
#define CMETA_RANGE_H

#include <cmeta/cmeta.h>
#include <cmeta/collector.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t cmeta_range_flags;
enum {
    CMETA_RANGE_NONE          = 0u,
    CMETA_RANGE_SIZED         = 1u << 0,
    CMETA_RANGE_ORDERED       = 1u << 1,
    CMETA_RANGE_SORTED        = 1u << 2,
    CMETA_RANGE_UNIQUE        = 1u << 3,
    CMETA_RANGE_CONTIGUOUS    = 1u << 4,
    CMETA_RANGE_RANDOM_ACCESS = 1u << 5,
    CMETA_RANGE_REUSABLE      = 1u << 6
};

typedef size_t (*cmeta_range_size_fn)(const void *object);
/* A cursor is caller-owned, zero-initialized opaque traversal state. Array and
 * sparse ranges use index; linked and tree ranges use state. It may only be
 * passed back to the Range that initialized it and never owns its pointers. */
typedef struct cmeta_range_cursor {
    size_t index;
    void *state[2];
} cmeta_range_cursor;
typedef cmeta_gen_status (*cmeta_range_next_fn)(const void *object,
                                                 cmeta_range_cursor *cursor,
                                                 void *out_value);
typedef uint64_t (*cmeta_range_version_fn)(const void *object);

typedef struct cmeta_range {
    const void *object;
    const cmeta_type_desc *element_type;
    cmeta_range_flags flags;
    cmeta_range_size_fn size;
    cmeta_range_next_fn next;
    uint64_t version;
    cmeta_range_version_fn current_version;
} cmeta_range;

/* A Range borrows its source object and element descriptor. The source handle
 * storage must outlive the Range. Mutation or destroy is reported as
 * CMETA_GEN_MUTATED while that handle storage remains alive; next() performs
 * this version check before changing either cursor or output. */

typedef cmeta_range (*cmeta_range_factory_fn)(const void *object);

typedef enum cmeta_container_view {
    CMETA_CONTAINER_VIEW_DEFAULT = 0,
    CMETA_CONTAINER_VIEW_KEYS,
    CMETA_CONTAINER_VIEW_VALUES,
    CMETA_CONTAINER_VIEW_ENTRIES
} cmeta_container_view;

typedef struct cmeta_container_desc {
    const char *name;
    const cmeta_type_desc *container_type;
    const cmeta_type_desc *element_type;
    const cmeta_type_desc *key_type;
    const cmeta_type_desc *value_type;
    cmeta_range_factory_fn range;
    cmeta_range_factory_fn keys_range;
    cmeta_range_factory_fn values_range;
    cmeta_range_factory_fn entries_range;
    cmeta_collector_factory_fn collector;
} cmeta_container_desc;

typedef struct cmeta_container_header {
    const cmeta_container_desc *descriptor;
} cmeta_container_header;

static inline const cmeta_container_desc *cmeta_container_descriptor(const void *object) {
    const cmeta_container_header *header = (const cmeta_container_header *)object;
    return header != NULL ? header->descriptor : NULL;
}

static inline bool cmeta_container_range_view(const void *object,
                                              cmeta_container_view view,
                                              cmeta_range *out_range) {
    const cmeta_container_desc *desc;
    cmeta_range_factory_fn factory = NULL;
    if (object == NULL || out_range == NULL) return false;
    desc = cmeta_container_descriptor(object);
    if (desc == NULL) return false;
    switch (view) {
        case CMETA_CONTAINER_VIEW_DEFAULT: factory = desc->range; break;
        case CMETA_CONTAINER_VIEW_KEYS: factory = desc->keys_range; break;
        case CMETA_CONTAINER_VIEW_VALUES: factory = desc->values_range; break;
        case CMETA_CONTAINER_VIEW_ENTRIES: factory = desc->entries_range; break;
        default: return false;
    }
    if (factory == NULL) return false;
    *out_range = factory(object);
    return out_range->object != NULL && out_range->element_type != NULL && out_range->next != NULL;
}

static inline size_t cmeta_range_size(const cmeta_range *range) {
    return (range != NULL && range->size != NULL) ? range->size(range->object) : 0U;
}

static inline uint64_t cmeta_range_capture_version(
    cmeta_range_version_fn current_version, const void *object) {
    return current_version != NULL && object != NULL ? current_version(object) :
                                                      UINT64_C(0);
}

static inline cmeta_gen_status cmeta_range_next(const cmeta_range *range,
                                                 cmeta_range_cursor *cursor,
                                                 void *out_value) {
    if (range == NULL || range->element_type == NULL || range->next == NULL ||
        cursor == NULL || out_value == NULL) {
        return CMETA_GEN_ERROR;
    }
    if (range->current_version != NULL) {
        if (range->object == NULL)
            return CMETA_GEN_ERROR;
        if (range->current_version(range->object) != range->version)
            return CMETA_GEN_MUTATED;
    }
    return range->next(range->object, cursor, out_value);
}

#define CMETA_RANGE_TYPE_ASSOC(row, ignored) \
    CMETA_TYPE_CTYPE(row) *: &CMETA_TYPE_DESC(row),
#define CMETA_TYPE_SELECT(type, fallback_desc) \
    _Generic((type *)0, \
        CMETA_PP_FOR_EACH_A(CMETA_RANGE_TYPE_ASSOC, ~, CMETA_TYPE_LIST) \
        default: (fallback_desc))
#define CMETA_TYPEOF(type) \
    CMETA_TYPE_SELECT(type, (const cmeta_type_desc *)0)
#define CMETA_TYPEOF_OR(type, fallback_desc) \
    CMETA_TYPE_SELECT(type, (fallback_desc))

#ifdef __cplusplus
}
#endif
#endif /* CMETA_RANGE_H */
