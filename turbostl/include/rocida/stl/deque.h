#ifndef TURBOSTL_DEQUE_H
#define TURBOSTL_DEQUE_H

#include <rocida/stl/status.h>

#include <cmeta/range.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct deque {
  cmeta_container_header cmeta;
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
} deque_t;

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
stl_status deque_raw_init(deque_t *deque,
                          const cmeta_type_desc *element_type,
                          size_t element_limit);
stl_status deque_raw_from_array(deque_t *deque, const void *elements,
                                size_t count,
                                const cmeta_type_desc *element_type,
                                size_t element_limit);
void deque_raw_destroy_storage(deque_t *deque);

stl_status deque_init_bytes(deque_t *deque, size_t elem_size, size_t elem_align,
                            size_t element_limit);
stl_status deque_from_array_bytes(deque_t *deque, const void *elements,
                                  size_t count, size_t elem_size,
                                  size_t elem_align, size_t element_limit);

static inline stl_status deque_init(deque_t *deque, size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (deque == NULL || deque->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = deque->cmeta.descriptor;
  type = deque->element_type;
  status = deque_raw_init(deque, type, element_limit);
  deque->cmeta.descriptor = kind;
  deque->element_type = type;
  return status;
}

static inline stl_status deque_from_array(deque_t *deque,
                                          const void *elements, size_t count,
                                          size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (deque == NULL || deque->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = deque->cmeta.descriptor;
  type = deque->element_type;
  status = deque_raw_from_array(deque, elements, count, type, element_limit);
  deque->cmeta.descriptor = kind;
  deque->element_type = type;
  return status;
}

static inline void deque_destroy(deque_t *deque) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  if (deque == NULL)
    return;
  kind = deque->cmeta.descriptor;
  type = deque->element_type;
  deque_raw_destroy_storage(deque);
  deque->cmeta.descriptor = kind;
  deque->element_type = type;
}

stl_status deque_clear(deque_t *deque);
stl_status deque_reserve(deque_t *deque, size_t min_capacity);
stl_status deque_push_back(deque_t *deque, const void *elem);
stl_status deque_push_front(deque_t *deque, const void *elem);
stl_status deque_pop_back(deque_t *deque, void *out_elem);
stl_status deque_pop_front(deque_t *deque, void *out_elem);
stl_status deque_set(deque_t *deque, size_t index, const void *elem);
void *deque_front(deque_t *deque);
const void *deque_front_const(const deque_t *deque);
void *deque_back(deque_t *deque);
const void *deque_back_const(const deque_t *deque);
void *deque_at(deque_t *deque, size_t index);
const void *deque_at_const(const deque_t *deque, size_t index);
size_t deque_size(const deque_t *deque);
size_t deque_capacity(const deque_t *deque);
uint64_t deque_generation(const deque_t *deque);
bool deque_empty(const deque_t *deque);


#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_DEQUE_H */
