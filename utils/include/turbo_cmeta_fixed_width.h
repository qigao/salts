#ifndef TURBO_CMETA_FIXED_WIDTH_H
#define TURBO_CMETA_FIXED_WIDTH_H

#include <cmeta/data.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
  #define TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(condition_, message_)                              \
    static_assert((condition_), message_)
#else
  #define TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(condition_, message_)                              \
    _Static_assert((condition_), message_)
#endif

TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(CHAR_BIT == 8,
                                      "fixed-width CMeta metadata requires 8-bit bytes");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int8_t) * CHAR_BIT == 8u,
                                      "int8_t must have exactly 8 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint8_t) * CHAR_BIT == 8u,
                                      "uint8_t must have exactly 8 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int16_t) * CHAR_BIT == 16u,
                                      "int16_t must have exactly 16 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint16_t) * CHAR_BIT == 16u,
                                      "uint16_t must have exactly 16 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int32_t) * CHAR_BIT == 32u,
                                      "int32_t must have exactly 32 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint32_t) * CHAR_BIT == 32u,
                                      "uint32_t must have exactly 32 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int64_t) * CHAR_BIT == 64u,
                                      "int64_t must have exactly 64 bits");
TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint64_t) * CHAR_BIT == 64u,
                                      "uint64_t must have exactly 64 bits");

#undef TURBO_CMETA_FIXED_WIDTH_STATIC_ASSERT

#define TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(name_, c_type_, data_kind_, bits_, id_)             \
  static const cmeta_type_identity name_##_cmeta_identity = CMETA_TYPE_ID_ATOM_INIT(id_);          \
  static const cmeta_type_desc name_##_cmeta_type = {                                              \
      #c_type_, sizeof(c_type_),        CMETA_ALIGNOF(c_type_), CMETA_T_INTEGER, NULL,             \
      NULL,     &name_##_cmeta_identity};                                                          \
  static const cmeta_data_integer_shape name_##_cmeta_shape = {bits_};                             \
  static const cmeta_data_desc name_##_cmeta_data = {                                              \
      sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION, id_ ".data", #c_type_, data_kind_,     \
      &name_##_cmeta_type,     &name_##_cmeta_shape,        NULL,        NULL,     NULL}

TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_int8, int8_t, CMETA_DATA_SINT, 8u, "turbo.int8");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_int16, int16_t, CMETA_DATA_SINT, 16u, "turbo.int16");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_int32, int32_t, CMETA_DATA_SINT, 32u, "turbo.int32");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_int64, int64_t, CMETA_DATA_SINT, 64u, "turbo.int64");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_uint8, uint8_t, CMETA_DATA_UINT, 8u, "turbo.uint8");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_uint16, uint16_t, CMETA_DATA_UINT, 16u,
                                       "turbo.uint16");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_uint32, uint32_t, CMETA_DATA_UINT, 32u,
                                       "turbo.uint32");
TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER(turbo_uint64, uint64_t, CMETA_DATA_UINT, 64u,
                                       "turbo.uint64");

#undef TURBO_CMETA_DEFINE_FIXED_WIDTH_INTEGER

#endif /* TURBO_CMETA_FIXED_WIDTH_H */
