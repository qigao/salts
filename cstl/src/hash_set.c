#include <cstl/hash_set.h>
#include <cstl/detail/instance_meta.h>

#include <stdint.h>

static bool hash_set_is_typed_semantic_zero(const hash_set_t *set) {
  const hash_map_t *table;
  if (set == NULL || set->cmeta.descriptor != &stl_hash_set_container_desc ||
      set->element_type == NULL || !cmeta_type_desc_valid(set->element_type))
    return false;
  table = &set->table;
  return !table->initialized && table->states == NULL && table->hashes == NULL &&
         table->keys == NULL && table->values == NULL &&
         table->size == 0u && table->capacity == 0u && table->tombstones == 0u &&
         table->key_size == 0u && table->key_stride == 0u && table->key_align == 0u &&
         table->value_size == 0u && table->value_stride == 0u &&
         table->value_align == 0u && table->entry_limit == 0u &&
         table->cmeta.descriptor == NULL && table->key_type == NULL &&
         table->value_type == NULL && table->hash == NULL &&
         table->equal == NULL && table->ctx == NULL;
}

static stl_status hash_set_materialize_for_mutation(
    hash_set_t *set, hash_set_t *zero_snapshot, bool *materialized) {
  stl_status status;
  if (zero_snapshot == NULL || materialized == NULL)
    return STL_INVALID_ARGUMENT;
  *materialized = false;
  if (set != NULL && set->table.initialized) return STL_OK;
  if (!hash_set_is_typed_semantic_zero(set)) return STL_INVALID_ARGUMENT;

  *zero_snapshot = *set;
  status = hash_set_raw_init(set, zero_snapshot->element_type, SIZE_MAX);
  if (status != STL_OK) {
    *set = *zero_snapshot;
    return status;
  }
  set->table.generation = zero_snapshot->table.generation;
  *materialized = true;
  return STL_OK;
}

static void hash_set_rollback_materialization(
    hash_set_t *set, const hash_set_t *zero_snapshot, bool materialized) {
  if (!materialized) return;
  hash_set_raw_destroy_storage(set);
  *set = *zero_snapshot;
}

stl_status hash_set_raw_init(hash_set_t *set,
                                     const cmeta_type_desc *key_type,
                                     size_t entry_limit) {
  if (set == NULL) return STL_INVALID_ARGUMENT;
  return hash_map_raw_init(&set->table, key_type, &cmeta_type_bool,
                             entry_limit);
}

stl_status hash_set_init_bytes(
    hash_set_t *set, size_t key_size, size_t key_align,
    size_t entry_limit, hash_fn hash, hash_equal_fn equal,
    void *ctx) {
  if (set == NULL) return STL_INVALID_ARGUMENT;
  return hash_map_init_bytes(&set->table, key_size, key_align,
                                   sizeof(uint8_t), _Alignof(uint8_t),
                                   entry_limit, hash, equal, ctx);
}

static stl_status hash_set_from_common(
    hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t key_size, size_t key_align,
    size_t entry_limit, hash_fn hash, hash_equal_fn equal,
    void *ctx) {
  hash_set_t temporary = {0};
  stl_status status;
  size_t index;

  if (set == NULL || set->table.initialized)
    return STL_INVALID_ARGUMENT;
  if (count != 0u && keys == NULL) return STL_INVALID_ARGUMENT;
  status = key_type != NULL
               ? hash_set_raw_init(&temporary, key_type, entry_limit)
               : hash_set_init_bytes(&temporary, key_size, key_align,
                                           entry_limit, hash, equal, ctx);
  if (status != STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = hash_set_add(
        &temporary, (const unsigned char *)keys + index * key_size);
    if (status != STL_OK) {
      hash_set_raw_destroy_storage(&temporary);
      return status;
    }
  }
  temporary.table.generation = set->table.generation + UINT64_C(1);
  *set = temporary;
  return STL_OK;
}

stl_status hash_set_raw_from_array(
    hash_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t entry_limit) {
  if (key_type == NULL) return STL_INVALID_ARGUMENT;
  return hash_set_from_common(set, keys, count, key_type,
                                    key_type->size, key_type->align,
                                    entry_limit, NULL, NULL, NULL);
}

stl_status hash_set_from_array_bytes(
    hash_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t entry_limit, hash_fn hash,
    hash_equal_fn equal, void *ctx) {
  return hash_set_from_common(set, keys, count, NULL, key_size,
                                    key_align, entry_limit, hash, equal, ctx);
}

void hash_set_raw_destroy_storage(hash_set_t *set) {
  if (set != NULL) hash_map_raw_destroy_storage(&set->table);
}

void hash_set_clear(hash_set_t *set) {
  if (set != NULL) hash_map_clear(&set->table);
}

stl_status hash_set_reserve(hash_set_t *set,
                                        size_t min_entries) {
  hash_set_t zero_snapshot = {0};
  bool materialized = false;
  stl_status status;
  if (set == NULL) return STL_INVALID_ARGUMENT;
  if (!set->table.initialized && hash_set_is_typed_semantic_zero(set) &&
      min_entries == 0u)
    return STL_OK;
  status = hash_set_materialize_for_mutation(set, &zero_snapshot, &materialized);
  if (status != STL_OK) return status;
  status = hash_map_reserve(&set->table, min_entries);
  if (status != STL_OK)
    hash_set_rollback_materialization(set, &zero_snapshot, materialized);
  return status;
}

stl_status hash_set_add(hash_set_t *set,
                                    const void *key) {
  hash_set_t zero_snapshot = {0};
  bool materialized = false;
  uint8_t present = 1u;
  stl_status status;
  if (set == NULL || key == NULL) return STL_INVALID_ARGUMENT;
  status = hash_set_materialize_for_mutation(set, &zero_snapshot, &materialized);
  if (status != STL_OK) return status;
  if (hash_map_contains(&set->table, key)) return STL_OK;
  status = hash_map_put(&set->table, key, &present);
  if (status != STL_OK)
    hash_set_rollback_materialization(set, &zero_snapshot, materialized);
  return status;
}

bool hash_set_contains(const hash_set_t *set, const void *key) {
  return set != NULL && hash_map_contains(&set->table, key);
}

stl_status hash_set_remove(hash_set_t *set,
                                       const void *key) {
  return set == NULL ? STL_INVALID_ARGUMENT
                     : hash_map_remove(&set->table, key, NULL);
}

size_t hash_set_size(const hash_set_t *set) {
  return set == NULL ? 0u : hash_map_size(&set->table);
}

size_t hash_set_capacity(const hash_set_t *set) {
  return set == NULL ? 0u : hash_map_capacity(&set->table);
}

size_t hash_set_entry_limit(const hash_set_t *set) {
  return set == NULL ? 0u : hash_map_entry_limit(&set->table);
}

uint64_t hash_set_generation(const hash_set_t *set) {
  return set == NULL ? UINT64_C(0)
                     : hash_map_generation(&set->table);
}

bool hash_set_empty(const hash_set_t *set) {
  return hash_set_size(set) == 0u;
}

const void *hash_set_key_at(const hash_set_t *set, size_t slot) {
  return set == NULL ? NULL : hash_map_key_at(&set->table, slot);
}
