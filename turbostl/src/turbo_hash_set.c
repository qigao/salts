#include <turbo/stl/hash_set.h>

#include <stdint.h>

turbo_stl_status turbo_hash_set_init(turbo_hash_set_t *set,
                                     const cmeta_type_desc *key_type,
                                     size_t entry_limit) {
  if (set == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_hash_map_init(&set->table, key_type, &cmeta_type_bool,
                             entry_limit);
}

turbo_stl_status turbo_hash_set_init_bytes(
    turbo_hash_set_t *set, size_t key_size, size_t key_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal,
    void *ctx) {
  if (set == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_hash_map_init_bytes(&set->table, key_size, key_align,
                                   sizeof(uint8_t), _Alignof(uint8_t),
                                   entry_limit, hash, equal, ctx);
}

static turbo_stl_status turbo_hash_set_from_common(
    turbo_hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t key_size, size_t key_align,
    size_t entry_limit, turbo_hash_fn hash, turbo_hash_equal_fn equal,
    void *ctx) {
  turbo_hash_set_t temporary = {0};
  turbo_stl_status status;
  size_t index;

  if (set == NULL || set->table.initialized)
    return TURBO_STL_INVALID_ARGUMENT;
  if (count != 0u && keys == NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = key_type != NULL
               ? turbo_hash_set_init(&temporary, key_type, entry_limit)
               : turbo_hash_set_init_bytes(&temporary, key_size, key_align,
                                           entry_limit, hash, equal, ctx);
  if (status != TURBO_STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_hash_set_add(
        &temporary, (const unsigned char *)keys + index * key_size);
    if (status != TURBO_STL_OK) {
      turbo_hash_set_destroy(&temporary);
      return status;
    }
  }
  temporary.table.generation = set->table.generation + UINT64_C(1);
  *set = temporary;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_hash_set_from_array(
    turbo_hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t entry_limit) {
  if (key_type == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_hash_set_from_common(set, keys, count, key_type,
                                    key_type->size, key_type->align,
                                    entry_limit, NULL, NULL, NULL);
}

turbo_stl_status turbo_hash_set_from_array_bytes(
    turbo_hash_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t entry_limit, turbo_hash_fn hash,
    turbo_hash_equal_fn equal, void *ctx) {
  return turbo_hash_set_from_common(set, keys, count, NULL, key_size,
                                    key_align, entry_limit, hash, equal, ctx);
}

void turbo_hash_set_destroy(turbo_hash_set_t *set) {
  if (set != NULL) turbo_hash_map_destroy(&set->table);
}

void turbo_hash_set_clear(turbo_hash_set_t *set) {
  if (set != NULL) turbo_hash_map_clear(&set->table);
}

turbo_stl_status turbo_hash_set_reserve(turbo_hash_set_t *set,
                                        size_t min_entries) {
  return set == NULL ? TURBO_STL_INVALID_ARGUMENT
                     : turbo_hash_map_reserve(&set->table, min_entries);
}

turbo_stl_status turbo_hash_set_add(turbo_hash_set_t *set,
                                    const void *key) {
  uint8_t present = 1u;
  if (set == NULL) return TURBO_STL_INVALID_ARGUMENT;
  if (turbo_hash_map_contains(&set->table, key)) return TURBO_STL_OK;
  return turbo_hash_map_put(&set->table, key, &present);
}

bool turbo_hash_set_contains(const turbo_hash_set_t *set, const void *key) {
  return set != NULL && turbo_hash_map_contains(&set->table, key);
}

turbo_stl_status turbo_hash_set_remove(turbo_hash_set_t *set,
                                       const void *key) {
  return set == NULL ? TURBO_STL_INVALID_ARGUMENT
                     : turbo_hash_map_remove(&set->table, key, NULL);
}

size_t turbo_hash_set_size(const turbo_hash_set_t *set) {
  return set == NULL ? 0u : turbo_hash_map_size(&set->table);
}

size_t turbo_hash_set_capacity(const turbo_hash_set_t *set) {
  return set == NULL ? 0u : turbo_hash_map_capacity(&set->table);
}

size_t turbo_hash_set_entry_limit(const turbo_hash_set_t *set) {
  return set == NULL ? 0u : turbo_hash_map_entry_limit(&set->table);
}

uint64_t turbo_hash_set_generation(const turbo_hash_set_t *set) {
  return set == NULL ? UINT64_C(0)
                     : turbo_hash_map_generation(&set->table);
}

bool turbo_hash_set_empty(const turbo_hash_set_t *set) {
  return turbo_hash_set_size(set) == 0u;
}

const void *turbo_hash_set_key_at(const turbo_hash_set_t *set, size_t slot) {
  return set == NULL ? NULL : turbo_hash_map_key_at(&set->table, slot);
}
