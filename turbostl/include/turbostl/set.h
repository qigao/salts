#ifndef TURBO_SET_H
#define TURBO_SET_H

#include <turbostl/map.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef map_compare_fn set_compare_fn;

typedef struct set {
  map_t map;
} set_t;

typedef struct set_iter {
  const set_t *owner;
  void *node;
} set_iter_t;

/* Set is a unique-key red-black tree. It is ordered by COMPARE and is not a
 * HashSet alias. */
turbostl_status set_init(
    set_t *set, const cmeta_type_desc *key_type, size_t element_limit);
turbostl_status set_init_bytes(
    set_t *set, size_t key_size, size_t key_align,
    size_t element_limit, set_compare_fn compare, void *context);
turbostl_status set_from_array(
    set_t *set, const void *keys, size_t count,
    const cmeta_type_desc *key_type, size_t element_limit);
turbostl_status set_from_array_bytes(
    set_t *set, const void *keys, size_t count, size_t key_size,
    size_t key_align, size_t element_limit, set_compare_fn compare,
    void *context);
void set_destroy(set_t *set);
void set_clear(set_t *set);
turbostl_status set_add(set_t *set,
                                              const void *key);
bool set_contains(const set_t *set,
                                      const void *key);
turbostl_status set_remove(set_t *set,
                                                 const void *key);
size_t set_size(const set_t *set);
size_t set_element_limit(const set_t *set);
uint64_t set_generation(const set_t *set);
bool set_empty(const set_t *set);

set_iter_t set_begin(const set_t *set);
set_iter_t set_end(const set_t *set);
set_iter_t set_lower_bound(const set_t *set,
                                                      const void *key);
set_iter_t set_upper_bound(const set_t *set,
                                                      const void *key);
turbostl_status set_iter_next(set_iter_t *iterator);
turbostl_status set_iter_prev(set_iter_t *iterator);
bool set_iter_equal(set_iter_t left,
                                         set_iter_t right);
const void *set_iter_value_const(set_iter_t iterator);
bool set_range_next(const set_t *set,
                                        cmeta_range_cursor *cursor,
                                        const void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SET_H */
