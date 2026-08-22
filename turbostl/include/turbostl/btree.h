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

#ifndef BTREE_DEFAULT_MIN_DEGREE
#define BTREE_DEFAULT_MIN_DEGREE 4U
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

typedef struct btree {
  cmeta_container_header cmeta;
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

/* Internal typed-storage bridges used by the natural instance wrappers. */
stl_status btree_raw_init(btree_t *tree,
                          const cmeta_type_desc *key_type,
                          const cmeta_type_desc *value_type,
                          size_t entry_limit);
stl_status btree_raw_init_with_order(
    btree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit);
stl_status btree_raw_from_arrays(
    btree_t *tree, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit);
void btree_raw_destroy_storage(btree_t *tree);

stl_status btree_init_bytes(
    btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    btree_compare_fn compare, void *compare_ctx);
stl_status btree_init_bytes_with_order(
    btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, btree_compare_fn compare, void *compare_ctx);
stl_status btree_from_arrays_bytes(
    btree_t *tree, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, btree_compare_fn compare, void *compare_ctx);

static inline stl_status btree_init(btree_t *tree, size_t entry_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  stl_status status;
  if (tree == NULL || tree->key_type == NULL || tree->value_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = tree->cmeta.descriptor;
  key_type = tree->key_type;
  value_type = tree->value_type;
  status = btree_raw_init(tree, key_type, value_type, entry_limit);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
  return status;
}

static inline stl_status btree_init_with_order(btree_t *tree,
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
  status = btree_raw_init_with_order(tree, key_type, value_type,
                                     min_degree, entry_limit);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
  return status;
}

static inline stl_status btree_from_arrays(
    btree_t *tree, const void *keys, const void *values, size_t count,
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
  status = btree_raw_from_arrays(tree, keys, values, count, key_type,
                                 value_type, entry_limit);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
  return status;
}

static inline void btree_destroy(btree_t *tree) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  if (tree == NULL)
    return;
  kind = tree->cmeta.descriptor;
  key_type = tree->key_type;
  value_type = tree->value_type;
  btree_raw_destroy_storage(tree);
  tree->cmeta.descriptor = kind;
  tree->key_type = key_type;
  tree->value_type = value_type;
}

void btree_clear(btree_t *tree);
stl_status btree_reserve(btree_t *tree, size_t min_capacity);
stl_status btree_put(btree_t *tree, const void *key, const void *value);
void *btree_get(btree_t *tree, const void *key);
const void *btree_get_const(const btree_t *tree, const void *key);
bool btree_contains(const btree_t *tree, const void *key);
stl_status btree_remove(btree_t *tree, const void *key, void *out_value);
size_t btree_size(const btree_t *tree);
size_t btree_capacity(const btree_t *tree);
size_t btree_entry_limit(const btree_t *tree);
uint64_t btree_generation(const btree_t *tree);
bool btree_empty(const btree_t *tree);
void *btree_key_at(btree_t *tree, size_t index);
const void *btree_key_at_const(const btree_t *tree, size_t index);
void *btree_value_at(btree_t *tree, size_t index);
const void *btree_value_at_const(const btree_t *tree, size_t index);
bool btree_range_next(const btree_t *tree, cmeta_range_cursor *cursor,
                      const void **out_key, const void **out_value);


#ifdef __cplusplus
}
#endif
#endif /* TURBO_BTREE_H */
