#ifndef TURBO_BPLUS_TREE_H
#define TURBO_BPLUS_TREE_H

#include <cmeta/cmeta.h>
#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE
#define TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*turbo_bplus_tree_compare_fn)(const void *left,
                                           const void *right, void *ctx);

typedef struct turbo_bplus_tree_entry_link {
  void *key;
  void *value;
  struct turbo_bplus_tree_entry_link *previous;
  struct turbo_bplus_tree_entry_link *next;
} turbo_bplus_tree_entry_link_t;

typedef struct turbo_bplus_tree_node {
  bool is_leaf;
  size_t num_keys;
  void **keys;
  void **values;
  turbo_bplus_tree_entry_link_t **links;
  struct turbo_bplus_tree_node **children;
  struct turbo_bplus_tree_node *parent;
  struct turbo_bplus_tree_node *next;
  /* Borrowed derived metadata: the first leaf key in this subtree. */
  void *first_key;
} turbo_bplus_tree_node_t;

typedef struct {
  turbo_bplus_tree_node_t *root;
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
  turbo_bplus_tree_entry_link_t *first;
  turbo_bplus_tree_entry_link_t *last;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  turbo_bplus_tree_compare_fn compare;
  void *compare_ctx;
  /* Cumulative mutation-maintenance node reads. This diagnostic counter does
   * not participate in logical generation and lets complexity tests measure
   * structural work without timing thresholds. */
  uint64_t maintenance_node_visits;
  uint64_t generation;
  bool initialized;
} turbo_bplus_tree_t;

/* Leaf entries own aligned key/value objects. Internal separators and
 * first_key metadata borrow leaf keys and are refreshed only along the
 * modified ancestor path; they never destroy or retain keys independently.
 * Search, put, and remove are O(log n) for fixed min_degree;
 * from-arrays is O(rows log live_entries). Indexed lookup is O(n), while the
 * ordered Range cursor follows derived entry links in O(n) total. */
CONTAINER_API container_status turbo_bplus_tree_init(
    turbo_bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
CONTAINER_API container_status turbo_bplus_tree_init_with_order(
    turbo_bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit);
CONTAINER_API container_status turbo_bplus_tree_init_bytes(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx);
CONTAINER_API container_status turbo_bplus_tree_init_bytes_with_order(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, turbo_bplus_tree_compare_fn compare,
    void *compare_ctx);
CONTAINER_API container_status turbo_bplus_tree_from_arrays(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
CONTAINER_API container_status turbo_bplus_tree_from_arrays_bytes(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx);

CONTAINER_API void turbo_bplus_tree_destroy(turbo_bplus_tree_t *tree);
CONTAINER_API void turbo_bplus_tree_clear(turbo_bplus_tree_t *tree);
CONTAINER_API container_status turbo_bplus_tree_reserve(
    turbo_bplus_tree_t *tree, size_t min_capacity);
CONTAINER_API container_status turbo_bplus_tree_put(turbo_bplus_tree_t *tree,
                                                     const void *key,
                                                     const void *value);
CONTAINER_API void *turbo_bplus_tree_get(turbo_bplus_tree_t *tree,
                                         const void *key);
CONTAINER_API const void *turbo_bplus_tree_get_const(
    const turbo_bplus_tree_t *tree, const void *key);
CONTAINER_API bool turbo_bplus_tree_contains(const turbo_bplus_tree_t *tree,
                                             const void *key);
CONTAINER_API container_status turbo_bplus_tree_remove(
    turbo_bplus_tree_t *tree, const void *key, void *out_value);
CONTAINER_API size_t turbo_bplus_tree_size(const turbo_bplus_tree_t *tree);
CONTAINER_API size_t turbo_bplus_tree_capacity(const turbo_bplus_tree_t *tree);
CONTAINER_API size_t turbo_bplus_tree_entry_limit(
    const turbo_bplus_tree_t *tree);
CONTAINER_API uint64_t turbo_bplus_tree_generation(
    const turbo_bplus_tree_t *tree);
CONTAINER_API bool turbo_bplus_tree_empty(const turbo_bplus_tree_t *tree);
CONTAINER_API void *turbo_bplus_tree_key_at(turbo_bplus_tree_t *tree,
                                            size_t index);
CONTAINER_API const void *turbo_bplus_tree_key_at_const(
    const turbo_bplus_tree_t *tree, size_t index);
CONTAINER_API void *turbo_bplus_tree_value_at(turbo_bplus_tree_t *tree,
                                              size_t index);
CONTAINER_API const void *turbo_bplus_tree_value_at_const(
    const turbo_bplus_tree_t *tree, size_t index);
CONTAINER_API bool turbo_bplus_tree_range_next(
    const turbo_bplus_tree_t *tree, size_t *cursor, const void **out_key,
    const void **out_value);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_BPLUS_TREE_H */
