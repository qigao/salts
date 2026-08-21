#ifndef TURBO_LIST_H
#define TURBO_LIST_H

#include <cmeta/range.h>
#include <turbo/stl/export.h>
#include <turbo/stl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_list {
  void *impl;
  uint64_t generation;
} turbo_list_t;

/* Iterators are borrowed node identities. Insertion preserves every existing
 * iterator; erase invalidates only the erased iterator. clear/destroy
 * invalidate all iterators. */
typedef struct turbo_list_iter {
  const turbo_list_t *owner;
  void *node;
} turbo_list_iter_t;

/* List is a node-based bidirectional sequence. Each successful insertion
 * allocates one independent node and is bounded by element_limit. Handles
 * must start as `{0}`; a destroyed handle may be initialized again. */
TURBO_STL_API turbo_stl_status turbo_list_init(
    turbo_list_t *list, const cmeta_type_desc *element_type,
    size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_list_init_bytes(
    turbo_list_t *list, size_t elem_size, size_t elem_align,
    size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_list_from_array(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_list_from_array_bytes(
    turbo_list_t *list, const void *elements, size_t count,
    size_t elem_size, size_t elem_align, size_t element_limit);
TURBO_STL_API void turbo_list_destroy(turbo_list_t *list);
TURBO_STL_API void turbo_list_clear(turbo_list_t *list);

TURBO_STL_API turbo_stl_status turbo_list_push_front(
    turbo_list_t *list, const void *elem, turbo_list_iter_t *out_iterator);
TURBO_STL_API turbo_stl_status turbo_list_push_back(
    turbo_list_t *list, const void *elem, turbo_list_iter_t *out_iterator);
TURBO_STL_API turbo_stl_status turbo_list_insert_before(
    turbo_list_t *list, turbo_list_iter_t position, const void *elem,
    turbo_list_iter_t *out_iterator);
TURBO_STL_API turbo_stl_status turbo_list_insert_after(
    turbo_list_t *list, turbo_list_iter_t position, const void *elem,
    turbo_list_iter_t *out_iterator);
/* Non-NULL output is aligned uninitialized storage and receives ownership by
 * move. NULL output destroys the removed value. */
TURBO_STL_API turbo_stl_status turbo_list_erase(
    turbo_list_t *list, turbo_list_iter_t position, void *out_elem);
TURBO_STL_API turbo_stl_status turbo_list_pop_front(turbo_list_t *list,
                                                     void *out_elem);
TURBO_STL_API turbo_stl_status turbo_list_pop_back(turbo_list_t *list,
                                                    void *out_elem);

TURBO_STL_API turbo_list_iter_t turbo_list_begin(const turbo_list_t *list);
TURBO_STL_API turbo_list_iter_t turbo_list_end(const turbo_list_t *list);
TURBO_STL_API turbo_stl_status turbo_list_iter_next(turbo_list_iter_t *iterator);
/* prev(end) selects the tail. prev(begin) returns TURBO_STL_NOT_FOUND and
 * leaves the iterator unchanged. */
TURBO_STL_API turbo_stl_status turbo_list_iter_prev(turbo_list_iter_t *iterator);
TURBO_STL_API bool turbo_list_iter_equal(turbo_list_iter_t left,
                                          turbo_list_iter_t right);
TURBO_STL_API void *turbo_list_iter_value(turbo_list_iter_t iterator);
TURBO_STL_API const void *turbo_list_iter_value_const(
    turbo_list_iter_t iterator);

TURBO_STL_API void *turbo_list_front(turbo_list_t *list);
TURBO_STL_API const void *turbo_list_front_const(const turbo_list_t *list);
TURBO_STL_API void *turbo_list_back(turbo_list_t *list);
TURBO_STL_API const void *turbo_list_back_const(const turbo_list_t *list);
TURBO_STL_API size_t turbo_list_size(const turbo_list_t *list);
TURBO_STL_API uint64_t turbo_list_generation(const turbo_list_t *list);
TURBO_STL_API bool turbo_list_empty(const turbo_list_t *list);

/* The cursor is zero-initialized before first use. It stores opaque traversal
 * state and becomes invalid after mutation; CMeta Range detects that mutation
 * through generation before calling this function. */
TURBO_STL_API bool turbo_list_range_next(const turbo_list_t *list,
                                         cmeta_range_cursor *cursor,
                                         const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LIST_H */
