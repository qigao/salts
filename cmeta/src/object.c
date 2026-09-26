#include <cmeta/object.h>

static void cmeta_object_clear(cmeta_object_ref *ref) {
    if (ref != NULL)
        *ref = (cmeta_object_ref)CMETA_OBJECT_REF_INIT;
}

static cmeta_status cmeta_object_contract_status(
    void *object, const cmeta_data_desc *data,
    const cmeta_receiver_method_set *methods) {
    if (object == NULL || !cmeta_data_desc_valid(data))
        return CMETA_INVALID_ARGUMENT;
    if (data->storage_type == NULL ||
        !cmeta_type_desc_valid(data->storage_type))
        return CMETA_TRAIT_MISSING;
    if (methods == NULL)
        return CMETA_OK;
    if (!cmeta_receiver_method_set_valid(methods))
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_type_equal(methods->receiver_type, data->storage_type))
        return CMETA_TYPE_MISMATCH;
    return CMETA_OK;
}

bool cmeta_object_ref_valid(const cmeta_object_ref *ref) {
    if (ref == NULL || ref->size < sizeof(cmeta_object_ref) ||
        ref->lifetime != CMETA_OBJECT_LIFETIME_BORROWED)
        return false;
    return cmeta_object_contract_status(
               ref->object, ref->data, ref->methods) == CMETA_OK;
}

cmeta_status cmeta_object_borrow(
    cmeta_object_ref *out, void *object, const cmeta_data_desc *data,
    const cmeta_receiver_method_set *methods) {
    cmeta_status status;

    if (out == NULL)
        return CMETA_INVALID_ARGUMENT;
    cmeta_object_clear(out);

    status = cmeta_object_contract_status(object, data, methods);
    if (status != CMETA_OK)
        return status;

    out->object = object;
    out->data = data;
    out->methods = methods;
    out->lifetime = CMETA_OBJECT_LIFETIME_BORROWED;
    return CMETA_OK;
}

void cmeta_object_release(cmeta_object_ref *ref) {
    cmeta_object_clear(ref);
}
