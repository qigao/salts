#ifndef CMETA_VECTOR_H
#define CMETA_VECTOR_H

#include <cmeta/cmeta.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cmeta_vector_lane_kind {
    CMETA_VECTOR_I8 = 0,
    CMETA_VECTOR_U8,
    CMETA_VECTOR_I16,
    CMETA_VECTOR_U16,
    CMETA_VECTOR_I32,
    CMETA_VECTOR_U32,
    CMETA_VECTOR_I64,
    CMETA_VECTOR_U64,
    CMETA_VECTOR_F32,
    CMETA_VECTOR_F64,
    CMETA_VECTOR_BOOL
} cmeta_vector_lane_kind;

/* Neutral ABI/storage carrier.  The semantic lane interpretation lives in
 * cmeta_vector_desc; consumers must not infer it from these bytes. */
typedef struct cmeta_v128_storage {
#if defined(__cplusplus)
    alignas(16) unsigned char bytes[16];
#else
    _Alignas(16) unsigned char bytes[16];
#endif
} cmeta_v128_storage;

typedef struct cmeta_vector_desc {
    const cmeta_type_desc *type;
    cmeta_vector_lane_kind lane_kind;
    uint16_t lane_bits;
    uint16_t lane_count;
    bool is_mask;
} cmeta_vector_desc;

extern const cmeta_type_desc cmeta_type_i8x16;
extern const cmeta_type_desc cmeta_type_u8x16;
extern const cmeta_type_desc cmeta_type_i16x8;
extern const cmeta_type_desc cmeta_type_u16x8;
extern const cmeta_type_desc cmeta_type_i32x4;
extern const cmeta_type_desc cmeta_type_u32x4;
extern const cmeta_type_desc cmeta_type_i64x2;
extern const cmeta_type_desc cmeta_type_u64x2;
extern const cmeta_type_desc cmeta_type_f32x4;
extern const cmeta_type_desc cmeta_type_f64x2;
extern const cmeta_type_desc cmeta_type_b8x16;
extern const cmeta_type_desc cmeta_type_b16x8;
extern const cmeta_type_desc cmeta_type_b32x4;
extern const cmeta_type_desc cmeta_type_b64x2;

extern const cmeta_vector_desc cmeta_vector_i8x16;
extern const cmeta_vector_desc cmeta_vector_u8x16;
extern const cmeta_vector_desc cmeta_vector_i16x8;
extern const cmeta_vector_desc cmeta_vector_u16x8;
extern const cmeta_vector_desc cmeta_vector_i32x4;
extern const cmeta_vector_desc cmeta_vector_u32x4;
extern const cmeta_vector_desc cmeta_vector_i64x2;
extern const cmeta_vector_desc cmeta_vector_u64x2;
extern const cmeta_vector_desc cmeta_vector_f32x4;
extern const cmeta_vector_desc cmeta_vector_f64x2;
extern const cmeta_vector_desc cmeta_vector_b8x16;
extern const cmeta_vector_desc cmeta_vector_b16x8;
extern const cmeta_vector_desc cmeta_vector_b32x4;
extern const cmeta_vector_desc cmeta_vector_b64x2;

bool cmeta_vector_desc_valid(const cmeta_vector_desc *desc);
const cmeta_vector_desc *cmeta_vector_desc_for_type(const cmeta_type_desc *type);
bool cmeta_type_is_vector(const cmeta_type_desc *type);
bool cmeta_type_is_mask(const cmeta_type_desc *type);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_VECTOR_H */
