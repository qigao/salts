#ifndef TURBO_MULTIMAP_H
#define TURBO_MULTIMAP_H

#include <turbo/container/hash_map.h>
#include <turbo/container/vec.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_hash_map_t map;
  size_t key_limit;
  size_t value_size;
  size_t value_align;
  size_t value_limit;
  size_t size;
} turbo_multimap_t;

static inline int turbo_multimap_init(turbo_multimap_t *map, size_t key_size, size_t key_align,
                                     size_t key_limit, size_t value_size, size_t value_align,
                                     size_t value_limit, turbo_hash_fn hash,
                                     turbo_hash_equal_fn equal, void *ctx) {
  if (!map || key_size == 0 || value_size == 0) return CONTAINER_INVALID_ARGUMENT;
  memset(map, 0, sizeof(*map));
  map->key_limit = key_limit;
  map->value_size = value_size;
  map->value_align = value_align;
  map->value_limit = value_limit;
  return turbo_hash_map_init_bytes(&map->map, key_size, key_align, sizeof(turbo_vec_t),
#ifdef __cplusplus
                                   alignof(turbo_vec_t),
#else
                                   _Alignof(turbo_vec_t),
#endif
                                   key_limit, hash, equal, ctx);
}

static inline void turbo_multimap_destroy(turbo_multimap_t *map) {
  size_t slot;
  turbo_vec_t *values;

  if (!map) return;
  for (slot = 0; slot < map->map.capacity; ++slot) {
    values = (turbo_vec_t *)turbo_hash_map_value_at(&map->map, slot);
    if (values != NULL) turbo_vec_destroy(values);
  }
  turbo_hash_map_destroy(&map->map);
  memset(map, 0, sizeof(*map));
}

static inline void turbo_multimap_clear(turbo_multimap_t *map) {
  size_t slot;
  turbo_vec_t *values;

  if (!map) return;
  for (slot = 0; slot < map->map.capacity; ++slot) {
    values = (turbo_vec_t *)turbo_hash_map_value_at(&map->map, slot);
    if (values != NULL) turbo_vec_destroy(values);
  }
  turbo_hash_map_clear(&map->map);
  map->size = 0;
}

static inline int turbo_multimap_reserve(turbo_multimap_t *map, size_t min_capacity) {
  if (!map) return CONTAINER_INVALID_ARGUMENT;
  return turbo_hash_map_reserve(&map->map, min_capacity);
}

static inline int turbo_multimap_put(turbo_multimap_t *map, const void *key, const void *value) {
  turbo_vec_t values;
  turbo_vec_t *stored_values;
  int rc;

  if (!map || !key || !value) return CONTAINER_INVALID_ARGUMENT;
  stored_values = (turbo_vec_t *)turbo_hash_map_get(&map->map, key);
  if (stored_values != NULL) {
    rc = turbo_vec_push(stored_values, value);
    if (rc != CONTAINER_OK) return rc;
    ++map->size;
    return CONTAINER_OK;
  }

  rc = turbo_vec_init_bytes(&values, map->value_size, map->value_align, map->value_limit);
  if (rc != CONTAINER_OK) return rc;
  rc = turbo_vec_push(&values, value);
  if (rc != CONTAINER_OK) {
    turbo_vec_destroy(&values);
    return rc;
  }

  rc = turbo_hash_map_put(&map->map, key, &values);
  if (rc != CONTAINER_OK) {
    turbo_vec_destroy(&values);
    return rc;
  }
  map->size += 1U;
  return CONTAINER_OK;
}

static inline const turbo_vec_t *turbo_multimap_get_values_const(const turbo_multimap_t *map,
                                                               const void *key) {
  return (const turbo_vec_t *)turbo_hash_map_get_const(map == NULL ? NULL : &map->map, key);
}

static inline turbo_vec_t *turbo_multimap_get_values(turbo_multimap_t *map, const void *key) {
  return (turbo_vec_t *)turbo_hash_map_get(map == NULL ? NULL : &map->map, key);
}

static inline bool turbo_multimap_contains(const turbo_multimap_t *map, const void *key) {
  return turbo_hash_map_contains(map == NULL ? NULL : &map->map, key);
}

static inline size_t turbo_multimap_key_count(const turbo_multimap_t *map, const void *key) {
  const turbo_vec_t *values = turbo_multimap_get_values_const(map, key);
  return values == NULL ? 0U : turbo_vec_size(values);
}

static inline size_t turbo_multimap_size(const turbo_multimap_t *map) {
  return map == NULL ? 0U : map->size;
}

static inline size_t turbo_multimap_capacity(const turbo_multimap_t *map) {
  return map == NULL ? 0U : turbo_hash_map_capacity(&map->map);
}

static inline bool turbo_multimap_empty(const turbo_multimap_t *map) {
  return map == NULL || map->size == 0U;
}

static inline bool turbo_multimap_remove(turbo_multimap_t *map, const void *key, void *out_value) {
  turbo_vec_t *values;
  turbo_vec_t removed_values;
  size_t before_count;
  int rc;

  if (!map || !key) return false;
  values = (turbo_vec_t *)turbo_hash_map_get(&map->map, key);
  if (!values) return false;
  before_count = turbo_vec_size(values);
  if (before_count == 0U) {
    (void)turbo_hash_map_remove(&map->map, key, NULL);
    return false;
  }
  if (before_count == 1U) {
    rc = turbo_hash_map_remove(&map->map, key, &removed_values);
    if (rc != CONTAINER_OK) return false;
    if (out_value && removed_values.size > 0U) {
      memcpy(out_value, (unsigned char *)removed_values.data + (removed_values.size - 1U) * map->value_size,
             map->value_size);
    }
    turbo_vec_destroy(&removed_values);
    map->size -= 1U;
    return true;
  }
  rc = turbo_vec_pop(values, out_value);
  if (rc != CONTAINER_OK) return false;
  map->size -= 1U;
  return true;
}

static inline size_t turbo_multimap_erase(turbo_multimap_t *map, const void *key) {
  turbo_vec_t values;
  size_t removed;
  int rc;

  if (!map || !key) return 0U;
  removed = turbo_multimap_key_count(map, key);
  if (removed == 0U) return 0U;
  rc = turbo_hash_map_remove(&map->map, key, &values);
  if (rc != CONTAINER_OK) return 0U;
  turbo_vec_destroy(&values);
  map->size -= removed;
  return removed;
}

#define TURBO_MULTI_MAP_DEFINE(name, key_type, value_type) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, turbo_multimap_t, turbo_multimap, CONTAINER_OK, _, TURBO_META_MULTIMAP_METHODS) \
  CMETA_CONTAINER2_OPAQUE_DESCRIPTOR_DEFINE(name, key_type, value_type)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MULTIMAP_H */
