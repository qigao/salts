#include <cmeta/function.h>

#include <string.h>

static bool cmeta_param_flags_valid(cmeta_param_flags flags) {
    const cmeta_param_flags ownership = flags & CMETA_PARAM_OWNERSHIP_MASK;

    if ((flags & CMETA_PARAM_FLAG_MASK) != flags)
        return false;
    if (ownership == CMETA_PARAM_OWNERSHIP_MASK)
        return false;
    return true;
}

bool cmeta_param_desc_valid(const cmeta_param_desc *desc) {
    if (desc == NULL || desc->size < sizeof(*desc) ||
        desc->name == NULL || desc->name[0] == '\0' ||
        !cmeta_type_desc_valid(desc->type) ||
        !cmeta_param_flags_valid(desc->flags))
        return false;

    if ((desc->flags & (CMETA_PARAM_OUT | CMETA_PARAM_NULLABLE |
                        CMETA_PARAM_OWNERSHIP_MASK |
                        CMETA_PARAM_RECEIVER)) != 0u &&
        desc->type->kind != CMETA_T_POINTER)
        return false;

    return true;
}

bool cmeta_function_desc_valid(const cmeta_function_desc *desc) {
    size_t i;
    size_t j;

    if (desc == NULL || desc->size < sizeof(*desc) ||
        desc->name == NULL || desc->name[0] == '\0' ||
        !cmeta_type_desc_valid(desc->return_type) ||
        !cmeta_effect_property_contract_valid(desc->effects, desc->properties))
        return false;

    if (desc->param_count != 0u && desc->params == NULL)
        return false;

    for (i = 0u; i < desc->param_count; ++i) {
        if (!cmeta_param_desc_valid(&desc->params[i]))
            return false;
        if ((desc->params[i].flags & CMETA_PARAM_RECEIVER) != 0u &&
            i != 0u)
            return false;
        for (j = 0u; j < i; ++j)
            if (strcmp(desc->params[i].name, desc->params[j].name) == 0)
                return false;
    }

    return true;
}

bool cmeta_function_abi_desc_valid(const cmeta_function_abi_desc *desc) {
    size_t i;

    if (desc == NULL || desc->size < sizeof(*desc) ||
        !cmeta_function_desc_valid(desc->function) ||
        !cmeta_abi_carrier_valid(desc->return_carrier) ||
        !cmeta_abi_carrier_matches_type(desc->return_carrier,
                                        desc->function->return_type))
        return false;

    if (desc->param_count != desc->function->param_count)
        return false;
    if (desc->param_count != 0u && desc->param_carriers == NULL)
        return false;

    for (i = 0u; i < desc->param_count; ++i) {
        if (!cmeta_abi_carrier_valid(desc->param_carriers[i]) ||
            !cmeta_abi_carrier_matches_type(
                desc->param_carriers[i], desc->function->params[i].type))
            return false;
    }
    return true;
}

bool cmeta_function_desc_equal(const cmeta_function_desc *left,
                               const cmeta_function_desc *right) {
    size_t i;

    if (left == right)
        return left != NULL && cmeta_function_desc_valid(left);
    if (!cmeta_function_desc_valid(left) || !cmeta_function_desc_valid(right))
        return false;
    if (strcmp(left->name, right->name) != 0 ||
        !cmeta_type_equal(left->return_type, right->return_type) ||
        left->param_count != right->param_count ||
        left->effects != right->effects ||
        left->properties != right->properties)
        return false;

    for (i = 0u; i < left->param_count; ++i) {
        const cmeta_param_desc *a = &left->params[i];
        const cmeta_param_desc *b = &right->params[i];
        if (strcmp(a->name, b->name) != 0 ||
            a->flags != b->flags ||
            !cmeta_type_equal(a->type, b->type))
            return false;
    }
    return true;
}

bool cmeta_function_abi_desc_equal(const cmeta_function_abi_desc *left,
                                   const cmeta_function_abi_desc *right) {
    size_t i;

    if (left == right)
        return left != NULL && cmeta_function_abi_desc_valid(left);
    if (!cmeta_function_abi_desc_valid(left) ||
        !cmeta_function_abi_desc_valid(right) ||
        !cmeta_function_desc_equal(left->function, right->function) ||
        left->return_carrier != right->return_carrier ||
        left->param_count != right->param_count)
        return false;

    for (i = 0u; i < left->param_count; ++i)
        if (left->param_carriers[i] != right->param_carriers[i])
            return false;
    return true;
}

cmeta_abi_carrier
cmeta_function_param_abi(const cmeta_function_abi_desc *desc, size_t index) {
    if (desc == NULL || index >= desc->param_count ||
        desc->param_carriers == NULL)
        return CMETA_ABI_UNSPECIFIED;
    return desc->param_carriers[index];
}

const cmeta_param_desc *
cmeta_function_param(const cmeta_function_desc *desc, size_t index) {
    if (desc == NULL || index >= desc->param_count || desc->params == NULL)
        return NULL;
    return &desc->params[index];
}

const cmeta_param_desc *
cmeta_function_find_param(const cmeta_function_desc *desc, const char *name) {
    size_t i;

    if (desc == NULL || name == NULL || desc->params == NULL)
        return NULL;
    for (i = 0u; i < desc->param_count; ++i)
        if (desc->params[i].name != NULL &&
            strcmp(desc->params[i].name, name) == 0)
            return &desc->params[i];
    return NULL;
}

const cmeta_param_desc *
cmeta_function_receiver(const cmeta_function_desc *desc) {
    if (!cmeta_function_desc_valid(desc) || desc->param_count == 0u)
        return NULL;
    return (desc->params[0].flags & CMETA_PARAM_RECEIVER) != 0u
               ? &desc->params[0]
               : NULL;
}
