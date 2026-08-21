#ifndef TURBO_MAP_H
#define TURBO_MAP_H

#include <turbo/container/btree.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef turbo_btree_t turbo_map_t;

/* Map is the ordered associative facade over BTree. Descriptors and raw
 * comparator context are borrowed through destroy. Keys require COMPARE in
 * typed mode; key/entry traversal is ordered, sorted, and unique. */
static inline container_status turbo_map_init(
    turbo_map_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit) {
  return turbo_btree_init(map, key_type, value_type, entry_limit);
}

static inline container_status turbo_map_init_bytes(
    turbo_map_t *map, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit, turbo_btree_compare_fn compare,
    void *ctx) {
  return turbo_btree_init_bytes(map, key_size, key_align, value_size,
                                value_align, entry_limit, compare, ctx);
}

static inline container_status turbo_map_from_arrays(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit) {
  return turbo_btree_from_arrays(map, keys, values, count, key_type,
                                 value_type, entry_limit);
}

static inline container_status turbo_map_from_arrays_bytes(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_btree_compare_fn compare, void *ctx) {
  return turbo_btree_from_arrays_bytes(
      map, keys, values, count, key_size, key_align, value_size, value_align,
      entry_limit, compare, ctx);
}

static inline void turbo_map_destroy(turbo_map_t *map) {
  turbo_btree_destroy(map);
}

static inline void turbo_map_clear(turbo_map_t *map) {
  turbo_btree_clear(map);
}

static inline container_status turbo_map_reserve(turbo_map_t *map,
                                                  size_t min_entries) {
  return turbo_btree_reserve(map, min_entries);
}

static inline container_status turbo_map_put(turbo_map_t *map,
                                              const void *key,
                                              const void *value) {
  return turbo_btree_put(map, key, value);
}

static inline void *turbo_map_get(turbo_map_t *map, const void *key) {
  return turbo_btree_get(map, key);
}

static inline const void *turbo_map_get_const(const turbo_map_t *map,
                                              const void *key) {
  return turbo_btree_get_const(map, key);
}

static inline bool turbo_map_contains(const turbo_map_t *map,
                                      const void *key) {
  return turbo_btree_contains(map, key);
}

static inline container_status turbo_map_remove(turbo_map_t *map,
                                                 const void *key,
                                                 void *out_value) {
  return turbo_btree_remove(map, key, out_value);
}

static inline size_t turbo_map_size(const turbo_map_t *map) {
  return turbo_btree_size(map);
}

static inline size_t turbo_map_capacity(const turbo_map_t *map) {
  return turbo_btree_capacity(map);
}

static inline size_t turbo_map_entry_limit(const turbo_map_t *map) {
  return turbo_btree_entry_limit(map);
}

static inline uint64_t turbo_map_generation(const turbo_map_t *map) {
  return turbo_btree_generation(map);
}

static inline bool turbo_map_empty(const turbo_map_t *map) {
  return turbo_btree_empty(map);
}

static inline void *turbo_map_key_at(turbo_map_t *map, size_t index) {
  return turbo_btree_key_at(map, index);
}

static inline const void *turbo_map_key_at_const(const turbo_map_t *map,
                                                 size_t index) {
  return turbo_btree_key_at_const(map, index);
}

static inline void *turbo_map_value_at(turbo_map_t *map, size_t index) {
  return turbo_btree_value_at(map, index);
}

static inline const void *turbo_map_value_at_const(const turbo_map_t *map,
                                                   size_t index) {
  return turbo_btree_value_at_const(map, index);
}

static inline bool turbo_map_range_next(const turbo_map_t *map,
                                        size_t *cursor,
                                        const void **out_key,
                                        const void **out_value) {
  return turbo_btree_range_next(map, cursor, out_key, out_value);
}

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MAP_H */
