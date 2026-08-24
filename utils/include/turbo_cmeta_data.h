#ifndef TURBO_CMETA_DATA_H
#define TURBO_CMETA_DATA_H

#include "turbo_str.h"
#include "turbo_uuid.h"
#include "turbo_vstr.h"

#include <cmeta/data.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define TURBO_CMETA_STATIC_ASSERT(condition_, message_) \
    static_assert((condition_), message_)
#else
#define TURBO_CMETA_STATIC_ASSERT(condition_, message_) \
    _Static_assert((condition_), message_)
#endif

TURBO_CMETA_STATIC_ASSERT(CHAR_BIT == 8,
                          "fixed-width CMeta metadata requires 8-bit bytes");
TURBO_CMETA_STATIC_ASSERT(sizeof(int8_t) * CHAR_BIT == 8u,
                          "int8_t must have exactly 8 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(uint8_t) * CHAR_BIT == 8u,
                          "uint8_t must have exactly 8 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(int16_t) * CHAR_BIT == 16u,
                          "int16_t must have exactly 16 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(uint16_t) * CHAR_BIT == 16u,
                          "uint16_t must have exactly 16 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(int32_t) * CHAR_BIT == 32u,
                          "int32_t must have exactly 32 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(uint32_t) * CHAR_BIT == 32u,
                          "uint32_t must have exactly 32 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(int64_t) * CHAR_BIT == 64u,
                          "int64_t must have exactly 64 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(uint64_t) * CHAR_BIT == 64u,
                          "uint64_t must have exactly 64 bits");
TURBO_CMETA_STATIC_ASSERT(sizeof(turbo_uuid_t) == TURBO_UUID_SIZE,
                          "turbo_uuid_t must have exactly 16 bytes");

#undef TURBO_CMETA_STATIC_ASSERT

#define TURBO_CMETA_DEFINE_INTEGER(name_, c_type_, data_kind_, bits_, id_)    \
    static const cmeta_type_identity name_##_cmeta_identity =                \
        CMETA_TYPE_ID_ATOM_INIT(id_);                                         \
    static const cmeta_type_desc name_##_cmeta_type = {                      \
        #c_type_, sizeof(c_type_), CMETA_ALIGNOF(c_type_), CMETA_T_INTEGER,   \
        NULL, NULL, &name_##_cmeta_identity                                   \
    };                                                                        \
    static const cmeta_data_integer_shape name_##_cmeta_shape = {bits_};      \
    static const cmeta_data_desc name_##_cmeta_data = {                      \
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,                \
        id_ ".data", #c_type_, data_kind_, &name_##_cmeta_type,             \
        &name_##_cmeta_shape, NULL, NULL, NULL                               \
    }

TURBO_CMETA_DEFINE_INTEGER(turbo_int8, int8_t, CMETA_DATA_SINT, 8u,
                           "turbo.int8");
TURBO_CMETA_DEFINE_INTEGER(turbo_int16, int16_t, CMETA_DATA_SINT, 16u,
                           "turbo.int16");
TURBO_CMETA_DEFINE_INTEGER(turbo_int32, int32_t, CMETA_DATA_SINT, 32u,
                           "turbo.int32");
TURBO_CMETA_DEFINE_INTEGER(turbo_int64, int64_t, CMETA_DATA_SINT, 64u,
                           "turbo.int64");
TURBO_CMETA_DEFINE_INTEGER(turbo_uint8, uint8_t, CMETA_DATA_UINT, 8u,
                           "turbo.uint8");
TURBO_CMETA_DEFINE_INTEGER(turbo_uint16, uint16_t, CMETA_DATA_UINT, 16u,
                           "turbo.uint16");
TURBO_CMETA_DEFINE_INTEGER(turbo_uint32, uint32_t, CMETA_DATA_UINT, 32u,
                           "turbo.uint32");
TURBO_CMETA_DEFINE_INTEGER(turbo_uint64, uint64_t, CMETA_DATA_UINT, 64u,
                           "turbo.uint64");

#undef TURBO_CMETA_DEFINE_INTEGER

static const cmeta_type_identity turbo_uuid_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("turbo.uuid");
static const cmeta_type_desc turbo_uuid_cmeta_type = {
    "turbo_uuid_t", sizeof(turbo_uuid_t), CMETA_ALIGNOF(turbo_uuid_t),
    CMETA_T_OBJECT, NULL, NULL, &turbo_uuid_cmeta_identity
};

static inline int turbo_uuid_cmeta_hex_value(unsigned char value) {
    if (value >= (unsigned char)'0' && value <= (unsigned char)'9')
        return (int)(value - (unsigned char)'0');
    if (value >= (unsigned char)'a' && value <= (unsigned char)'f')
        return (int)(value - (unsigned char)'a') + 10;
    if (value >= (unsigned char)'A' && value <= (unsigned char)'F')
        return (int)(value - (unsigned char)'A') + 10;
    return -1;
}

static inline bool turbo_uuid_cmeta_is_zero(const void *object) {
    static const turbo_uuid_t zero = {{0}};
    return object != NULL &&
           memcmp(((const turbo_uuid_t *)object)->bytes, zero.bytes,
                  TURBO_UUID_SIZE) == 0;
}

static inline cmeta_status turbo_uuid_cmeta_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    static const size_t group_ends[] = {4u, 6u, 8u, 10u};
    turbo_uuid_t parsed = {{0}};
    size_t text_index = 0u;
    size_t group_index = 0u;
    size_t byte_index;

    if (object == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;
    if (!turbo_uuid_cmeta_is_zero(object))
        return CMETA_INVALID_ARGUMENT;
    if (size != TURBO_UUID_STRING_LENGTH)
        return CMETA_INVALID_ARGUMENT;

    for (byte_index = 0u; byte_index < TURBO_UUID_SIZE; ++byte_index) {
        int high;
        int low;

        if (group_index < sizeof(group_ends) / sizeof(group_ends[0]) &&
            byte_index == group_ends[group_index]) {
            if (data[text_index] != (unsigned char)'-')
                return CMETA_INVALID_ARGUMENT;
            ++text_index;
            ++group_index;
        }

        high = turbo_uuid_cmeta_hex_value(data[text_index++]);
        low = turbo_uuid_cmeta_hex_value(data[text_index++]);
        if (high < 0 || low < 0)
            return CMETA_INVALID_ARGUMENT;
        parsed.bytes[byte_index] = (uint8_t)((high << 4) | low);
    }

    memcpy(object, &parsed, sizeof(parsed));
    return CMETA_OK;
}

static inline void turbo_uuid_cmeta_restore_zero(void *object) {
    if (object != NULL)
        memset(object, 0, sizeof(turbo_uuid_t));
}

static const cmeta_data_buffer_shape turbo_uuid_cmeta_shape = {
    CMETA_DATA_BUFFER_OWNED
};

/**
 * Owned UUID string adapter.
 *
 * Assignment parses an exact borrowed 36-byte canonical string into the
 * destination's fixed storage. No input address is retained and no allocation
 * occurs; restore clears all 16 bytes.
 */
static const cmeta_data_buffer_ops turbo_uuid_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &turbo_uuid_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    turbo_uuid_cmeta_is_zero, turbo_uuid_cmeta_assign,
    turbo_uuid_cmeta_restore_zero
};

static const cmeta_data_desc turbo_uuid_cmeta_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "turbo.uuid.data", "turbo_uuid_t", CMETA_DATA_STRING,
    &turbo_uuid_cmeta_type, &turbo_uuid_cmeta_shape,
    &turbo_uuid_cmeta_buffer_ops, NULL, NULL
};

static const cmeta_type_identity turbo_tstr_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("turbo.tstr");
static const cmeta_type_identity turbo_vstr_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("turbo.vstr");

/** Header-local metadata is compared semantically, never by address. */
static const cmeta_type_desc turbo_tstr_cmeta_type = {
    "tstr", sizeof(tstr), CMETA_ALIGNOF(tstr), CMETA_T_OBJECT,
    NULL, NULL, &turbo_tstr_cmeta_identity
};

static const cmeta_type_desc turbo_vstr_cmeta_type = {
    "vstr", sizeof(vstr), CMETA_ALIGNOF(vstr), CMETA_T_OBJECT,
    NULL, NULL, &turbo_vstr_cmeta_identity
};

static inline bool turbo_tstr_cmeta_is_zero(const void *object) {
    return object != NULL && *(const tstr *)object == NULL;
}

static inline cmeta_status turbo_tstr_cmeta_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    tstr value;

    if (object == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;
    if (*(tstr *)object != NULL)
        return CMETA_INVALID_ARGUMENT;
    if (size == 0u)
        return CMETA_OK;

    value = tstr_new_len(data, size);
    if (value == NULL)
        return CMETA_OUT_OF_MEMORY;
    *(tstr *)object = value;
    return CMETA_OK;
}

static inline void turbo_tstr_cmeta_restore_zero(void *object) {
    if (object != NULL)
        tstr_freep((tstr *)object);
}

static inline bool turbo_vstr_cmeta_is_zero(const void *object) {
    const vstr *value = (const vstr *)object;
    return value != NULL && value->data == NULL && value->len == 0u;
}

static inline cmeta_status turbo_vstr_cmeta_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    vstr *value = (vstr *)object;

    if (value == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;
    if (value->data != NULL || value->len != 0u)
        return CMETA_INVALID_ARGUMENT;

    value->data = size == 0u ? NULL : (const char *)data;
    value->len = size;
    return CMETA_OK;
}

static inline void turbo_vstr_cmeta_restore_zero(void *object) {
    vstr *value = (vstr *)object;
    if (value != NULL) {
        value->data = NULL;
        value->len = 0u;
    }
}

/**
 * Unique-owned, binary-safe tstr storage adapter.
 *
 * Zero is NULL. Assignment copies exact bytes and restore frees the tstr.
 */
static const cmeta_data_buffer_ops turbo_tstr_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &turbo_tstr_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    turbo_tstr_cmeta_is_zero, turbo_tstr_cmeta_assign,
    turbo_tstr_cmeta_restore_zero
};

/**
 * Borrowed vstr storage adapter.
 *
 * Zero is {NULL, 0}. Assignment stores the address without extending its
 * lifetime; the caller must enforce an appropriate source-lifetime contract.
 */
static const cmeta_data_buffer_ops turbo_vstr_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &turbo_vstr_cmeta_type, CMETA_DATA_BUFFER_BORROWED,
    turbo_vstr_cmeta_is_zero, turbo_vstr_cmeta_assign,
    turbo_vstr_cmeta_restore_zero
};

#ifdef __cplusplus
}
#endif

#endif /* TURBO_CMETA_DATA_H */
