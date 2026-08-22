#ifndef TURBO_BPLUS_TREE_H
#define TURBO_BPLUS_TREE_H

#include <cmeta/range.h>
#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE
#define TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*bplus_tree_compare_fn)(const void *left,
                                           const void *right, void *ctx);

typedef struct bplus_tree_entry_link {
  void *key;
  void *value;
  struct bplus_tree_entry_link *previous;
  struct bplus_tree_entry_link *next;
} bplus_tree_entry_link_t;

typedef struct bplus_tree_node {
  bool is_leaf;
  size_t num_keys;
  void **keys;
  void **values;
  bplus_tree_entry_link_t **links;
  struct bplus_tree_node **children;
  struct bplus_tree_node *parent;
  struct bplus_tree_node *next;
  /* Borrowed derived metadata: the first leaf key in this subtree. */
  void *first_key;
} bplus_tree_node_t;

typedef struct {
  bplus_tree_node_t *root;
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
  bplus_tree_entry_link_t *first;
  bplus_tree_entry_link_t *last;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  bplus_tree_compare_fn compare;
  void *compare_ctx;
  /* Cumulative mutation-maintenance node reads. This diagnostic counter does
   * not participate in logical generation and lets complexity tests measure
   * structural work without timing thresholds. */
  uint64_t maintenance_node_visits;
  uint64_t generation;
  bool initialized;
} bplus_tree_t;

/* Leaf entries own aligned key/value objects. Internal separators and
 * first_key metadata borrow leaf keys and are refreshed only along the
 * modified ancestor path; they never destroy or retain keys independently.
 * Search, put, and remove are O(log n) for fixed min_degree;
 * from-arrays is O(rows log live_entries). Indexed lookup is O(n), while the
 * ordered Range cursor follows derived entry links in O(n) total. */
turbostl_status bplus_tree_init(
    bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
turbostl_status bplus_tree_init_with_order(
    bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit);
turbostl_status bplus_tree_init_bytes(
    bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    bplus_tree_compare_fn compare, void *compare_ctx);
turbostl_status bplus_tree_init_bytes_with_order(
    bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, bplus_tree_compare_fn compare,
    void *compare_ctx);
turbostl_status bplus_tree_from_arrays(
    bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
turbostl_status bplus_tree_from_arrays_bytes(
    bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit,
    bplus_tree_compare_fn compare, void *compare_ctx);

void bplus_tree_destroy(bplus_tree_t *tree);
void bplus_tree_clear(bplus_tree_t *tree);
turbostl_status bplus_tree_reserve(
    bplus_tree_t *tree, size_t min_capacity);
turbostl_status bplus_tree_put(bplus_tree_t *tree,
                                                     const void *key,
                                                     const void *value);
void *bplus_tree_get(bplus_tree_t *tree,
                                         const void *key);
const void *bplus_tree_get_const(
    const bplus_tree_t *tree, const void *key);
bool bplus_tree_contains(const bplus_tree_t *tree,
                                             const void *key);
turbostl_status bplus_tree_remove(
    bplus_tree_t *tree, const void *key, void *out_value);
size_t bplus_tree_size(const bplus_tree_t *tree);
size_t bplus_tree_capacity(const bplus_tree_t *tree);
size_t bplus_tree_entry_limit(
    const bplus_tree_t *tree);
uint64_t bplus_tree_generation(
    const bplus_tree_t *tree);
bool bplus_tree_empty(const bplus_tree_t *tree);
void *bplus_tree_key_at(bplus_tree_t *tree,
                                            size_t index);
const void *bplus_tree_key_at_const(
    const bplus_tree_t *tree, size_t index);
void *bplus_tree_value_at(bplus_tree_t *tree,
                                              size_t index);
const void *bplus_tree_value_at_const(
    const bplus_tree_t *tree, size_t index);
bool bplus_tree_range_next(
    const bplus_tree_t *tree, cmeta_range_cursor *cursor,
    const void **out_key,
    const void **out_value);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_BPLUS_TREE_H */
