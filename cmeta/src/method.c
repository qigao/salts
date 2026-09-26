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
        !cmeta_type_desc_valid(set->receiver_type))
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
