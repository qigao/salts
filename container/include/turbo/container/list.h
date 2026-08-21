#ifndef TURBO_LIST_H
#define TURBO_LIST_H

#include <cmeta/cmeta.h>
#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_list_node turbo_list_node_t;

typedef struct turbo_list {
  turbo_list_node_t *head;
  turbo_list_node_t *tail;
  turbo_list_node_t *free_nodes;
  size_t size;
  size_t capacity;
  size_t elem_size;
  size_t elem_stride;
  size_t elem_align;
  size_t element_limit;
  const cmeta_type_desc *element_type;
  uint64_t generation;
  bool initialized;
} turbo_list_t;

/* List owns independently allocated, aligned node payloads. capacity() is the
 * total number of live plus reusable free nodes; clear() destroys live values
 * and retains all nodes in the free pool. Handles start as `{0}` and borrow
 * the type descriptor through destroy. */
CONTAINER_API container_status turbo_list_init(
    turbo_list_t *list, const cmeta_type_desc *element_type,
    size_t element_limit);
CONTAINER_API container_status turbo_list_init_bytes(
    turbo_list_t *list, size_t elem_size, size_t elem_align,
    size_t element_limit);
CONTAINER_API container_status turbo_list_from_array(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *element_type, size_t element_limit);
CONTAINER_API container_status turbo_list_from_array_bytes(
    turbo_list_t *list, const void *elements, size_t count,
    size_t elem_size, size_t elem_align, size_t element_limit);
CONTAINER_API void turbo_list_destroy(turbo_list_t *list);
CONTAINER_API void turbo_list_clear(turbo_list_t *list);
/* reserve() transactionally preallocates unconstructed nodes. It never moves
 * live nodes, so success preserves borrowed pointers and Range generation.
 * Failure leaves contents, capacity, pool, and generation unchanged. */
CONTAINER_API container_status turbo_list_reserve(turbo_list_t *list,
                                                   size_t min_capacity);
CONTAINER_API container_status turbo_list_push_front(turbo_list_t *list,
                                                      const void *elem);
CONTAINER_API container_status turbo_list_push_back(turbo_list_t *list,
                                                     const void *elem);
/* Non-NULL output is aligned uninitialized storage and receives ownership by
 * move. NULL output destroys the removed value. */
CONTAINER_API container_status turbo_list_pop_front(turbo_list_t *list,
                                                     void *out_elem);
CONTAINER_API container_status turbo_list_pop_back(turbo_list_t *list,
                                                    void *out_elem);
CONTAINER_API void *turbo_list_front(turbo_list_t *list);
CONTAINER_API const void *turbo_list_front_const(const turbo_list_t *list);
CONTAINER_API void *turbo_list_back(turbo_list_t *list);
CONTAINER_API const void *turbo_list_back_const(const turbo_list_t *list);
CONTAINER_API void *turbo_list_at(turbo_list_t *list, size_t index);
CONTAINER_API const void *turbo_list_at_const(const turbo_list_t *list,
                                              size_t index);
CONTAINER_API size_t turbo_list_size(const turbo_list_t *list);
CONTAINER_API size_t turbo_list_capacity(const turbo_list_t *list);
CONTAINER_API uint64_t turbo_list_generation(const turbo_list_t *list);
CONTAINER_API bool turbo_list_empty(const turbo_list_t *list);
/* cursor is zero before first use and SIZE_MAX after exhaustion. The returned
 * pointer is borrowed until the next successful mutation or destroy. */
CONTAINER_API bool turbo_list_range_next(const turbo_list_t *list,
                                         size_t *cursor,
                                         const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LIST_H */
