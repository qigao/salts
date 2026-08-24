#include "internal.h"

bool cbind_data_kind_is_buffer(cmeta_data_kind kind) {
    return kind == CMETA_DATA_STRING || kind == CMETA_DATA_BYTES;
}

static cbind_status cbind_buffer_error(
    cbind_error *error, cbind_status status, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth) {
    cbind_error_set(error, status, CSERDE_OK, shape, field, depth);
    return status;
}

static cbind_status cbind_buffer_target_error(
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

cbind_status cbind_validate_buffer(
    const cbind_context *context, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth, cbind_error *error) {
    const cmeta_data_buffer_shape *buffer_shape;

    if (shape == NULL || !cmeta_data_desc_valid(shape) ||
        !cbind_data_kind_is_buffer(shape->kind))
        return cbind_buffer_error(error, CBIND_INVALID_SHAPE, shape, field,
                                  depth);
    if (shape->struct_size < CBIND_FIELD_END(cmeta_data_desc, buffer_ops) ||
        shape->buffer_ops == NULL)
        return cbind_buffer_error(error, CBIND_UNSUPPORTED, shape, field,
                                  depth);

    buffer_shape = (const cmeta_data_buffer_shape *)shape->shape;
    if (buffer_shape->ownership == CMETA_DATA_BUFFER_CUSTOM)
        return cbind_buffer_error(error, CBIND_UNSUPPORTED, shape, field,
                                  depth);
    if (cmeta_data_buffer_ops_of(shape) == NULL)
        return cbind_buffer_error(error, CBIND_INVALID_SHAPE, shape, field,
                                  depth);
    if (context == NULL ||
        context->struct_size < CBIND_FIELD_END(cbind_context,
                                               max_buffer_bytes))
        return cbind_buffer_error(error, CBIND_INVALID_CONTEXT, shape, field,
                                  depth);
    return CBIND_OK;
}

cbind_status cbind_decode_buffer(
    cbind_decode_state *state, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth, void *out) {
    const cmeta_data_buffer_shape *buffer_shape =
        (const cmeta_data_buffer_shape *)shape->shape;
    cserde_token token = {0};
    cserde_token_kind expected = shape->kind == CMETA_DATA_STRING
                                     ? CSERDE_STRING
                                     : CSERDE_BYTES;
    cmeta_status target;
    cbind_status status;

    status = cbind_read_required(state, &token, shape, field, depth);
    if (status != CBIND_OK)
        return status;
    if (token.kind != expected)
        return cbind_buffer_error(state->error, CBIND_TOKEN_MISMATCH,
                                  shape, field, depth);
    if (buffer_shape->ownership == CMETA_DATA_BUFFER_BORROWED &&
        token.value.slice.lifetime != CSERDE_VIEW_STABLE)
        return cbind_buffer_error(state->error, CBIND_UNSUPPORTED,
                                  shape, field, depth);

    target = cmeta_data_buffer_assign(shape, out, token.value.slice.data,
                                      token.value.slice.size,
                                      state->context->max_buffer_bytes);
    return target == CMETA_OK
               ? CBIND_OK
               : cbind_buffer_target_error(state, target, shape, field, depth);
}
