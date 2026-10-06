#include <cmeta/native/object.h>

cmeta_status cmeta_native_object_i32_admit(const cmeta_object_ref *object,
    const cmeta_receiver_operation *operation, const cmeta_native_object_provider *provider,
    cmeta_native_binding *out) {
    const cmeta_native_binding empty = CMETA_NATIVE_BINDING_INIT;
    cmeta_native_binding candidate = CMETA_NATIVE_BINDING_INIT;
    cmeta_invokable reference = CMETA_INVOKABLE_INIT;
    cmeta_status status;
    if (out == NULL) return CMETA_INVALID_ARGUMENT;
    *out = empty;
    if (!cmeta_object_ref_valid(object) || operation == NULL || provider == NULL || provider->bind == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (provider->reference != object->operation_provider) return CMETA_TYPE_MISMATCH;
    status = cmeta_object_operation_invokable_bind(object, operation, &reference);
    if (status != CMETA_OK) return status;
    status = provider->bind(provider->reference->context, object->object, operation, &candidate);
    if (status != CMETA_OK) return status;
    if (candidate.kind != CMETA_NATIVE_CONTEXT_I32) return CMETA_TYPE_MISMATCH;
    return cmeta_native_context_i32_admit(operation->abi, candidate.abi, candidate.contextual,
        candidate.context, out);
}
