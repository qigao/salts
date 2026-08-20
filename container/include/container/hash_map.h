#ifndef CONTAINER_HASH_MAP_H
#define CONTAINER_HASH_MAP_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*container_hash_fn)(const void *key, size_t key_size, void *ctx);
typedef bool (*container_hash_equal_fn)(const void *left, const void *right, size_t key_size,
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
  container_hash_fn hash;
  container_hash_equal_fn equal;
  void *ctx;
} container_hash_map_t;

CXX_C_API size_t container_hash_bytes(const void *key, size_t key_size, void *ctx);
CXX_C_API bool container_hash_key_equal(const void *left, const void *right, size_t key_size,
                                    void *ctx);

CXX_C_API int container_hash_map_init(container_hash_map_t *map, size_t key_size, size_t value_size,
                                  container_hash_fn hash, container_hash_equal_fn equal, void *ctx);
/** Initialize a map by copying corresponding elements from parallel key/value arrays. */
CXX_C_API int container_hash_map_from_arrays(container_hash_map_t *map, const void *keys,
                                         const void *values, size_t count, size_t key_size,
                                         size_t value_size, container_hash_fn hash,
                                         container_hash_equal_fn equal, void *ctx);
CXX_C_API void container_hash_map_destroy(container_hash_map_t *map);
CXX_C_API void container_hash_map_clear(container_hash_map_t *map);
CXX_C_API int container_hash_map_reserve(container_hash_map_t *map, size_t min_capacity);
CXX_C_API int container_hash_map_put(container_hash_map_t *map, const void *key, const void *value);
CXX_C_API void *container_hash_map_get(container_hash_map_t *map, const void *key);
CXX_C_API const void *container_hash_map_get_const(const container_hash_map_t *map, const void *key);
CXX_C_API bool container_hash_map_contains(const container_hash_map_t *map, const void *key);
CXX_C_API int container_hash_map_remove(container_hash_map_t *map, const void *key, void *out_value);
CXX_C_API size_t container_hash_map_size(const container_hash_map_t *map);
CXX_C_API size_t container_hash_map_capacity(const container_hash_map_t *map);
CXX_C_API bool container_hash_map_empty(const container_hash_map_t *map);
CXX_C_API const void *container_hash_map_key_at(const container_hash_map_t *map, size_t slot);
CXX_C_API void *container_hash_map_value_at(container_hash_map_t *map, size_t slot);
CXX_C_API const void *container_hash_map_value_at_const(const container_hash_map_t *map, size_t slot);


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_HASH_MAP_H */
