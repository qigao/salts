#ifndef SALTS_BPLUS_TREE_H
#define SALTS_BPLUS_TREE_H

#include <cmeta/range.h>
#include <cstl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BPLUS_TREE_DEFAULT_MIN_DEGREE
#define BPLUS_TREE_DEFAULT_MIN_DEGREE 4U
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
  void *first_key;
} bplus_tree_node_t;

typedef struct bplus_tree {
  cmeta_container_header cmeta;
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
  uint64_t maintenance_node_visits;
  uint64_t generation;
  bool initialized;
} bplus_tree_t;

/* Storage bridges shared by generated facades and explicitly bound raw handles. */
stl_status bplus_tree_raw_init(
    bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
stl_status bplus_tree_raw_init_with_order(
    bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit);
stl_status bplus_tree_raw_from_arrays(
    bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit);
void bplus_tree_raw_destroy_storage(bplus_tree_t *tree);

stl_status bplus_tree_init_bytes(
    bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    bplus_tree_compare_fn compare, void *compare_ctx);
stl_status bplus_tree_init_bytes_with_order(
    bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, bplus_tree_compare_fn compare,
    void *compare_ctx);
stl_status bplus_tree_from_arrays_bytes(
    bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit,
    bplus_tree_compare_fn compare, void *compare_ctx);

static inline stl_status bplus_tree_init(bplus_tree_t *tree,
                                         size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (tree == NULL || tree->key_type == NULL || tree->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = tree->cmeta.descriptor;
  key_type = tree->key_type;
  value_type = tree->value_type;
  status = bplus_tree_raw_init(tree, key_type, value_type, entry_limit);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
  return status;
}

static inline stl_status bplus_tree_init_with_order(bplus_tree_t *tree,
                                                    size_t min_degree,
                                                    size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (tree == NULL || tree->key_type == NULL || tree->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = tree->cmeta.descriptor;
  key_type = tree->key_type;
  value_type = tree->value_type;
  status = bplus_tree_raw_init_with_order(tree, key_type, value_type,
                                          min_degree, entry_limit);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
  return status;
}

static inline stl_status bplus_tree_from_arrays(
    bplus_tree_t *tree, const void *keys, const void *values, size_t count,
    size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (tree == NULL || tree->key_type == NULL || tree->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = tree->cmeta.descriptor;
  key_type = tree->key_type;
  value_type = tree->value_type;
  status = bplus_tree_raw_from_arrays(tree, keys, values, count,
                                      key_type, value_type, entry_limit);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
  return status;
}

static inline void bplus_tree_destroy(bplus_tree_t *tree) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  if (tree == NULL)
    return;
  kind = tree->cmeta.descriptor;
  key_type = tree->key_type;
  value_type = tree->value_type;
  bplus_tree_raw_destroy_storage(tree);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
}

void bplus_tree_clear(bplus_tree_t *tree);
stl_status bplus_tree_reserve(bplus_tree_t *tree, size_t min_capacity);
stl_status bplus_tree_put(bplus_tree_t *tree, const void *key,
                          const void *value);
void *bplus_tree_get(bplus_tree_t *tree, const void *key);
const void *bplus_tree_get_const(const bplus_tree_t *tree, const void *key);
bool bplus_tree_contains(const bplus_tree_t *tree, const void *key);
stl_status bplus_tree_remove(bplus_tree_t *tree, const void *key,
                             void *out_value);
size_t bplus_tree_size(const bplus_tree_t *tree);
size_t bplus_tree_capacity(const bplus_tree_t *tree);
size_t bplus_tree_entry_limit(const bplus_tree_t *tree);
uint64_t bplus_tree_generation(const bplus_tree_t *tree);
bool bplus_tree_empty(const bplus_tree_t *tree);
void *bplus_tree_key_at(bplus_tree_t *tree, size_t index);
const void *bplus_tree_key_at_const(const bplus_tree_t *tree, size_t index);
void *bplus_tree_value_at(bplus_tree_t *tree, size_t index);
const void *bplus_tree_value_at_const(const bplus_tree_t *tree, size_t index);
bool bplus_tree_range_next(const bplus_tree_t *tree,
                           cmeta_range_cursor *cursor,
                           const void **out_key,
                           const void **out_value);


#ifdef __cplusplus
}
#endif
#endif /* SALTS_BPLUS_TREE_H */
