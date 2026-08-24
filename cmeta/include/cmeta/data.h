#ifndef CMETA_DATA_H
#define CMETA_DATA_H

#include <cmeta/cmeta.h>
#include <cmeta/struct.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cmeta_data_kind {
    CMETA_DATA_BOOL,
    CMETA_DATA_SINT,
    CMETA_DATA_UINT,
    CMETA_DATA_FLOAT,
    CMETA_DATA_STRING,
    CMETA_DATA_BYTES,
    CMETA_DATA_ENUM,
    CMETA_DATA_STRUCT,
    CMETA_DATA_VARIANT,
    CMETA_DATA_SEQUENCE,
    CMETA_DATA_SET,
    CMETA_DATA_MAP,
    CMETA_DATA_CUSTOM
} cmeta_data_kind;

enum {
    CMETA_DATA_DESC_ABI_VERSION = 1u
};

typedef struct cmeta_data_buffer_ops cmeta_data_buffer_ops;
typedef struct cmeta_data_enum_ops cmeta_data_enum_ops;
typedef struct cmeta_data_variant_ops cmeta_data_variant_ops;

typedef struct cmeta_data_desc {
    size_t struct_size;
    uint32_t abi_version;
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const cmeta_type_desc *storage_type;
    const void *shape;
    const cmeta_data_buffer_ops *buffer_ops;
    const cmeta_data_enum_ops *enum_ops;
    const cmeta_data_variant_ops *variant_ops;
} cmeta_data_desc;

typedef struct cmeta_data_integer_shape {
    uint8_t bits;
} cmeta_data_integer_shape;

typedef struct cmeta_data_float_shape {
    uint8_t bits;
} cmeta_data_float_shape;

typedef enum cmeta_data_buffer_ownership {
    CMETA_DATA_BUFFER_OWNED,
    CMETA_DATA_BUFFER_BORROWED,
    CMETA_DATA_BUFFER_CUSTOM
} cmeta_data_buffer_ownership;

typedef struct cmeta_data_buffer_shape {
    cmeta_data_buffer_ownership ownership;
} cmeta_data_buffer_shape;

enum {
    CMETA_DATA_BUFFER_OPS_ABI_VERSION = 1u
};

typedef bool (*cmeta_data_buffer_is_zero_fn)(const void *object);
typedef cmeta_status (*cmeta_data_buffer_assign_fn)(
    void *object, const unsigned char *data, size_t size, size_t max_bytes);
typedef void (*cmeta_data_buffer_restore_zero_fn)(void *object);

struct cmeta_data_buffer_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_buffer_ownership ownership;
    cmeta_data_buffer_is_zero_fn is_zero;
    cmeta_data_buffer_assign_fn assign;
    cmeta_data_buffer_restore_zero_fn restore_zero;
};

enum {
    CMETA_DATA_ENUM_OPS_ABI_VERSION = 1u
};

typedef bool (*cmeta_data_enum_is_zero_fn)(const void *object);
typedef cmeta_status (*cmeta_data_enum_read_fn)(const void *object,
                                                int64_t *out);
typedef cmeta_status (*cmeta_data_enum_assign_fn)(void *object,
                                                  int64_t value);
typedef void (*cmeta_data_enum_restore_zero_fn)(void *object);

struct cmeta_data_enum_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_enum_is_zero_fn is_zero;
    cmeta_data_enum_read_fn read;
    cmeta_data_enum_assign_fn assign;
    cmeta_data_enum_restore_zero_fn restore_zero;
};

enum {
    CMETA_DATA_VARIANT_OPS_ABI_VERSION = 1u
};

typedef bool (*cmeta_data_variant_is_zero_fn)(const void *object);
typedef cmeta_status (*cmeta_data_variant_active_tag_fn)(
    const void *object, int64_t *out);
typedef cmeta_status (*cmeta_data_variant_select_fn)(void *object,
                                                     int64_t tag);
typedef void (*cmeta_data_variant_restore_zero_fn)(void *object);

struct cmeta_data_variant_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_variant_is_zero_fn is_zero;
    cmeta_data_variant_active_tag_fn active_tag;
    cmeta_data_variant_select_fn select;
    cmeta_data_variant_restore_zero_fn restore_zero;
};

typedef struct cmeta_data_enum_shape {
    const cmeta_enum_desc *meta;
} cmeta_data_enum_shape;

typedef struct cmeta_data_field_desc {
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_field_desc;

typedef struct cmeta_data_struct_shape {
    const cmeta_struct_desc *layout;
    const cmeta_data_field_desc *fields;
    size_t field_count;
} cmeta_data_struct_shape;

typedef struct cmeta_data_variant_case {
    int64_t tag;
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_variant_case;

typedef struct cmeta_data_variant_shape {
    size_t tag_offset;
    const cmeta_data_desc *tag;
    const cmeta_data_variant_case *cases;
    size_t case_count;
} cmeta_data_variant_shape;

bool cmeta_data_kind_valid(cmeta_data_kind kind);
bool cmeta_data_kind_is_container(cmeta_data_kind kind);
bool cmeta_data_desc_valid(const cmeta_data_desc *desc);

/**
 * Return a validated STRING/BYTES adapter, or NULL when the descriptor does
 * not expose a complete, matching v1 adapter. Descriptor and adapter addresses
 * are not type identities; storage types are compared semantically and then
 * checked for exact kind, size, and alignment.
 */
const cmeta_data_buffer_ops *cmeta_data_buffer_ops_of(
    const cmeta_data_desc *desc);

/**
 * Query the provider-defined semantic zero state.
 *
 * @param desc STRING/BYTES descriptor with an accessible matching adapter.
 * @param object Address of one object of desc->storage_type.
 * @param out Receives true only when object is in semantic zero state.
 * @return CMETA_OK, CMETA_INVALID_ARGUMENT, or CMETA_TYPE_MISMATCH.
 */
cmeta_status cmeta_data_buffer_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out);

/**
 * Assign one bounded byte slice to a semantic-zero destination.
 *
 * The provider chooses copy or borrow semantics from its ownership contract.
 * On failure the checked facade invokes restore_zero before returning.
 *
 * @param data Borrowed input for this call; may be NULL only when size is zero.
 * @param size Input byte count.
 * @param max_bytes Hard per-value byte limit; larger input is rejected before
 *        the provider callback.
 * @return The exact provider status, CMETA_CAPACITY_EXCEEDED for the bound,
 *         or a validation error.
 *
 * Example: `cmeta_data_buffer_assign(desc, &value, bytes, count, 4096u)`.
 */
cmeta_status cmeta_data_buffer_assign(
    const cmeta_data_desc *desc, void *object,
    const unsigned char *data, size_t size, size_t max_bytes);

/**
 * Release owned state or clear borrowed state, then verify semantic zero.
 *
 * @return CMETA_OK, a descriptor validation error, or CMETA_CALLBACK_ERROR
 *         when the provider did not establish its declared zero state.
 */
cmeta_status cmeta_data_buffer_restore_zero(
    const cmeta_data_desc *desc, void *object);

/** Return a complete, storage-matching enum adapter, or NULL. */
const cmeta_data_enum_ops *cmeta_data_enum_ops_of(
    const cmeta_data_desc *desc);

/** Query the provider-defined semantic-zero state of an enum object. */
cmeta_status cmeta_data_enum_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out);

/** Read one enum object as its reflected signed 64-bit value. */
cmeta_status cmeta_data_enum_read(
    const cmeta_data_desc *desc, const void *object, int64_t *out);

/**
 * Assign a declared enum value to a semantic-zero destination.
 *
 * Provider failure or a violated read-back postcondition restores zero before
 * returning.
 */
cmeta_status cmeta_data_enum_assign(
    const cmeta_data_desc *desc, void *object, int64_t value);

/** Restore and verify the provider-defined enum semantic-zero state. */
cmeta_status cmeta_data_enum_restore_zero(
    const cmeta_data_desc *desc, void *object);

/** Return a complete, storage-matching variant adapter, or NULL. */
const cmeta_data_variant_ops *cmeta_data_variant_ops_of(
    const cmeta_data_desc *desc);

/** Query the provider-defined unselected semantic-zero state. */
cmeta_status cmeta_data_variant_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out);

/** Read the active tag; an unselected object is not an active variant. */
cmeta_status cmeta_data_variant_active_tag(
    const cmeta_data_desc *desc, const void *object, int64_t *out);

/**
 * Select a declared case in a semantic-zero object and verify engagement.
 *
 * On provider failure or postcondition violation the object is restored to
 * semantic zero before returning.
 */
cmeta_status cmeta_data_variant_select(
    const cmeta_data_desc *desc, void *object, int64_t tag);

/** Destroy any active payload, restore semantic zero, and verify the result. */
cmeta_status cmeta_data_variant_restore_zero(
    const cmeta_data_desc *desc, void *object);

const cmeta_data_field_desc *cmeta_data_struct_field(
    const cmeta_data_struct_shape *shape, size_t index);
const cmeta_data_field_desc *cmeta_data_struct_find_field(
    const cmeta_data_struct_shape *shape, const char *name);
const cmeta_data_variant_case *cmeta_data_variant_case_by_tag(
    const cmeta_data_variant_shape *shape, int64_t tag);

extern const cmeta_data_desc cmeta_data_bool;
extern const cmeta_data_desc cmeta_data_int;
extern const cmeta_data_desc cmeta_data_long;
extern const cmeta_data_desc cmeta_data_size;
extern const cmeta_data_desc cmeta_data_float;
extern const cmeta_data_desc cmeta_data_double;

extern const cmeta_data_desc cmeta_data_sequence;
extern const cmeta_data_desc cmeta_data_set;
extern const cmeta_data_desc cmeta_data_map;

#ifdef __cplusplus
}
#endif

#endif /* CMETA_DATA_H */
