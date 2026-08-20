#ifndef CONTAINER_HASH_SET_H
#define CONTAINER_HASH_SET_H

#include <container/set.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef container_set_t container_hash_set_t;

static inline int container_hash_set_init(container_hash_set_t *set, size_t key_size,
                                      container_hash_fn hash, container_hash_equal_fn equal, void *ctx) {
  return container_set_init(set, key_size, hash, equal, ctx);
}
static inline int container_hash_set_from_array(container_hash_set_t *set, const void *keys,
                                            size_t count, size_t key_size,
                                            container_hash_fn hash, container_hash_equal_fn equal, void *ctx) {
  return container_set_from_array(set, keys, count, key_size, hash, equal, ctx);
}
static inline void container_hash_set_destroy(container_hash_set_t *set) { container_set_destroy(set); }
static inline void container_hash_set_clear(container_hash_set_t *set) { container_set_clear(set); }
static inline int container_hash_set_reserve(container_hash_set_t *set, size_t capacity) { return container_set_reserve(set, capacity); }
static inline int container_hash_set_add(container_hash_set_t *set, const void *key) { return container_set_add(set, key); }
static inline bool container_hash_set_contains(const container_hash_set_t *set, const void *key) { return container_set_contains(set, key); }
static inline int container_hash_set_remove(container_hash_set_t *set, const void *key) { return container_set_remove(set, key); }
static inline size_t container_hash_set_size(const container_hash_set_t *set) { return container_set_size(set); }
static inline size_t container_hash_set_capacity(const container_hash_set_t *set) { return container_set_capacity(set); }
static inline bool container_hash_set_empty(const container_hash_set_t *set) { return container_set_empty(set); }
static inline const void *container_hash_set_key_at(const container_hash_set_t *set, size_t slot) { return container_set_key_at(set, slot); }


#ifdef __cplusplus
}
#endif
#endif /* CONTAINER_HASH_SET_H */
