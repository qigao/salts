#include <cmeta/entry.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool cmeta_entry_type_valid(const cmeta_type_desc *type) {
    return type != NULL && cmeta_type_desc_valid(type) && type->size != 0u &&
           type->align != 0u;
}

static bool cmeta_entry_alloc_aligned(const cmeta_type_desc *type,
                                      void **out_storage,
                                      void **out_object) {
    size_t extra;
    size_t bytes;
    uintptr_t address;
    uintptr_t alignment;
    uintptr_t remainder;
    size_t offset;
    void *storage;

    if (out_storage == NULL || out_object == NULL ||
        !cmeta_entry_type_valid(type))
        return false;
    *out_storage = NULL;
    *out_object = NULL;

    extra = type->align - 1u;
    if (type->size > SIZE_MAX - extra)
        return false;
    if (type->align > (size_t)UINTPTR_MAX)
        return false;
    bytes = type->size + extra;
    storage = malloc(bytes);
    if (storage == NULL)
        return false;

    address = (uintptr_t)storage;
    alignment = (uintptr_t)type->align;
    remainder = address % alignment;
    offset = remainder == 0u ? 0u : type->align - (size_t)remainder;

    *out_storage = storage;
    *out_object = (unsigned char *)storage + offset;
    return true;
}

static bool cmeta_entry_copy_construct(void *destination_,
                                       const void *source_) {
    cmeta_entry *destination = (cmeta_entry *)destination_;
    const cmeta_entry *source = (const cmeta_entry *)source_;
    cmeta_entry temporary = {0};
    void *key_object = NULL;
    void *value_object = NULL;
    bool key_constructed = false;

    if (destination == NULL || source == NULL || destination == source)
        return false;
    memset(destination, 0, sizeof(*destination));
    if (!cmeta_entry_type_valid(source->key_type) ||
        !cmeta_entry_type_valid(source->value_type) || source->key == NULL ||
        source->value == NULL)
        return false;
    if (cmeta_type_require_traits(source->key_type,
                                  CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) !=
            CMETA_OK ||
        cmeta_type_require_traits(source->value_type,
                                  CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) !=
            CMETA_OK)
        return false;

    temporary.key_type = source->key_type;
    temporary.value_type = source->value_type;

    if (!cmeta_entry_alloc_aligned(source->key_type, &temporary.key_storage,
                                   &key_object))
        goto fail;
    temporary.key = key_object;
    if (!source->key_type->traits->copy_construct(key_object, source->key))
        goto fail;
    key_constructed = true;

    if (!cmeta_entry_alloc_aligned(source->value_type,
                                   &temporary.value_storage, &value_object))
        goto fail;
    temporary.value = value_object;
    if (!source->value_type->traits->copy_construct(value_object,
                                                     source->value))
        goto fail;

    *destination = temporary;
    return true;

fail:
    if (temporary.value_storage != NULL)
        free(temporary.value_storage);
    if (key_constructed)
        source->key_type->traits->destroy(key_object);
    if (temporary.key_storage != NULL)
        free(temporary.key_storage);
    memset(destination, 0, sizeof(*destination));
    return false;
}

static void cmeta_entry_move_construct(void *destination_, void *source_) {
    cmeta_entry *destination = (cmeta_entry *)destination_;
    cmeta_entry *source = (cmeta_entry *)source_;

    if (destination == NULL || source == NULL || destination == source)
        return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void cmeta_entry_destroy(void *entry_) {
    cmeta_entry *entry = (cmeta_entry *)entry_;

    if (entry == NULL)
        return;
    if (entry->value_storage != NULL && entry->value != NULL &&
        cmeta_type_require_traits(entry->value_type, CMETA_TRAIT_DESTROY) ==
            CMETA_OK)
        entry->value_type->traits->destroy((void *)entry->value);
    if (entry->key_storage != NULL && entry->key != NULL &&
        cmeta_type_require_traits(entry->key_type, CMETA_TRAIT_DESTROY) ==
            CMETA_OK)
        entry->key_type->traits->destroy((void *)entry->key);
    free(entry->value_storage);
    free(entry->key_storage);
    memset(entry, 0, sizeof(*entry));
}

static bool cmeta_entry_key_compatible(const cmeta_entry *left,
                                       const cmeta_entry *right,
                                       cmeta_trait_flags required) {
    return left != NULL && right != NULL && left->key != NULL &&
           right->key != NULL && left->key_type != NULL &&
           right->key_type != NULL &&
           cmeta_type_equal(left->key_type, right->key_type) &&
           cmeta_type_require_traits(left->key_type, required) == CMETA_OK;
}

static bool cmeta_hash_entry_equal(const void *left_, const void *right_) {
    const cmeta_entry *left = (const cmeta_entry *)left_;
    const cmeta_entry *right = (const cmeta_entry *)right_;

    return cmeta_entry_key_compatible(left, right, CMETA_TRAIT_EQUAL) &&
           left->key_type->traits->equal(left->key, right->key);
}

static uint64_t cmeta_hash_entry_hash(const void *entry_) {
    const cmeta_entry *entry = (const cmeta_entry *)entry_;

    if (entry == NULL || entry->key == NULL || entry->key_type == NULL ||
        cmeta_type_require_traits(entry->key_type, CMETA_TRAIT_HASH) !=
            CMETA_OK)
        return UINT64_C(0);
    return entry->key_type->traits->hash(entry->key);
}

static int cmeta_ordered_entry_compare(const void *left_, const void *right_) {
    const cmeta_entry *left = (const cmeta_entry *)left_;
    const cmeta_entry *right = (const cmeta_entry *)right_;

    if (!cmeta_entry_key_compatible(left, right, CMETA_TRAIT_COMPARE))
        return 0;
    return left->key_type->traits->compare(left->key, right->key);
}

static const cmeta_type_traits cmeta_hash_entry_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
             CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH,
    .equal = cmeta_hash_entry_equal,
    .hash = cmeta_hash_entry_hash,
    .compare = NULL,
    .copy_construct = cmeta_entry_copy_construct,
    .move_construct = cmeta_entry_move_construct,
    .destroy = cmeta_entry_destroy
};

static const cmeta_type_traits cmeta_ordered_entry_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
             CMETA_TRAIT_COMPARE,
    .equal = NULL,
    .hash = NULL,
    .compare = cmeta_ordered_entry_compare,
    .copy_construct = cmeta_entry_copy_construct,
    .move_construct = cmeta_entry_move_construct,
    .destroy = cmeta_entry_destroy
};

const cmeta_type_desc cmeta_type_hash_entry = {
    .name = "cmeta_hash_entry",
    .size = sizeof(cmeta_entry),
    .align = _Alignof(cmeta_entry),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cmeta_hash_entry_traits,
    .identity = NULL
};

const cmeta_type_desc cmeta_type_ordered_entry = {
    .name = "cmeta_ordered_entry",
    .size = sizeof(cmeta_entry),
    .align = _Alignof(cmeta_entry),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cmeta_ordered_entry_traits,
    .identity = NULL
};
