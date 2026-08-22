#ifndef TURBOSTL_HASH_MAP_H
#define TURBOSTL_HASH_MAP_H

#include <turbostl/status.h>

#include <cmeta/range.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*hash_fn)(const void *key, size_t key_size, void *context);
typedef bool (*hash_equal_fn)(const void *left, const void *right,
                              size_t key_size, void *context);

typedef struct hash_map {
  cmeta_container_header cmeta;
  uint8_t *states;
  size_t *hashes;
  unsigned char *keys;
  unsigned char *values;
  size_t size;
  size_t capacity;
  size_t tombstones;
  size_t key_size;
  size_t key_stride;
  size_t key_align;
  size_t value_size;
  size_t value_stride;
  size_t value_align;
  size_t entry_limit;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  hash_fn hash;
  hash_equal_fn equal;
  void *ctx;
  uint64_t generation;
  bool initialized;
} hash_map_t;

size_t hash_bytes(const void *key, size_t key_size, void *context);
bool hash_key_equal(const void *left, const void *right, size_t key_size,
                    void *context);

/* Internal typed bridge for compiled implementation and legacy wrappers. */
stl_status hash_map_raw_init(hash_map_t *map,
                             const cmeta_type_desc *key_type,
                             const cmeta_type_desc *value_type,
                             size_t entry_limit);
stl_status hash_map_raw_from_arrays(
    hash_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
void hash_map_raw_destroy_storage(hash_map_t *map);

stl_status hash_map_init_bytes(hash_map_t *map, size_t key_size,
                               size_t key_align, size_t value_size,
                               size_t value_align, size_t entry_limit,
                               hash_fn hash, hash_equal_fn equal,
                               void *context);
stl_status hash_map_from_arrays_bytes(
    hash_map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, hash_fn hash, hash_equal_fn equal, void *context);

static inline stl_status hash_map_init(hash_map_t *map, size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (map == NULL || map->key_type == NULL || map->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  status = hash_map_raw_init(map, key_type, value_type, entry_limit);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
  return status;
}

static inline stl_status hash_map_from_arrays(
    hash_map_t *map, const void *keys, const void *values, size_t count,
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
  status = hash_map_raw_from_arrays(map, keys, values, count, key_type,
                                    value_type, entry_limit);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
  return status;
}

static inline void hash_map_destroy(hash_map_t *map) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  if (map == NULL)
    return;
  kind = map->cmeta.descriptor;
  key_type = map->key_type;
  value_type = map->value_type;
  hash_map_raw_destroy_storage(map);
  map->cmeta.descriptor = kind;
  map->key_type = key_type;
  map->value_type = value_type;
}

void hash_map_clear(hash_map_t *map);
stl_status hash_map_reserve(hash_map_t *map, size_t min_entries);
stl_status hash_map_put(hash_map_t *map, const void *key, const void *value);
void *hash_map_get(hash_map_t *map, const void *key);
const void *hash_map_get_const(const hash_map_t *map, const void *key);
bool hash_map_contains(const hash_map_t *map, const void *key);
stl_status hash_map_remove(hash_map_t *map, const void *key, void *out_value);
size_t hash_map_size(const hash_map_t *map);
size_t hash_map_capacity(const hash_map_t *map);
size_t hash_map_entry_limit(const hash_map_t *map);
uint64_t hash_map_generation(const hash_map_t *map);
bool hash_map_empty(const hash_map_t *map);
const void *hash_map_key_at(const hash_map_t *map, size_t slot);
static inline const void *hash_map_key_at_const(const hash_map_t *map,
                                                 size_t slot) {
  return hash_map_key_at(map, slot);
}
void *hash_map_value_at(hash_map_t *map, size_t slot);
const void *hash_map_value_at_const(const hash_map_t *map, size_t slot);

/* Temporary repository-migration aliases. */
typedef hash_fn turbo_hash_fn;
typedef hash_equal_fn turbo_hash_equal_fn;
typedef hash_map_t turbo_hash_map_t;
#define turbo_hash_bytes hash_bytes
#define turbo_hash_key_equal hash_key_equal
#define turbo_hash_map_init hash_map_raw_init
#define turbo_hash_map_init_bytes hash_map_init_bytes
#define turbo_hash_map_from_arrays hash_map_raw_from_arrays
#define turbo_hash_map_from_arrays_bytes hash_map_from_arrays_bytes
#define turbo_hash_map_destroy hash_map_raw_destroy_storage
#define turbo_hash_map_clear hash_map_clear
#define turbo_hash_map_reserve hash_map_reserve
#define turbo_hash_map_put hash_map_put
#define turbo_hash_map_get hash_map_get
#define turbo_hash_map_get_const hash_map_get_const
#define turbo_hash_map_contains hash_map_contains
#define turbo_hash_map_remove hash_map_remove
#define turbo_hash_map_size hash_map_size
#define turbo_hash_map_capacity hash_map_capacity
#define turbo_hash_map_entry_limit hash_map_entry_limit
#define turbo_hash_map_generation hash_map_generation
#define turbo_hash_map_empty hash_map_empty
#define turbo_hash_map_key_at hash_map_key_at
#define turbo_hash_map_key_at_const hash_map_key_at_const
#define turbo_hash_map_value_at hash_map_value_at
#define turbo_hash_map_value_at_const hash_map_value_at_const

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_HASH_MAP_H */
