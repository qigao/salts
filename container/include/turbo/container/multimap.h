#ifndef TURBO_MULTIMAP_H
#define TURBO_MULTIMAP_H

#include <turbo/container/hash_map.h>
#include <turbo/container/vec.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raw key hash/equality callbacks and ctx are borrowed until destroy. The map
 * owns one heap-allocated Vec per live key; returned Vec/key pointers are
 * borrowed and invalidate after a successful mutation, clear, or destroy.
 * capacity() reports hash bucket slots, while key_limit bounds live keys and
 * value_limit bounds values retained by each key. */
typedef struct {
  turbo_hash_map_t map;
  size_t key_limit;
  size_t value_size;
  size_t value_align;
  size_t value_limit;
  size_t size;
} turbo_multimap_t;

static inline turbo_vec_t **turbo_multimap_values_carrier(turbo_multimap_t *map,
                                                           const void *key) {
  return map == NULL ? NULL : (turbo_vec_t **)turbo_hash_map_get(&map->map, key);
}

static inline turbo_vec_t *const *turbo_multimap_values_carrier_const(
    const turbo_multimap_t *map, const void *key) {
  return map == NULL ? NULL : (turbo_vec_t *const *)turbo_hash_map_get_const(&map->map, key);
}

static inline void turbo_multimap_destroy_vectors(turbo_multimap_t *map) {
  size_t slot;
  for (slot = 0u; map != NULL && slot < map->map.capacity; ++slot) {
    turbo_vec_t **carrier = (turbo_vec_t **)turbo_hash_map_value_at(&map->map, slot);
    if (carrier != NULL && *carrier != NULL) {
      turbo_vec_destroy(*carrier);
      free(*carrier);
      *carrier = NULL;
    }
  }
}

static inline int turbo_multimap_init(turbo_multimap_t *map, size_t key_size, size_t key_align,
                                      size_t key_limit, size_t value_size, size_t value_align,
                                      size_t value_limit, turbo_hash_fn hash,
                                      turbo_hash_equal_fn equal, void *ctx) {
  if (map == NULL || map->map.initialized || key_size == 0u || value_size == 0u)
    return CONTAINER_INVALID_ARGUMENT;
  memset(map, 0, sizeof(*map));
  map->key_limit = key_limit;
  map->value_size = value_size;
  map->value_align = value_align;
  map->value_limit = value_limit;
  return turbo_hash_map_init_bytes(&map->map, key_size, key_align, sizeof(turbo_vec_t *),
#ifdef __cplusplus
                                   alignof(turbo_vec_t *),
#else
                                   _Alignof(turbo_vec_t *),
#endif
                                   key_limit, hash, equal, ctx);
}

static inline void turbo_multimap_destroy(turbo_multimap_t *map) {
  if (map == NULL) return;
  turbo_multimap_destroy_vectors(map);
  turbo_hash_map_destroy(&map->map);
  memset(map, 0, sizeof(*map));
}

static inline void turbo_multimap_clear(turbo_multimap_t *map) {
  if (map == NULL) return;
  turbo_multimap_destroy_vectors(map);
  turbo_hash_map_clear(&map->map);
  map->size = 0u;
}

static inline int turbo_multimap_reserve(turbo_multimap_t *map, size_t min_keys) {
  return map == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_hash_map_reserve(&map->map, min_keys);
}

static inline int turbo_multimap_put(turbo_multimap_t *map, const void *key, const void *value) {
  turbo_vec_t **carrier;
  turbo_vec_t *values;
  int rc;

  if (map == NULL || key == NULL || value == NULL) return CONTAINER_INVALID_ARGUMENT;
  carrier = turbo_multimap_values_carrier(map, key);
  if (carrier != NULL) {
    rc = turbo_vec_push(*carrier, value);
    if (rc == CONTAINER_OK) ++map->size;
    return rc;
  }
  values = (turbo_vec_t *)malloc(sizeof(*values));
  if (values == NULL) return CONTAINER_OUT_OF_MEMORY;
  memset(values, 0, sizeof(*values));
  rc = turbo_vec_init_bytes(values, map->value_size, map->value_align, map->value_limit);
  if (rc != CONTAINER_OK) { free(values); return rc; }
  rc = turbo_vec_push(values, value);
  if (rc != CONTAINER_OK) { turbo_vec_destroy(values); free(values); return rc; }
  rc = turbo_hash_map_put(&map->map, key, &values);
  if (rc != CONTAINER_OK) { turbo_vec_destroy(values); free(values); return rc; }
  ++map->size;
  return CONTAINER_OK;
}

static inline const turbo_vec_t *turbo_multimap_get_values_const(const turbo_multimap_t *map,
                                                                   const void *key) {
  turbo_vec_t *const *carrier = turbo_multimap_values_carrier_const(map, key);
  return carrier == NULL ? NULL : *carrier;
}

static inline turbo_vec_t *turbo_multimap_get_values(turbo_multimap_t *map, const void *key) {
  turbo_vec_t **carrier = turbo_multimap_values_carrier(map, key);
  return carrier == NULL ? NULL : *carrier;
}

static inline bool turbo_multimap_contains(const turbo_multimap_t *map, const void *key) {
  return map != NULL && turbo_hash_map_contains(&map->map, key);
}

static inline size_t turbo_multimap_key_count(const turbo_multimap_t *map, const void *key) {
  const turbo_vec_t *values = turbo_multimap_get_values_const(map, key);
  return values == NULL ? 0u : turbo_vec_size(values);
}

static inline size_t turbo_multimap_size(const turbo_multimap_t *map) {
  return map == NULL ? 0u : map->size;
}

static inline size_t turbo_multimap_capacity(const turbo_multimap_t *map) {
  return map == NULL ? 0u : turbo_hash_map_capacity(&map->map);
}

static inline size_t turbo_multimap_entry_limit(const turbo_multimap_t *map) {
  return map == NULL ? 0u : turbo_hash_map_entry_limit(&map->map);
}

static inline bool turbo_multimap_empty(const turbo_multimap_t *map) {
  return map == NULL || map->size == 0u;
}

static inline bool turbo_multimap_remove(turbo_multimap_t *map, const void *key, void *out_value) {
  turbo_vec_t **carrier;
  turbo_vec_t *values;
  size_t before_count;
  int rc;

  if (map == NULL || key == NULL) return false;
  carrier = turbo_multimap_values_carrier(map, key);
  if (carrier == NULL || *carrier == NULL) return false;
  values = *carrier;
  before_count = turbo_vec_size(values);
  if (before_count == 0u) return false;
  rc = turbo_vec_pop(values, out_value);
  if (rc != CONTAINER_OK) return false;
  --map->size;
  if (before_count == 1u) {
    rc = turbo_hash_map_remove(&map->map, key, NULL);
    if (rc != CONTAINER_OK) return false;
    turbo_vec_destroy(values);
    free(values);
  }
  return true;
}

static inline size_t turbo_multimap_erase(turbo_multimap_t *map, const void *key) {
  turbo_vec_t **carrier;
  turbo_vec_t *values;
  size_t removed;
  if (map == NULL || key == NULL) return 0u;
  carrier = turbo_multimap_values_carrier(map, key);
  if (carrier == NULL || *carrier == NULL) return 0u;
  values = *carrier;
  removed = turbo_vec_size(values);
  if (turbo_hash_map_remove(&map->map, key, NULL) != CONTAINER_OK) return 0u;
  turbo_vec_destroy(values);
  free(values);
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
