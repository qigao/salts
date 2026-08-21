#ifndef TURBO_HASH_H
#define TURBO_HASH_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "turbo_container_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*turbo_hash_fn)(const void *key, size_t key_size, void *ctx);
typedef bool (*turbo_hash_equal_fn)(const void *left, const void *right, size_t key_size,
                                    void *ctx);

typedef struct {
  uint8_t *states;
  size_t *hashes;
  unsigned char *keys;
  unsigned char *values;
  size_t size;
  size_t capacity;
  size_t tombstones;
  size_t key_size;
  size_t value_size;
  turbo_hash_fn hash;
  turbo_hash_equal_fn equal;
  void *ctx;
} turbo_hash_map_t;

CXX_C_API size_t turbo_hash_bytes(const void *key, size_t key_size, void *ctx);
CXX_C_API bool turbo_hash_key_equal(const void *left, const void *right, size_t key_size,
                                    void *ctx);

CXX_C_API int turbo_hash_map_init(turbo_hash_map_t *map, size_t key_size, size_t value_size,
                                  turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx);
/** Initialize a map by copying corresponding elements from parallel key/value arrays. */
CXX_C_API int turbo_hash_map_from_arrays(turbo_hash_map_t *map, const void *keys,
                                         const void *values, size_t count, size_t key_size,
                                         size_t value_size, turbo_hash_fn hash,
                                         turbo_hash_equal_fn equal, void *ctx);
CXX_C_API void turbo_hash_map_destroy(turbo_hash_map_t *map);
CXX_C_API void turbo_hash_map_clear(turbo_hash_map_t *map);
CXX_C_API int turbo_hash_map_reserve(turbo_hash_map_t *map, size_t min_capacity);
CXX_C_API int turbo_hash_map_put(turbo_hash_map_t *map, const void *key, const void *value);
CXX_C_API void *turbo_hash_map_get(turbo_hash_map_t *map, const void *key);
CXX_C_API const void *turbo_hash_map_get_const(const turbo_hash_map_t *map, const void *key);
CXX_C_API bool turbo_hash_map_contains(const turbo_hash_map_t *map, const void *key);
CXX_C_API int turbo_hash_map_remove(turbo_hash_map_t *map, const void *key, void *out_value);
CXX_C_API size_t turbo_hash_map_size(const turbo_hash_map_t *map);
CXX_C_API size_t turbo_hash_map_capacity(const turbo_hash_map_t *map);
CXX_C_API bool turbo_hash_map_empty(const turbo_hash_map_t *map);
CXX_C_API const void *turbo_hash_map_key_at(const turbo_hash_map_t *map, size_t slot);
CXX_C_API void *turbo_hash_map_value_at(turbo_hash_map_t *map, size_t slot);
CXX_C_API const void *turbo_hash_map_value_at_const(const turbo_hash_map_t *map, size_t slot);

#define TURBO_HASH_MAP_DEFINE(name, key_type, value_type) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, turbo_hash_map_t, turbo_hash_map, TURBO_OK, _, TURBO_META_HASH_MAP_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, turbo_hash_map, key_at, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HASH_H */
