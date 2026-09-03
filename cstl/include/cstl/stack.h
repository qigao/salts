#ifndef CSTL_STACK_H
#define CSTL_STACK_H

#include <cstl/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stack {
  vec_t raw;
} cstl_stack_t;

/* Darwin reserves stack_t for signal-stack state. New code should use the
 * prefixed handle; the legacy alias remains available where it is unambiguous. */
#if !defined(__APPLE__) && !defined(CSTL_NO_LEGACY_STACK_T)
typedef cstl_stack_t stack_t;
#endif

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
static inline stl_status stack_raw_init(cstl_stack_t *stack,
                                        const cmeta_type_desc *type,
                                        size_t element_limit) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_raw_init(&stack->raw, type, element_limit);
}

static inline stl_status stack_init(cstl_stack_t *stack, size_t element_limit) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_init(&stack->raw, element_limit);
}

static inline stl_status stack_init_bytes(cstl_stack_t *stack, size_t elem_size,
                                          size_t elem_align,
                                          size_t element_limit) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_init_bytes(&stack->raw, elem_size, elem_align,
                                        element_limit);
}

static inline stl_status stack_raw_from_array(
    cstl_stack_t *stack, const void *elements, size_t count,
    const cmeta_type_desc *type, size_t element_limit) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_raw_from_array(&stack->raw, elements, count, type,
                                            element_limit);
}

static inline stl_status stack_from_array(cstl_stack_t *stack,
                                          const void *elements, size_t count,
                                          size_t element_limit) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_from_array(&stack->raw, elements, count,
                                        element_limit);
}

static inline stl_status stack_from_array_bytes(
    cstl_stack_t *stack, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_from_array_bytes(&stack->raw, elements, count,
                                              elem_size, elem_align,
                                              element_limit);
}
static inline void stack_destroy(cstl_stack_t *stack) {
  if (stack != NULL) vec_destroy(&stack->raw);
}
static inline void stack_clear(cstl_stack_t *stack) {
  if (stack != NULL) (void)vec_clear(&stack->raw);
}
static inline stl_status stack_reserve(cstl_stack_t *stack, size_t capacity) {
  return stack == NULL ? STL_INVALID_ARGUMENT
                       : vec_reserve(&stack->raw, capacity);
}
static inline stl_status stack_push(cstl_stack_t *stack, const void *elem) {
  return stack == NULL ? STL_INVALID_ARGUMENT : vec_push(&stack->raw, elem);
}
static inline stl_status stack_pop(cstl_stack_t *stack, void *out_elem) {
  return stack == NULL ? STL_INVALID_ARGUMENT : vec_pop(&stack->raw, out_elem);
}
static inline void *stack_top(cstl_stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U)
             ? NULL
             : vec_at(&stack->raw, stack->raw.size - 1U);
}
static inline const void *stack_top_const(const cstl_stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U)
             ? NULL
             : vec_at_const(&stack->raw, stack->raw.size - 1U);
}
static inline const void *stack_at_const(const cstl_stack_t *stack, size_t index) {
  return stack == NULL ? NULL : vec_at_const(&stack->raw, index);
}
static inline size_t stack_size(const cstl_stack_t *stack) {
  return stack == NULL ? 0U : vec_size(&stack->raw);
}
static inline size_t stack_capacity(const cstl_stack_t *stack) {
  return stack == NULL ? 0U : vec_capacity(&stack->raw);
}
static inline uint64_t stack_generation(const cstl_stack_t *stack) {
  return stack == NULL ? UINT64_C(0) : vec_generation(&stack->raw);
}
static inline bool stack_empty(const cstl_stack_t *stack) {
  return stack == NULL || vec_empty(&stack->raw);
}


#ifdef __cplusplus
}
#endif
#endif /* CSTL_STACK_H */
