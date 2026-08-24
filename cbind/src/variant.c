#include "internal.h"

#include <limits.h>
#include <stdint.h>

static cbind_status cbind_variant_error(
    cbind_error *error, cbind_status status, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth) {
    cbind_error_set(error, status, CSERDE_OK, shape, field, depth);
    return status;
}

static cbind_status cbind_variant_target_error(
    cbind_decode_state *state, cmeta_status target,
    const cmeta_data_desc *shape, const cmeta_data_field_desc *field,
    size_t depth) {
    cbind_error_set(state->error, CBIND_TARGET_ERROR, CSERDE_OK,
                    shape, field, depth);
    cbind_error_set_target(state->error, target);
    return CBIND_TARGET_ERROR;
}

static bool cbind_variant_signed_tag_fits(int64_t tag, uint8_t bits) {
    int64_t limit;

    if (bits == 64u)
        return true;
    limit = INT64_C(1) << (bits - 1u);
    return tag >= -limit && tag < limit;
}

static bool cbind_variant_unsigned_tag_fits(int64_t tag, uint8_t bits) {
    uint64_t limit;

    if (tag < 0)
        return false;
    if (bits == 64u)
        return true;
    limit = UINT64_C(1) << bits;
    return (uint64_t)tag < limit;
}

static bool cbind_variant_case_tag_valid(
    const cmeta_data_desc *tag_shape, int64_t tag) {
    if (tag_shape->kind == CMETA_DATA_ENUM) {
        const cmeta_data_enum_shape *enum_shape =
            (const cmeta_data_enum_shape *)tag_shape->shape;
        return cmeta_enum_item_by_value(enum_shape->meta, tag) != NULL;
    }
    if (tag_shape->kind == CMETA_DATA_SINT)
        return cbind_variant_signed_tag_fits(
            tag, ((const cmeta_data_integer_shape *)tag_shape->shape)->bits);
    return cbind_variant_unsigned_tag_fits(
        tag, ((const cmeta_data_integer_shape *)tag_shape->shape)->bits);
}

static bool cbind_variant_member_fits(
    const cmeta_type_desc *storage_type,
    size_t offset,
    const cmeta_type_desc *member_type) {
    if (storage_type == NULL || member_type == NULL ||
        storage_type->align == 0u || member_type->align == 0u ||
        storage_type->align % member_type->align != 0u ||
        offset % member_type->align != 0u ||
        offset > storage_type->size)
        return false;
    return member_type->size <= storage_type->size - offset;
}

static cbind_status cbind_validate_variant_shape(
    const cbind_context *context, const cmeta_data_desc *shape, size_t depth,
    const cbind_validation_frame *parent, size_t active_scratch,
    size_t *max_scratch, cbind_error *error) {
    const cmeta_data_variant_shape *variant_shape;
    cbind_validation_frame frame;
    size_t current_depth;
    size_t i;

    if (!cbind_depth_advance(context, depth, &current_depth))
        return cbind_variant_error(error, CBIND_LIMIT_EXCEEDED, shape, NULL,
                                   current_depth);
    if (shape == NULL || !cmeta_data_desc_valid(shape) ||
        shape->kind != CMETA_DATA_VARIANT)
        return cbind_variant_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                   current_depth);
    if (cbind_validation_cycle_contains(parent, shape))
        return cbind_variant_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                   current_depth);
    if (shape->struct_size <
            CBIND_FIELD_END(cmeta_data_desc, variant_ops) ||
        shape->variant_ops == NULL)
        return cbind_variant_error(error, CBIND_UNSUPPORTED, shape, NULL,
                                   current_depth);
    if (cmeta_data_variant_ops_of(shape) == NULL)
        return cbind_variant_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                   current_depth);

    variant_shape = (const cmeta_data_variant_shape *)shape->shape;
    if (!cbind_variant_member_fits(shape->storage_type,
                                   variant_shape->tag_offset,
                                   variant_shape->tag->storage_type))
        return cbind_variant_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                   current_depth);

    frame.shape = shape;
    frame.parent = parent;
    for (i = 0u; i < variant_shape->case_count; ++i) {
        const cmeta_data_variant_case *item = &variant_shape->cases[i];
        const cmeta_data_desc *child = item->value;
        cbind_status status;

        if (!cbind_variant_case_tag_valid(variant_shape->tag, item->tag))
            return cbind_variant_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                       current_depth);
        if (child == NULL || cbind_data_kind_is_container(child->kind))
            return cbind_variant_error(
                error, child != NULL ? CBIND_UNSUPPORTED : CBIND_INVALID_SHAPE,
                shape, NULL, current_depth);
        if (!cmeta_data_desc_valid(child) ||
            !cbind_variant_member_fits(shape->storage_type, item->offset,
                                       child->storage_type))
            return cbind_variant_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                       current_depth);

        if (child->kind == CMETA_DATA_STRUCT) {
            status = cbind_validate_struct_graph(
                context, child, current_depth, &frame, active_scratch,
                max_scratch, error);
        } else {
            status = cbind_validate_graph(
                context, child, current_depth, &frame, active_scratch,
                max_scratch, error);
        }
        if (status != CBIND_OK)
            return status;
    }
    return CBIND_OK;
}

cbind_status cbind_measure_variant_resources(
    const cbind_context *context, const cmeta_data_desc *shape, size_t depth,
    size_t active_scratch, size_t *max_scratch, cbind_error *error) {
    const cmeta_data_variant_shape *variant_shape =
        (const cmeta_data_variant_shape *)shape->shape;
    size_t current_depth;
    size_t i;

    if (!cbind_depth_advance(context, depth, &current_depth))
        return cbind_variant_error(error, CBIND_LIMIT_EXCEEDED, shape, NULL,
                                   current_depth);

    for (i = 0u; i < variant_shape->case_count; ++i) {
        const cmeta_data_desc *child = variant_shape->cases[i].value;
        cbind_status status;

        if (child->kind == CMETA_DATA_STRUCT) {
            status = cbind_measure_struct_resources(
                context, child, current_depth, active_scratch,
                max_scratch, error);
        } else if (child->kind == CMETA_DATA_VARIANT) {
            status = cbind_measure_variant_resources(
                context, child, current_depth, active_scratch,
                max_scratch, error);
        } else {
            continue;
        }
        if (status != CBIND_OK)
            return status;
    }
    return CBIND_OK;
}

cbind_status cbind_validate_variant_graph(
    const cbind_context *context, const cmeta_data_desc *shape, size_t depth,
    const cbind_validation_frame *parent, size_t active_scratch,
    size_t *max_scratch, cbind_error *error) {
    cbind_status status = cbind_validate_variant_shape(
        context, shape, depth, parent, active_scratch, max_scratch, error);

    if (status != CBIND_OK)
        return status;
    return cbind_measure_variant_resources(context, shape, depth,
                                           active_scratch, max_scratch, error);
}

static cbind_status cbind_variant_numeric_tag_from_token(
    cbind_decode_state *state, const cmeta_data_desc *tag_shape,
    const cmeta_data_desc *variant, const cmeta_data_field_desc *field,
    size_t depth, const cserde_token *token, int64_t *out) {
    int64_t tag;

    if (token->kind == CSERDE_SINT) {
        tag = token->value.sint;
    } else if (token->kind == CSERDE_UINT) {
        if (token->value.uint > (uint64_t)INT64_MAX)
            return cbind_variant_error(state->error, CBIND_VALUE_OUT_OF_RANGE,
                                       variant, field, depth);
        tag = (int64_t)token->value.uint;
    } else {
        return cbind_variant_error(state->error, CBIND_TOKEN_MISMATCH,
                                   variant, field, depth);
    }

    if (!cbind_variant_case_tag_valid(tag_shape, tag))
        return cbind_variant_error(state->error, CBIND_VALUE_OUT_OF_RANGE,
                                   variant, field, depth);
    *out = tag;
    return CBIND_OK;
}

static bool cbind_variant_payload_is_zero(const cmeta_data_desc *shape,
                                          const void *value) {
    return shape->kind == CMETA_DATA_STRUCT
               ? cbind_struct_is_empty(shape, value)
               : cbind_value_is_empty(shape, value);
}

cbind_status cbind_decode_variant(
    cbind_decode_state *state, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *parent_field, size_t depth, void *out) {
    const cmeta_data_variant_shape *variant_shape =
        (const cmeta_data_variant_shape *)shape->shape;
    const cmeta_data_variant_case *item;
    size_t current_depth = depth + 1u;
    cserde_token token = {0};
    int64_t tag = 0;
    void *payload;
    cmeta_status target;
    cbind_status status;

    status = cbind_read_required(state, &token, shape, parent_field,
                                 current_depth);
    if (status != CBIND_OK)
        return status;
    if (token.kind != CSERDE_ARRAY_BEGIN)
        return cbind_variant_error(state->error, CBIND_TOKEN_MISMATCH,
                                   shape, parent_field, current_depth);

    status = cbind_read_required(state, &token, shape, parent_field,
                                 current_depth);
    if (status != CBIND_OK)
        return status;
    if (variant_shape->tag->kind == CMETA_DATA_ENUM) {
        status = cbind_enum_value_from_token(
            state, variant_shape->tag, parent_field, current_depth,
            &token, &tag);
    } else {
        status = cbind_variant_numeric_tag_from_token(
            state, variant_shape->tag, shape, parent_field, current_depth,
            &token, &tag);
    }
    if (status != CBIND_OK)
        return status;

    item = cmeta_data_variant_case_by_tag(variant_shape, tag);
    if (item == NULL)
        return cbind_variant_error(state->error, CBIND_VALUE_OUT_OF_RANGE,
                                   shape, parent_field, current_depth);

    target = cmeta_data_variant_select(shape, out, tag);
    if (target != CMETA_OK)
        return cbind_variant_target_error(state, target, shape, parent_field,
                                          current_depth);

    payload = (unsigned char *)out + item->offset;
    if (!cbind_variant_payload_is_zero(item->value, payload))
        return cbind_variant_target_error(state, CMETA_CALLBACK_ERROR,
                                          shape, parent_field, current_depth);
    if (item->value->kind == CMETA_DATA_STRUCT) {
        status = cbind_prepare_struct_containers(
            item->value, payload, state->error, current_depth);
        if (status != CBIND_OK)
            return status;
    }

    status = cbind_decode_value(state, item->value, parent_field, NULL,
                                current_depth, payload);
    if (status != CBIND_OK)
        return status;

    status = cbind_read_required(state, &token, shape, parent_field,
                                 current_depth);
    if (status != CBIND_OK)
        return status;
    return token.kind == CSERDE_ARRAY_END
               ? CBIND_OK
               : cbind_variant_error(state->error, CBIND_TOKEN_MISMATCH,
                                     shape, parent_field, current_depth);
}
