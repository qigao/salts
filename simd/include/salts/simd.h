#ifndef SALTS_SIMD_H
#define SALTS_SIMD_H

#include <cmeta/vector.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public SIMD values use the neutral CMeta carrier. SIMDe/native vector types
 * are intentionally private to the Salts::SIMD implementation. */
typedef cmeta_v128_storage salts_v128;

typedef union salts_simd_scalar {
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    float f32;
    double f64;
} salts_simd_scalar;

typedef enum salts_simd_unary_op {
    SALTS_SIMD_UNARY_NOT = 0,
    SALTS_SIMD_UNARY_ABS,
    SALTS_SIMD_UNARY_NEG,
    SALTS_SIMD_UNARY_SQRT,
    SALTS_SIMD_UNARY_POPCOUNT
} salts_simd_unary_op;

typedef enum salts_simd_binary_op {
    SALTS_SIMD_BINARY_ADD = 0,
    SALTS_SIMD_BINARY_SUB,
    SALTS_SIMD_BINARY_MUL,
    SALTS_SIMD_BINARY_DIV,
    SALTS_SIMD_BINARY_MIN,
    SALTS_SIMD_BINARY_MAX,
    SALTS_SIMD_BINARY_PSEUDO_MIN,
    SALTS_SIMD_BINARY_PSEUDO_MAX,
    SALTS_SIMD_BINARY_AND,
    SALTS_SIMD_BINARY_OR,
    SALTS_SIMD_BINARY_XOR
} salts_simd_binary_op;

typedef enum salts_simd_compare_op {
    SALTS_SIMD_COMPARE_EQ = 0,
    SALTS_SIMD_COMPARE_NE,
    SALTS_SIMD_COMPARE_LT,
    SALTS_SIMD_COMPARE_LE,
    SALTS_SIMD_COMPARE_GT,
    SALTS_SIMD_COMPARE_GE
} salts_simd_compare_op;

typedef enum salts_simd_shift_op {
    SALTS_SIMD_SHIFT_LEFT = 0,
    SALTS_SIMD_SHIFT_RIGHT
} salts_simd_shift_op;


typedef enum salts_simd_saturating_op {
    SALTS_SIMD_SATURATING_ADD = 0,
    SALTS_SIMD_SATURATING_SUB
} salts_simd_saturating_op;

typedef enum salts_simd_reduce_op {
    SALTS_SIMD_REDUCE_ANY_TRUE = 0,
    SALTS_SIMD_REDUCE_ALL_TRUE,
    SALTS_SIMD_REDUCE_BITMASK
} salts_simd_reduce_op;

void salts_simd_v128_load(salts_v128 *out, const void *source);
void salts_simd_v128_store(void *destination, const salts_v128 *value);

bool salts_simd_splat(const cmeta_vector_desc *desc,
                      salts_v128 *out,
                      salts_simd_scalar scalar);


bool salts_simd_load_splat(const cmeta_vector_desc *desc,
                           salts_v128 *out,
                           const void *source);

bool salts_simd_load_extend(const cmeta_vector_desc *desc,
                            salts_v128 *out,
                            const void *source);

bool salts_simd_load_zero(uint16_t loaded_bits,
                          salts_v128 *out,
                          const void *source);

bool salts_simd_extract_lane(const cmeta_vector_desc *desc,
                             const salts_v128 *value,
                             uint16_t lane,
                             salts_simd_scalar *out);

bool salts_simd_replace_lane(const cmeta_vector_desc *desc,
                             salts_v128 *out,
                             const salts_v128 *value,
                             uint16_t lane,
                             salts_simd_scalar scalar);

bool salts_simd_shuffle_bytes(salts_v128 *out,
                              const salts_v128 *left,
                              const salts_v128 *right,
                              const uint8_t lanes[16]);

bool salts_simd_swizzle_bytes(salts_v128 *out,
                              const salts_v128 *value,
                              const salts_v128 *indices);


bool salts_simd_unary(const cmeta_vector_desc *desc,
                      salts_simd_unary_op op,
                      salts_v128 *out,
                      const salts_v128 *value);

bool salts_simd_binary(const cmeta_vector_desc *desc,
                       salts_simd_binary_op op,
                       salts_v128 *out,
                       const salts_v128 *left,
                       const salts_v128 *right);

bool salts_simd_compare(const cmeta_vector_desc *desc,
                        salts_simd_compare_op op,
                        salts_v128 *out_mask,
                        const salts_v128 *left,
                        const salts_v128 *right);

bool salts_simd_shift(const cmeta_vector_desc *desc,
                      salts_simd_shift_op op,
                      salts_v128 *out,
                      const salts_v128 *value,
                      uint32_t count);


bool salts_simd_saturating_binary(
    const cmeta_vector_desc *desc,
    salts_simd_saturating_op op,
    salts_v128 *out,
    const salts_v128 *left,
    const salts_v128 *right);

bool salts_simd_reduce(const cmeta_vector_desc *desc,
                       salts_simd_reduce_op op,
                       const salts_v128 *value,
                       uint32_t *out);

bool salts_simd_select(const cmeta_vector_desc *desc,
                       salts_v128 *out,
                       const salts_v128 *when_set,
                       const salts_v128 *when_unset,
                       const salts_v128 *mask);

/* Compatibility helpers. New consumers should prefer the descriptor-driven
 * generic API above. */
void salts_simd_i32x4_splat(salts_v128 *out, int32_t value);
void salts_simd_i32x4_add(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right);
void salts_simd_i32x4_sub(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right);
void salts_simd_i32x4_mul(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right);
void salts_simd_i32x4_eq(salts_v128 *out_mask,
                         const salts_v128 *left,
                         const salts_v128 *right);

void salts_simd_f32x4_add(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right);
void salts_simd_f32x4_mul(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right);

void salts_simd_v128_bitselect(salts_v128 *out,
                               const salts_v128 *when_set,
                               const salts_v128 *when_unset,
                               const salts_v128 *mask);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_SIMD_H */
