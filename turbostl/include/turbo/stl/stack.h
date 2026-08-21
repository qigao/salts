#ifndef TURBO_STACK_H
#define TURBO_STACK_H

#include <turbo/stl/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_vec_t raw;
} turbo_stack_t;

static inline turbo_stl_status turbo_stack_init(turbo_stack_t *stack,
                                                 const cmeta_type_desc *type,
                                                 size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_vec_init(&stack->raw, type, element_limit);
}
static inline turbo_stl_status turbo_stack_init_bytes(turbo_stack_t *stack, size_t elem_size,
                                                       size_t elem_align,
                                                       size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_vec_init_bytes(&stack->raw, elem_size, elem_align,
                                              element_limit);
}
static inline turbo_stl_status turbo_stack_from_array(
    turbo_stack_t *stack, const void *elements, size_t count,
    const cmeta_type_desc *type, size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_vec_from_array(&stack->raw, elements, count, type,
                                              element_limit);
}
static inline turbo_stl_status turbo_stack_from_array_bytes(
    turbo_stack_t *stack, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_vec_from_array_bytes(&stack->raw, elements, count, elem_size, elem_align,
                                                    element_limit);
}
static inline void turbo_stack_destroy(turbo_stack_t *stack) {
  if (stack != NULL) turbo_vec_destroy(&stack->raw);
}
static inline void turbo_stack_clear(turbo_stack_t *stack) {
  if (stack != NULL) turbo_vec_clear(&stack->raw);
}
static inline int turbo_stack_reserve(turbo_stack_t *stack, size_t capacity) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT : turbo_vec_reserve(&stack->raw, capacity);
}
static inline int turbo_stack_push(turbo_stack_t *stack, const void *elem) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT : turbo_vec_push(&stack->raw, elem);
}
static inline int turbo_stack_pop(turbo_stack_t *stack, void *out_elem) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT : turbo_vec_pop(&stack->raw, out_elem);
}
static inline void *turbo_stack_top(turbo_stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U) ? NULL : turbo_vec_at(&stack->raw, stack->raw.size - 1U);
}
static inline const void *turbo_stack_top_const(const turbo_stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U) ? NULL : turbo_vec_at_const(&stack->raw, stack->raw.size - 1U);
}
static inline const void *turbo_stack_at_const(const turbo_stack_t *stack, size_t index) {
  return stack == NULL ? NULL : turbo_vec_at_const(&stack->raw, index);
}
static inline size_t turbo_stack_size(const turbo_stack_t *stack) {
  return stack == NULL ? 0U : turbo_vec_size(&stack->raw);
}
static inline size_t turbo_stack_capacity(const turbo_stack_t *stack) {
  return stack == NULL ? 0U : turbo_vec_capacity(&stack->raw);
}
static inline uint64_t turbo_stack_generation(const turbo_stack_t *stack) {
  return stack == NULL ? UINT64_C(0) : turbo_vec_generation(&stack->raw);
}
static inline bool turbo_stack_empty(const turbo_stack_t *stack) {
  return stack == NULL || turbo_vec_empty(&stack->raw);
}

#ifdef __cplusplus
}
#endif
#endif /* TURBO_STACK_H */
