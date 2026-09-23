#include <salts/simd.h>

#include <simde/wasm/simd128.h>

static simde_v128_t salts_simd_load_value(const salts_v128 *value) {
    return simde_wasm_v128_load(value->bytes);
}

static void salts_simd_store_value(salts_v128 *out, simde_v128_t value) {
    simde_wasm_v128_store(out->bytes, value);
}

void salts_simd_v128_load(salts_v128 *out, const void *source) {
    if (out == NULL || source == NULL) return;
    salts_simd_store_value(out, simde_wasm_v128_load(source));
}

void salts_simd_v128_store(void *destination, const salts_v128 *value) {
    if (destination == NULL || value == NULL) return;
    simde_wasm_v128_store(destination, salts_simd_load_value(value));
}

void salts_simd_i32x4_splat(salts_v128 *out, int32_t value) {
    if (out == NULL) return;
    salts_simd_store_value(out, simde_wasm_i32x4_splat(value));
}

void salts_simd_i32x4_add(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    if (out == NULL || left == NULL || right == NULL) return;
    salts_simd_store_value(
        out, simde_wasm_i32x4_add(salts_simd_load_value(left),
                                  salts_simd_load_value(right)));
}

void salts_simd_i32x4_sub(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    if (out == NULL || left == NULL || right == NULL) return;
    salts_simd_store_value(
        out, simde_wasm_i32x4_sub(salts_simd_load_value(left),
                                  salts_simd_load_value(right)));
}

void salts_simd_i32x4_mul(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    if (out == NULL || left == NULL || right == NULL) return;
    salts_simd_store_value(
        out, simde_wasm_i32x4_mul(salts_simd_load_value(left),
                                  salts_simd_load_value(right)));
}

void salts_simd_i32x4_eq(salts_v128 *out_mask,
                         const salts_v128 *left,
                         const salts_v128 *right) {
    if (out_mask == NULL || left == NULL || right == NULL) return;
    salts_simd_store_value(
        out_mask, simde_wasm_i32x4_eq(salts_simd_load_value(left),
                                      salts_simd_load_value(right)));
}

void salts_simd_f32x4_add(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    if (out == NULL || left == NULL || right == NULL) return;
    salts_simd_store_value(
        out, simde_wasm_f32x4_add(salts_simd_load_value(left),
                                  salts_simd_load_value(right)));
}

void salts_simd_f32x4_mul(salts_v128 *out,
                          const salts_v128 *left,
                          const salts_v128 *right) {
    if (out == NULL || left == NULL || right == NULL) return;
    salts_simd_store_value(
        out, simde_wasm_f32x4_mul(salts_simd_load_value(left),
                                  salts_simd_load_value(right)));
}

void salts_simd_v128_bitselect(salts_v128 *out,
                               const salts_v128 *when_set,
                               const salts_v128 *when_unset,
                               const salts_v128 *mask) {
    if (out == NULL || when_set == NULL || when_unset == NULL || mask == NULL)
        return;
    salts_simd_store_value(
        out, simde_wasm_v128_bitselect(salts_simd_load_value(when_set),
                                       salts_simd_load_value(when_unset),
                                       salts_simd_load_value(mask)));
}
