#include "internal.h"

bool cbind_context_valid(const cbind_context *context) {
    return context != NULL &&
           context->struct_size >= CBIND_FIELD_END(cbind_context, max_depth) &&
           context->abi_version == CBIND_CONTEXT_ABI_VERSION &&
           (context->scratch_size == 0u || context->scratch != NULL);
}

bool cbind_error_valid(const cbind_error *error) {
    return error != NULL &&
           error->struct_size >= CBIND_FIELD_END(cbind_error, depth) &&
           error->abi_version == CBIND_ERROR_ABI_VERSION;
}

void cbind_error_clear(cbind_error *error) {
    if (error == NULL)
        return;
    error->status = CBIND_OK;
    error->source_status = CSERDE_OK;
    error->shape = NULL;
    error->field = NULL;
    error->depth = 0u;
    if (error->struct_size >= CBIND_FIELD_END(cbind_error, target_status))
        error->target_status = CMETA_OK;
}

void cbind_error_set(cbind_error *error,
                     cbind_status status,
                     cserde_status source_status,
                     const cmeta_data_desc *shape,
                     const cmeta_data_field_desc *field,
                     size_t depth) {
    if (error == NULL)
        return;
    error->status = status;
    error->source_status = source_status;
    error->shape = shape;
    error->field = field;
    error->depth = depth;
    if (error->struct_size >= CBIND_FIELD_END(cbind_error, target_status))
        error->target_status = CMETA_OK;
}

void cbind_error_set_target(cbind_error *error, cmeta_status target_status) {
    if (error != NULL &&
        error->struct_size >= CBIND_FIELD_END(cbind_error, target_status))
        error->target_status = target_status;
}

cbind_status cbind_read_required(cbind_decode_state *state,
                                 cserde_token *token,
                                 const cmeta_data_desc *shape,
                                 const cmeta_data_field_desc *field,
                                 size_t depth) {
    cserde_status source = cserde_reader_next(state->reader, token);

    if (source == CSERDE_OK)
        return CBIND_OK;
    if (source == CSERDE_DONE) {
        cbind_error_set(state->error, CBIND_UNEXPECTED_END, CSERDE_DONE,
                        shape, field, depth);
        return CBIND_UNEXPECTED_END;
    }
    cbind_error_set(state->error, CBIND_SOURCE_ERROR, source,
                    shape, field, depth);
    return CBIND_SOURCE_ERROR;
}

cbind_status cbind_decode_value(cbind_decode_state *state,
                                const cmeta_data_desc *shape,
                                const cmeta_data_field_desc *field,
                                const cmeta_field_desc *reflected,
                                size_t depth,
                                void *out) {
    if (shape->kind == CMETA_DATA_STRUCT)
        return cbind_decode_struct(state, shape, field, depth, out);
    if (cbind_data_kind_is_container(shape->kind))
        return cbind_decode_container(state, shape, field, reflected,
                                      depth, out);
    if (cbind_data_kind_is_buffer(shape->kind))
        return cbind_decode_buffer(state, shape, field, depth, out);
    return cbind_decode_scalar(state, shape, field, depth, out);
}

cbind_status cbind_decode(const cbind_context *context,
                          const cmeta_data_desc *shape,
                          cserde_reader *reader,
                          void *out,
                          cbind_error *error) {
    cbind_decode_state state;
    size_t max_scratch = 0u;
    cbind_status status;
    bool destination_empty;

    if (shape == NULL || reader == NULL || out == NULL)
        return CBIND_INVALID_ARGUMENT;
    if (error != NULL && !cbind_error_valid(error))
        return CBIND_INVALID_ARGUMENT;
    if (!cbind_context_valid(context)) {
        cbind_error_set(error, CBIND_INVALID_CONTEXT, CSERDE_OK,
                        shape, NULL, 0u);
        return CBIND_INVALID_CONTEXT;
    }

    if (shape->kind == CMETA_DATA_STRUCT)
        status = cbind_validate_struct_graph(context, shape, 0u, NULL, 0u,
                                             &max_scratch, error);
    else
        status = cbind_validate_graph(context, shape, 0u, NULL, 0u,
                                      &max_scratch, error);
    if (status != CBIND_OK)
        return status;
    if (max_scratch > context->scratch_size) {
        cbind_error_set(error, CBIND_LIMIT_EXCEEDED, CSERDE_OK,
                        shape, NULL, 0u);
        return CBIND_LIMIT_EXCEEDED;
    }

    destination_empty = shape->kind == CMETA_DATA_STRUCT
                            ? cbind_struct_is_empty(shape, out)
                            : cbind_value_is_empty(shape, out);
    if (!destination_empty) {
        cbind_error_set(error, CBIND_DESTINATION_NOT_EMPTY, CSERDE_OK,
                        shape, NULL, 0u);
        return CBIND_DESTINATION_NOT_EMPTY;
    }

    state.context = context;
    state.reader = reader;
    state.error = error;
    state.scratch = (unsigned char *)context->scratch;
    state.scratch_used = 0u;

    if (shape->kind == CMETA_DATA_STRUCT) {
        status = cbind_prepare_struct_containers(shape, out, error, 0u);
        if (status != CBIND_OK) {
            cbind_struct_reset(shape, out);
            return status;
        }
    }

    status = cbind_decode_value(&state, shape, NULL, NULL, 0u, out);
    if (status != CBIND_OK) {
        if (shape->kind == CMETA_DATA_STRUCT)
            cbind_struct_reset(shape, out);
        else
            cbind_value_reset(shape, out);
        return status;
    }

    cbind_error_clear(error);
    return CBIND_OK;
}
