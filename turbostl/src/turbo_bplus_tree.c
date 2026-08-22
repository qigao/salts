#include <turbostl/bplus_tree.h>

#include "turbo_sequence_internal.h"

#include <string.h>

static bool turbo_bplus_valid(const turbo_bplus_tree_t *tree) {
  return tree != NULL && tree->initialized && tree->key_size != 0u &&
         tree->value_size != 0u && tree->min_degree >= 2u &&
         (tree->key_type != NULL || tree->compare != NULL);
}

static int turbo_bplus_compare_key(const turbo_bplus_tree_t *tree,
                                   const void *left, const void *right) {
  return tree->key_type != NULL
             ? tree->key_type->traits->compare(left, right)
             : tree->compare(left, right, tree->compare_ctx);
}

static void turbo_bplus_destroy_object(const cmeta_type_desc *type,
                                       void *object) {
  if (object == NULL) return;
  if (type != NULL) type->traits->destroy(object);
  turbo_sequence_deallocate(object);
}

static turbo_stl_status turbo_bplus_copy_object(
    const cmeta_type_desc *type, size_t size, size_t stride, size_t alignment,
    const void *source, void **out_object) {
  turbo_stl_status status;
  if (source == NULL || out_object == NULL) return TURBO_STL_INVALID_ARGUMENT;
  *out_object = NULL;
  status = turbo_sequence_allocate(1u, stride, alignment, out_object);
  if (status != TURBO_STL_OK) return status;
  if (type != NULL) {
    if (!type->traits->copy_construct(*out_object, source)) {
      turbo_sequence_deallocate(*out_object);
      *out_object = NULL;
      return TURBO_STL_OUT_OF_MEMORY;
    }
  } else {
    memcpy(*out_object, source, size);
  }
  return TURBO_STL_OK;
}

static turbo_bplus_tree_node_t *turbo_bplus_node_new(
    const turbo_bplus_tree_t *tree, bool leaf) {
  turbo_bplus_tree_node_t *node;
  if (!turbo_bplus_valid(tree) ||
      tree->max_keys > SIZE_MAX / sizeof(void *) ||
      tree->max_children > SIZE_MAX / sizeof(turbo_bplus_tree_node_t *))
    return NULL;
  node = (turbo_bplus_tree_node_t *)calloc(1u, sizeof(*node));
  if (node == NULL) return NULL;
  node->keys = (void **)calloc(tree->max_keys, sizeof(void *));
  node->values = (void **)calloc(tree->max_keys, sizeof(void *));
  node->links = (turbo_bplus_tree_entry_link_t **)calloc(
      tree->max_keys, sizeof(*node->links));
  node->children = (turbo_bplus_tree_node_t **)calloc(
      tree->max_children, sizeof(*node->children));
  if (node->keys == NULL || node->values == NULL || node->links == NULL ||
      node->children == NULL) {
    free(node->children);
    free(node->links);
    free(node->values);
    free(node->keys);
    free(node);
    return NULL;
  }
  node->is_leaf = leaf;
  return node;
}

static void turbo_bplus_node_destroy(const turbo_bplus_tree_t *tree,
                                     turbo_bplus_tree_node_t *node) {
  size_t index;
  if (node == NULL) return;
  if (node->is_leaf) {
    for (index = 0u; index < node->num_keys; ++index) {
      turbo_bplus_destroy_object(tree->key_type, node->keys[index]);
      turbo_bplus_destroy_object(tree->value_type, node->values[index]);
      free(node->links[index]);
    }
  } else {
    for (index = 0u; index <= node->num_keys; ++index)
      turbo_bplus_node_destroy(tree, node->children[index]);
  }
  free(node->children);
  free(node->links);
  free(node->values);
  free(node->keys);
  free(node);
}

static void turbo_bplus_node_free_shell(turbo_bplus_tree_node_t *node) {
  if (node == NULL) return;
  free(node->children);
  free(node->links);
  free(node->values);
  free(node->keys);
  free(node);
}

static turbo_bplus_tree_entry_link_t *turbo_bplus_link_new(void *key,
                                                            void *value) {
  turbo_bplus_tree_entry_link_t *link =
      (turbo_bplus_tree_entry_link_t *)calloc(1u, sizeof(*link));
  if (link != NULL) {
    link->key = key;
    link->value = value;
  }
  return link;
}

static void turbo_bplus_link_insert_before(
    turbo_bplus_tree_t *tree, turbo_bplus_tree_entry_link_t *next,
    turbo_bplus_tree_entry_link_t *link) {
  link->next = next;
  link->previous = next != NULL ? next->previous : tree->last;
  if (link->previous != NULL)
    link->previous->next = link;
  else
    tree->first = link;
  if (next != NULL)
    next->previous = link;
  else
    tree->last = link;
}

static void turbo_bplus_link_unlink(turbo_bplus_tree_t *tree,
                                    turbo_bplus_tree_entry_link_t *link) {
  if (link->previous != NULL)
    link->previous->next = link->next;
  else
    tree->first = link->next;
  if (link->next != NULL)
    link->next->previous = link->previous;
  else
    tree->last = link->previous;
}

static size_t turbo_bplus_leaf_lower_bound(const turbo_bplus_tree_t *tree,
                                           const turbo_bplus_tree_node_t *leaf,
                                           const void *key) {
  size_t left = 0u;
  size_t right = leaf->num_keys;
  while (left < right) {
    size_t middle = left + (right - left) / 2u;
    if (turbo_bplus_compare_key(tree, leaf->keys[middle], key) < 0)
      left = middle + 1u;
    else
      right = middle;
  }
  return left;
}

static size_t turbo_bplus_child_index(const turbo_bplus_tree_t *tree,
                                      const turbo_bplus_tree_node_t *node,
                                      const void *key) {
  size_t index = 0u;
  while (index < node->num_keys &&
         turbo_bplus_compare_key(tree, key, node->keys[index]) >= 0)
    ++index;
  return index;
}

static void turbo_bplus_refresh_node(turbo_bplus_tree_t *tree,
                                     turbo_bplus_tree_node_t *node) {
  size_t index;
  if (node == NULL) return;
  ++tree->maintenance_node_visits;
  if (node->is_leaf) {
    node->first_key = node->num_keys == 0u ? NULL : node->keys[0];
    return;
  }
  node->first_key = node->children[0] == NULL
                        ? NULL
                        : node->children[0]->first_key;
  if (node->children[0] != NULL) ++tree->maintenance_node_visits;
  for (index = 0u; index < node->num_keys; ++index) {
    turbo_bplus_tree_node_t *child = node->children[index + 1u];
    if (child != NULL) ++tree->maintenance_node_visits;
    node->keys[index] = child == NULL ? NULL : child->first_key;
  }
}

static void turbo_bplus_refresh_ancestors(turbo_bplus_tree_t *tree,
                                          turbo_bplus_tree_node_t *node) {
  while (node != NULL) {
    turbo_bplus_refresh_node(tree, node);
    node = node->parent;
  }
}

static void turbo_bplus_parent_insert(turbo_bplus_tree_node_t *parent,
                                      size_t child_index, void *separator,
                                      turbo_bplus_tree_node_t *right) {
  size_t index;
  for (index = parent->num_keys; index > child_index; --index)
    parent->keys[index] = parent->keys[index - 1u];
  for (index = parent->num_keys + 1u; index > child_index + 1u; --index)
    parent->children[index] = parent->children[index - 1u];
  parent->keys[child_index] = separator;
  parent->children[child_index + 1u] = right;
  right->parent = parent;
  ++parent->num_keys;
}

static void turbo_bplus_split_child(
    turbo_bplus_tree_t *tree, turbo_bplus_tree_node_t *parent,
    size_t child_index, turbo_bplus_tree_node_t *right) {
  turbo_bplus_tree_node_t *left = parent->children[child_index];
  size_t index;
  right->is_leaf = left->is_leaf;
  right->parent = parent;
  if (left->is_leaf) {
    size_t split = tree->min_degree;
    right->num_keys = left->num_keys - split;
    for (index = 0u; index < right->num_keys; ++index) {
      right->keys[index] = left->keys[split + index];
      right->values[index] = left->values[split + index];
      right->links[index] = left->links[split + index];
      left->keys[split + index] = NULL;
      left->values[split + index] = NULL;
      left->links[split + index] = NULL;
    }
    left->num_keys = split;
    right->next = left->next;
    left->next = right;
    turbo_bplus_refresh_node(tree, left);
    turbo_bplus_refresh_node(tree, right);
    turbo_bplus_parent_insert(parent, child_index, right->keys[0], right);
  } else {
    size_t median = tree->min_degree - 1u;
    right->num_keys = left->num_keys - median - 1u;
    for (index = 0u; index < right->num_keys; ++index) {
      right->keys[index] = left->keys[median + 1u + index];
      left->keys[median + 1u + index] = NULL;
    }
    for (index = 0u; index <= right->num_keys; ++index) {
      right->children[index] = left->children[median + 1u + index];
      left->children[median + 1u + index] = NULL;
      right->children[index]->parent = right;
    }
    left->keys[median] = NULL;
    left->num_keys = median;
    turbo_bplus_refresh_node(tree, left);
    turbo_bplus_refresh_node(tree, right);
    turbo_bplus_parent_insert(parent, child_index, right->first_key, right);
  }
  turbo_bplus_refresh_node(tree, parent);
}

typedef struct turbo_bplus_node_pool {
  turbo_bplus_tree_node_t **nodes;
  size_t count;
  size_t used;
} turbo_bplus_node_pool;

static void turbo_bplus_pool_destroy(turbo_bplus_node_pool *pool) {
  size_t index;
  if (pool == NULL) return;
  for (index = pool->used; index < pool->count; ++index)
    turbo_bplus_node_free_shell(pool->nodes[index]);
  free(pool->nodes);
  memset(pool, 0, sizeof(*pool));
}

static turbo_stl_status turbo_bplus_pool_prepare(
    const turbo_bplus_tree_t *tree, turbo_bplus_node_pool *pool) {
  const turbo_bplus_tree_node_t *node = tree->root;
  size_t levels = 0u;
  size_t index;
  while (node != NULL) {
    ++levels;
    node = node->is_leaf ? NULL : node->children[0];
  }
  if (levels > SIZE_MAX - 2u) return TURBO_STL_CAPACITY_EXCEEDED;
  pool->count = levels + 2u;
  if (pool->count > SIZE_MAX / sizeof(*pool->nodes))
    return TURBO_STL_CAPACITY_EXCEEDED;
  pool->nodes = (turbo_bplus_tree_node_t **)calloc(pool->count,
                                                   sizeof(*pool->nodes));
  if (pool->nodes == NULL) return TURBO_STL_OUT_OF_MEMORY;
  for (index = 0u; index < pool->count; ++index) {
    pool->nodes[index] = turbo_bplus_node_new(tree, true);
    if (pool->nodes[index] == NULL) {
      turbo_bplus_pool_destroy(pool);
      return TURBO_STL_OUT_OF_MEMORY;
    }
  }
  return TURBO_STL_OK;
}

static turbo_bplus_tree_node_t *turbo_bplus_pool_take(
    turbo_bplus_node_pool *pool, bool leaf) {
  turbo_bplus_tree_node_t *node = pool->nodes[pool->used++];
  node->is_leaf = leaf;
  return node;
}

static void turbo_bplus_insert_prepared(
    turbo_bplus_tree_t *tree, void *key, void *value,
    turbo_bplus_tree_entry_link_t *link, turbo_bplus_node_pool *pool) {
  turbo_bplus_tree_node_t *node;
  turbo_bplus_tree_entry_link_t *next_link;
  size_t index;
  if (tree->root == NULL) {
    node = turbo_bplus_pool_take(pool, true);
    node->keys[0] = key;
    node->values[0] = value;
    node->links[0] = link;
    node->num_keys = 1u;
    turbo_bplus_refresh_node(tree, node);
    tree->root = node;
  } else {
    if (tree->root->num_keys == tree->max_keys) {
      node = turbo_bplus_pool_take(pool, false);
      node->children[0] = tree->root;
      tree->root->parent = node;
      tree->root = node;
      turbo_bplus_split_child(
          tree, node, 0u,
          turbo_bplus_pool_take(pool, node->children[0]->is_leaf));
    }
    node = tree->root;
    while (!node->is_leaf) {
      index = turbo_bplus_child_index(tree, node, key);
      if (node->children[index]->num_keys == tree->max_keys) {
        turbo_bplus_split_child(
            tree, node, index,
            turbo_bplus_pool_take(pool, node->children[index]->is_leaf));
        if (turbo_bplus_compare_key(tree, key, node->keys[index]) >= 0)
          ++index;
      }
      node = node->children[index];
    }
    index = turbo_bplus_leaf_lower_bound(tree, node, key);
    next_link = index < node->num_keys ? node->links[index]
                                      : (node->next != NULL &&
                                                 node->next->num_keys != 0u
                                             ? node->next->links[0]
                                             : NULL);
    if (index < node->num_keys) {
      memmove(node->keys + index + 1u, node->keys + index,
              (node->num_keys - index) * sizeof(void *));
      memmove(node->values + index + 1u, node->values + index,
              (node->num_keys - index) * sizeof(void *));
      memmove(node->links + index + 1u, node->links + index,
              (node->num_keys - index) * sizeof(*node->links));
    }
    node->keys[index] = key;
    node->values[index] = value;
    node->links[index] = link;
    ++node->num_keys;
    turbo_bplus_refresh_node(tree, node);
    turbo_bplus_link_insert_before(tree, next_link, link);
    turbo_bplus_refresh_ancestors(tree, node->parent);
    ++tree->size;
    return;
  }
  turbo_bplus_link_insert_before(tree, NULL, link);
  ++tree->size;
}

static turbo_stl_status turbo_bplus_init_common(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, turbo_bplus_tree_compare_fn compare,
    void *compare_ctx) {
  size_t key_stride;
  size_t value_stride;
  uint64_t generation;
  turbo_stl_status status;
  if (tree == NULL || tree->initialized || min_degree < 2u ||
      (key_type == NULL && compare == NULL))
    return TURBO_STL_INVALID_ARGUMENT;
  if (min_degree > SIZE_MAX / 2u) return TURBO_STL_CAPACITY_EXCEEDED;
  status = turbo_sequence_stride(key_size, key_align, &key_stride);
  if (status != TURBO_STL_OK) return status;
  status = turbo_sequence_stride(value_size, value_align, &value_stride);
  if (status != TURBO_STL_OK) return status;
  if ((2u * min_degree - 1u) > SIZE_MAX / sizeof(void *) ||
      (2u * min_degree) > SIZE_MAX / sizeof(turbo_bplus_tree_node_t *))
    return TURBO_STL_CAPACITY_EXCEEDED;
  generation = tree->generation + UINT64_C(1);
  memset(tree, 0, sizeof(*tree));
  tree->key_size = key_size;
  tree->key_align = key_align;
  tree->key_stride = key_stride;
  tree->value_size = value_size;
  tree->value_align = value_align;
  tree->value_stride = value_stride;
  tree->min_degree = min_degree;
  tree->max_children = 2u * min_degree;
  tree->max_keys = tree->max_children - 1u;
  tree->entry_limit = entry_limit;
  tree->key_type = key_type;
  tree->value_type = value_type;
  tree->compare = compare;
  tree->compare_ctx = compare_ctx;
  tree->generation = generation;
  tree->initialized = true;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_bplus_tree_init_with_order(
    turbo_bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit) {
  turbo_stl_status status;
  if (key_type == NULL || value_type == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(key_type, true);
  if (status != TURBO_STL_OK) return status;
  status = turbo_sequence_require_type(value_type, false);
  if (status != TURBO_STL_OK) return status;
  return turbo_bplus_init_common(
      tree, key_type->size, key_type->align, value_type->size,
      value_type->align, min_degree, entry_limit, key_type, value_type, NULL,
      NULL);
}

turbo_stl_status turbo_bplus_tree_init(turbo_bplus_tree_t *tree,
                                       const cmeta_type_desc *key_type,
                                       const cmeta_type_desc *value_type,
                                       size_t entry_limit) {
  return turbo_bplus_tree_init_with_order(
      tree, key_type, value_type, TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE,
      entry_limit);
}

turbo_stl_status turbo_bplus_tree_init_bytes_with_order(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, turbo_bplus_tree_compare_fn compare,
    void *compare_ctx) {
  return turbo_bplus_init_common(tree, key_size, key_align, value_size,
                                 value_align, min_degree, entry_limit, NULL,
                                 NULL, compare, compare_ctx);
}

turbo_stl_status turbo_bplus_tree_init_bytes(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  return turbo_bplus_tree_init_bytes_with_order(
      tree, key_size, key_align, value_size, value_align,
      TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE, entry_limit, compare, compare_ctx);
}

static turbo_bplus_tree_node_t *turbo_bplus_leftmost(
    const turbo_bplus_tree_t *tree) {
  turbo_bplus_tree_node_t *node = tree->root;
  while (node != NULL && !node->is_leaf) node = node->children[0];
  return node;
}

static bool turbo_bplus_pair_at(const turbo_bplus_tree_t *tree, size_t index,
                                const void **out_key,
                                const void **out_value) {
  turbo_bplus_tree_node_t *leaf;
  if (!turbo_bplus_valid(tree) || index >= tree->size || out_key == NULL ||
      out_value == NULL)
    return false;
  leaf = turbo_bplus_leftmost(tree);
  while (leaf != NULL) {
    if (index < leaf->num_keys) {
      *out_key = leaf->keys[index];
      *out_value = leaf->values[index];
      return true;
    }
    index -= leaf->num_keys;
    leaf = leaf->next;
  }
  return false;
}

static turbo_bplus_tree_node_t *turbo_bplus_find_leaf(
    const turbo_bplus_tree_t *tree, const void *key) {
  turbo_bplus_tree_node_t *node;
  if (!turbo_bplus_valid(tree) || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL && !node->is_leaf)
    node = node->children[turbo_bplus_child_index(tree, node, key)];
  return node;
}

static const void *turbo_bplus_find_value(const turbo_bplus_tree_t *tree,
                                          const void *key) {
  turbo_bplus_tree_node_t *leaf = turbo_bplus_find_leaf(tree, key);
  size_t index;
  if (leaf == NULL) return NULL;
  index = turbo_bplus_leaf_lower_bound(tree, leaf, key);
  if (index >= leaf->num_keys ||
      turbo_bplus_compare_key(tree, leaf->keys[index], key) != 0)
    return NULL;
  return leaf->values[index];
}

turbo_stl_status turbo_bplus_tree_put(turbo_bplus_tree_t *tree,
                                      const void *key, const void *value) {
  turbo_bplus_node_pool pool = {0};
  turbo_bplus_tree_node_t *leaf;
  turbo_bplus_tree_entry_link_t *link;
  void *key_copy = NULL;
  void *value_copy = NULL;
  size_t index;
  turbo_stl_status status;
  if (!turbo_bplus_valid(tree) || key == NULL || value == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  leaf = turbo_bplus_find_leaf(tree, key);
  if (leaf != NULL) {
    index = turbo_bplus_leaf_lower_bound(tree, leaf, key);
    if (index < leaf->num_keys &&
        turbo_bplus_compare_key(tree, leaf->keys[index], key) == 0) {
      status = turbo_bplus_copy_object(
          tree->value_type, tree->value_size, tree->value_stride,
          tree->value_align, value, &value_copy);
      if (status != TURBO_STL_OK) return status;
      turbo_bplus_destroy_object(tree->value_type, leaf->values[index]);
      leaf->values[index] = value_copy;
      leaf->links[index]->value = value_copy;
      ++tree->generation;
      return TURBO_STL_OK;
    }
  }
  if (tree->size >= tree->entry_limit) return TURBO_STL_CAPACITY_EXCEEDED;
  status = turbo_bplus_copy_object(
      tree->key_type, tree->key_size, tree->key_stride, tree->key_align, key,
      &key_copy);
  if (status != TURBO_STL_OK) return status;
  status = turbo_bplus_copy_object(tree->value_type, tree->value_size,
                                   tree->value_stride, tree->value_align,
                                   value, &value_copy);
  if (status != TURBO_STL_OK) {
    turbo_bplus_destroy_object(tree->key_type, key_copy);
    return status;
  }
  link = turbo_bplus_link_new(key_copy, value_copy);
  if (link == NULL) {
    turbo_bplus_destroy_object(tree->value_type, value_copy);
    turbo_bplus_destroy_object(tree->key_type, key_copy);
    return TURBO_STL_OUT_OF_MEMORY;
  }
  status = turbo_bplus_pool_prepare(tree, &pool);
  if (status != TURBO_STL_OK) {
    free(link);
    turbo_bplus_destroy_object(tree->value_type, value_copy);
    turbo_bplus_destroy_object(tree->key_type, key_copy);
    return status;
  }
  turbo_bplus_insert_prepared(tree, key_copy, value_copy, link, &pool);
  turbo_bplus_pool_destroy(&pool);
  ++tree->generation;
  return TURBO_STL_OK;
}

void *turbo_bplus_tree_get(turbo_bplus_tree_t *tree, const void *key) {
  return (void *)turbo_bplus_find_value(tree, key);
}

const void *turbo_bplus_tree_get_const(const turbo_bplus_tree_t *tree,
                                       const void *key) {
  return turbo_bplus_find_value(tree, key);
}

bool turbo_bplus_tree_contains(const turbo_bplus_tree_t *tree,
                               const void *key) {
  return turbo_bplus_find_value(tree, key) != NULL;
}

static size_t turbo_bplus_parent_child_index(
    const turbo_bplus_tree_node_t *parent,
    const turbo_bplus_tree_node_t *child) {
  size_t index;
  for (index = 0u; index <= parent->num_keys; ++index)
    if (parent->children[index] == child) return index;
  return parent->num_keys + 1u;
}

static void turbo_bplus_leaf_remove_slot(turbo_bplus_tree_node_t *leaf,
                                         size_t index) {
  if (index + 1u < leaf->num_keys) {
    memmove(leaf->keys + index, leaf->keys + index + 1u,
            (leaf->num_keys - index - 1u) * sizeof(void *));
    memmove(leaf->values + index, leaf->values + index + 1u,
            (leaf->num_keys - index - 1u) * sizeof(void *));
    memmove(leaf->links + index, leaf->links + index + 1u,
            (leaf->num_keys - index - 1u) * sizeof(*leaf->links));
  }
  --leaf->num_keys;
  leaf->keys[leaf->num_keys] = NULL;
  leaf->values[leaf->num_keys] = NULL;
  leaf->links[leaf->num_keys] = NULL;
  leaf->first_key = leaf->num_keys == 0u ? NULL : leaf->keys[0];
}

static void turbo_bplus_parent_remove_child(turbo_bplus_tree_t *tree,
                                            turbo_bplus_tree_node_t *parent,
                                            size_t child_index) {
  if (child_index < parent->num_keys)
    memmove(parent->children + child_index,
            parent->children + child_index + 1u,
            (parent->num_keys - child_index) * sizeof(*parent->children));
  --parent->num_keys;
  parent->children[parent->num_keys + 1u] = NULL;
  turbo_bplus_refresh_node(tree, parent);
}

static void turbo_bplus_rebalance_after_remove(turbo_bplus_tree_t *tree,
                                                turbo_bplus_tree_node_t *node) {
  size_t minimum = tree->min_degree - 1u;
  while (node != tree->root && node->num_keys < minimum) {
    turbo_bplus_tree_node_t *parent = node->parent;
    size_t child_index = turbo_bplus_parent_child_index(parent, node);
    turbo_bplus_tree_node_t *left =
        child_index > 0u ? parent->children[child_index - 1u] : NULL;
    turbo_bplus_tree_node_t *right =
        child_index < parent->num_keys ? parent->children[child_index + 1u]
                                       : NULL;
    size_t index;
    if (node->is_leaf) {
      if (left != NULL && left->num_keys > minimum) {
        memmove(node->keys + 1u, node->keys,
                node->num_keys * sizeof(void *));
        memmove(node->values + 1u, node->values,
                node->num_keys * sizeof(void *));
        memmove(node->links + 1u, node->links,
                node->num_keys * sizeof(*node->links));
        node->keys[0] = left->keys[left->num_keys - 1u];
        node->values[0] = left->values[left->num_keys - 1u];
        node->links[0] = left->links[left->num_keys - 1u];
        left->keys[left->num_keys - 1u] = NULL;
        left->values[left->num_keys - 1u] = NULL;
        left->links[left->num_keys - 1u] = NULL;
        --left->num_keys;
        ++node->num_keys;
        turbo_bplus_refresh_node(tree, left);
        turbo_bplus_refresh_ancestors(tree, node);
        return;
      }
      if (right != NULL && right->num_keys > minimum) {
        node->keys[node->num_keys] = right->keys[0];
        node->values[node->num_keys] = right->values[0];
        node->links[node->num_keys] = right->links[0];
        ++node->num_keys;
        turbo_bplus_leaf_remove_slot(right, 0u);
        turbo_bplus_refresh_ancestors(tree, node);
        return;
      }
      if (left != NULL) {
        for (index = 0u; index < node->num_keys; ++index) {
          left->keys[left->num_keys + index] = node->keys[index];
          left->values[left->num_keys + index] = node->values[index];
          left->links[left->num_keys + index] = node->links[index];
        }
        left->num_keys += node->num_keys;
        left->next = node->next;
        turbo_bplus_refresh_node(tree, left);
        turbo_bplus_parent_remove_child(tree, parent, child_index);
        turbo_bplus_node_free_shell(node);
      } else {
        for (index = 0u; index < right->num_keys; ++index) {
          node->keys[node->num_keys + index] = right->keys[index];
          node->values[node->num_keys + index] = right->values[index];
          node->links[node->num_keys + index] = right->links[index];
        }
        node->num_keys += right->num_keys;
        node->next = right->next;
        turbo_bplus_refresh_node(tree, node);
        turbo_bplus_parent_remove_child(tree, parent, child_index + 1u);
        turbo_bplus_node_free_shell(right);
      }
    } else {
      if (left != NULL && left->num_keys > minimum) {
        memmove(node->children + 1u, node->children,
                (node->num_keys + 1u) * sizeof(*node->children));
        node->children[0] = left->children[left->num_keys];
        node->children[0]->parent = node;
        left->children[left->num_keys] = NULL;
        --left->num_keys;
        ++node->num_keys;
        turbo_bplus_refresh_node(tree, left);
        turbo_bplus_refresh_node(tree, node);
        turbo_bplus_refresh_ancestors(tree, parent);
        return;
      }
      if (right != NULL && right->num_keys > minimum) {
        node->children[node->num_keys + 1u] = right->children[0];
        node->children[node->num_keys + 1u]->parent = node;
        memmove(right->children, right->children + 1u,
                right->num_keys * sizeof(*right->children));
        right->children[right->num_keys] = NULL;
        --right->num_keys;
        ++node->num_keys;
        turbo_bplus_refresh_node(tree, right);
        turbo_bplus_refresh_node(tree, node);
        turbo_bplus_refresh_ancestors(tree, parent);
        return;
      }
      if (left != NULL) {
        size_t left_children = left->num_keys + 1u;
        for (index = 0u; index <= node->num_keys; ++index) {
          left->children[left_children + index] = node->children[index];
          node->children[index]->parent = left;
        }
        left->num_keys += node->num_keys + 1u;
        turbo_bplus_refresh_node(tree, left);
        turbo_bplus_parent_remove_child(tree, parent, child_index);
        turbo_bplus_node_free_shell(node);
      } else {
        size_t node_children = node->num_keys + 1u;
        for (index = 0u; index <= right->num_keys; ++index) {
          node->children[node_children + index] = right->children[index];
          right->children[index]->parent = node;
        }
        node->num_keys += right->num_keys + 1u;
        turbo_bplus_refresh_node(tree, node);
        turbo_bplus_parent_remove_child(tree, parent, child_index + 1u);
        turbo_bplus_node_free_shell(right);
      }
    }
    node = parent;
  }
  if (tree->root != NULL && !tree->root->is_leaf &&
      tree->root->num_keys == 0u) {
    turbo_bplus_tree_node_t *old_root = tree->root;
    tree->root = old_root->children[0];
    tree->root->parent = NULL;
    old_root->children[0] = NULL;
    turbo_bplus_node_free_shell(old_root);
    turbo_bplus_refresh_ancestors(tree, tree->root);
    return;
  }
  turbo_bplus_refresh_ancestors(tree, node);
}

turbo_stl_status turbo_bplus_tree_remove(turbo_bplus_tree_t *tree,
                                         const void *key, void *out_value) {
  turbo_bplus_tree_node_t *leaf;
  turbo_bplus_tree_entry_link_t *removed_link;
  void *removed_key;
  void *removed_value;
  size_t index;
  if (!turbo_bplus_valid(tree) || key == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  leaf = turbo_bplus_find_leaf(tree, key);
  if (leaf == NULL) return TURBO_STL_NOT_FOUND;
  index = turbo_bplus_leaf_lower_bound(tree, leaf, key);
  if (index >= leaf->num_keys ||
      turbo_bplus_compare_key(tree, leaf->keys[index], key) != 0)
    return TURBO_STL_NOT_FOUND;
  removed_key = leaf->keys[index];
  removed_value = leaf->values[index];
  removed_link = leaf->links[index];
  turbo_bplus_leaf_remove_slot(leaf, index);
  if (leaf == tree->root && leaf->num_keys == 0u) {
    turbo_bplus_node_free_shell(leaf);
    tree->root = NULL;
  } else {
    turbo_bplus_rebalance_after_remove(tree, leaf);
  }
  turbo_bplus_link_unlink(tree, removed_link);
  if (out_value != NULL) {
    if (tree->value_type != NULL)
      tree->value_type->traits->move_construct(out_value, removed_value);
    else
      memcpy(out_value, removed_value, tree->value_size);
  }
  turbo_bplus_destroy_object(tree->key_type, removed_key);
  turbo_bplus_destroy_object(tree->value_type, removed_value);
  free(removed_link);
  --tree->size;
  ++tree->generation;
  return TURBO_STL_OK;
}

void turbo_bplus_tree_clear(turbo_bplus_tree_t *tree) {
  if (!turbo_bplus_valid(tree) || tree->root == NULL) return;
  turbo_bplus_node_destroy(tree, tree->root);
  tree->root = NULL;
  tree->size = 0u;
  tree->first = NULL;
  tree->last = NULL;
  ++tree->generation;
}

void turbo_bplus_tree_destroy(turbo_bplus_tree_t *tree) {
  uint64_t generation;
  if (tree == NULL) return;
  generation = tree->generation;
  if (tree->initialized) {
    turbo_bplus_node_destroy(tree, tree->root);
    ++generation;
  }
  memset(tree, 0, sizeof(*tree));
  tree->generation = generation;
}

turbo_stl_status turbo_bplus_tree_reserve(turbo_bplus_tree_t *tree,
                                          size_t min_capacity) {
  if (!turbo_bplus_valid(tree)) return TURBO_STL_INVALID_ARGUMENT;
  return min_capacity <= tree->entry_limit ? TURBO_STL_OK
                                            : TURBO_STL_CAPACITY_EXCEEDED;
}

size_t turbo_bplus_tree_size(const turbo_bplus_tree_t *tree) {
  return tree == NULL ? 0u : tree->size;
}

size_t turbo_bplus_tree_capacity(const turbo_bplus_tree_t *tree) {
  return tree == NULL ? 0u : tree->size;
}

size_t turbo_bplus_tree_entry_limit(const turbo_bplus_tree_t *tree) {
  return tree == NULL ? 0u : tree->entry_limit;
}

uint64_t turbo_bplus_tree_generation(const turbo_bplus_tree_t *tree) {
  return tree == NULL ? UINT64_C(0) : tree->generation;
}

bool turbo_bplus_tree_empty(const turbo_bplus_tree_t *tree) {
  return tree == NULL || tree->size == 0u;
}

void *turbo_bplus_tree_key_at(turbo_bplus_tree_t *tree, size_t index) {
  const void *key;
  const void *value;
  return turbo_bplus_pair_at(tree, index, &key, &value) ? (void *)key : NULL;
}

const void *turbo_bplus_tree_key_at_const(const turbo_bplus_tree_t *tree,
                                          size_t index) {
  const void *key;
  const void *value;
  return turbo_bplus_pair_at(tree, index, &key, &value) ? key : NULL;
}

void *turbo_bplus_tree_value_at(turbo_bplus_tree_t *tree, size_t index) {
  const void *key;
  const void *value;
  return turbo_bplus_pair_at(tree, index, &key, &value) ? (void *)value : NULL;
}

const void *turbo_bplus_tree_value_at_const(const turbo_bplus_tree_t *tree,
                                            size_t index) {
  const void *key;
  const void *value;
  return turbo_bplus_pair_at(tree, index, &key, &value) ? value : NULL;
}

bool turbo_bplus_tree_range_next(const turbo_bplus_tree_t *tree,
                                 cmeta_range_cursor *cursor,
                                 const void **out_key,
                                 const void **out_value) {
  turbo_bplus_tree_entry_link_t *link;
  if (!turbo_bplus_valid(tree) || cursor == NULL || out_key == NULL ||
      out_value == NULL)
    return false;
  if (cursor->state[1] == NULL) {
    cursor->state[1] = (void *)tree;
    link = tree->first;
  } else {
    if (cursor->state[1] != (void *)tree) return false;
    link = (turbo_bplus_tree_entry_link_t *)cursor->state[0];
  }
  if (link == NULL) return false;
  *out_key = link->key;
  *out_value = link->value;
  cursor->state[0] = link->next;
  return true;
}

static turbo_stl_status turbo_bplus_from_arrays_common(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  turbo_bplus_tree_t next = {0};
  size_t index;
  uint64_t generation;
  turbo_stl_status status;
  if (tree == NULL || (count != 0u && (keys == NULL || values == NULL)))
    return TURBO_STL_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return TURBO_STL_CAPACITY_EXCEEDED;
  if (key_type != NULL)
    status = turbo_bplus_tree_init(&next, key_type, value_type, entry_limit);
  else
    status = turbo_bplus_tree_init_bytes(
        &next, key_size, key_align, value_size, value_align, entry_limit,
        compare, compare_ctx);
  if (status != TURBO_STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_bplus_tree_put(
        &next, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != TURBO_STL_OK) {
      turbo_bplus_tree_destroy(&next);
      return status;
    }
  }
  generation = tree->generation + UINT64_C(1);
  if (tree->initialized) turbo_bplus_node_destroy(tree, tree->root);
  *tree = next;
  tree->generation = generation;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_bplus_tree_from_arrays(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit) {
  if (key_type == NULL || value_type == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_bplus_from_arrays_common(
      tree, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, entry_limit, NULL,
      NULL);
}

turbo_stl_status turbo_bplus_tree_from_arrays_bytes(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  return turbo_bplus_from_arrays_common(
      tree, keys, values, count, NULL, NULL, key_size, key_align, value_size,
      value_align, entry_limit, compare, compare_ctx);
}
