#ifndef CMETA_MANIFEST_H
#define CMETA_MANIFEST_H

#include <cmeta/cmeta.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define CMETA_MANIFEST_U64(value_) static_cast<uint64_t>(value_)
#define CMETA_MANIFEST_U8(value_) static_cast<uint8_t>(value_)
#define CMETA_MANIFEST_BYTES(value_) reinterpret_cast<const uint8_t *>(value_)
#define CMETA_MANIFEST_VOID_PTR(value_) static_cast<const void *>(value_)
extern "C" {
#else
#define CMETA_MANIFEST_U64(value_) ((uint64_t)(value_))
#define CMETA_MANIFEST_U8(value_) ((uint8_t)(value_))
#define CMETA_MANIFEST_BYTES(value_) ((const uint8_t *)(value_))
#define CMETA_MANIFEST_VOID_PTR(value_) (value_)
#endif

#define CMETA_MANIFEST_FORMAT_VERSION UINT32_C(1)
#define CMETA_ABI_FINGERPRINT_VERSION UINT32_C(1)

typedef enum cmeta_manifest_kind {
    CMETA_MANIFEST_GENERIC = 0,
    CMETA_MANIFEST_PLUGIN = 1,
    CMETA_MANIFEST_CAPABILITY = 2,
    CMETA_MANIFEST_TRACEPOINT = 3,
    CMETA_MANIFEST_FAULT_POINT = 4
} cmeta_manifest_kind;

typedef struct cmeta_manifest_entry {
    const char *name;
    cmeta_manifest_kind kind;
    const void *descriptor;
    uint64_t abi_fingerprint;
    uint32_t flags;
} cmeta_manifest_entry;

typedef struct cmeta_manifest {
    const char *name;
    const cmeta_manifest_entry *entries;
    size_t count;
    uint32_t format_version;
} cmeta_manifest;

/*
 * Static manifests are immutable compile-time tables. They do not register
 * ownership, run constructors, or create a process-global mutable registry.
 * Platform linker sections may aggregate these tables behind this contract,
 * but section syntax is deliberately not part of the public API.
 */
#ifdef __cplusplus
#define cmeta_entry(symbol) \
    { #symbol, CMETA_MANIFEST_GENERIC, static_cast<const void *>(&(symbol)), UINT64_C(0), UINT32_C(0) },
#define cmeta_manifest_entry(name_, kind_, descriptor_, fingerprint_, flags_) \
    { (name_), (kind_), static_cast<const void *>(descriptor_), (fingerprint_), (flags_) },
#else
#define cmeta_entry(symbol) \
    { #symbol, CMETA_MANIFEST_GENERIC, &(symbol), UINT64_C(0), UINT32_C(0) },
#define cmeta_manifest_entry(name_, kind_, descriptor_, fingerprint_, flags_) \
    { (name_), (kind_), (descriptor_), (fingerprint_), (flags_) },
#endif

#define cmeta_registry(name, entries_) \
    static const cmeta_manifest_entry name##_cmeta_entries[] = { entries_ }; \
    static const cmeta_manifest name = { \
        #name, name##_cmeta_entries, \
        sizeof(name##_cmeta_entries) / sizeof(name##_cmeta_entries[0]), \
        CMETA_MANIFEST_FORMAT_VERSION \
    }

typedef struct cmeta_abi_fingerprint_builder {
    uint64_t value;
} cmeta_abi_fingerprint_builder;

static inline cmeta_abi_fingerprint_builder cmeta_abi_fingerprint_begin(void) {
    cmeta_abi_fingerprint_builder builder = { UINT64_C(14695981039346656037) };
    return builder;
}

static inline void cmeta_abi_fingerprint_byte(cmeta_abi_fingerprint_builder *builder,
                                              uint8_t byte) {
    if (builder == NULL)
        return;
    builder->value ^= CMETA_MANIFEST_U64(byte);
    builder->value *= UINT64_C(1099511628211);
}

static inline void cmeta_abi_fingerprint_bytes(cmeta_abi_fingerprint_builder *builder,
                                               const void *data, size_t size) {
    const uint8_t *bytes = CMETA_MANIFEST_BYTES(data);
    size_t i;
    if (builder == NULL || (data == NULL && size != 0u))
        return;
    for (i = 0u; i < size; ++i)
        cmeta_abi_fingerprint_byte(builder, bytes[i]);
}

static inline void cmeta_abi_fingerprint_u64(cmeta_abi_fingerprint_builder *builder,
                                             uint64_t value) {
    unsigned shift;
    for (shift = 0u; shift < 64u; shift += 8u)
        cmeta_abi_fingerprint_byte(builder, CMETA_MANIFEST_U8((value >> shift) & UINT64_C(0xff)));
}

static inline void cmeta_abi_fingerprint_string(cmeta_abi_fingerprint_builder *builder,
                                                const char *text) {
    size_t size = 0u;
    if (text != NULL) {
        while (text[size] != '\0')
            ++size;
    }
    cmeta_abi_fingerprint_u64(builder, CMETA_MANIFEST_U64(size));
    cmeta_abi_fingerprint_bytes(builder, text, size);
}

static inline uint64_t cmeta_abi_fingerprint_finish(
    const cmeta_abi_fingerprint_builder *builder) {
    return builder == NULL ? UINT64_C(0) : builder->value;
}

static inline void cmeta_abi_fingerprint_identity(
    cmeta_abi_fingerprint_builder *builder,
    const cmeta_type_identity *identity) {
    size_t i;
    if (identity == NULL) {
        cmeta_abi_fingerprint_u64(builder, UINT64_C(0));
        return;
    }

    cmeta_abi_fingerprint_u64(builder, CMETA_MANIFEST_U64(identity->form) + UINT64_C(1));
    switch (identity->form) {
        case CMETA_TYPE_ATOM:
            cmeta_abi_fingerprint_string(builder, identity->stable_atom_id);
            break;
        case CMETA_TYPE_POINTER:
        case CMETA_TYPE_CONST:
            cmeta_abi_fingerprint_identity(builder, identity->base);
            break;
        case CMETA_TYPE_APPLY:
            cmeta_abi_fingerprint_string(
                builder,
                identity->constructor == NULL ? NULL : identity->constructor->stable_id);
            cmeta_abi_fingerprint_u64(builder, CMETA_MANIFEST_U64(identity->arity));
            for (i = 0u; i < identity->arity; ++i)
                cmeta_abi_fingerprint_identity(builder, identity->args[i]);
            break;
        default:
            cmeta_abi_fingerprint_u64(builder, UINT64_MAX);
            break;
    }
}

static inline uint64_t cmeta_abi_fingerprint_type(const cmeta_type_desc *type) {
    cmeta_abi_fingerprint_builder builder = cmeta_abi_fingerprint_begin();

    cmeta_abi_fingerprint_u64(&builder, CMETA_ABI_FINGERPRINT_VERSION);
    if (type == NULL)
        return cmeta_abi_fingerprint_finish(&builder);

    cmeta_abi_fingerprint_string(&builder, type->name);
    cmeta_abi_fingerprint_u64(&builder, CMETA_MANIFEST_U64(type->size));
    cmeta_abi_fingerprint_u64(&builder, CMETA_MANIFEST_U64(type->align));
    cmeta_abi_fingerprint_u64(&builder, CMETA_MANIFEST_U64(type->kind));
    cmeta_abi_fingerprint_u64(
        &builder, type->traits == NULL ? UINT64_C(0) : CMETA_MANIFEST_U64(type->traits->flags));
    cmeta_abi_fingerprint_identity(&builder, type->identity);

    if (type->pointee == NULL) {
        cmeta_abi_fingerprint_u64(&builder, UINT64_C(0));
    } else {
        cmeta_abi_fingerprint_u64(&builder, UINT64_C(1));
        cmeta_abi_fingerprint_string(&builder, type->pointee->name);
        cmeta_abi_fingerprint_u64(&builder, CMETA_MANIFEST_U64(type->pointee->size));
        cmeta_abi_fingerprint_u64(&builder, CMETA_MANIFEST_U64(type->pointee->align));
        cmeta_abi_fingerprint_u64(&builder, CMETA_MANIFEST_U64(type->pointee->kind));
        cmeta_abi_fingerprint_identity(&builder, type->pointee->identity);
    }

    return cmeta_abi_fingerprint_finish(&builder);
}

#ifdef __cplusplus
}
#endif

#undef CMETA_MANIFEST_U64
#undef CMETA_MANIFEST_U8
#undef CMETA_MANIFEST_BYTES
#undef CMETA_MANIFEST_VOID_PTR

#endif /* CMETA_MANIFEST_H */
