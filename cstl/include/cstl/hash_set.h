#ifndef CSTL_HASH_SET_H
#define CSTL_HASH_SET_H

#include <cstl/hash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HashSet is an independent hash-backed type. The element binding lives on
 * the outer handle; the internal hash map owns keys and a byte presence value. */
typedef struct hash_set {
  cmeta_container_header cmeta;
  const cmeta_type_desc *element_type;
  hash_map_t table;
} hash_set_t;

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
stl_status hash_set_raw_init(hash_set_t *set,
                             const cmeta_type_desc *key_type,
                             size_t entry_limit);
stl_status hash_set_raw_from_array(
    hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t entry_limit);
void hash_set_raw_destroy_storage(hash_set_t *set);

stl_status hash_set_init_bytes(hash_set_t *set, size_t key_size,
                               size_t key_align, size_t entry_limit,
                               hash_fn hash, hash_equal_fn equal, void *ctx);
stl_status hash_set_from_array_bytes(
    hash_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t entry_limit, hash_fn hash,
    hash_equal_fn equal, void *ctx);

static inline stl_status hash_set_init(hash_set_t *set, size_t entry_limit) {
  if (set == NULL || set->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  return hash_set_raw_init(set, set->element_type, entry_limit);
}

static inline stl_status hash_set_from_array(hash_set_t *set,
                                             const void *keys, size_t count,
                                             size_t entry_limit) {
  const cmeta_type_desc *type;
  stl_status status;
  if (set == NULL || set->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  type = set->element_type;
  status = hash_set_raw_from_array(set, keys, count, type, entry_limit);
  set->element_type = type;
  return status;
}

static inline void hash_set_destroy(hash_set_t *set) {
  if (set != NULL)
    hash_set_raw_destroy_storage(set);
}

void hash_set_clear(hash_set_t *set);
stl_status hash_set_reserve(hash_set_t *set, size_t min_entries);
stl_status hash_set_add(hash_set_t *set, const void *key);
bool hash_set_contains(const hash_set_t *set, const void *key);
stl_status hash_set_remove(hash_set_t *set, const void *key);
size_t hash_set_size(const hash_set_t *set);
size_t hash_set_capacity(const hash_set_t *set);
size_t hash_set_entry_limit(const hash_set_t *set);
uint64_t hash_set_generation(const hash_set_t *set);
bool hash_set_empty(const hash_set_t *set);
const void *hash_set_key_at(const hash_set_t *set, size_t slot);


#ifdef __cplusplus
}
#endif

#endif /* CSTL_HASH_SET_H */
