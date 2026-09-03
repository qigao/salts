#ifndef SALTS_RBTREE_INTERNAL_H
#define SALTS_RBTREE_INTERNAL_H

#include <cmeta/cmeta.h>
#include <cstl/status.h>

#include <stdbool.h>
#include <stddef.h>

typedef int (*rbtree_compare_fn)(const void *left, const void *right,
                                       void *context);

typedef struct rbtree_node {
  struct rbtree_node *parent;
  struct rbtree_node *left;
  struct rbtree_node *right;
  struct rbtree_node *previous;
  struct rbtree_node *next;
  void *key;
  void *value;
  bool red;
} rbtree_node_t;

typedef struct rbtree {
  rbtree_node_t *root;
  rbtree_node_t *head;
  rbtree_node_t *tail;
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
  rbtree_compare_fn compare;
  void *compare_context;
  bool allow_duplicates;
} rbtree_t;

typedef enum rbtree_put_result {
  RBTREE_INSERTED,
  RBTREE_REPLACED
} rbtree_put_result;

stl_status rbtree_create(
    rbtree_t **out_tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    rbtree_compare_fn compare, void *compare_context,
    bool allow_duplicates);
void rbtree_destroy(rbtree_t *tree);
void rbtree_clear(rbtree_t *tree);
stl_status rbtree_put(rbtree_t *tree, const void *key,
                                  const void *value,
                                  rbtree_put_result *out_result);
rbtree_node_t *rbtree_find(const rbtree_t *tree,
                                       const void *key);
rbtree_node_t *rbtree_lower_bound(const rbtree_t *tree,
                                              const void *key);
rbtree_node_t *rbtree_upper_bound(const rbtree_t *tree,
                                              const void *key);
stl_status rbtree_remove_node(rbtree_t *tree,
                                          rbtree_node_t *node,
                                          void *out_value);

#endif /* SALTS_RBTREE_INTERNAL_H */
