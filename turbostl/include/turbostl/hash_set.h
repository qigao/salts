#ifndef TURBO_HASH_SET_H
#define TURBO_HASH_SET_H

#include <turbostl/hash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HashSet is an independent hash-backed type. Its table owns keys while an
 * internal byte value only marks presence; it never aliases ordered Set. */
typedef struct turbo_hash_set {
  turbo_hash_map_t table;
} turbo_hash_set_t;

turbo_stl_status turbo_hash_set_init(
    turbo_hash_set_t *set, const cmeta_type_desc *key_type,
    size_t entry_limit);
turbo_stl_status turbo_hash_set_init_bytes(
    turbo_hash_set_t *set, size_t key_size, size_t key_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal,
    void *ctx);
turbo_stl_status turbo_hash_set_from_array(
    turbo_hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t entry_limit);
turbo_stl_status turbo_hash_set_from_array_bytes(
    turbo_hash_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t entry_limit, turbo_hash_fn hash,
    turbo_hash_equal_fn equal, void *ctx);
void turbo_hash_set_destroy(turbo_hash_set_t *set);
void turbo_hash_set_clear(turbo_hash_set_t *set);
turbo_stl_status turbo_hash_set_reserve(
    turbo_hash_set_t *set, size_t min_entries);
turbo_stl_status turbo_hash_set_add(turbo_hash_set_t *set,
                                                   const void *key);
bool turbo_hash_set_contains(const turbo_hash_set_t *set,
                                            const void *key);
turbo_stl_status turbo_hash_set_remove(turbo_hash_set_t *set,
                                                      const void *key);
size_t turbo_hash_set_size(const turbo_hash_set_t *set);
/* capacity is the internal bucket count; entry_limit is the hard live-key
 * bound supplied at initialization. */
size_t turbo_hash_set_capacity(const turbo_hash_set_t *set);
size_t turbo_hash_set_entry_limit(const turbo_hash_set_t *set);
uint64_t turbo_hash_set_generation(const turbo_hash_set_t *set);
bool turbo_hash_set_empty(const turbo_hash_set_t *set);
const void *turbo_hash_set_key_at(const turbo_hash_set_t *set,
                                                size_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HASH_SET_H */
