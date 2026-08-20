#ifndef TURBO_MAP_H
#define TURBO_MAP_H

#include "turbo_hash_map.h"

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

#define TURBO_MAP_DEFINE(name, key_type, value_type)                                                 \
  typedef struct {                                                                                  \
    key_type key;                                                                                   \
    value_type value;                                                                               \
  } name##_entry;                                                                                   \
  typedef struct {                                                                                  \
    turbo_map_t raw;                                                                               \
  } name;                                                                                           \
  static inline int name##_init(name *map) {                                                         \
    return turbo_map_init(&map->raw, sizeof(key_type), sizeof(value_type), NULL, NULL, NULL);        \
  }                                                                                                 \
  static inline int name##_from(name *map, const name##_entry *entries, size_t count) {              \
    size_t i;                                                                                       \
    int rc;                                                                                         \
    if (!map || (count > 0 && !entries)) return TURBO_EINVAL;                                       \
    rc = name##_init(map);                                                                          \
    if (rc != TURBO_OK) return rc;                                                                  \
    rc = turbo_map_reserve(&map->raw, count);                                                       \
    if (rc != TURBO_OK) {                                                                           \
      turbo_map_destroy(&map->raw);                                                                 \
      return rc;                                                                                    \
    }                                                                                               \
    for (i = 0; i < count; ++i) {                                                                   \
      rc = turbo_map_put(&map->raw, &entries[i].key, &entries[i].value);                            \
      if (rc != TURBO_OK) {                                                                         \
        turbo_map_destroy(&map->raw);                                                               \
        return rc;                                                                                  \
      }                                                                                             \
    }                                                                                               \
    return TURBO_OK;                                                                                \
  }                                                                                                 \
  static inline void name##_destroy(name *map) { turbo_map_destroy(&map->raw); }                     \
  static inline void name##_clear(name *map) { turbo_map_clear(&map->raw); }                         \
  static inline int name##_reserve(name *map, size_t capacity) {                                      \
    return turbo_map_reserve(&map->raw, capacity);                                                  \
  }                                                                                                 \
  static inline int name##_put(name *map, key_type key, value_type value) {                          \
    return turbo_map_put(&map->raw, &key, &value);                                                  \
  }                                                                                                 \
  static inline value_type *name##_get(name *map, key_type key) {                                    \
    return (value_type *)turbo_map_get(&map->raw, &key);                                            \
  }                                                                                                 \
  static inline const value_type *name##_get_const(const name *map, key_type key) {                   \
    return (const value_type *)turbo_map_get_const(&map->raw, &key);                                \
  }                                                                                                 \
  static inline bool name##_contains(const name *map, key_type key) {                                \
    return turbo_map_contains(&map->raw, &key);                                                      \
  }                                                                                                 \
  static inline int name##_remove(name *map, key_type key, value_type *out_value) {                   \
    return turbo_map_remove(&map->raw, &key, out_value);                                            \
  }                                                                                                 \
  static inline size_t name##_size(const name *map) { return turbo_map_size(&map->raw); }             \
  static inline size_t name##_capacity(const name *map) { return turbo_map_capacity(&map->raw); }     \
  static inline bool name##_empty(const name *map) { return turbo_map_empty(&map->raw); }             \
  static inline const key_type *name##_key_at(const name *map, size_t slot) {                        \
    return (const key_type *)turbo_map_key_at(&map->raw, slot);                                     \
  }                                                                                                 \
  static inline value_type *name##_value_at(name *map, size_t slot) {                                \
    return (value_type *)turbo_map_value_at(&map->raw, slot);                                       \
  }                                                                                                 \
  static inline const value_type *name##_value_at_const(const name *map, size_t slot) {               \
    return (const value_type *)turbo_map_value_at_const(&map->raw, slot);                           \
  }

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MAP_H */
