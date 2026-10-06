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

static bool cmeta_result_flags_valid(
    const cmeta_type_desc *return_type, cmeta_result_flags flags) {
    const cmeta_result_flags result_class = flags & CMETA_RESULT_CLASS_MASK;

    if (return_type == NULL || (flags & CMETA_RESULT_FLAG_MASK) != flags)
        return false;
    if (result_class != 0u &&
        (result_class & (result_class - 1u)) != 0u)
        return false;
    if (return_type->kind == CMETA_T_VOID)
        return flags == CMETA_RESULT_UNKNOWN;
    if ((flags & CMETA_RESULT_NULLABLE) != 0u &&
        return_type->kind != CMETA_T_POINTER)
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
        !cmeta_result_flags_valid(desc->return_type, desc->result_flags) ||
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

bool cmeta_function_receiver_valid(const cmeta_function_desc *function) {
    const cmeta_param_desc *receiver = cmeta_function_receiver(function);
    return receiver != NULL && receiver->type->kind == CMETA_T_POINTER &&
           cmeta_type_desc_valid(receiver->type->pointee);
}

bool cmeta_function_receiver_projection_valid(
    const cmeta_function_desc *function,
    const cmeta_function_desc *projected) {
    size_t i;
    if (!cmeta_function_receiver_valid(function) ||
        !cmeta_function_desc_valid(projected))
        return false;
    if (function->param_count - 1u != projected->param_count ||
        !cmeta_type_equal(function->return_type, projected->return_type) ||
        function->result_flags != projected->result_flags ||
        function->effects != projected->effects ||
        function->properties != projected->properties)
        return false;
    for (i = 0u; i < projected->param_count; ++i) {
        const cmeta_param_desc *source = &function->params[i + 1u];
        const cmeta_param_desc *target = &projected->params[i];
        if (strcmp(source->name, target->name) != 0 ||
            source->flags != target->flags ||
            !cmeta_type_equal(source->type, target->type))
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
        left->properties != right->properties ||
        left->result_flags != right->result_flags)
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

bool cmeta_function_abi_contract_compatible(const cmeta_function_abi_desc *expected,
                                          const cmeta_function_abi_desc *candidate) {
    size_t i;
    const cmeta_function_desc *a;
    const cmeta_function_desc *b;
    if (!cmeta_function_abi_desc_valid(expected) ||
        !cmeta_function_abi_desc_valid(candidate))
        return false;
    a = expected->function;
    b = candidate->function;
    if (expected->return_carrier == CMETA_ABI_UNSPECIFIED ||
        expected->return_carrier != candidate->return_carrier ||
        expected->param_count != candidate->param_count ||
        !cmeta_type_equal(a->return_type, b->return_type) ||
        a->result_flags != b->result_flags || a->effects != b->effects ||
        a->properties != b->properties)
        return false;
    for (i = 0u; i < expected->param_count; ++i) {
        if (expected->param_carriers[i] == CMETA_ABI_UNSPECIFIED ||
            expected->param_carriers[i] != candidate->param_carriers[i] ||
            a->params[i].flags != b->params[i].flags ||
            !cmeta_type_equal(a->params[i].type, b->params[i].type))
            return false;
    }
    return true;
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
