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
}
