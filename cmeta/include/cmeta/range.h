#ifndef CMETA_RANGE_H
#define CMETA_RANGE_H

#include <cmeta/cmeta.h>
#include <cmeta/collector.h>
#include <cmeta/data.h>
#include <cmeta/declared_type.h>
#include <cmeta/type_select.h>

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

enum {
    CMETA_CONTAINER_TYPE_OPS_ABI_VERSION = 1u,
    CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION = 1u,
    CMETA_CONTAINER_EXT_ABI_VERSION = 1u
};

typedef const cmeta_type_desc *(*cmeta_container_type_argument_fn)(
    const void *object, size_t index);

typedef struct cmeta_container_type_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_generic_desc *constructor;
    size_t arity;
    cmeta_container_type_argument_fn argument;
} cmeta_container_type_ops;

struct cmeta_container_desc;

typedef cmeta_status (*cmeta_container_bind_types_fn)(
    void *object, const cmeta_type_desc *const *arguments, size_t arity);

typedef struct cmeta_container_construct_ops {
    size_t struct_size;
    uint32_t abi_version;
    const struct cmeta_container_desc *descriptor;
    cmeta_container_bind_types_fn bind_types;
} cmeta_container_construct_ops;

typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
    const cmeta_data_desc *data;
    const cmeta_container_construct_ops *construction;
} cmeta_container_ext;

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
    const cmeta_container_ext *ext;
} cmeta_container_desc;

typedef struct cmeta_container_header {
    const cmeta_container_desc *descriptor;
} cmeta_container_header;

static inline const cmeta_container_desc *cmeta_container_descriptor(const void *object) {
    const cmeta_container_header *header = (const cmeta_container_header *)object;
    return header != NULL ? header->descriptor : NULL;
}

const cmeta_container_ext *cmeta_container_extension(const void *object);
const cmeta_data_desc *cmeta_container_data(const void *object);
const cmeta_container_construct_ops *
cmeta_container_construction(const void *object);
const cmeta_generic_desc *cmeta_container_type_constructor(const void *object);
size_t cmeta_container_type_arity(const void *object);
const cmeta_type_desc *cmeta_container_type_argument(const void *object,
                                                     size_t index);
bool cmeta_container_type_application_valid(const void *object);
cmeta_status cmeta_container_bind_types(
    void *object, const cmeta_declared_type *declared);

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

#ifdef __cplusplus
}
#endif
#endif /* CMETA_RANGE_H */
