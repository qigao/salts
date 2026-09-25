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
#define CMETA_COLLECTION_OPS_SIZE \
    CMETA_COLLECTION_FIELD_END(cmeta_data_collection_ops, read)

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
    if (ops->struct_size < CMETA_COLLECTION_OPS_SIZE ||
        ops->abi_version != CMETA_DATA_COLLECTION_OPS_ABI_VERSION ||
        ops->storage_type == NULL || !cmeta_type_desc_valid(ops->storage_type) ||
        ops->read == NULL)
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

    status = ops->read(object, &view);
    if (status != CMETA_OK) return status;
    if (view.count != 0u &&
        (view.data == NULL || view.stride == 0u ||
         view.element == NULL || !cmeta_data_desc_valid(view.element)))
        return CMETA_CALLBACK_ERROR;
    if (view.count == 0u && view.data != NULL && view.stride == 0u)
        return CMETA_CALLBACK_ERROR;

    *out = view;
    return CMETA_OK;
}

#undef CMETA_COLLECTION_OPS_SIZE
#undef CMETA_COLLECTION_DESC_OPS_SIZE
#undef CMETA_COLLECTION_FIELD_END
