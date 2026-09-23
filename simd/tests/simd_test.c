#include <salts/simd.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_descriptors(void) {
    const cmeta_vector_desc *desc;

    assert(sizeof(salts_v128) == 16u);
    assert(cmeta_type_find("i32x4") == &cmeta_type_i32x4);
    assert(cmeta_type_find("b32x4") == &cmeta_type_b32x4);

    desc = cmeta_vector_desc_for_type(&cmeta_type_i32x4);
    assert(desc == &cmeta_vector_i32x4);
    assert(cmeta_vector_desc_valid(desc));
    assert(desc->lane_bits == 32u);
    assert(desc->lane_count == 4u);
    assert(!desc->is_mask);
    assert(cmeta_type_is_vector(desc->type));

    desc = cmeta_vector_desc_for_type(&cmeta_type_b32x4);
    assert(desc == &cmeta_vector_b32x4);
    assert(cmeta_vector_desc_valid(desc));
    assert(desc->is_mask);
    assert(cmeta_type_is_mask(desc->type));
}

static void test_i32x4(void) {
    const int32_t left_data[4] = {1, 2, 3, 4};
    const int32_t right_data[4] = {4, 3, 2, 1};
    const int32_t expected_sum[4] = {5, 5, 5, 5};
    const int32_t expected_product[4] = {4, 6, 6, 4};
    int32_t actual[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);

    salts_simd_i32x4_add(&result, &left, &right);
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_sum, sizeof(actual)) == 0);

    salts_simd_i32x4_mul(&result, &left, &right);
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_product, sizeof(actual)) == 0);
}

static void test_i32x4_mask(void) {
    const int32_t left_data[4] = {1, 2, 3, 4};
    const int32_t right_data[4] = {1, 0, 3, 9};
    const uint32_t expected[4] = {UINT32_MAX, 0u, UINT32_MAX, 0u};
    uint32_t actual[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, mask = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);
    salts_simd_i32x4_eq(&mask, &left, &right);
    salts_simd_v128_store(actual, &mask);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

static void test_f32x4(void) {
    const float left_data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const float right_data[4] = {0.5f, 2.0f, 1.5f, 2.0f};
    const float expected[4] = {0.5f, 4.0f, 4.5f, 8.0f};
    float actual[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);
    salts_simd_f32x4_mul(&result, &left, &right);
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

int main(void) {
    test_descriptors();
    test_i32x4();
    test_i32x4_mask();
    test_f32x4();
    return 0;
}
