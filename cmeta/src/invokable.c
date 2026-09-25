#include <cmeta/invokable.h>

static cmeta_status cmeta_invokable_validate_pair(
    const cmeta_function_desc *function, const cmeta_callable *callable,
    const cmeta_sig_desc **out_signature) {
    const cmeta_sig_desc *signature;
    size_t i;

    if (out_signature != NULL) *out_signature = NULL;
    if (!cmeta_function_desc_valid(function) || callable == NULL ||
        !cmeta_callable_contract_valid(*callable))
        return CMETA_INVALID_ARGUMENT;

    signature = cmeta_callable_signature(*callable);
    if (signature == NULL || signature->protocol != CMETA_FN_PROTOCOL_VALUE)
        return CMETA_TRAIT_MISSING;
    if (signature->param_count != function->param_count)
        return CMETA_TYPE_MISMATCH;
    if (signature->return_type == NULL || function->return_type == NULL ||
        !cmeta_type_equal(signature->return_type, function->return_type))
        return CMETA_TYPE_MISMATCH;

    for (i = 0u; i < function->param_count; ++i) {
        const cmeta_param_desc *param = cmeta_function_param(function, i);
        if (param == NULL || signature->params[i] == NULL ||
            !cmeta_type_equal(signature->params[i], param->type))
            return CMETA_TYPE_MISMATCH;
    }

    if (callable->meta.effects != function->effects ||
        callable->meta.properties != function->properties)
        return CMETA_TYPE_MISMATCH;

    if (out_signature != NULL) *out_signature = signature;
    return CMETA_OK;
}

cmeta_status cmeta_invokable_bind(
    const cmeta_function_desc *function, cmeta_callable callable,
    cmeta_invokable *out) {
    cmeta_callable bound = {0};
    cmeta_status status;

    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    *out = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    if (function == NULL || !cmeta_callable_bind(callable, &bound))
        return CMETA_INVALID_ARGUMENT;

    status = cmeta_invokable_validate_pair(function, &bound, NULL);
    if (status != CMETA_OK) return status;

    out->function = function;
    out->callable = bound;
    return CMETA_OK;
}

bool cmeta_invokable_valid(const cmeta_invokable *invokable) {
    return invokable != NULL &&
           invokable->size >= sizeof(cmeta_invokable) &&
           invokable->function != NULL &&
           cmeta_invokable_validate_pair(
               invokable->function, &invokable->callable, NULL) == CMETA_OK;
}

cmeta_status cmeta_invokable_invoke(
    const cmeta_invokable *invokable, void *out,
    const void *const *args) {
    const cmeta_type_desc *return_type;
    size_t i;

    if (!cmeta_invokable_valid(invokable))
        return CMETA_INVALID_ARGUMENT;
    return_type = invokable->function->return_type;
    if (!cmeta_type_equal(return_type, &cmeta_type_void) && out == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (invokable->function->param_count != 0u && args == NULL)
        return CMETA_INVALID_ARGUMENT;
    for (i = 0u; i < invokable->function->param_count; ++i)
        if (args[i] == NULL)
            return CMETA_INVALID_ARGUMENT;

    return cmeta_callable_invoke(&invokable->callable, out, args)
               ? CMETA_OK
               : CMETA_CALLBACK_ERROR;
}
