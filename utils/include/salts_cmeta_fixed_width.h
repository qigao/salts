#ifndef SALTS_CMETA_FIXED_WIDTH_H
#define SALTS_CMETA_FIXED_WIDTH_H

#include <cmeta/data.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
  #define SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(condition_, message_)                              \
    static_assert((condition_), message_)
#else
  #define SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(condition_, message_)                              \
    _Static_assert((condition_), message_)
#endif

SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(CHAR_BIT == 8,
                                      "fixed-width CMeta metadata requires 8-bit bytes");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int8_t) * CHAR_BIT == 8u,
                                      "int8_t must have exactly 8 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint8_t) * CHAR_BIT == 8u,
                                      "uint8_t must have exactly 8 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int16_t) * CHAR_BIT == 16u,
                                      "int16_t must have exactly 16 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint16_t) * CHAR_BIT == 16u,
                                      "uint16_t must have exactly 16 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int32_t) * CHAR_BIT == 32u,
                                      "int32_t must have exactly 32 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint32_t) * CHAR_BIT == 32u,
                                      "uint32_t must have exactly 32 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(int64_t) * CHAR_BIT == 64u,
                                      "int64_t must have exactly 64 bits");
SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT(sizeof(uint64_t) * CHAR_BIT == 64u,
                                      "uint64_t must have exactly 64 bits");

#undef SALTS_CMETA_FIXED_WIDTH_STATIC_ASSERT

static const cmeta_type_identity salts_bool8_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("salts.bool8");
static const cmeta_type_desc salts_bool8_cmeta_type = {
    "uint8_t", sizeof(uint8_t), CMETA_ALIGNOF(uint8_t), CMETA_T_INTEGER,
    NULL, NULL, &salts_bool8_cmeta_identity
};

static inline bool salts_bool8_cmeta_is_zero(const void *object) {
  return object != NULL && *(const uint8_t *)object == 0u;
}

static inline cmeta_status salts_bool8_cmeta_copy(void *destination,
                                                  const void *source) {
  uint8_t value;
  if (destination == NULL || source == NULL)
    return CMETA_INVALID_ARGUMENT;
  value = *(const uint8_t *)source;
  if (value > 1u) return CMETA_INVALID_ARGUMENT;
  *(uint8_t *)destination = value;
  return CMETA_OK;
}

static inline void salts_bool8_cmeta_restore_zero(void *object) {
  if (object != NULL)
    *(uint8_t *)object = 0u;
}

/** Canonical octet-backed Bool provider for schemas with an 8-bit native ABI. */
static const cmeta_data_fixed_ops salts_bool8_cmeta_fixed_ops = {
    sizeof(cmeta_data_fixed_ops), CMETA_DATA_FIXED_OPS_ABI_VERSION,
    &salts_bool8_cmeta_type, sizeof(uint8_t), salts_bool8_cmeta_is_zero,
    salts_bool8_cmeta_copy, salts_bool8_cmeta_restore_zero
};

static const cmeta_data_desc salts_bool8_cmeta_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "salts.bool8.data", "uint8_t Bool", CMETA_DATA_BOOL,
    &salts_bool8_cmeta_type, NULL, NULL, NULL, NULL,
    &salts_bool8_cmeta_fixed_ops, NULL, NULL, NULL, NULL
};

/*
 * Exact-width integer authority lives in CMeta core. Keep no Salts-private
 * duplicate descriptors here: consumers use cmeta_type_int32 /
 * cmeta_data_int32 (and the corresponding widths/signs) directly.
 */


#endif /* SALTS_CMETA_FIXED_WIDTH_H */
