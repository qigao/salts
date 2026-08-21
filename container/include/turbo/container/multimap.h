#ifndef TURBO_MULTIMAP_H
#define TURBO_MULTIMAP_H

#include <turbo/container/map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef turbo_map_compare_fn turbo_multimap_compare_fn;

typedef struct turbo_multimap {
  void *impl;
  uint64_t generation;
} turbo_multimap_t;

typedef struct turbo_multimap_iter {
  const turbo_multimap_t *owner;
  void *node;
} turbo_multimap_iter_t;

/* MultiMap is a red-black tree that stores each key/value pair in its own
 * node. Equivalent keys retain insertion order in traversal and all pairs
 * share one explicit element_limit. */
CONTAINER_API container_status turbo_multimap_init(
    turbo_multimap_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t element_limit);
CONTAINER_API container_status turbo_multimap_init_bytes(
    turbo_multimap_t *map, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    turbo_multimap_compare_fn compare, void *context);
CONTAINER_API container_status turbo_multimap_from_arrays(
    turbo_multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t element_limit);
CONTAINER_API container_status turbo_multimap_from_arrays_bytes(
    turbo_multimap_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, turbo_multimap_compare_fn compare, void *context);
CONTAINER_API void turbo_multimap_destroy(turbo_multimap_t *map);
CONTAINER_API void turbo_multimap_clear(turbo_multimap_t *map);
CONTAINER_API container_status turbo_multimap_put(turbo_multimap_t *map,
                                                   const void *key,
                                                   const void *value);
CONTAINER_API bool turbo_multimap_contains(const turbo_multimap_t *map,
                                           const void *key);
CONTAINER_API size_t turbo_multimap_count(const turbo_multimap_t *map,
                                          const void *key);
/* remove erases the most recently inserted equivalent pair. */
CONTAINER_API bool turbo_multimap_remove(turbo_multimap_t *map,
                                         const void *key, void *out_value);
CONTAINER_API size_t turbo_multimap_erase(turbo_multimap_t *map,
                                          const void *key);
CONTAINER_API size_t turbo_multimap_size(const turbo_multimap_t *map);
CONTAINER_API size_t turbo_multimap_element_limit(const turbo_multimap_t *map);
CONTAINER_API uint64_t turbo_multimap_generation(const turbo_multimap_t *map);
CONTAINER_API bool turbo_multimap_empty(const turbo_multimap_t *map);

CONTAINER_API turbo_multimap_iter_t turbo_multimap_begin(
    const turbo_multimap_t *map);
CONTAINER_API turbo_multimap_iter_t turbo_multimap_end(
    const turbo_multimap_t *map);
CONTAINER_API turbo_multimap_iter_t turbo_multimap_lower_bound(
    const turbo_multimap_t *map, const void *key);
CONTAINER_API turbo_multimap_iter_t turbo_multimap_upper_bound(
    const turbo_multimap_t *map, const void *key);
CONTAINER_API container_status turbo_multimap_iter_next(
    turbo_multimap_iter_t *iterator);
CONTAINER_API container_status turbo_multimap_iter_prev(
    turbo_multimap_iter_t *iterator);
CONTAINER_API bool turbo_multimap_iter_equal(turbo_multimap_iter_t left,
                                              turbo_multimap_iter_t right);
CONTAINER_API const void *turbo_multimap_iter_key_const(
    turbo_multimap_iter_t iterator);
CONTAINER_API void *turbo_multimap_iter_value(turbo_multimap_iter_t iterator);
CONTAINER_API const void *turbo_multimap_iter_value_const(
    turbo_multimap_iter_t iterator);
CONTAINER_API bool turbo_multimap_range_next(
    const turbo_multimap_t *map, cmeta_range_cursor *cursor,
    const void **out_key, const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MULTIMAP_H */
