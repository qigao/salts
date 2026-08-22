#ifndef TURBO_STACK_H
#define TURBO_STACK_H

#include <turbostl/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  vec_t raw;
} stack_t;

static inline turbostl_status stack_init(stack_t *stack,
                                                 const cmeta_type_desc *type,
                                                 size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : vec_init(&stack->raw, type, element_limit);
}
static inline turbostl_status stack_init_bytes(stack_t *stack, size_t elem_size,
                                                       size_t elem_align,
                                                       size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : vec_init_bytes(&stack->raw, elem_size, elem_align,
                                              element_limit);
}
static inline turbostl_status stack_from_array(
    stack_t *stack, const void *elements, size_t count,
    const cmeta_type_desc *type, size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : vec_from_array(&stack->raw, elements, count, type,
                                              element_limit);
}
static inline turbostl_status stack_from_array_bytes(
    stack_t *stack, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : vec_from_array_bytes(&stack->raw, elements, count, elem_size, elem_align,
                                                    element_limit);
}
static inline void stack_destroy(stack_t *stack) {
  if (stack != NULL) vec_destroy(&stack->raw);
}
static inline void stack_clear(stack_t *stack) {
  if (stack != NULL) vec_clear(&stack->raw);
}
static inline int stack_reserve(stack_t *stack, size_t capacity) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT : vec_reserve(&stack->raw, capacity);
}
static inline int stack_push(stack_t *stack, const void *elem) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT : vec_push(&stack->raw, elem);
}
static inline int stack_pop(stack_t *stack, void *out_elem) {
  return stack == NULL ? TURBO_STL_INVALID_ARGUMENT : vec_pop(&stack->raw, out_elem);
}
static inline void *stack_top(stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U) ? NULL : vec_at(&stack->raw, stack->raw.size - 1U);
}
static inline const void *stack_top_const(const stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U) ? NULL : vec_at_const(&stack->raw, stack->raw.size - 1U);
}
static inline const void *stack_at_const(const stack_t *stack, size_t index) {
  return stack == NULL ? NULL : vec_at_const(&stack->raw, index);
}
static inline size_t stack_size(const stack_t *stack) {
  return stack == NULL ? 0U : vec_size(&stack->raw);
}
static inline size_t stack_capacity(const stack_t *stack) {
  return stack == NULL ? 0U : vec_capacity(&stack->raw);
}
static inline uint64_t stack_generation(const stack_t *stack) {
  return stack == NULL ? UINT64_C(0) : vec_generation(&stack->raw);
}
static inline bool stack_empty(const stack_t *stack) {
  return stack == NULL || vec_empty(&stack->raw);
}

#ifdef __cplusplus
}
#endif
#endif /* TURBO_STACK_H */
