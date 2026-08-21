#ifndef TURBO_HASH_SET_H
#define TURBO_HASH_SET_H

#include <turbo/container/set.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef turbo_set_t turbo_hash_set_t;

static inline container_status turbo_hash_set_init(turbo_hash_set_t *set,
                                                    const cmeta_type_desc *key_type,
                                                    size_t entry_limit) {
  return turbo_set_init(set, key_type, entry_limit);
}
static inline container_status turbo_hash_set_init_bytes(turbo_hash_set_t *set, size_t key_size,
                                                         size_t key_align, size_t entry_limit,
                                                         turbo_hash_fn hash,
                                                         turbo_hash_equal_fn equal, void *ctx) {
  return turbo_set_init_bytes(set, key_size, key_align, entry_limit, hash, equal, ctx);
}
static inline container_status turbo_hash_set_from_array(turbo_hash_set_t *set, const void *keys,
                                                         size_t count, const cmeta_type_desc *type,
                                                         size_t entry_limit) {
  return turbo_set_from_array(set, keys, count, type, entry_limit);
}
static inline container_status turbo_hash_set_from_array_bytes(turbo_hash_set_t *set,
    const void *keys, size_t count, size_t key_size, size_t key_align, size_t entry_limit,
    turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx) {
  return turbo_set_from_array_bytes(set, keys, count, key_size, key_align, entry_limit,
                                    hash, equal, ctx);
}
static inline void turbo_hash_set_destroy(turbo_hash_set_t *set) { turbo_set_destroy(set); }
static inline void turbo_hash_set_clear(turbo_hash_set_t *set) { turbo_set_clear(set); }
static inline container_status turbo_hash_set_reserve(turbo_hash_set_t *set, size_t entries) { return turbo_set_reserve(set, entries); }
static inline container_status turbo_hash_set_add(turbo_hash_set_t *set, const void *key) { return turbo_set_add(set, key); }
static inline bool turbo_hash_set_contains(const turbo_hash_set_t *set, const void *key) { return turbo_set_contains(set, key); }
static inline container_status turbo_hash_set_remove(turbo_hash_set_t *set, const void *key) { return turbo_set_remove(set, key); }
static inline size_t turbo_hash_set_size(const turbo_hash_set_t *set) { return turbo_set_size(set); }
static inline size_t turbo_hash_set_capacity(const turbo_hash_set_t *set) { return turbo_set_capacity(set); }
static inline uint64_t turbo_hash_set_generation(const turbo_hash_set_t *set) { return turbo_set_generation(set); }
static inline bool turbo_hash_set_empty(const turbo_hash_set_t *set) { return turbo_set_empty(set); }
static inline const void *turbo_hash_set_key_at(const turbo_hash_set_t *set, size_t slot) { return turbo_set_key_at(set, slot); }

#define TURBO_HASH_SET_DEFINE(name, key_type) \
  CMETA_CONTAINER1_DEFINE(name, key_type, turbo_hash_set_t, turbo_hash_set, CONTAINER_OK, _, TURBO_META_HASH_SET_METHODS) \
  CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name, key_type, turbo_hash_set, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif
#endif /* TURBO_HASH_SET_H */
