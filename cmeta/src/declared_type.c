#include <cmeta/declared_type.h>
#include <cmeta/range.h>

#include <stddef.h>
#include <stdint.h>

#define CMETA_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))

bool cmeta_declared_type_valid(const cmeta_declared_type *declared) {
    const cmeta_type_identity *identities[UINT8_MAX + 1u];
    size_t i;

    if (declared == NULL || !cmeta_type_desc_valid(declared->storage_type) ||
        !cmeta_generic_accepts_arity(declared->constructor, declared->arity) ||
        (declared->arity != 0u && declared->arguments == NULL))
        return false;

    for (i = 0u; i < declared->arity; ++i) {
        const cmeta_type_desc *argument = declared->arguments[i];
        const cmeta_type_identity *identity;
        if (argument == NULL || !cmeta_type_desc_valid(argument))
            return false;
        identity = cmeta_type_identity_of(argument);
        if (identity == NULL || !cmeta_type_identity_valid(identity))
            return false;
        identities[i] = identity;
    }

    return cmeta_type_application_valid(
        declared->constructor,
        declared->arity == 0u ? NULL : identities,
        declared->arity);
}

bool cmeta_declared_type_constructible(const cmeta_declared_type *declared) {
    const cmeta_container_construct_ops *ops;

    if (!cmeta_declared_type_valid(declared))
        return false;
    ops = declared->construction;
    return ops != NULL &&
           ops->struct_size >=
               CMETA_FIELD_END(cmeta_container_construct_ops, bind_types) &&
           ops->abi_version == CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION &&
           ops->descriptor != NULL && ops->bind_types != NULL;
}

const cmeta_type_desc *cmeta_declared_type_argument(
    const cmeta_declared_type *declared, size_t index) {
    if (declared == NULL || declared->arguments == NULL ||
        index >= declared->arity)
        return NULL;
    return declared->arguments[index];
}

#undef CMETA_FIELD_END
