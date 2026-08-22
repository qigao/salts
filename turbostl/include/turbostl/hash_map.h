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
} turbo_hash_map_t;

size_t turbo_hash_bytes(const void *key, size_t key_size, void *ctx);
bool turbo_hash_key_equal(const void *left, const void *right, size_t key_size,
                                        void *ctx);

/* HashMap is an independent open-addressed hash table, not an ordered tree.
 * Stored type descriptors, raw hash/equality callbacks, and ctx are borrowed;
 * the caller keeps them valid until destroy. Handles must first be initialized with `{0}`. Typed keys require equal,
 * hash, copy, move, and destroy traits; typed values require copy, move, and
 * destroy traits. A destroyed handle may be reused. */
turbo_stl_status turbo_hash_map_init(turbo_hash_map_t *map,
                                                   const cmeta_type_desc *key_type,
                                                   const cmeta_type_desc *value_type,
                                                   size_t entry_limit);
/* Raw-byte maps are explicitly trivial: callers must supply key/value size and
 * alignment plus non-NULL hash and equality callbacks. */
turbo_stl_status turbo_hash_map_init_bytes(turbo_hash_map_t *map,
                                                         size_t key_size, size_t key_align,
                                                         size_t value_size, size_t value_align,
                                                         size_t entry_limit, turbo_hash_fn hash,
                                                         turbo_hash_equal_fn equal, void *ctx);
turbo_stl_status turbo_hash_map_from_arrays(
    turbo_hash_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type, size_t entry_limit);
turbo_stl_status turbo_hash_map_from_arrays_bytes(
    turbo_hash_map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal, void *ctx);
void turbo_hash_map_destroy(turbo_hash_map_t *map);
void turbo_hash_map_clear(turbo_hash_map_t *map);
/* min_entries is a live-entry request, never a bucket count. entry_limit is a
 * hard live-entry limit; capacity() reports internal bucket slots. */
turbo_stl_status turbo_hash_map_reserve(turbo_hash_map_t *map,
                                                      size_t min_entries);
turbo_stl_status turbo_hash_map_put(turbo_hash_map_t *map, const void *key,
                                                  const void *value);
/* Returned pointers are borrowed and become invalid after any successful
 * reserve, put, remove, clear, or destroy on this map. */
void *turbo_hash_map_get(turbo_hash_map_t *map, const void *key);
const void *turbo_hash_map_get_const(const turbo_hash_map_t *map, const void *key);
bool turbo_hash_map_contains(const turbo_hash_map_t *map, const void *key);
/* out_value, when non-NULL, is sufficiently aligned uninitialized storage.
 * Success transfers the value there; NULL destroys it. Failures leave output
 * and the map unchanged. */
turbo_stl_status turbo_hash_map_remove(turbo_hash_map_t *map, const void *key,
                                                     void *out_value);
size_t turbo_hash_map_size(const turbo_hash_map_t *map);
size_t turbo_hash_map_capacity(const turbo_hash_map_t *map);
size_t turbo_hash_map_entry_limit(const turbo_hash_map_t *map);
uint64_t turbo_hash_map_generation(const turbo_hash_map_t *map);
bool turbo_hash_map_empty(const turbo_hash_map_t *map);
const void *turbo_hash_map_key_at(const turbo_hash_map_t *map, size_t slot);
static inline const void *turbo_hash_map_key_at_const(const turbo_hash_map_t *map,
                                                       size_t slot) {
  return turbo_hash_map_key_at(map, slot);
}
void *turbo_hash_map_value_at(turbo_hash_map_t *map, size_t slot);
const void *turbo_hash_map_value_at_const(const turbo_hash_map_t *map,
                                                        size_t slot);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HASH_H */
