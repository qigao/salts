#include <turbostl/set.h>

#include <stdint.h>

turbostl_status set_init(set_t *set,
                                const cmeta_type_desc *key_type,
                                size_t element_limit) {
  if (set == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return map_init(&set->map, key_type, &cmeta_type_bool,
                        element_limit);
}

turbostl_status set_init_bytes(set_t *set, size_t key_size,
                                      size_t key_align, size_t element_limit,
                                      set_compare_fn compare,
                                      void *context) {
  if (set == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return map_init_bytes(&set->map, key_size, key_align,
                              sizeof(uint8_t), _Alignof(uint8_t),
                              element_limit, compare, context);
}

static turbostl_status set_from_common(
    set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t key_size, size_t key_align,
    size_t element_limit, set_compare_fn compare, void *context) {
  set_t temporary = {0};
  turbostl_status status;
  uint64_t generation;
  size_t index;

  if (set == NULL || (count != 0u && keys == NULL))
    return TURBO_STL_INVALID_ARGUMENT;
  if (count != 0u && key_size > SIZE_MAX / count)
    return TURBO_STL_CAPACITY_EXCEEDED;
  status = key_type != NULL
               ? set_init(&temporary, key_type, element_limit)
               : set_init_bytes(&temporary, key_size, key_align,
                                      element_limit, compare, context);
  if (status != TURBO_STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = set_add(
        &temporary, (const unsigned char *)keys + index * key_size);
    if (status != TURBO_STL_OK) {
      set_destroy(&temporary);
      return status;
    }
  }
  generation = set_generation(set) + UINT64_C(1);
  set_destroy(set);
  temporary.map.generation = generation;
  *set = temporary;
  return TURBO_STL_OK;
}

turbostl_status set_from_array(set_t *set, const void *keys,
                                      size_t count,
                                      const cmeta_type_desc *key_type,
                                      size_t element_limit) {
  if (key_type == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return set_from_common(set, keys, count, key_type, key_type->size,
                               key_type->align, element_limit, NULL, NULL);
}

turbostl_status set_from_array_bytes(
    set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t element_limit, set_compare_fn compare,
    void *context) {
  return set_from_common(set, keys, count, NULL, key_size, key_align,
                               element_limit, compare, context);
}

void set_destroy(set_t *set) {
  if (set != NULL) map_destroy(&set->map);
}

void set_clear(set_t *set) {
  if (set != NULL) map_clear(&set->map);
}

turbostl_status set_add(set_t *set, const void *key) {
  uint8_t present = 1u;
  if (set == NULL || key == NULL) return TURBO_STL_INVALID_ARGUMENT;
  if (map_contains(&set->map, key)) return TURBO_STL_OK;
  return map_put(&set->map, key, &present);
}

bool set_contains(const set_t *set, const void *key) {
  return set != NULL && map_contains(&set->map, key);
}

turbostl_status set_remove(set_t *set, const void *key) {
  return set == NULL ? TURBO_STL_INVALID_ARGUMENT
                     : map_remove(&set->map, key, NULL);
}

size_t set_size(const set_t *set) {
  return set == NULL ? 0u : map_size(&set->map);
}

size_t set_element_limit(const set_t *set) {
  return set == NULL ? 0u : map_entry_limit(&set->map);
}

uint64_t set_generation(const set_t *set) {
  return set == NULL ? UINT64_C(0) : map_generation(&set->map);
}

bool set_empty(const set_t *set) {
  return set_size(set) == 0u;
}

static set_iter_t set_iterator(const set_t *set,
                                           map_iter_t iterator) {
  set_iter_t result = {set, iterator.node};
  return result;
}

set_iter_t set_begin(const set_t *set) {
  return set_iterator(set, map_begin(set == NULL ? NULL : &set->map));
}

set_iter_t set_end(const set_t *set) {
  return set_iterator(set, map_end(set == NULL ? NULL : &set->map));
}

set_iter_t set_lower_bound(const set_t *set,
                                       const void *key) {
  return set_iterator(
      set, map_lower_bound(set == NULL ? NULL : &set->map, key));
}

set_iter_t set_upper_bound(const set_t *set,
                                       const void *key) {
  return set_iterator(
      set, map_upper_bound(set == NULL ? NULL : &set->map, key));
}

turbostl_status set_iter_next(set_iter_t *iterator) {
  map_iter_t map_iterator;
  turbostl_status status;
  if (iterator == NULL || iterator->owner == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  map_iterator.owner = &iterator->owner->map;
  map_iterator.node = iterator->node;
  status = map_iter_next(&map_iterator);
  if (status == TURBO_STL_OK) iterator->node = map_iterator.node;
  return status;
}

turbostl_status set_iter_prev(set_iter_t *iterator) {
  map_iter_t map_iterator;
  turbostl_status status;
  if (iterator == NULL || iterator->owner == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  map_iterator.owner = &iterator->owner->map;
  map_iterator.node = iterator->node;
  status = map_iter_prev(&map_iterator);
  if (status == TURBO_STL_OK) iterator->node = map_iterator.node;
  return status;
}

bool set_iter_equal(set_iter_t left, set_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

const void *set_iter_value_const(set_iter_t iterator) {
  map_iter_t map_iterator;
  if (iterator.owner == NULL) return NULL;
  map_iterator.owner = &iterator.owner->map;
  map_iterator.node = iterator.node;
  return map_iter_key_const(map_iterator);
}

bool set_range_next(const set_t *set,
                          cmeta_range_cursor *cursor,
                          const void **out_value) {
  const void *ignored;
  return set != NULL &&
         map_range_next(&set->map, cursor, out_value, &ignored);
}
