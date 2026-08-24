#ifndef TURBO_CMETA_DATA_H
#define TURBO_CMETA_DATA_H

#include "turbo_str.h"
#include "turbo_vstr.h"

#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

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
