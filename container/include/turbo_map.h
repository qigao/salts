#ifndef TURBO_MAP_H
#define TURBO_MAP_H

#include "turbo_hash_map.h"
#include "turbo_container_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef turbo_hash_map_t turbo_map_t;

static inline int turbo_map_init(turbo_map_t *map, size_t key_size, size_t value_size,
                                turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx) {
  return turbo_hash_map_init(map, key_size, value_size, hash, equal, ctx);
}

static inline int turbo_map_from_arrays(turbo_map_t *map, const void *keys,
                                        const void *values, size_t count, size_t key_size,
                                        size_t value_size, turbo_hash_fn hash,
                                        turbo_hash_equal_fn equal, void *ctx) {
  return turbo_hash_map_from_arrays(map, keys, values, count, key_size, value_size,
                                    hash, equal, ctx);
}

static inline void turbo_map_destroy(turbo_map_t *map) {
  turbo_hash_map_destroy(map);
}

static inline void turbo_map_clear(turbo_map_t *map) {
  turbo_hash_map_clear(map);
}

static inline int turbo_map_reserve(turbo_map_t *map, size_t min_capacity) {
  return turbo_hash_map_reserve(map, min_capacity);
}

static inline int turbo_map_put(turbo_map_t *map, const void *key, const void *value) {
  return turbo_hash_map_put(map, key, value);
}

static inline void *turbo_map_get(turbo_map_t *map, const void *key) {
  return turbo_hash_map_get(map, key);
}

static inline const void *turbo_map_get_const(const turbo_map_t *map, const void *key) {
  return turbo_hash_map_get_const(map, key);
}

static inline bool turbo_map_contains(const turbo_map_t *map, const void *key) {
  return turbo_hash_map_contains(map, key);
}

static inline int turbo_map_remove(turbo_map_t *map, const void *key, void *out_value) {
  return turbo_hash_map_remove(map, key, out_value);
}

static inline size_t turbo_map_size(const turbo_map_t *map) {
  return turbo_hash_map_size(map);
}

static inline size_t turbo_map_capacity(const turbo_map_t *map) {
  return turbo_hash_map_capacity(map);
}

static inline bool turbo_map_empty(const turbo_map_t *map) {
  return turbo_hash_map_empty(map);
}

static inline const void *turbo_map_key_at(const turbo_map_t *map, size_t slot) {
  return turbo_hash_map_key_at(map, slot);
}

static inline const void *turbo_map_key_at_const(const turbo_map_t *map, size_t slot) {
  return turbo_map_key_at(map, slot);
}

static inline void *turbo_map_value_at(turbo_map_t *map, size_t slot) {
  return turbo_hash_map_value_at(map, slot);
}

static inline const void *turbo_map_value_at_const(const turbo_map_t *map, size_t slot) {
  return turbo_hash_map_value_at_const(map, slot);
}

#define TURBO_MAP_DEFINE(name, key_type, value_type) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, turbo_map_t, turbo_map, TURBO_OK, _, TURBO_META_MAP_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, turbo_map, key_at_const, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MAP_H */
