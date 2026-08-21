#include <turbo/container/btree.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef union turbo_btree_max_align {
  long double ld;
  long long ll;
  void *ptr;
  size_t size;
} turbo_btree_max_align_t;

static int align_up(size_t value, size_t alignment, size_t *out) {
  size_t rem;
  if (alignment == 0U || out == NULL) return CONTAINER_INVALID_ARGUMENT;
  rem = value % alignment;
  if (rem == 0U) { *out = value; return CONTAINER_OK; }
  if (value > SIZE_MAX - (alignment - rem)) return CONTAINER_OUT_OF_MEMORY;
  *out = value + (alignment - rem);
  return CONTAINER_OK;
}

static bool valid(const turbo_btree_t *tree) {
  return tree != NULL && tree->key_size > 0U && tree->value_size > 0U &&
         tree->key_stride > 0U && tree->value_stride > 0U &&
         tree->min_degree >= 2U && tree->compare != NULL;
}

static unsigned char *node_key(const turbo_btree_t *tree, const turbo_btree_node_t *node,
                               size_t index) {
  return node == NULL ? NULL : node->keys + index * tree->key_stride;
}

static unsigned char *node_value(const turbo_btree_t *tree, const turbo_btree_node_t *node,
                                 size_t index) {
  return node == NULL ? NULL : node->values + index * tree->value_stride;
}

static turbo_btree_node_t *node_new(const turbo_btree_t *tree, bool leaf) {
  turbo_btree_node_t *node;
  size_t key_bytes;
  size_t value_bytes;

  if (!valid(tree)) return NULL;
  if (tree->max_keys > SIZE_MAX / tree->key_stride ||
      tree->max_keys > SIZE_MAX / tree->value_stride ||
      tree->max_children > SIZE_MAX / sizeof(turbo_btree_node_t *)) return NULL;
  key_bytes = tree->max_keys * tree->key_stride;
  value_bytes = tree->max_keys * tree->value_stride;

  node = (turbo_btree_node_t *)calloc(1U, sizeof(*node));
  if (node == NULL) return NULL;
  node->keys = (unsigned char *)malloc(key_bytes);
  node->values = (unsigned char *)malloc(value_bytes);
  node->children = (turbo_btree_node_t **)calloc(tree->max_children, sizeof(*node->children));
  if (node->keys == NULL || node->values == NULL || node->children == NULL) {
    free(node->children); free(node->values); free(node->keys); free(node); return NULL;
  }
  node->leaf = leaf;
  return node;
}

static void node_destroy(turbo_btree_node_t *node) {
  size_t i;
  if (node == NULL) return;
  if (!node->leaf) {
    for (i = 0U; i <= node->num_keys; ++i) node_destroy(node->children[i]);
  }
  free(node->children);
  free(node->values);
  free(node->keys);
  free(node);
}

static size_t lower_bound(const turbo_btree_t *tree, const turbo_btree_node_t *node,
                          const void *key) {
  size_t lo = 0U;
  size_t hi = node == NULL ? 0U : node->num_keys;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2U;
    if (tree->compare(node_key(tree, node, mid), key, tree->compare_ctx) < 0) lo = mid + 1U;
    else hi = mid;
  }
  return lo;
}

static unsigned char *find_value(turbo_btree_t *tree, const void *key) {
  turbo_btree_node_t *node;
  size_t i;
  int cmp;
  if (!valid(tree) || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL) {
    i = lower_bound(tree, node, key);
    if (i < node->num_keys) {
      cmp = tree->compare(node_key(tree, node, i), key, tree->compare_ctx);
      if (cmp == 0) return node_value(tree, node, i);
    }
    if (node->leaf) return NULL;
    node = node->children[i];
  }
  return NULL;
}

static const unsigned char *find_value_const(const turbo_btree_t *tree, const void *key) {
  const turbo_btree_node_t *node;
  size_t i;
  int cmp;
  if (!valid(tree) || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL) {
    i = lower_bound(tree, node, key);
    if (i < node->num_keys) {
      cmp = tree->compare(node_key(tree, node, i), key, tree->compare_ctx);
      if (cmp == 0) return node_value(tree, node, i);
    }
    if (node->leaf) return NULL;
    node = node->children[i];
  }
  return NULL;
}

static int split_child(turbo_btree_t *tree, turbo_btree_node_t *parent, size_t child_index) {
  turbo_btree_node_t *left;
  turbo_btree_node_t *right;
  size_t t;
  size_t median;
  size_t j;

  left = parent->children[child_index];
  t = tree->min_degree;
  median = t - 1U;
  right = node_new(tree, left->leaf);
  if (right == NULL) return CONTAINER_OUT_OF_MEMORY;
  right->num_keys = t - 1U;

  for (j = 0U; j < t - 1U; ++j) {
    memcpy(node_key(tree, right, j), node_key(tree, left, j + t), tree->key_size);
    memcpy(node_value(tree, right, j), node_value(tree, left, j + t), tree->value_size);
  }
  if (!left->leaf) {
    for (j = 0U; j < t; ++j) {
      right->children[j] = left->children[j + t];
      left->children[j + t] = NULL;
    }
  }

  for (j = parent->num_keys + 1U; j > child_index + 1U; --j) {
    parent->children[j] = parent->children[j - 1U];
  }
  parent->children[child_index + 1U] = right;

  for (j = parent->num_keys; j > child_index; --j) {
    memcpy(node_key(tree, parent, j), node_key(tree, parent, j - 1U), tree->key_size);
    memcpy(node_value(tree, parent, j), node_value(tree, parent, j - 1U), tree->value_size);
  }
  memcpy(node_key(tree, parent, child_index), node_key(tree, left, median), tree->key_size);
  memcpy(node_value(tree, parent, child_index), node_value(tree, left, median), tree->value_size);
  left->num_keys = t - 1U;
  parent->num_keys += 1U;
  return CONTAINER_OK;
}

static int insert_nonfull(turbo_btree_t *tree, turbo_btree_node_t *node,
                          const void *key, const void *value) {
  size_t i;
  int rc;
  i = lower_bound(tree, node, key);
  if (node->leaf) {
    size_t j;
    for (j = node->num_keys; j > i; --j) {
      memcpy(node_key(tree, node, j), node_key(tree, node, j - 1U), tree->key_size);
      memcpy(node_value(tree, node, j), node_value(tree, node, j - 1U), tree->value_size);
    }
    memcpy(node_key(tree, node, i), key, tree->key_size);
    memcpy(node_value(tree, node, i), value, tree->value_size);
    node->num_keys += 1U;
    return CONTAINER_OK;
  }

  if (node->children[i]->num_keys == tree->max_keys) {
    rc = split_child(tree, node, i);
    if (rc != CONTAINER_OK) return rc;
    if (tree->compare(key, node_key(tree, node, i), tree->compare_ctx) > 0) ++i;
  }
  return insert_nonfull(tree, node->children[i], key, value);
}

int turbo_btree_init_with_order(turbo_btree_t *tree, size_t key_size, size_t value_size,
                                turbo_btree_compare_fn compare, void *compare_ctx,
                                size_t min_degree) {
  size_t align = _Alignof(turbo_btree_max_align_t);
  if (tree == NULL || key_size == 0U || value_size == 0U || compare == NULL || min_degree < 2U) return CONTAINER_INVALID_ARGUMENT;
  if (min_degree > SIZE_MAX / 2U) return CONTAINER_OUT_OF_MEMORY;
  memset(tree, 0, sizeof(*tree));
  tree->key_size = key_size;
  tree->value_size = value_size;
  tree->min_degree = min_degree;
  tree->max_children = 2U * min_degree;
  tree->max_keys = tree->max_children - 1U;
  tree->compare = compare;
  tree->compare_ctx = compare_ctx;
  if (align_up(key_size, align, &tree->key_stride) != CONTAINER_OK ||
      align_up(value_size, align, &tree->value_stride) != CONTAINER_OK) {
    memset(tree, 0, sizeof(*tree));
    return CONTAINER_OUT_OF_MEMORY;
  }
  return CONTAINER_OK;
}

int turbo_btree_init(turbo_btree_t *tree, size_t key_size, size_t value_size,
                     turbo_btree_compare_fn compare, void *compare_ctx) {
  return turbo_btree_init_with_order(tree, key_size, value_size, compare, compare_ctx,
                                     TURBO_BTREE_DEFAULT_MIN_DEGREE);
}

void turbo_btree_destroy(turbo_btree_t *tree) {
  if (tree == NULL) return;
  node_destroy(tree->root);
  memset(tree, 0, sizeof(*tree));
}

void turbo_btree_clear(turbo_btree_t *tree) {
  if (tree == NULL) return;
  node_destroy(tree->root);
  tree->root = NULL;
  tree->size = 0U;
}

int turbo_btree_reserve(turbo_btree_t *tree, size_t min_capacity) {
  (void)min_capacity;
  return valid(tree) ? CONTAINER_OK : CONTAINER_INVALID_ARGUMENT;
}

int turbo_btree_put(turbo_btree_t *tree, const void *key, const void *value) {
  turbo_btree_node_t *root;
  turbo_btree_node_t *next;
  unsigned char *existing;
  int rc;
  if (!valid(tree) || key == NULL || value == NULL) return CONTAINER_INVALID_ARGUMENT;
  existing = find_value(tree, key);
  if (existing != NULL) {
    memcpy(existing, value, tree->value_size);
    return CONTAINER_OK;
  }
  if (tree->root == NULL) {
    tree->root = node_new(tree, true);
    if (tree->root == NULL) return CONTAINER_OUT_OF_MEMORY;
    memcpy(node_key(tree, tree->root, 0U), key, tree->key_size);
    memcpy(node_value(tree, tree->root, 0U), value, tree->value_size);
    tree->root->num_keys = 1U;
    tree->size = 1U;
    return CONTAINER_OK;
  }
  root = tree->root;
  if (root->num_keys == tree->max_keys) {
    next = node_new(tree, false);
    if (next == NULL) return CONTAINER_OUT_OF_MEMORY;
    next->children[0] = root;
    rc = split_child(tree, next, 0U);
    if (rc != CONTAINER_OK) { next->children[0] = NULL; node_destroy(next); return rc; }
    tree->root = next;
    rc = insert_nonfull(tree, next, key, value);
  } else {
    rc = insert_nonfull(tree, root, key, value);
  }
  if (rc == CONTAINER_OK) tree->size += 1U;
  return rc;
}

void *turbo_btree_get(turbo_btree_t *tree, const void *key) { return find_value(tree, key); }
const void *turbo_btree_get_const(const turbo_btree_t *tree, const void *key) { return find_value_const(tree, key); }
bool turbo_btree_contains(const turbo_btree_t *tree, const void *key) { return find_value_const(tree, key) != NULL; }
size_t turbo_btree_size(const turbo_btree_t *tree) { return tree == NULL ? 0U : tree->size; }
size_t turbo_btree_capacity(const turbo_btree_t *tree) { return tree == NULL ? 0U : tree->size; }
bool turbo_btree_empty(const turbo_btree_t *tree) { return tree == NULL || tree->size == 0U; }

static bool nth_pair(const turbo_btree_t *tree, const turbo_btree_node_t *node,
                     size_t target, size_t *cursor,
                     const unsigned char **out_key, const unsigned char **out_value) {
  size_t i;
  if (node == NULL) return false;
  for (i = 0U; i < node->num_keys; ++i) {
    if (!node->leaf && nth_pair(tree, node->children[i], target, cursor, out_key, out_value)) return true;
    if (*cursor == target) {
      *out_key = node_key(tree, node, i);
      *out_value = node_value(tree, node, i);
      return true;
    }
    *cursor += 1U;
  }
  if (!node->leaf) return nth_pair(tree, node->children[node->num_keys], target, cursor, out_key, out_value);
  return false;
}

static bool pair_at(const turbo_btree_t *tree, size_t index,
                    const unsigned char **out_key, const unsigned char **out_value) {
  size_t cursor = 0U;
  if (!valid(tree) || index >= tree->size || out_key == NULL || out_value == NULL) return false;
  return nth_pair(tree, tree->root, index, &cursor, out_key, out_value);
}

void *turbo_btree_key_at(turbo_btree_t *tree, size_t index) {
  const unsigned char *key; const unsigned char *value;
  return pair_at(tree, index, &key, &value) ? (void *)key : NULL;
}
const void *turbo_btree_key_at_const(const turbo_btree_t *tree, size_t index) {
  const unsigned char *key; const unsigned char *value;
  return pair_at(tree, index, &key, &value) ? key : NULL;
}
void *turbo_btree_value_at(turbo_btree_t *tree, size_t index) {
  const unsigned char *key; const unsigned char *value;
  return pair_at(tree, index, &key, &value) ? (void *)value : NULL;
}
const void *turbo_btree_value_at_const(const turbo_btree_t *tree, size_t index) {
  const unsigned char *key; const unsigned char *value;
  return pair_at(tree, index, &key, &value) ? value : NULL;
}

static int rebuild_without(turbo_btree_t *tree, const void *remove_key, void *out_value) {
  turbo_btree_t next;
  size_t i;
  bool removed = false;
  int rc;
  const void *key;
  const void *value;

  rc = turbo_btree_init_with_order(&next, tree->key_size, tree->value_size,
                                   tree->compare, tree->compare_ctx, tree->min_degree);
  if (rc != CONTAINER_OK) return rc;
  for (i = 0U; i < tree->size; ++i) {
    key = turbo_btree_key_at_const(tree, i);
    value = turbo_btree_value_at_const(tree, i);
    if (!removed && tree->compare(key, remove_key, tree->compare_ctx) == 0) {
      if (out_value != NULL) memcpy(out_value, value, tree->value_size);
      removed = true;
      continue;
    }
    rc = turbo_btree_put(&next, key, value);
    if (rc != CONTAINER_OK) { turbo_btree_destroy(&next); return rc; }
  }
  if (!removed) { turbo_btree_destroy(&next); return CONTAINER_NOT_FOUND; }
  node_destroy(tree->root);
  tree->root = next.root;
  tree->size = next.size;
  next.root = NULL;
  turbo_btree_destroy(&next);
  return CONTAINER_OK;
}

int turbo_btree_remove(turbo_btree_t *tree, const void *key, void *out_value) {
  if (!valid(tree) || key == NULL) return CONTAINER_INVALID_ARGUMENT;
  if (!turbo_btree_contains(tree, key)) return CONTAINER_NOT_FOUND;
  return rebuild_without(tree, key, out_value);
}

int turbo_btree_from_arrays(turbo_btree_t *tree, const void *keys, const void *values,
                            size_t count, size_t key_size, size_t value_size,
                            turbo_btree_compare_fn compare, void *compare_ctx) {
  const unsigned char *k = (const unsigned char *)keys;
  const unsigned char *v = (const unsigned char *)values;
  size_t i;
  int rc;
  if (tree == NULL || (count > 0U && (keys == NULL || values == NULL))) return CONTAINER_INVALID_ARGUMENT;
  rc = turbo_btree_init(tree, key_size, value_size, compare, compare_ctx);
  if (rc != CONTAINER_OK) return rc;
  for (i = 0U; i < count; ++i) {
    rc = turbo_btree_put(tree, k + i * key_size, v + i * value_size);
    if (rc != CONTAINER_OK) { turbo_btree_destroy(tree); return rc; }
  }
  return CONTAINER_OK;
}
