#ifndef TURBO_MULTIMAP_H
#define TURBO_MULTIMAP_H

#include <turbostl/map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef map_compare_fn multimap_compare_fn;

typedef struct multimap {
  void *impl;
  uint64_t generation;
} multimap_t;

typedef struct multimap_iter {
  const multimap_t *owner;
  void *node;
} multimap_iter_t;

/* MultiMap is a red-black tree that stores each key/value pair in its own
 * node. Equivalent keys retain insertion order in traversal and all pairs
 * share one explicit element_limit. */
turbostl_status multimap_init(
    multimap_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t element_limit);
turbostl_status multimap_init_bytes(
    multimap_t *map, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    multimap_compare_fn compare, void *context);
turbostl_status multimap_from_arrays(
    multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t element_limit);
turbostl_status multimap_from_arrays_bytes(
    multimap_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, multimap_compare_fn compare, void *context);
void multimap_destroy(multimap_t *map);
void multimap_clear(multimap_t *map);
turbostl_status multimap_put(multimap_t *map,
                                                   const void *key,
                                                   const void *value);
bool multimap_contains(const multimap_t *map,
                                           const void *key);
size_t multimap_count(const multimap_t *map,
                                          const void *key);
/* remove erases the most recently inserted equivalent pair. */
bool multimap_remove(multimap_t *map,
                                         const void *key, void *out_value);
size_t multimap_erase(multimap_t *map,
                                          const void *key);
size_t multimap_size(const multimap_t *map);
size_t multimap_element_limit(const multimap_t *map);
uint64_t multimap_generation(const multimap_t *map);
bool multimap_empty(const multimap_t *map);

multimap_iter_t multimap_begin(
    const multimap_t *map);
multimap_iter_t multimap_end(
    const multimap_t *map);
multimap_iter_t multimap_lower_bound(
    const multimap_t *map, const void *key);
multimap_iter_t multimap_upper_bound(
    const multimap_t *map, const void *key);
turbostl_status multimap_iter_next(
    multimap_iter_t *iterator);
turbostl_status multimap_iter_prev(
    multimap_iter_t *iterator);
bool multimap_iter_equal(multimap_iter_t left,
                                              multimap_iter_t right);
const void *multimap_iter_key_const(
    multimap_iter_t iterator);
void *multimap_iter_value(multimap_iter_t iterator);
const void *multimap_iter_value_const(
    multimap_iter_t iterator);
bool multimap_range_next(
    const multimap_t *map, cmeta_range_cursor *cursor,
    const void **out_key, const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MULTIMAP_H */
