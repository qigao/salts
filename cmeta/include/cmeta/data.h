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
typedef struct cmeta_data_enum_bits_ops cmeta_data_enum_bits_ops;
typedef struct cmeta_data_variant_ops cmeta_data_variant_ops;
typedef struct cmeta_data_fixed_ops cmeta_data_fixed_ops;
typedef struct cmeta_data_collection_ops cmeta_data_collection_ops;
typedef struct cmeta_data_map_ops cmeta_data_map_ops;

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
    /** Optional exact native-value operations appended to the v1 prefix. */
    const cmeta_data_fixed_ops *fixed_ops;
    /** Canonical enum domain provider; mutually exclusive with shape/enum_ops. */
    const cmeta_data_enum_bits_ops *enum_bits_ops;
    /** Optional provider-neutral collection read adapter. */
    const cmeta_data_collection_ops *collection_ops;
    /** Optional provider-neutral map key/value adapter. */
    const cmeta_data_map_ops *map_ops;
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
    CMETA_DATA_BUFFER_OPS_ABI_VERSION = 2u
};

typedef bool (*cmeta_data_buffer_is_zero_fn)(const void *object);
typedef cmeta_status (*cmeta_data_buffer_assign_fn)(
    void *object, const unsigned char *data, size_t size, size_t max_bytes);
typedef void (*cmeta_data_buffer_restore_zero_fn)(void *object);
/**
 * Return one provider-owned immutable byte span for the callback duration.
 *
 * Success initializes both outputs. A non-empty span has non-NULL data. The
 * provider does not transfer ownership or extend the span lifetime; callers
 * follow the enclosing object's documented mutation and destruction rules.
 */
typedef cmeta_status (*cmeta_data_buffer_read_fn)(
    const void *object, const unsigned char **out_data, size_t *out_size);
/** Initialize one raw storage slot to the provider-defined semantic zero. */
typedef cmeta_status (*cmeta_data_buffer_init_zero_fn)(void *object);
/**
 * Move one complete provider value into a semantic-zero destination.
 *
 * This operation is no-fail. It transfers provider-owned state without
 * allocation and leaves source in semantic zero. Both objects remain valid for
 * restore_zero after the call.
 */
typedef void (*cmeta_data_buffer_move_fn)(void *destination, void *source);

/**
 * Version-2 native buffer lifecycle provider.
 *
 * v2 is intentionally strict: init_zero and move are mandatory and providers
 * compiled against v1 are not admitted. read remains optional because some
 * exact fixed/native values use buffer assignment only and expose their read
 * representation through a separate canonical authority. There is no v1
 * fallback path.
 */
struct cmeta_data_buffer_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_buffer_ownership ownership;
    cmeta_data_buffer_is_zero_fn is_zero;
    cmeta_data_buffer_assign_fn assign;
    cmeta_data_buffer_restore_zero_fn restore_zero;
    cmeta_data_buffer_read_fn read;
    cmeta_data_buffer_init_zero_fn init_zero;
    cmeta_data_buffer_move_fn move;
};

enum {
    CMETA_DATA_FIXED_OPS_ABI_VERSION = 1u
};

/**
 * Exact native-value provider contract.
 *
 * extent is the complete native object extent and must exactly match the
 * associated storage type. copy receives one source object of that extent and
 * a semantic-zero destination. A failed copy may partially mutate the
 * destination; the checked facade restores it to semantic zero before
 * returning. restore_zero is no-fail, idempotent, and accepts partially
 * initialized objects.
 */
typedef bool (*cmeta_data_fixed_is_zero_fn)(const void *object);
typedef cmeta_status (*cmeta_data_fixed_copy_fn)(void *destination,
                                                 const void *source);
typedef void (*cmeta_data_fixed_restore_zero_fn)(void *object);

struct cmeta_data_fixed_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    size_t extent;
    cmeta_data_fixed_is_zero_fn is_zero;
    cmeta_data_fixed_copy_fn copy;
    cmeta_data_fixed_restore_zero_fn restore_zero;
};

/**
 * Define header-local canonical metadata for one bounded inline byte value.
 *
 * storage_type must be a complete native type, including an array typedef when
 * the native value is an array. extent is explicit and compilation fails when
 * it differs from sizeof(storage_type). The generated semantic zero is the
 * all-zero byte representation; copy and restore neither allocate nor retain
 * pointers. Repeated declarations in separate translation units compare by
 * the supplied stable type identity rather than by descriptor address.
 */
#ifdef __cplusplus
#define CMETA_DATA_BYTES_MUTABLE(object_) \
    reinterpret_cast<unsigned char *>(object_)
#define CMETA_DATA_BYTES_CONST(object_) \
    reinterpret_cast<const unsigned char *>(object_)
#else
#define CMETA_DATA_BYTES_MUTABLE(object_) ((unsigned char *)(object_))
#define CMETA_DATA_BYTES_CONST(object_) ((const unsigned char *)(object_))
#endif

#define CMETA_DEFINE_FIXED_BYTES(name_, storage_type_, extent_, stable_id_,  \
                                 display_name_)                              \
    typedef char name_##_cmeta_extent_must_match_storage[                    \
        ((extent_) > 0u && sizeof(storage_type_) == (extent_)) ? 1 : -1];    \
    static inline bool name_##_cmeta_is_zero(const void *object_) {          \
        const unsigned char *bytes_;                                         \
        size_t index_;                                                       \
        if (object_ == NULL)                                                 \
            return false;                                                    \
        bytes_ = CMETA_DATA_BYTES_CONST(object_);                            \
        for (index_ = 0u; index_ < (extent_); ++index_)                      \
            if (bytes_[index_] != 0u)                                        \
                return false;                                                \
        return true;                                                         \
    }                                                                        \
    static inline cmeta_status name_##_cmeta_copy(                           \
        void *destination_, const void *source_) {                           \
        unsigned char *destination_bytes_;                                   \
        const unsigned char *source_bytes_;                                  \
        size_t index_;                                                       \
        if (destination_ == NULL || source_ == NULL)                         \
            return CMETA_INVALID_ARGUMENT;                                  \
        destination_bytes_ = CMETA_DATA_BYTES_MUTABLE(destination_);         \
        source_bytes_ = CMETA_DATA_BYTES_CONST(source_);                     \
        for (index_ = 0u; index_ < (extent_); ++index_)                      \
            destination_bytes_[index_] = source_bytes_[index_];              \
        return CMETA_OK;                                                     \
    }                                                                        \
    static inline void name_##_cmeta_restore_zero(void *object_) {           \
        unsigned char *bytes_;                                               \
        size_t index_;                                                       \
        if (object_ == NULL)                                                 \
            return;                                                          \
        bytes_ = CMETA_DATA_BYTES_MUTABLE(object_);                          \
        for (index_ = 0u; index_ < (extent_); ++index_)                      \
            bytes_[index_] = 0u;                                             \
    }                                                                        \
    static const cmeta_type_identity name_##_cmeta_identity =                \
        CMETA_TYPE_ID_ATOM_INIT(stable_id_);                                 \
    static const cmeta_type_desc name_##_cmeta_type = {                      \
        #storage_type_, sizeof(storage_type_), CMETA_ALIGNOF(storage_type_), \
        CMETA_T_OBJECT, NULL, NULL, &name_##_cmeta_identity};                \
    static const cmeta_data_buffer_shape name_##_cmeta_shape = {             \
        CMETA_DATA_BUFFER_OWNED};                                            \
    static const cmeta_data_fixed_ops name_##_cmeta_fixed_ops = {            \
        sizeof(cmeta_data_fixed_ops), CMETA_DATA_FIXED_OPS_ABI_VERSION,      \
        &name_##_cmeta_type, (extent_), name_##_cmeta_is_zero,               \
        name_##_cmeta_copy, name_##_cmeta_restore_zero};                     \
    static const cmeta_data_desc name_##_cmeta_data = {                      \
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,                \
        stable_id_ ".data", display_name_, CMETA_DATA_BYTES,               \
        &name_##_cmeta_type, &name_##_cmeta_shape, NULL, NULL, NULL,         \
        &name_##_cmeta_fixed_ops, NULL}

enum {
    CMETA_DATA_ENUM_OPS_ABI_VERSION = 1u
};

/**
 * Enum adapter callback contract.
 *
 * Every object points to one correctly aligned instance of storage_type.
 * is_zero and read do not mutate it. On successful read, out must receive a
 * value declared by the descriptor's enum metadata. assign is called only for
 * a semantic-zero object and a declared value; success must make read return
 * that exact value. assign may partially mutate on failure because the checked
 * facade always follows failure with restore_zero.
 *
 * restore_zero is a no-fail cleanup callback. It must release provider-owned
 * resources, be safe for both zero and partially/fully initialized objects,
 * be idempotent, and leave is_zero(object) true.
 */
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

enum { CMETA_DATA_ENUM_BITS_OPS_ABI_VERSION = 1u };
typedef cmeta_status (*cmeta_data_enum_read_bits_fn)(const void *object,
                                                    uint64_t *out);
typedef cmeta_status (*cmeta_data_enum_assign_bits_fn)(void *object,
                                                      uint64_t bits);

/**
 * Canonical enum provider, never routed through the legacy int64_t adapter.
 * The descriptor sets shape and enum_ops to NULL and appends enum_bits_ops.
 * domain is the sole membership/width/flags authority, not storage_type.
 * Object alignment, ownership and lifetime follow storage_type. Metadata is
 * immutable and borrowed; callbacks retain no arguments. Callers synchronize
 * mutable objects; the facade provides no concurrent atomicity.
 *
 * is_zero/read never mutate the object. read initializes out on success.
 * assign receives a semantic-zero destination and validated canonical bits;
 * success must read back exactly those bits. Failure may partially mutate.
 * restore_zero is no-fail, idempotent, handles partial objects and releases
 * provider-owned resources. Failure rollback guarantees semantic zero, not
 * byte-for-byte restoration. A broken cleanup callback cannot be repaired by
 * the facade and is reported as CMETA_CALLBACK_ERROR.
 */
struct cmeta_data_enum_bits_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    const cmeta_enum_domain *domain;
    cmeta_data_enum_is_zero_fn is_zero;
    cmeta_data_enum_read_bits_fn read;
    cmeta_data_enum_assign_bits_fn assign;
    cmeta_data_enum_restore_zero_fn restore_zero;
};

enum {
    CMETA_DATA_VARIANT_OPS_ABI_VERSION = 1u
};

/**
 * Variant adapter callback contract.
 *
 * Every object points to one correctly aligned instance of storage_type.
 * is_zero and active_tag do not mutate it. active_tag is called only for an
 * active object and, on success, must return a declared case tag. select is
 * called only for a semantic-zero object and a declared tag. On success it
 * must engage exactly that tag and initialize the selected payload to the
 * payload descriptor's semantic-zero state. It may partially mutate on
 * failure because the checked facade always follows failure with restore_zero.
 *
 * restore_zero is the single no-fail destruction path. It must release the
 * active payload and all provider-owned resources, accept zero or partially
 * initialized objects, be idempotent, and leave is_zero(object) true. Callback
 * implementations must not retain object or out pointers after returning.
 */
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

/*
 * Provider-neutral collection reflection.
 *
 * A view borrows immutable element/key/value storage for the duration of one
 * consumer operation. It does not imply ownership, mutability, contiguity of
 * the provider's native container, or any CSTL/VM-specific representation.
 */
enum { CMETA_DATA_COLLECTION_OPS_ABI_VERSION = 1u };

typedef struct cmeta_data_collection_view {
    const void *data;
    size_t count;
    size_t stride;
    const cmeta_data_desc *element;
} cmeta_data_collection_view;

typedef const cmeta_data_desc *(*cmeta_data_collection_element_fn)(
    const void *object);
typedef cmeta_status (*cmeta_data_collection_read_fn)(
    const void *object, cmeta_data_collection_view *out);
typedef cmeta_status (*cmeta_data_collection_visit_fn)(
    void *context, const void *element);
typedef cmeta_status (*cmeta_data_collection_foreach_fn)(
    const void *object, cmeta_data_collection_visit_fn visit, void *context,
    size_t max_items);

typedef struct cmeta_data_collection_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_collection_element_fn element;
    cmeta_data_collection_read_fn read;
    cmeta_data_collection_foreach_fn foreach;
} cmeta_data_collection_ops;

enum { CMETA_DATA_MAP_OPS_ABI_VERSION = 1u };

typedef const cmeta_data_desc *(*cmeta_data_map_member_fn)(
    const void *object);
typedef cmeta_status (*cmeta_data_map_visit_fn)(
    void *context, const void *key, const void *value);
typedef cmeta_status (*cmeta_data_map_foreach_fn)(
    const void *object, cmeta_data_map_visit_fn visit, void *context,
    size_t max_items);

typedef struct cmeta_data_map_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_map_member_fn key;
    cmeta_data_map_member_fn value;
    cmeta_data_map_foreach_fn foreach;
} cmeta_data_map_ops;

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
 * not expose a complete, matching v2 adapter. Descriptor and adapter addresses
 * are not type identities; storage types are compared semantically and then
 * checked for exact kind, size, and alignment. v1 providers are rejected;
 * there is no compatibility fallback.
 */
const cmeta_data_buffer_ops *cmeta_data_buffer_ops_of(
    const cmeta_data_desc *desc);

/** Return a validated SEQUENCE/SET collection adapter, or NULL. */
const cmeta_data_collection_ops *cmeta_data_collection_ops_of(
    const cmeta_data_desc *desc);

/** Borrow one immutable collection view for the provider callback lifetime. */
cmeta_status cmeta_data_collection_read(
    const cmeta_data_desc *desc, const void *object,
    cmeta_data_collection_view *out);

/**
 * Visit collection elements without exposing native iterator representation.
 * The facade enforces max_items before invoking a visitor beyond the bound.
 */
cmeta_status cmeta_data_collection_foreach(
    const cmeta_data_desc *desc, const void *object,
    cmeta_data_collection_visit_fn visit, void *context, size_t max_items);

const cmeta_data_map_ops *cmeta_data_map_ops_of(
    const cmeta_data_desc *desc);
cmeta_status cmeta_data_map_foreach(
    const cmeta_data_desc *desc, const void *object,
    cmeta_data_map_visit_fn visit, void *context, size_t max_items);

/** Initialize one raw storage slot to provider semantic zero. */
cmeta_status cmeta_data_buffer_init_zero(
    const cmeta_data_desc *desc, void *object);

/**
 * Move source into an already semantic-zero destination and restore source to
 * semantic zero. A violated provider postcondition returns
 * CMETA_CALLBACK_ERROR after best-effort zero restoration.
 */
cmeta_status cmeta_data_buffer_move(
    const cmeta_data_desc *desc, void *destination, void *source);

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

/**
 * Read one bounded borrowed byte view without mutating the source object.
 *
 * The returned view remains provider-owned. It expires when the object is
 * assigned, restored, moved, destroyed, concurrently mutated, or when the
 * provider otherwise invalidates its storage. The caller must not retain it
 * across any such boundary. A zero-length view may have a NULL data pointer.
 *
 * read remains an optional capability within an otherwise complete v2
 * lifecycle provider. A provider without read returns CMETA_TRAIT_MISSING.
 */
cmeta_status cmeta_data_buffer_read(
    const cmeta_data_desc *desc, const void *object, size_t max_bytes,
    const unsigned char **out_data, size_t *out_size);

/** Return a complete, storage-matching exact native-value provider, or NULL. */
const cmeta_data_fixed_ops *cmeta_data_fixed_ops_of(
    const cmeta_data_desc *desc);

/** Return the provider-declared exact native object extent. */
cmeta_status cmeta_data_fixed_extent(
    const cmeta_data_desc *desc, size_t *out);

/** Query the provider-defined semantic-zero state of a native object. */
cmeta_status cmeta_data_fixed_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out);

/**
 * Copy one exact native value into a semantic-zero destination.
 *
 * source_extent must equal the provider extent. Provider failure restores the
 * destination to semantic zero before returning.
 */
cmeta_status cmeta_data_fixed_copy(
    const cmeta_data_desc *desc, void *destination, const void *source,
    size_t source_extent);

/** Restore and verify the provider-defined semantic-zero state. */
cmeta_status cmeta_data_fixed_restore_zero(
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

/** Return a complete canonical domain provider, or NULL; no legacy fallback. */
const cmeta_data_enum_bits_ops *cmeta_data_enum_bits_ops_of(
    const cmeta_data_desc *desc);

/** Query canonical enum semantic zero; output changes only on success. */
cmeta_status cmeta_data_enum_bits_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out);

/**
 * Read validated canonical bits. Every failure leaves out unchanged.
 * Malformed arguments/descriptors return CMETA_INVALID_ARGUMENT; mismatched
 * provider storage returns CMETA_TYPE_MISMATCH. Any provider failure or
 * invalid provider result returns CMETA_CALLBACK_ERROR (not the callback's
 * status), keeping provider execution failures distinct from malformed input.
 */
cmeta_status cmeta_data_enum_read_bits(
    const cmeta_data_desc *desc, const void *object, uint64_t *out);

/**
 * Assign canonical bits to semantic zero, checking width and membership before
 * mutation. Invalid values/nonzero destinations are unchanged and return
 * CMETA_INVALID_ARGUMENT. Provider/read-back failure restores semantic zero
 * and returns CMETA_CALLBACK_ERROR. Descriptor errors match read_bits.
 */
cmeta_status cmeta_data_enum_assign_bits(
    const cmeta_data_desc *desc, void *object, uint64_t bits);

/** Restore semantic zero and verify cleanup; errors match read_bits. */
cmeta_status cmeta_data_enum_bits_restore_zero(
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

extern const cmeta_data_desc cmeta_data_int8;
extern const cmeta_data_desc cmeta_data_uint8;
extern const cmeta_data_desc cmeta_data_int16;
extern const cmeta_data_desc cmeta_data_uint16;
extern const cmeta_data_desc cmeta_data_int32;
extern const cmeta_data_desc cmeta_data_uint32;
extern const cmeta_data_desc cmeta_data_int64;
extern const cmeta_data_desc cmeta_data_uint64;
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