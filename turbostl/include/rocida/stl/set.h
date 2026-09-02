#ifndef TURBOSTL_SET_H
#define TURBOSTL_SET_H

#include <rocida/stl/map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef map_compare_fn set_compare_fn;

typedef struct set {
  cmeta_container_header cmeta;
  const cmeta_type_desc *element_type;
  map_t map;
} set_t;

typedef struct set_iter {
  const set_t *owner;
  void *node;
} set_iter_t;

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
stl_status set_raw_init(set_t *set, const cmeta_type_desc *key_type,
                        size_t element_limit);
stl_status set_raw_from_array(set_t *set, const void *keys, size_t count,
                              const cmeta_type_desc *key_type,
                              size_t element_limit);
void set_raw_destroy_storage(set_t *set);

stl_status set_init_bytes(set_t *set, size_t key_size, size_t key_align,
                          size_t element_limit, set_compare_fn compare,
                          void *context);
stl_status set_from_array_bytes(set_t *set, const void *keys, size_t count,
                                size_t key_size, size_t key_align,
                                size_t element_limit, set_compare_fn compare,
                                void *context);

static inline stl_status set_init(set_t *set, size_t element_limit) {
  if (set == NULL || set->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  return set_raw_init(set, set->element_type, element_limit);
}

static inline stl_status set_from_array(set_t *set, const void *keys,
                                        size_t count, size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (set == NULL || set->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = set->cmeta.descriptor;
  type = set->element_type;
  status = set_raw_from_array(set, keys, count, type, element_limit);
  set->cmeta.descriptor = kind;
  set->element_type = type;
  return status;
}

static inline void set_destroy(set_t *set) {
  if (set != NULL)
    set_raw_destroy_storage(set);
}

void set_clear(set_t *set);
stl_status set_add(set_t *set, const void *key);
bool set_contains(const set_t *set, const void *key);
stl_status set_remove(set_t *set, const void *key);
size_t set_size(const set_t *set);
size_t set_element_limit(const set_t *set);
uint64_t set_generation(const set_t *set);
bool set_empty(const set_t *set);

set_iter_t set_begin(const set_t *set);
set_iter_t set_end(const set_t *set);
set_iter_t set_lower_bound(const set_t *set, const void *key);
set_iter_t set_upper_bound(const set_t *set, const void *key);
stl_status set_iter_next(set_iter_t *iterator);
stl_status set_iter_prev(set_iter_t *iterator);
bool set_iter_equal(set_iter_t left, set_iter_t right);
const void *set_iter_value_const(set_iter_t iterator);
bool set_range_next(const set_t *set, cmeta_range_cursor *cursor,
                    const void **out_value);


#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_SET_H */
