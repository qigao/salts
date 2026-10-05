#include <cmeta/fixed_array.h>

#include <stdlib.h>

bool cmeta_data_fixed_array_spec_valid(const cmeta_data_fixed_array_spec *spec) {
    const cmeta_type_desc *element;
    if (spec == NULL || spec->count == 0u ||
        !cmeta_type_desc_valid(spec->storage_type) ||
        !cmeta_data_desc_valid(spec->element))
        return false;
    element = spec->element->storage_type;
    return element != NULL && element->size != 0u && element->align != 0u &&
           spec->count <= SIZE_MAX / element->size &&
           spec->storage_type->size == spec->count * element->size &&
           spec->storage_type->align >= element->align &&
           cmeta_data_value_copy_supported(spec->element) &&
           cmeta_data_value_move_supported(spec->element);
}

static void *array_slot(const cmeta_data_fixed_array_spec *spec,
                        void *object, size_t index) {
    return (unsigned char *)object + index * spec->element->storage_type->size;
}

static const void *array_const_slot(const cmeta_data_fixed_array_spec *spec,
                                    const void *object, size_t index) {
    return (const unsigned char *)object +
           index * spec->element->storage_type->size;
}

bool cmeta_data_fixed_array_is_zero(
    const cmeta_data_fixed_array_spec *spec, const void *object) {
    size_t i;
    if (!cmeta_data_fixed_array_spec_valid(spec) || object == NULL) return false;
    for (i = 0u; i < spec->count; ++i) {
        bool zero = false;
        if (cmeta_data_value_is_zero(spec->element,
                array_const_slot(spec, object, i), &zero) != CMETA_OK || !zero)
            return false;
    }
    return true;
}

cmeta_status cmeta_data_fixed_array_init_zero(
    const cmeta_data_fixed_array_spec *spec, void *object) {
    size_t i;
    if (!cmeta_data_fixed_array_spec_valid(spec) || object == NULL)
        return CMETA_INVALID_ARGUMENT;
    for (i = 0u; i < spec->count; ++i)
        if (cmeta_data_value_init_zero(spec->element,
                array_slot(spec, object, i)) != CMETA_OK)
            abort(); /* The declared array element zero initializer is no-fail. */
    return CMETA_OK;
}

void cmeta_data_fixed_array_restore_zero(
    const cmeta_data_fixed_array_spec *spec, void *object) {
    size_t i;
    if (object == NULL) return;
    if (!cmeta_data_fixed_array_spec_valid(spec)) abort();
    i = spec->count;
    while (i != 0u)
        if (cmeta_data_value_restore_zero(spec->element,
                array_slot(spec, object, --i)) != CMETA_OK)
            abort();
}

void cmeta_data_fixed_array_move(
    const cmeta_data_fixed_array_spec *spec, void *destination, void *source) {
    size_t i;
    if (!cmeta_data_fixed_array_spec_valid(spec) || source == NULL ||
        source == destination || !cmeta_data_fixed_array_is_zero(spec, destination))
        abort();
    for (i = 0u; i < spec->count; ++i)
        if (cmeta_data_value_move(spec->element, array_slot(spec, destination, i),
                array_slot(spec, source, i)) != CMETA_OK)
            abort();
}

cmeta_status cmeta_data_fixed_array_copy_construct(
    const cmeta_data_fixed_array_spec *spec, void *destination, const void *source) {
    cmeta_status status;
    size_t i;
    if (source == NULL || source == destination) return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_fixed_array_init_zero(spec, destination);
    if (status != CMETA_OK) return status;
    for (i = 0u; i < spec->count; ++i) {
        status = cmeta_data_value_copy(spec->element,
            array_slot(spec, destination, i), array_const_slot(spec, source, i));
        if (status != CMETA_OK) {
            cmeta_data_fixed_array_restore_zero(spec, destination);
            return status;
        }
    }
    return CMETA_OK;
}

cmeta_status cmeta_data_fixed_array_read(
    const cmeta_data_fixed_array_spec *spec, const void *object,
    cmeta_data_collection_view *out) {
    if (!cmeta_data_fixed_array_spec_valid(spec) || object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    out->data = object;
    out->count = spec->count;
    out->stride = spec->element->storage_type->size;
    out->element = spec->element;
    return CMETA_OK;
}

cmeta_status cmeta_data_fixed_array_foreach(
    const cmeta_data_fixed_array_spec *spec, const void *object,
    cmeta_data_collection_visit_fn visit, void *context, size_t max_items) {
    size_t i;
    if (!cmeta_data_fixed_array_spec_valid(spec) || object == NULL || visit == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (spec->count > max_items) return CMETA_CAPACITY_EXCEEDED;
    for (i = 0u; i < spec->count; ++i) {
        cmeta_status status = visit(context, array_const_slot(spec, object, i));
        if (status != CMETA_OK) return status;
    }
    return CMETA_OK;
}

cmeta_gen_status cmeta_data_fixed_array_next(
    const cmeta_data_fixed_array_spec *spec, const void *object,
    cmeta_range_cursor *cursor, const void **out) {
    if (out != NULL) *out = NULL;
    if (!cmeta_data_fixed_array_spec_valid(spec) || object == NULL ||
        cursor == NULL || out == NULL)
        return CMETA_GEN_ERROR;
    if (cursor->index >= spec->count) return CMETA_GEN_DONE;
    *out = array_const_slot(spec, object, cursor->index++);
    return cursor->index == spec->count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

cmeta_status cmeta_data_fixed_array_collect_begin(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector,
    const cmeta_type_desc *input, size_t limit) {
    if (!cmeta_data_fixed_array_spec_valid(spec) || collector == NULL ||
        collector->context != collector || collector->count != 0u ||
        !cmeta_type_equal(spec->element->storage_type, input))
        return CMETA_INVALID_ARGUMENT;
    if (limit < spec->count) return CMETA_CAPACITY_EXCEEDED;
    return cmeta_data_fixed_array_is_zero(spec, collector->zero_output)
               ? CMETA_OK : CMETA_INVALID_ARGUMENT;
}

cmeta_status cmeta_data_fixed_array_collect_accept(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector,
    const void *value) {
    if (!cmeta_data_fixed_array_spec_valid(spec) || collector == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (collector->count >= spec->count) return CMETA_CAPACITY_EXCEEDED;
    return cmeta_data_value_copy(spec->element,
        array_slot(spec, collector->zero_output, collector->count), value);
}

cmeta_status cmeta_data_fixed_array_collect_finish(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector) {
    if (!cmeta_data_fixed_array_spec_valid(spec) || collector == NULL)
        return CMETA_INVALID_ARGUMENT;
    return collector->count == spec->count ? CMETA_OK : CMETA_TYPE_MISMATCH;
}

void cmeta_data_fixed_array_collect_abort(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector) {
    /* Failed begin must preserve a non-zero output it did not acquire. */
    if (collector != NULL && (collector->state == CMETA_COLLECTOR_BEGUN ||
                             collector->state == CMETA_COLLECTOR_ACCEPTING))
        cmeta_data_fixed_array_restore_zero(spec, collector->zero_output);
}
