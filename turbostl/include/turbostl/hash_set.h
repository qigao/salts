#ifndef TURBO_HASH_SET_H
#define TURBO_HASH_SET_H

#include <turbostl/hash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HashSet is an independent hash-backed type. Its table owns keys while an
 * internal byte value only marks presence; it never aliases ordered Set. */
typedef struct hash_set {
  hash_map_t table;
} hash_set_t;

turbostl_status hash_set_init(
    hash_set_t *set, const cmeta_type_desc *key_type,
    size_t entry_limit);
turbostl_status hash_set_init_bytes(
    hash_set_t *set, size_t key_size, size_t key_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal,
    void *ctx);
turbostl_status hash_set_from_array(
    hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t entry_limit);
turbostl_status hash_set_from_array_bytes(
    hash_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t entry_limit, turbo_hash_fn hash,
    turbo_hash_equal_fn equal, void *ctx);
void hash_set_destroy(hash_set_t *set);
void hash_set_clear(hash_set_t *set);
turbostl_status hash_set_reserve(
    hash_set_t *set, size_t min_entries);
turbostl_status hash_set_add(hash_set_t *set,
                                                   const void *key);
bool hash_set_contains(const hash_set_t *set,
                                            const void *key);
turbostl_status hash_set_remove(hash_set_t *set,
                                                      const void *key);
size_t hash_set_size(const hash_set_t *set);
/* capacity is the internal bucket count; entry_limit is the hard live-key
 * bound supplied at initialization. */
size_t hash_set_capacity(const hash_set_t *set);
size_t hash_set_entry_limit(const hash_set_t *set);
uint64_t hash_set_generation(const hash_set_t *set);
bool hash_set_empty(const hash_set_t *set);
const void *hash_set_key_at(const hash_set_t *set,
                                                size_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HASH_SET_H */
