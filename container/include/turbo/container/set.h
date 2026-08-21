#ifndef TURBO_SET_H
#define TURBO_SET_H

#include <turbo/container/hash_map.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_hash_map_t map;
} turbo_set_t;

CONTAINER_API int turbo_set_init(turbo_set_t *set, size_t key_size, turbo_hash_fn hash,
                             turbo_hash_equal_fn equal, void *ctx);
/** Initialize a set by copying count keys from a contiguous array. */
CONTAINER_API int turbo_set_from_array(turbo_set_t *set, const void *keys, size_t count,
                                   size_t key_size, turbo_hash_fn hash,
                                   turbo_hash_equal_fn equal, void *ctx);
CONTAINER_API void turbo_set_destroy(turbo_set_t *set);
CONTAINER_API void turbo_set_clear(turbo_set_t *set);
CONTAINER_API int turbo_set_reserve(turbo_set_t *set, size_t min_capacity);
CONTAINER_API int turbo_set_add(turbo_set_t *set, const void *key);
CONTAINER_API bool turbo_set_contains(const turbo_set_t *set, const void *key);
CONTAINER_API int turbo_set_remove(turbo_set_t *set, const void *key);
CONTAINER_API size_t turbo_set_size(const turbo_set_t *set);
CONTAINER_API size_t turbo_set_capacity(const turbo_set_t *set);
CONTAINER_API bool turbo_set_empty(const turbo_set_t *set);
CONTAINER_API const void *turbo_set_key_at(const turbo_set_t *set, size_t slot);

#define TURBO_SET_DEFINE(name, key_type) \
  CMETA_CONTAINER1_DEFINE(name, key_type, turbo_set_t, turbo_set, CONTAINER_OK, _, TURBO_META_SET_METHODS) \
  CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name, key_type, turbo_set, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SET_H */
