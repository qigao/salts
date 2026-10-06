#ifndef CMETA_LIFECYCLE_H
#define CMETA_LIFECYCLE_H
#include <cmeta/data.h>

/* Bind the same concrete lifecycle for lexical, pool and local storage. */
CMETA_INLINE cmeta_status cmeta_lifecycle_bind(
    const cmeta_data_desc *data, size_t size, size_t align,
    const cmeta_data_construct_ops **out) {
    const cmeta_data_construct_ops *ops;
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    *out = NULL;
    if (data == NULL ||
        data->struct_size < offsetof(cmeta_data_desc, construct_ops) + sizeof(data->construct_ops) ||
        data->abi_version != CMETA_DATA_DESC_ABI_VERSION)
        return CMETA_INVALID_ARGUMENT;
    ops = data->construct_ops;
    if (ops == NULL) return CMETA_TRAIT_MISSING;
    if (ops->struct_size < sizeof(*ops) ||
        ops->abi_version != CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION ||
        ops->init_zero == NULL || ops->restore_zero == NULL ||
        data->storage_type == NULL || ops->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (data->storage_type->size != size || data->storage_type->align != align ||
        ops->storage_type->size != size || ops->storage_type->align != align)
        return CMETA_TYPE_MISMATCH;
    *out = ops;
    return CMETA_OK;
}
#endif
