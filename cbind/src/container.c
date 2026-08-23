#include "internal.h"

#include <string.h>

typedef union cbind_scalar_storage {
    bool boolean;
    int sint;
    long slong;
    size_t uint;
    float f32;
    double f64;
} cbind_scalar_storage;

bool cbind_data_kind_is_container(cmeta_data_kind kind) {
    return kind == CMETA_DATA_SEQUENCE || kind == CMETA_DATA_SET ||
           kind == CMETA_DATA_MAP;
}

static cbind_status cbind_container_error(
    cbind_error *error, cbind_status status, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth) {
    cbind_error_set(error, status, CSERDE_OK, shape, field, depth);
    return status;
}

static cbind_status cbind_container_target_error(
    cbind_decode_state *state, cmeta_status target,
    const cmeta_data_desc *shape, const cmeta_data_field_desc *field,
    size_t depth) {
    cbind_status status = target == CMETA_CAPACITY_EXCEEDED
                              ? CBIND_LIMIT_EXCEEDED
                              : CBIND_TARGET_ERROR;
    cbind_error_set(state->error, status, CSERDE_OK, shape, field, depth);
    cbind_error_set_target(state->error, target);
    return status;
}

static bool cbind_container_ext_valid(
    const cmeta_container_ext *ext,
    const cmeta_container_construct_ops *construction) {
    return ext != NULL &&
           ext->struct_size >= CBIND_FIELD_END(cmeta_container_ext,
                                               construction) &&
           ext->abi_version == CMETA_CONTAINER_EXT_ABI_VERSION &&
           ext->construction == construction;
}

cbind_status cbind_validate_container_field(
    const cbind_context *context, const cmeta_data_desc *shape,
    const cmeta_field_desc *reflected,
    const cmeta_data_field_desc *field, size_t depth, cbind_error *error) {
    const cmeta_declared_type *declared;
    const cmeta_container_construct_ops *construction;
    const cmeta_container_desc *descriptor;
    const cmeta_container_ext *ext;
    size_t expected_arity;
    size_t i;

    if (context->struct_size <
        CBIND_FIELD_END(cbind_context, max_container_items))
        return cbind_container_error(error, CBIND_INVALID_CONTEXT, shape,
                                     field, depth);
    if (reflected == NULL || reflected->declared_type == NULL)
        return cbind_container_error(error, CBIND_INVALID_SHAPE, shape,
                                     field, depth);

    declared = reflected->declared_type;
    if (!cmeta_declared_type_constructible(declared) ||
        declared->storage_type == NULL || reflected->type == NULL ||
        reflected->type != declared->storage_type ||
        reflected->size != declared->storage_type->size ||
        reflected->align != declared->storage_type->align)
        return cbind_container_error(error, CBIND_INVALID_SHAPE, shape,
                                     field, depth);

    construction = declared->construction;
    if (construction->struct_size <
            CBIND_FIELD_END(cmeta_container_construct_ops, restore_zero) ||
        construction->restore_zero == NULL)
        return cbind_container_error(error, CBIND_UNSUPPORTED, shape,
                                     field, depth);

    descriptor = construction->descriptor;
    ext = descriptor != NULL ? descriptor->ext : NULL;
    if (!cbind_container_ext_valid(ext, construction) || ext->data == NULL ||
        !cmeta_data_desc_valid(ext->data) || ext->data->kind != shape->kind ||
        descriptor->collector == NULL)
        return cbind_container_error(error, CBIND_INVALID_SHAPE, shape,
                                     field, depth);

    expected_arity = shape->kind == CMETA_DATA_MAP ? 2u : 1u;
    if (declared->arity != expected_arity)
        return cbind_container_error(error, CBIND_INVALID_SHAPE, shape,
                                     field, depth);
    for (i = 0u; i < expected_arity; ++i)
        if (cbind_scalar_shape_for_type(declared->arguments[i]) == NULL)
            return cbind_container_error(error, CBIND_UNSUPPORTED, shape,
                                         field, depth);

    return CBIND_OK;
}

bool cbind_container_is_zero(const cmeta_field_desc *reflected,
                             const void *value) {
    const unsigned char *bytes = (const unsigned char *)value;
    size_t i;

    if (reflected == NULL || value == NULL)
        return false;
    for (i = 0u; i < reflected->size; ++i)
        if (bytes[i] != 0u)
            return false;
    return true;
}

void cbind_container_reset(const cmeta_field_desc *reflected, void *value) {
    if (reflected == NULL || reflected->declared_type == NULL || value == NULL)
        return;
    (void)cmeta_container_restore_zero(value, reflected->declared_type);
}

static cbind_status cbind_container_read(
    cbind_decode_state *state, cserde_token *token,
    const cmeta_data_desc *shape, const cmeta_data_field_desc *field,
    size_t depth, cmeta_collector *collector) {
    cbind_status status = cbind_read_required(state, token, shape, field, depth);
    if (status != CBIND_OK)
        cmeta_collector_abort(collector);
    return status;
}

static cbind_status cbind_decode_linear_container(
    cbind_decode_state *state, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth,
    const cmeta_declared_type *declared, cmeta_collector *collector) {
    const cmeta_data_desc *element_shape =
        cbind_scalar_shape_for_type(declared->arguments[0]);
    cserde_token token = {0};
    cbind_status status;

    status = cbind_container_read(state, &token, shape, field, depth,
                                  collector);
    if (status != CBIND_OK)
        return status;
    if (token.kind != CSERDE_ARRAY_BEGIN) {
        cmeta_collector_abort(collector);
        return cbind_container_error(state->error, CBIND_TOKEN_MISMATCH,
                                     shape, field, depth);
    }

    for (;;) {
        cbind_scalar_storage scalar = {0};
        cmeta_status target;

        status = cbind_container_read(state, &token, shape, field, depth,
                                      collector);
        if (status != CBIND_OK)
            return status;
        if (token.kind == CSERDE_ARRAY_END)
            break;
        if (collector->count >= collector->limit) {
            cmeta_collector_abort(collector);
            return cbind_container_target_error(
                state, CMETA_CAPACITY_EXCEEDED, shape, field, depth);
        }
        status = cbind_decode_scalar_token(state, element_shape, field, depth,
                                           &token, &scalar);
        if (status != CBIND_OK) {
            cmeta_collector_abort(collector);
            return status;
        }
        target = cmeta_collector_accept(
            collector, declared->arguments[0], &scalar);
        if (target != CMETA_OK)
            return cbind_container_target_error(state, target, shape, field,
                                                depth);
    }

    {
        cmeta_status target = cmeta_collector_finish(collector);
        return target == CMETA_OK
                   ? CBIND_OK
                   : cbind_container_target_error(state, target, shape,
                                                  field, depth);
    }
}

static cbind_status cbind_decode_map_container(
    cbind_decode_state *state, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth,
    const cmeta_declared_type *declared, cmeta_collector *collector) {
    const cmeta_data_desc *key_shape =
        cbind_scalar_shape_for_type(declared->arguments[0]);
    const cmeta_data_desc *value_shape =
        cbind_scalar_shape_for_type(declared->arguments[1]);
    cserde_token token = {0};
    cbind_status status;

    status = cbind_container_read(state, &token, shape, field, depth,
                                  collector);
    if (status != CBIND_OK)
        return status;
    if (token.kind != CSERDE_MAP_BEGIN) {
        cmeta_collector_abort(collector);
        return cbind_container_error(state->error, CBIND_TOKEN_MISMATCH,
                                     shape, field, depth);
    }

    for (;;) {
        cbind_scalar_storage key = {0};
        cbind_scalar_storage value = {0};
        cmeta_entry entry;
        cmeta_status target;

        status = cbind_container_read(state, &token, shape, field, depth,
                                      collector);
        if (status != CBIND_OK)
            return status;
        if (token.kind == CSERDE_MAP_END)
            break;
        if (collector->count >= collector->limit) {
            cmeta_collector_abort(collector);
            return cbind_container_target_error(
                state, CMETA_CAPACITY_EXCEEDED, shape, field, depth);
        }
        status = cbind_decode_scalar_token(state, key_shape, field, depth,
                                           &token, &key);
        if (status != CBIND_OK) {
            cmeta_collector_abort(collector);
            return status;
        }
        status = cbind_container_read(state, &token, shape, field, depth,
                                      collector);
        if (status != CBIND_OK)
            return status;
        status = cbind_decode_scalar_token(state, value_shape, field, depth,
                                           &token, &value);
        if (status != CBIND_OK) {
            cmeta_collector_abort(collector);
            return status;
        }
        entry = (cmeta_entry){
            declared->arguments[0], declared->arguments[1],
            &key, &value, NULL, NULL};
        target = cmeta_collector_accept(
            collector, collector->input_type, &entry);
        if (target != CMETA_OK)
            return cbind_container_target_error(state, target, shape, field,
                                                depth);
    }

    {
        cmeta_status target = cmeta_collector_finish(collector);
        return target == CMETA_OK
                   ? CBIND_OK
                   : cbind_container_target_error(state, target, shape,
                                                  field, depth);
    }
}

cbind_status cbind_decode_container(
    cbind_decode_state *state, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, const cmeta_field_desc *reflected,
    size_t depth, void *out) {
    const cmeta_declared_type *declared = reflected->declared_type;
    const cmeta_container_desc *descriptor =
        declared->construction->descriptor;
    cmeta_collector collector =
        descriptor->collector(out, state->context->max_container_items);
    cmeta_status target = cmeta_collector_begin(&collector);

    if (target != CMETA_OK)
        return cbind_container_target_error(state, target, shape, field, depth);
    if (shape->kind == CMETA_DATA_MAP)
        return cbind_decode_map_container(state, shape, field, depth,
                                          declared, &collector);
    return cbind_decode_linear_container(state, shape, field, depth,
                                         declared, &collector);
}
