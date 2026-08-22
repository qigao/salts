#ifndef TURBOSTL_LIST_H
#define TURBOSTL_LIST_H

#include <cmeta/range.h>
#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct list {
  void *impl;
  uint64_t generation;
} list_t;

typedef struct list_iter {
  const list_t *owner;
  void *node;
} list_iter_t;

/* List is a node-based bidirectional sequence. Each successful insertion
 * allocates one independent node and is bounded by element_limit. */
stl_status list_init(list_t *list, const cmeta_type_desc *element_type,
                     size_t element_limit);
stl_status list_init_bytes(list_t *list, size_t elem_size, size_t elem_align,
                           size_t element_limit);
stl_status list_from_array(list_t *list, const void *elements, size_t count,
                           const cmeta_type_desc *element_type,
                           size_t element_limit);
stl_status list_from_array_bytes(list_t *list, const void *elements,
                                 size_t count, size_t elem_size,
                                 size_t elem_align, size_t element_limit);
void list_destroy(list_t *list);
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

/* Temporary repository-migration aliases. */
typedef list_t turbo_list_t;
typedef list_iter_t turbo_list_iter_t;
#define turbo_list_init list_init
#define turbo_list_init_bytes list_init_bytes
#define turbo_list_from_array list_from_array
#define turbo_list_from_array_bytes list_from_array_bytes
#define turbo_list_destroy list_destroy
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
