#include "internal.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef enum cbind_storage_match {
    CBIND_STORAGE_DIFFERENT = 0,
    CBIND_STORAGE_EXACT,
    CBIND_STORAGE_MALFORMED
} cbind_storage_match;

static cbind_storage_match cbind_storage_classify(
    const cmeta_type_desc *actual,
    const cmeta_type_desc *canonical) {
    if (actual == NULL || canonical == NULL ||
        !cmeta_type_equal(actual, canonical))
        return CBIND_STORAGE_DIFFERENT;
    if (actual->kind != canonical->kind ||
        actual->size != canonical->size ||
        actual->align != canonical->align)
        return CBIND_STORAGE_MALFORMED;
    return CBIND_STORAGE_EXACT;
}

static bool cbind_type_matches(const cmeta_type_desc *actual,
                               const cmeta_type_desc *canonical) {
    return cbind_storage_classify(actual, canonical) == CBIND_STORAGE_EXACT;
}

const cmeta_data_desc *cbind_scalar_shape_for_type(
    const cmeta_type_desc *type) {
    if (cbind_type_matches(type, &cmeta_type_bool))
        return &cmeta_data_bool;
    if (cbind_type_matches(type, &cmeta_type_int))
        return &cmeta_data_int;
    if (cbind_type_matches(type, &cmeta_type_long))
        return &cmeta_data_long;
    if (cbind_type_matches(type, &cmeta_type_size))
        return &cmeta_data_size;
    if (cbind_type_matches(type, &cmeta_type_float))
        return &cmeta_data_float;
    if (cbind_type_matches(type, &cmeta_type_double))
        return &cmeta_data_double;
    return NULL;
}

static cbind_status cbind_validation_error(cbind_error *error,
                                           cbind_status status,
                                           const cmeta_data_desc *shape,
                                           size_t depth) {
    cbind_error_set(error, status, CSERDE_OK, shape, NULL, depth);
    return status;
}

static size_t cbind_integer_alignment(cmeta_data_kind kind, uint8_t bits) {
    if (kind == CMETA_DATA_SINT) {
        switch (bits) {
            case 8u: return _Alignof(int8_t);
            case 16u: return _Alignof(int16_t);
            case 32u: return _Alignof(int32_t);
            case 64u: return _Alignof(int64_t);
            default: return 0u;
        }
    }

    switch (bits) {
        case 8u: return _Alignof(uint8_t);
        case 16u: return _Alignof(uint16_t);
        case 32u: return _Alignof(uint32_t);
        case 64u: return _Alignof(uint64_t);
        default: return 0u;
    }
}

static bool cbind_integer_storage_valid(const cmeta_data_desc *shape) {
    const cmeta_data_integer_shape *integer_shape =
        (const cmeta_data_integer_shape *)shape->shape;
    size_t expected_alignment;

    if (CHAR_BIT != 8 || integer_shape == NULL ||
        shape->storage_type == NULL ||
        shape->storage_type->kind != CMETA_T_INTEGER)
        return false;
    expected_alignment = cbind_integer_alignment(shape->kind,
                                                 integer_shape->bits);
    return expected_alignment != 0u &&
           shape->storage_type->size ==
               (size_t)integer_shape->bits / (size_t)CHAR_BIT &&
           shape->storage_type->align == expected_alignment;
}

static bool cbind_float_width_matches(const cmeta_data_desc *shape) {
    const cmeta_data_float_shape *float_shape =
        (const cmeta_data_float_shape *)shape->shape;

    return float_shape != NULL && shape->storage_type != NULL &&
           shape->storage_type->size <= SIZE_MAX / CHAR_BIT &&
           (size_t)float_shape->bits == shape->storage_type->size * CHAR_BIT;
}

static cbind_status cbind_validate_one_storage(
    const cmeta_data_desc *shape,
    const cmeta_type_desc *canonical,
    bool width_matches,
    size_t depth,
    cbind_error *error) {
    cbind_storage_match match =
        cbind_storage_classify(shape->storage_type, canonical);

    if (match == CBIND_STORAGE_DIFFERENT)
        return CBIND_UNSUPPORTED;
    if (match == CBIND_STORAGE_MALFORMED || !width_matches)
        return cbind_validation_error(error, CBIND_INVALID_SHAPE, shape, depth);
    return CBIND_OK;
}

cbind_status cbind_validate_graph(const cbind_context *context,
                                  const cmeta_data_desc *shape,
                                  size_t depth,
                                  const cbind_validation_frame *parent,
                                  size_t active_scratch,
                                  size_t *max_scratch,
                                  cbind_error *error) {
    cbind_status status;

    if (shape == NULL || !cmeta_data_desc_valid(shape))
        return cbind_validation_error(error, CBIND_INVALID_SHAPE, shape, depth);

    switch (shape->kind) {
        case CMETA_DATA_BOOL:
            status = cbind_validate_one_storage(shape, &cmeta_type_bool,
                                                true, depth, error);
            return status == CBIND_UNSUPPORTED
                       ? cbind_validation_error(error, CBIND_UNSUPPORTED,
                                                shape, depth)
                       : status;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            return cbind_integer_storage_valid(shape)
                       ? CBIND_OK
                       : cbind_validation_error(error, CBIND_INVALID_SHAPE,
                                                shape, depth);
        case CMETA_DATA_FLOAT:
            status = cbind_validate_one_storage(shape, &cmeta_type_float,
                                                cbind_float_width_matches(shape),
                                                depth, error);
            if (status != CBIND_UNSUPPORTED)
                return status;
            status = cbind_validate_one_storage(shape, &cmeta_type_double,
                                                cbind_float_width_matches(shape),
                                                depth, error);
            return status == CBIND_UNSUPPORTED
                       ? cbind_validation_error(error, CBIND_UNSUPPORTED,
                                                shape, depth)
                       : status;
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            return cbind_validate_buffer(context, shape, NULL, depth, error);
        case CMETA_DATA_ENUM:
            if (shape->struct_size <
                    CBIND_FIELD_END(cmeta_data_desc, enum_ops) ||
                shape->enum_ops == NULL)
                return cbind_validation_error(error, CBIND_UNSUPPORTED,
                                              shape, depth);
            return cmeta_data_enum_ops_of(shape) != NULL
                       ? CBIND_OK
                       : cbind_validation_error(error, CBIND_INVALID_SHAPE,
                                                shape, depth);
        case CMETA_DATA_STRUCT:
            return cbind_validation_error(error, CBIND_UNSUPPORTED, shape, depth);
        case CMETA_DATA_VARIANT:
            return cbind_validate_variant_graph(
                context, shape, depth, parent, active_scratch,
                max_scratch, error);
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
        case CMETA_DATA_SINT: {
            const cmeta_data_integer_shape *integer_shape =
                (const cmeta_data_integer_shape *)shape->shape;
            switch (integer_shape->bits) {
                case 8u: {
                    int8_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0;
                }
                case 16u: {
                    int16_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0;
                }
                case 32u: {
                    int32_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0;
                }
                case 64u: {
                    int64_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0;
                }
                default: return false;
            }
        }
        case CMETA_DATA_UINT: {
            const cmeta_data_integer_shape *integer_shape =
                (const cmeta_data_integer_shape *)shape->shape;
            switch (integer_shape->bits) {
                case 8u: {
                    uint8_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0u;
                }
                case 16u: {
                    uint16_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0u;
                }
                case 32u: {
                    uint32_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0u;
                }
                case 64u: {
                    uint64_t native;
                    memcpy(&native, value, sizeof(native));
                    return native == 0u;
                }
                default: return false;
            }
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
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES: {
            bool empty = false;
            return cmeta_data_buffer_is_zero(shape, value, &empty) == CMETA_OK &&
                   empty;
        }
        case CMETA_DATA_ENUM: {
            bool empty = false;
            return cmeta_data_enum_is_zero(shape, value, &empty) == CMETA_OK &&
                   empty;
        }
        case CMETA_DATA_VARIANT: {
            bool empty = false;
            return cmeta_data_variant_is_zero(shape, value, &empty) ==
                       CMETA_OK && empty;
        }
        default:
            return false;
    }
}

static void cbind_store_signed(uint8_t bits, int64_t value, void *out);
static void cbind_store_unsigned(uint8_t bits, uint64_t value, void *out);

void cbind_value_reset(const cmeta_data_desc *shape, void *value) {
    if (shape == NULL || value == NULL)
        return;

    switch (shape->kind) {
        case CMETA_DATA_BOOL: {
            bool native = false;
            memcpy(value, &native, sizeof(native));
            break;
        }
        case CMETA_DATA_SINT: {
            const cmeta_data_integer_shape *integer_shape =
                (const cmeta_data_integer_shape *)shape->shape;
            cbind_store_signed(integer_shape->bits, INT64_C(0), value);
            break;
        }
        case CMETA_DATA_UINT: {
            const cmeta_data_integer_shape *integer_shape =
                (const cmeta_data_integer_shape *)shape->shape;
            cbind_store_unsigned(integer_shape->bits, UINT64_C(0), value);
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
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            (void)cmeta_data_buffer_restore_zero(shape, value);
            break;
        case CMETA_DATA_ENUM:
            (void)cmeta_data_enum_restore_zero(shape, value);
            break;
        case CMETA_DATA_VARIANT:
            (void)cmeta_data_variant_restore_zero(shape, value);
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

static bool cbind_slice_equal_text(const cserde_slice *slice,
                                   const char *text) {
    size_t text_size;

    if (slice == NULL || text == NULL)
        return false;
    text_size = strlen(text);
    return slice->size == text_size &&
           (text_size == 0u || memcmp(slice->data, text, text_size) == 0);
}

cbind_status cbind_enum_value_from_token(
    cbind_decode_state *state, const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field, size_t depth,
    const cserde_token *token, int64_t *out) {
    const cmeta_data_enum_shape *enum_shape;
    int64_t value;
    size_t i;

    if (token == NULL || out == NULL)
        return cbind_scalar_error(state, CBIND_INVALID_ARGUMENT,
                                  shape, field, depth);
    enum_shape = (const cmeta_data_enum_shape *)shape->shape;

    if (token->kind == CSERDE_STRING) {
        for (i = 0u; i < enum_shape->meta->count; ++i) {
            const cmeta_enum_item_desc *item = &enum_shape->meta->items[i];
            if (cbind_slice_equal_text(&token->value.slice, item->text) ||
                cbind_slice_equal_text(&token->value.slice, item->symbol)) {
                *out = item->value;
                return CBIND_OK;
            }
        }
        return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                  shape, field, depth);
    }

    if (token->kind == CSERDE_SINT) {
        value = token->value.sint;
    } else if (token->kind == CSERDE_UINT) {
        if (token->value.uint > (uint64_t)INT64_MAX)
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = (int64_t)token->value.uint;
    } else {
        return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                  shape, field, depth);
    }

    if (cmeta_enum_item_by_value(enum_shape->meta, value) == NULL)
        return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                  shape, field, depth);
    *out = value;
    return CBIND_OK;
}

static cbind_status cbind_decode_enum(cbind_decode_state *state,
                                      const cmeta_data_desc *shape,
                                      const cmeta_data_field_desc *field,
                                      size_t depth,
                                      const cserde_token *token,
                                      void *out) {
    int64_t value = 0;
    cmeta_status target;
    cbind_status status = cbind_enum_value_from_token(
        state, shape, field, depth, token, &value);

    if (status != CBIND_OK)
        return status;
    target = cmeta_data_enum_assign(shape, out, value);
    if (target != CMETA_OK) {
        cbind_error_set(state->error, CBIND_TARGET_ERROR, CSERDE_OK,
                        shape, field, depth);
        cbind_error_set_target(state->error, target);
        return CBIND_TARGET_ERROR;
    }
    return CBIND_OK;
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

static void cbind_store_signed(uint8_t bits, int64_t value, void *out) {
    switch (bits) {
        case 8u: {
            int8_t native = (int8_t)value;
            memcpy(out, &native, sizeof(native));
            break;
        }
        case 16u: {
            int16_t native = (int16_t)value;
            memcpy(out, &native, sizeof(native));
            break;
        }
        case 32u: {
            int32_t native = (int32_t)value;
            memcpy(out, &native, sizeof(native));
            break;
        }
        case 64u: {
            int64_t native = value;
            memcpy(out, &native, sizeof(native));
            break;
        }
    }
}

static void cbind_store_unsigned(uint8_t bits, uint64_t value, void *out) {
    switch (bits) {
        case 8u: {
            uint8_t native = (uint8_t)value;
            memcpy(out, &native, sizeof(native));
            break;
        }
        case 16u: {
            uint16_t native = (uint16_t)value;
            memcpy(out, &native, sizeof(native));
            break;
        }
        case 32u: {
            uint32_t native = (uint32_t)value;
            memcpy(out, &native, sizeof(native));
            break;
        }
        case 64u: {
            uint64_t native = value;
            memcpy(out, &native, sizeof(native));
            break;
        }
    }
}

static int64_t cbind_signed_min(uint8_t bits) {
    return bits == 64u ? INT64_MIN
                       : -(INT64_C(1) << (bits - 1u));
}

static int64_t cbind_signed_max(uint8_t bits) {
    return bits == 64u ? INT64_MAX
                       : (INT64_C(1) << (bits - 1u)) - INT64_C(1);
}

static uint64_t cbind_unsigned_max(uint8_t bits) {
    return bits == 64u ? UINT64_MAX
                       : (UINT64_C(1) << bits) - UINT64_C(1);
}

static cbind_status cbind_decode_signed(cbind_decode_state *state,
                                        const cmeta_data_desc *shape,
                                        const cmeta_data_field_desc *field,
                                        size_t depth,
                                        const cserde_token *token,
                                        void *out) {
    const cmeta_data_integer_shape *integer_shape =
        (const cmeta_data_integer_shape *)shape->shape;
    uint8_t bits = integer_shape->bits;
    int64_t value;

    if (token->kind == CSERDE_SINT) {
        if (token->value.sint < cbind_signed_min(bits) ||
            token->value.sint > cbind_signed_max(bits))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = token->value.sint;
    } else if (token->kind == CSERDE_UINT) {
        if (token->value.uint > (uint64_t)cbind_signed_max(bits))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = (int64_t)token->value.uint;
    } else if (token->kind == CSERDE_FLOAT) {
        if (!cbind_double_in_signed_width(token->value.floating, bits))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = (int64_t)token->value.floating;
    } else {
        return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                  shape, field, depth);
    }

    cbind_store_signed(bits, value, out);
    return CBIND_OK;
}

static cbind_status cbind_decode_unsigned(cbind_decode_state *state,
                                          const cmeta_data_desc *shape,
                                          const cmeta_data_field_desc *field,
                                          size_t depth,
                                          const cserde_token *token,
                                          void *out) {
    const cmeta_data_integer_shape *integer_shape =
        (const cmeta_data_integer_shape *)shape->shape;
    uint8_t bits = integer_shape->bits;
    uint64_t value;

    if (token->kind == CSERDE_UINT) {
        if (token->value.uint > cbind_unsigned_max(bits))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = token->value.uint;
    } else if (token->kind == CSERDE_SINT) {
        if (token->value.sint < 0 ||
            (uint64_t)token->value.sint > cbind_unsigned_max(bits))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = (uint64_t)token->value.sint;
    } else if (token->kind == CSERDE_FLOAT) {
        if (!cbind_double_in_unsigned_width(token->value.floating, bits))
            return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                      shape, field, depth);
        value = (uint64_t)token->value.floating;
    } else {
        return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                  shape, field, depth);
    }

    cbind_store_unsigned(bits, value, out);
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
            if (isfinite(token->value.floating) &&
                fabs(token->value.floating) > (double)FLT_MAX)
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
            native = (float)token->value.floating;
            if (isfinite(token->value.floating) &&
                token->value.floating != 0.0 && native == 0.0f)
                return cbind_scalar_error(state, CBIND_VALUE_OUT_OF_RANGE,
                                          shape, field, depth);
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

cbind_status cbind_decode_scalar_token(cbind_decode_state *state,
                                       const cmeta_data_desc *shape,
                                       const cmeta_data_field_desc *field,
                                       size_t depth,
                                       const cserde_token *token,
                                       void *out) {
    if (token == NULL)
        return cbind_scalar_error(state, CBIND_INVALID_ARGUMENT,
                                  shape, field, depth);
    switch (shape->kind) {
        case CMETA_DATA_BOOL:
            if (token->kind != CSERDE_BOOL)
                return cbind_scalar_error(state, CBIND_TOKEN_MISMATCH,
                                          shape, field, depth);
            memcpy(out, &token->value.boolean, sizeof(token->value.boolean));
            return CBIND_OK;
        case CMETA_DATA_SINT:
            return cbind_decode_signed(state, shape, field, depth, token, out);
        case CMETA_DATA_UINT:
            return cbind_decode_unsigned(state, shape, field, depth, token, out);
        case CMETA_DATA_FLOAT:
            return cbind_decode_floating(state, shape, field, depth, token, out);
        case CMETA_DATA_ENUM:
            return cbind_decode_enum(state, shape, field, depth, token, out);
        default:
            return cbind_scalar_error(state, CBIND_UNSUPPORTED,
                                      shape, field, depth);
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
    return cbind_decode_scalar_token(state, shape, field, depth, &token, out);
}
