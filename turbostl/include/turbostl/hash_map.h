#ifndef TURBO_HASH_H
#define TURBO_HASH_H

#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cmeta/cmeta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*turbo_hash_fn)(const void *key, size_t key_size, void *ctx);
typedef bool (*turbo_hash_equal_fn)(const void *left, const void *right, size_t key_size,
                                    void *ctx);

typedef struct {
  /* One checked allocation is partitioned into four bucket-indexed arrays:
   * states/hashes metadata plus contiguous aligned key/value payload arrays. */
  uint8_t *states;
  size_t *hashes;
  unsigned char *keys;
  unsigned char *values;
  size_t size;
  size_t capacity;
  size_t tombstones;
  size_t key_size;
  size_t key_stride;
  size_t key_align;
  size_t value_size;
  size_t value_stride;
  size_t value_align;
  size_t entry_limit;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  turbo_hash_fn hash;
  turbo_hash_equal_fn equal;
  void *ctx;
  uint64_t generation;
  bool initialized;
} hash_map_t;

size_t turbo_hash_bytes(const void *key, size_t key_size, void *ctx);
bool turbo_hash_key_equal(const void *left, const void *right, size_t key_size,
                                        void *ctx);

/* HashMap is an independent open-addressed hash table, not an ordered tree.
 * Stored type descriptors, raw hash/equality callbacks, and ctx are borrowed;
 * the caller keeps them valid until destroy. Handles must first be initialized with `{0}`. Typed keys require equal,
 * hash, copy, move, and destroy traits; typed values require copy, move, and
 * destroy traits. A destroyed handle may be reused. */
turbostl_status hash_map_init(hash_map_t *map,
                                                   const cmeta_type_desc *key_type,
                                                   const cmeta_type_desc *value_type,
                                                   size_t entry_limit);
/* Raw-byte maps are explicitly trivial: callers must supply key/value size and
 * alignment plus non-NULL hash and equality callbacks. */
turbostl_status hash_map_init_bytes(hash_map_t *map,
                                                         size_t key_size, size_t key_align,
                                                         size_t value_size, size_t value_align,
                                                         size_t entry_limit, turbo_hash_fn hash,
                                                         turbo_hash_equal_fn equal, void *ctx);
turbostl_status hash_map_from_arrays(
    hash_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type, size_t entry_limit);
turbostl_status hash_map_from_arrays_bytes(
    hash_map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx);
void hash_map_destroy(hash_map_t *map);
void hash_map_clear(hash_map_t *map);
/* min_entries is a live-entry request, never a bucket count. entry_limit is a
 * hard live-entry limit; capacity() reports internal bucket slots. */
turbostl_status hash_map_reserve(hash_map_t *map,
                                                      size_t min_entries);
turbostl_status hash_map_put(hash_map_t *map, const void *key,
                                                  const void *value);
/* Returned pointers are borrowed and become invalid after any successful
 * reserve, put, remove, clear, or destroy on this map. */
void *hash_map_get(hash_map_t *map, const void *key);
const void *hash_map_get_const(const hash_map_t *map, const void *key);
bool hash_map_contains(const hash_map_t *map, const void *key);
/* out_value, when non-NULL, is sufficiently aligned uninitialized storage.
 * Success transfers the value there; NULL destroys it. Failures leave output
 * and the map unchanged. */
turbostl_status hash_map_remove(hash_map_t *map, const void *key,
                                                     void *out_value);
size_t hash_map_size(const hash_map_t *map);
size_t hash_map_capacity(const hash_map_t *map);
size_t hash_map_entry_limit(const hash_map_t *map);
uint64_t hash_map_generation(const hash_map_t *map);
bool hash_map_empty(const hash_map_t *map);
const void *hash_map_key_at(const hash_map_t *map, size_t slot);
static inline const void *hash_map_key_at_const(const hash_map_t *map,
                                                       size_t slot) {
  return hash_map_key_at(map, slot);
}
void *hash_map_value_at(hash_map_t *map, size_t slot);
const void *hash_map_value_at_const(const hash_map_t *map,
                                                        size_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HASH_H */
