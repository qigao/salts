#ifndef CONTAINER_STACK_H
#define CONTAINER_STACK_H

#include <container/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  container_vec_t raw;
} container_stack_t;

static inline int container_stack_init(container_stack_t *stack, size_t elem_size) {
  return stack == NULL ? TURBO_EINVAL : container_vec_init(&stack->raw, elem_size);
}
static inline int container_stack_from_array(container_stack_t *stack, const void *elements,
                                         size_t count, size_t elem_size) {
  return stack == NULL ? TURBO_EINVAL : container_vec_from_array(&stack->raw, elements, count, elem_size);
}
static inline void container_stack_destroy(container_stack_t *stack) {
  if (stack != NULL) container_vec_destroy(&stack->raw);
}
static inline void container_stack_clear(container_stack_t *stack) {
  if (stack != NULL) container_vec_clear(&stack->raw);
}
static inline int container_stack_reserve(container_stack_t *stack, size_t capacity) {
  return stack == NULL ? TURBO_EINVAL : container_vec_reserve(&stack->raw, capacity);
}
static inline int container_stack_push(container_stack_t *stack, const void *elem) {
  return stack == NULL ? TURBO_EINVAL : container_vec_push(&stack->raw, elem);
}
static inline int container_stack_pop(container_stack_t *stack, void *out_elem) {
  return stack == NULL ? TURBO_EINVAL : container_vec_pop(&stack->raw, out_elem);
}
static inline void *container_stack_top(container_stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U) ? NULL : container_vec_at(&stack->raw, stack->raw.size - 1U);
}
static inline const void *container_stack_top_const(const container_stack_t *stack) {
  return (stack == NULL || stack->raw.size == 0U) ? NULL : container_vec_at_const(&stack->raw, stack->raw.size - 1U);
}
static inline const void *container_stack_at_const(const container_stack_t *stack, size_t index) {
  return stack == NULL ? NULL : container_vec_at_const(&stack->raw, index);
}
static inline size_t container_stack_size(const container_stack_t *stack) {
  return stack == NULL ? 0U : container_vec_size(&stack->raw);
}
static inline size_t container_stack_capacity(const container_stack_t *stack) {
  return stack == NULL ? 0U : container_vec_capacity(&stack->raw);
}
static inline bool container_stack_empty(const container_stack_t *stack) {
  return stack == NULL || container_vec_empty(&stack->raw);
}


#ifdef __cplusplus
}
#endif
#endif /* CONTAINER_STACK_H */
