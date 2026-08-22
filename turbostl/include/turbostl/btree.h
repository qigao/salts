#ifndef TURBO_BTREE_H
#define TURBO_BTREE_H

#include <cmeta/range.h>
#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TURBO_BTREE_DEFAULT_MIN_DEGREE
#define TURBO_BTREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*btree_compare_fn)(const void *left, const void *right,
                                      void *ctx);

typedef struct btree_entry_link {
  void *key;
  void *value;
  struct btree_entry_link *previous;
  struct btree_entry_link *next;
} btree_entry_link_t;

typedef struct btree_node {
  bool leaf;
  size_t num_keys;
  void **keys;
  void **values;
  btree_entry_link_t **links;
  struct btree_node **children;
} btree_node_t;

typedef struct {
  btree_node_t *root;
  size_t key_size;
  size_t key_align;
  size_t key_stride;
  size_t value_size;
  size_t value_align;
  size_t value_stride;
  size_t min_degree;
  size_t max_keys;
  size_t max_children;
  size_t entry_limit;
  size_t size;
  btree_entry_link_t *first;
  btree_entry_link_t *last;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  btree_compare_fn compare;
  void *compare_ctx;
  uint64_t generation;
  bool initialized;
} btree_t;

/* Descriptors and raw comparator/context are borrowed through destroy.
 * Typed keys require COMPARE|COPY|MOVE|DESTROY and typed values require
 * COPY|MOVE|DESTROY. Handles must be zero-initialized before first use.
 * Search, put, and remove are O(log n) for fixed min_degree; from-arrays is
 * O(rows log live_entries). Indexed key/value lookup is O(n), while the
 * ordered Range cursor follows the derived entry links in O(n) total. */
turbostl_status btree_init(
    btree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
turbostl_status btree_init_with_order(
    btree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit);
turbostl_status btree_init_bytes(
    btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    btree_compare_fn compare, void *compare_ctx);
turbostl_status btree_init_bytes_with_order(
    btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, btree_compare_fn compare, void *compare_ctx);

turbostl_status btree_from_arrays(
    btree_t *tree, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
turbostl_status btree_from_arrays_bytes(
    btree_t *tree, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, btree_compare_fn compare, void *compare_ctx);

void btree_destroy(btree_t *tree);
void btree_clear(btree_t *tree);
turbostl_status btree_reserve(btree_t *tree,
                                                    size_t min_capacity);
turbostl_status btree_put(btree_t *tree,
                                                const void *key,
                                                const void *value);
/* Returned pointers are borrowed and invalidate after any successful mutation,
 * clear, or destroy. */
void *btree_get(btree_t *tree, const void *key);
const void *btree_get_const(const btree_t *tree,
                                                const void *key);
bool btree_contains(const btree_t *tree,
                                        const void *key);
/* out_value is aligned uninitialized storage. Success transfers the removed
 * value there; NULL destroys it. Failure leaves tree and output unchanged. */
turbostl_status btree_remove(btree_t *tree,
                                                   const void *key,
                                                   void *out_value);
size_t btree_size(const btree_t *tree);
size_t btree_capacity(const btree_t *tree);
size_t btree_entry_limit(const btree_t *tree);
uint64_t btree_generation(const btree_t *tree);
bool btree_empty(const btree_t *tree);
void *btree_key_at(btree_t *tree, size_t index);
const void *btree_key_at_const(const btree_t *tree,
                                                   size_t index);
void *btree_value_at(btree_t *tree, size_t index);
const void *btree_value_at_const(const btree_t *tree,
                                                     size_t index);
/* cursor is zero-initialized before first use. The returned key/value pointers
 * are borrowed until the next successful mutation. */
bool btree_range_next(const btree_t *tree,
                                          cmeta_range_cursor *cursor,
                                          const void **out_key,
                                          const void **out_value);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_BTREE_H */
