#ifndef SALTS_SIMD_H
#define SALTS_SIMD_H

#include <cmeta/vector.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public SIMD values use the neutral CMeta carrier. SIMDe/native vector types
 * are intentionally private to the Salts::SIMD implementation. */
typedef cmeta_v128_storage salts_v128;

void salts_simd_v128_load(salts_v128 *out, const void *source);
void salts_simd_v128_store(void *destination, const salts_v128 *value);

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
