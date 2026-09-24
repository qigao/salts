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



static void test_load_extend(void) {
    const int8_t i8_source[8] = {-1, 2, -3, 4, -5, 6, -7, 8};
    const int16_t i16_expected[8] = {-1, 2, -3, 4, -5, 6, -7, 8};
    const uint8_t u8_source[8] = {255u, 2u, 253u, 4u, 251u, 6u, 249u, 8u};
    const uint16_t u16_expected[8] = {255u, 2u, 253u, 4u, 251u, 6u, 249u, 8u};

    const int16_t i16_source[4] = {-1, 2, -300, 400};
    const int32_t i32_expected[4] = {-1, 2, -300, 400};
    const uint16_t u16_source[4] = {65535u, 2u, 65000u, 400u};
    const uint32_t u32_expected[4] = {65535u, 2u, 65000u, 400u};

    const int32_t i32_source[2] = {-1, INT32_MIN};
    const int64_t i64_expected[2] = {-1, (int64_t)INT32_MIN};
    const uint32_t u32_source[2] = {UINT32_MAX, UINT32_C(0x80000000)};
    const uint64_t u64_expected[2] = {UINT32_MAX, UINT32_C(0x80000000)};

    salts_v128 result = {{0}};
    int16_t i16_actual[8] = {0};
    uint16_t u16_actual[8] = {0};
    int32_t i32_actual[4] = {0};
    uint32_t u32_actual[4] = {0};
    int64_t i64_actual[2] = {0};
    uint64_t u64_actual[2] = {0};

    assert(salts_simd_load_extend(
        &cmeta_vector_i16x8, &result, i8_source));
    salts_simd_v128_store(i16_actual, &result);
    assert(memcmp(i16_actual, i16_expected, sizeof(i16_actual)) == 0);

    assert(salts_simd_load_extend(
        &cmeta_vector_u16x8, &result, u8_source));
    salts_simd_v128_store(u16_actual, &result);
    assert(memcmp(u16_actual, u16_expected, sizeof(u16_actual)) == 0);

    assert(salts_simd_load_extend(
        &cmeta_vector_i32x4, &result, i16_source));
    salts_simd_v128_store(i32_actual, &result);
    assert(memcmp(i32_actual, i32_expected, sizeof(i32_actual)) == 0);

    assert(salts_simd_load_extend(
        &cmeta_vector_u32x4, &result, u16_source));
    salts_simd_v128_store(u32_actual, &result);
    assert(memcmp(u32_actual, u32_expected, sizeof(u32_actual)) == 0);

    assert(salts_simd_load_extend(
        &cmeta_vector_i64x2, &result, i32_source));
    salts_simd_v128_store(i64_actual, &result);
    assert(memcmp(i64_actual, i64_expected, sizeof(i64_actual)) == 0);

    assert(salts_simd_load_extend(
        &cmeta_vector_u64x2, &result, u32_source));
    salts_simd_v128_store(u64_actual, &result);
    assert(memcmp(u64_actual, u64_expected, sizeof(u64_actual)) == 0);

    assert(!salts_simd_load_extend(
        &cmeta_vector_i8x16, &result, i8_source));
}

static void test_load_splat_and_zero(void) {
    const uint8_t byte = UINT8_C(0xa5);
    const uint16_t word = UINT16_C(0x1234);
    const uint32_t dword = UINT32_C(0x89abcdef);
    const uint64_t qword = UINT64_C(0x0123456789abcdef);
    uint8_t bytes[16] = {0};
    uint16_t words[8] = {0};
    uint32_t dwords[4] = {0};
    uint64_t qwords[2] = {0};
    salts_v128 value = {{0}};
    size_t i;

    assert(salts_simd_load_splat(
        &cmeta_vector_u8x16, &value, &byte));
    salts_simd_v128_store(bytes, &value);
    for (i = 0u; i < 16u; ++i) assert(bytes[i] == byte);

    assert(salts_simd_load_splat(
        &cmeta_vector_u16x8, &value, &word));
    salts_simd_v128_store(words, &value);
    for (i = 0u; i < 8u; ++i) assert(words[i] == word);

    assert(salts_simd_load_splat(
        &cmeta_vector_u32x4, &value, &dword));
    salts_simd_v128_store(dwords, &value);
    for (i = 0u; i < 4u; ++i) assert(dwords[i] == dword);

    assert(salts_simd_load_splat(
        &cmeta_vector_u64x2, &value, &qword));
    salts_simd_v128_store(qwords, &value);
    assert(qwords[0] == qword && qwords[1] == qword);

    memset(bytes, 0xff, sizeof(bytes));
    assert(salts_simd_load_zero(32u, &value, &dword));
    salts_simd_v128_store(bytes, &value);
    assert(memcmp(bytes, &dword, sizeof(dword)) == 0);
    for (i = sizeof(dword); i < sizeof(bytes); ++i) assert(bytes[i] == 0u);

    memset(bytes, 0xff, sizeof(bytes));
    assert(salts_simd_load_zero(64u, &value, &qword));
    salts_simd_v128_store(bytes, &value);
    assert(memcmp(bytes, &qword, sizeof(qword)) == 0);
    for (i = sizeof(qword); i < sizeof(bytes); ++i) assert(bytes[i] == 0u);

    assert(!salts_simd_load_zero(16u, &value, &word));
}

static void test_lane_extract_replace(void) {
    const int16_t original[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const int16_t expected[8] = {10, 20, 30, -1234, 50, 60, 70, 80};
    int16_t actual[8] = {0};
    salts_v128 value = {{0}};
    salts_v128 replaced = {{0}};
    salts_simd_scalar scalar = {0};
    salts_simd_scalar extracted = {0};

    salts_simd_v128_load(&value, original);

    scalar.i16 = -1234;
    assert(salts_simd_replace_lane(
        &cmeta_vector_i16x8, &replaced, &value, 3u, scalar));
    salts_simd_v128_store(actual, &replaced);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);

    assert(salts_simd_extract_lane(
        &cmeta_vector_i16x8, &replaced, 3u, &extracted));
    assert(extracted.i16 == -1234);

    assert(!salts_simd_replace_lane(
        &cmeta_vector_i16x8, &replaced, &value, 8u, scalar));
    assert(!salts_simd_extract_lane(
        &cmeta_vector_i16x8, &value, 8u, &extracted));

    {
        const uint8_t unsigned_bytes[16] = {
            0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
            8u, 9u, 10u, 11u, 12u, 13u, 14u, 255u
        };
        salts_simd_v128_load(&value, unsigned_bytes);
        assert(salts_simd_extract_lane(
            &cmeta_vector_u8x16, &value, 15u, &extracted));
        assert(extracted.u8 == 255u);
    }
}

static void test_shuffle_and_swizzle(void) {
    const uint8_t left_data[16] = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
    };
    const uint8_t right_data[16] = {
        16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u,
        24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u
    };
    const uint8_t lanes[16] = {
        31u, 0u, 16u, 15u, 17u, 1u, 30u, 2u,
        29u, 3u, 28u, 4u, 27u, 5u, 26u, 6u
    };
    const uint8_t expected_shuffle[16] = {
        31u, 0u, 16u, 15u, 17u, 1u, 30u, 2u,
        29u, 3u, 28u, 4u, 27u, 5u, 26u, 6u
    };
    const uint8_t swizzle_index_data[16] = {
        15u, 0u, 1u, 16u, 2u, 17u, 3u, 255u,
        4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u
    };
    const uint8_t expected_swizzle[16] = {
        15u, 0u, 1u, 0u, 2u, 0u, 3u, 0u,
        4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u
    };
    uint8_t actual[16] = {0};
    uint8_t bad_lanes[16];
    salts_v128 left = {{0}}, right = {{0}}, indices = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_data);
    salts_simd_v128_load(&right, right_data);

    assert(salts_simd_shuffle_bytes(
        &result, &left, &right, lanes));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_shuffle, sizeof(actual)) == 0);

    memcpy(bad_lanes, lanes, sizeof(bad_lanes));
    bad_lanes[7] = 32u;
    assert(!salts_simd_shuffle_bytes(
        &result, &left, &right, bad_lanes));

    salts_simd_v128_load(&indices, swizzle_index_data);
    assert(salts_simd_swizzle_bytes(
        &result, &left, &indices));
    salts_simd_v128_store(actual, &result);
    assert(memcmp(actual, expected_swizzle, sizeof(actual)) == 0);
}


static void test_advanced_unary(void) {
    const int32_t ints[4] = {-1, -2, 3, -4};
    const int32_t expected_abs[4] = {1, 2, 3, 4};
    const int32_t expected_neg[4] = {1, 2, -3, 4};
    const float floats[4] = {1.0f, 4.0f, 9.0f, 16.0f};
    const float expected_sqrt[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const uint8_t pop_source[16] = {
        0u, 1u, 3u, 7u, 15u, 31u, 63u, 127u,
        255u, 0x55u, 0xaau, 0x81u, 0x80u, 0x0fu, 0xf0u, 0xfeu
    };
    const uint8_t pop_expected[16] = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
        8u, 4u, 4u, 2u, 1u, 4u, 4u, 7u
    };
    int32_t int_actual[4] = {0};
    float float_actual[4] = {0};
    uint8_t pop_actual[16] = {0};
    salts_v128 value = {{0}}, result = {{0}};

    salts_simd_v128_load(&value, ints);
    assert(salts_simd_unary(
        &cmeta_vector_i32x4, SALTS_SIMD_UNARY_ABS,
        &result, &value));
    salts_simd_v128_store(int_actual, &result);
    assert(memcmp(int_actual, expected_abs, sizeof(int_actual)) == 0);

    assert(salts_simd_unary(
        &cmeta_vector_i32x4, SALTS_SIMD_UNARY_NEG,
        &result, &value));
    salts_simd_v128_store(int_actual, &result);
    assert(memcmp(int_actual, expected_neg, sizeof(int_actual)) == 0);

    salts_simd_v128_load(&value, floats);
    assert(salts_simd_unary(
        &cmeta_vector_f32x4, SALTS_SIMD_UNARY_SQRT,
        &result, &value));
    salts_simd_v128_store(float_actual, &result);
    assert(memcmp(float_actual, expected_sqrt, sizeof(float_actual)) == 0);

    salts_simd_v128_load(&value, pop_source);
    assert(salts_simd_unary(
        &cmeta_vector_u8x16, SALTS_SIMD_UNARY_POPCOUNT,
        &result, &value));
    salts_simd_v128_store(pop_actual, &result);
    assert(memcmp(pop_actual, pop_expected, sizeof(pop_actual)) == 0);
}

static void test_advanced_min_max(void) {
    const int32_t left_s[4] = {-5, 100, -1, 7};
    const int32_t right_s[4] = {-3, 2, -9, 7};
    const int32_t min_s[4] = {-5, 2, -9, 7};
    const int32_t max_s[4] = {-3, 100, -1, 7};
    const uint32_t left_u[4] = {UINT32_MAX, 1u, 8u, 3u};
    const uint32_t right_u[4] = {1u, 2u, 4u, 9u};
    const uint32_t min_u[4] = {1u, 1u, 4u, 3u};
    const uint32_t max_u[4] = {UINT32_MAX, 2u, 8u, 9u};
    int32_t actual_s[4] = {0};
    uint32_t actual_u[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_s);
    salts_simd_v128_load(&right, right_s);
    assert(salts_simd_binary(
        &cmeta_vector_i32x4, SALTS_SIMD_BINARY_MIN,
        &result, &left, &right));
    salts_simd_v128_store(actual_s, &result);
    assert(memcmp(actual_s, min_s, sizeof(actual_s)) == 0);

    assert(salts_simd_binary(
        &cmeta_vector_i32x4, SALTS_SIMD_BINARY_MAX,
        &result, &left, &right));
    salts_simd_v128_store(actual_s, &result);
    assert(memcmp(actual_s, max_s, sizeof(actual_s)) == 0);

    salts_simd_v128_load(&left, left_u);
    salts_simd_v128_load(&right, right_u);
    assert(salts_simd_binary(
        &cmeta_vector_u32x4, SALTS_SIMD_BINARY_MIN,
        &result, &left, &right));
    salts_simd_v128_store(actual_u, &result);
    assert(memcmp(actual_u, min_u, sizeof(actual_u)) == 0);

    assert(salts_simd_binary(
        &cmeta_vector_u32x4, SALTS_SIMD_BINARY_MAX,
        &result, &left, &right));
    salts_simd_v128_store(actual_u, &result);
    assert(memcmp(actual_u, max_u, sizeof(actual_u)) == 0);
}

static void test_pseudo_min_max(void) {
    const float left_f32[4] = {1.0f, 8.0f, -3.0f, 4.0f};
    const float right_f32[4] = {2.0f, 7.0f, -4.0f, 5.0f};
    const float expected_pmin_f32[4] = {1.0f, 7.0f, -4.0f, 4.0f};
    const double left_f64[2] = {9.0, -2.0};
    const double right_f64[2] = {8.0, -3.0};
    const double expected_pmax_f64[2] = {9.0, -2.0};
    float actual_f32[4] = {0};
    double actual_f64[2] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_f32);
    salts_simd_v128_load(&right, right_f32);
    assert(salts_simd_binary(
        &cmeta_vector_f32x4, SALTS_SIMD_BINARY_PSEUDO_MIN,
        &result, &left, &right));
    salts_simd_v128_store(actual_f32, &result);
    assert(memcmp(
        actual_f32, expected_pmin_f32, sizeof(actual_f32)) == 0);

    salts_simd_v128_load(&left, left_f64);
    salts_simd_v128_load(&right, right_f64);
    assert(salts_simd_binary(
        &cmeta_vector_f64x2, SALTS_SIMD_BINARY_PSEUDO_MAX,
        &result, &left, &right));
    salts_simd_v128_store(actual_f64, &result);
    assert(memcmp(
        actual_f64, expected_pmax_f64, sizeof(actual_f64)) == 0);

    assert(!salts_simd_binary(
        &cmeta_vector_i32x4, SALTS_SIMD_BINARY_PSEUDO_MIN,
        &result, &left, &right));
}

static void test_saturating_binary(void) {
    const int8_t left_s[16] = {
        120, -120, 100, -100, 1, -1, 127, -128,
        10, 20, 30, 40, -10, -20, -30, -40
    };
    const int8_t right_s[16] = {
        20, -20, 50, -50, 2, -2, 1, -1,
        -100, 100, 100, 100, -100, -100, -100, -100
    };
    const int8_t add_s[16] = {
        127, -128, 127, -128, 3, -3, 127, -128,
        -90, 120, 127, 127, -110, -120, -128, -128
    };
    const uint8_t left_u[16] = {
        250u, 1u, 200u, 0u, 10u, 20u, 30u, 40u,
        50u, 60u, 70u, 80u, 90u, 100u, 110u, 120u
    };
    const uint8_t right_u[16] = {
        10u, 2u, 100u, 1u, 20u, 30u, 40u, 50u,
        60u, 70u, 80u, 90u, 100u, 110u, 120u, 130u
    };
    const uint8_t sub_u[16] = {
        240u, 0u, 100u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    int8_t actual_s[16] = {0};
    uint8_t actual_u[16] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, left_s);
    salts_simd_v128_load(&right, right_s);
    assert(salts_simd_saturating_binary(
        &cmeta_vector_i8x16, SALTS_SIMD_SATURATING_ADD,
        &result, &left, &right));
    salts_simd_v128_store(actual_s, &result);
    assert(memcmp(actual_s, add_s, sizeof(actual_s)) == 0);

    salts_simd_v128_load(&left, left_u);
    salts_simd_v128_load(&right, right_u);
    assert(salts_simd_saturating_binary(
        &cmeta_vector_u8x16, SALTS_SIMD_SATURATING_SUB,
        &result, &left, &right));
    salts_simd_v128_store(actual_u, &result);
    assert(memcmp(actual_u, sub_u, sizeof(actual_u)) == 0);
}

static void test_reductions(void) {
    const int16_t all_nonzero[8] = {1, -1, 2, -2, 3, -3, 4, -4};
    const int16_t with_zero[8] = {1, -1, 2, 0, 3, -3, 4, -4};
    const int8_t bitmask_lanes[16] = {
        -1, 1, -2, 2, -3, 3, -4, 4,
        -5, 5, -6, 6, -7, 7, -8, 8
    };
    uint32_t reduced = 0u;
    salts_v128 value = {{0}};

    salts_simd_v128_load(&value, all_nonzero);
    assert(salts_simd_reduce(
        &cmeta_vector_i16x8, SALTS_SIMD_REDUCE_ALL_TRUE,
        &value, &reduced));
    assert(reduced == 1u);

    salts_simd_v128_load(&value, with_zero);
    assert(salts_simd_reduce(
        &cmeta_vector_i16x8, SALTS_SIMD_REDUCE_ALL_TRUE,
        &value, &reduced));
    assert(reduced == 0u);
    assert(salts_simd_reduce(
        &cmeta_vector_i16x8, SALTS_SIMD_REDUCE_ANY_TRUE,
        &value, &reduced));
    assert(reduced == 1u);

    salts_simd_v128_load(&value, bitmask_lanes);
    assert(salts_simd_reduce(
        &cmeta_vector_i8x16, SALTS_SIMD_REDUCE_BITMASK,
        &value, &reduced));
    assert(reduced == UINT32_C(0x5555));

    memset(&value, 0, sizeof(value));
    assert(salts_simd_reduce(
        &cmeta_vector_i8x16, SALTS_SIMD_REDUCE_ANY_TRUE,
        &value, &reduced));
    assert(reduced == 0u);
}


static void test_widen_narrow_families(void) {
    const int16_t narrow_low[8] = {
        -200, -128, -1, 0, 1, 127, 128, 300
    };
    const int16_t narrow_high[8] = {
        -300, -129, -2, 2, 126, 200, 1000, -1000
    };
    const int8_t narrow_expected[16] = {
        -128, -128, -1, 0, 1, 127, 127, 127,
        -128, -128, -2, 2, 126, 127, 127, -128
    };
    const int8_t extend_source[16] = {
        -8, -7, -6, -5, -4, -3, -2, -1,
        1, 2, 3, 4, 5, 6, 7, 8
    };
    const int16_t extend_expected[8] = {
        1, 2, 3, 4, 5, 6, 7, 8
    };
    const int8_t mul_left[16] = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16
    };
    const int8_t mul_right[16] = {
        2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17
    };
    const int16_t mul_expected[8] = {
        90, 110, 132, 156, 182, 210, 240, 272
    };
    const int8_t pairwise_source[16] = {
        1, 2, 3, 4, -1, -2, -3, -4,
        5, 6, 7, 8, -5, -6, -7, -8
    };
    const int16_t pairwise_expected[8] = {
        3, 7, -3, -7, 11, 15, -11, -15
    };

    int8_t narrow_actual[16] = {0};
    int16_t wide_actual[8] = {0};
    salts_v128 a = {{0}}, b = {{0}}, result = {{0}};

    salts_simd_v128_load(&a, narrow_low);
    salts_simd_v128_load(&b, narrow_high);
    assert(salts_simd_narrow(
        &cmeta_vector_i8x16, &result, &a, &b));
    salts_simd_v128_store(narrow_actual, &result);
    assert(memcmp(
        narrow_actual, narrow_expected, sizeof(narrow_actual)) == 0);

    salts_simd_v128_load(&a, extend_source);
    assert(salts_simd_extend_half(
        &cmeta_vector_i16x8, SALTS_SIMD_HALF_HIGH,
        &result, &a));
    salts_simd_v128_store(wide_actual, &result);
    assert(memcmp(
        wide_actual, extend_expected, sizeof(wide_actual)) == 0);

    salts_simd_v128_load(&a, mul_left);
    salts_simd_v128_load(&b, mul_right);
    assert(salts_simd_extmul_half(
        &cmeta_vector_i16x8, SALTS_SIMD_HALF_HIGH,
        &result, &a, &b));
    salts_simd_v128_store(wide_actual, &result);
    assert(memcmp(
        wide_actual, mul_expected, sizeof(wide_actual)) == 0);

    salts_simd_v128_load(&a, pairwise_source);
    assert(salts_simd_extadd_pairwise(
        &cmeta_vector_i16x8, &result, &a));
    salts_simd_v128_store(wide_actual, &result);
    assert(memcmp(
        wide_actual, pairwise_expected, sizeof(wide_actual)) == 0);

    assert(!salts_simd_extend_half(
        &cmeta_vector_f32x4, SALTS_SIMD_HALF_LOW,
        &result, &a));
    assert(!salts_simd_extmul_half(
        &cmeta_vector_i16x8, (salts_simd_half)99,
        &result, &a, &b));
}

static void test_q15_and_dot(void) {
    const int16_t q15_left[8] = {
        16384, 32767, -32768, 8192,
        -16384, 1000, -1000, 0
    };
    const int16_t q15_right[8] = {
        16384, 32767, -32768, 16384,
        16384, 2000, 2000, 32767
    };
    const int16_t q15_expected[8] = {
        8192, 32766, 32767, 4096,
        -8192, 61, -61, 0
    };
    const int16_t dot_left[8] = {
        1, 2, 3, 4, -1, -2, 100, 200
    };
    const int16_t dot_right[8] = {
        10, 20, 30, 40, 5, 6, -2, 3
    };
    const int32_t dot_expected[4] = {
        50, 250, -17, 400
    };
    int16_t q15_actual[8] = {0};
    int32_t dot_actual[4] = {0};
    salts_v128 left = {{0}}, right = {{0}}, result = {{0}};

    salts_simd_v128_load(&left, q15_left);
    salts_simd_v128_load(&right, q15_right);
    assert(salts_simd_q15mulr_sat(
        &cmeta_vector_i16x8, &result, &left, &right));
    salts_simd_v128_store(q15_actual, &result);
    assert(memcmp(q15_actual, q15_expected, sizeof(q15_actual)) == 0);

    salts_simd_v128_load(&left, dot_left);
    salts_simd_v128_load(&right, dot_right);
    assert(salts_simd_dot_pairwise(
        &cmeta_vector_i32x4, &result, &left, &right));
    salts_simd_v128_store(dot_actual, &result);
    assert(memcmp(dot_actual, dot_expected, sizeof(dot_actual)) == 0);

    assert(!salts_simd_q15mulr_sat(
        &cmeta_vector_u16x8, &result, &left, &right));
    assert(!salts_simd_dot_pairwise(
        &cmeta_vector_u32x4, &result, &left, &right));
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
        &cmeta_vector_u32x4,
        SALTS_SIMD_UNARY_ABS,
        &result, &left));
    assert(!salts_simd_unary(
        &cmeta_vector_i32x4,
        SALTS_SIMD_UNARY_SQRT,
        &result, &left));
    assert(!salts_simd_reduce(
        &cmeta_vector_f32x4,
        SALTS_SIMD_REDUCE_ALL_TRUE,
        &left, &(uint32_t){0u}));
    assert(!salts_simd_saturating_binary(
        &cmeta_vector_i8x16,
        (salts_simd_saturating_op)99,
        &result, &left, &right));
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
    test_load_extend();
    test_load_splat_and_zero();
    test_lane_extract_replace();
    test_shuffle_and_swizzle();
    test_advanced_unary();
    test_advanced_min_max();
    test_pseudo_min_max();
    test_saturating_binary();
    test_reductions();
    test_widen_narrow_families();
    test_q15_and_dot();
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
