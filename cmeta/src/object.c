#include <cmeta/object.h>

static void cmeta_object_clear(cmeta_object_ref *ref) {
    if (ref != NULL)
        *ref = (cmeta_object_ref)CMETA_OBJECT_REF_INIT;
}

bool cmeta_object_lifecycle_valid(
    const cmeta_object_lifecycle *lifecycle) {
    bool shared_pair;

    if (lifecycle == NULL || lifecycle->size < sizeof(*lifecycle))
        return false;
    shared_pair =
        (lifecycle->retain == NULL) == (lifecycle->release == NULL);
    return shared_pair &&
           (lifecycle->retain != NULL || lifecycle->destroy != NULL);
}

bool cmeta_object_method_provider_valid(
    const cmeta_object_method_provider *provider) {
    return provider != NULL && provider->size >= sizeof(*provider) &&
           cmeta_receiver_method_set_valid(provider->methods) &&
           provider->bind != NULL;
}

bool cmeta_object_field_provider_valid(
    const cmeta_object_field_provider *provider) {
    return provider != NULL && provider->size >= sizeof(*provider) &&
           cmeta_data_desc_valid(provider->data) &&
           provider->data->kind == CMETA_DATA_STRUCT &&
           provider->data->shape != NULL &&
           provider->assign != NULL;
}

static cmeta_status cmeta_object_contract_status(
    void *object, const cmeta_data_desc *data,
    const cmeta_object_field_provider *field_provider,
    const cmeta_receiver_method_set *methods,
    const cmeta_object_method_provider *method_provider) {
    if (object == NULL || !cmeta_data_desc_valid(data))
        return CMETA_INVALID_ARGUMENT;
    if (data->storage_type == NULL ||
        !cmeta_type_desc_valid(data->storage_type))
        return CMETA_TRAIT_MISSING;
    if (methods != NULL) {
        if (!cmeta_receiver_method_set_valid(methods))
            return CMETA_INVALID_ARGUMENT;
        if (!cmeta_type_equal(methods->receiver_type, data->storage_type))
            return CMETA_TYPE_MISMATCH;
    }
    if (field_provider != NULL) {
        if (!cmeta_object_field_provider_valid(field_provider) ||
            field_provider->data != data)
            return CMETA_INVALID_ARGUMENT;
    }
    if (method_provider != NULL) {
        if (!cmeta_object_method_provider_valid(method_provider) ||
            method_provider->methods != methods)
            return CMETA_INVALID_ARGUMENT;
    }
    return CMETA_OK;
}

bool cmeta_object_ref_valid(const cmeta_object_ref *ref) {
    if (ref == NULL || ref->size < sizeof(cmeta_object_ref) ||
        cmeta_object_contract_status(
            ref->object, ref->data, ref->field_provider, ref->methods,
            ref->method_provider) != CMETA_OK)
        return false;

    switch (ref->lifetime) {
    case CMETA_OBJECT_LIFETIME_BORROWED:
        return ref->lifecycle == NULL;
    case CMETA_OBJECT_LIFETIME_SHARED:
        return cmeta_object_lifecycle_valid(ref->lifecycle) &&
               ref->lifecycle->retain != NULL &&
               ref->lifecycle->release != NULL;
    case CMETA_OBJECT_LIFETIME_OWNED:
        return cmeta_object_lifecycle_valid(ref->lifecycle) &&
               ref->lifecycle->destroy != NULL;
    default:
        return false;
    }
}

cmeta_status cmeta_object_borrow(
    cmeta_object_ref *out, void *object, const cmeta_data_desc *data,
    const cmeta_receiver_method_set *methods) {
    cmeta_status status;

    if (out == NULL)
        return CMETA_INVALID_ARGUMENT;
    cmeta_object_clear(out);

    status = cmeta_object_contract_status(
        object, data, NULL, methods, NULL);
    if (status != CMETA_OK)
        return status;

    out->object = object;
    out->data = data;
    out->field_provider = NULL;
    out->methods = methods;
    out->method_provider = NULL;
    out->lifetime = CMETA_OBJECT_LIFETIME_BORROWED;
    out->lifecycle = NULL;
    return CMETA_OK;
}

cmeta_status cmeta_object_borrow_with_provider(
    cmeta_object_ref *out, void *object, const cmeta_data_desc *data,
    const cmeta_object_method_provider *provider) {
    cmeta_status status;

    if (out == NULL)
        return CMETA_INVALID_ARGUMENT;
    cmeta_object_clear(out);
    if (!cmeta_object_method_provider_valid(provider))
        return CMETA_INVALID_ARGUMENT;

    status = cmeta_object_contract_status(
        object, data, NULL, provider->methods, provider);
    if (status != CMETA_OK)
        return status;

    out->object = object;
    out->data = data;
    out->field_provider = NULL;
    out->methods = provider->methods;
    out->method_provider = provider;
    out->lifetime = CMETA_OBJECT_LIFETIME_BORROWED;
    out->lifecycle = NULL;
    return CMETA_OK;
}

cmeta_status cmeta_object_borrow_with_providers(
    cmeta_object_ref *out, void *object, const cmeta_data_desc *data,
    const cmeta_object_field_provider *field_provider,
    const cmeta_object_method_provider *method_provider) {
    const cmeta_receiver_method_set *methods = NULL;
    cmeta_status status;

    if (out == NULL || (field_provider == NULL && method_provider == NULL))
        return CMETA_INVALID_ARGUMENT;
    cmeta_object_clear(out);

    if (field_provider != NULL &&
        !cmeta_object_field_provider_valid(field_provider))
        return CMETA_INVALID_ARGUMENT;
    if (method_provider != NULL) {
        if (!cmeta_object_method_provider_valid(method_provider))
            return CMETA_INVALID_ARGUMENT;
        methods = method_provider->methods;
    }

    status = cmeta_object_contract_status(
        object, data, field_provider, methods, method_provider);
    if (status != CMETA_OK)
        return status;

    out->object = object;
    out->data = data;
    out->field_provider = field_provider;
    out->methods = methods;
    out->method_provider = method_provider;
    out->lifetime = CMETA_OBJECT_LIFETIME_BORROWED;
    out->lifecycle = NULL;
    return CMETA_OK;
}

cmeta_status cmeta_object_share(
    cmeta_object_ref *ref, const cmeta_object_lifecycle *lifecycle) {
    cmeta_status status;

    if (!cmeta_object_ref_valid(ref) ||
        ref->lifetime != CMETA_OBJECT_LIFETIME_BORROWED ||
        !cmeta_object_lifecycle_valid(lifecycle) ||
        lifecycle->retain == NULL || lifecycle->release == NULL)
        return CMETA_INVALID_ARGUMENT;

    status = lifecycle->retain(lifecycle->context, ref->object);
    if (status != CMETA_OK)
        return status;

    ref->lifetime = CMETA_OBJECT_LIFETIME_SHARED;
    ref->lifecycle = lifecycle;
    return CMETA_OK;
}

cmeta_status cmeta_object_take(
    cmeta_object_ref *ref, const cmeta_object_lifecycle *lifecycle) {
    if (!cmeta_object_ref_valid(ref) ||
        ref->lifetime != CMETA_OBJECT_LIFETIME_BORROWED ||
        !cmeta_object_lifecycle_valid(lifecycle) ||
        lifecycle->destroy == NULL)
        return CMETA_INVALID_ARGUMENT;

    ref->lifetime = CMETA_OBJECT_LIFETIME_OWNED;
    ref->lifecycle = lifecycle;
    return CMETA_OK;
}

void cmeta_object_release(cmeta_object_ref *ref) {
    cmeta_object_lifetime lifetime;
    const cmeta_object_lifecycle *lifecycle;
    void *object;

    if (ref == NULL)
        return;

    lifetime = ref->lifetime;
    lifecycle = ref->lifecycle;
    object = ref->object;
    cmeta_object_clear(ref);

    if (lifecycle == NULL || object == NULL)
        return;
    if (lifetime == CMETA_OBJECT_LIFETIME_SHARED &&
        lifecycle->release != NULL)
        lifecycle->release(lifecycle->context, object);
    else if (lifetime == CMETA_OBJECT_LIFETIME_OWNED &&
             lifecycle->destroy != NULL)
        lifecycle->destroy(lifecycle->context, object);
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

cmeta_status cmeta_object_field_assign(
    cmeta_object_ref *ref, const char *name,
    const cmeta_data_desc *value_data, const void *value) {
    const cmeta_data_field_desc *field = NULL;
    cmeta_status status;

    if (value == NULL || !cmeta_data_desc_valid(value_data))
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_object_field_status(ref, name, &field, NULL);
    if (status != CMETA_OK)
        return status;
    if (!cmeta_data_desc_equal(field->value, value_data))
        return CMETA_TYPE_MISMATCH;
    if (!cmeta_object_field_provider_valid(ref->field_provider) ||
        ref->field_provider->data != ref->data)
        return CMETA_TRAIT_MISSING;

    return ref->field_provider->assign(
        ref->field_provider->context, ref->object, field, value);
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
