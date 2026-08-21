#ifndef TURBO_SET_H
#define TURBO_SET_H

#include <turbo/container/hash_map.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { turbo_hash_map_t map; } turbo_set_t;

/* The key descriptor, or raw hash/equality callbacks and ctx, are borrowed
 * until destroy. capacity() reports bucket slots; entry_limit is a live-key
 * hard limit. Returned key pointers are borrowed and invalidate after every
 * successful reserve, add, remove, clear, or destroy. */
CONTAINER_API container_status turbo_set_init(turbo_set_t *set,
                                              const cmeta_type_desc *key_type,
                                              size_t entry_limit);
CONTAINER_API container_status turbo_set_init_bytes(turbo_set_t *set, size_t key_size,
                                                    size_t key_align, size_t entry_limit,
                                                    turbo_hash_fn hash,
                                                    turbo_hash_equal_fn equal, void *ctx);
CONTAINER_API container_status turbo_set_from_array(turbo_set_t *set, const void *keys,
                                                    size_t count,
                                                    const cmeta_type_desc *key_type,
                                                    size_t entry_limit);
CONTAINER_API container_status turbo_set_from_array_bytes(turbo_set_t *set, const void *keys,
                                                          size_t count, size_t key_size,
                                                          size_t key_align, size_t entry_limit,
                                                          turbo_hash_fn hash,
                                                          turbo_hash_equal_fn equal, void *ctx);
CONTAINER_API void turbo_set_destroy(turbo_set_t *set);
CONTAINER_API void turbo_set_clear(turbo_set_t *set);
CONTAINER_API container_status turbo_set_reserve(turbo_set_t *set, size_t min_entries);
CONTAINER_API container_status turbo_set_add(turbo_set_t *set, const void *key);
CONTAINER_API bool turbo_set_contains(const turbo_set_t *set, const void *key);
CONTAINER_API container_status turbo_set_remove(turbo_set_t *set, const void *key);
CONTAINER_API size_t turbo_set_size(const turbo_set_t *set);
CONTAINER_API size_t turbo_set_capacity(const turbo_set_t *set);
CONTAINER_API size_t turbo_set_entry_limit(const turbo_set_t *set);
CONTAINER_API uint64_t turbo_set_generation(const turbo_set_t *set);
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
