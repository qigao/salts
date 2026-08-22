#ifndef TURBOSTL_MULTIMAP_H
#define TURBOSTL_MULTIMAP_H

#include <turbostl/map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef map_compare_fn multimap_compare_fn;

typedef struct multimap {
  cmeta_container_header cmeta;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  void *impl;
  uint64_t generation;
} multimap_t;

typedef struct multimap_iter {
  const multimap_t *owner;
  void *node;
} multimap_iter_t;

/* Internal typed bridges for legacy/generated callers. */
stl_status multimap_raw_init(multimap_t *map,
                             const cmeta_type_desc *key_type,
                             const cmeta_type_desc *value_type,
                             size_t element_limit);
stl_status multimap_raw_from_arrays(
    multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t element_limit);
void multimap_raw_destroy_storage(multimap_t *map);

stl_status multimap_init_bytes(multimap_t *map, size_t key_size,
                               size_t key_align, size_t value_size,
                               size_t value_align, size_t element_limit,
                               multimap_compare_fn compare, void *context);
stl_status multimap_from_arrays_bytes(
    multimap_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, multimap_compare_fn compare, void *context);

static inline stl_status multimap_init(multimap_t *map,
                                       size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (map == NULL || map->key_type == NULL || map->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  status = multimap_raw_init(map, key_type, value_type, element_limit);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
  return status;
}

static inline stl_status multimap_from_arrays(
    multimap_t *map, const void *keys, const void *values, size_t count,
    size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (map == NULL || map->key_type == NULL || map->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  status = multimap_raw_from_arrays(map, keys, values, count, key_type,
                                    value_type, element_limit);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
  return status;
}

static inline void multimap_destroy(multimap_t *map) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  if (map == NULL)
    return;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  multimap_raw_destroy_storage(map);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
}

void multimap_clear(multimap_t *map);
stl_status multimap_put(multimap_t *map, const void *key, const void *value);
bool multimap_contains(const multimap_t *map, const void *key);
size_t multimap_count(const multimap_t *map, const void *key);
bool multimap_remove(multimap_t *map, const void *key, void *out_value);
size_t multimap_erase(multimap_t *map, const void *key);
size_t multimap_size(const multimap_t *map);
size_t multimap_element_limit(const multimap_t *map);
uint64_t multimap_generation(const multimap_t *map);
bool multimap_empty(const multimap_t *map);

multimap_iter_t multimap_begin(const multimap_t *map);
multimap_iter_t multimap_end(const multimap_t *map);
multimap_iter_t multimap_lower_bound(const multimap_t *map, const void *key);
multimap_iter_t multimap_upper_bound(const multimap_t *map, const void *key);
stl_status multimap_iter_next(multimap_iter_t *iterator);
stl_status multimap_iter_prev(multimap_iter_t *iterator);
bool multimap_iter_equal(multimap_iter_t left, multimap_iter_t right);
const void *multimap_iter_key_const(multimap_iter_t iterator);
void *multimap_iter_value(multimap_iter_t iterator);
const void *multimap_iter_value_const(multimap_iter_t iterator);
bool multimap_range_next(const multimap_t *map, cmeta_range_cursor *cursor,
                         const void **out_key, const void **out_value);


#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_MULTIMAP_H */
