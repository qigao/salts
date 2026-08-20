#ifndef CONTAINER_MAP_H
#define CONTAINER_MAP_H

#include <container/hash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef container_hash_map_t container_map_t;

static inline int container_map_init(container_map_t *map, size_t key_size, size_t value_size,
                                container_hash_fn hash, container_hash_equal_fn equal, void *ctx) {
  return container_hash_map_init(map, key_size, value_size, hash, equal, ctx);
}

static inline int container_map_from_arrays(container_map_t *map, const void *keys,
                                        const void *values, size_t count, size_t key_size,
                                        size_t value_size, container_hash_fn hash,
                                        container_hash_equal_fn equal, void *ctx) {
  return container_hash_map_from_arrays(map, keys, values, count, key_size, value_size,
                                    hash, equal, ctx);
}

static inline void container_map_destroy(container_map_t *map) {
  container_hash_map_destroy(map);
}

static inline void container_map_clear(container_map_t *map) {
  container_hash_map_clear(map);
}

static inline int container_map_reserve(container_map_t *map, size_t min_capacity) {
  return container_hash_map_reserve(map, min_capacity);
}

static inline int container_map_put(container_map_t *map, const void *key, const void *value) {
  return container_hash_map_put(map, key, value);
}

static inline void *container_map_get(container_map_t *map, const void *key) {
  return container_hash_map_get(map, key);
}

static inline const void *container_map_get_const(const container_map_t *map, const void *key) {
  return container_hash_map_get_const(map, key);
}

static inline bool container_map_contains(const container_map_t *map, const void *key) {
  return container_hash_map_contains(map, key);
}

static inline int container_map_remove(container_map_t *map, const void *key, void *out_value) {
  return container_hash_map_remove(map, key, out_value);
}

static inline size_t container_map_size(const container_map_t *map) {
  return container_hash_map_size(map);
}

static inline size_t container_map_capacity(const container_map_t *map) {
  return container_hash_map_capacity(map);
}

static inline bool container_map_empty(const container_map_t *map) {
  return container_hash_map_empty(map);
}

static inline const void *container_map_key_at(const container_map_t *map, size_t slot) {
  return container_hash_map_key_at(map, slot);
}

static inline const void *container_map_key_at_const(const container_map_t *map, size_t slot) {
  return container_map_key_at(map, slot);
}

static inline void *container_map_value_at(container_map_t *map, size_t slot) {
  return container_hash_map_value_at(map, slot);
}

static inline const void *container_map_value_at_const(const container_map_t *map, size_t slot) {
  return container_hash_map_value_at_const(map, slot);
}


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_MAP_H */
