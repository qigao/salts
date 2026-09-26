#include <cmeta/object.h>

#include <string.h>

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


static cmeta_status cmeta_object_field_status(
    const cmeta_object_ref *ref, const char *name,
    const cmeta_data_field_desc **out_field, const void **out_value) {
    const cmeta_data_struct_shape *shape;
    const cmeta_data_field_desc *field;
    const cmeta_data_desc *value;
    size_t owner_size;

    if (out_field != NULL)
        *out_field = NULL;
    if (out_value != NULL)
        *out_value = NULL;

    if (!cmeta_object_ref_valid(ref) || name == NULL || name[0] == '\0')
        return CMETA_INVALID_ARGUMENT;
    if (ref->data->kind != CMETA_DATA_STRUCT || ref->data->shape == NULL)
        return CMETA_TRAIT_MISSING;

    shape = (const cmeta_data_struct_shape *)ref->data->shape;
    field = cmeta_data_struct_find_field(shape, name);
    if (field == NULL)
        return CMETA_INVALID_ARGUMENT;

    value = field->value;
    if (!cmeta_data_desc_valid(value) || value->storage_type == NULL)
        return CMETA_INVALID_ARGUMENT;

    owner_size = ref->data->storage_type->size;
    if (field->offset > owner_size ||
        value->storage_type->size > owner_size - field->offset ||
        (value->storage_type->align != 0u &&
         (field->offset % value->storage_type->align) != 0u))
        return CMETA_INVALID_ARGUMENT;

    if (out_field != NULL)
        *out_field = field;
    if (out_value != NULL)
        *out_value = (const unsigned char *)ref->object + field->offset;
    return CMETA_OK;
}

cmeta_status cmeta_object_field_read(
    const cmeta_object_ref *ref, const char *name,
    const cmeta_data_desc **out_data, const void **out_value) {
    const cmeta_data_field_desc *field = NULL;
    const void *value = NULL;
    cmeta_status status;

    if (out_data == NULL || out_value == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out_data = NULL;
    *out_value = NULL;

    status = cmeta_object_field_status(ref, name, &field, &value);
    if (status != CMETA_OK)
        return status;

    *out_data = field->value;
    *out_value = value;
    return CMETA_OK;
}

cmeta_receiver_resolve_status cmeta_object_method_resolve(
    const cmeta_object_ref *ref, const char *owner_name,
    const char *method_name,
    const cmeta_type_desc *const *argument_types, size_t argument_count,
    cmeta_receiver_resolution *out) {
    if (!cmeta_object_ref_valid(ref) || out == NULL ||
        out->size < sizeof(*out))
        return CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT;
    if (ref->methods == NULL)
        return CMETA_RECEIVER_RESOLVE_INVALID_METHOD_SET;
    return cmeta_receiver_method_resolve(
        ref->methods, ref->data->storage_type, owner_name, method_name,
        argument_types, argument_count, out);
}
