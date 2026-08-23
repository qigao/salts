#include <cmeta/range.h>

#include <stdint.h>

static const cmeta_container_type_ops *
cmeta_container_type_ops_of(const void *object) {
    const cmeta_container_desc *desc;
    const cmeta_container_ext *ext;
    const cmeta_container_type_ops *ops;

    if (object == NULL)
        return NULL;
    desc = cmeta_container_descriptor(object);
    if (desc == NULL)
        return NULL;
    ext = desc->ext;
    if (ext == NULL || ext->struct_size < sizeof(*ext) ||
        ext->abi_version != CMETA_CONTAINER_EXT_ABI_VERSION)
        return NULL;
    ops = ext->type;
    if (ops == NULL || ops->struct_size < sizeof(*ops) ||
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
