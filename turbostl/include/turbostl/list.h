#ifndef TURBOSTL_LIST_H
#define TURBOSTL_LIST_H

#include <cmeta/range.h>
#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

/* Internal typed bridge used while implementation symbols are migrated. */
stl_status list_init_with_type(list_t *list,
                               const cmeta_type_desc *element_type,
                               size_t element_limit);
stl_status list_from_array_with_type(list_t *list, const void *elements,
                                     size_t count,
                                     const cmeta_type_desc *element_type,
                                     size_t element_limit);
void list_destroy_storage(list_t *list);

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
  status = list_init_with_type(list, type, element_limit);
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
  status = list_from_array_with_type(list, elements, count, type,
                                     element_limit);
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
  list_destroy_storage(list);
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

/* Instance-driven CMeta adapter. */
static inline cmeta_status stl_list_cmeta_status(stl_status status) {
  switch (status) {
    case STL_OK: return CMETA_OK;
    case STL_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
    case STL_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
    case STL_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
    case STL_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
    case STL_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
    case STL_EMPTY:
    case STL_NOT_FOUND:
    default: return CMETA_CALLBACK_ERROR;
  }
}

static inline size_t stl_list_range_size(const void *object) {
  return list_size((const list_t *)object);
}

static inline uint64_t stl_list_range_version(const void *object) {
  return list_generation((const list_t *)object);
}

static inline cmeta_gen_status stl_list_range_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
  const list_t *list = (const list_t *)object;
  const void *value = NULL;
  if (list == NULL || cursor == NULL || out_value == NULL ||
      list->element_type == NULL)
    return CMETA_GEN_ERROR;
  if (!list_range_next(list, cursor, &value))
    return CMETA_GEN_DONE;
  memcpy(out_value, value, list->element_type->size);
  return cursor->state[0] == NULL ? CMETA_GEN_VALUE_AND_DONE :
                                    CMETA_GEN_VALUE;
}

static inline cmeta_range stl_list_range_factory(const void *object) {
  const list_t *list = (const list_t *)object;
  cmeta_range range = {
      object,
      list != NULL ? list->element_type : NULL,
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
      stl_list_range_size,
      stl_list_range_next,
      stl_list_range_version(object),
      stl_list_range_version};
  return range;
}

static inline cmeta_status stl_list_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
  list_t *output = (list_t *)context;
  if (output == NULL || output->element_type == NULL || input == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (!cmeta_type_equal(output->element_type, input))
    return CMETA_TYPE_MISMATCH;
  return stl_list_cmeta_status(list_init(output, limit));
}

static inline cmeta_status stl_list_collector_accept(void *context,
                                                      const void *value) {
  return stl_list_cmeta_status(
      list_push_back((list_t *)context, value, NULL));
}

static inline cmeta_status stl_list_collector_finish(void *context) {
  (void)context;
  return CMETA_OK;
}

static inline void stl_list_collector_abort(void *context) {
  list_destroy((list_t *)context);
}

static const cmeta_collector_ops stl_list_collector_ops = {
    stl_list_collector_begin,
    stl_list_collector_accept,
    stl_list_collector_finish,
    stl_list_collector_abort};

static inline cmeta_collector stl_list_collector_factory(void *zero_output,
                                                          size_t limit) {
  list_t *output = (list_t *)zero_output;
  cmeta_collector result = {
      &stl_list_collector_ops,
      output,
      output,
      output != NULL ? output->element_type : NULL,
      limit,
      0u,
      CMETA_COLLECTOR_ZERO,
      CMETA_OK};
  return result;
}

static const cmeta_container_desc stl_list_container_desc = {
    "List",
    NULL,
    NULL,
    NULL,
    NULL,
    stl_list_range_factory,
    NULL,
    NULL,
    NULL,
    stl_list_collector_factory};

/* Temporary repository-migration aliases. */
typedef list_t turbo_list_t;
typedef list_iter_t turbo_list_iter_t;
#define turbo_list_init list_init_with_type
#define turbo_list_init_bytes list_init_bytes
#define turbo_list_from_array list_from_array_with_type
#define turbo_list_from_array_bytes list_from_array_bytes
#define turbo_list_destroy list_destroy_storage
#define turbo_list_clear list_clear
#define turbo_list_push_front list_push_front
#define turbo_list_push_back list_push_back
#define turbo_list_insert_before list_insert_before
#define turbo_list_insert_after list_insert_after
#define turbo_list_erase list_erase
#define turbo_list_pop_front list_pop_front
#define turbo_list_pop_back list_pop_back
#define turbo_list_begin list_begin
#define turbo_list_end list_end
#define turbo_list_iter_next list_iter_next
#define turbo_list_iter_prev list_iter_prev
#define turbo_list_iter_equal list_iter_equal
#define turbo_list_iter_value list_iter_value
#define turbo_list_iter_value_const list_iter_value_const
#define turbo_list_front list_front
#define turbo_list_front_const list_front_const
#define turbo_list_back list_back
#define turbo_list_back_const list_back_const
#define turbo_list_size list_size
#define turbo_list_generation list_generation
#define turbo_list_empty list_empty
#define turbo_list_range_next list_range_next

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_LIST_H */
