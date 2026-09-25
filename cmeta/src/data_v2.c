/*
 * Strict buffer lifecycle facade.
 *
 * The canonical data implementation remains the single source for every
 * non-buffer semantic descriptor.  Its v1-era public buffer facades are
 * renamed inside this translation unit and are intentionally not declared by
 * the SDK.  The public symbols below admit only the v2 provider contract; no
 * legacy-provider fallback is reachable through the public API.
 */
#define cmeta_data_buffer_ops_of cmeta_data_buffer_ops_of_legacy_internal
#define cmeta_data_buffer_is_zero cmeta_data_buffer_is_zero_legacy_internal
#define cmeta_data_buffer_assign cmeta_data_buffer_assign_legacy_internal
#define cmeta_data_buffer_restore_zero cmeta_data_buffer_restore_zero_legacy_internal
#define cmeta_data_buffer_read cmeta_data_buffer_read_legacy_internal
#include "data.c"
#undef cmeta_data_buffer_ops_of
#undef cmeta_data_buffer_is_zero
#undef cmeta_data_buffer_assign
#undef cmeta_data_buffer_restore_zero
#undef cmeta_data_buffer_read

#include <stdlib.h>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

#define CMETA_BUFFER_V2_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
#define CMETA_BUFFER_V2_DESC_OPS_SIZE \
    CMETA_BUFFER_V2_FIELD_END(cmeta_data_desc, buffer_ops)
#define CMETA_BUFFER_V2_OPS_SIZE \
    CMETA_BUFFER_V2_FIELD_END(cmeta_data_buffer_ops, move)
#define CMETA_BUFFER_V2_READ_SIZE \
    CMETA_BUFFER_V2_FIELD_END(cmeta_data_buffer_ops, read)

static cmeta_status cmeta_data_buffer_v2_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_buffer_ops **out) {
    const cmeta_data_buffer_shape *shape;
    const cmeta_data_buffer_ops *ops;

    if (out != NULL)
        *out = NULL;
    if (!cmeta_data_desc_valid(desc) ||
        (desc->kind != CMETA_DATA_STRING && desc->kind != CMETA_DATA_BYTES) ||
        desc->struct_size < CMETA_BUFFER_V2_DESC_OPS_SIZE ||
        desc->buffer_ops == NULL)
        return CMETA_INVALID_ARGUMENT;

    ops = desc->buffer_ops;
    if (ops->struct_size < CMETA_BUFFER_V2_OPS_SIZE ||
        ops->abi_version != CMETA_DATA_BUFFER_OPS_ABI_VERSION ||
        ops->storage_type == NULL ||
        !cmeta_type_desc_valid(ops->storage_type) ||
        !cmeta_data_buffer_ownership_valid(ops->ownership) ||
        ops->is_zero == NULL || ops->assign == NULL ||
        ops->restore_zero == NULL || ops->init_zero == NULL ||
        ops->move == NULL)
        return CMETA_INVALID_ARGUMENT;

    shape = (const cmeta_data_buffer_shape *)desc->shape;
    if (!cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align ||
        shape->ownership != ops->ownership)
        return CMETA_TYPE_MISMATCH;

    if (out != NULL)
        *out = ops;
    return CMETA_OK;
}

const cmeta_data_buffer_ops *cmeta_data_buffer_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_buffer_ops *ops = NULL;
    return cmeta_data_buffer_v2_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_buffer_init_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_v2_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;

    status = ops->init_zero(object);
    if (status != CMETA_OK) {
        ops->restore_zero(object);
        return ops->is_zero(object) ? status : CMETA_CALLBACK_ERROR;
    }
    if (!ops->is_zero(object)) {
        ops->restore_zero(object);
        return CMETA_CALLBACK_ERROR;
    }
    return CMETA_OK;
}

cmeta_status cmeta_data_buffer_move(
    const cmeta_data_desc *desc, void *destination, void *source) {
    const cmeta_data_buffer_ops *ops = NULL;
    bool source_was_zero;
    cmeta_status status;

    if (destination == NULL || source == NULL || destination == source)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_v2_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (!ops->is_zero(destination))
        return CMETA_INVALID_ARGUMENT;

    source_was_zero = ops->is_zero(source);
    ops->move(destination, source);
    if (!ops->is_zero(source) || ops->is_zero(destination) != source_was_zero) {
        ops->restore_zero(destination);
        ops->restore_zero(source);
        return ops->is_zero(destination) && ops->is_zero(source)
                   ? CMETA_CALLBACK_ERROR
                   : CMETA_CALLBACK_ERROR;
    }
    return CMETA_OK;
}

cmeta_status cmeta_data_buffer_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_v2_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->is_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_buffer_assign(
    const cmeta_data_desc *desc, void *object,
    const unsigned char *data, size_t size, size_t max_bytes) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_v2_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (!ops->is_zero(object))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;

    status = ops->assign(object, data, size, max_bytes);
    if (status != CMETA_OK) {
        ops->restore_zero(object);
        if (!ops->is_zero(object))
            return CMETA_CALLBACK_ERROR;
    }
    return status;
}

cmeta_status cmeta_data_buffer_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_v2_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    ops->restore_zero(object);
    return ops->is_zero(object) ? CMETA_OK : CMETA_CALLBACK_ERROR;
}

cmeta_status cmeta_data_buffer_read(
    const cmeta_data_desc *desc, const void *object, size_t max_bytes,
    const unsigned char **out_data, size_t *out_size) {
    const cmeta_data_buffer_ops *ops = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;
    cmeta_status status;

    if (object == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_v2_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (ops->struct_size < CMETA_BUFFER_V2_READ_SIZE || ops->read == NULL)
        return CMETA_TRAIT_MISSING;

    status = ops->read(object, &data, &size);
    if (status != CMETA_OK)
        return status;
    if (size != 0u && data == NULL)
        return CMETA_CALLBACK_ERROR;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;

    *out_data = data;
    *out_size = size;
    return CMETA_OK;
}

#undef CMETA_BUFFER_V2_READ_SIZE
#undef CMETA_BUFFER_V2_OPS_SIZE
#undef CMETA_BUFFER_V2_DESC_OPS_SIZE
#undef CMETA_BUFFER_V2_FIELD_END


#define CMETA_COLLECTION_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
#define CMETA_COLLECTION_DESC_OPS_SIZE \
    CMETA_COLLECTION_FIELD_END(cmeta_data_desc, collection_ops)
#define CMETA_COLLECTION_OPS_BASE_SIZE \
    CMETA_COLLECTION_FIELD_END(cmeta_data_collection_ops, foreach)
#define CMETA_COLLECTION_OPS_COLLECTOR_SIZE \
    CMETA_COLLECTION_FIELD_END(cmeta_data_collection_ops, collector)

static cmeta_status cmeta_data_collection_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_collection_ops **out) {
    const cmeta_data_collection_ops *ops;

    if (out != NULL) *out = NULL;
    if (!cmeta_data_desc_valid(desc) ||
        (desc->kind != CMETA_DATA_SEQUENCE && desc->kind != CMETA_DATA_SET) ||
        desc->struct_size < CMETA_COLLECTION_DESC_OPS_SIZE ||
        desc->collection_ops == NULL)
        return CMETA_INVALID_ARGUMENT;

    ops = desc->collection_ops;
    if (ops->struct_size < CMETA_COLLECTION_OPS_BASE_SIZE ||
        ops->abi_version != CMETA_DATA_COLLECTION_OPS_ABI_VERSION ||
        ops->storage_type == NULL || !cmeta_type_desc_valid(ops->storage_type) ||
        ops->element == NULL || (ops->read == NULL && ops->foreach == NULL))
        return CMETA_INVALID_ARGUMENT;

    if (desc->storage_type == NULL ||
        !cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align)
        return CMETA_TYPE_MISMATCH;

    if (out != NULL) *out = ops;
    return CMETA_OK;
}

const cmeta_data_collection_ops *cmeta_data_collection_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_collection_ops *ops = NULL;
    return cmeta_data_collection_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_collection_read(
    const cmeta_data_desc *desc, const void *object,
    cmeta_data_collection_view *out) {
    const cmeta_data_collection_ops *ops = NULL;
    cmeta_data_collection_view view = {0};
    cmeta_status status;

    if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_collection_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;

    view.element = ops->element(object);
    if (view.element == NULL || !cmeta_data_desc_valid(view.element))
        return CMETA_TRAIT_MISSING;
    status = ops->read(object, &view);
    if (status != CMETA_OK) return status;
    if (view.element == NULL || !cmeta_data_desc_valid(view.element))
        return CMETA_CALLBACK_ERROR;
    if (view.count != 0u &&
        (view.data == NULL || view.stride == 0u))
        return CMETA_CALLBACK_ERROR;
    if (view.count == 0u && view.data != NULL && view.stride == 0u)
        return CMETA_CALLBACK_ERROR;

    *out = view;
    return CMETA_OK;
}

#undef CMETA_COLLECTION_OPS_COLLECTOR_SIZE
#undef CMETA_COLLECTION_OPS_BASE_SIZE
#undef CMETA_COLLECTION_DESC_OPS_SIZE
#undef CMETA_COLLECTION_FIELD_END


const cmeta_data_desc *cmeta_data_integer_width(bool is_signed, uint8_t bits) {
    switch (bits) {
        case 8u: return is_signed ? &cmeta_data_int8 : &cmeta_data_uint8;
        case 16u: return is_signed ? &cmeta_data_int16 : &cmeta_data_uint16;
        case 32u: return is_signed ? &cmeta_data_int32 : &cmeta_data_uint32;
        case 64u: return is_signed ? &cmeta_data_int64 : &cmeta_data_uint64;
        default: return NULL;
    }
}


typedef struct cmeta_data_collection_foreach_context {
    cmeta_data_collection_visit_fn visit;
    void *context;
    size_t remaining;
} cmeta_data_collection_foreach_context;

static cmeta_status cmeta_data_collection_bounded_visit(
    void *context, const void *element) {
    cmeta_data_collection_foreach_context *bounded =
        (cmeta_data_collection_foreach_context *)context;
    if (bounded == NULL || bounded->visit == NULL || element == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (bounded->remaining == 0u) return CMETA_CAPACITY_EXCEEDED;
    --bounded->remaining;
    return bounded->visit(bounded->context, element);
}

cmeta_status cmeta_data_collection_foreach(
    const cmeta_data_desc *desc, const void *object,
    cmeta_data_collection_visit_fn visit, void *context, size_t max_items) {
    const cmeta_data_collection_ops *ops = NULL;
    cmeta_data_collection_foreach_context bounded;
    cmeta_status status;
    size_t i;

    if (object == NULL || visit == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_collection_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;

    if (ops->foreach != NULL) {
        bounded.visit = visit;
        bounded.context = context;
        bounded.remaining = max_items;
        return ops->foreach(
            object, cmeta_data_collection_bounded_visit, &bounded, max_items);
    }

    if (ops->read != NULL) {
        cmeta_data_collection_view view = {0};
        status = cmeta_data_collection_read(desc, object, &view);
        if (status != CMETA_OK) return status;
        if (view.count > max_items) return CMETA_CAPACITY_EXCEEDED;
        for (i = 0u; i < view.count; ++i) {
            status = visit(context,
                           (const unsigned char *)view.data + i * view.stride);
            if (status != CMETA_OK) return status;
        }
        return CMETA_OK;
    }
    return CMETA_TRAIT_MISSING;
}


#define CMETA_MAP_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
#define CMETA_MAP_DESC_OPS_SIZE CMETA_MAP_FIELD_END(cmeta_data_desc, map_ops)
#define CMETA_MAP_OPS_BASE_SIZE CMETA_MAP_FIELD_END(cmeta_data_map_ops, foreach)
#define CMETA_MAP_OPS_COLLECTOR_SIZE CMETA_MAP_FIELD_END(cmeta_data_map_ops, collector)
#define CMETA_MAP_OPS_ACCEPT_SIZE CMETA_MAP_FIELD_END(cmeta_data_map_ops, accept)

static cmeta_status cmeta_data_map_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_map_ops **out) {
    const cmeta_data_map_ops *ops;
    if (out != NULL) *out = NULL;
    if (!cmeta_data_desc_valid(desc) || desc->kind != CMETA_DATA_MAP ||
        desc->struct_size < CMETA_MAP_DESC_OPS_SIZE || desc->map_ops == NULL)
        return CMETA_INVALID_ARGUMENT;
    ops = desc->map_ops;
    if (ops->struct_size < CMETA_MAP_OPS_BASE_SIZE ||
        ops->abi_version != CMETA_DATA_MAP_OPS_ABI_VERSION ||
        ops->storage_type == NULL || !cmeta_type_desc_valid(ops->storage_type) ||
        (ops->flags & ~CMETA_DATA_MAP_FLAGS_MASK) != 0u ||
        ((ops->flags & CMETA_DATA_MAP_UNIQUE_KEYS) != 0u &&
         (ops->flags & CMETA_DATA_MAP_REPEATED_KEYS) != 0u) ||
        ((ops->flags & (CMETA_DATA_MAP_UNIQUE_KEYS |
                        CMETA_DATA_MAP_REPEATED_KEYS)) == 0u) ||
        ops->key == NULL || ops->value == NULL || ops->foreach == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (desc->storage_type == NULL ||
        !cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align)
        return CMETA_TYPE_MISMATCH;
    if (out != NULL) *out = ops;
    return CMETA_OK;
}

const cmeta_data_map_ops *cmeta_data_map_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_map_ops *ops = NULL;
    return cmeta_data_map_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

typedef struct cmeta_data_map_bounded_context {
    cmeta_data_map_visit_fn visit;
    void *context;
    size_t remaining;
} cmeta_data_map_bounded_context;

static cmeta_status cmeta_data_map_bounded_visit(
    void *context, const void *key, const void *value) {
    cmeta_data_map_bounded_context *bounded =
        (cmeta_data_map_bounded_context *)context;
    if (bounded == NULL || bounded->visit == NULL || key == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (bounded->remaining == 0u) return CMETA_CAPACITY_EXCEEDED;
    --bounded->remaining;
    return bounded->visit(bounded->context, key, value);
}

cmeta_status cmeta_data_map_foreach(
    const cmeta_data_desc *desc, const void *object,
    cmeta_data_map_visit_fn visit, void *context, size_t max_items) {
    const cmeta_data_map_ops *ops = NULL;
    const cmeta_data_desc *key_data;
    const cmeta_data_desc *value_data;
    cmeta_data_map_bounded_context bounded;
    cmeta_status status;
    if (object == NULL || visit == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_map_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    key_data = ops->key(object);
    value_data = ops->value(object);
    if (key_data == NULL || value_data == NULL ||
        !cmeta_data_desc_valid(key_data) || !cmeta_data_desc_valid(value_data))
        return CMETA_TRAIT_MISSING;
    bounded.visit = visit;
    bounded.context = context;
    bounded.remaining = max_items;
    return ops->foreach(object, cmeta_data_map_bounded_visit, &bounded, max_items);
}

#undef CMETA_MAP_OPS_ACCEPT_SIZE
#undef CMETA_MAP_OPS_COLLECTOR_SIZE
#undef CMETA_MAP_OPS_BASE_SIZE
#undef CMETA_MAP_DESC_OPS_SIZE
#undef CMETA_MAP_FIELD_END


#define CMETA_CONSTRUCT_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
#define CMETA_CONSTRUCT_DESC_SIZE \
    CMETA_CONSTRUCT_FIELD_END(cmeta_data_desc, construct_ops)
#define CMETA_CONSTRUCT_OPS_SIZE \
    CMETA_CONSTRUCT_FIELD_END(cmeta_data_construct_ops, move)

static cmeta_status cmeta_data_construct_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_construct_ops **out) {
    const cmeta_data_construct_ops *ops;
    if (out != NULL) *out = NULL;
    if (!cmeta_data_desc_valid(desc))
        return CMETA_INVALID_ARGUMENT;
    if (desc->struct_size < CMETA_CONSTRUCT_DESC_SIZE ||
        desc->construct_ops == NULL)
        return CMETA_TRAIT_MISSING;
    ops = desc->construct_ops;
    if (ops->struct_size < CMETA_CONSTRUCT_OPS_SIZE ||
        ops->abi_version != CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION ||
        ops->storage_type == NULL || !cmeta_type_desc_valid(ops->storage_type) ||
        ops->init_zero == NULL || ops->restore_zero == NULL || ops->move == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (desc->storage_type == NULL ||
        !cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align)
        return CMETA_TYPE_MISMATCH;
    if (out != NULL) *out = ops;
    return CMETA_OK;
}

const cmeta_data_construct_ops *cmeta_data_construct_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_construct_ops *ops = NULL;
    return cmeta_data_construct_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_construct_init_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_construct_ops *ops = NULL;
    cmeta_status status;
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_construct_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    status = ops->init_zero(object);
    if (status != CMETA_OK) ops->restore_zero(object);
    return status;
}

cmeta_status cmeta_data_construct_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_construct_ops *ops = NULL;
    cmeta_status status;
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_construct_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    ops->restore_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_construct_move(
    const cmeta_data_desc *desc, void *destination, void *source) {
    const cmeta_data_construct_ops *ops = NULL;
    cmeta_status status;
    if (destination == NULL || source == NULL || destination == source)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_construct_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    ops->move(destination, source);
    return CMETA_OK;
}

#undef CMETA_CONSTRUCT_OPS_SIZE
#undef CMETA_CONSTRUCT_DESC_SIZE
#undef CMETA_CONSTRUCT_FIELD_END


cmeta_status cmeta_data_collection_collector(
    const cmeta_data_desc *desc, void *zero_output, size_t limit,
    cmeta_collector *out) {
    const cmeta_data_collection_ops *ops = NULL;
    cmeta_collector collector;
    cmeta_status status;
    if (zero_output == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_collection_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    if (ops->struct_size < CMETA_COLLECTION_OPS_COLLECTOR_SIZE ||
        ops->collector == NULL)
        return CMETA_TRAIT_MISSING;
    collector = ops->collector(zero_output, limit);
    if (!cmeta_collector_ops_valid(collector.ops) ||
        collector.zero_output != zero_output ||
        collector.limit != limit ||
        collector.input_type == NULL)
        return CMETA_CALLBACK_ERROR;
    *out = collector;
    return CMETA_OK;
}


cmeta_status cmeta_data_map_collector(
    const cmeta_data_desc *desc, void *zero_output, size_t limit,
    cmeta_collector *out) {
    const cmeta_data_map_ops *ops = NULL;
    cmeta_collector collector;
    cmeta_status status;
    if (zero_output == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_map_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    if (ops->struct_size < CMETA_MAP_OPS_COLLECTOR_SIZE ||
        ops->collector == NULL)
        return CMETA_TRAIT_MISSING;
    collector = ops->collector(zero_output, limit);
    if (!cmeta_collector_ops_valid(collector.ops) ||
        collector.zero_output != zero_output ||
        collector.limit != limit ||
        collector.input_type == NULL)
        return CMETA_CALLBACK_ERROR;
    *out = collector;
    return CMETA_OK;
}


cmeta_status cmeta_data_collection_accept(
    const cmeta_data_desc *desc, cmeta_collector *collector,
    const cmeta_data_desc *element_data, const void *element) {
    const cmeta_data_collection_ops *ops = NULL;
    const cmeta_data_desc *expected;
    cmeta_status status;
    if (collector == NULL || element_data == NULL || element == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_collection_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    expected = ops->element(collector->zero_output);
    if (expected == NULL || !cmeta_data_desc_valid(expected) ||
        !cmeta_data_desc_valid(element_data))
        return CMETA_TRAIT_MISSING;
    if (expected != element_data &&
        (expected->stable_id == NULL || element_data->stable_id == NULL ||
         strcmp(expected->stable_id, element_data->stable_id) != 0))
        return CMETA_TYPE_MISMATCH;
    if (element_data->storage_type == NULL ||
        !cmeta_type_equal(element_data->storage_type, collector->input_type))
        return CMETA_TYPE_MISMATCH;
    return cmeta_collector_accept(
        collector, element_data->storage_type, element);
}


cmeta_status cmeta_data_value_init_zero(
    const cmeta_data_desc *desc, void *object) {
    if (!cmeta_data_desc_valid(desc) || object == NULL ||
        desc->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            memset(object, 0, desc->storage_type->size);
            return CMETA_OK;
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            return cmeta_data_buffer_init_zero(desc, object);
        case CMETA_DATA_ENUM:
            if (desc->enum_bits_ops != NULL)
                return cmeta_data_enum_bits_restore_zero(desc, object);
            return cmeta_data_enum_restore_zero(desc, object);
        case CMETA_DATA_STRUCT:
            return cmeta_data_struct_init_zero(desc, object);
        default:
            break;
    }
    if (desc->fixed_ops != NULL)
        return cmeta_data_fixed_restore_zero(desc, object);
    if (desc->variant_ops != NULL)
        return cmeta_data_variant_restore_zero(desc, object);
    return cmeta_data_construct_init_zero(desc, object);
}

cmeta_status cmeta_data_value_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    if (!cmeta_data_desc_valid(desc) || object == NULL ||
        desc->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            memset(object, 0, desc->storage_type->size);
            return CMETA_OK;
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            return cmeta_data_buffer_restore_zero(desc, object);
        case CMETA_DATA_ENUM:
            if (desc->enum_bits_ops != NULL)
                return cmeta_data_enum_bits_restore_zero(desc, object);
            return cmeta_data_enum_restore_zero(desc, object);
        case CMETA_DATA_STRUCT:
            return cmeta_data_struct_restore_zero(desc, object);
        default:
            break;
    }
    if (desc->fixed_ops != NULL)
        return cmeta_data_fixed_restore_zero(desc, object);
    if (desc->variant_ops != NULL)
        return cmeta_data_variant_restore_zero(desc, object);
    return cmeta_data_construct_restore_zero(desc, object);
}

static bool cmeta_data_struct_field_bounds_valid(
    const cmeta_data_desc *owner, const cmeta_data_field_desc *field) {
    const cmeta_data_desc *value;
    size_t owner_size;
    if (owner == NULL || field == NULL || field->value == NULL ||
        owner->storage_type == NULL)
        return false;
    value = field->value;
    if (!cmeta_data_desc_valid(value) || value->storage_type == NULL)
        return false;
    owner_size = owner->storage_type->size;
    if (field->offset > owner_size ||
        value->storage_type->size > owner_size - field->offset)
        return false;
    if (value->storage_type->align != 0u &&
        (field->offset % value->storage_type->align) != 0u)
        return false;
    return true;
}

static bool cmeta_data_value_move_supported_depth(
    const cmeta_data_desc *desc, unsigned depth) {
    const cmeta_data_struct_shape *shape;
    size_t i;
    if (depth > 64u || !cmeta_data_desc_valid(desc) ||
        desc->storage_type == NULL)
        return false;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            return true;
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            return cmeta_data_buffer_ops_of(desc) != NULL;
        case CMETA_DATA_STRUCT:
            shape = (const cmeta_data_struct_shape *)desc->shape;
            if (shape == NULL) return false;
            for (i = 0u; i < shape->field_count; ++i) {
                const cmeta_data_field_desc *field = &shape->fields[i];
                if (!cmeta_data_struct_field_bounds_valid(desc, field) ||
                    !cmeta_data_value_move_supported_depth(
                        field->value, depth + 1u))
                    return false;
            }
            return true;
        default:
            return cmeta_data_construct_ops_of(desc) != NULL;
    }
}

bool cmeta_data_struct_constructible(const cmeta_data_desc *desc) {
    return desc != NULL && desc->kind == CMETA_DATA_STRUCT &&
           cmeta_data_value_move_supported_depth(desc, 0u);
}

static cmeta_status cmeta_data_struct_init_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_struct_shape *shape;
    size_t i;
    if (!cmeta_data_struct_constructible(desc) || object == NULL)
        return CMETA_TRAIT_MISSING;
    shape = (const cmeta_data_struct_shape *)desc->shape;
    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        cmeta_status status = cmeta_data_value_init_zero(
            field->value, (unsigned char *)object + field->offset);
        if (status != CMETA_OK) {
            while (i != 0u) {
                --i;
                field = &shape->fields[i];
                (void)cmeta_data_value_restore_zero(
                    field->value, (unsigned char *)object + field->offset);
            }
            return status;
        }
    }
    return CMETA_OK;
}

static cmeta_status cmeta_data_struct_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_struct_shape *shape;
    size_t i;
    cmeta_status result = CMETA_OK;
    if (!cmeta_data_struct_constructible(desc) || object == NULL)
        return CMETA_TRAIT_MISSING;
    shape = (const cmeta_data_struct_shape *)desc->shape;
    i = shape->field_count;
    while (i != 0u) {
        const cmeta_data_field_desc *field = &shape->fields[--i];
        cmeta_status status = cmeta_data_value_restore_zero(
            field->value, (unsigned char *)object + field->offset);
        if (result == CMETA_OK && status != CMETA_OK)
            result = status;
    }
    return result;
}

static cmeta_status cmeta_data_struct_move(
    const cmeta_data_desc *desc, void *destination, void *source) {
    const cmeta_data_struct_shape *shape;
    size_t i;
    if (!cmeta_data_struct_constructible(desc) ||
        destination == NULL || source == NULL || destination == source)
        return CMETA_TRAIT_MISSING;
    shape = (const cmeta_data_struct_shape *)desc->shape;
    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        cmeta_status status = cmeta_data_value_move(
            field->value,
            (unsigned char *)destination + field->offset,
            (unsigned char *)source + field->offset);
        if (status != CMETA_OK)
            return status; /* capability preflight makes this path no-fail */
    }
    return CMETA_OK;
}

bool cmeta_data_value_move_supported(const cmeta_data_desc *desc) {
    return cmeta_data_value_move_supported_depth(desc, 0u);
}

cmeta_status cmeta_data_value_move(
    const cmeta_data_desc *desc, void *destination, void *source) {
    cmeta_status status;
    if (!cmeta_data_desc_valid(desc) || destination == NULL || source == NULL ||
        destination == source || desc->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            memcpy(destination, source, desc->storage_type->size);
            memset(source, 0, desc->storage_type->size);
            return CMETA_OK;
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            return cmeta_data_buffer_move(desc, destination, source);
        case CMETA_DATA_STRUCT:
            return cmeta_data_struct_move(desc, destination, source);
        default:
            break;
    }
    status = cmeta_data_construct_move(desc, destination, source);
    return status;
}

static cmeta_status cmeta_data_temp_init(
    const cmeta_data_desc *desc, void *storage,
    cmeta_data_temp_lifecycle *lifecycle) {
    cmeta_status status;
    if (desc == NULL || storage == NULL || lifecycle == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_value_init_zero(desc, storage);
    if (status != CMETA_OK) return status;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            *lifecycle = CMETA_DATA_TEMP_TRIVIAL; break;
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            *lifecycle = CMETA_DATA_TEMP_BUFFER; break;
        case CMETA_DATA_ENUM:
            *lifecycle = desc->enum_bits_ops != NULL
                             ? CMETA_DATA_TEMP_ENUM_BITS
                             : CMETA_DATA_TEMP_ENUM;
            break;
        default:
            if (desc->fixed_ops != NULL) *lifecycle = CMETA_DATA_TEMP_FIXED;
            else if (desc->variant_ops != NULL) *lifecycle = CMETA_DATA_TEMP_VARIANT;
            else *lifecycle = CMETA_DATA_TEMP_CONSTRUCT;
            break;
    }
    return CMETA_OK;
}

cmeta_status cmeta_data_temp_open(
    const cmeta_data_desc *desc, size_t max_bytes, cmeta_data_temp *out) {
    void *storage;
    size_t extent;
    size_t alignment;
    size_t padded;
    cmeta_status status;
    cmeta_data_temp_lifecycle lifecycle = CMETA_DATA_TEMP_NONE;

    if (out == NULL || !cmeta_data_desc_valid(desc) ||
        desc->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = (cmeta_data_temp){0};
    extent = desc->storage_type->size;
    alignment = desc->storage_type->align;
    if (extent == 0u || alignment == 0u || extent > max_bytes)
        return extent > max_bytes ? CMETA_CAPACITY_EXCEEDED
                                  : CMETA_INVALID_ARGUMENT;
    if ((alignment & (alignment - 1u)) != 0u)
        return CMETA_INVALID_ARGUMENT;
    if (alignment <= _Alignof(max_align_t)) {
        padded = extent;
        storage = malloc(padded);
    } else {
        if (extent > SIZE_MAX - (alignment - 1u))
            return CMETA_CAPACITY_EXCEEDED;
        padded = (extent + alignment - 1u) & ~(alignment - 1u);
#if defined(_MSC_VER)
        storage = _aligned_malloc(padded, alignment);
#else
        storage = aligned_alloc(alignment, padded);
#endif
    }
    if (storage == NULL) return CMETA_OUT_OF_MEMORY;
    memset(storage, 0, padded);
    status = cmeta_data_temp_init(desc, storage, &lifecycle);
    if (status != CMETA_OK) {
#if defined(_MSC_VER)
        if (alignment > _Alignof(max_align_t)) _aligned_free(storage);
        else free(storage);
#else
        free(storage);
#endif
        return status;
    }
    out->data = desc;
    out->storage = storage;
    out->extent = extent;
    out->alignment = alignment;
    out->lifecycle = lifecycle;
    return CMETA_OK;
}

void cmeta_data_temp_close(cmeta_data_temp *temp) {
    if (temp == NULL || temp->storage == NULL) return;
    if (temp->data != NULL)
        (void)cmeta_data_value_restore_zero(temp->data, temp->storage);
#if defined(_MSC_VER)
    if (temp->alignment > _Alignof(max_align_t)) _aligned_free(temp->storage);
    else free(temp->storage);
#else
    free(temp->storage);
#endif
    *temp = (cmeta_data_temp){0};
}


cmeta_status cmeta_data_map_accept(
    const cmeta_data_desc *desc, cmeta_collector *collector,
    const cmeta_data_desc *key_data, const void *key,
    const cmeta_data_desc *value_data, const void *value) {
    const cmeta_data_map_ops *ops = NULL;
    const cmeta_data_desc *expected_key;
    const cmeta_data_desc *expected_value;
    cmeta_status status;
    if (collector == NULL || key_data == NULL || key == NULL ||
        value_data == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_map_ops_status(desc, &ops);
    if (status != CMETA_OK) return status;
    expected_key = ops->key(collector->zero_output);
    expected_value = ops->value(collector->zero_output);
    if (expected_key == NULL || expected_value == NULL ||
        !cmeta_data_desc_valid(expected_key) ||
        !cmeta_data_desc_valid(expected_value))
        return CMETA_TRAIT_MISSING;
    if (expected_key != key_data || expected_value != value_data)
        return CMETA_TYPE_MISMATCH;
    if (ops->struct_size < CMETA_MAP_OPS_ACCEPT_SIZE || ops->accept == NULL)
        return CMETA_TRAIT_MISSING;
    return ops->accept(collector, key_data, key, value_data, value);
}
