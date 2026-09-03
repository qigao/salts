#include <cstl/typed.h>

#include <string.h>

#define STL_DEFINE_UNARY_CONSTRUCTION(                                         \
    prefix, handle_type, descriptor_expr, type_expr, live_expr)                \
const cmeta_type_desc stl_##prefix##_storage_type = {                           \
    #handle_type, sizeof(handle_type), CMETA_ALIGNOF(handle_type),              \
    CMETA_T_OBJECT, NULL, NULL, NULL};                                           \
static cmeta_status stl_##prefix##_bind_types(                                  \
    void *object, const cmeta_type_desc *const *arguments, size_t arity) {      \
    handle_type *self = (handle_type *)object;                                  \
    const cmeta_container_desc *descriptor;                                     \
    const cmeta_type_desc *bound;                                               \
    if (self == NULL || arguments == NULL || arity != 1u ||                    \
        arguments[0] == NULL || !cmeta_type_desc_valid(arguments[0]))           \
        return CMETA_INVALID_ARGUMENT;                                          \
    descriptor = (descriptor_expr);                                             \
    bound = (type_expr);                                                        \
    if (live_expr)                                                              \
        return CMETA_INVALID_ARGUMENT;                                          \
    if (descriptor == NULL && bound == NULL) {                                  \
        (descriptor_expr) = &stl_##prefix##_container_desc;                     \
        (type_expr) = arguments[0];                                             \
        return CMETA_OK;                                                        \
    }                                                                           \
    if (descriptor == NULL || bound == NULL)                                    \
        return CMETA_INVALID_ARGUMENT;                                          \
    if (descriptor != &stl_##prefix##_container_desc ||                         \
        !cmeta_type_equal(bound, arguments[0]))                                 \
        return CMETA_TYPE_MISMATCH;                                             \
    return CMETA_OK;                                                            \
}                                                                               \
static void stl_##prefix##_restore_zero(void *object) {                         \
    handle_type *self = (handle_type *)object;                                  \
    if (self == NULL)                                                           \
        return;                                                                 \
    prefix##_destroy(self);                                                     \
    memset(self, 0, sizeof(*self));                                             \
}                                                                               \
const cmeta_container_construct_ops stl_##prefix##_construct_ops = {            \
    sizeof(cmeta_container_construct_ops),                                      \
    CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION,                                  \
    &stl_##prefix##_container_desc,                                             \
    stl_##prefix##_bind_types,                                                  \
    stl_##prefix##_restore_zero};

#define STL_DEFINE_BINARY_CONSTRUCTION(                                        \
    prefix, handle_type, descriptor_expr, key_expr, value_expr, live_expr)     \
const cmeta_type_desc stl_##prefix##_storage_type = {                           \
    #handle_type, sizeof(handle_type), CMETA_ALIGNOF(handle_type),              \
    CMETA_T_OBJECT, NULL, NULL, NULL};                                           \
static cmeta_status stl_##prefix##_bind_types(                                  \
    void *object, const cmeta_type_desc *const *arguments, size_t arity) {      \
    handle_type *self = (handle_type *)object;                                  \
    const cmeta_container_desc *descriptor;                                     \
    const cmeta_type_desc *key_type;                                            \
    const cmeta_type_desc *value_type;                                          \
    if (self == NULL || arguments == NULL || arity != 2u ||                    \
        arguments[0] == NULL || arguments[1] == NULL ||                         \
        !cmeta_type_desc_valid(arguments[0]) ||                                 \
        !cmeta_type_desc_valid(arguments[1]))                                   \
        return CMETA_INVALID_ARGUMENT;                                          \
    descriptor = (descriptor_expr);                                             \
    key_type = (key_expr);                                                      \
    value_type = (value_expr);                                                  \
    if (live_expr)                                                              \
        return CMETA_INVALID_ARGUMENT;                                          \
    if (descriptor == NULL && key_type == NULL && value_type == NULL) {         \
        (descriptor_expr) = &stl_##prefix##_container_desc;                     \
        (key_expr) = arguments[0];                                              \
        (value_expr) = arguments[1];                                            \
        return CMETA_OK;                                                        \
    }                                                                           \
    if (descriptor == NULL || key_type == NULL || value_type == NULL)           \
        return CMETA_INVALID_ARGUMENT;                                          \
    if (descriptor != &stl_##prefix##_container_desc ||                         \
        !cmeta_type_equal(key_type, arguments[0]) ||                            \
        !cmeta_type_equal(value_type, arguments[1]))                            \
        return CMETA_TYPE_MISMATCH;                                             \
    return CMETA_OK;                                                            \
}                                                                               \
static void stl_##prefix##_restore_zero(void *object) {                         \
    handle_type *self = (handle_type *)object;                                  \
    if (self == NULL)                                                           \
        return;                                                                 \
    prefix##_destroy(self);                                                     \
    memset(self, 0, sizeof(*self));                                             \
}                                                                               \
const cmeta_container_construct_ops stl_##prefix##_construct_ops = {            \
    sizeof(cmeta_container_construct_ops),                                      \
    CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION,                                  \
    &stl_##prefix##_container_desc,                                             \
    stl_##prefix##_bind_types,                                                  \
    stl_##prefix##_restore_zero};

STL_DEFINE_UNARY_CONSTRUCTION(
    vec, vec_t, self->cmeta.descriptor, self->element_type,
    self->initialized)
STL_DEFINE_UNARY_CONSTRUCTION(
    deque, deque_t, self->cmeta.descriptor, self->element_type,
    self->initialized)
STL_DEFINE_UNARY_CONSTRUCTION(
    list, list_t, self->cmeta.descriptor, self->element_type,
    self->impl != NULL)
STL_DEFINE_UNARY_CONSTRUCTION(
    stack, cstl_stack_t, self->raw.cmeta.descriptor, self->raw.element_type,
    self->raw.initialized)
STL_DEFINE_UNARY_CONSTRUCTION(
    queue, queue_t, self->raw.cmeta.descriptor, self->raw.element_type,
    self->raw.initialized)
STL_DEFINE_UNARY_CONSTRUCTION(
    heap, heap_t, self->cmeta.descriptor, self->element_type,
    self->initialized)
STL_DEFINE_UNARY_CONSTRUCTION(
    set, set_t, self->cmeta.descriptor, self->element_type,
    self->map.impl != NULL)
STL_DEFINE_UNARY_CONSTRUCTION(
    hash_set, hash_set_t, self->cmeta.descriptor, self->element_type,
    self->table.initialized)

STL_DEFINE_BINARY_CONSTRUCTION(
    hash_map, hash_map_t, self->cmeta.descriptor,
    self->key_type, self->value_type, self->initialized)
STL_DEFINE_BINARY_CONSTRUCTION(
    map, map_t, self->cmeta.descriptor,
    self->key_type, self->value_type, self->impl != NULL)
STL_DEFINE_BINARY_CONSTRUCTION(
    multimap, multimap_t, self->cmeta.descriptor,
    self->key_type, self->value_type, self->impl != NULL)
STL_DEFINE_BINARY_CONSTRUCTION(
    btree, btree_t, self->cmeta.descriptor,
    self->key_type, self->value_type, self->initialized)
STL_DEFINE_BINARY_CONSTRUCTION(
    bplus_tree, bplus_tree_t, self->cmeta.descriptor,
    self->key_type, self->value_type, self->initialized)

#undef STL_DEFINE_UNARY_CONSTRUCTION
#undef STL_DEFINE_BINARY_CONSTRUCTION
