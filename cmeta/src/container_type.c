#include <cmeta/range.h>

#include <stdint.h>

#define CMETA_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))

const cmeta_container_ext *
cmeta_container_extension(const void *object) {
    const cmeta_container_desc *desc;
    const cmeta_container_ext *ext;

    if (object == NULL)
        return NULL;
    desc = cmeta_container_descriptor(object);
    if (desc == NULL || desc->ext == NULL)
        return NULL;
    ext = desc->ext;
    if (ext->abi_version != CMETA_CONTAINER_EXT_ABI_VERSION ||
        ext->struct_size < CMETA_FIELD_END(cmeta_container_ext, type))
        return NULL;
    return ext;
}

const cmeta_data_desc *
cmeta_container_data(const void *object) {
    const cmeta_container_ext *ext = cmeta_container_extension(object);
    if (ext == NULL ||
        ext->struct_size < CMETA_FIELD_END(cmeta_container_ext, data) ||
        ext->data == NULL || !cmeta_data_desc_valid(ext->data))
        return NULL;
    return ext->data;
}

static const cmeta_container_type_ops *
cmeta_container_type_ops_of(const void *object) {
    const cmeta_container_ext *ext;
    const cmeta_container_type_ops *ops;

    ext = cmeta_container_extension(object);
    if (ext == NULL)
        return NULL;
    ops = ext->type;
    if (ops == NULL ||
        ops->struct_size < CMETA_FIELD_END(cmeta_container_type_ops, argument) ||
        ops->abi_version != CMETA_CONTAINER_TYPE_OPS_ABI_VERSION)
        return NULL;
    if (ops->arity != 0u && ops->argument == NULL)
        return NULL;
    return ops;
}

const cmeta_generic_desc *
cmeta_container_type_constructor(const void *object) {
    const cmeta_container_type_ops *ops = cmeta_container_type_ops_of(object);
    return ops != NULL ? ops->constructor : NULL;
}

size_t cmeta_container_type_arity(const void *object) {
    const cmeta_container_type_ops *ops = cmeta_container_type_ops_of(object);
    return ops != NULL ? ops->arity : 0u;
}

const cmeta_type_desc *
cmeta_container_type_argument(const void *object, size_t index) {
    const cmeta_container_type_ops *ops = cmeta_container_type_ops_of(object);
    if (ops == NULL || index >= ops->arity || ops->argument == NULL)
        return NULL;
    return ops->argument(object, index);
}

bool cmeta_container_type_application_valid(const void *object) {
    const cmeta_container_type_ops *ops = cmeta_container_type_ops_of(object);
    const cmeta_type_identity *identities[UINT8_MAX + 1u];
    size_t i;

    if (ops == NULL || !cmeta_generic_accepts_arity(ops->constructor, ops->arity))
        return false;
    for (i = 0u; i < ops->arity; ++i) {
        const cmeta_type_desc *argument = cmeta_container_type_argument(object, i);
        const cmeta_type_identity *identity;
        if (argument == NULL || !cmeta_type_desc_valid(argument))
            return false;
        identity = cmeta_type_identity_of(argument);
        if (identity == NULL || !cmeta_type_identity_valid(identity))
            return false;
        identities[i] = identity;
    }
    return cmeta_type_application_valid(
        ops->constructor, ops->arity == 0u ? NULL : identities, ops->arity);
}

#undef CMETA_FIELD_END
