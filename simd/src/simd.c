#include <salts/simd.h>

#include <simde/wasm/simd128.h>

#include <string.h>

static simde_v128_t salts_simd_load_value(const salts_v128 *value) {
    return simde_wasm_v128_load(value->bytes);
}

static void salts_simd_store_value(salts_v128 *out, simde_v128_t value) {
    simde_wasm_v128_store(out->bytes, value);
}

static bool salts_simd_desc_valid(const cmeta_vector_desc *desc) {
    return desc != NULL && cmeta_vector_desc_valid(desc);
}

void salts_simd_v128_load(salts_v128 *out, const void *source) {
    if (out == NULL || source == NULL) return;
    salts_simd_store_value(out, simde_wasm_v128_load(source));
}

void salts_simd_v128_store(void *destination, const salts_v128 *value) {
    if (destination == NULL || value == NULL) return;
    simde_wasm_v128_store(destination, salts_simd_load_value(value));
}

bool salts_simd_splat(const cmeta_vector_desc *desc,
                      salts_v128 *out,
                      salts_simd_scalar scalar) {
    simde_v128_t result;

    if (!salts_simd_desc_valid(desc) || desc->is_mask || out == NULL)
        return false;

    switch (desc->lane_kind) {
        case CMETA_VECTOR_I8:
            result = simde_wasm_i8x16_splat(scalar.i8);
            break;
        case CMETA_VECTOR_U8: {
            int8_t value;
            memcpy(&value, &scalar.u8, sizeof(value));
            result = simde_wasm_i8x16_splat(value);
            break;
        }
        case CMETA_VECTOR_I16:
            result = simde_wasm_i16x8_splat(scalar.i16);
            break;
        case CMETA_VECTOR_U16: {
            int16_t value;
            memcpy(&value, &scalar.u16, sizeof(value));
            result = simde_wasm_i16x8_splat(value);
            break;
        }
        case CMETA_VECTOR_I32:
            result = simde_wasm_i32x4_splat(scalar.i32);
            break;
        case CMETA_VECTOR_U32: {
            int32_t value;
            memcpy(&value, &scalar.u32, sizeof(value));
            result = simde_wasm_i32x4_splat(value);
            break;
        }
        case CMETA_VECTOR_I64:
            result = simde_wasm_i64x2_splat(scalar.i64);
            break;
        case CMETA_VECTOR_U64: {
            int64_t value;
            memcpy(&value, &scalar.u64, sizeof(value));
            result = simde_wasm_i64x2_splat(value);
            break;
        }
        case CMETA_VECTOR_F32:
            result = simde_wasm_f32x4_splat(scalar.f32);
            break;
        case CMETA_VECTOR_F64:
            result = simde_wasm_f64x2_splat(scalar.f64);
            break;
        case CMETA_VECTOR_BOOL:
        default:
            return false;
    }

    salts_simd_store_value(out, result);
    return true;
}

bool salts_simd_unary(const cmeta_vector_desc *desc,
                      salts_simd_unary_op op,
                      salts_v128 *out,
                      const salts_v128 *value) {
    simde_v128_t input;

    if (!salts_simd_desc_valid(desc) || out == NULL || value == NULL)
        return false;

    input = salts_simd_load_value(value);

    switch (op) {
        case SALTS_SIMD_UNARY_NOT:
            salts_simd_store_value(out, simde_wasm_v128_not(input));
            return true;
        case SALTS_SIMD_UNARY_ABS:
        case SALTS_SIMD_UNARY_NEG:
        case SALTS_SIMD_UNARY_SQRT:
        default:
            return false;
    }
}

bool salts_simd_binary(const cmeta_vector_desc *desc,
                       salts_simd_binary_op op,
                       salts_v128 *out,
                       const salts_v128 *left,
                       const salts_v128 *right) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;

    if (!salts_simd_desc_valid(desc) ||
        out == NULL || left == NULL || right == NULL)
        return false;

    a = salts_simd_load_value(left);
    b = salts_simd_load_value(right);

    switch (op) {
        case SALTS_SIMD_BINARY_AND:
            salts_simd_store_value(out, simde_wasm_v128_and(a, b));
            return true;
        case SALTS_SIMD_BINARY_OR:
            salts_simd_store_value(out, simde_wasm_v128_or(a, b));
            return true;
        case SALTS_SIMD_BINARY_XOR:
            salts_simd_store_value(out, simde_wasm_v128_xor(a, b));
            return true;
        default:
            break;
    }

    if (desc->is_mask)
        return false;

    switch (desc->lane_kind) {
        case CMETA_VECTOR_I8:
        case CMETA_VECTOR_U8:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i8x16_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i8x16_sub(a, b);
            else
                return false;
            break;

        case CMETA_VECTOR_I16:
        case CMETA_VECTOR_U16:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i16x8_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i16x8_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_i16x8_mul(a, b);
            else
                return false;
            break;

        case CMETA_VECTOR_I32:
        case CMETA_VECTOR_U32:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i32x4_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i32x4_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_i32x4_mul(a, b);
            else
                return false;
            break;

        case CMETA_VECTOR_I64:
        case CMETA_VECTOR_U64:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_i64x2_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_i64x2_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_i64x2_mul(a, b);
            else
                return false;
            break;

        case CMETA_VECTOR_F32:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_f32x4_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_f32x4_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_f32x4_mul(a, b);
            else if (op == SALTS_SIMD_BINARY_DIV)
                result = simde_wasm_f32x4_div(a, b);
            else
                return false;
            break;

        case CMETA_VECTOR_F64:
            if (op == SALTS_SIMD_BINARY_ADD)
                result = simde_wasm_f64x2_add(a, b);
            else if (op == SALTS_SIMD_BINARY_SUB)
                result = simde_wasm_f64x2_sub(a, b);
            else if (op == SALTS_SIMD_BINARY_MUL)
                result = simde_wasm_f64x2_mul(a, b);
            else if (op == SALTS_SIMD_BINARY_DIV)
                result = simde_wasm_f64x2_div(a, b);
            else
                return false;
            break;

        case CMETA_VECTOR_BOOL:
        default:
            return false;
    }

    salts_simd_store_value(out, result);
    return true;
}

static bool salts_simd_compare_i8(cmeta_vector_lane_kind lane_kind,
                                  salts_simd_compare_op op,
                                  simde_v128_t a,
                                  simde_v128_t b,
                                  simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i8x16_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i8x16_ne(a, b);
        return true;
    }
    if (lane_kind == CMETA_VECTOR_I8) {
        switch (op) {
            case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i8x16_lt(a, b); return true;
            case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i8x16_le(a, b); return true;
            case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i8x16_gt(a, b); return true;
            case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i8x16_ge(a, b); return true;
            default: return false;
        }
    }
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_u8x16_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_u8x16_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_u8x16_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_u8x16_ge(a, b); return true;
        default: return false;
    }
}

static bool salts_simd_compare_i16(cmeta_vector_lane_kind lane_kind,
                                   salts_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i16x8_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i16x8_ne(a, b);
        return true;
    }
    if (lane_kind == CMETA_VECTOR_I16) {
        switch (op) {
            case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i16x8_lt(a, b); return true;
            case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i16x8_le(a, b); return true;
            case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i16x8_gt(a, b); return true;
            case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i16x8_ge(a, b); return true;
            default: return false;
        }
    }
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_u16x8_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_u16x8_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_u16x8_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_u16x8_ge(a, b); return true;
        default: return false;
    }
}

static bool salts_simd_compare_i32(cmeta_vector_lane_kind lane_kind,
                                   salts_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i32x4_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i32x4_ne(a, b);
        return true;
    }
    if (lane_kind == CMETA_VECTOR_I32) {
        switch (op) {
            case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i32x4_lt(a, b); return true;
            case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i32x4_le(a, b); return true;
            case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i32x4_gt(a, b); return true;
            case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i32x4_ge(a, b); return true;
            default: return false;
        }
    }
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_u32x4_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_u32x4_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_u32x4_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_u32x4_ge(a, b); return true;
        default: return false;
    }
}

static bool salts_simd_compare_i64(cmeta_vector_lane_kind lane_kind,
                                   salts_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    if (op == SALTS_SIMD_COMPARE_EQ) {
        *out = simde_wasm_i64x2_eq(a, b);
        return true;
    }
    if (op == SALTS_SIMD_COMPARE_NE) {
        *out = simde_wasm_i64x2_ne(a, b);
        return true;
    }
    if (lane_kind != CMETA_VECTOR_I64)
        return false;
    switch (op) {
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_i64x2_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_i64x2_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_i64x2_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_i64x2_ge(a, b); return true;
        default: return false;
    }
}

static bool salts_simd_compare_f32(salts_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    switch (op) {
        case SALTS_SIMD_COMPARE_EQ: *out = simde_wasm_f32x4_eq(a, b); return true;
        case SALTS_SIMD_COMPARE_NE: *out = simde_wasm_f32x4_ne(a, b); return true;
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_f32x4_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_f32x4_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_f32x4_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_f32x4_ge(a, b); return true;
        default: return false;
    }
}

static bool salts_simd_compare_f64(salts_simd_compare_op op,
                                   simde_v128_t a,
                                   simde_v128_t b,
                                   simde_v128_t *out) {
    switch (op) {
        case SALTS_SIMD_COMPARE_EQ: *out = simde_wasm_f64x2_eq(a, b); return true;
        case SALTS_SIMD_COMPARE_NE: *out = simde_wasm_f64x2_ne(a, b); return true;
        case SALTS_SIMD_COMPARE_LT: *out = simde_wasm_f64x2_lt(a, b); return true;
        case SALTS_SIMD_COMPARE_LE: *out = simde_wasm_f64x2_le(a, b); return true;
        case SALTS_SIMD_COMPARE_GT: *out = simde_wasm_f64x2_gt(a, b); return true;
        case SALTS_SIMD_COMPARE_GE: *out = simde_wasm_f64x2_ge(a, b); return true;
        default: return false;
    }
}

bool salts_simd_compare(const cmeta_vector_desc *desc,
                        salts_simd_compare_op op,
                        salts_v128 *out_mask,
                        const salts_v128 *left,
                        const salts_v128 *right) {
    simde_v128_t a;
    simde_v128_t b;
    simde_v128_t result;
    bool supported;

    if (!salts_simd_desc_valid(desc) || desc->is_mask ||
        out_mask == NULL || left == NULL || right == NULL)
        return false;

    a = salts_simd_load_value(left);
    b = salts_simd_load_value(right);

    switch (desc->lane_kind) {
        case CMETA_VECTOR_I8:
        case CMETA_VECTOR_U8:
            supported = salts_simd_compare_i8(desc->lane_kind, op, a, b, &result);
            break;
        case CMETA_VECTOR_I16:
        case CMETA_VECTOR_U16:
            supported = salts_simd_compare_i16(desc->lane_kind, op, a, b, &result);
            break;
        case CMETA_VECTOR_I32:
        case CMETA_VECTOR_U32:
            supported = salts_simd_compare_i32(desc->lane_kind, op, a, b, &result);
            break;
        case CMETA_VECTOR_I64:
        case CMETA_VECTOR_U64:
            supported = salts_simd_compare_i64(desc->lane_kind, op, a, b, &result);
            break;
        case CMETA_VECTOR_F32:
            supported = salts_simd_compare_f32(op, a, b, &result);
            break;
        case CMETA_VECTOR_F64:
            supported = salts_simd_compare_f64(op, a, b, &result);
            break;
        case CMETA_VECTOR_BOOL:
        default:
            supported = false;
            break;
    }

    if (!supported)
        return false;

    salts_simd_store_value(out_mask, result);
    return true;
}

bool salts_simd_shift(const cmeta_vector_desc *desc,
                      salts_simd_shift_op op,
                      salts_v128 *out,
                      const salts_v128 *value,
                      uint32_t count) {
    simde_v128_t input;
    simde_v128_t result;

    if (!salts_simd_desc_valid(desc) || desc->is_mask ||
        out == NULL || value == NULL)
        return false;

    input = salts_simd_load_value(value);

    switch (desc->lane_kind) {
        case CMETA_VECTOR_I8:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i8x16_shl(input, count)
                : simde_wasm_i8x16_shr(input, count);
            break;
        case CMETA_VECTOR_U8:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i8x16_shl(input, count)
                : simde_wasm_u8x16_shr(input, count);
            break;
        case CMETA_VECTOR_I16:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i16x8_shl(input, count)
                : simde_wasm_i16x8_shr(input, count);
            break;
        case CMETA_VECTOR_U16:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i16x8_shl(input, count)
                : simde_wasm_u16x8_shr(input, count);
            break;
        case CMETA_VECTOR_I32:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i32x4_shl(input, count)
                : simde_wasm_i32x4_shr(input, count);
            break;
        case CMETA_VECTOR_U32:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i32x4_shl(input, count)
                : simde_wasm_u32x4_shr(input, count);
            break;
        case CMETA_VECTOR_I64:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i64x2_shl(input, count)
                : simde_wasm_i64x2_shr(input, count);
            break;
        case CMETA_VECTOR_U64:
            result = op == SALTS_SIMD_SHIFT_LEFT
                ? simde_wasm_i64x2_shl(input, count)
                : simde_wasm_u64x2_shr(input, count);
            break;
        case CMETA_VECTOR_F32:
        case CMETA_VECTOR_F64:
        case CMETA_VECTOR_BOOL:
        default:
            return false;
    }

    salts_simd_store_value(out, result);
    return true;
}

bool salts_simd_select(const cmeta_vector_desc *desc,
                       salts_v128 *out,
                       const salts_v128 *when_set,
                       const salts_v128 *when_unset,
                       const salts_v128 *mask) {
    if (!salts_simd_desc_valid(desc) ||
        out == NULL || when_set == NULL ||
        when_unset == NULL || mask == NULL)
        return false;

    salts_simd_store_value(
        out,
        simde_wasm_v128_bitselect(
            salts_simd_load_value(when_set),
            salts_simd_load_value(when_unset),
            salts_simd_load_value(mask)));
    return true;
}

void salts_simd_i32x4_splat(salts_v128 *out, int32_t value) {
    salts_simd_scalar scalar;
    scalar.i32 = value;
    (void)salts_simd_splat(&cmeta_vector_i32x4, out, scalar);
}

void salts_simd_i32x4_add(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    (void)salts_simd_binary(
        &cmeta_vector_i32x4, SALTS_SIMD_BINARY_ADD,
        out, left, right);
}

void salts_simd_i32x4_sub(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    (void)salts_simd_binary(
        &cmeta_vector_i32x4, SALTS_SIMD_BINARY_SUB,
        out, left, right);
}

void salts_simd_i32x4_mul(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    (void)salts_simd_binary(
        &cmeta_vector_i32x4, SALTS_SIMD_BINARY_MUL,
        out, left, right);
}

void salts_simd_i32x4_eq(salts_v128 *out_mask,
                         const salts_v128 *left,
                         const salts_v128 *right) {
    (void)salts_simd_compare(
        &cmeta_vector_i32x4, SALTS_SIMD_COMPARE_EQ,
        out_mask, left, right);
}

void salts_simd_f32x4_add(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    (void)salts_simd_binary(
        &cmeta_vector_f32x4, SALTS_SIMD_BINARY_ADD,
        out, left, right);
}

void salts_simd_f32x4_mul(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    (void)salts_simd_binary(
        &cmeta_vector_f32x4, SALTS_SIMD_BINARY_MUL,
        out, left, right);
}

void salts_simd_v128_bitselect(salts_v128 *out,
                               const salts_v128 *when_set,
                               const salts_v128 *when_unset,
                               const salts_v128 *mask) {
    (void)salts_simd_select(
        &cmeta_vector_i8x16,
        out, when_set, when_unset, mask);
}
