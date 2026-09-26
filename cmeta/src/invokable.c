#include <cmeta/invokable.h>
#include <cmeta/interface.h>
#include <cmeta/method.h>

#include <string.h>
#include <cmeta/method.h>

bool cmeta_function_data_desc_valid(
    const cmeta_function_data_desc *desc) {
    const cmeta_function_desc *function;
    size_t i;

    if (desc == NULL || desc->size < sizeof(*desc) ||
        !cmeta_function_desc_valid(desc->function))
        return false;
    function = desc->function;
    if (desc->param_count != function->param_count ||
        (desc->param_count != 0u && desc->params == NULL))
        return false;

    if (cmeta_type_equal(function->return_type, &cmeta_type_void)) {
        if (desc->return_data != NULL) return false;
    } else {
        if (!cmeta_data_desc_valid(desc->return_data) ||
            desc->return_data->storage_type == NULL ||
            !cmeta_type_equal(
                desc->return_data->storage_type, function->return_type))
            return false;
    }

    for (i = 0u; i < desc->param_count; ++i) {
        const cmeta_param_desc *param = cmeta_function_param(function, i);
        const cmeta_data_desc *data = desc->params[i];
        if (param == NULL || !cmeta_data_desc_valid(data) ||
            data->storage_type == NULL ||
            !cmeta_type_equal(data->storage_type, param->type))
            return false;
    }
    return true;
}

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
    out->data = NULL;
    out->callable = bound;
    return CMETA_OK;
}

cmeta_status cmeta_invokable_bind_data(
    const cmeta_function_data_desc *data, cmeta_callable callable,
    cmeta_invokable *out) {
    cmeta_status status;
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    *out = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    if (!cmeta_function_data_desc_valid(data))
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_invokable_bind(data->function, callable, out);
    if (status != CMETA_OK) return status;
    out->data = data;
    return CMETA_OK;
}

cmeta_status cmeta_interface_method_invokable_bind(
    const cmeta_interface_method_desc *method,
    const cmeta_function_data_desc *data,
    cmeta_callable callable, cmeta_invokable *out) {
    if (method == NULL || data == NULL || out == NULL ||
        !cmeta_interface_method_reflection_valid(method) ||
        !cmeta_function_data_desc_valid(data))
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_function_desc_equal(method->function, data->function))
        return CMETA_TYPE_MISMATCH;
    return cmeta_invokable_bind_data(data, callable, out);
}

cmeta_status cmeta_receiver_method_invokable_bind(
    const cmeta_receiver_method *method,
    const cmeta_function_data_desc *data,
    cmeta_callable callable, cmeta_invokable *out) {
    if (method == NULL || data == NULL || out == NULL ||
        !cmeta_receiver_method_reflection_valid(method) ||
        !cmeta_function_data_desc_valid(data))
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_receiver_method_projection_valid(method, data->function))
        return CMETA_TYPE_MISMATCH;
    return cmeta_invokable_bind_data(data, callable, out);
}

static bool cmeta_object_method_member(
    const cmeta_receiver_method_set *set,
    const cmeta_receiver_method *method) {
    size_t i;
    if (!cmeta_receiver_method_set_valid(set) || method == NULL)
        return false;
    for (i = 0u; i < set->method_count; ++i)
        if (&set->methods[i] == method)
            return true;
    return false;
}

cmeta_status cmeta_object_method_invokable_bind(
    const cmeta_object_ref *object,
    const cmeta_receiver_method *method,
    cmeta_invokable *out) {
    const cmeta_object_method_provider *provider;
    cmeta_object_method_binding binding = CMETA_OBJECT_METHOD_BINDING_INIT;
    cmeta_status status;

    if (out == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    if (!cmeta_object_ref_valid(object) || method == NULL)
        return CMETA_INVALID_ARGUMENT;
    provider = object->method_provider;
    if (!cmeta_object_method_provider_valid(provider))
        return CMETA_TRAIT_MISSING;
    if (provider->methods != object->methods ||
        !cmeta_object_method_member(object->methods, method))
        return CMETA_INVALID_ARGUMENT;

    status = provider->bind(
        provider->context, object->object, method, &binding);
    if (status != CMETA_OK)
        return status;
    if (binding.size < sizeof(binding) || binding.data == NULL)
        return CMETA_INVALID_ARGUMENT;

    return cmeta_receiver_method_invokable_bind(
        method, binding.data, binding.callable, out);
}

bool cmeta_invokable_valid(const cmeta_invokable *invokable) {
    return invokable != NULL &&
           invokable->size >= sizeof(cmeta_invokable) &&
           invokable->function != NULL &&
           cmeta_invokable_validate_pair(
               invokable->function, &invokable->callable, NULL) == CMETA_OK &&
           (invokable->data == NULL ||
            (cmeta_function_data_desc_valid(invokable->data) &&
             cmeta_function_desc_equal(
                 invokable->data->function, invokable->function)));
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
