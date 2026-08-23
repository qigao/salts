#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static bool cbind_type_matches(const cmeta_type_desc *actual,
                               const cmeta_type_desc *canonical) {
    return actual != NULL && canonical != NULL &&
           cmeta_type_equal(actual, canonical) &&
           actual->size == canonical->size &&
           actual->align == canonical->align;
}

static cbind_status cbind_validation_error(cbind_error *error,
                                           cbind_status status,
                                           const cmeta_data_desc *shape,
                                           size_t depth) {
    cbind_error_set(error, status, CSERDE_OK, shape, NULL, depth);
    return status;
}

static bool cbind_integer_width_matches(const cmeta_data_desc *shape) {
    const cmeta_data_integer_shape *integer_shape =
        (const cmeta_data_integer_shape *)shape->shape;
    return integer_shape != NULL &&
           (size_t)integer_shape->bits == shape->storage_type->size * CHAR_BIT;
}

static bool cbind_float_width_matches(const cmeta_data_desc *shape) {
    const cmeta_data_float_shape *float_shape =
        (const cmeta_data_float_shape *)shape->shape;
    return float_shape != NULL &&
           (size_t)float_shape->bits == shape->storage_type->size * CHAR_BIT;
}

cbind_status cbind_validate_graph(const cbind_context *context,
                                  const cmeta_data_desc *shape,
                                  size_t depth,
                                  const cbind_validation_frame *parent,
                                  size_t active_scratch,
                                  size_t *max_scratch,
                                  cbind_error *error) {
    (void)context;
    (void)parent;
    (void)active_scratch;
    (void)max_scratch;

    if (shape == NULL || !cmeta_data_desc_valid(shape))
        return cbind_validation_error(error, CBIND_INVALID_SHAPE, shape, depth);

    switch (shape->kind) {
        case CMETA_DATA_BOOL:
            return cbind_type_matches(shape->storage_type, &cmeta_type_bool)
                       ? CBIND_OK
                       : cbind_validation_error(error, CBIND_UNSUPPORTED,
                                                shape, depth);
        case CMETA_DATA_SINT:
            if (!cbind_type_matches(shape->storage_type, &cmeta_type_int) &&
                !cbind_type_matches(shape->storage_type, &cmeta_type_long))
                return cbind_validation_error(error, CBIND_UNSUPPORTED,
                                              shape, depth);
            return cbind_integer_width_matches(shape)
                       ? CBIND_OK
                       : cbind_validation_error(error, CBIND_INVALID_SHAPE,
                                                shape, depth);
        case CMETA_DATA_UINT:
            if (!cbind_type_matches(shape->storage_type, &cmeta_type_size))
                return cbind_validation_error(error, CBIND_UNSUPPORTED,
                                              shape, depth);
            return cbind_integer_width_matches(shape)
                       ? CBIND_OK
                       : cbind_validation_error(error, CBIND_INVALID_SHAPE,
                                                shape, depth);
        case CMETA_DATA_FLOAT:
            if (!cbind_type_matches(shape->storage_type, &cmeta_type_float) &&
                !cbind_type_matches(shape->storage_type, &cmeta_type_double))
                return cbind_validation_error(error, CBIND_UNSUPPORTED,
                                              shape, depth);
            return cbind_float_width_matches(shape)
                       ? CBIND_OK
                       : cbind_validation_error(error, CBIND_INVALID_SHAPE,
                                                shape, depth);
        case CMETA_DATA_STRUCT:
            return cbind_validation_error(error, CBIND_UNSUPPORTED, shape, depth);
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
        case CMETA_DATA_ENUM:
        case CMETA_DATA_VARIANT:
        case CMETA_DATA_SEQUENCE:
        case CMETA_DATA_SET:
        case CMETA_DATA_MAP:
        case CMETA_DATA_CUSTOM:
            return cbind_validation_error(error, CBIND_UNSUPPORTED, shape, depth);
    }
    return cbind_validation_error(error, CBIND_INVALID_SHAPE, shape, depth);
}

bool cbind_value_is_empty(const cmeta_data_desc *shape, const void *value) {
    if (shape == NULL || value == NULL)
        return false;

    switch (shape->kind) {
        case CMETA_DATA_BOOL: {
            bool native;
            memcpy(&native, value, sizeof(native));
            return !native;
        }
        case CMETA_DATA_SINT:
            if (cbind_type_matches(shape->storage_type, &cmeta_type_int)) {
                int native;
                memcpy(&native, value, sizeof(native));
                return native == 0;
            } else {
                long native;
                memcpy(&native, value, sizeof(native));
                return native == 0;
            }
        case CMETA_DATA_UINT: {
            size_t native;
            memcpy(&native, value, sizeof(native));
            return native == 0u;
        }
        case CMETA_DATA_FLOAT:
            if (cbind_type_matches(shape->storage_type, &cmeta_type_float)) {
                float native;
                memcpy(&native, value, sizeof(native));
                return native == 0.0f;
            } else {
                double native;
                memcpy(&native, value, sizeof(native));
                return native == 0.0;
            }
        default:
            return false;
    }
}

void cbind_value_reset(const cmeta_data_desc *shape, void *value) {
    if (shape == NULL || value == NULL)
        return;

    switch (shape->kind) {
        case CMETA_DATA_BOOL: {
            bool native = false;
            memcpy(value, &native, sizeof(native));
            break;
        }
        case CMETA_DATA_SINT:
            if (cbind_type_matches(shape->storage_type, &cmeta_type_int)) {
                int native = 0;
                memcpy(value, &native, sizeof(native));
            } else {
                long native = 0;
                memcpy(value, &native, sizeof(native));
            }
            break;
        case CMETA_DATA_UINT: {
            size_t native = 0u;
            memcpy(value, &native, sizeof(native));
            break;
        }
        case CMETA_DATA_FLOAT:
            if (cbind_type_matches(shape->storage_type, &cmeta_type_float)) {
                float native = 0.0f;
                memcpy(value, &native, sizeof(native));
            } else {
                double native = 0.0;
                memcpy(value, &native, sizeof(native));
            }
            break;
        default:
            break;
    }
}

static cbind_status cbind_scalar_error(cbind_decode_state *state,
                                       cbind_status status,
                                       const cmeta_data_desc *shape,
                                       const cmeta_data_field_desc *field,
                                       size_t depth) {
    cbind_error_set(state->error, status, CSERDE_OK, shape, field, depth);
    return status;
}

static cbind_status cbind_decode_signed(cbind_decode_state *state,
                                        const cmeta_data_desc *shape,
                                        const cmeta_data_field_desc *field,
                                        size_t depth,
                                        const cserde_token *token,
                                        void *out) {
    if (cbind_type_matches(shape->storage_type, &cmeta_type_int)) {
        int native;
        if (token->kind == CSERDE_SINT) {
            if (token->value.sint < (int64_t)INT_MIN ||
                token->value.sint > (int64_t)INT_MAX)
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (int)token->value.sint;
        } else if (token->kind == CSERDE_UINT) {
            if (token->value.uint > (uint64_t)INT_MAX)
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (int)token->value.uint;
        } else {
            return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                      shape, field, depth);
        }
        memcpy(out, &native, sizeof(native));
        return CBIND_OK;
    }

    {
        long native;
        if (token->kind == CSERDE_SINT) {
            if (token->value.sint < (int64_t)LONG_MIN ||
                token->value.sint > (int64_t)LONG_MAX)
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (long)token->value.sint;
        } else if (token->kind == CSERDE_UINT) {
            if (token->value.uint > (uint64_t)LONG_MAX)
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (long)token->value.uint;
        } else {
            return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                      shape, field, depth);
        }
        memcpy(out, &native, sizeof(native));
        return CBIND_OK;
    }
}

static cbind_status cbind_decode_unsigned(cbind_decode_state *state,
                                          const cmeta_data_desc *shape,
                                          const cmeta_data_field_desc *field,
                                          size_t depth,
                                          const cserde_token *token,
                                          void *out) {
    size_t native;

    if (token->kind == CSERDE_UINT) {
        if (token->value.uint > (uint64_t)SIZE_MAX)
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        native = (size_t)token->value.uint;
    } else if (token->kind == CSERDE_SINT) {
        if (token->value.sint < 0 ||
            (uint64_t)token->value.sint > (uint64_t)SIZE_MAX)
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        native = (size_t)token->value.sint;
    } else {
        return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                  shape, field, depth);
    }

    memcpy(out, &native, sizeof(native));
    return CBIND_OK;
}

cbind_status cbind_decode_scalar(cbind_decode_state *state,
                                 const cmeta_data_desc *shape,
                                 const cmeta_data_field_desc *field,
                                 size_t depth,
                                 void *out) {
    cserde_token token = {0};
    cbind_status status = cbind_read_required(state, &token, shape, field, depth);

    if (status != CBIND_OK)
        return status;

    switch (shape->kind) {
        case CMETA_DATA_BOOL:
            if (token.kind != CSERDE_BOOL)
                return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                          shape, field, depth);
            memcpy(out, &token.value.boolean, sizeof(token.value.boolean));
            return CBIND_OK;
        case CMETA_DATA_SINT:
            return cbind_decode_signed(state, shape, field, depth, &token, out);
        case CMETA_DATA_UINT:
            return cbind_decode_unsigned(state, shape, field, depth, &token, out);
        case CMETA_DATA_FLOAT:
            return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                      shape, field, depth);
        default:
            return cbind_scalar_error(state, CBIND_UNSUPPORTED,
                                      shape, field, depth);
    }
}
