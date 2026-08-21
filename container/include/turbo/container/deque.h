#ifndef TURBO_DEQUE_H
#define TURBO_DEQUE_H

#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  size_t elem_stride;
  size_t elem_align;
  size_t element_limit;
  const cmeta_type_desc *element_type;
  uint64_t generation;
  bool initialized;
  size_t head;
} turbo_deque_t;

/* Borrowed pointers from front/back/at become invalid after any successful
 * mutation, storage-changing reserve, clear, or destroy. */
CONTAINER_API container_status turbo_deque_init(turbo_deque_t *deque,
                                                const cmeta_type_desc *element_type,
                                                size_t element_limit);
CONTAINER_API container_status turbo_deque_init_bytes(turbo_deque_t *deque, size_t elem_size,
                                                      size_t elem_align, size_t element_limit);
CONTAINER_API container_status turbo_deque_from_array(turbo_deque_t *deque,
                                                      const void *elements, size_t count,
                                                      const cmeta_type_desc *element_type,
                                                      size_t element_limit);
CONTAINER_API container_status turbo_deque_from_array_bytes(turbo_deque_t *deque,
                                                            const void *elements, size_t count,
                                                            size_t elem_size, size_t elem_align,
                                                            size_t element_limit);
CONTAINER_API void turbo_deque_destroy(turbo_deque_t *deque);
CONTAINER_API container_status turbo_deque_clear(turbo_deque_t *deque);
CONTAINER_API container_status turbo_deque_reserve(turbo_deque_t *deque, size_t min_capacity);
CONTAINER_API container_status turbo_deque_push_back(turbo_deque_t *deque, const void *elem);
CONTAINER_API container_status turbo_deque_push_front(turbo_deque_t *deque, const void *elem);
CONTAINER_API container_status turbo_deque_pop_back(turbo_deque_t *deque, void *out_elem);
CONTAINER_API container_status turbo_deque_pop_front(turbo_deque_t *deque, void *out_elem);
CONTAINER_API container_status turbo_deque_set(turbo_deque_t *deque, size_t index,
                                               const void *elem);
CONTAINER_API void *turbo_deque_front(turbo_deque_t *deque);
CONTAINER_API const void *turbo_deque_front_const(const turbo_deque_t *deque);
CONTAINER_API void *turbo_deque_back(turbo_deque_t *deque);
CONTAINER_API const void *turbo_deque_back_const(const turbo_deque_t *deque);
CONTAINER_API void *turbo_deque_at(turbo_deque_t *deque, size_t index);
CONTAINER_API const void *turbo_deque_at_const(const turbo_deque_t *deque, size_t index);
CONTAINER_API size_t turbo_deque_size(const turbo_deque_t *deque);
CONTAINER_API size_t turbo_deque_capacity(const turbo_deque_t *deque);
CONTAINER_API uint64_t turbo_deque_generation(const turbo_deque_t *deque);
CONTAINER_API bool turbo_deque_empty(const turbo_deque_t *deque);

#define TURBO_DEQUE_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, turbo_deque_t, turbo_deque, CONTAINER_OK, _, TURBO_META_DEQUE_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, turbo_deque, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_DEQUE_H */
