#include <cmeta/method.h>

#include <string.h>

static bool cmeta_receiver_method_valid(
    const cmeta_receiver_method_set *set,
    const cmeta_receiver_method *method) {
    const cmeta_param_desc *receiver;

    if (set == NULL || method == NULL ||
        method->name == NULL || method->name[0] == '\0' ||
        !cmeta_function_desc_valid(method->function) ||
        !cmeta_function_abi_desc_valid(method->abi))
        return false;

    if (!cmeta_function_desc_equal(method->function, method->abi->function))
        return false;

    receiver = cmeta_function_receiver(method->function);
    if (receiver == NULL || receiver->type == NULL ||
        receiver->type->kind != CMETA_T_POINTER ||
        receiver->type->pointee == NULL)
        return false;

    return cmeta_type_equal(receiver->type->pointee, set->receiver_type);
}

bool cmeta_receiver_method_set_valid(const cmeta_receiver_method_set *set) {
    size_t i;
    size_t j;

    if (set == NULL || set->size < sizeof(*set) ||
        !cmeta_type_desc_valid(set->receiver_type) ||
        set->owner_name == NULL || set->owner_name[0] == '\0')
        return false;

    if (set->method_count != 0u && set->methods == NULL)
        return false;

    for (i = 0u; i < set->method_count; ++i) {
        if (!cmeta_receiver_method_valid(set, &set->methods[i]))
            return false;
        for (j = 0u; j < i; ++j)
            if (strcmp(set->methods[i].name, set->methods[j].name) == 0)
                return false;
    }

    return true;
}

const cmeta_receiver_method *
cmeta_receiver_method_find(const cmeta_receiver_method_set *set,
                           const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0' ||
        !cmeta_receiver_method_set_valid(set))
        return NULL;

    for (i = 0u; i < set->method_count; ++i)
        if (strcmp(set->methods[i].name, name) == 0)
            return &set->methods[i];

    return NULL;
}

cmeta_receiver_resolve_status
cmeta_receiver_method_resolve(
    const cmeta_receiver_method_set *set,
    const cmeta_type_desc *receiver_type,
    const char *owner_name,
    const char *method_name,
    const cmeta_type_desc *const *argument_types,
    size_t argument_count,
    cmeta_receiver_resolution *out) {
    const cmeta_receiver_method *method;
    size_t i;

    if (out == NULL || out->size < sizeof(*out))
        return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;

    out->method = NULL;
    out->argument_index = CMETA_RECEIVER_ARGUMENT_NONE;

    if (receiver_type == NULL || !cmeta_type_desc_valid(receiver_type) ||
        method_name == NULL || method_name[0] == '\0' ||
        (argument_count != 0u && argument_types == NULL))
        return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;

    if (!cmeta_receiver_method_set_valid(set))
        return CMETA_RECEIVER_RESOLVE_INVALID_METHOD_SET;

    if (!cmeta_type_equal(set->receiver_type, receiver_type))
        return CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH;

    if (owner_name != NULL &&
        (owner_name[0] == '\0' || strcmp(owner_name, set->owner_name) != 0))
        return CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH;

    method = cmeta_receiver_method_find(set, method_name);
    if (method == NULL)
        return CMETA_RECEIVER_RESOLVE_METHOD_NOT_FOUND;

    if (method->function->param_count != argument_count + 1u)
        return CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH;

    for (i = 0u; i < argument_count; ++i) {
        if (argument_types[i] == NULL ||
            !cmeta_type_desc_valid(argument_types[i]))
            return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;
        if (!cmeta_type_equal(
                method->function->params[i + 1u].type,
                argument_types[i])) {
            out->argument_index = i;
            return CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH;
        }
    }

    out->method = method;
    return CMETA_RECEIVER_RESOLVE_OK;
}
