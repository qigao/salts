#ifndef TURBO_RBTREE_INTERNAL_H
#define TURBO_RBTREE_INTERNAL_H

#include <cmeta/cmeta.h>
#include <turbo/stl/status.h>

#include <stdbool.h>
#include <stddef.h>

typedef int (*turbo_rbtree_compare_fn)(const void *left, const void *right,
                                       void *context);

typedef struct turbo_rbtree_node {
  struct turbo_rbtree_node *parent;
  struct turbo_rbtree_node *left;
  struct turbo_rbtree_node *right;
  struct turbo_rbtree_node *previous;
  struct turbo_rbtree_node *next;
  void *key;
  void *value;
  bool red;
} turbo_rbtree_node_t;

typedef struct turbo_rbtree {
  turbo_rbtree_node_t *root;
  turbo_rbtree_node_t *head;
  turbo_rbtree_node_t *tail;
  size_t size;
  size_t key_size;
  size_t key_stride;
  size_t key_align;
  size_t value_size;
  size_t value_stride;
  size_t value_align;
  size_t element_limit;
  const cmeta_type_desc *key_type;
  const cmeta_type_desc *value_type;
  turbo_rbtree_compare_fn compare;
  void *compare_context;
  bool allow_duplicates;
} turbo_rbtree_t;

typedef enum turbo_rbtree_put_result {
  TURBO_RBTREE_INSERTED,
  TURBO_RBTREE_REPLACED
} turbo_rbtree_put_result;

turbo_stl_status turbo_rbtree_create(
    turbo_rbtree_t **out_tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    turbo_rbtree_compare_fn compare, void *compare_context,
    bool allow_duplicates);
void turbo_rbtree_destroy(turbo_rbtree_t *tree);
void turbo_rbtree_clear(turbo_rbtree_t *tree);
turbo_stl_status turbo_rbtree_put(turbo_rbtree_t *tree, const void *key,
                                  const void *value,
                                  turbo_rbtree_put_result *out_result);
turbo_rbtree_node_t *turbo_rbtree_find(const turbo_rbtree_t *tree,
                                       const void *key);
turbo_rbtree_node_t *turbo_rbtree_lower_bound(const turbo_rbtree_t *tree,
                                              const void *key);
turbo_rbtree_node_t *turbo_rbtree_upper_bound(const turbo_rbtree_t *tree,
                                              const void *key);
turbo_stl_status turbo_rbtree_remove_node(turbo_rbtree_t *tree,
                                          turbo_rbtree_node_t *node,
                                          void *out_value);

#endif /* TURBO_RBTREE_INTERNAL_H */
