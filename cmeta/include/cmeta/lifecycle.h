#ifndef CMETA_LIFECYCLE_H
#define CMETA_LIFECYCLE_H
#include <cmeta/data.h>
#include <cmeta/compiler.h>
#include <cmeta/pp.h>

/* A static declaration must name constant-address, immutable canonical ops.
 * The C++ spelling also rejects dynamic initialization of that address. */
#ifdef __cplusplus
#define CMETA_LIFECYCLE_STATIC_ADDRESS_ static constexpr
#define CMETA_LIFECYCLE_STORAGE_(type,value) static_cast<type *>(value)
#else
#define CMETA_LIFECYCLE_STATIC_ADDRESS_ static
#define CMETA_LIFECYCLE_STORAGE_(type,value) ((type *)(value))
#endif

/** Publish a native-typed view of locally owned canonical construct ops.
 * The declaration owner guarantees valid ABI/storage identity/layout and
 * init/restore contracts; ops must be the same immutable, static-lifetime
 * object referenced by DataDesc.construct_ops. This is not foreign admission.
 * The accessor's argument is a type witness only and may be NULL; it is never
 * dereferenced or retained. No callbacks, validation or allocation occur here.
 * Optional facts must be the same canonical declaration constants used by ops;
 * the two-argument form declares unknown (conservatively fallible) facts.
 */
#define CMETA_DEFINE_STATIC_LIFECYCLE(...) \
    CMETA_PP_OVERLOAD(CMETA_DEFINE_STATIC_LIFECYCLE_, __VA_ARGS__)(__VA_ARGS__)
/* An unclassified local provider remains conservatively fallible. */
#define CMETA_DEFINE_STATIC_LIFECYCLE_2(type_, ops_) \
    CMETA_DEFINE_STATIC_LIFECYCLE_3(type_, ops_, 0)
#define CMETA_DEFINE_STATIC_LIFECYCLE_3(type_, ops_, facts_) \
    CMETA_STATIC_ASSERT(((facts_) & ~CMETA_LIFECYCLE_FLAG_MASK) == 0, \
        "CMeta lifecycle unknown classification"); \
    CMETA_STATIC_ASSERT(((facts_) & CMETA_LIFECYCLE_TRIVIAL_ZERO) == 0 || \
        ((facts_) & CMETA_LIFECYCLE_INIT_NOFAIL) != 0, \
        "CMeta trivial zero requires nofail initialization"); \
    enum { type_##_cmeta_lifecycle_flags = (facts_) }; \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&(ops_), \
        const cmeta_data_construct_ops *), \
        "CMeta static lifecycle requires immutable canonical construct ops"); \
    CMETA_LIFECYCLE_STATIC_ADDRESS_ const cmeta_data_construct_ops *const \
        CMETA_PP_CAT(type_, _cmeta_lifecycle_ops) = &(ops_); \
    CMETA_INLINE const cmeta_data_construct_ops * \
    CMETA_PP_CAT(type_, _cmeta_lifecycle)(const type_ *native_type_) { \
        (void)native_type_; \
        return CMETA_PP_CAT(type_, _cmeta_lifecycle_ops); \
    }

#define CMETA_LIFECYCLE_ACCESSOR_(type_) CMETA_PP_CAT(type_, _cmeta_lifecycle)

/** Bind borrowed canonical lifecycle operations to the requested native layout.
 * No allocation or callbacks; move remains an optional capability. Invalid
 * metadata returns INVALID_ARGUMENT, missing operations return TRAIT_MISSING,
 * identity/kind/layout disagreement returns TYPE_MISMATCH. Failure clears *out.
 */
CMETA_INLINE cmeta_status cmeta_lifecycle_bind(
    const cmeta_data_desc *data, size_t size, size_t align,
    const cmeta_data_construct_ops **out) {
    const cmeta_data_construct_ops *ops;
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    *out = NULL;
    if (data == NULL ||
        data->struct_size < offsetof(cmeta_data_desc, construct_ops) + sizeof(data->construct_ops) ||
        !cmeta_data_desc_valid(data))
        return CMETA_INVALID_ARGUMENT;
    ops = data->construct_ops;
    if (ops == NULL) return CMETA_TRAIT_MISSING;
    if (ops->struct_size < offsetof(cmeta_data_construct_ops, move) + sizeof(ops->move) ||
        ops->abi_version != CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION ||
        ops->init_zero == NULL || ops->restore_zero == NULL ||
        data->storage_type == NULL || !cmeta_type_desc_valid(ops->storage_type) ||
        ops->storage_type->size == 0u ||
        (ops->storage_type->align & (ops->storage_type->align - 1u)) != 0u ||
        ops->storage_type->size % ops->storage_type->align != 0u)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_lifecycle_flags_valid(ops))
        return CMETA_INVALID_ARGUMENT;
    /* Native layout alone cannot authorize another type's lifecycle callbacks. */
    if (!cmeta_type_equal(data->storage_type, ops->storage_type) ||
        data->storage_type->kind != ops->storage_type->kind ||
        data->storage_type->size != size || data->storage_type->align != align ||
        ops->storage_type->size != size || ops->storage_type->align != align)
        return CMETA_TYPE_MISMATCH;
    *out = ops;
    return CMETA_OK;
}

/** Local generator: typed callbacks, canonical ops and lowering facts originate
 * in one declaration. Callback contracts (including nofail) remain the native
 * owner's responsibility. Descriptor identity/layout is checked on admission. */
#define CMETA_DEFINE_LIFECYCLE(type,descriptor,init,restore,move_fn,facts) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&init,cmeta_status (*)(type *)) && \
        CMETA_TYPE_MATCHES(&restore,void (*)(type *)) && \
        CMETA_TYPE_MATCHES(&move_fn,void (*)(type *,type *)), \
        "CMeta lifecycle native callback mismatch"); \
    CMETA_INLINE cmeta_status type##__init_erased(void *value) { return init(CMETA_LIFECYCLE_STORAGE_(type,value)); } \
    CMETA_INLINE void type##__restore_erased(void *value) { restore(CMETA_LIFECYCLE_STORAGE_(type,value)); } \
    CMETA_INLINE void type##__move_erased(void *dst,void *src) { \
        move_fn(CMETA_LIFECYCLE_STORAGE_(type,dst),CMETA_LIFECYCLE_STORAGE_(type,src)); } \
    CMETA_LOCAL const cmeta_data_construct_ops type##_construct_ops = { \
        sizeof(cmeta_data_construct_ops),CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,descriptor, \
        type##__init_erased,type##__restore_erased,type##__move_erased,facts }; \
    CMETA_DEFINE_STATIC_LIFECYCLE(type,type##_construct_ops,facts)

#ifdef __cplusplus
#define CMETA_LIFECYCLE_TRIVIAL_PROOF_(type) \
    static_assert(std::is_trivial<type>::value, "CMeta trivial lifecycle requires trivial native storage");
#else
#define CMETA_LIFECYCLE_TRIVIAL_PROOF_(type)
#endif
#define CMETA_DEFINE_TRIVIAL_LIFECYCLE(type,descriptor) \
    CMETA_LIFECYCLE_TRIVIAL_PROOF_(type) \
    CMETA_INLINE cmeta_status type##__trivial_init(type *value) { \
        type zero = {0}; *value = zero; return CMETA_OK; } \
    CMETA_INLINE void type##__trivial_restore(type *value) { type zero = {0}; *value = zero; } \
    CMETA_INLINE void type##__trivial_move(type *dst,type *src) { \
        *dst = *src; type##__trivial_restore(src); } \
    CMETA_DEFINE_LIFECYCLE(type,descriptor,type##__trivial_init,type##__trivial_restore, \
        type##__trivial_move,CMETA_LIFECYCLE_INIT_NOFAIL | CMETA_LIFECYCLE_TRIVIAL_ZERO | \
            CMETA_LIFECYCLE_TRIVIAL_CLEANUP | CMETA_LIFECYCLE_MOVABLE)

/** Validated borrowed binding produced by cmeta_lifecycle_admit(), not an
 * unforgeable security capability. Only use a successful, unmodified binding;
 * never forge or mutate a live record. Canonical descriptors/providers remain
 * the semantic authority, not this record or its address. They and any outer
 * Plugin/module lease must outlive every use, including cleanup. No allocation,
 * retention or value ownership is implied; no cookie can enforce this C trust
 * contract. Admitted operations do not revalidate caller-owned metadata. */
typedef struct cmeta_lifecycle_binding {
    const cmeta_data_desc *data;
    const cmeta_data_construct_ops *ops;
} cmeta_lifecycle_binding;

#define CMETA_LIFECYCLE_BINDING_INIT { NULL, NULL }

CMETA_INLINE cmeta_status cmeta_lifecycle_admit(
    const cmeta_data_desc *data, size_t size, size_t align,
    cmeta_lifecycle_binding *out) {
    const cmeta_data_construct_ops *ops = NULL;
    cmeta_status status;
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    out->data = NULL;
    out->ops = NULL;
    status = cmeta_lifecycle_bind(data, size, align, &ops);
    if (status != CMETA_OK) return status;
    out->data = data;
    out->ops = ops;
    return CMETA_OK;
}

/* All storage must have the admitted native layout. init rolls back a partial
 * failure exactly once. move requires a distinct semantic-zero destination;
 * its source remains semantic zero, valid for cleanup/reinitialization. */
CMETA_INLINE cmeta_status cmeta_lifecycle_init(
    const cmeta_lifecycle_binding *binding, void *storage) {
    cmeta_status status;
    if (binding == NULL || binding->ops == NULL || storage == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = binding->ops->init_zero(storage);
    if (status != CMETA_OK) binding->ops->restore_zero(storage);
    return status;
}
CMETA_INLINE cmeta_status cmeta_lifecycle_restore(
    const cmeta_lifecycle_binding *binding, void *storage) {
    if (binding == NULL || binding->ops == NULL || storage == NULL)
        return CMETA_INVALID_ARGUMENT;
    binding->ops->restore_zero(storage);
    return CMETA_OK;
}
CMETA_INLINE cmeta_status cmeta_lifecycle_move(
    const cmeta_lifecycle_binding *binding, void *destination, void *source) {
    if (binding == NULL || binding->ops == NULL || destination == NULL ||
        source == NULL || destination == source) return CMETA_INVALID_ARGUMENT;
    if (binding->ops->move == NULL) return CMETA_TRAIT_MISSING;
    binding->ops->move(destination, source);
    return CMETA_OK;
}
#endif
