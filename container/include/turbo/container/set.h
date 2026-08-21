#ifndef TURBO_SET_H
#define TURBO_SET_H

#include <turbo/container/map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef turbo_map_compare_fn turbo_set_compare_fn;

typedef struct turbo_set {
  turbo_map_t map;
} turbo_set_t;

typedef struct turbo_set_iter {
  const turbo_set_t *owner;
  void *node;
} turbo_set_iter_t;

/* Set is a unique-key red-black tree. It is ordered by COMPARE and is not a
 * HashSet alias. */
CONTAINER_API container_status turbo_set_init(
    turbo_set_t *set, const cmeta_type_desc *key_type, size_t element_limit);
CONTAINER_API container_status turbo_set_init_bytes(
    turbo_set_t *set, size_t key_size, size_t key_align,
    size_t element_limit, turbo_set_compare_fn compare, void *context);
CONTAINER_API container_status turbo_set_from_array(
    turbo_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t element_limit);
CONTAINER_API container_status turbo_set_from_array_bytes(
    turbo_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t element_limit, turbo_set_compare_fn compare,
    void *context);
CONTAINER_API void turbo_set_destroy(turbo_set_t *set);
CONTAINER_API void turbo_set_clear(turbo_set_t *set);
CONTAINER_API container_status turbo_set_add(turbo_set_t *set,
                                              const void *key);
CONTAINER_API bool turbo_set_contains(const turbo_set_t *set,
                                      const void *key);
CONTAINER_API container_status turbo_set_remove(turbo_set_t *set,
                                                 const void *key);
CONTAINER_API size_t turbo_set_size(const turbo_set_t *set);
CONTAINER_API size_t turbo_set_element_limit(const turbo_set_t *set);
CONTAINER_API uint64_t turbo_set_generation(const turbo_set_t *set);
CONTAINER_API bool turbo_set_empty(const turbo_set_t *set);

CONTAINER_API turbo_set_iter_t turbo_set_begin(const turbo_set_t *set);
CONTAINER_API turbo_set_iter_t turbo_set_end(const turbo_set_t *set);
CONTAINER_API turbo_set_iter_t turbo_set_lower_bound(const turbo_set_t *set,
                                                      const void *key);
CONTAINER_API turbo_set_iter_t turbo_set_upper_bound(const turbo_set_t *set,
                                                      const void *key);
CONTAINER_API container_status turbo_set_iter_next(turbo_set_iter_t *iterator);
CONTAINER_API container_status turbo_set_iter_prev(turbo_set_iter_t *iterator);
CONTAINER_API bool turbo_set_iter_equal(turbo_set_iter_t left,
                                         turbo_set_iter_t right);
CONTAINER_API const void *turbo_set_iter_value_const(turbo_set_iter_t iterator);
CONTAINER_API bool turbo_set_range_next(const turbo_set_t *set,
                                        cmeta_range_cursor *cursor,
                                        const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SET_H */
