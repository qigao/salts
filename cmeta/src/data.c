#include <cmeta/data.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define CMETA_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
#define CMETA_DATA_DESC_PREFIX_SIZE CMETA_FIELD_END(cmeta_data_desc, shape)

static bool cmeta_data_nonempty(const char *text) {
    return text != NULL && text[0] != '\0';
}

static bool cmeta_data_integer_bits_valid(uint8_t bits) {
    return bits == 8u || bits == 16u || bits == 32u || bits == 64u;
}

static bool cmeta_data_float_bits_valid(uint8_t bits) {
    return bits == 32u || bits == 64u;
}

static bool cmeta_data_buffer_ownership_valid(
    cmeta_data_buffer_ownership ownership) {
    switch (ownership) {
        case CMETA_DATA_BUFFER_OWNED:
        case CMETA_DATA_BUFFER_BORROWED:
        case CMETA_DATA_BUFFER_CUSTOM:
            return true;
    }
    return false;
}

bool cmeta_data_kind_valid(cmeta_data_kind kind) {
    switch (kind) {
        case CMETA_DATA_BOOL:
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
        case CMETA_DATA_ENUM:
        case CMETA_DATA_STRUCT:
        case CMETA_DATA_VARIANT:
        case CMETA_DATA_SEQUENCE:
        case CMETA_DATA_SET:
        case CMETA_DATA_MAP:
        case CMETA_DATA_CUSTOM:
            return true;
    }
    return false;
}

bool cmeta_data_kind_is_container(cmeta_data_kind kind) {
    return kind == CMETA_DATA_SEQUENCE || kind == CMETA_DATA_SET ||
           kind == CMETA_DATA_MAP;
}

static bool cmeta_data_enum_shape_valid(const cmeta_data_enum_shape *shape) {
    const cmeta_enum_desc *meta;
    if (shape == NULL || shape->meta == NULL)
        return false;
    meta = shape->meta;
    return cmeta_data_nonempty(meta->name) &&
           (meta->count == 0u || meta->items != NULL);
}

static bool cmeta_data_struct_shape_valid(
    const cmeta_data_struct_shape *shape) {
    const cmeta_struct_desc *layout;
    size_t i;

    if (shape == NULL || shape->layout == NULL)
        return false;
    layout = shape->layout;
    if (!cmeta_data_nonempty(layout->name) ||
        (layout->field_count != 0u && layout->fields == NULL) ||
        (shape->field_count != 0u && shape->fields == NULL) ||
        shape->field_count > layout->field_count)
        return false;

    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        const cmeta_field_desc *layout_field;
        if (!cmeta_data_nonempty(field->stable_id) ||
            !cmeta_data_nonempty(field->name) || field->value == NULL)
            return false;
        layout_field = cmeta_struct_find_field(layout, field->name);
        if (layout_field == NULL || layout_field->offset != field->offset)
            return false;
    }
    return true;
}

static bool cmeta_data_variant_tag_kind_valid(const cmeta_data_desc *tag) {
    return tag != NULL && cmeta_data_desc_valid(tag) &&
           (tag->kind == CMETA_DATA_SINT || tag->kind == CMETA_DATA_UINT ||
            tag->kind == CMETA_DATA_ENUM);
}

static bool cmeta_data_variant_shape_valid(
    const cmeta_data_variant_shape *shape) {
    size_t i;
    size_t j;

    if (shape == NULL || !cmeta_data_variant_tag_kind_valid(shape->tag) ||
        (shape->case_count != 0u && shape->cases == NULL))
        return false;

    for (i = 0u; i < shape->case_count; ++i) {
        const cmeta_data_variant_case *item = &shape->cases[i];
        if (!cmeta_data_nonempty(item->stable_id) ||
            !cmeta_data_nonempty(item->name) || item->value == NULL)
            return false;
        for (j = i + 1u; j < shape->case_count; ++j)
            if (shape->cases[j].tag == item->tag)
                return false;
    }
    return true;
}

bool cmeta_data_desc_valid(const cmeta_data_desc *desc) {
    if (desc == NULL || desc->struct_size < CMETA_DATA_DESC_PREFIX_SIZE ||
        desc->abi_version != CMETA_DATA_DESC_ABI_VERSION ||
        !cmeta_data_nonempty(desc->stable_id) ||
        !cmeta_data_nonempty(desc->display_name) ||
        !cmeta_data_kind_valid(desc->kind))
        return false;

    if (cmeta_data_kind_is_container(desc->kind))
        return desc->storage_type == NULL && desc->shape == NULL;

    if (desc->storage_type == NULL || !cmeta_type_desc_valid(desc->storage_type))
        return false;

    switch (desc->kind) {
        case CMETA_DATA_BOOL:
            return desc->shape == NULL;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            return desc->shape != NULL &&
                   cmeta_data_integer_bits_valid(
                       ((const cmeta_data_integer_shape *)desc->shape)->bits);
        case CMETA_DATA_FLOAT:
            return desc->shape != NULL &&
                   cmeta_data_float_bits_valid(
                       ((const cmeta_data_float_shape *)desc->shape)->bits);
        case CMETA_DATA_STRING:
        case CMETA_DATA_BYTES:
            return desc->shape != NULL &&
                   cmeta_data_buffer_ownership_valid(
                       ((const cmeta_data_buffer_shape *)desc->shape)->ownership);
        case CMETA_DATA_ENUM:
            return cmeta_data_enum_shape_valid(
                (const cmeta_data_enum_shape *)desc->shape);
        case CMETA_DATA_STRUCT:
            return cmeta_data_struct_shape_valid(
                (const cmeta_data_struct_shape *)desc->shape);
        case CMETA_DATA_VARIANT:
            return cmeta_data_variant_shape_valid(
                (const cmeta_data_variant_shape *)desc->shape);
        case CMETA_DATA_CUSTOM:
            return desc->shape != NULL;
        case CMETA_DATA_SEQUENCE:
        case CMETA_DATA_SET:
        case CMETA_DATA_MAP:
            return false;
    }
    return false;
}

const cmeta_data_field_desc *cmeta_data_struct_field(
    const cmeta_data_struct_shape *shape, size_t index) {
    return shape != NULL && shape->fields != NULL && index < shape->field_count
               ? &shape->fields[index]
               : NULL;
}

const cmeta_data_field_desc *cmeta_data_struct_find_field(
    const cmeta_data_struct_shape *shape, const char *name) {
    size_t i;
    if (shape == NULL || shape->fields == NULL || name == NULL)
        return NULL;
    for (i = 0u; i < shape->field_count; ++i)
        if (shape->fields[i].name != NULL &&
            strcmp(shape->fields[i].name, name) == 0)
            return &shape->fields[i];
    return NULL;
}

const cmeta_data_variant_case *cmeta_data_variant_case_by_tag(
    const cmeta_data_variant_shape *shape, int64_t tag) {
    size_t i;
    if (shape == NULL || shape->cases == NULL)
        return NULL;
    for (i = 0u; i < shape->case_count; ++i)
        if (shape->cases[i].tag == tag)
            return &shape->cases[i];
    return NULL;
}

static const cmeta_data_integer_shape cmeta_data_int_shape = {
    (uint8_t)(sizeof(int) * CHAR_BIT)
};
static const cmeta_data_integer_shape cmeta_data_long_shape = {
    (uint8_t)(sizeof(long) * CHAR_BIT)
};
static const cmeta_data_integer_shape cmeta_data_size_shape = {
    (uint8_t)(sizeof(size_t) * CHAR_BIT)
};
static const cmeta_data_float_shape cmeta_data_float_shape_value = {
    (uint8_t)(sizeof(float) * CHAR_BIT)
};
static const cmeta_data_float_shape cmeta_data_double_shape = {
    (uint8_t)(sizeof(double) * CHAR_BIT)
};

const cmeta_data_desc cmeta_data_bool = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.bool.data", "bool", CMETA_DATA_BOOL, &cmeta_type_bool, NULL
};
const cmeta_data_desc cmeta_data_int = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.int.data", "int", CMETA_DATA_SINT, &cmeta_type_int,
    &cmeta_data_int_shape
};
const cmeta_data_desc cmeta_data_long = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.long.data", "long", CMETA_DATA_SINT, &cmeta_type_long,
    &cmeta_data_long_shape
};
const cmeta_data_desc cmeta_data_size = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.size.data", "size_t", CMETA_DATA_UINT, &cmeta_type_size,
    &cmeta_data_size_shape
};
const cmeta_data_desc cmeta_data_float = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.float.data", "float", CMETA_DATA_FLOAT, &cmeta_type_float,
    &cmeta_data_float_shape_value
};
const cmeta_data_desc cmeta_data_double = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.double.data", "double", CMETA_DATA_FLOAT, &cmeta_type_double,
    &cmeta_data_double_shape
};

const cmeta_data_desc cmeta_data_sequence = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.data.sequence", "sequence", CMETA_DATA_SEQUENCE, NULL, NULL
};
const cmeta_data_desc cmeta_data_set = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.data.set", "set", CMETA_DATA_SET, NULL, NULL
};
const cmeta_data_desc cmeta_data_map = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.data.map", "map", CMETA_DATA_MAP, NULL, NULL
};

#undef CMETA_DATA_DESC_PREFIX_SIZE
#undef CMETA_FIELD_END
