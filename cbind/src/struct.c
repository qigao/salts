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

bool cbind_validation_cycle_contains(const cbind_validation_frame *parent,
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
    if (cbind_validation_cycle_contains(parent, shape))
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

        if (child->kind == CMETA_DATA_STRUCT) {
            if (current_depth >= context->max_depth)
                return cbind_struct_error(error, CBIND_LIMIT_EXCEEDED,
                                          child, field, current_depth + 1u);
            status = cbind_validate_struct_shape(context, child,
                                                 current_depth, &frame, error);
        } else if (cbind_data_kind_is_container(child->kind)) {
            status = cbind_validate_container_field(
                context, child, reflected, field, current_depth, error);
        } else {
            size_t ignored_scratch = 0u;
            status = cbind_validate_graph(context, child, current_depth,
                                          &frame, 0u, &ignored_scratch, error);
        }
        if (status != CBIND_OK)
            return status;

        if ((!cbind_data_kind_is_container(child->kind) &&
             (child->storage_type == NULL ||
              reflected->size != child->storage_type->size ||
              reflected->align != child->storage_type->align)) ||
            field->offset > shape->storage_type->size ||
            reflected->size >
                shape->storage_type->size - field->offset)
            return cbind_struct_error(error, CBIND_INVALID_SHAPE, shape, field,
                                      current_depth);
    }

    return CBIND_OK;
}

cbind_status cbind_measure_struct_resources(
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

        if (child->kind == CMETA_DATA_STRUCT) {
            status = cbind_measure_struct_resources(
                context, child, current_depth, next_active, max_scratch, error);
        } else if (child->kind == CMETA_DATA_VARIANT) {
            status = cbind_measure_variant_resources(
                context, child, current_depth, next_active, max_scratch, error);
        } else {
            continue;
        }
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
        const cmeta_field_desc *reflected = cmeta_struct_find_field(
            struct_shape->layout, field->name);
        bool empty;
        if (field->value->kind == CMETA_DATA_STRUCT)
            empty = cbind_struct_is_empty(field->value, member);
        else if (cbind_data_kind_is_container(field->value->kind))
            empty = cbind_container_is_zero(reflected, member);
        else
            empty = cbind_value_is_empty(field->value, member);
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
        const cmeta_field_desc *reflected = cmeta_struct_find_field(
            struct_shape->layout, field->name);
        void *member = base + field->offset;
        if (field->value->kind == CMETA_DATA_STRUCT)
            cbind_struct_reset(field->value, member);
        else if (cbind_data_kind_is_container(field->value->kind))
            cbind_container_reset(reflected, member);
        else
            cbind_value_reset(field->value, member);
    }
}

cbind_status cbind_prepare_struct_containers(
    const cmeta_data_desc *shape, void *value, cbind_error *error,
    size_t depth) {
    const cmeta_data_struct_shape *struct_shape =
        (const cmeta_data_struct_shape *)shape->shape;
    unsigned char *base = (unsigned char *)value;
    size_t current_depth = depth + 1u;
    size_t i;

    for (i = 0u; i < struct_shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &struct_shape->fields[i];
        const cmeta_field_desc *reflected = cmeta_struct_find_field(
            struct_shape->layout, field->name);
        void *member = base + field->offset;
        cmeta_status target;
        cbind_status status;

        if (field->value->kind == CMETA_DATA_STRUCT) {
            status = cbind_prepare_struct_containers(
                field->value, member, error, current_depth);
            if (status != CBIND_OK)
                return status;
            continue;
        }
        if (!cbind_data_kind_is_container(field->value->kind))
            continue;

        target = cmeta_container_bind_types(member, reflected->declared_type);
        if (target != CMETA_OK) {
            cbind_error_set(error, CBIND_TARGET_ERROR, CSERDE_OK,
                            field->value, field, current_depth);
            cbind_error_set_target(error, target);
            return CBIND_TARGET_ERROR;
        }
    }
    return CBIND_OK;
}

const cmeta_data_field_desc *cbind_find_field_slice(
    const cmeta_data_struct_shape *shape,
    const cserde_slice *key,
    size_t *index) {
    size_t i;

    if (shape == NULL || key == NULL)
        return NULL;
    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        size_t name_size = strlen(field->name);

        if (name_size == key->size &&
            (key->size == 0u ||
             memcmp(key->data, field->name, key->size) == 0)) {
            if (index != NULL)
                *index = i;
            return field;
        }
    }
    return NULL;
}

static bool cbind_field_seen(const unsigned char *bitmap, size_t index) {
    return (bitmap[index / 8u] &
            (unsigned char)(1u << (index % 8u))) != 0u;
}

static void cbind_mark_field_seen(unsigned char *bitmap, size_t index) {
    bitmap[index / 8u] |= (unsigned char)(1u << (index % 8u));
}

cbind_status cbind_decode_struct(cbind_decode_state *state,
                                 const cmeta_data_desc *shape,
                                 const cmeta_data_field_desc *parent_field,
                                 size_t depth,
                                 void *out) {
    const cmeta_data_struct_shape *struct_shape =
        (const cmeta_data_struct_shape *)shape->shape;
    size_t current_depth = depth + 1u;
    size_t frame_bytes = cbind_bitmap_bytes(struct_shape->field_count);
    size_t mark;
    unsigned char *bitmap = NULL;
    cserde_token token = {0};
    cbind_status status;

    status = cbind_read_required(state, &token, shape, parent_field,
                                 current_depth);
    if (status != CBIND_OK)
        return status;
    if (token.kind != CSERDE_MAP_BEGIN)
        return cbind_struct_error(state->error, CBIND_TOKEN_MISMATCH,
                                  shape, parent_field, current_depth);

    mark = state->scratch_used;
    if (mark > state->context->scratch_size ||
        frame_bytes > state->context->scratch_size - mark)
        return cbind_struct_error(state->error, CBIND_LIMIT_EXCEEDED,
                                  shape, parent_field, current_depth);

    if (frame_bytes != 0u) {
        bitmap = state->scratch + mark;
        memset(bitmap, 0, frame_bytes);
    }
    state->scratch_used = mark + frame_bytes;

    for (;;) {
        const cmeta_data_field_desc *field;
        size_t field_index = 0u;
        size_t i;

        status = cbind_read_required(state, &token, shape, parent_field,
                                     current_depth);
        if (status != CBIND_OK)
            break;

        if (token.kind == CSERDE_MAP_END) {
            status = CBIND_OK;
            for (i = 0u; i < struct_shape->field_count; ++i) {
                if (!cbind_field_seen(bitmap, i)) {
                    status = cbind_struct_error(state->error,
                                                CBIND_MISSING_FIELD,
                                                shape,
                                                &struct_shape->fields[i],
                                                current_depth);
                    break;
                }
            }
            break;
        }

        if (token.kind != CSERDE_STRING) {
            status = cbind_struct_error(state->error, CBIND_TOKEN_MISMATCH,
                                        shape, parent_field, current_depth);
            break;
        }

        field = cbind_find_field_slice(struct_shape, &token.value.slice,
                                       &field_index);
        if (field == NULL) {
            status = cbind_struct_error(state->error, CBIND_UNKNOWN_FIELD,
                                        shape, parent_field, current_depth);
            break;
        }
        if (cbind_field_seen(bitmap, field_index)) {
            status = cbind_struct_error(state->error, CBIND_DUPLICATE_FIELD,
                                        shape, field, current_depth);
            break;
        }

        status = cbind_decode_value(
            state, field->value, field,
            cmeta_struct_find_field(struct_shape->layout, field->name),
            current_depth, (unsigned char *)out + field->offset);
        if (status != CBIND_OK)
            break;
        cbind_mark_field_seen(bitmap, field_index);
    }

    state->scratch_used = mark;
    return status;
}
