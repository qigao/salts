#ifndef CMETA_LIFECYCLE_H
#define CMETA_LIFECYCLE_H
#include <cmeta/data.h>
#include <cmeta/compiler.h>
#include <cmeta/pp.h>

/* A static declaration must name constant-address, immutable canonical ops.
 * The C++ spelling also rejects dynamic initialization of that address. */
#ifdef __cplusplus
#define CMETA_LIFECYCLE_STATIC_ADDRESS_ static constexpr
#else
#define CMETA_LIFECYCLE_STATIC_ADDRESS_ static
#endif

/** Publish a native-typed view of locally owned canonical construct ops.
 * The declaration owner guarantees valid ABI/storage identity/layout and
 * init/restore contracts; ops must be the same immutable, static-lifetime
 * object referenced by DataDesc.construct_ops. This is not foreign admission.
 * The accessor's argument is a type witness only and may be NULL; it is never
 * dereferenced or retained. No callbacks, validation or allocation occur here.
 */
#define CMETA_DEFINE_STATIC_LIFECYCLE(type_, ops_) \
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
    if (ops->struct_size < sizeof(*ops) ||
        ops->abi_version != CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION ||
        ops->init_zero == NULL || ops->restore_zero == NULL ||
        data->storage_type == NULL || !cmeta_type_desc_valid(ops->storage_type) ||
        ops->storage_type->size == 0u ||
        (ops->storage_type->align & (ops->storage_type->align - 1u)) != 0u ||
        ops->storage_type->size % ops->storage_type->align != 0u)
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
#endif
