#include <cmeta/declared_type.h>

#include <stdint.h>

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
    return cmeta_declared_type_valid(declared) &&
           declared->construction != NULL;
}

const cmeta_type_desc *cmeta_declared_type_argument(
    const cmeta_declared_type *declared, size_t index) {
    if (declared == NULL || declared->arguments == NULL ||
        index >= declared->arity)
        return NULL;
    return declared->arguments[index];
}
