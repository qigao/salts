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


static void test_generic_splat(void) {
    salts_simd_scalar scalar = {0};
    salts_v128 value = {{0}};
    uint16_t lanes[8] = {0};
    size_t index;

    scalar.u16 = UINT16_C(0xff80);
    assert(salts_simd_splat(
        &cmeta_vector_u16x8, &value, scalar));
    salts_simd_v128_store(lanes, &value);
    for (index = 0u; index < 8u; ++index)
        assert(lanes[index] == UINT16_C(0xff80));
}

static void test_generic_integer_binary(void) {
    const int16_t left_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const int16_t right_data[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    const int16_t expected_add[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    const int16_t expected_mul[8] = {8, 14, 18, 20, 20, 18, 14, 8};
    int16_t actual[8] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);

    assert(salts_simd_binary(
        &cmeta_vector_i16x8,
        SALTS_SIMD_BINARY_ADD,
        &result, &left, &right));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_add, sizeof(actual)) == 0);

    assert(salts_simd_binary(
        &cmeta_vector_i16x8,
        SALTS_SIMD_BINARY_MUL,
        &result, &left, &right));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_mul, sizeof(actual)) == 0);
}

static void test_generic_float_binary(void) {
    const double left_data[2] = {12.0, 9.0};
    const double right_data[2] = {3.0, 1.5};
    const double expected[2] = {4.0, 6.0};
    double actual[2] = {0.0, 0.0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);
    assert(salts_simd_binary(
        &cmeta_vector_f64x2,
        SALTS_SIMD_BINARY_DIV,
        &result, &left, &right));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

static void test_generic_compare_signedness(void) {
    const uint32_t left_u[4] = {
        UINT32_MAX, 1u, 2u, UINT32_C(0x80000000)
    };
    const uint32_t right_u[4] = {
        1u, 2u, 2u, UINT32_MAX
    };
    const uint32_t expected_u[4] = {
        0u, UINT32_MAX, 0u, UINT32_MAX
    };
    const int32_t left_s[4] = {-1, 1, 2, INT32_MIN};
    const int32_t right_s[4] = {1, 2, 2, -1};
    const uint32_t expected_s[4] = {
        UINT32_MAX, UINT32_MAX, 0u, UINT32_MAX
    };
    uint32_t actual[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, mask = {{0}};

    salts_simd_v128_load(&left, left_u);
    salts_simd_v128_load(&right, right_u);
    assert(salts_simd_compare(
        &cmeta_vector_u32x4,
        SALTS_SIMD_COMPARE_LT,
        &mask, &left, &right));
    salts_simd_v128_store(actual, &mask);
    assert(memcmp(actual, expected_u, sizeof(actual)) == 0);

    salts_simd_v128_load(&left, left_s);
    salts_simd_v128_load(&right, right_s);
    assert(salts_simd_compare(
        &cmeta_vector_i32x4,
        SALTS_SIMD_COMPARE_LT,
        &mask, &left, &right));
    salts_simd_v128_store(actual, &mask);
    assert(memcmp(actual, expected_s, sizeof(actual)) == 0);
}

static void test_generic_bitwise(void) {
    const uint32_t left_data[4] = {
        UINT32_C(0xff00ff00), UINT32_C(0xaaaaaaaa),
        UINT32_C(0x12345678), UINT32_C(0xffffffff)
    };
    const uint32_t right_data[4] = {
        UINT32_C(0x0f0f0f0f), UINT32_C(0x55555555),
        UINT32_C(0xffff0000), UINT32_C(0x00000000)
    };
    const uint32_t expected_xor[4] = {
        UINT32_C(0xf00ff00f), UINT32_C(0xffffffff),
        UINT32_C(0xedcb5678), UINT32_C(0xffffffff)
    };
    uint32_t actual[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);
    assert(salts_simd_binary(
        &cmeta_vector_u32x4,
        SALTS_SIMD_BINARY_XOR,
        &result, &left, &right));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_xor, sizeof(actual)) == 0);

    assert(salts_simd_unary(
        &cmeta_vector_u32x4,
        SALTS_SIMD_UNARY_NOT,
        &result, &left));
    salts_simd_v128_store(actual, &result);
    assert(actual[0] == UINT32_C(0x00ff00ff));
    assert(actual[1] == UINT32_C(0x55555555));
}

static void test_generic_shift(void) {
    const int32_t signed_data[4] = {-8, -4, 8, 4};
    const int32_t expected_signed[4] = {-4, -2, 4, 2};
    const uint32_t unsigned_data[4] = {
        UINT32_C(0xfffffff8), UINT32_C(0x80000000), 8u, 4u
    };
    const uint32_t expected_unsigned[4] = {
        UINT32_C(0x7ffffffc), UINT32_C(0x40000000), 4u, 2u
    };
    int32_t signed_actual[4] = {0};
    uint32_t unsigned_actual[4] = {0};
    salts_v128 value = {{0}}, result = {{0}};

    salts_simd_v128_load(&value, signed_data);
    assert(salts_simd_shift(
        &cmeta_vector_i32x4,
        SALTS_SIMD_SHIFT_RIGHT,
        &result, &value, 1u));
    salts_simd_v128_store(signed_actual, &result);
    assert(memcmp(
        signed_actual, expected_signed, sizeof(signed_actual)) == 0);

    salts_simd_v128_load(&value, unsigned_data);
    assert(salts_simd_shift(
        &cmeta_vector_u32x4,
        SALTS_SIMD_SHIFT_RIGHT,
        &result, &value, 1u));
    salts_simd_v128_store(unsigned_actual, &result);
    assert(memcmp(
        unsigned_actual, expected_unsigned, sizeof(unsigned_actual)) == 0);
}

static void test_generic_select(void) {
    const uint32_t when_set_data[4] = {1u, 2u, 3u, 4u};
    const uint32_t when_unset_data[4] = {10u, 20u, 30u, 40u};
    const uint32_t mask_data[4] = {
        UINT32_MAX, 0u, UINT32_MAX, 0u
    };
    const uint32_t expected[4] = {1u, 20u, 3u, 40u};
    uint32_t actual[4] = {0};
    salts_v128 when_set = {{0}}, when_unset = {{0}};
    salts_v128 mask = {{0}}, result = {{0}};

    salts_simd_v128_load(&when_set, when_set_data);
    salts_simd_v128_load(&when_unset, when_unset_data);
    salts_simd_v128_load(&mask, mask_data);

    assert(salts_simd_select(
        &cmeta_vector_u32x4,
        &result, &when_set, &when_unset, &mask));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
}

static void test_generic_unsupported_pairs(void) {
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    assert(!salts_simd_binary(
        &cmeta_vector_i32x4,
        SALTS_SIMD_BINARY_DIV,
        &result, &left, &right));

    assert(!salts_simd_compare(
        &cmeta_vector_u64x2,
        SALTS_SIMD_COMPARE_LT,
        &result, &left, &right));

    assert(!salts_simd_unary(
        &cmeta_vector_i32x4,
        SALTS_SIMD_UNARY_ABS,
        &result, &left));
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
    test_generic_splat();
    test_generic_integer_binary();
    test_generic_float_binary();
    test_generic_compare_signedness();
    test_generic_bitwise();
    test_generic_shift();
    test_generic_select();
    test_generic_unsupported_pairs();
    test_i32x4();
    test_i32x4_mask();
    test_f32x4();
    return 0;
}
