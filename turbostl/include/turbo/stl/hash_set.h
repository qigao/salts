#ifndef TURBO_HASH_SET_H
#define TURBO_HASH_SET_H

#include <turbo/stl/hash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HashSet is an independent hash-backed type. Its table owns keys while an
 * internal byte value only marks presence; it never aliases ordered Set. */
typedef struct turbo_hash_set {
  turbo_hash_map_t table;
} turbo_hash_set_t;

TURBO_STL_API turbo_stl_status turbo_hash_set_init(
    turbo_hash_set_t *set, const cmeta_type_desc *key_type,
    size_t entry_limit);
TURBO_STL_API turbo_stl_status turbo_hash_set_init_bytes(
    turbo_hash_set_t *set, size_t key_size, size_t key_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal,
    void *ctx);
TURBO_STL_API turbo_stl_status turbo_hash_set_from_array(
    turbo_hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t entry_limit);
TURBO_STL_API turbo_stl_status turbo_hash_set_from_array_bytes(
    turbo_hash_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t entry_limit, turbo_hash_fn hash,
    turbo_hash_equal_fn equal, void *ctx);
TURBO_STL_API void turbo_hash_set_destroy(turbo_hash_set_t *set);
TURBO_STL_API void turbo_hash_set_clear(turbo_hash_set_t *set);
TURBO_STL_API turbo_stl_status turbo_hash_set_reserve(
    turbo_hash_set_t *set, size_t min_entries);
TURBO_STL_API turbo_stl_status turbo_hash_set_add(turbo_hash_set_t *set,
                                                   const void *key);
TURBO_STL_API bool turbo_hash_set_contains(const turbo_hash_set_t *set,
                                            const void *key);
TURBO_STL_API turbo_stl_status turbo_hash_set_remove(turbo_hash_set_t *set,
                                                      const void *key);
TURBO_STL_API size_t turbo_hash_set_size(const turbo_hash_set_t *set);
/* capacity is the internal bucket count; entry_limit is the hard live-key
 * bound supplied at initialization. */
TURBO_STL_API size_t turbo_hash_set_capacity(const turbo_hash_set_t *set);
TURBO_STL_API size_t turbo_hash_set_entry_limit(const turbo_hash_set_t *set);
TURBO_STL_API uint64_t turbo_hash_set_generation(const turbo_hash_set_t *set);
TURBO_STL_API bool turbo_hash_set_empty(const turbo_hash_set_t *set);
TURBO_STL_API const void *turbo_hash_set_key_at(const turbo_hash_set_t *set,
                                                size_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HASH_SET_H */
