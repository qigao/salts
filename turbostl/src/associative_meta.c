#include <turbostl/typed.h>

#include <string.h>

typedef enum stl_assoc_view {
    STL_ASSOC_KEYS = 0,
    STL_ASSOC_VALUES = 1,
    STL_ASSOC_ENTRIES = 2
} stl_assoc_view;

static cmeta_status stl_assoc_cmeta_status(stl_status status) {
    switch (status) {
        case STL_OK: return CMETA_OK;
        case STL_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
        case STL_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
        case STL_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
        case STL_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
        case STL_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
        case STL_EMPTY:
        case STL_NOT_FOUND:
        default: return CMETA_CALLBACK_ERROR;
    }
}

static bool stl_hash_entry_binding_valid(const cmeta_type_desc *key_type,
                                         const cmeta_type_desc *value_type) {
    return key_type != NULL && value_type != NULL &&
           cmeta_type_require_traits(
               key_type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY |
                             CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH) == CMETA_OK &&
           cmeta_type_require_traits(
               value_type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK;
}

static bool stl_ordered_entry_binding_valid(const cmeta_type_desc *key_type,
                                            const cmeta_type_desc *value_type) {
    return key_type != NULL && value_type != NULL &&
           cmeta_type_require_traits(
               key_type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY |
                             CMETA_TRAIT_COMPARE) == CMETA_OK &&
           cmeta_type_require_traits(
               value_type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK;
}

static cmeta_status stl_assoc_validate_input(
    const cmeta_type_desc *input, const cmeta_type_desc *expected) {
    cmeta_status status;
    if (input == NULL || expected == NULL || expected->traits == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_type_equal(input, expected))
        return CMETA_TYPE_MISMATCH;
    status = cmeta_type_require_traits(input, expected->traits->flags);
    return status == CMETA_OK ? CMETA_OK : CMETA_TRAIT_MISSING;
}

static cmeta_status stl_assoc_validate_entry_binding(
    const cmeta_entry *entry, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type) {
    if (entry == NULL || entry->key_type == NULL || entry->value_type == NULL ||
        entry->key == NULL || entry->value == NULL || key_type == NULL ||
        value_type == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_type_equal(entry->key_type, key_type) ||
        !cmeta_type_equal(entry->value_type, value_type))
        return CMETA_TYPE_MISMATCH;
    return CMETA_OK;
}

/* -------------------------------------------------------------------------
 * HashMap: sparse slot traversal
 * ------------------------------------------------------------------------- */

static size_t stl_hash_map_range_size(const void *object) {
    return hash_map_size((const hash_map_t *)object);
}

static uint64_t stl_hash_map_range_version(const void *object) {
    return hash_map_generation((const hash_map_t *)object);
}

static cmeta_gen_status stl_hash_map_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value,
    stl_assoc_view view) {
    const hash_map_t *map = (const hash_map_t *)object;
    size_t capacity;
    if (map == NULL || cursor == NULL || out_value == NULL ||
        map->key_type == NULL || map->value_type == NULL)
        return CMETA_GEN_ERROR;
    capacity = hash_map_capacity(map);
    while (cursor->index < capacity) {
        size_t slot = cursor->index;
        const void *key = hash_map_key_at(map, slot);
        const void *value = hash_map_value_at_const(map, slot);
        ++cursor->index;
        if (key == NULL || value == NULL)
            continue;
        if (view == STL_ASSOC_KEYS) {
            memcpy(out_value, key, map->key_type->size);
        } else if (view == STL_ASSOC_VALUES) {
            memcpy(out_value, value, map->value_type->size);
        } else {
            cmeta_entry *entry = (cmeta_entry *)out_value;
            *entry = (cmeta_entry){
                .key_type = map->key_type,
                .value_type = map->value_type,
                .key = key,
                .value = value,
                .key_storage = NULL,
                .value_storage = NULL};
        }
        return CMETA_GEN_VALUE;
    }
    return CMETA_GEN_DONE;
}

static cmeta_gen_status stl_hash_map_keys_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    return stl_hash_map_next(object, cursor, out_value, STL_ASSOC_KEYS);
}

static cmeta_gen_status stl_hash_map_values_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    return stl_hash_map_next(object, cursor, out_value, STL_ASSOC_VALUES);
}

static cmeta_gen_status stl_hash_map_entries_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    return stl_hash_map_next(object, cursor, out_value, STL_ASSOC_ENTRIES);
}

static cmeta_range stl_hash_map_keys_range_factory(const void *object) {
    const hash_map_t *map = (const hash_map_t *)object;
    cmeta_range range = {0};
    if (map == NULL || map->key_type == NULL)
        return range;
    range = (cmeta_range){
        object, map->key_type,
        CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
        stl_hash_map_range_size, stl_hash_map_keys_next,
        stl_hash_map_range_version(object), stl_hash_map_range_version};
    return range;
}

static cmeta_range stl_hash_map_values_range_factory(const void *object) {
    const hash_map_t *map = (const hash_map_t *)object;
    cmeta_range range = {0};
    if (map == NULL || map->value_type == NULL)
        return range;
    range = (cmeta_range){
        object, map->value_type,
        CMETA_RANGE_SIZED | CMETA_RANGE_REUSABLE,
        stl_hash_map_range_size, stl_hash_map_values_next,
        stl_hash_map_range_version(object), stl_hash_map_range_version};
    return range;
}

static cmeta_range stl_hash_map_entries_range_factory(const void *object) {
    const hash_map_t *map = (const hash_map_t *)object;
    cmeta_range range = {0};
    if (map == NULL ||
        !stl_hash_entry_binding_valid(map->key_type, map->value_type))
        return range;
    range = (cmeta_range){
        object, &cmeta_type_hash_entry,
        CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
        stl_hash_map_range_size, stl_hash_map_entries_next,
        stl_hash_map_range_version(object), stl_hash_map_range_version};
    return range;
}

static cmeta_status stl_hash_map_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
    hash_map_t *output = (hash_map_t *)context;
    cmeta_status status =
        stl_assoc_validate_input(input, &cmeta_type_hash_entry);
    if (status != CMETA_OK)
        return status;
    if (output == NULL ||
        !stl_hash_entry_binding_valid(output->key_type, output->value_type))
        return CMETA_TRAIT_MISSING;
    return stl_assoc_cmeta_status(hash_map_init(output, limit));
}

static cmeta_status stl_hash_map_collector_accept(
    void *context, const void *value) {
    hash_map_t *output = (hash_map_t *)context;
    const cmeta_entry *entry = (const cmeta_entry *)value;
    cmeta_status status;
    if (output == NULL)
        return CMETA_INVALID_ARGUMENT;
    status = stl_assoc_validate_entry_binding(
        entry, output->key_type, output->value_type);
    if (status != CMETA_OK)
        return status;
    return stl_assoc_cmeta_status(
        hash_map_put(output, entry->key, entry->value));
}

static cmeta_status stl_hash_map_collector_finish(void *context) {
    (void)context;
    return CMETA_OK;
}

static void stl_hash_map_collector_abort(void *context) {
    if (context != NULL)
        hash_map_destroy((hash_map_t *)context);
}

static const cmeta_collector_ops stl_hash_map_collector_ops = {
    stl_hash_map_collector_begin,
    stl_hash_map_collector_accept,
    stl_hash_map_collector_finish,
    stl_hash_map_collector_abort};

static cmeta_collector stl_hash_map_collector_factory(
    void *zero_output, size_t limit) {
    cmeta_collector collector = {
        &stl_hash_map_collector_ops,
        zero_output,
        zero_output,
        &cmeta_type_hash_entry,
        limit,
        0u,
        CMETA_COLLECTOR_ZERO,
        CMETA_OK};
    return collector;
}

const cmeta_container_desc stl_hash_map_container_desc = {
    "HashMap", NULL, NULL, NULL, NULL,
    stl_hash_map_entries_range_factory,
    stl_hash_map_keys_range_factory,
    stl_hash_map_values_range_factory,
    stl_hash_map_entries_range_factory,
    stl_hash_map_collector_factory};

/* -------------------------------------------------------------------------
 * Ordered linked associative kinds
 * ------------------------------------------------------------------------- */

#define STL_DEFINE_ORDERED_ASSOC_META(                                          \
    prefix, handle_type, display_name, init_fn, put_fn, destroy_fn, size_fn,    \
    generation_fn, range_next_fn, key_flags, value_flags, entry_flags,          \
    descriptor_symbol)                                                          \
static size_t stl_##prefix##_range_size(const void *object) {                   \
    return size_fn((const handle_type *)object);                                \
}                                                                                \
static uint64_t stl_##prefix##_range_version(const void *object) {              \
    return generation_fn((const handle_type *)object);                          \
}                                                                                \
static cmeta_gen_status stl_##prefix##_next(                                    \
    const void *object, cmeta_range_cursor *cursor, void *out_value,             \
    stl_assoc_view view) {                                                       \
    const handle_type *self = (const handle_type *)object;                      \
    const void *key = NULL;                                                      \
    const void *value = NULL;                                                    \
    if (self == NULL || cursor == NULL || out_value == NULL ||                  \
        self->key_type == NULL || self->value_type == NULL)                     \
        return CMETA_GEN_ERROR;                                                  \
    if (!range_next_fn(self, cursor, &key, &value))                             \
        return CMETA_GEN_DONE;                                                   \
    if (key == NULL || value == NULL)                                            \
        return CMETA_GEN_ERROR;                                                  \
    if (view == STL_ASSOC_KEYS) {                                                \
        memcpy(out_value, key, self->key_type->size);                           \
    } else if (view == STL_ASSOC_VALUES) {                                      \
        memcpy(out_value, value, self->value_type->size);                       \
    } else {                                                                     \
        cmeta_entry *entry = (cmeta_entry *)out_value;                          \
        *entry = (cmeta_entry){                                                  \
            .key_type = self->key_type,                                          \
            .value_type = self->value_type,                                      \
            .key = key,                                                          \
            .value = value,                                                      \
            .key_storage = NULL,                                                 \
            .value_storage = NULL};                                              \
    }                                                                            \
    return cursor->state[0] == NULL ? CMETA_GEN_VALUE_AND_DONE :                \
                                      CMETA_GEN_VALUE;                           \
}                                                                                \
static cmeta_gen_status stl_##prefix##_keys_next(                               \
    const void *object, cmeta_range_cursor *cursor, void *out_value) {           \
    return stl_##prefix##_next(object, cursor, out_value, STL_ASSOC_KEYS);       \
}                                                                                \
static cmeta_gen_status stl_##prefix##_values_next(                             \
    const void *object, cmeta_range_cursor *cursor, void *out_value) {           \
    return stl_##prefix##_next(object, cursor, out_value, STL_ASSOC_VALUES);     \
}                                                                                \
static cmeta_gen_status stl_##prefix##_entries_next(                            \
    const void *object, cmeta_range_cursor *cursor, void *out_value) {           \
    return stl_##prefix##_next(object, cursor, out_value, STL_ASSOC_ENTRIES);    \
}                                                                                \
static cmeta_range stl_##prefix##_keys_range_factory(const void *object) {      \
    const handle_type *self = (const handle_type *)object;                      \
    cmeta_range range = {0};                                                     \
    if (self == NULL || self->key_type == NULL)                                 \
        return range;                                                            \
    range = (cmeta_range){                                                       \
        object, self->key_type, (key_flags), stl_##prefix##_range_size,          \
        stl_##prefix##_keys_next, stl_##prefix##_range_version(object),          \
        stl_##prefix##_range_version};                                           \
    return range;                                                                \
}                                                                                \
static cmeta_range stl_##prefix##_values_range_factory(const void *object) {    \
    const handle_type *self = (const handle_type *)object;                      \
    cmeta_range range = {0};                                                     \
    if (self == NULL || self->value_type == NULL)                               \
        return range;                                                            \
    range = (cmeta_range){                                                       \
        object, self->value_type, (value_flags), stl_##prefix##_range_size,      \
        stl_##prefix##_values_next, stl_##prefix##_range_version(object),        \
        stl_##prefix##_range_version};                                           \
    return range;                                                                \
}                                                                                \
static cmeta_range stl_##prefix##_entries_range_factory(const void *object) {   \
    const handle_type *self = (const handle_type *)object;                      \
    cmeta_range range = {0};                                                     \
    if (self == NULL ||                                                          \
        !stl_ordered_entry_binding_valid(self->key_type, self->value_type))      \
        return range;                                                            \
    range = (cmeta_range){                                                       \
        object, &cmeta_type_ordered_entry, (entry_flags),                        \
        stl_##prefix##_range_size, stl_##prefix##_entries_next,                  \
        stl_##prefix##_range_version(object), stl_##prefix##_range_version};     \
    return range;                                                                \
}                                                                                \
static cmeta_status stl_##prefix##_collector_begin(                             \
    void *context, const cmeta_type_desc *input, size_t limit) {                \
    handle_type *output = (handle_type *)context;                               \
    cmeta_status status =                                                       \
        stl_assoc_validate_input(input, &cmeta_type_ordered_entry);              \
    if (status != CMETA_OK)                                                      \
        return status;                                                           \
    if (output == NULL ||                                                        \
        !stl_ordered_entry_binding_valid(output->key_type,                       \
                                         output->value_type))                    \
        return CMETA_TRAIT_MISSING;                                              \
    return stl_assoc_cmeta_status(init_fn(output, limit));                      \
}                                                                                \
static cmeta_status stl_##prefix##_collector_accept(                            \
    void *context, const void *value) {                                          \
    handle_type *output = (handle_type *)context;                               \
    const cmeta_entry *entry = (const cmeta_entry *)value;                      \
    cmeta_status status;                                                         \
    if (output == NULL)                                                          \
        return CMETA_INVALID_ARGUMENT;                                           \
    status = stl_assoc_validate_entry_binding(                                  \
        entry, output->key_type, output->value_type);                           \
    if (status != CMETA_OK)                                                      \
        return status;                                                           \
    return stl_assoc_cmeta_status(put_fn(output, entry->key, entry->value));    \
}                                                                                \
static cmeta_status stl_##prefix##_collector_finish(void *context) {            \
    (void)context;                                                               \
    return CMETA_OK;                                                             \
}                                                                                \
static void stl_##prefix##_collector_abort(void *context) {                     \
    if (context != NULL)                                                         \
        destroy_fn((handle_type *)context);                                     \
}                                                                                \
static const cmeta_collector_ops stl_##prefix##_collector_ops = {               \
    stl_##prefix##_collector_begin,                                              \
    stl_##prefix##_collector_accept,                                             \
    stl_##prefix##_collector_finish,                                             \
    stl_##prefix##_collector_abort};                                             \
static cmeta_collector stl_##prefix##_collector_factory(                        \
    void *zero_output, size_t limit) {                                           \
    cmeta_collector collector = {                                                \
        &stl_##prefix##_collector_ops, zero_output, zero_output,                 \
        &cmeta_type_ordered_entry, limit, 0u, CMETA_COLLECTOR_ZERO, CMETA_OK};  \
    return collector;                                                            \
}                                                                                \
const cmeta_container_desc descriptor_symbol = {                                \
    display_name, NULL, NULL, NULL, NULL,                                        \
    stl_##prefix##_entries_range_factory,                                        \
    stl_##prefix##_keys_range_factory,                                           \
    stl_##prefix##_values_range_factory,                                         \
    stl_##prefix##_entries_range_factory,                                        \
    stl_##prefix##_collector_factory};

STL_DEFINE_ORDERED_ASSOC_META(
    multimap, multimap_t, "MultiMap", multimap_init, multimap_put,
    multimap_destroy, multimap_size, multimap_generation, multimap_range_next,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
        CMETA_RANGE_REUSABLE,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
        CMETA_RANGE_REUSABLE,
    stl_multimap_container_desc)

STL_DEFINE_ORDERED_ASSOC_META(
    btree, btree_t, "BTree", btree_init, btree_put, btree_destroy, btree_size,
    btree_generation, btree_range_next,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
        CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
        CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
    stl_btree_container_desc)

STL_DEFINE_ORDERED_ASSOC_META(
    bplus_tree, bplus_tree_t, "BPlusTree", bplus_tree_init, bplus_tree_put,
    bplus_tree_destroy, bplus_tree_size, bplus_tree_generation,
    bplus_tree_range_next,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
        CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
        CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
    stl_bplus_tree_container_desc)

#undef STL_DEFINE_ORDERED_ASSOC_META
