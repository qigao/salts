#include <cmeta/operation.h>

#include <string.h>

bool cmeta_receiver_operation_reflection_valid(
    const cmeta_receiver_operation *operation) {
    return operation != NULL && operation->name != NULL &&
           operation->name[0] != '\0' &&
           cmeta_function_abi_desc_valid(operation->abi) &&
           cmeta_function_receiver_valid(operation->abi->function);
}

static bool cmeta_receiver_operation_valid(
    const cmeta_receiver_operation_set *set,
    const cmeta_receiver_operation *operation) {
    const cmeta_param_desc *receiver;

    if (set == NULL || !cmeta_receiver_operation_reflection_valid(operation))
        return false;

    receiver = cmeta_function_receiver(operation->abi->function);
    return cmeta_type_equal(receiver->type->pointee, set->receiver_type);
}

bool cmeta_receiver_operation_set_valid(const cmeta_receiver_operation_set *set) {
    size_t i;
    size_t j;

    if (set == NULL || set->size < sizeof(*set) ||
        !cmeta_type_desc_valid(set->receiver_type) ||
        (set->owner != NULL && !cmeta_generic_desc_valid(set->owner)))
        return false;

    if (set->operation_count != 0u && set->operations == NULL)
        return false;

    for (i = 0u; i < set->operation_count; ++i) {
        if (!cmeta_receiver_operation_valid(set, &set->operations[i]))
            return false;
        for (j = 0u; j < i; ++j)
            if (strcmp(set->operations[i].name, set->operations[j].name) == 0)
                return false;
    }

    return true;
}

const cmeta_receiver_operation *
cmeta_receiver_operation_find(const cmeta_receiver_operation_set *set,
                              const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0' ||
        !cmeta_receiver_operation_set_valid(set))
        return NULL;

    for (i = 0u; i < set->operation_count; ++i)
        if (strcmp(set->operations[i].name, name) == 0)
            return &set->operations[i];

    return NULL;
}


cmeta_receiver_resolve_status
cmeta_receiver_operation_resolve(
    const cmeta_receiver_operation_set *set,
    const cmeta_type_desc *receiver_type,
    const cmeta_generic_desc *owner,
    const char *operation_name,
    const cmeta_type_desc *const *argument_types,
    size_t argument_count,
    cmeta_receiver_resolution *out) {
    const cmeta_receiver_operation *operation;
    size_t i;

    if (out == NULL || out->size < sizeof(*out))
        return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;

    out->operation = NULL;
    out->argument_index = CMETA_RECEIVER_ARGUMENT_NONE;

    if (receiver_type == NULL || !cmeta_type_desc_valid(receiver_type) ||
        (owner != NULL && !cmeta_generic_desc_valid(owner)) ||
        operation_name == NULL || operation_name[0] == '\0' ||
        (argument_count != 0u && argument_types == NULL))
        return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;

    if (!cmeta_receiver_operation_set_valid(set))
        return CMETA_RECEIVER_RESOLVE_INVALID_OPERATION_SET;

    if (!cmeta_type_equal(set->receiver_type, receiver_type))
        return CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH;

    if (owner != NULL &&
        (set->owner == NULL || !cmeta_generic_desc_equal(owner, set->owner)))
        return CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH;

    operation = cmeta_receiver_operation_find(set, operation_name);
    if (operation == NULL)
        return CMETA_RECEIVER_RESOLVE_OPERATION_NOT_FOUND;

    if (operation->abi->function->param_count - 1u != argument_count)
        return CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH;

    for (i = 0u; i < argument_count; ++i) {
        if (argument_types[i] == NULL ||
            !cmeta_type_desc_valid(argument_types[i]))
            return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;
        if (!cmeta_type_equal(
                operation->abi->function->params[i + 1u].type,
                argument_types[i])) {
            out->argument_index = i;
            return CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH;
        }
    }

    out->operation = operation;
    return CMETA_RECEIVER_RESOLVE_OK;
}
