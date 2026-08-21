#include <turbo/container/set.h>

#include <stdint.h>

container_status turbo_set_init(turbo_set_t *set,
                                const cmeta_type_desc *key_type,
                                size_t element_limit) {
  if (set == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_map_init(&set->map, key_type, &cmeta_type_bool,
                        element_limit);
}

container_status turbo_set_init_bytes(turbo_set_t *set, size_t key_size,
                                      size_t key_align, size_t element_limit,
                                      turbo_set_compare_fn compare,
                                      void *context) {
  if (set == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_map_init_bytes(&set->map, key_size, key_align,
                              sizeof(uint8_t), _Alignof(uint8_t),
                              element_limit, compare, context);
}

static container_status turbo_set_from_common(
    turbo_set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t key_size, size_t key_align,
    size_t element_limit, turbo_set_compare_fn compare, void *context) {
  turbo_set_t temporary = {0};
  container_status status;
  uint64_t generation;
  size_t index;

  if (set == NULL || (count != 0u && keys == NULL))
    return CONTAINER_INVALID_ARGUMENT;
  if (count != 0u && key_size > SIZE_MAX / count)
    return CONTAINER_CAPACITY_EXCEEDED;
  status = key_type != NULL
               ? turbo_set_init(&temporary, key_type, element_limit)
               : turbo_set_init_bytes(&temporary, key_size, key_align,
                                      element_limit, compare, context);
  if (status != CONTAINER_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_set_add(
        &temporary, (const unsigned char *)keys + index * key_size);
    if (status != CONTAINER_OK) {
      turbo_set_destroy(&temporary);
      return status;
    }
  }
  generation = turbo_set_generation(set) + UINT64_C(1);
  turbo_set_destroy(set);
  temporary.map.generation = generation;
  *set = temporary;
  return CONTAINER_OK;
}

container_status turbo_set_from_array(turbo_set_t *set, const void *keys,
                                      size_t count,
                                      const cmeta_type_desc *key_type,
                                      size_t element_limit) {
  if (key_type == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_set_from_common(set, keys, count, key_type, key_type->size,
                               key_type->align, element_limit, NULL, NULL);
}

container_status turbo_set_from_array_bytes(
    turbo_set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t element_limit, turbo_set_compare_fn compare,
    void *context) {
  return turbo_set_from_common(set, keys, count, NULL, key_size, key_align,
                               element_limit, compare, context);
}

void turbo_set_destroy(turbo_set_t *set) {
  if (set != NULL) turbo_map_destroy(&set->map);
}

void turbo_set_clear(turbo_set_t *set) {
  if (set != NULL) turbo_map_clear(&set->map);
}

container_status turbo_set_add(turbo_set_t *set, const void *key) {
  uint8_t present = 1u;
  if (set == NULL || key == NULL) return CONTAINER_INVALID_ARGUMENT;
  if (turbo_map_contains(&set->map, key)) return CONTAINER_OK;
  return turbo_map_put(&set->map, key, &present);
}

bool turbo_set_contains(const turbo_set_t *set, const void *key) {
  return set != NULL && turbo_map_contains(&set->map, key);
}

container_status turbo_set_remove(turbo_set_t *set, const void *key) {
  return set == NULL ? CONTAINER_INVALID_ARGUMENT
                     : turbo_map_remove(&set->map, key, NULL);
}

size_t turbo_set_size(const turbo_set_t *set) {
  return set == NULL ? 0u : turbo_map_size(&set->map);
}

size_t turbo_set_element_limit(const turbo_set_t *set) {
  return set == NULL ? 0u : turbo_map_entry_limit(&set->map);
}

uint64_t turbo_set_generation(const turbo_set_t *set) {
  return set == NULL ? UINT64_C(0) : turbo_map_generation(&set->map);
}

bool turbo_set_empty(const turbo_set_t *set) {
  return turbo_set_size(set) == 0u;
}

static turbo_set_iter_t turbo_set_iterator(const turbo_set_t *set,
                                           turbo_map_iter_t iterator) {
  turbo_set_iter_t result = {set, iterator.node};
  return result;
}

turbo_set_iter_t turbo_set_begin(const turbo_set_t *set) {
  return turbo_set_iterator(set, turbo_map_begin(set == NULL ? NULL : &set->map));
}

turbo_set_iter_t turbo_set_end(const turbo_set_t *set) {
  return turbo_set_iterator(set, turbo_map_end(set == NULL ? NULL : &set->map));
}

turbo_set_iter_t turbo_set_lower_bound(const turbo_set_t *set,
                                       const void *key) {
  return turbo_set_iterator(
      set, turbo_map_lower_bound(set == NULL ? NULL : &set->map, key));
}

turbo_set_iter_t turbo_set_upper_bound(const turbo_set_t *set,
                                       const void *key) {
  return turbo_set_iterator(
      set, turbo_map_upper_bound(set == NULL ? NULL : &set->map, key));
}

container_status turbo_set_iter_next(turbo_set_iter_t *iterator) {
  turbo_map_iter_t map_iterator;
  container_status status;
  if (iterator == NULL || iterator->owner == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  map_iterator.owner = &iterator->owner->map;
  map_iterator.node = iterator->node;
  status = turbo_map_iter_next(&map_iterator);
  if (status == CONTAINER_OK) iterator->node = map_iterator.node;
  return status;
}

container_status turbo_set_iter_prev(turbo_set_iter_t *iterator) {
  turbo_map_iter_t map_iterator;
  container_status status;
  if (iterator == NULL || iterator->owner == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  map_iterator.owner = &iterator->owner->map;
  map_iterator.node = iterator->node;
  status = turbo_map_iter_prev(&map_iterator);
  if (status == CONTAINER_OK) iterator->node = map_iterator.node;
  return status;
}

bool turbo_set_iter_equal(turbo_set_iter_t left, turbo_set_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

const void *turbo_set_iter_value_const(turbo_set_iter_t iterator) {
  turbo_map_iter_t map_iterator;
  if (iterator.owner == NULL) return NULL;
  map_iterator.owner = &iterator.owner->map;
  map_iterator.node = iterator.node;
  return turbo_map_iter_key_const(map_iterator);
}

bool turbo_set_range_next(const turbo_set_t *set,
                          cmeta_range_cursor *cursor,
                          const void **out_value) {
  const void *ignored;
  return set != NULL &&
         turbo_map_range_next(&set->map, cursor, out_value, &ignored);
}
