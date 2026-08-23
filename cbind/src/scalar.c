#include "internal.h"

#include <float.h>
#include <limits.h>
#include <math.h>
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

static bool cbind_double_is_integral(double value) {
    return isfinite(value) && trunc(value) == value;
}

static bool cbind_double_in_signed_width(double value, size_t bits) {
    double limit;

    if (!cbind_double_is_integral(value) || bits == 0u)
        return false;
    limit = ldexp(1.0, (int)(bits - 1u));
    return value >= -limit && value < limit;
}

static bool cbind_double_in_unsigned_width(double value, size_t bits) {
    double limit;

    if (!cbind_double_is_integral(value) || value < 0.0 || bits == 0u)
        return false;
    limit = ldexp(1.0, (int)bits);
    return value < limit;
}

static uint64_t cbind_signed_magnitude(int64_t value) {
    if (value >= 0)
        return (uint64_t)value;
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static unsigned cbind_u64_bit_length(uint64_t value) {
    unsigned bits = 0u;

    while (value != 0u) {
        ++bits;
        value >>= 1u;
    }
    return bits;
}

static bool cbind_u64_exact_in_binary(uint64_t value, unsigned precision) {
    unsigned bits = cbind_u64_bit_length(value);
    unsigned shift;
    uint64_t mask;

    if (bits <= precision)
        return true;
    shift = bits - precision;
    if (shift >= 64u)
        return false;
    mask = (UINT64_C(1) << shift) - UINT64_C(1);
    return (value & mask) == 0u;
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
        } else if (token->kind == CSERDE_FLOAT) {
            if (!cbind_double_in_signed_width(token->value.floating,
                                              sizeof(native) * CHAR_BIT))
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (int)token->value.floating;
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
        } else if (token->kind == CSERDE_FLOAT) {
            if (!cbind_double_in_signed_width(token->value.floating,
                                              sizeof(native) * CHAR_BIT))
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (long)token->value.floating;
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
    } else if (token->kind == CSERDE_FLOAT) {
        if (!cbind_double_in_unsigned_width(token->value.floating,
                                            sizeof(native) * CHAR_BIT))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        native = (size_t)token->value.floating;
    } else {
        return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                  shape, field, depth);
    }

    memcpy(out, &native, sizeof(native));
    return CBIND_OK;
}

static cbind_status cbind_decode_floating(cbind_decode_state *state,
                                          const cmeta_data_desc *shape,
                                          const cmeta_data_field_desc *field,
                                          size_t depth,
                                          const cserde_token *token,
                                          void *out) {
    if (cbind_type_matches(shape->storage_type, &cmeta_type_double)) {
        double native;

        if (token->kind == CSERDE_FLOAT) {
            native = token->value.floating;
        } else if (token->kind == CSERDE_SINT) {
            if (!cbind_u64_exact_in_binary(
                    cbind_signed_magnitude(token->value.sint), DBL_MANT_DIG))
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (double)token->value.sint;
        } else if (token->kind == CSERDE_UINT) {
            if (!cbind_u64_exact_in_binary(token->value.uint, DBL_MANT_DIG))
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (double)token->value.uint;
        } else {
            return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                      shape, field, depth);
        }
        memcpy(out, &native, sizeof(native));
        return CBIND_OK;
    }

    {
        float native;

        if (token->kind == CSERDE_FLOAT) {
            native = (float)token->value.floating;
            if (isfinite(token->value.floating)) {
                if (isinf(native) ||
                    (token->value.floating != 0.0 && native == 0.0f))
                    return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                              shape, field, depth);
            }
        } else if (token->kind == CSERDE_SINT) {
            if (!cbind_u64_exact_in_binary(
                    cbind_signed_magnitude(token->value.sint), FLT_MANT_DIG))
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (float)token->value.sint;
        } else if (token->kind == CSERDE_UINT) {
            if (!cbind_u64_exact_in_binary(token->value.uint, FLT_MANT_DIG))
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (float)token->value.uint;
        } else {
            return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                      shape, field, depth);
        }
        memcpy(out, &native, sizeof(native));
        return CBIND_OK;
    }
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
            return cbind_decode_floating(state, shape, field, depth, &token, out);
        default:
            return cbind_scalar_error(state, CBIND_UNSUPPORTED,
                                      shape, field, depth);
    }
}
