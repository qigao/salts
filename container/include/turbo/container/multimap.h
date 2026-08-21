#ifndef TURBO_MULTIMAP_H
#define TURBO_MULTIMAP_H

#include <turbo/container/hash_map.h>
#include <turbo/container/vec.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raw key hash/equality callbacks and ctx are borrowed until destroy. The map
 * owns one heap-allocated Vec per live key; returned Vec/key pointers are
 * borrowed and invalidate after a successful mutation, clear, or destroy.
 * capacity() reports hash bucket slots, while key_limit bounds live keys and
 * value_limit bounds values retained by each key. */
typedef struct {
  turbo_hash_map_t map;
  size_t key_limit;
  size_t value_size;
  size_t value_align;
  size_t value_limit;
  size_t size;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  uint64_t generation;
} turbo_multimap_t;

static inline turbo_vec_t **turbo_multimap_values_carrier(turbo_multimap_t *map,
                                                           const void *key) {
  return map == NULL ? NULL : (turbo_vec_t **)turbo_hash_map_get(&map->map, key);
}

static inline turbo_vec_t *const *turbo_multimap_values_carrier_const(
    const turbo_multimap_t *map, const void *key) {
  return map == NULL ? NULL : (turbo_vec_t *const *)turbo_hash_map_get_const(&map->map, key);
}

static inline void turbo_multimap_destroy_vectors(turbo_multimap_t *map) {
  size_t slot;
  for (slot = 0u; map != NULL && slot < map->map.capacity; ++slot) {
    turbo_vec_t **carrier = (turbo_vec_t **)turbo_hash_map_value_at(&map->map, slot);
    if (carrier != NULL && *carrier != NULL) {
      turbo_vec_destroy(*carrier);
      free(*carrier);
      *carrier = NULL;
    }
  }
}

static inline bool turbo_multimap_pointer_copy(void *destination, const void *source) {
  if (destination == NULL || source == NULL) return false;
  *(turbo_vec_t **)destination = *(turbo_vec_t *const *)source;
  return true;
}
static inline void turbo_multimap_pointer_move(void *destination, void *source) {
  if (destination != NULL && source != NULL) {
    *(turbo_vec_t **)destination = *(turbo_vec_t **)source;
    *(turbo_vec_t **)source = NULL;
  }
}
static inline void turbo_multimap_pointer_destroy(void *value) { (void)value; }
static const cmeta_type_traits turbo_multimap_pointer_traits = {
  CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
      CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
  NULL, NULL, NULL, turbo_multimap_pointer_copy, turbo_multimap_pointer_move,
  turbo_multimap_pointer_destroy
};
static const cmeta_type_desc turbo_multimap_pointer_type = {
  "turbo_vec_t *", sizeof(turbo_vec_t *),
#ifdef __cplusplus
  alignof(turbo_vec_t *),
#else
  _Alignof(turbo_vec_t *),
#endif
  CMETA_T_POINTER, NULL, &turbo_multimap_pointer_traits
};

static inline container_status turbo_multimap_init(
    turbo_multimap_t *map, const cmeta_type_desc *key_type, size_t key_limit,
    const cmeta_type_desc *value_type, size_t value_limit) {
  container_status status;
  uint64_t generation;
  if (map == NULL || map->map.initialized) return CONTAINER_INVALID_ARGUMENT;
  if (value_type == NULL || value_type->size == 0u || value_type->align == 0u ||
      (value_type->align & (value_type->align - 1u)) != 0u ||
      cmeta_type_require_traits(value_type, CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                               CMETA_TRAIT_DESTROY) != CMETA_OK)
    return value_type == NULL || value_type->size == 0u
               ? CONTAINER_INVALID_ARGUMENT
               : CONTAINER_TRAIT_MISSING;
  generation = map->generation + UINT64_C(1);
  status = turbo_hash_map_init(&map->map, key_type, &turbo_multimap_pointer_type,
                               key_limit);
  if (status != CONTAINER_OK) return status;
  map->key_limit = key_limit;
  map->value_size = value_type->size;
  map->value_align = value_type->align;
  map->value_limit = value_limit;
  map->size = 0u;
  map->key_type = key_type;
  map->value_type = value_type;
  map->generation = generation;
  return CONTAINER_OK;
}

static inline container_status turbo_multimap_init_bytes(
    turbo_multimap_t *map, size_t key_size, size_t key_align,
    size_t key_limit, size_t value_size, size_t value_align,
    size_t value_limit, turbo_hash_fn hash,
    turbo_hash_equal_fn equal, void *ctx) {
  turbo_multimap_t temporary = {0};
  container_status status;
  uint64_t generation;
  if (map == NULL || map->map.initialized || key_size == 0u || value_size == 0u ||
      key_align == 0u || (key_align & (key_align - 1u)) != 0u ||
      value_align == 0u || (value_align & (value_align - 1u)) != 0u)
    return CONTAINER_INVALID_ARGUMENT;
  generation = map->generation + UINT64_C(1);
  temporary.key_limit = key_limit;
  temporary.value_size = value_size;
  temporary.value_align = value_align;
  temporary.value_limit = value_limit;
  temporary.generation = generation;
  status = turbo_hash_map_init_bytes(&temporary.map, key_size, key_align,
                                     sizeof(turbo_vec_t *),
#ifdef __cplusplus
                                   alignof(turbo_vec_t *),
#else
                                   _Alignof(turbo_vec_t *),
#endif
                                   key_limit, hash, equal, ctx);
  if (status != CONTAINER_OK) return status;
  *map = temporary;
  return CONTAINER_OK;
}

static inline void turbo_multimap_destroy(turbo_multimap_t *map) {
  uint64_t generation;
  if (map == NULL) return;
  generation = map->generation;
  if (map->map.initialized) ++generation;
  turbo_multimap_destroy_vectors(map);
  turbo_hash_map_destroy(&map->map);
  memset(map, 0, sizeof(*map));
  map->generation = generation;
}

static inline void turbo_multimap_clear(turbo_multimap_t *map) {
  bool changed;
  if (map == NULL) return;
  changed = map->size != 0u;
  turbo_multimap_destroy_vectors(map);
  turbo_hash_map_clear(&map->map);
  map->size = 0u;
  if (changed) ++map->generation;
}

static inline int turbo_multimap_reserve(turbo_multimap_t *map, size_t min_keys) {
  uint64_t before;
  int status;
  if (map == NULL) return CONTAINER_INVALID_ARGUMENT;
  before = turbo_hash_map_generation(&map->map);
  status = turbo_hash_map_reserve(&map->map, min_keys);
  if (status == CONTAINER_OK && turbo_hash_map_generation(&map->map) != before)
    ++map->generation;
  return status;
}

static inline int turbo_multimap_put(turbo_multimap_t *map, const void *key, const void *value) {
  turbo_vec_t **carrier;
  turbo_vec_t *values;
  int rc;

  if (map == NULL || key == NULL || value == NULL) return CONTAINER_INVALID_ARGUMENT;
  carrier = turbo_multimap_values_carrier(map, key);
  if (carrier != NULL) {
    rc = turbo_vec_push(*carrier, value);
    if (rc == CONTAINER_OK) { ++map->size; ++map->generation; }
    return rc;
  }
  values = (turbo_vec_t *)malloc(sizeof(*values));
  if (values == NULL) return CONTAINER_OUT_OF_MEMORY;
  memset(values, 0, sizeof(*values));
  rc = map->value_type != NULL
           ? turbo_vec_init(values, map->value_type, map->value_limit)
           : turbo_vec_init_bytes(values, map->value_size, map->value_align,
                                  map->value_limit);
  if (rc != CONTAINER_OK) { free(values); return rc; }
  rc = turbo_vec_push(values, value);
  if (rc != CONTAINER_OK) { turbo_vec_destroy(values); free(values); return rc; }
  rc = turbo_hash_map_put(&map->map, key, &values);
  if (rc != CONTAINER_OK) { turbo_vec_destroy(values); free(values); return rc; }
  ++map->size;
  ++map->generation;
  return CONTAINER_OK;
}

static inline const turbo_vec_t *turbo_multimap_get_values_const(const turbo_multimap_t *map,
                                                                   const void *key) {
  turbo_vec_t *const *carrier = turbo_multimap_values_carrier_const(map, key);
  return carrier == NULL ? NULL : *carrier;
}

static inline turbo_vec_t *turbo_multimap_get_values(turbo_multimap_t *map, const void *key) {
  turbo_vec_t **carrier = turbo_multimap_values_carrier(map, key);
  return carrier == NULL ? NULL : *carrier;
}

static inline bool turbo_multimap_contains(const turbo_multimap_t *map, const void *key) {
  return map != NULL && turbo_hash_map_contains(&map->map, key);
}

static inline size_t turbo_multimap_key_count(const turbo_multimap_t *map, const void *key) {
  const turbo_vec_t *values = turbo_multimap_get_values_const(map, key);
  return values == NULL ? 0u : turbo_vec_size(values);
}

static inline size_t turbo_multimap_size(const turbo_multimap_t *map) {
  return map == NULL ? 0u : map->size;
}

static inline size_t turbo_multimap_capacity(const turbo_multimap_t *map) {
  return map == NULL ? 0u : turbo_hash_map_capacity(&map->map);
}

static inline size_t turbo_multimap_entry_limit(const turbo_multimap_t *map) {
  return map == NULL ? 0u : turbo_hash_map_entry_limit(&map->map);
}

static inline bool turbo_multimap_empty(const turbo_multimap_t *map) {
  return map == NULL || map->size == 0u;
}

static inline bool turbo_multimap_remove(turbo_multimap_t *map, const void *key, void *out_value) {
  turbo_vec_t **carrier;
  turbo_vec_t *values;
  size_t before_count;
  int rc;

  if (map == NULL || key == NULL) return false;
  carrier = turbo_multimap_values_carrier(map, key);
  if (carrier == NULL || *carrier == NULL) return false;
  values = *carrier;
  before_count = turbo_vec_size(values);
  if (before_count == 0u) return false;
  rc = turbo_vec_pop(values, out_value);
  if (rc != CONTAINER_OK) return false;
  --map->size;
  ++map->generation;
  if (before_count == 1u) {
    rc = turbo_hash_map_remove(&map->map, key, NULL);
    if (rc != CONTAINER_OK) return false;
    turbo_vec_destroy(values);
    free(values);
  }
  return true;
}

static inline size_t turbo_multimap_erase(turbo_multimap_t *map, const void *key) {
  turbo_vec_t **carrier;
  turbo_vec_t *values;
  size_t removed;
  if (map == NULL || key == NULL) return 0u;
  carrier = turbo_multimap_values_carrier(map, key);
  if (carrier == NULL || *carrier == NULL) return 0u;
  values = *carrier;
  removed = turbo_vec_size(values);
  if (turbo_hash_map_remove(&map->map, key, NULL) != CONTAINER_OK) return 0u;
  turbo_vec_destroy(values);
  free(values);
  map->size -= removed;
  ++map->generation;
  return removed;
}

static inline uint64_t turbo_multimap_generation(const turbo_multimap_t *map) {
  return map == NULL ? UINT64_C(0) : map->generation;
}

static inline const void *turbo_multimap_key_at_const(
    const turbo_multimap_t *map, size_t pair_index) {
  size_t slot;
  if (map == NULL) return NULL;
  for (slot = 0u; slot < map->map.capacity; ++slot) {
    const void *key = turbo_hash_map_key_at(&map->map, slot);
    turbo_vec_t *const *carrier;
    size_t count;
    if (key == NULL) continue;
    carrier = (turbo_vec_t *const *)turbo_hash_map_value_at_const(&map->map, slot);
    count = carrier == NULL || *carrier == NULL ? 0u : turbo_vec_size(*carrier);
    if (pair_index < count) return key;
    pair_index -= count;
  }
  return NULL;
}

static inline const void *turbo_multimap_value_at_const(
    const turbo_multimap_t *map, size_t pair_index) {
  size_t slot;
  if (map == NULL) return NULL;
  for (slot = 0u; slot < map->map.capacity; ++slot) {
    turbo_vec_t *const *carrier = (turbo_vec_t *const *)
        turbo_hash_map_value_at_const(&map->map, slot);
    size_t count = carrier == NULL || *carrier == NULL ? 0u : turbo_vec_size(*carrier);
    if (pair_index < count) return turbo_vec_at_const(*carrier, pair_index);
    pair_index -= count;
  }
  return NULL;
}

static inline size_t turbo_multimap_range_capacity(const turbo_multimap_t *map) {
  return turbo_multimap_size(map);
}
static inline size_t turbo_multimap_range_size(const turbo_multimap_t *map) {
  return turbo_multimap_size(map);
}
static inline uint64_t turbo_multimap_range_generation(const turbo_multimap_t *map) {
  return turbo_multimap_generation(map);
}
static inline const void *turbo_multimap_range_key_at_const(
    const turbo_multimap_t *map, size_t index) {
  return turbo_multimap_key_at_const(map, index);
}
static inline const void *turbo_multimap_range_value_at_const(
    const turbo_multimap_t *map, size_t index) {
  return turbo_multimap_value_at_const(map, index);
}

#ifdef __cplusplus
}
#endif

#endif /* TURBO_MULTIMAP_H */
