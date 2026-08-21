#ifndef TURBO_MAP_H
#define TURBO_MAP_H

#include <cmeta/range.h>
#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*turbo_map_compare_fn)(const void *left, const void *right,
                                    void *context);

typedef struct turbo_map {
  void *impl;
  uint64_t generation;
} turbo_map_t;

typedef struct turbo_map_iter {
  const turbo_map_t *owner;
  void *node;
} turbo_map_iter_t;

/* Map is a unique-key red-black tree. Type descriptors or the raw comparator
 * and context are borrowed through destroy. Every live entry owns one copied
 * key and value and is bounded by entry_limit. */
CONTAINER_API container_status turbo_map_init(
    turbo_map_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
CONTAINER_API container_status turbo_map_init_bytes(
    turbo_map_t *map, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit, turbo_map_compare_fn compare,
    void *context);
CONTAINER_API container_status turbo_map_from_arrays(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
CONTAINER_API container_status turbo_map_from_arrays_bytes(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_map_compare_fn compare, void *context);
CONTAINER_API void turbo_map_destroy(turbo_map_t *map);
CONTAINER_API void turbo_map_clear(turbo_map_t *map);
CONTAINER_API container_status turbo_map_put(turbo_map_t *map,
                                              const void *key,
                                              const void *value);
CONTAINER_API void *turbo_map_get(turbo_map_t *map, const void *key);
CONTAINER_API const void *turbo_map_get_const(const turbo_map_t *map,
                                               const void *key);
CONTAINER_API bool turbo_map_contains(const turbo_map_t *map,
                                      const void *key);
CONTAINER_API container_status turbo_map_remove(turbo_map_t *map,
                                                 const void *key,
                                                 void *out_value);
CONTAINER_API size_t turbo_map_size(const turbo_map_t *map);
CONTAINER_API size_t turbo_map_entry_limit(const turbo_map_t *map);
CONTAINER_API uint64_t turbo_map_generation(const turbo_map_t *map);
CONTAINER_API bool turbo_map_empty(const turbo_map_t *map);

CONTAINER_API turbo_map_iter_t turbo_map_begin(const turbo_map_t *map);
CONTAINER_API turbo_map_iter_t turbo_map_end(const turbo_map_t *map);
CONTAINER_API turbo_map_iter_t turbo_map_lower_bound(const turbo_map_t *map,
                                                      const void *key);
CONTAINER_API turbo_map_iter_t turbo_map_upper_bound(const turbo_map_t *map,
                                                      const void *key);
CONTAINER_API container_status turbo_map_iter_next(turbo_map_iter_t *iterator);
CONTAINER_API container_status turbo_map_iter_prev(turbo_map_iter_t *iterator);
CONTAINER_API bool turbo_map_iter_equal(turbo_map_iter_t left,
                                         turbo_map_iter_t right);
CONTAINER_API const void *turbo_map_iter_key_const(turbo_map_iter_t iterator);
CONTAINER_API void *turbo_map_iter_value(turbo_map_iter_t iterator);
CONTAINER_API const void *turbo_map_iter_value_const(
    turbo_map_iter_t iterator);

CONTAINER_API bool turbo_map_range_next(const turbo_map_t *map,
                                        cmeta_range_cursor *cursor,
                                        const void **out_key,
                                        const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MAP_H */
