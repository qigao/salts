#include <cmeta/data.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define CMETA_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
#define CMETA_DATA_DESC_PREFIX_SIZE CMETA_FIELD_END(cmeta_data_desc, shape)
#define CMETA_DATA_DESC_BUFFER_OPS_SIZE \
    CMETA_FIELD_END(cmeta_data_desc, buffer_ops)
#define CMETA_DATA_DESC_ENUM_OPS_SIZE \
    CMETA_FIELD_END(cmeta_data_desc, enum_ops)
#define CMETA_DATA_DESC_VARIANT_OPS_SIZE \
    CMETA_FIELD_END(cmeta_data_desc, variant_ops)
#define CMETA_DATA_DESC_FIXED_OPS_SIZE \
    CMETA_FIELD_END(cmeta_data_desc, fixed_ops)
#define CMETA_DATA_DESC_ENUM_BITS_OPS_SIZE \
    CMETA_FIELD_END(cmeta_data_desc, enum_bits_ops)
#define CMETA_DATA_ENUM_BITS_OPS_PREFIX_SIZE \
    CMETA_FIELD_END(cmeta_data_enum_bits_ops, restore_zero)
#define CMETA_ENUM_DOMAIN_PREFIX_SIZE \
    CMETA_FIELD_END(cmeta_enum_domain, declared_mask)
#define CMETA_DATA_BUFFER_OPS_PREFIX_SIZE \
    CMETA_FIELD_END(cmeta_data_buffer_ops, restore_zero)
#define CMETA_DATA_BUFFER_OPS_READ_SIZE \
    CMETA_FIELD_END(cmeta_data_buffer_ops, read)
#define CMETA_DATA_ENUM_OPS_PREFIX_SIZE \
    CMETA_FIELD_END(cmeta_data_enum_ops, restore_zero)
#define CMETA_DATA_VARIANT_OPS_PREFIX_SIZE \
    CMETA_FIELD_END(cmeta_data_variant_ops, restore_zero)
#define CMETA_DATA_FIXED_OPS_PREFIX_SIZE \
    CMETA_FIELD_END(cmeta_data_fixed_ops, restore_zero)

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

static uint64_t cmeta_data_enum_width_mask(uint8_t bits) {
    return bits == 64u ? UINT64_MAX : (UINT64_C(1) << bits) - 1u;
}

static bool cmeta_data_enum_domain_valid(const cmeta_enum_domain *domain) {
    size_t i;
    uint64_t mask = 0u;
    uint64_t width_mask;
    if (domain == NULL || domain->struct_size < CMETA_ENUM_DOMAIN_PREFIX_SIZE ||
        domain->abi_version != CMETA_ENUM_DOMAIN_ABI_VERSION ||
        !cmeta_data_integer_bits_valid(domain->bits) ||
        (domain->signedness != CMETA_ENUM_SIGNED &&
         domain->signedness != CMETA_ENUM_UNSIGNED) ||
        (domain->kind != CMETA_ENUM_ORDINARY && domain->kind != CMETA_ENUM_FLAGS) ||
        (domain->count != 0u && domain->items == NULL))
        return false;
    width_mask = cmeta_data_enum_width_mask(domain->bits);
    for (i = 0u; i < domain->count; ++i) {
        const cmeta_enum_bits_item *item = &domain->items[i];
        if ((item->bits & ~width_mask) != 0u ||
            !cmeta_data_nonempty(item->symbol) || !cmeta_data_nonempty(item->text))
            return false;
        mask |= item->bits;
    }
    return domain->declared_mask ==
           (domain->kind == CMETA_ENUM_FLAGS ? mask : 0u);
}

static bool cmeta_data_enum_bits_valid(const cmeta_enum_domain *domain,
                                      uint64_t bits) {
    size_t i;
    if ((bits & ~cmeta_data_enum_width_mask(domain->bits)) != 0u)
        return false;
    if (domain->kind == CMETA_ENUM_FLAGS)
        return (bits & ~domain->declared_mask) == 0u;
    for (i = 0u; i < domain->count; ++i)
        if (domain->items[i].bits == bits)
            return true;
    return false;
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
    if (tag == NULL ||
        (tag->kind != CMETA_DATA_SINT && tag->kind != CMETA_DATA_UINT &&
         tag->kind != CMETA_DATA_ENUM))
        return false;
    /* Parent variant ops own tag access; tags still require legacy metadata. */
    if (tag->kind == CMETA_DATA_ENUM)
        return cmeta_data_desc_valid(tag) && tag->shape != NULL;
    return cmeta_data_desc_valid(tag);
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

    if (cmeta_data_kind_is_container(desc->kind)) {
        if (desc->shape != NULL) return false;
        /* Kind-only semantic descriptors remain valid. A concrete native
         * provider may additionally select storage and collection_ops. */
        if (desc->storage_type == NULL)
            return desc->struct_size < offsetof(cmeta_data_desc, collection_ops) +
                                        sizeof(desc->collection_ops) ||
                   desc->collection_ops == NULL;
        return cmeta_type_desc_valid(desc->storage_type) &&
               desc->kind != CMETA_DATA_MAP &&
               desc->struct_size >= offsetof(cmeta_data_desc, collection_ops) +
                                    sizeof(desc->collection_ops) &&
               desc->collection_ops != NULL;
    }

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
            if (desc->struct_size >= CMETA_DATA_DESC_ENUM_BITS_OPS_SIZE &&
                desc->enum_bits_ops != NULL) {
                const cmeta_data_enum_bits_ops *ops = desc->enum_bits_ops;
                return desc->shape == NULL && desc->enum_ops == NULL &&
                       ops->struct_size >= CMETA_DATA_ENUM_BITS_OPS_PREFIX_SIZE &&
                       ops->abi_version == CMETA_DATA_ENUM_BITS_OPS_ABI_VERSION &&
                       cmeta_data_enum_domain_valid(ops->domain);
            }
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

static cmeta_status cmeta_data_buffer_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_buffer_ops **out) {
    const cmeta_data_buffer_shape *shape;
    const cmeta_data_buffer_ops *ops;

    if (out != NULL)
        *out = NULL;
    if (!cmeta_data_desc_valid(desc) ||
        (desc->kind != CMETA_DATA_STRING && desc->kind != CMETA_DATA_BYTES) ||
        desc->struct_size < CMETA_DATA_DESC_BUFFER_OPS_SIZE ||
        desc->buffer_ops == NULL)
        return CMETA_INVALID_ARGUMENT;

    ops = desc->buffer_ops;
    if (ops->struct_size < CMETA_DATA_BUFFER_OPS_PREFIX_SIZE ||
        ops->abi_version != CMETA_DATA_BUFFER_OPS_ABI_VERSION ||
        ops->storage_type == NULL ||
        !cmeta_type_desc_valid(ops->storage_type) ||
        !cmeta_data_buffer_ownership_valid(ops->ownership) ||
        ops->is_zero == NULL || ops->assign == NULL ||
        ops->restore_zero == NULL)
        return CMETA_INVALID_ARGUMENT;

    shape = (const cmeta_data_buffer_shape *)desc->shape;
    if (!cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align ||
        shape->ownership != ops->ownership)
        return CMETA_TYPE_MISMATCH;

    if (out != NULL)
        *out = ops;
    return CMETA_OK;
}

const cmeta_data_buffer_ops *cmeta_data_buffer_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_buffer_ops *ops = NULL;
    return cmeta_data_buffer_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_buffer_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->is_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_buffer_assign(
    const cmeta_data_desc *desc, void *object,
    const unsigned char *data, size_t size, size_t max_bytes) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (!ops->is_zero(object))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;

    status = ops->assign(object, data, size, max_bytes);
    if (status != CMETA_OK)
        ops->restore_zero(object);
    return status;
}

cmeta_status cmeta_data_buffer_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_buffer_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    ops->restore_zero(object);
    return ops->is_zero(object) ? CMETA_OK : CMETA_CALLBACK_ERROR;
}

cmeta_status cmeta_data_buffer_read(
    const cmeta_data_desc *desc, const void *object, size_t max_bytes,
    const unsigned char **out_data, size_t *out_size) {
    const cmeta_data_buffer_ops *ops = NULL;
    const unsigned char *data = NULL;
    size_t size = 0u;
    cmeta_status status;

    if (object == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_buffer_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (ops->struct_size < CMETA_DATA_BUFFER_OPS_READ_SIZE ||
        ops->read == NULL)
        return CMETA_TRAIT_MISSING;

    status = ops->read(object, &data, &size);
    if (status != CMETA_OK)
        return status;
    if (size != 0u && data == NULL)
        return CMETA_CALLBACK_ERROR;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;

    *out_data = data;
    *out_size = size;
    return CMETA_OK;
}

static cmeta_status cmeta_data_fixed_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_fixed_ops **out) {
    const cmeta_data_fixed_ops *ops;

    if (out != NULL)
        *out = NULL;
    if (!cmeta_data_desc_valid(desc) ||
        desc->struct_size < CMETA_DATA_DESC_FIXED_OPS_SIZE ||
        desc->fixed_ops == NULL)
        return CMETA_INVALID_ARGUMENT;

    ops = desc->fixed_ops;
    if (ops->struct_size < CMETA_DATA_FIXED_OPS_PREFIX_SIZE ||
        ops->abi_version != CMETA_DATA_FIXED_OPS_ABI_VERSION ||
        ops->storage_type == NULL ||
        !cmeta_type_desc_valid(ops->storage_type) || ops->extent == 0u ||
        ops->is_zero == NULL || ops->copy == NULL ||
        ops->restore_zero == NULL)
        return CMETA_INVALID_ARGUMENT;

    if (!cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align ||
        ops->extent != desc->storage_type->size)
        return CMETA_TYPE_MISMATCH;

    if (out != NULL)
        *out = ops;
    return CMETA_OK;
}

const cmeta_data_fixed_ops *cmeta_data_fixed_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_fixed_ops *ops = NULL;
    return cmeta_data_fixed_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_fixed_extent(
    const cmeta_data_desc *desc, size_t *out) {
    const cmeta_data_fixed_ops *ops = NULL;
    cmeta_status status;

    if (out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_fixed_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->extent;
    return CMETA_OK;
}

cmeta_status cmeta_data_fixed_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out) {
    const cmeta_data_fixed_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_fixed_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->is_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_fixed_copy(
    const cmeta_data_desc *desc, void *destination, const void *source,
    size_t source_extent) {
    const cmeta_data_fixed_ops *ops = NULL;
    cmeta_status status;

    if (destination == NULL || source == NULL || destination == source)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_fixed_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (source_extent != ops->extent)
        return CMETA_TYPE_MISMATCH;
    if (!ops->is_zero(destination))
        return CMETA_INVALID_ARGUMENT;

    status = ops->copy(destination, source);
    if (status != CMETA_OK) {
        ops->restore_zero(destination);
        if (!ops->is_zero(destination))
            return CMETA_CALLBACK_ERROR;
    }
    return status;
}

cmeta_status cmeta_data_fixed_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_fixed_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_fixed_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    ops->restore_zero(object);
    return ops->is_zero(object) ? CMETA_OK : CMETA_CALLBACK_ERROR;
}

static cmeta_status cmeta_data_enum_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_enum_ops **out) {
    const cmeta_data_enum_ops *ops;

    if (out != NULL)
        *out = NULL;
    if (!cmeta_data_desc_valid(desc) || desc->kind != CMETA_DATA_ENUM ||
        desc->struct_size < CMETA_DATA_DESC_ENUM_OPS_SIZE ||
        desc->enum_ops == NULL)
        return CMETA_INVALID_ARGUMENT;

    ops = desc->enum_ops;
    if (ops->struct_size < CMETA_DATA_ENUM_OPS_PREFIX_SIZE ||
        ops->abi_version != CMETA_DATA_ENUM_OPS_ABI_VERSION ||
        ops->storage_type == NULL ||
        !cmeta_type_desc_valid(ops->storage_type) ||
        ops->is_zero == NULL || ops->read == NULL || ops->assign == NULL ||
        ops->restore_zero == NULL)
        return CMETA_INVALID_ARGUMENT;

    if (!cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align)
        return CMETA_TYPE_MISMATCH;

    if (out != NULL)
        *out = ops;
    return CMETA_OK;
}

const cmeta_data_enum_ops *cmeta_data_enum_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_enum_ops *ops = NULL;
    return cmeta_data_enum_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_enum_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out) {
    const cmeta_data_enum_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->is_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_enum_read(
    const cmeta_data_desc *desc, const void *object, int64_t *out) {
    const cmeta_data_enum_shape *shape;
    const cmeta_data_enum_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    status = ops->read(object, out);
    if (status != CMETA_OK)
        return status;
    shape = (const cmeta_data_enum_shape *)desc->shape;
    return cmeta_enum_item_by_value(shape->meta, *out) != NULL
               ? CMETA_OK
               : CMETA_CALLBACK_ERROR;
}

cmeta_status cmeta_data_enum_assign(
    const cmeta_data_desc *desc, void *object, int64_t value) {
    const cmeta_data_enum_shape *shape;
    const cmeta_data_enum_ops *ops = NULL;
    int64_t actual = 0;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    shape = (const cmeta_data_enum_shape *)desc->shape;
    if (cmeta_enum_item_by_value(shape->meta, value) == NULL ||
        !ops->is_zero(object))
        return CMETA_INVALID_ARGUMENT;

    status = ops->assign(object, value);
    if (status == CMETA_OK) {
        status = ops->read(object, &actual);
        if (status == CMETA_OK && actual != value)
            status = CMETA_CALLBACK_ERROR;
    }
    if (status != CMETA_OK)
        ops->restore_zero(object);
    return status;
}

cmeta_status cmeta_data_enum_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_enum_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    ops->restore_zero(object);
    return ops->is_zero(object) ? CMETA_OK : CMETA_CALLBACK_ERROR;
}

static cmeta_status cmeta_data_enum_bits_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_enum_bits_ops **out) {
    const cmeta_data_enum_bits_ops *ops;
    if (!cmeta_data_desc_valid(desc) || desc->kind != CMETA_DATA_ENUM ||
        desc->struct_size < CMETA_DATA_DESC_ENUM_BITS_OPS_SIZE ||
        desc->enum_bits_ops == NULL)
        return CMETA_INVALID_ARGUMENT;
    ops = desc->enum_bits_ops;
    if (!cmeta_type_desc_valid(ops->storage_type) || ops->is_zero == NULL ||
        ops->read == NULL || ops->assign == NULL || ops->restore_zero == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align)
        return CMETA_TYPE_MISMATCH;
    *out = ops;
    return CMETA_OK;
}

const cmeta_data_enum_bits_ops *cmeta_data_enum_bits_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_enum_bits_ops *ops = NULL;
    return cmeta_data_enum_bits_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_enum_bits_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out) {
    const cmeta_data_enum_bits_ops *ops = NULL;
    cmeta_status status;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_bits_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->is_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_enum_read_bits(
    const cmeta_data_desc *desc, const void *object, uint64_t *out) {
    const cmeta_data_enum_bits_ops *ops = NULL;
    uint64_t actual = 0u;
    cmeta_status status;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_bits_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (ops->read(object, &actual) != CMETA_OK ||
        !cmeta_data_enum_bits_valid(ops->domain, actual))
        return CMETA_CALLBACK_ERROR;
    *out = actual;
    return CMETA_OK;
}

cmeta_status cmeta_data_enum_assign_bits(
    const cmeta_data_desc *desc, void *object, uint64_t bits) {
    const cmeta_data_enum_bits_ops *ops = NULL;
    uint64_t actual = 0u;
    cmeta_status status;
    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_bits_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (!cmeta_data_enum_bits_valid(ops->domain, bits) || !ops->is_zero(object))
        return CMETA_INVALID_ARGUMENT;
    if (ops->assign(object, bits) != CMETA_OK ||
        ops->read(object, &actual) != CMETA_OK || actual != bits) {
        ops->restore_zero(object);
        return CMETA_CALLBACK_ERROR;
    }
    return CMETA_OK;
}

cmeta_status cmeta_data_enum_bits_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_enum_bits_ops *ops = NULL;
    cmeta_status status;
    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_enum_bits_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    ops->restore_zero(object);
    return ops->is_zero(object) ? CMETA_OK : CMETA_CALLBACK_ERROR;
}

static cmeta_status cmeta_data_variant_ops_status(
    const cmeta_data_desc *desc, const cmeta_data_variant_ops **out) {
    const cmeta_data_variant_ops *ops;

    if (out != NULL)
        *out = NULL;
    if (!cmeta_data_desc_valid(desc) || desc->kind != CMETA_DATA_VARIANT ||
        desc->struct_size < CMETA_DATA_DESC_VARIANT_OPS_SIZE ||
        desc->variant_ops == NULL)
        return CMETA_INVALID_ARGUMENT;

    ops = desc->variant_ops;
    if (ops->struct_size < CMETA_DATA_VARIANT_OPS_PREFIX_SIZE ||
        ops->abi_version != CMETA_DATA_VARIANT_OPS_ABI_VERSION ||
        ops->storage_type == NULL ||
        !cmeta_type_desc_valid(ops->storage_type) ||
        ops->is_zero == NULL || ops->active_tag == NULL ||
        ops->select == NULL || ops->restore_zero == NULL)
        return CMETA_INVALID_ARGUMENT;

    if (!cmeta_type_equal(desc->storage_type, ops->storage_type) ||
        desc->storage_type->kind != ops->storage_type->kind ||
        desc->storage_type->size != ops->storage_type->size ||
        desc->storage_type->align != ops->storage_type->align)
        return CMETA_TYPE_MISMATCH;

    if (out != NULL)
        *out = ops;
    return CMETA_OK;
}

const cmeta_data_variant_ops *cmeta_data_variant_ops_of(
    const cmeta_data_desc *desc) {
    const cmeta_data_variant_ops *ops = NULL;
    return cmeta_data_variant_ops_status(desc, &ops) == CMETA_OK ? ops : NULL;
}

cmeta_status cmeta_data_variant_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out) {
    const cmeta_data_variant_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_variant_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    *out = ops->is_zero(object);
    return CMETA_OK;
}

cmeta_status cmeta_data_variant_active_tag(
    const cmeta_data_desc *desc, const void *object, int64_t *out) {
    const cmeta_data_variant_shape *shape;
    const cmeta_data_variant_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_variant_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    if (ops->is_zero(object))
        return CMETA_INVALID_ARGUMENT;
    status = ops->active_tag(object, out);
    if (status != CMETA_OK)
        return status;
    shape = (const cmeta_data_variant_shape *)desc->shape;
    return cmeta_data_variant_case_by_tag(shape, *out) != NULL
               ? CMETA_OK
               : CMETA_CALLBACK_ERROR;
}

cmeta_status cmeta_data_variant_select(
    const cmeta_data_desc *desc, void *object, int64_t tag) {
    const cmeta_data_variant_ops *ops = NULL;
    const cmeta_data_variant_shape *shape;
    int64_t active = 0;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_variant_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    shape = (const cmeta_data_variant_shape *)desc->shape;
    if (cmeta_data_variant_case_by_tag(shape, tag) == NULL ||
        !ops->is_zero(object))
        return CMETA_INVALID_ARGUMENT;

    status = ops->select(object, tag);
    if (status == CMETA_OK) {
        if (ops->is_zero(object)) {
            status = CMETA_CALLBACK_ERROR;
        } else {
            status = ops->active_tag(object, &active);
            if (status == CMETA_OK && active != tag)
                status = CMETA_CALLBACK_ERROR;
        }
    }
    if (status != CMETA_OK)
        ops->restore_zero(object);
    return status;
}

cmeta_status cmeta_data_variant_restore_zero(
    const cmeta_data_desc *desc, void *object) {
    const cmeta_data_variant_ops *ops = NULL;
    cmeta_status status;

    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = cmeta_data_variant_ops_status(desc, &ops);
    if (status != CMETA_OK)
        return status;
    ops->restore_zero(object);
    return ops->is_zero(object) ? CMETA_OK : CMETA_CALLBACK_ERROR;
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

static const cmeta_data_integer_shape cmeta_data_int8_shape = {8u};
static const cmeta_data_integer_shape cmeta_data_uint8_shape = {8u};
static const cmeta_data_integer_shape cmeta_data_int16_shape = {16u};
static const cmeta_data_integer_shape cmeta_data_uint16_shape = {16u};
static const cmeta_data_integer_shape cmeta_data_int32_shape = {32u};
static const cmeta_data_integer_shape cmeta_data_uint32_shape = {32u};
static const cmeta_data_integer_shape cmeta_data_int64_shape = {64u};
static const cmeta_data_integer_shape cmeta_data_uint64_shape = {64u};
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

const cmeta_data_desc cmeta_data_int8 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.int8.data", "int8", CMETA_DATA_SINT, &cmeta_type_int8,
    &cmeta_data_int8_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_uint8 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.uint8.data", "uint8", CMETA_DATA_UINT, &cmeta_type_uint8,
    &cmeta_data_uint8_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_int16 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.int16.data", "int16", CMETA_DATA_SINT, &cmeta_type_int16,
    &cmeta_data_int16_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_uint16 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.uint16.data", "uint16", CMETA_DATA_UINT, &cmeta_type_uint16,
    &cmeta_data_uint16_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_int32 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.int32.data", "int32", CMETA_DATA_SINT, &cmeta_type_int32,
    &cmeta_data_int32_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_uint32 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.uint32.data", "uint32", CMETA_DATA_UINT, &cmeta_type_uint32,
    &cmeta_data_uint32_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_int64 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.int64.data", "int64", CMETA_DATA_SINT, &cmeta_type_int64,
    &cmeta_data_int64_shape, NULL, NULL, NULL, NULL, NULL};
const cmeta_data_desc cmeta_data_uint64 = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.uint64.data", "uint64", CMETA_DATA_UINT, &cmeta_type_uint64,
    &cmeta_data_uint64_shape, NULL, NULL, NULL, NULL, NULL};

const cmeta_data_desc cmeta_data_bool = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.bool.data", "bool", CMETA_DATA_BOOL, &cmeta_type_bool, NULL,
    NULL, NULL, NULL, NULL, NULL
};
const cmeta_data_desc cmeta_data_int = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.int.data", "int", CMETA_DATA_SINT, &cmeta_type_int,
    &cmeta_data_int_shape, NULL, NULL, NULL, NULL, NULL
};
const cmeta_data_desc cmeta_data_long = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.long.data", "long", CMETA_DATA_SINT, &cmeta_type_long,
    &cmeta_data_long_shape, NULL, NULL, NULL, NULL, NULL
};
const cmeta_data_desc cmeta_data_size = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.size.data", "size_t", CMETA_DATA_UINT, &cmeta_type_size,
    &cmeta_data_size_shape, NULL, NULL, NULL, NULL, NULL
};
const cmeta_data_desc cmeta_data_float = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.float.data", "float", CMETA_DATA_FLOAT, &cmeta_type_float,
    &cmeta_data_float_shape_value, NULL, NULL, NULL, NULL, NULL
};
const cmeta_data_desc cmeta_data_double = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.double.data", "double", CMETA_DATA_FLOAT, &cmeta_type_double,
    &cmeta_data_double_shape, NULL, NULL, NULL, NULL, NULL
};

const cmeta_data_desc cmeta_data_sequence = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.data.sequence", "sequence", CMETA_DATA_SEQUENCE, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL
};
const cmeta_data_desc cmeta_data_set = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.data.set", "set", CMETA_DATA_SET, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL
};
const cmeta_data_desc cmeta_data_map = {
    CMETA_DATA_DESC_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "cmeta.data.map", "map", CMETA_DATA_MAP, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL
};

#undef CMETA_DATA_VARIANT_OPS_PREFIX_SIZE
#undef CMETA_DATA_FIXED_OPS_PREFIX_SIZE
#undef CMETA_DATA_ENUM_OPS_PREFIX_SIZE
#undef CMETA_DATA_ENUM_BITS_OPS_PREFIX_SIZE
#undef CMETA_ENUM_DOMAIN_PREFIX_SIZE
#undef CMETA_DATA_DESC_ENUM_BITS_OPS_SIZE
#undef CMETA_DATA_BUFFER_OPS_READ_SIZE
#undef CMETA_DATA_BUFFER_OPS_PREFIX_SIZE
#undef CMETA_DATA_DESC_VARIANT_OPS_SIZE
#undef CMETA_DATA_DESC_FIXED_OPS_SIZE
#undef CMETA_DATA_DESC_ENUM_OPS_SIZE
#undef CMETA_DATA_DESC_BUFFER_OPS_SIZE
#undef CMETA_DATA_DESC_PREFIX_SIZE
#undef CMETA_FIELD_END
