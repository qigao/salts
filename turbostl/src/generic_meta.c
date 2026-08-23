#include <cmeta/data.h>
#include <turbostl/typed.h>

#define STL_DEFINE_UNARY_GENERIC_META(                                         \
    prefix, display_name, stable_id, handle_type, element_expr, data_desc)    \
const cmeta_generic_desc stl_##prefix##_generic_desc =                         \
    CMETA_GENERIC_DESC_INIT(                                                   \
        (stable_id), (display_name), 1u, 1u, CMETA_GENERIC_CONTAINER);         \
static const cmeta_type_desc *stl_##prefix##_generic_argument(                 \
    const void *object, size_t index) {                                        \
    const handle_type *self = (const handle_type *)object;                    \
    return self != NULL && index == 0u ? (element_expr) : NULL;               \
}                                                                              \
static const cmeta_container_type_ops stl_##prefix##_generic_type_ops = {      \
    sizeof(cmeta_container_type_ops),                                          \
    CMETA_CONTAINER_TYPE_OPS_ABI_VERSION,                                      \
    &stl_##prefix##_generic_desc,                                              \
    1u,                                                                        \
    stl_##prefix##_generic_argument};                                          \
const cmeta_container_ext stl_##prefix##_container_ext = {                     \
    sizeof(cmeta_container_ext),                                               \
    CMETA_CONTAINER_EXT_ABI_VERSION,                                           \
    &stl_##prefix##_generic_type_ops,                                          \
    (data_desc)};

#define STL_DEFINE_BINARY_GENERIC_META(                                        \
    prefix, display_name, stable_id, handle_type, key_expr, value_expr,       \
    data_desc)                                                                 \
const cmeta_generic_desc stl_##prefix##_generic_desc =                         \
    CMETA_GENERIC_DESC_INIT(                                                   \
        (stable_id), (display_name), 2u, 2u, CMETA_GENERIC_CONTAINER);         \
static const cmeta_type_desc *stl_##prefix##_generic_argument(                 \
    const void *object, size_t index) {                                        \
    const handle_type *self = (const handle_type *)object;                    \
    if (self == NULL) return NULL;                                             \
    if (index == 0u) return (key_expr);                                        \
    return index == 1u ? (value_expr) : NULL;                                  \
}                                                                              \
static const cmeta_container_type_ops stl_##prefix##_generic_type_ops = {      \
    sizeof(cmeta_container_type_ops),                                          \
    CMETA_CONTAINER_TYPE_OPS_ABI_VERSION,                                      \
    &stl_##prefix##_generic_desc,                                              \
    2u,                                                                        \
    stl_##prefix##_generic_argument};                                          \
const cmeta_container_ext stl_##prefix##_container_ext = {                     \
    sizeof(cmeta_container_ext),                                               \
    CMETA_CONTAINER_EXT_ABI_VERSION,                                           \
    &stl_##prefix##_generic_type_ops,                                          \
    (data_desc)};

STL_DEFINE_UNARY_GENERIC_META(
    vec, "Vec", "turbostl.Vec", vec_t, self->element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    deque, "Deque", "turbostl.Deque", deque_t, self->element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    list, "List", "turbostl.List", list_t, self->element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    stack, "Stack", "turbostl.Stack", stack_t, self->raw.element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    queue, "Queue", "turbostl.Queue", queue_t, self->raw.element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    heap, "Heap", "turbostl.Heap", heap_t, self->element_type, NULL)
STL_DEFINE_UNARY_GENERIC_META(
    set, "Set", "turbostl.Set", set_t, self->element_type,
    &cmeta_data_set)
STL_DEFINE_UNARY_GENERIC_META(
    hash_set, "HashSet", "turbostl.HashSet", hash_set_t, self->element_type,
    &cmeta_data_set)

STL_DEFINE_BINARY_GENERIC_META(
    hash_map, "HashMap", "turbostl.HashMap", hash_map_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    map, "Map", "turbostl.Map", map_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    multimap, "MultiMap", "turbostl.MultiMap", multimap_t,
    self->key_type, self->value_type, NULL)
STL_DEFINE_BINARY_GENERIC_META(
    btree, "BTree", "turbostl.BTree", btree_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    bplus_tree, "BPlusTree", "turbostl.BPlusTree", bplus_tree_t,
    self->key_type, self->value_type, &cmeta_data_map)

#undef STL_DEFINE_UNARY_GENERIC_META
#undef STL_DEFINE_BINARY_GENERIC_META
