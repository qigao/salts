#ifndef TURBO_MAP_H
#define TURBO_MAP_H

#include <cmeta/range.h>
#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*map_compare_fn)(const void *left, const void *right,
                                    void *context);

typedef struct map {
  void *impl;
  uint64_t generation;
} map_t;

typedef struct map_iter {
  const map_t *owner;
  void *node;
} map_iter_t;

/* Map is a unique-key red-black tree. Type descriptors or the raw comparator
 * and context are borrowed through destroy. Every live entry owns one copied
 * key and value and is bounded by entry_limit. */
turbostl_status map_init(
    map_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
turbostl_status map_init_bytes(
    map_t *map, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit, map_compare_fn compare,
    void *context);
turbostl_status map_from_arrays(
    map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
turbostl_status map_from_arrays_bytes(
    map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, map_compare_fn compare, void *context);
void map_destroy(map_t *map);
void map_clear(map_t *map);
turbostl_status map_put(map_t *map,
                                              const void *key,
                                              const void *value);
void *map_get(map_t *map, const void *key);
const void *map_get_const(const map_t *map,
                                               const void *key);
bool map_contains(const map_t *map,
                                      const void *key);
turbostl_status map_remove(map_t *map,
                                                 const void *key,
                                                 void *out_value);
size_t map_size(const map_t *map);
size_t map_entry_limit(const map_t *map);
uint64_t map_generation(const map_t *map);
bool map_empty(const map_t *map);

map_iter_t map_begin(const map_t *map);
map_iter_t map_end(const map_t *map);
map_iter_t map_lower_bound(const map_t *map,
                                                      const void *key);
map_iter_t map_upper_bound(const map_t *map,
                                                      const void *key);
turbostl_status map_iter_next(map_iter_t *iterator);
turbostl_status map_iter_prev(map_iter_t *iterator);
bool map_iter_equal(map_iter_t left,
                                         map_iter_t right);
const void *map_iter_key_const(map_iter_t iterator);
void *map_iter_value(map_iter_t iterator);
const void *map_iter_value_const(
    map_iter_t iterator);

bool map_range_next(const map_t *map,
                                        cmeta_range_cursor *cursor,
                                        const void **out_key,
                                        const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MAP_H */
