#ifndef TURBOSTL_MAP_H
#define TURBOSTL_MAP_H

#include <cmeta/range.h>
#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*map_compare_fn)(const void *left, const void *right,
                              void *context);

typedef struct map {
  cmeta_container_header cmeta;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  void *impl;
  uint64_t generation;
} map_t;

typedef struct map_iter {
  const map_t *owner;
  void *node;
} map_iter_t;

/* Internal typed bridge used while implementation symbols are migrated. */
stl_status map_init_with_types(map_t *map, const cmeta_type_desc *key_type,
                               const cmeta_type_desc *value_type,
                               size_t entry_limit);
stl_status map_from_arrays_with_types(
    map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
void map_destroy_storage(map_t *map);

/* Raw byte entry points remain explicit. */
stl_status map_init_bytes(map_t *map, size_t key_size, size_t key_align,
                          size_t value_size, size_t value_align,
                          size_t entry_limit, map_compare_fn compare,
                          void *context);
stl_status map_from_arrays_bytes(
    map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, map_compare_fn compare, void *context);

/* Self-describing typed entry points consume declaration-time bindings. */
static inline stl_status map_init(map_t *map, size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (map == NULL || map->key_type == NULL || map->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  status = map_init_with_types(map, key_type, value_type, entry_limit);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
  return status;
}

static inline stl_status map_from_arrays(map_t *map, const void *keys,
                                         const void *values, size_t count,
                                         size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (map == NULL || map->key_type == NULL || map->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  status = map_from_arrays_with_types(map, keys, values, count, key_type,
                                      value_type, entry_limit);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
  return status;
}

static inline void map_destroy(map_t *map) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  if (map == NULL)
    return;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  map_destroy_storage(map);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
}

void map_clear(map_t *map);
stl_status map_put(map_t *map, const void *key, const void *value);
void *map_get(map_t *map, const void *key);
const void *map_get_const(const map_t *map, const void *key);
bool map_contains(const map_t *map, const void *key);
stl_status map_remove(map_t *map, const void *key, void *out_value);
size_t map_size(const map_t *map);
size_t map_entry_limit(const map_t *map);
uint64_t map_generation(const map_t *map);
bool map_empty(const map_t *map);

map_iter_t map_begin(const map_t *map);
map_iter_t map_end(const map_t *map);
map_iter_t map_lower_bound(const map_t *map, const void *key);
map_iter_t map_upper_bound(const map_t *map, const void *key);
stl_status map_iter_next(map_iter_t *iterator);
stl_status map_iter_prev(map_iter_t *iterator);
bool map_iter_equal(map_iter_t left, map_iter_t right);
const void *map_iter_key_const(map_iter_t iterator);
void *map_iter_value(map_iter_t iterator);
const void *map_iter_value_const(map_iter_t iterator);

bool map_range_next(const map_t *map, cmeta_range_cursor *cursor,
                    const void **out_key, const void **out_value);

/* Instance-driven values Range used by CFlow stream_values(). */
static inline size_t stl_map_range_size(const void *object) {
  return map_size((const map_t *)object);
}

static inline uint64_t stl_map_range_version(const void *object) {
  return map_generation((const map_t *)object);
}

static inline cmeta_gen_status stl_map_values_range_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
  const map_t *map = (const map_t *)object;
  const void *key = NULL;
  const void *value = NULL;
  if (map == NULL || cursor == NULL || out_value == NULL ||
      map->value_type == NULL)
    return CMETA_GEN_ERROR;
  if (!map_range_next(map, cursor, &key, &value))
    return CMETA_GEN_DONE;
  (void)key;
  memcpy(out_value, value, map->value_type->size);
  return cursor->state[0] == NULL ? CMETA_GEN_VALUE_AND_DONE :
                                    CMETA_GEN_VALUE;
}

static inline cmeta_range stl_map_values_range_factory(const void *object) {
  const map_t *map = (const map_t *)object;
  cmeta_range range = {
      object,
      map != NULL ? map->value_type : NULL,
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
      stl_map_range_size,
      stl_map_values_range_next,
      stl_map_range_version(object),
      stl_map_range_version};
  return range;
}

static const cmeta_container_desc stl_map_container_desc = {
    "Map",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    stl_map_values_range_factory,
    NULL,
    NULL};

/* Temporary repository-migration aliases. */
typedef map_compare_fn turbo_map_compare_fn;
typedef map_t turbo_map_t;
typedef map_iter_t turbo_map_iter_t;
#define turbo_map_init map_init_with_types
#define turbo_map_init_bytes map_init_bytes
#define turbo_map_from_arrays map_from_arrays_with_types
#define turbo_map_from_arrays_bytes map_from_arrays_bytes
#define turbo_map_destroy map_destroy_storage
#define turbo_map_clear map_clear
#define turbo_map_put map_put
#define turbo_map_get map_get
#define turbo_map_get_const map_get_const
#define turbo_map_contains map_contains
#define turbo_map_remove map_remove
#define turbo_map_size map_size
#define turbo_map_entry_limit map_entry_limit
#define turbo_map_generation map_generation
#define turbo_map_empty map_empty
#define turbo_map_begin map_begin
#define turbo_map_end map_end
#define turbo_map_lower_bound map_lower_bound
#define turbo_map_upper_bound map_upper_bound
#define turbo_map_iter_next map_iter_next
#define turbo_map_iter_prev map_iter_prev
#define turbo_map_iter_equal map_iter_equal
#define turbo_map_iter_key_const map_iter_key_const
#define turbo_map_iter_value map_iter_value
#define turbo_map_iter_value_const map_iter_value_const
#define turbo_map_range_next map_range_next

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_MAP_H */
