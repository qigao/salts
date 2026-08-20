#ifndef TURBO_MULTIMAP_H
#define TURBO_MULTIMAP_H

#include "turbo_hash_map.h"
#include "turbo_vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_hash_map_t map;
  size_t value_size;
  size_t size;
} turbo_multimap_t;

static inline int turbo_multimap_init(turbo_multimap_t *map, size_t key_size, size_t value_size,
                                     turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx) {
  if (!map || key_size == 0 || value_size == 0) return TURBO_EINVAL;
  memset(map, 0, sizeof(*map));
  map->value_size = value_size;
  return turbo_hash_map_init(&map->map, key_size, sizeof(turbo_vec_t), hash, equal, ctx);
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
  if (!map) return TURBO_EINVAL;
  return turbo_hash_map_reserve(&map->map, min_capacity);
}

static inline int turbo_multimap_put(turbo_multimap_t *map, const void *key, const void *value) {
  turbo_vec_t values;
  turbo_vec_t *stored_values;
  int rc;

  if (!map || !key || !value) return TURBO_EINVAL;
  stored_values = (turbo_vec_t *)turbo_hash_map_get(&map->map, key);
  if (stored_values != NULL) {
    rc = turbo_vec_push(stored_values, value);
    if (rc != TURBO_OK) return rc;
    ++map->size;
    return TURBO_OK;
  }

  rc = turbo_vec_init(&values, map->value_size);
  if (rc != TURBO_OK) return rc;
  rc = turbo_vec_push(&values, value);
  if (rc != TURBO_OK) {
    turbo_vec_destroy(&values);
    return rc;
  }

  rc = turbo_hash_map_put(&map->map, key, &values);
  if (rc != TURBO_OK) {
    turbo_vec_destroy(&values);
    return rc;
  }
  map->size += 1U;
  return TURBO_OK;
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
    if (rc != TURBO_OK) return false;
    if (out_value && removed_values.size > 0U) {
      memcpy(out_value, (unsigned char *)removed_values.data + (removed_values.size - 1U) * map->value_size,
             map->value_size);
    }
    turbo_vec_destroy(&removed_values);
    map->size -= 1U;
    return true;
  }
  rc = turbo_vec_pop(values, out_value);
  if (rc != TURBO_OK) return false;
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
  if (rc != TURBO_OK) return 0U;
  turbo_vec_destroy(&values);
  map->size -= removed;
  return removed;
}

#define TURBO_MULTI_MAP_DEFINE(name, key_type, value_type)                                            \
  typedef struct {                                                                                   \
    key_type key;                                                                                    \
    value_type value;                                                                                \
  } name##_entry;                                                                                    \
  typedef struct {                                                                                   \
    turbo_multimap_t raw;                                                                           \
  } name;                                                                                            \
  static inline int name##_init(name *map) {                                                         \
    return turbo_multimap_init(&map->raw, sizeof(key_type), sizeof(value_type), NULL, NULL, NULL);    \
  }                                                                                                 \
  static inline int name##_from(name *map, const name##_entry *entries, size_t count) {              \
    size_t i;                                                                                        \
    int rc;                                                                                          \
    if (!map || (count > 0 && !entries)) return TURBO_EINVAL;                                        \
    rc = name##_init(map);                                                                           \
    if (rc != TURBO_OK) return rc;                                                                   \
    rc = turbo_multimap_reserve(&map->raw, count);                                                   \
    if (rc != TURBO_OK) {                                                                            \
      turbo_multimap_destroy(&map->raw);                                                             \
      return rc;                                                                                     \
    }                                                                                                \
    for (i = 0; i < count; ++i) {                                                                    \
      rc = turbo_multimap_put(&map->raw, &entries[i].key, &entries[i].value);                        \
      if (rc != TURBO_OK) {                                                                          \
        turbo_multimap_destroy(&map->raw);                                                           \
        return rc;                                                                                   \
      }                                                                                              \
    }                                                                                                \
    return TURBO_OK;                                                                                 \
  }                                                                                                  \
  static inline void name##_destroy(name *map) { turbo_multimap_destroy(&map->raw); }                 \
  static inline void name##_clear(name *map) { turbo_multimap_clear(&map->raw); }                     \
  static inline int name##_reserve(name *map, size_t capacity) {                                      \
    return turbo_multimap_reserve(&map->raw, capacity);                                              \
  }                                                                                                 \
  static inline int name##_put(name *map, key_type key, value_type value) {                          \
    return turbo_multimap_put(&map->raw, &key, &value);                                              \
  }                                                                                                 \
  static inline const turbo_vec_t *name##_values_const(const name *map, key_type key) {                \
    return turbo_multimap_get_values_const(&map->raw, &key);                                          \
  }                                                                                                 \
  static inline turbo_vec_t *name##_values(name *map, key_type key) {                                 \
    return turbo_multimap_get_values(&map->raw, &key);                                                \
  }                                                                                                 \
  static inline size_t name##_count(const name *map, key_type key) {                                  \
    return turbo_multimap_key_count(&map->raw, &key);                                                 \
  }                                                                                                 \
  static inline bool name##_remove(name *map, key_type key, value_type *out_value) {                   \
    return turbo_multimap_remove(&map->raw, &key, out_value);                                        \
  }                                                                                                 \
  static inline size_t name##_erase(name *map, key_type key) {                                        \
    return turbo_multimap_erase(&map->raw, &key);                                                    \
  }                                                                                                 \
  static inline size_t name##_size(const name *map) { return turbo_multimap_size(&map->raw); }         \
  static inline size_t name##_capacity(const name *map) { return turbo_multimap_capacity(&map->raw); } \
  static inline bool name##_empty(const name *map) { return turbo_multimap_empty(&map->raw); }         \
  static inline bool name##_contains(const name *map, key_type key) {                                 \
    return turbo_multimap_contains(&map->raw, &key);                                                  \
  }

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MULTIMAP_H */
