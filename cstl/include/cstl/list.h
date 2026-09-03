#ifndef CSTL_LIST_H
#define CSTL_LIST_H

#include <cmeta/range.h>
#include <cstl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct list {
  cmeta_container_header cmeta;
  const cmeta_type_desc *element_type;
  void *impl;
  uint64_t generation;
} list_t;

typedef struct list_iter {
  const list_t *owner;
  void *node;
} list_iter_t;

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
stl_status list_raw_init(list_t *list, const cmeta_type_desc *element_type,
                         size_t element_limit);
stl_status list_raw_from_array(list_t *list, const void *elements,
                               size_t count,
                               const cmeta_type_desc *element_type,
                               size_t element_limit);
void list_raw_destroy_storage(list_t *list);

/* Raw byte entry points remain explicit. */
stl_status list_init_bytes(list_t *list, size_t elem_size, size_t elem_align,
                           size_t element_limit);
stl_status list_from_array_bytes(list_t *list, const void *elements,
                                 size_t count, size_t elem_size,
                                 size_t elem_align, size_t element_limit);

/* Self-describing typed entry points consume the declaration-time binding. */
static inline stl_status list_init(list_t *list, size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (list == NULL || list->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = list->cmeta.descriptor;
  type = list->element_type;
  status = list_raw_init(list, type, element_limit);
  list->cmeta.descriptor = kind;
  list->element_type = type;
  return status;
}

static inline stl_status list_from_array(list_t *list, const void *elements,
                                         size_t count,
                                         size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (list == NULL || list->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = list->cmeta.descriptor;
  type = list->element_type;
  status = list_raw_from_array(list, elements, count, type, element_limit);
  list->cmeta.descriptor = kind;
  list->element_type = type;
  return status;
}

static inline void list_destroy(list_t *list) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  if (list == NULL)
    return;
  kind = list->cmeta.descriptor;
  type = list->element_type;
  list_raw_destroy_storage(list);
  list->cmeta.descriptor = kind;
  list->element_type = type;
}

void list_clear(list_t *list);
stl_status list_push_front(list_t *list, const void *elem,
                           list_iter_t *out_iterator);
stl_status list_push_back(list_t *list, const void *elem,
                          list_iter_t *out_iterator);
stl_status list_insert_before(list_t *list, list_iter_t position,
                              const void *elem, list_iter_t *out_iterator);
stl_status list_insert_after(list_t *list, list_iter_t position,
                             const void *elem, list_iter_t *out_iterator);
stl_status list_erase(list_t *list, list_iter_t position, void *out_elem);
stl_status list_pop_front(list_t *list, void *out_elem);
stl_status list_pop_back(list_t *list, void *out_elem);
list_iter_t list_begin(const list_t *list);
list_iter_t list_end(const list_t *list);
stl_status list_iter_next(list_iter_t *iterator);
stl_status list_iter_prev(list_iter_t *iterator);
bool list_iter_equal(list_iter_t left, list_iter_t right);
void *list_iter_value(list_iter_t iterator);
const void *list_iter_value_const(list_iter_t iterator);
void *list_front(list_t *list);
const void *list_front_const(const list_t *list);
void *list_back(list_t *list);
const void *list_back_const(const list_t *list);
size_t list_size(const list_t *list);
uint64_t list_generation(const list_t *list);
bool list_empty(const list_t *list);
bool list_range_next(const list_t *list, cmeta_range_cursor *cursor,
                     const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* CSTL_LIST_H */
