#ifndef SALTS_CMETA_DATA_H
#define SALTS_CMETA_DATA_H

#include "salts_cmeta_fixed_width.h"
#include "salts_str.h"
#include "salts_api.h"
#include "salts_uuid.h"
#include "salts_vstr.h"

#include <cmeta/data.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define SALTS_CMETA_STATIC_ASSERT(condition_, message_) \
    static_assert((condition_), message_)
#else
#define SALTS_CMETA_STATIC_ASSERT(condition_, message_) \
    _Static_assert((condition_), message_)
#endif

SALTS_CMETA_STATIC_ASSERT(sizeof(salts_uuid_t) == SALTS_UUID_SIZE,
                          "salts_uuid_t must have exactly 16 bytes");

#undef SALTS_CMETA_STATIC_ASSERT

/**
 * Canonical process-wide UUID metadata exported by Salts::Core.
 *
 * These objects have stable linkage identities across translation units.
 * Semantic admission still uses salts_uuid_cmeta_data_valid rather than
 * descriptor address equality so intact metadata copies remain valid.
 */
SALTS_API extern const cmeta_type_desc salts_uuid_cmeta_type;
SALTS_API extern const cmeta_data_buffer_shape salts_uuid_cmeta_shape;
SALTS_API extern const cmeta_data_buffer_ops salts_uuid_cmeta_buffer_ops;
SALTS_API extern const cmeta_data_desc salts_uuid_cmeta_data;

/**
 * Validate UUID metadata against the canonical Core callback authority.
 *
 * Intact semantic copies are accepted; callback replacements are rejected.
 *
 * @param candidate Trusted immutable generic descriptor, or NULL.
 * @return true only when candidate retains the provider's UUID semantics.
 */
SALTS_API bool salts_uuid_cmeta_data_valid(
    const cmeta_data_desc *candidate);

static const cmeta_type_identity salts_tstr_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("salts.tstr");
static const cmeta_type_identity salts_vstr_cmeta_identity =
    CMETA_TYPE_ID_ATOM_INIT("salts.vstr");

/** Header-local metadata is compared semantically, never by address. */
static const cmeta_type_desc salts_tstr_cmeta_type = {
    "tstr", sizeof(tstr), CMETA_ALIGNOF(tstr), CMETA_T_OBJECT,
    NULL, NULL, &salts_tstr_cmeta_identity
};

static const cmeta_type_desc salts_vstr_cmeta_type = {
    "vstr", sizeof(vstr), CMETA_ALIGNOF(vstr), CMETA_T_OBJECT,
    NULL, NULL, &salts_vstr_cmeta_identity
};

static inline bool salts_tstr_cmeta_is_zero(const void *object) {
    return object != NULL && *(const tstr *)object == NULL;
}

static inline cmeta_status salts_tstr_cmeta_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const tstr value = object != NULL ? *(const tstr *)object : NULL;
    if (object == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out_data = (const unsigned char *)value;
    *out_size = tstr_len(value);
    return CMETA_OK;
}

static inline cmeta_status salts_tstr_cmeta_assign(
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

static inline void salts_tstr_cmeta_restore_zero(void *object) {
    if (object != NULL)
        tstr_freep((tstr *)object);
}

static inline bool salts_vstr_cmeta_is_zero(const void *object) {
    const vstr *value = (const vstr *)object;
    return value != NULL && value->data == NULL && value->len == 0u;
}

static inline cmeta_status salts_vstr_cmeta_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const vstr *value = (const vstr *)object;
    if (value == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (value->len != 0u && value->data == NULL)
        return CMETA_CALLBACK_ERROR;
    *out_data = (const unsigned char *)value->data;
    *out_size = value->len;
    return CMETA_OK;
}

static inline cmeta_status salts_vstr_cmeta_assign(
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

static inline void salts_vstr_cmeta_restore_zero(void *object) {
    vstr *value = (vstr *)object;
    if (value != NULL) {
        value->data = NULL;
        value->len = 0u;
    }
}

/**
 * Unique-owned, binary-safe tstr storage adapter.
 *
 * Zero is NULL. Assignment copies exact bytes, read borrows the current byte
 * span, and restore frees the tstr. The read view expires on any mutation or
 * restore of the source tstr.
 */
static const cmeta_data_buffer_ops salts_tstr_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &salts_tstr_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    salts_tstr_cmeta_is_zero, salts_tstr_cmeta_assign,
    salts_tstr_cmeta_restore_zero, salts_tstr_cmeta_read
};

/**
 * Borrowed vstr storage adapter.
 *
 * Zero is {NULL, 0}. Assignment stores the address without extending its
 * lifetime; the caller must enforce an appropriate source-lifetime contract.
 * Read returns that same borrowed span and does not extend its lifetime.
 */
static const cmeta_data_buffer_ops salts_vstr_cmeta_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &salts_vstr_cmeta_type, CMETA_DATA_BUFFER_BORROWED,
    salts_vstr_cmeta_is_zero, salts_vstr_cmeta_assign,
    salts_vstr_cmeta_restore_zero, salts_vstr_cmeta_read
};

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CMETA_DATA_H */
