#ifndef CONTAINER_SET_H
#define CONTAINER_SET_H

#include <container/hash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  container_hash_map_t map;
} container_set_t;

CXX_C_API int container_set_init(container_set_t *set, size_t key_size, container_hash_fn hash,
                             container_hash_equal_fn equal, void *ctx);
/** Initialize a set by copying count keys from a contiguous array. */
CXX_C_API int container_set_from_array(container_set_t *set, const void *keys, size_t count,
                                   size_t key_size, container_hash_fn hash,
                                   container_hash_equal_fn equal, void *ctx);
CXX_C_API void container_set_destroy(container_set_t *set);
CXX_C_API void container_set_clear(container_set_t *set);
CXX_C_API int container_set_reserve(container_set_t *set, size_t min_capacity);
CXX_C_API int container_set_add(container_set_t *set, const void *key);
CXX_C_API bool container_set_contains(const container_set_t *set, const void *key);
CXX_C_API int container_set_remove(container_set_t *set, const void *key);
CXX_C_API size_t container_set_size(const container_set_t *set);
CXX_C_API size_t container_set_capacity(const container_set_t *set);
CXX_C_API bool container_set_empty(const container_set_t *set);
CXX_C_API const void *container_set_key_at(const container_set_t *set, size_t slot);


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_SET_H */
