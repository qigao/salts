#include <turbo/container/btree.h>

#include "turbo_sequence_internal.h"

#include <string.h>

static bool turbo_btree_valid(const turbo_btree_t *tree) {
  return tree != NULL && tree->initialized && tree->key_size != 0u &&
         tree->value_size != 0u && tree->min_degree >= 2u &&
         (tree->key_type != NULL || tree->compare != NULL);
}

static int turbo_btree_compare_key(const turbo_btree_t *tree,
                                   const void *left, const void *right) {
  return tree->key_type != NULL
             ? tree->key_type->traits->compare(left, right)
             : tree->compare(left, right, tree->compare_ctx);
}

static void turbo_btree_destroy_object(const cmeta_type_desc *type,
                                       void *object) {
  if (object == NULL) return;
  if (type != NULL) type->traits->destroy(object);
  turbo_sequence_deallocate(object);
}

static container_status turbo_btree_copy_object(
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

static turbo_btree_node_t *turbo_btree_node_new(const turbo_btree_t *tree,
                                                 bool leaf) {
  turbo_btree_node_t *node;
  if (!turbo_btree_valid(tree) ||
      tree->max_keys > SIZE_MAX / sizeof(void *) ||
      tree->max_children > SIZE_MAX / sizeof(turbo_btree_node_t *))
    return NULL;
  node = (turbo_btree_node_t *)calloc(1u, sizeof(*node));
  if (node == NULL) return NULL;
  node->keys = (void **)calloc(tree->max_keys, sizeof(void *));
  node->values = (void **)calloc(tree->max_keys, sizeof(void *));
  node->links = (turbo_btree_entry_link_t **)calloc(tree->max_keys,
                                                    sizeof(*node->links));
  node->children = (turbo_btree_node_t **)calloc(tree->max_children,
                                                 sizeof(*node->children));
  if (node->keys == NULL || node->values == NULL || node->links == NULL ||
      node->children == NULL) {
    free(node->children);
    free(node->links);
    free(node->values);
    free(node->keys);
    free(node);
    return NULL;
  }
  node->leaf = leaf;
  return node;
}

static void turbo_btree_node_destroy(const turbo_btree_t *tree,
                                     turbo_btree_node_t *node) {
  size_t index;
  if (node == NULL) return;
  if (!node->leaf) {
    for (index = 0u; index <= node->num_keys; ++index)
      turbo_btree_node_destroy(tree, node->children[index]);
  }
  for (index = 0u; index < node->num_keys; ++index) {
    turbo_btree_destroy_object(tree->key_type, node->keys[index]);
    turbo_btree_destroy_object(tree->value_type, node->values[index]);
    free(node->links[index]);
  }
  free(node->children);
  free(node->links);
  free(node->values);
  free(node->keys);
  free(node);
}

static void turbo_btree_node_free_shell(turbo_btree_node_t *node) {
  if (node == NULL) return;
  free(node->children);
  free(node->links);
  free(node->values);
  free(node->keys);
  free(node);
}

static turbo_btree_entry_link_t *turbo_btree_link_new(void *key,
                                                       void *value) {
  turbo_btree_entry_link_t *link =
      (turbo_btree_entry_link_t *)calloc(1u, sizeof(*link));
  if (link != NULL) {
    link->key = key;
    link->value = value;
  }
  return link;
}

static void turbo_btree_link_insert_before(turbo_btree_t *tree,
                                            turbo_btree_entry_link_t *next,
                                            turbo_btree_entry_link_t *link) {
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

static void turbo_btree_link_unlink(turbo_btree_t *tree,
                                     turbo_btree_entry_link_t *link) {
  if (link->previous != NULL)
    link->previous->next = link->next;
  else
    tree->first = link->next;
  if (link->next != NULL)
    link->next->previous = link->previous;
  else
    tree->last = link->previous;
}

static size_t turbo_btree_lower_bound(const turbo_btree_t *tree,
                                      const turbo_btree_node_t *node,
                                      const void *key) {
  size_t left = 0u;
  size_t right = node == NULL ? 0u : node->num_keys;
  while (left < right) {
    size_t middle = left + (right - left) / 2u;
    if (turbo_btree_compare_key(tree, node->keys[middle], key) < 0)
      left = middle + 1u;
    else
      right = middle;
  }
  return left;
}

static void turbo_btree_split_child(turbo_btree_t *tree,
                                    turbo_btree_node_t *parent,
                                    size_t child_index,
                                    turbo_btree_node_t *right) {
  turbo_btree_node_t *left = parent->children[child_index];
  size_t median = tree->min_degree - 1u;
  size_t index;

  right->leaf = left->leaf;
  right->num_keys = tree->min_degree - 1u;
  for (index = 0u; index < right->num_keys; ++index) {
    right->keys[index] = left->keys[index + tree->min_degree];
    right->values[index] = left->values[index + tree->min_degree];
    right->links[index] = left->links[index + tree->min_degree];
    left->keys[index + tree->min_degree] = NULL;
    left->values[index + tree->min_degree] = NULL;
    left->links[index + tree->min_degree] = NULL;
  }
  if (!left->leaf) {
    for (index = 0u; index < tree->min_degree; ++index) {
      right->children[index] = left->children[index + tree->min_degree];
      left->children[index + tree->min_degree] = NULL;
    }
  }
  for (index = parent->num_keys + 1u; index > child_index + 1u; --index)
    parent->children[index] = parent->children[index - 1u];
  parent->children[child_index + 1u] = right;
  for (index = parent->num_keys; index > child_index; --index) {
    parent->keys[index] = parent->keys[index - 1u];
    parent->values[index] = parent->values[index - 1u];
    parent->links[index] = parent->links[index - 1u];
  }
  parent->keys[child_index] = left->keys[median];
  parent->values[child_index] = left->values[median];
  parent->links[child_index] = left->links[median];
  left->keys[median] = NULL;
  left->values[median] = NULL;
  left->links[median] = NULL;
  left->num_keys = median;
  ++parent->num_keys;
}

typedef struct turbo_btree_node_pool {
  turbo_btree_node_t **nodes;
  size_t count;
  size_t used;
} turbo_btree_node_pool;

static void turbo_btree_pool_destroy(turbo_btree_node_pool *pool) {
  size_t index;
  if (pool == NULL) return;
  for (index = pool->used; index < pool->count; ++index)
    turbo_btree_node_free_shell(pool->nodes[index]);
  free(pool->nodes);
  memset(pool, 0, sizeof(*pool));
}

static container_status turbo_btree_pool_prepare(const turbo_btree_t *tree,
                                                  turbo_btree_node_pool *pool) {
  const turbo_btree_node_t *node = tree->root;
  size_t levels = 0u;
  size_t index;
  while (node != NULL) {
    ++levels;
    node = node->leaf ? NULL : node->children[0];
  }
  if (levels > SIZE_MAX - 2u) return CONTAINER_CAPACITY_EXCEEDED;
  pool->count = levels + 2u;
  if (pool->count > SIZE_MAX / sizeof(*pool->nodes))
    return CONTAINER_CAPACITY_EXCEEDED;
  pool->nodes = (turbo_btree_node_t **)calloc(pool->count,
                                              sizeof(*pool->nodes));
  if (pool->nodes == NULL) return CONTAINER_OUT_OF_MEMORY;
  for (index = 0u; index < pool->count; ++index) {
    pool->nodes[index] = turbo_btree_node_new(tree, true);
    if (pool->nodes[index] == NULL) {
      turbo_btree_pool_destroy(pool);
      return CONTAINER_OUT_OF_MEMORY;
    }
  }
  return CONTAINER_OK;
}

static turbo_btree_node_t *turbo_btree_pool_take(
    turbo_btree_node_pool *pool, bool leaf) {
  turbo_btree_node_t *node = pool->nodes[pool->used++];
  node->leaf = leaf;
  return node;
}

/* All allocations complete before this function, so mutation cannot fail. */
static void turbo_btree_insert_prepared(turbo_btree_t *tree, void *key,
                                        void *value,
                                        turbo_btree_entry_link_t *link,
                                        turbo_btree_node_pool *pool) {
  turbo_btree_node_t *node;
  size_t index;
  turbo_btree_entry_link_t *next_link = NULL;

  if (tree->root == NULL) {
    node = turbo_btree_pool_take(pool, true);
    node->keys[0] = key;
    node->values[0] = value;
    node->links[0] = link;
    node->num_keys = 1u;
    tree->root = node;
  } else {
    if (tree->root->num_keys == tree->max_keys) {
      node = turbo_btree_pool_take(pool, false);
      node->children[0] = tree->root;
      tree->root = node;
      turbo_btree_split_child(tree, node, 0u,
                              turbo_btree_pool_take(pool,
                                                    node->children[0]->leaf));
    }
    node = tree->root;
    while (!node->leaf) {
      index = turbo_btree_lower_bound(tree, node, key);
      if (index < node->num_keys) next_link = node->links[index];
      if (node->children[index]->num_keys == tree->max_keys) {
        turbo_btree_split_child(
            tree, node, index,
            turbo_btree_pool_take(pool, node->children[index]->leaf));
        if (turbo_btree_compare_key(tree, key, node->keys[index]) > 0)
          ++index;
        else
          next_link = node->links[index];
      }
      node = node->children[index];
    }
    index = turbo_btree_lower_bound(tree, node, key);
    if (index < node->num_keys) next_link = node->links[index];
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
  }
  turbo_btree_link_insert_before(tree, next_link, link);
  ++tree->size;
}

static container_status turbo_btree_init_common(
    turbo_btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, turbo_btree_compare_fn compare,
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
      (2u * min_degree) > SIZE_MAX / sizeof(turbo_btree_node_t *))
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

container_status turbo_btree_init_with_order(
    turbo_btree_t *tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t min_degree,
    size_t entry_limit) {
  container_status status;
  if (key_type == NULL || value_type == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(key_type, true);
  if (status != CONTAINER_OK) return status;
  status = turbo_sequence_require_type(value_type, false);
  if (status != CONTAINER_OK) return status;
  return turbo_btree_init_common(
      tree, key_type->size, key_type->align, value_type->size,
      value_type->align, min_degree, entry_limit, key_type, value_type, NULL,
      NULL);
}

container_status turbo_btree_init(turbo_btree_t *tree,
                                  const cmeta_type_desc *key_type,
                                  const cmeta_type_desc *value_type,
                                  size_t entry_limit) {
  return turbo_btree_init_with_order(tree, key_type, value_type,
                                     TURBO_BTREE_DEFAULT_MIN_DEGREE,
                                     entry_limit);
}

container_status turbo_btree_init_bytes_with_order(
    turbo_btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t min_degree,
    size_t entry_limit, turbo_btree_compare_fn compare, void *compare_ctx) {
  return turbo_btree_init_common(tree, key_size, key_align, value_size,
                                 value_align, min_degree, entry_limit, NULL,
                                 NULL, compare, compare_ctx);
}

container_status turbo_btree_init_bytes(
    turbo_btree_t *tree, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_btree_compare_fn compare, void *compare_ctx) {
  return turbo_btree_init_bytes_with_order(
      tree, key_size, key_align, value_size, value_align,
      TURBO_BTREE_DEFAULT_MIN_DEGREE, entry_limit, compare, compare_ctx);
}

static bool turbo_btree_nth(const turbo_btree_t *tree,
                            const turbo_btree_node_t *node, size_t target,
                            size_t *cursor, const void **out_key,
                            const void **out_value) {
  size_t index;
  if (node == NULL) return false;
  for (index = 0u; index < node->num_keys; ++index) {
    if (!node->leaf && turbo_btree_nth(tree, node->children[index], target,
                                      cursor, out_key, out_value))
      return true;
    if (*cursor == target) {
      *out_key = node->keys[index];
      *out_value = node->values[index];
      return true;
    }
    ++*cursor;
  }
  return !node->leaf && turbo_btree_nth(
                            tree, node->children[node->num_keys], target,
                            cursor, out_key, out_value);
}

static bool turbo_btree_pair_at(const turbo_btree_t *tree, size_t index,
                                const void **out_key,
                                const void **out_value) {
  size_t cursor = 0u;
  return turbo_btree_valid(tree) && index < tree->size && out_key != NULL &&
         out_value != NULL && turbo_btree_nth(tree, tree->root, index, &cursor,
                                             out_key, out_value);
}

static const void *turbo_btree_find_value(const turbo_btree_t *tree,
                                          const void *key) {
  const turbo_btree_node_t *node;
  if (!turbo_btree_valid(tree) || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL) {
    size_t index = turbo_btree_lower_bound(tree, node, key);
    if (index < node->num_keys &&
        turbo_btree_compare_key(tree, node->keys[index], key) == 0)
      return node->values[index];
    if (node->leaf) return NULL;
    node = node->children[index];
  }
  return NULL;
}

static bool turbo_btree_find_entry(const turbo_btree_t *tree,
                                   const void *key,
                                   turbo_btree_node_t **out_node,
                                   size_t *out_index) {
  turbo_btree_node_t *node = tree->root;
  while (node != NULL) {
    size_t index = turbo_btree_lower_bound(tree, node, key);
    if (index < node->num_keys &&
        turbo_btree_compare_key(tree, node->keys[index], key) == 0) {
      if (out_node != NULL) *out_node = node;
      if (out_index != NULL) *out_index = index;
      return true;
    }
    if (node->leaf) return false;
    node = node->children[index];
  }
  return false;
}

container_status turbo_btree_put(turbo_btree_t *tree, const void *key,
                                 const void *value) {
  turbo_btree_node_t *existing = NULL;
  turbo_btree_node_pool pool = {0};
  turbo_btree_entry_link_t *link = NULL;
  void *key_copy = NULL;
  void *value_copy = NULL;
  size_t existing_index = 0u;
  container_status status;
  if (!turbo_btree_valid(tree) || key == NULL || value == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (turbo_btree_find_entry(tree, key, &existing, &existing_index)) {
    status = turbo_btree_copy_object(
        tree->value_type, tree->value_size, tree->value_stride,
        tree->value_align, value, &value_copy);
    if (status != CONTAINER_OK) return status;
    turbo_btree_destroy_object(tree->value_type,
                               existing->values[existing_index]);
    existing->values[existing_index] = value_copy;
    existing->links[existing_index]->value = value_copy;
    ++tree->generation;
    return CONTAINER_OK;
  }
  if (tree->size >= tree->entry_limit) return CONTAINER_CAPACITY_EXCEEDED;
  status = turbo_btree_copy_object(
      tree->key_type, tree->key_size, tree->key_stride, tree->key_align, key,
      &key_copy);
  if (status != CONTAINER_OK) return status;
  status = turbo_btree_copy_object(tree->value_type, tree->value_size,
                                   tree->value_stride, tree->value_align,
                                   value, &value_copy);
  if (status != CONTAINER_OK) {
    turbo_btree_destroy_object(tree->key_type, key_copy);
    return status;
  }
  link = turbo_btree_link_new(key_copy, value_copy);
  if (link == NULL) {
    turbo_btree_destroy_object(tree->value_type, value_copy);
    turbo_btree_destroy_object(tree->key_type, key_copy);
    return CONTAINER_OUT_OF_MEMORY;
  }
  status = turbo_btree_pool_prepare(tree, &pool);
  if (status != CONTAINER_OK) {
    free(link);
    turbo_btree_destroy_object(tree->value_type, value_copy);
    turbo_btree_destroy_object(tree->key_type, key_copy);
    return status;
  }
  turbo_btree_insert_prepared(tree, key_copy, value_copy, link, &pool);
  turbo_btree_pool_destroy(&pool);
  ++tree->generation;
  return CONTAINER_OK;
}

void *turbo_btree_get(turbo_btree_t *tree, const void *key) {
  return (void *)turbo_btree_find_value(tree, key);
}

const void *turbo_btree_get_const(const turbo_btree_t *tree,
                                  const void *key) {
  return turbo_btree_find_value(tree, key);
}

bool turbo_btree_contains(const turbo_btree_t *tree, const void *key) {
  return turbo_btree_find_value(tree, key) != NULL;
}

static void turbo_btree_node_remove_slot(turbo_btree_node_t *node,
                                         size_t index) {
  if (index + 1u < node->num_keys) {
    memmove(node->keys + index, node->keys + index + 1u,
            (node->num_keys - index - 1u) * sizeof(void *));
    memmove(node->values + index, node->values + index + 1u,
            (node->num_keys - index - 1u) * sizeof(void *));
    memmove(node->links + index, node->links + index + 1u,
            (node->num_keys - index - 1u) * sizeof(*node->links));
  }
  --node->num_keys;
  node->keys[node->num_keys] = NULL;
  node->values[node->num_keys] = NULL;
  node->links[node->num_keys] = NULL;
}

static void turbo_btree_borrow_previous(turbo_btree_node_t *parent,
                                        size_t child_index) {
  turbo_btree_node_t *child = parent->children[child_index];
  turbo_btree_node_t *sibling = parent->children[child_index - 1u];
  memmove(child->keys + 1u, child->keys,
          child->num_keys * sizeof(void *));
  memmove(child->values + 1u, child->values,
          child->num_keys * sizeof(void *));
  memmove(child->links + 1u, child->links,
          child->num_keys * sizeof(*child->links));
  if (!child->leaf)
    memmove(child->children + 1u, child->children,
            (child->num_keys + 1u) * sizeof(*child->children));
  child->keys[0] = parent->keys[child_index - 1u];
  child->values[0] = parent->values[child_index - 1u];
  child->links[0] = parent->links[child_index - 1u];
  if (!child->leaf) {
    child->children[0] = sibling->children[sibling->num_keys];
    sibling->children[sibling->num_keys] = NULL;
  }
  parent->keys[child_index - 1u] = sibling->keys[sibling->num_keys - 1u];
  parent->values[child_index - 1u] = sibling->values[sibling->num_keys - 1u];
  parent->links[child_index - 1u] = sibling->links[sibling->num_keys - 1u];
  sibling->keys[sibling->num_keys - 1u] = NULL;
  sibling->values[sibling->num_keys - 1u] = NULL;
  sibling->links[sibling->num_keys - 1u] = NULL;
  --sibling->num_keys;
  ++child->num_keys;
}

static void turbo_btree_borrow_next(turbo_btree_node_t *parent,
                                    size_t child_index) {
  turbo_btree_node_t *child = parent->children[child_index];
  turbo_btree_node_t *sibling = parent->children[child_index + 1u];
  child->keys[child->num_keys] = parent->keys[child_index];
  child->values[child->num_keys] = parent->values[child_index];
  child->links[child->num_keys] = parent->links[child_index];
  if (!child->leaf) child->children[child->num_keys + 1u] = sibling->children[0];
  parent->keys[child_index] = sibling->keys[0];
  parent->values[child_index] = sibling->values[0];
  parent->links[child_index] = sibling->links[0];
  turbo_btree_node_remove_slot(sibling, 0u);
  if (!sibling->leaf) {
    memmove(sibling->children, sibling->children + 1u,
            (sibling->num_keys + 1u) * sizeof(*sibling->children));
    sibling->children[sibling->num_keys + 1u] = NULL;
  }
  ++child->num_keys;
}

static turbo_btree_node_t *turbo_btree_merge_children(
    turbo_btree_node_t *parent, size_t child_index) {
  turbo_btree_node_t *left = parent->children[child_index];
  turbo_btree_node_t *right = parent->children[child_index + 1u];
  size_t left_count = left->num_keys;
  size_t index;
  left->keys[left_count] = parent->keys[child_index];
  left->values[left_count] = parent->values[child_index];
  left->links[left_count] = parent->links[child_index];
  for (index = 0u; index < right->num_keys; ++index) {
    left->keys[left_count + 1u + index] = right->keys[index];
    left->values[left_count + 1u + index] = right->values[index];
    left->links[left_count + 1u + index] = right->links[index];
  }
  if (!left->leaf)
    for (index = 0u; index <= right->num_keys; ++index)
      left->children[left_count + 1u + index] = right->children[index];
  left->num_keys += right->num_keys + 1u;
  if (child_index + 1u < parent->num_keys) {
    memmove(parent->keys + child_index, parent->keys + child_index + 1u,
            (parent->num_keys - child_index - 1u) * sizeof(void *));
    memmove(parent->values + child_index, parent->values + child_index + 1u,
            (parent->num_keys - child_index - 1u) * sizeof(void *));
    memmove(parent->links + child_index, parent->links + child_index + 1u,
            (parent->num_keys - child_index - 1u) * sizeof(*parent->links));
  }
  memmove(parent->children + child_index + 1u,
          parent->children + child_index + 2u,
          (parent->num_keys - child_index - 1u) * sizeof(*parent->children));
  --parent->num_keys;
  parent->keys[parent->num_keys] = NULL;
  parent->values[parent->num_keys] = NULL;
  parent->links[parent->num_keys] = NULL;
  parent->children[parent->num_keys + 1u] = NULL;
  turbo_btree_node_free_shell(right);
  return left;
}

static void turbo_btree_delete_structural(
    turbo_btree_t *tree, turbo_btree_node_t *node, const void *key,
    void **out_key, void **out_value, turbo_btree_entry_link_t **out_link) {
  size_t index = turbo_btree_lower_bound(tree, node, key);
  if (index < node->num_keys &&
      turbo_btree_compare_key(tree, node->keys[index], key) == 0) {
    if (node->leaf) {
      *out_key = node->keys[index];
      *out_value = node->values[index];
      *out_link = node->links[index];
      turbo_btree_node_remove_slot(node, index);
      return;
    }
    if (node->children[index]->num_keys >= tree->min_degree) {
      turbo_btree_node_t *pred = node->children[index];
      void *discard_key;
      void *discard_value;
      turbo_btree_entry_link_t *discard_link;
      *out_key = node->keys[index];
      *out_value = node->values[index];
      *out_link = node->links[index];
      while (!pred->leaf) pred = pred->children[pred->num_keys];
      node->keys[index] = pred->keys[pred->num_keys - 1u];
      node->values[index] = pred->values[pred->num_keys - 1u];
      node->links[index] = pred->links[pred->num_keys - 1u];
      turbo_btree_delete_structural(tree, node->children[index],
                                    node->keys[index], &discard_key,
                                    &discard_value, &discard_link);
      return;
    }
    if (node->children[index + 1u]->num_keys >= tree->min_degree) {
      turbo_btree_node_t *succ = node->children[index + 1u];
      void *discard_key;
      void *discard_value;
      turbo_btree_entry_link_t *discard_link;
      *out_key = node->keys[index];
      *out_value = node->values[index];
      *out_link = node->links[index];
      while (!succ->leaf) succ = succ->children[0];
      node->keys[index] = succ->keys[0];
      node->values[index] = succ->values[0];
      node->links[index] = succ->links[0];
      turbo_btree_delete_structural(tree, node->children[index + 1u],
                                    node->keys[index], &discard_key,
                                    &discard_value, &discard_link);
      return;
    }
    node = turbo_btree_merge_children(node, index);
    turbo_btree_delete_structural(tree, node, key, out_key, out_value,
                                  out_link);
    return;
  }

  if (node->children[index]->num_keys == tree->min_degree - 1u) {
    if (index > 0u &&
        node->children[index - 1u]->num_keys >= tree->min_degree) {
      turbo_btree_borrow_previous(node, index);
    } else if (index < node->num_keys &&
               node->children[index + 1u]->num_keys >= tree->min_degree) {
      turbo_btree_borrow_next(node, index);
    } else if (index < node->num_keys) {
      node = turbo_btree_merge_children(node, index);
    } else {
      node = turbo_btree_merge_children(node, index - 1u);
    }
  } else {
    node = node->children[index];
  }
  turbo_btree_delete_structural(tree, node, key, out_key, out_value,
                                out_link);
}

container_status turbo_btree_remove(turbo_btree_t *tree, const void *key,
                                    void *out_value) {
  void *removed_key = NULL;
  void *removed_value = NULL;
  turbo_btree_entry_link_t *removed_link = NULL;
  if (!turbo_btree_valid(tree) || key == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (!turbo_btree_find_entry(tree, key, NULL, NULL))
    return CONTAINER_NOT_FOUND;
  turbo_btree_delete_structural(tree, tree->root, key, &removed_key,
                                &removed_value, &removed_link);
  if (tree->root->num_keys == 0u) {
    turbo_btree_node_t *old_root = tree->root;
    tree->root = old_root->leaf ? NULL : old_root->children[0];
    old_root->children[0] = NULL;
    turbo_btree_node_free_shell(old_root);
  }
  turbo_btree_link_unlink(tree, removed_link);
  if (out_value != NULL) {
    if (tree->value_type != NULL)
      tree->value_type->traits->move_construct(out_value, removed_value);
    else
      memcpy(out_value, removed_value, tree->value_size);
  }
  turbo_btree_destroy_object(tree->key_type, removed_key);
  turbo_btree_destroy_object(tree->value_type, removed_value);
  free(removed_link);
  --tree->size;
  ++tree->generation;
  return CONTAINER_OK;
}

void turbo_btree_clear(turbo_btree_t *tree) {
  if (!turbo_btree_valid(tree)) return;
  if (tree->root == NULL) return;
  turbo_btree_node_destroy(tree, tree->root);
  tree->root = NULL;
  tree->size = 0u;
  tree->first = NULL;
  tree->last = NULL;
  ++tree->generation;
}

void turbo_btree_destroy(turbo_btree_t *tree) {
  uint64_t generation;
  if (tree == NULL) return;
  generation = tree->generation;
  if (tree->initialized) {
    turbo_btree_node_destroy(tree, tree->root);
    ++generation;
  }
  memset(tree, 0, sizeof(*tree));
  tree->generation = generation;
}

container_status turbo_btree_reserve(turbo_btree_t *tree,
                                     size_t min_capacity) {
  if (!turbo_btree_valid(tree)) return CONTAINER_INVALID_ARGUMENT;
  return min_capacity <= tree->entry_limit ? CONTAINER_OK
                                            : CONTAINER_CAPACITY_EXCEEDED;
}

size_t turbo_btree_size(const turbo_btree_t *tree) {
  return tree == NULL ? 0u : tree->size;
}

size_t turbo_btree_capacity(const turbo_btree_t *tree) {
  return tree == NULL ? 0u : tree->size;
}

size_t turbo_btree_entry_limit(const turbo_btree_t *tree) {
  return tree == NULL ? 0u : tree->entry_limit;
}

uint64_t turbo_btree_generation(const turbo_btree_t *tree) {
  return tree == NULL ? UINT64_C(0) : tree->generation;
}

bool turbo_btree_empty(const turbo_btree_t *tree) {
  return tree == NULL || tree->size == 0u;
}

void *turbo_btree_key_at(turbo_btree_t *tree, size_t index) {
  const void *key;
  const void *value;
  return turbo_btree_pair_at(tree, index, &key, &value) ? (void *)key : NULL;
}

const void *turbo_btree_key_at_const(const turbo_btree_t *tree,
                                     size_t index) {
  const void *key;
  const void *value;
  return turbo_btree_pair_at(tree, index, &key, &value) ? key : NULL;
}

void *turbo_btree_value_at(turbo_btree_t *tree, size_t index) {
  const void *key;
  const void *value;
  return turbo_btree_pair_at(tree, index, &key, &value) ? (void *)value : NULL;
}

const void *turbo_btree_value_at_const(const turbo_btree_t *tree,
                                       size_t index) {
  const void *key;
  const void *value;
  return turbo_btree_pair_at(tree, index, &key, &value) ? value : NULL;
}

bool turbo_btree_range_next(const turbo_btree_t *tree,
                            cmeta_range_cursor *cursor,
                            const void **out_key, const void **out_value) {
  turbo_btree_entry_link_t *link;
  if (!turbo_btree_valid(tree) || cursor == NULL || out_key == NULL ||
      out_value == NULL)
    return false;
  if (cursor->state[1] == NULL) {
    cursor->state[1] = (void *)tree;
    link = tree->first;
  } else {
    if (cursor->state[1] != (void *)tree) return false;
    link = (turbo_btree_entry_link_t *)cursor->state[0];
  }
  if (link == NULL) return false;
  *out_key = link->key;
  *out_value = link->value;
  cursor->state[0] = link->next;
  return true;
}

static container_status turbo_btree_from_arrays_common(
    turbo_btree_t *tree, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_btree_compare_fn compare, void *compare_ctx) {
  turbo_btree_t next = {0};
  size_t index;
  uint64_t generation;
  container_status status;
  if (tree == NULL || (count != 0u && (keys == NULL || values == NULL)))
    return CONTAINER_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return CONTAINER_CAPACITY_EXCEEDED;
  if (key_type != NULL)
    status = turbo_btree_init(&next, key_type, value_type, entry_limit);
  else
    status = turbo_btree_init_bytes(&next, key_size, key_align, value_size,
                                    value_align, entry_limit, compare,
                                    compare_ctx);
  if (status != CONTAINER_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_btree_put(
        &next, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != CONTAINER_OK) {
      turbo_btree_destroy(&next);
      return status;
    }
  }
  generation = tree->generation + UINT64_C(1);
  if (tree->initialized) turbo_btree_node_destroy(tree, tree->root);
  *tree = next;
  tree->generation = generation;
  return CONTAINER_OK;
}

container_status turbo_btree_from_arrays(
    turbo_btree_t *tree, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit) {
  if (key_type == NULL || value_type == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_btree_from_arrays_common(
      tree, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, entry_limit, NULL,
      NULL);
}

container_status turbo_btree_from_arrays_bytes(
    turbo_btree_t *tree, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_btree_compare_fn compare, void *compare_ctx) {
  return turbo_btree_from_arrays_common(
      tree, keys, values, count, NULL, NULL, key_size, key_align, value_size,
      value_align, entry_limit, compare, compare_ctx);
}
