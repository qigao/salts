#include <cmeta/range.h>

#include <stdint.h>
#include <string.h>

#define CMETA_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))

static const cmeta_container_ext *
cmeta_container_extension_of_descriptor(const cmeta_container_desc *desc) {
    const cmeta_container_ext *ext;
    if (desc == NULL || desc->ext == NULL)
        return NULL;
    ext = desc->ext;
    if (ext->abi_version != CMETA_CONTAINER_EXT_ABI_VERSION ||
        ext->struct_size < CMETA_FIELD_END(cmeta_container_ext, type))
        return NULL;
    return ext;
}

const cmeta_container_ext *
cmeta_container_extension(const void *object) {
    if (object == NULL)
        return NULL;
    return cmeta_container_extension_of_descriptor(
        cmeta_container_descriptor(object));
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
cmeta_container_type_ops_of_descriptor(const cmeta_container_desc *desc) {
    const cmeta_container_ext *ext =
        cmeta_container_extension_of_descriptor(desc);
    const cmeta_container_type_ops *ops;
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

static const cmeta_container_type_ops *
cmeta_container_type_ops_of(const void *object) {
    if (object == NULL)
        return NULL;
    return cmeta_container_type_ops_of_descriptor(
        cmeta_container_descriptor(object));
}

static bool cmeta_container_construct_ops_valid(
    const cmeta_container_construct_ops *ops) {
    return ops != NULL &&
           ops->struct_size >=
               CMETA_FIELD_END(cmeta_container_construct_ops, bind_types) &&
           ops->abi_version == CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION &&
           ops->descriptor != NULL && ops->bind_types != NULL;
}

static bool cmeta_container_restore_ops_valid(
    const cmeta_container_construct_ops *ops) {
    return cmeta_container_construct_ops_valid(ops) &&
           ops->struct_size >=
               CMETA_FIELD_END(cmeta_container_construct_ops, restore_zero) &&
           ops->restore_zero != NULL;
}

static const cmeta_container_construct_ops *
cmeta_container_construction_of_descriptor(const cmeta_container_desc *desc) {
    const cmeta_container_ext *ext =
        cmeta_container_extension_of_descriptor(desc);
    const cmeta_container_construct_ops *ops;
    if (ext == NULL ||
        ext->struct_size < CMETA_FIELD_END(cmeta_container_ext, construction) ||
        ext->construction == NULL)
        return NULL;
    ops = ext->construction;
    if (!cmeta_container_construct_ops_valid(ops) || ops->descriptor != desc)
        return NULL;
    return ops;
}

const cmeta_container_construct_ops *
cmeta_container_construction(const void *object) {
    if (object == NULL)
        return NULL;
    return cmeta_container_construction_of_descriptor(
        cmeta_container_descriptor(object));
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

static bool cmeta_generic_same(const cmeta_generic_desc *left,
                               const cmeta_generic_desc *right) {
    if (left == right)
        return left != NULL && cmeta_generic_desc_valid(left);
    return cmeta_generic_desc_valid(left) && cmeta_generic_desc_valid(right) &&
           strcmp(left->stable_id, right->stable_id) == 0;
}

cmeta_status cmeta_container_bind_types(
    void *object, const cmeta_declared_type *declared) {
    const cmeta_container_construct_ops *construction;
    const cmeta_container_type_ops *type_ops;

    if (object == NULL || !cmeta_declared_type_constructible(declared))
        return CMETA_INVALID_ARGUMENT;

    construction = declared->construction;
    if (!cmeta_container_construct_ops_valid(construction) ||
        cmeta_container_construction_of_descriptor(construction->descriptor) !=
            construction)
        return CMETA_INVALID_ARGUMENT;

    type_ops = cmeta_container_type_ops_of_descriptor(construction->descriptor);
    if (type_ops == NULL ||
        !cmeta_generic_same(type_ops->constructor, declared->constructor) ||
        type_ops->arity != declared->arity)
        return CMETA_INVALID_ARGUMENT;

    return construction->bind_types(
        object, declared->arguments, declared->arity);
}

cmeta_status cmeta_container_restore_zero(
    void *object, const cmeta_declared_type *declared) {
    const cmeta_container_construct_ops *construction;
    const cmeta_container_desc *current;

    if (object == NULL || !cmeta_declared_type_constructible(declared))
        return CMETA_INVALID_ARGUMENT;

    construction = declared->construction;
    if (!cmeta_container_restore_ops_valid(construction) ||
        cmeta_container_construction_of_descriptor(construction->descriptor) !=
            construction)
        return CMETA_INVALID_ARGUMENT;

    current = cmeta_container_descriptor(object);
    if (current != NULL && current != construction->descriptor)
        return CMETA_TYPE_MISMATCH;

    construction->restore_zero(object);
    return CMETA_OK;
}

#undef CMETA_FIELD_END
