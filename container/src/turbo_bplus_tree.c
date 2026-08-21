#include <turbo/container/bplus_tree.h>

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

static container_status turbo_bplus_copy_object(
    const cmeta_type_desc *type, size_t size, size_t stride, size_t alignment,
    const void *source, void **out_object) {
  container_status status;
  if (source == NULL || out_object == NULL) return CONTAINER_INVALID_ARGUMENT;
  *out_object = NULL;
  status = turbo_sequence_allocate(1u, stride, alignment, out_object);
  if (status != CONTAINER_OK) return status;
  if (type != NULL) {
    if (!type->traits->copy_construct(*out_object, source)) {
      turbo_sequence_deallocate(*out_object);
      *out_object = NULL;
      return CONTAINER_OUT_OF_MEMORY;
    }
  } else {
    memcpy(*out_object, source, size);
  }
  return CONTAINER_OK;
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
  node->children = (turbo_bplus_tree_node_t **)calloc(
      tree->max_children, sizeof(*node->children));
  if (node->keys == NULL || node->values == NULL || node->children == NULL) {
    free(node->children);
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
    }
  } else {
    for (index = 0u; index <= node->num_keys; ++index)
      turbo_bplus_node_destroy(tree, node->children[index]);
  }
  free(node->children);
  free(node->values);
  free(node->keys);
  free(node);
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

static void *turbo_bplus_refresh_separators(turbo_bplus_tree_node_t *node) {
  size_t index;
  void *first;
  if (node->is_leaf) return node->num_keys == 0u ? NULL : node->keys[0];
  first = turbo_bplus_refresh_separators(node->children[0]);
  for (index = 0u; index < node->num_keys; ++index)
    node->keys[index] = turbo_bplus_refresh_separators(
        node->children[index + 1u]);
  return first;
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

static container_status turbo_bplus_split_child(
    turbo_bplus_tree_t *tree, turbo_bplus_tree_node_t *parent,
    size_t child_index) {
  turbo_bplus_tree_node_t *left = parent->children[child_index];
  turbo_bplus_tree_node_t *right = turbo_bplus_node_new(tree, left->is_leaf);
  size_t index;
  if (right == NULL) return CONTAINER_OUT_OF_MEMORY;
  right->parent = parent;
  if (left->is_leaf) {
    size_t split = tree->min_degree;
    right->num_keys = left->num_keys - split;
    for (index = 0u; index < right->num_keys; ++index) {
      right->keys[index] = left->keys[split + index];
      right->values[index] = left->values[split + index];
      left->keys[split + index] = NULL;
      left->values[split + index] = NULL;
    }
    left->num_keys = split;
    right->next = left->next;
    left->next = right;
    turbo_bplus_parent_insert(parent, child_index, right->keys[0], right);
  } else {
    size_t median = tree->min_degree - 1u;
    void *promoted = left->keys[median];
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
    turbo_bplus_parent_insert(parent, child_index, promoted, right);
  }
  return CONTAINER_OK;
}

/* On success consumes key/value. On failure they remain caller-owned. */
static container_status turbo_bplus_insert_owned(turbo_bplus_tree_t *tree,
                                                  void *key, void *value) {
  turbo_bplus_tree_node_t *node;
  size_t index;
  container_status status;
  if (tree->root == NULL) {
    node = turbo_bplus_node_new(tree, true);
    if (node == NULL) return CONTAINER_OUT_OF_MEMORY;
    node->keys[0] = key;
    node->values[0] = value;
    node->num_keys = 1u;
    tree->root = node;
    ++tree->size;
    return CONTAINER_OK;
  }
  if (tree->root->num_keys == tree->max_keys) {
    node = turbo_bplus_node_new(tree, false);
    if (node == NULL) return CONTAINER_OUT_OF_MEMORY;
    node->children[0] = tree->root;
    tree->root->parent = node;
    tree->root = node;
    status = turbo_bplus_split_child(tree, node, 0u);
    if (status != CONTAINER_OK) return status;
  }
  node = tree->root;
  while (!node->is_leaf) {
    index = turbo_bplus_child_index(tree, node, key);
    if (node->children[index]->num_keys == tree->max_keys) {
      status = turbo_bplus_split_child(tree, node, index);
      if (status != CONTAINER_OK) return status;
      if (turbo_bplus_compare_key(tree, key, node->keys[index]) >= 0) ++index;
    }
    node = node->children[index];
  }
  index = turbo_bplus_leaf_lower_bound(tree, node, key);
  if (index < node->num_keys) {
    memmove(node->keys + index + 1u, node->keys + index,
            (node->num_keys - index) * sizeof(void *));
    memmove(node->values + index + 1u, node->values + index,
            (node->num_keys - index) * sizeof(void *));
  }
  node->keys[index] = key;
  node->values[index] = value;
  ++node->num_keys;
  ++tree->size;
  (void)turbo_bplus_refresh_separators(tree->root);
  return CONTAINER_OK;
}

static container_status turbo_bplus_insert_copy(turbo_bplus_tree_t *tree,
                                                 const void *key,
                                                 const void *value) {
  void *key_copy = NULL;
  void *value_copy = NULL;
  container_status status = turbo_bplus_copy_object(
      tree->key_type, tree->key_size, tree->key_stride, tree->key_align, key,
      &key_copy);
  if (status != CONTAINER_OK) return status;
  status = turbo_bplus_copy_object(tree->value_type, tree->value_size,
                                   tree->value_stride, tree->value_align,
                                   value, &value_copy);
  if (status != CONTAINER_OK) {
    turbo_bplus_destroy_object(tree->key_type, key_copy);
    return status;
  }
  status = turbo_bplus_insert_owned(tree, key_copy, value_copy);
  if (status != CONTAINER_OK) {
    turbo_bplus_destroy_object(tree->key_type, key_copy);
    turbo_bplus_destroy_object(tree->value_type, value_copy);
  }
  return status;
}

static container_status turbo_bplus_init_common(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, turbo_bplus_tree_compare_fn compare,
    void *compare_ctx) {
  size_t key_stride;
  size_t value_stride;
  uint64_t generation;
  container_status status;
  if (tree == NULL || tree->initialized || min_degree < 2u ||
      (key_type == NULL && compare == NULL))
    return CONTAINER_INVALID_ARGUMENT;
  if (min_degree > SIZE_MAX / 2u) return CONTAINER_CAPACITY_EXCEEDED;
  status = turbo_sequence_stride(key_size, key_align, &key_stride);
  if (status != CONTAINER_OK) return status;
  status = turbo_sequence_stride(value_size, value_align, &value_stride);
  if (status != CONTAINER_OK) return status;
  if ((2u * min_degree - 1u) > SIZE_MAX / sizeof(void *) ||
      (2u * min_degree) > SIZE_MAX / sizeof(turbo_bplus_tree_node_t *))
    return CONTAINER_CAPACITY_EXCEEDED;
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
  return CONTAINER_OK;
}

container_status turbo_bplus_tree_init_with_order(
    turbo_bplus_tree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit) {
  container_status status;
  if (key_type == NULL || value_type == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(key_type, true);
  if (status != CONTAINER_OK) return status;
  status = turbo_sequence_require_type(value_type, false);
  if (status != CONTAINER_OK) return status;
  return turbo_bplus_init_common(
      tree, key_type->size, key_type->align, value_type->size,
      value_type->align, min_degree, entry_limit, key_type, value_type, NULL,
      NULL);
}

container_status turbo_bplus_tree_init(turbo_bplus_tree_t *tree,
                                       const cmeta_type_desc *key_type,
                                       const cmeta_type_desc *value_type,
                                       size_t entry_limit) {
  return turbo_bplus_tree_init_with_order(
      tree, key_type, value_type, TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE,
      entry_limit);
}

container_status turbo_bplus_tree_init_bytes_with_order(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, turbo_bplus_tree_compare_fn compare,
    void *compare_ctx) {
  return turbo_bplus_init_common(tree, key_size, key_align, value_size,
                                 value_align, min_degree, entry_limit, NULL,
                                 NULL, compare, compare_ctx);
}

container_status turbo_bplus_tree_init_bytes(
    turbo_bplus_tree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  return turbo_bplus_tree_init_bytes_with_order(
      tree, key_size, key_align, value_size, value_align,
      TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE, entry_limit, compare, compare_ctx);
}

static container_status turbo_bplus_clone_config(
    const turbo_bplus_tree_t *tree, turbo_bplus_tree_t *out) {
  memset(out, 0, sizeof(*out));
  return turbo_bplus_init_common(
      out, tree->key_size, tree->key_align, tree->value_size,
      tree->value_align, tree->min_degree, tree->entry_limit, tree->key_type,
      tree->value_type, tree->compare, tree->compare_ctx);
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

static void turbo_bplus_commit(turbo_bplus_tree_t *tree,
                               turbo_bplus_tree_t *next) {
  uint64_t generation = tree->generation + UINT64_C(1);
  turbo_bplus_node_destroy(tree, tree->root);
  *tree = *next;
  tree->generation = generation;
  memset(next, 0, sizeof(*next));
}

container_status turbo_bplus_tree_put(turbo_bplus_tree_t *tree,
                                      const void *key, const void *value) {
  turbo_bplus_tree_t next;
  turbo_bplus_tree_node_t *leaf;
  bool replaced = false;
  container_status status;
  if (!turbo_bplus_valid(tree) || key == NULL || value == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (turbo_bplus_find_value(tree, key) == NULL &&
      tree->size >= tree->entry_limit)
    return CONTAINER_CAPACITY_EXCEEDED;
  status = turbo_bplus_clone_config(tree, &next);
  if (status != CONTAINER_OK) return status;
  leaf = turbo_bplus_leftmost(tree);
  while (leaf != NULL) {
    size_t index;
    for (index = 0u; index < leaf->num_keys; ++index) {
      if (!replaced &&
          turbo_bplus_compare_key(tree, leaf->keys[index], key) == 0) {
        status = turbo_bplus_insert_copy(&next, leaf->keys[index], value);
        replaced = status == CONTAINER_OK;
      } else {
        status = turbo_bplus_insert_copy(&next, leaf->keys[index],
                                         leaf->values[index]);
      }
      if (status != CONTAINER_OK) {
        turbo_bplus_tree_destroy(&next);
        return status;
      }
    }
    leaf = leaf->next;
  }
  if (!replaced) {
    status = turbo_bplus_insert_copy(&next, key, value);
    if (status != CONTAINER_OK) {
      turbo_bplus_tree_destroy(&next);
      return status;
    }
  }
  turbo_bplus_commit(tree, &next);
  return CONTAINER_OK;
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

container_status turbo_bplus_tree_remove(turbo_bplus_tree_t *tree,
                                         const void *key, void *out_value) {
  turbo_bplus_tree_t next;
  turbo_bplus_tree_node_t *leaf;
  const void *removed_value = NULL;
  container_status status;
  if (!turbo_bplus_valid(tree) || key == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (!turbo_bplus_tree_contains(tree, key)) return CONTAINER_NOT_FOUND;
  status = turbo_bplus_clone_config(tree, &next);
  if (status != CONTAINER_OK) return status;
  leaf = turbo_bplus_leftmost(tree);
  while (leaf != NULL) {
    size_t index;
    for (index = 0u; index < leaf->num_keys; ++index) {
      if (removed_value == NULL &&
          turbo_bplus_compare_key(tree, leaf->keys[index], key) == 0) {
        removed_value = leaf->values[index];
        continue;
      }
      status = turbo_bplus_insert_copy(&next, leaf->keys[index],
                                       leaf->values[index]);
      if (status != CONTAINER_OK) {
        turbo_bplus_tree_destroy(&next);
        return status;
      }
    }
    leaf = leaf->next;
  }
  if (out_value != NULL) {
    if (tree->value_type != NULL)
      tree->value_type->traits->move_construct(out_value,
                                               (void *)removed_value);
    else
      memcpy(out_value, removed_value, tree->value_size);
  }
  turbo_bplus_commit(tree, &next);
  return CONTAINER_OK;
}

void turbo_bplus_tree_clear(turbo_bplus_tree_t *tree) {
  if (!turbo_bplus_valid(tree) || tree->root == NULL) return;
  turbo_bplus_node_destroy(tree, tree->root);
  tree->root = NULL;
  tree->size = 0u;
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

container_status turbo_bplus_tree_reserve(turbo_bplus_tree_t *tree,
                                          size_t min_capacity) {
  if (!turbo_bplus_valid(tree)) return CONTAINER_INVALID_ARGUMENT;
  return min_capacity <= tree->entry_limit ? CONTAINER_OK
                                            : CONTAINER_CAPACITY_EXCEEDED;
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

static container_status turbo_bplus_from_arrays_common(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  turbo_bplus_tree_t next = {0};
  size_t index;
  uint64_t generation;
  container_status status;
  if (tree == NULL || (count != 0u && (keys == NULL || values == NULL)) ||
      count > entry_limit)
    return count > entry_limit ? CONTAINER_CAPACITY_EXCEEDED
                               : CONTAINER_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return CONTAINER_CAPACITY_EXCEEDED;
  if (key_type != NULL)
    status = turbo_bplus_tree_init(&next, key_type, value_type, entry_limit);
  else
    status = turbo_bplus_tree_init_bytes(
        &next, key_size, key_align, value_size, value_align, entry_limit,
        compare, compare_ctx);
  if (status != CONTAINER_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_bplus_tree_put(
        &next, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != CONTAINER_OK) {
      turbo_bplus_tree_destroy(&next);
      return status;
    }
  }
  generation = tree->generation + UINT64_C(1);
  if (tree->initialized) turbo_bplus_node_destroy(tree, tree->root);
  *tree = next;
  tree->generation = generation;
  return CONTAINER_OK;
}

container_status turbo_bplus_tree_from_arrays(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t entry_limit) {
  if (key_type == NULL || value_type == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_bplus_from_arrays_common(
      tree, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, entry_limit, NULL,
      NULL);
}

container_status turbo_bplus_tree_from_arrays_bytes(
    turbo_bplus_tree_t *tree, const void *keys, const void *values,
    size_t count, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit,
    turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  return turbo_bplus_from_arrays_common(
      tree, keys, values, count, NULL, NULL, key_size, key_align, value_size,
      value_align, entry_limit, compare, compare_ctx);
}
