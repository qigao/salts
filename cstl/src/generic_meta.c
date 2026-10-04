#include <cstl/typed.h>
#include <cstl/detail/generic_ids.h>

#define STL_CONTAINER_EXT_PREFIX_SIZE \
    (offsetof(cmeta_container_ext, construction) + \
     sizeof(((cmeta_container_ext *)0)->construction))

#define STL_DEFINE_UNARY_GENERIC_META(                                         \
    prefix, display_name, stable_id, handle_type, element_expr, semantic_)    \
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
    STL_CONTAINER_EXT_PREFIX_SIZE,                                             \
    CMETA_CONTAINER_EXT_ABI_VERSION,                                           \
    &stl_##prefix##_generic_type_ops,                                          \
    (semantic_),                                                               \
    &stl_##prefix##_construct_ops};

#define STL_DEFINE_BINARY_GENERIC_META(                                        \
    prefix, display_name, stable_id, handle_type, key_expr, value_expr,       \
    semantic_)                                                                 \
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
    STL_CONTAINER_EXT_PREFIX_SIZE,                                             \
    CMETA_CONTAINER_EXT_ABI_VERSION,                                           \
    &stl_##prefix##_generic_type_ops,                                          \
    (semantic_),                                                               \
    &stl_##prefix##_construct_ops};

STL_DEFINE_UNARY_GENERIC_META(
    vec, "Vec", CSTL_GENERIC_STABLE_ID(Vec), vec_t, self->element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    deque, "Deque", CSTL_GENERIC_STABLE_ID(Deque), deque_t, self->element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    list, "List", CSTL_GENERIC_STABLE_ID(List), list_t, self->element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    stack, "Stack", CSTL_GENERIC_STABLE_ID(Stack), cstl_stack_t, self->raw.element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    queue, "Queue", CSTL_GENERIC_STABLE_ID(Queue), queue_t, self->raw.element_type,
    &cmeta_data_sequence)
STL_DEFINE_UNARY_GENERIC_META(
    heap, "Heap", CSTL_GENERIC_STABLE_ID(Heap), heap_t, self->element_type,
    NULL)
STL_DEFINE_UNARY_GENERIC_META(
    set, "Set", CSTL_GENERIC_STABLE_ID(Set), set_t, self->element_type,
    &cmeta_data_set)
STL_DEFINE_UNARY_GENERIC_META(
    hash_set, "HashSet", CSTL_GENERIC_STABLE_ID(HashSet), hash_set_t, self->element_type,
    &cmeta_data_set)

STL_DEFINE_BINARY_GENERIC_META(
    hash_map, "HashMap", CSTL_GENERIC_STABLE_ID(HashMap), hash_map_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    map, "Map", CSTL_GENERIC_STABLE_ID(Map), map_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    multimap, "MultiMap", CSTL_GENERIC_STABLE_ID(MultiMap), multimap_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    btree, "BTree", CSTL_GENERIC_STABLE_ID(BTree), btree_t,
    self->key_type, self->value_type, &cmeta_data_map)
STL_DEFINE_BINARY_GENERIC_META(
    bplus_tree, "BPlusTree", CSTL_GENERIC_STABLE_ID(BPlusTree), bplus_tree_t,
    self->key_type, self->value_type, &cmeta_data_map)

#undef STL_DEFINE_UNARY_GENERIC_META
#undef STL_DEFINE_BINARY_GENERIC_META
#undef STL_CONTAINER_EXT_PREFIX_SIZE
