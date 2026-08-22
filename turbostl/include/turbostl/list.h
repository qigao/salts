#ifndef TURBO_LIST_H
#define TURBO_LIST_H

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

/* Iterators are borrowed node identities. Insertion preserves every existing
 * iterator; erase invalidates only the erased iterator. clear/destroy
 * invalidate all iterators. */
typedef struct list_iter {
  const list_t *owner;
  void *node;
} list_iter_t;

/* List is a node-based bidirectional sequence. Each successful insertion
 * allocates one independent node and is bounded by element_limit. Handles
 * must start as `{0}`; a destroyed handle may be initialized again. */
turbostl_status list_init(
    list_t *list, const cmeta_type_desc *element_type,
    size_t element_limit);
turbostl_status list_init_bytes(
    list_t *list, size_t elem_size, size_t elem_align,
    size_t element_limit);
turbostl_status list_from_array(
    list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t element_limit);
turbostl_status list_from_array_bytes(
    list_t *list, const void *elements, size_t count,
    size_t elem_size, size_t elem_align, size_t element_limit);
void list_destroy(list_t *list);
void list_clear(list_t *list);

turbostl_status list_push_front(
    list_t *list, const void *elem, list_iter_t *out_iterator);
turbostl_status list_push_back(
    list_t *list, const void *elem, list_iter_t *out_iterator);
turbostl_status list_insert_before(
    list_t *list, list_iter_t position, const void *elem,
    list_iter_t *out_iterator);
turbostl_status list_insert_after(
    list_t *list, list_iter_t position, const void *elem,
    list_iter_t *out_iterator);
/* Non-NULL output is aligned uninitialized storage and receives ownership by
 * move. NULL output destroys the removed value. */
turbostl_status list_erase(
    list_t *list, list_iter_t position, void *out_elem);
turbostl_status list_pop_front(list_t *list,
                                                     void *out_elem);
turbostl_status list_pop_back(list_t *list,
                                                    void *out_elem);

list_iter_t list_begin(const list_t *list);
list_iter_t list_end(const list_t *list);
turbostl_status list_iter_next(list_iter_t *iterator);
/* prev(end) selects the tail. prev(begin) returns TURBO_STL_NOT_FOUND and
 * leaves the iterator unchanged. */
turbostl_status list_iter_prev(list_iter_t *iterator);
bool list_iter_equal(list_iter_t left,
                                          list_iter_t right);
void *list_iter_value(list_iter_t iterator);
const void *list_iter_value_const(
    list_iter_t iterator);

void *list_front(list_t *list);
const void *list_front_const(const list_t *list);
void *list_back(list_t *list);
const void *list_back_const(const list_t *list);
size_t list_size(const list_t *list);
uint64_t list_generation(const list_t *list);
bool list_empty(const list_t *list);

/* The cursor is zero-initialized before first use. It stores opaque traversal
 * state and becomes invalid after mutation; CMeta Range detects that mutation
 * through generation before calling this function. */
bool list_range_next(const list_t *list,
                                         cmeta_range_cursor *cursor,
                                         const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LIST_H */
