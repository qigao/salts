#include "internal.h"

#include <stdint.h>
#include <string.h>

static cbind_status cbind_struct_error(cbind_error *error,
                                       cbind_status status,
                                       const cmeta_data_desc *shape,
                                       const cmeta_data_field_desc *field,
                                       size_t depth) {
    cbind_error_set(error, status, CSERDE_OK, shape, field, depth);
    return status;
}

size_t cbind_bitmap_bytes(size_t field_count) {
    return field_count / 8u + (field_count % 8u != 0u ? 1u : 0u);
}

static bool cbind_cycle_contains(const cbind_validation_frame *parent,
                                 const cmeta_data_desc *shape) {
    const cbind_validation_frame *frame;

    for (frame = parent; frame != NULL; frame = frame->parent)
        if (frame->shape == shape)
            return true;
    return false;
}

static cbind_status cbind_validate_struct_shape(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    const cbind_validation_frame *parent,
    cbind_error *error) {
    const cmeta_data_struct_shape *struct_shape;
    const cmeta_struct_desc *layout;
    cbind_validation_frame frame;
    size_t current_depth = depth + 1u;
    size_t i;
    size_t j;

    if (shape == NULL || !cmeta_data_desc_valid(shape) ||
        shape->kind != CMETA_DATA_STRUCT)
        return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                  current_depth);
    if (cbind_cycle_contains(parent, shape))
        return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                  current_depth);

    struct_shape = (const cmeta_data_struct_shape *)shape->shape;
    layout = struct_shape->layout;
    if (shape->storage_type == NULL || layout == NULL ||
        shape->storage_type->size != layout->size ||
        shape->storage_type->align != layout->align)
        return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape, NULL,
                                  current_depth);

    frame.shape = shape;
    frame.parent = parent;

    for (i = 0u; i < struct_shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &struct_shape->fields[i];
        const cmeta_field_desc *reflected =
            cmeta_struct_find_field(layout, field->name);
        const cmeta_data_desc *child = field->value;
        cbind_status status;

        if (reflected == NULL || child == NULL ||
            reflected->offset != field->offset)
            return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape, field,
                                      current_depth);

        for (j = 0u; j < i; ++j) {
            const cmeta_data_field_desc *prior = &struct_shape->fields[j];
            const cmeta_field_desc *prior_reflected =
                cmeta_struct_find_field(layout, prior->name);
            if (strcmp(prior->name, field->name) == 0 ||
                prior_reflected == reflected)
                return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape,
                                          field, current_depth);
        }

        if (child->kind == CMETA_DATA_STRUCT)
            status = cbind_validate_struct_shape(context, child,
                                                 current_depth, &frame, error);
        else {
            size_t ignored_scratch = 0u;
            status = cbind_validate_graph(context, child, current_depth,
                                          &frame, 0u, &ignored_scratch, error);
        }
        if (status != CBIND_OK)
            return status;

        if (child->storage_type == NULL ||
            reflected->size != child->storage_type->size ||
            reflected->align != child->storage_type->align ||
            field->offset > shape->storage_type->size ||
            child->storage_type->size >
                shape->storage_type->size - field->offset)
            return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape, field,
                                      current_depth);
    }

    return CBIND_OK;
}

static cbind_status cbind_measure_struct_resources(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error) {
    const cmeta_data_struct_shape *struct_shape =
        (const cmeta_data_struct_shape *)shape->shape;
    size_t current_depth = depth + 1u;
    size_t frame_bytes = cbind_bitmap_bytes(struct_shape->field_count);
    size_t next_active;
    size_t i;

    if (current_depth > context->max_depth)
        return cbind_struct_error(error, CBIND_LIMIT_EXCEEDED, shape, NULL,
                                  current_depth);
    if (active_scratch > SIZE_MAX - frame_bytes)
        return cbind_struct_error(error, CBIND_LIMIT_EXCEEDED, shape, NULL,
                                  current_depth);

    next_active = active_scratch + frame_bytes;
    if (next_active > *max_scratch)
        *max_scratch = next_active;

    for (i = 0u; i < struct_shape->field_count; ++i) {
        const cmeta_data_desc *child = struct_shape->fields[i].value;
        cbind_status status;

        if (child->kind != CMETA_DATA_STRUCT)
            continue;
        status = cbind_measure_struct_resources(context, child, current_depth,
                                                next_active, max_scratch, error);
        if (status != CBIND_OK)
            return status;
    }
    return CBIND_OK;
}

cbind_status cbind_validate_struct_graph(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    const cbind_validation_frame *parent,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error) {
    cbind_status status;

    status = cbind_validate_struct_shape(context, shape, depth, parent, error);
    if (status != CBIND_OK)
        return status;
    return cbind_measure_struct_resources(context, shape, depth, active_scratch,
                                          max_scratch, error);
}

bool cbind_struct_is_empty(const cmeta_data_desc *shape, const void *value) {
    const cmeta_data_struct_shape *struct_shape;
    const unsigned char *base = (const unsigned char *)value;
    size_t i;

    if (shape == NULL || value == NULL || shape->kind != CMETA_DATA_STRUCT)
        return false;
    struct_shape = (const cmeta_data_struct_shape *)shape->shape;
    for (i = 0u; i < struct_shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &struct_shape->fields[i];
        const void *member = base + field->offset;
        bool empty = field->value->kind == CMETA_DATA_STRUCT
                         ? cbind_struct_is_empty(field->value, member)
                         : cbind_value_is_empty(field->value, member);
        if (!empty)
            return false;
    }
    return true;
}

void cbind_struct_reset(const cmeta_data_desc *shape, void *value) {
    const cmeta_data_struct_shape *struct_shape;
    unsigned char *base = (unsigned char *)value;
    size_t i;

    if (shape == NULL || value == NULL || shape->kind != CMETA_DATA_STRUCT)
        return;
    struct_shape = (const cmeta_data_struct_shape *)shape->shape;
    for (i = 0u; i < struct_shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &struct_shape->fields[i];
        void *member = base + field->offset;
        if (field->value->kind == CMETA_DATA_STRUCT)
            cbind_struct_reset(field->value, member);
        else
            cbind_value_reset(field->value, member);
    }
}
