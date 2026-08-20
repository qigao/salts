#ifndef TURBO_BPLUS_TREE_H
#define TURBO_BPLUS_TREE_H

#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#define TURBO_BPLUS_TREE_ALIGNOF(Type) alignof(Type)
#else
#define TURBO_BPLUS_TREE_ALIGNOF(Type) _Alignof(Type)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE
#define TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE 4U
#endif

typedef int (*turbo_bplus_tree_compare_fn)(const void *left, const void *right, void *ctx);

typedef struct turbo_bplus_tree_node_t {
  bool is_leaf;
  size_t num_keys;
  struct turbo_bplus_tree_node_t *parent;
  struct turbo_bplus_tree_node_t *next;
  unsigned char *keys;
  unsigned char *values;
  struct turbo_bplus_tree_node_t **children;
} turbo_bplus_tree_node_t;

typedef union turbo_bplus_tree_max_align {
  long double long_double_value;
  long long long_long_value;
  void *pointer_value;
  size_t size_value;
} turbo_bplus_tree_max_align_t;

typedef struct {
  turbo_bplus_tree_node_t *root;
  size_t key_size;
  size_t value_size;
  size_t key_stride;
  size_t value_stride;
  size_t align;
  size_t min_degree;
  size_t max_keys;
  size_t min_keys;
  size_t max_children;
  turbo_bplus_tree_compare_fn compare;
  void *compare_ctx;
  size_t size;
} turbo_bplus_tree_t;

static inline int turbo_bplus_tree_align_up(size_t value, size_t alignment, size_t *out) {
  size_t remainder;
  if (out == NULL || alignment == 0U) return TURBO_EINVAL;
  remainder = value % alignment;
  if (remainder == 0U) {
    *out = value;
    return TURBO_OK;
  }
  if (value > SIZE_MAX - (alignment - remainder)) return TURBO_ENOMEM;
  *out = value + (alignment - remainder);
  return TURBO_OK;
}

static inline bool turbo_bplus_tree_is_valid(const turbo_bplus_tree_t *map) {
  return map != NULL && map->compare != NULL && map->key_size > 0U && map->value_size > 0U &&
         map->min_degree >= 2U && map->align > 0U;
}

static inline size_t turbo_bplus_tree_node_key_index_offset(const turbo_bplus_tree_t *map, size_t index) {
  return index * map->key_stride;
}

static inline size_t turbo_bplus_tree_node_value_index_offset(const turbo_bplus_tree_t *map, size_t index) {
  return index * map->value_stride;
}

static inline unsigned char *turbo_bplus_tree_node_key(const turbo_bplus_tree_t *map,
                                                     const turbo_bplus_tree_node_t *node,
                                                     size_t index) {
  return node == NULL || map == NULL ? NULL
                                     : (unsigned char *)(node->keys + turbo_bplus_tree_node_key_index_offset(map, index));
}

static inline unsigned char *turbo_bplus_tree_node_value(const turbo_bplus_tree_t *map,
                                                        const turbo_bplus_tree_node_t *node,
                                                        size_t index) {
  return node == NULL || map == NULL || node->values == NULL
             ? NULL
             : (unsigned char *)(node->values + turbo_bplus_tree_node_value_index_offset(map, index));
}

static inline void turbo_bplus_tree_node_destroy(turbo_bplus_tree_node_t *node) {
  size_t slot;
  if (node == NULL) return;
  if (!node->is_leaf) {
    for (slot = 0; slot <= node->num_keys; ++slot) {
      turbo_bplus_tree_node_destroy(node->children[slot]);
    }
    free(node->children);
  }
  free(node->keys);
  free(node->values);
  free(node);
}

static inline turbo_bplus_tree_node_t *turbo_bplus_tree_node_alloc(const turbo_bplus_tree_t *tree, bool leaf) {
  turbo_bplus_tree_node_t *node;
  size_t key_bytes;
  size_t value_bytes;
  size_t children_bytes;

  if (tree == NULL || tree->max_keys == 0U || tree->key_stride == 0U || tree->value_stride == 0U) return NULL;
  if (tree->max_keys > SIZE_MAX / tree->key_stride) return NULL;
  key_bytes = tree->max_keys * tree->key_stride;
  if (leaf) {
    if (tree->max_keys > SIZE_MAX / tree->value_stride) return NULL;
    value_bytes = tree->max_keys * tree->value_stride;
    children_bytes = 0U;
  } else {
    if (tree->max_children > SIZE_MAX / sizeof(turbo_bplus_tree_node_t *)) return NULL;
    value_bytes = 0U;
    children_bytes = tree->max_children * sizeof(turbo_bplus_tree_node_t *);
  }

  node = (turbo_bplus_tree_node_t *)malloc(sizeof(*node));
  if (node == NULL) return NULL;
  node->is_leaf = leaf;
  node->num_keys = 0U;
  node->parent = NULL;
  node->next = NULL;
  node->keys = (unsigned char *)malloc(key_bytes);
  node->values = leaf ? (unsigned char *)malloc(value_bytes) : NULL;
  node->children = leaf ? NULL : (turbo_bplus_tree_node_t **)malloc(children_bytes);
  if (node->keys == NULL || (leaf && node->values == NULL) || (!leaf && node->children == NULL)) {
    free(node->values);
    free(node->children);
    free(node->keys);
    free(node);
    return NULL;
  }
  memset(node->keys, 0, key_bytes);
  if (leaf) memset(node->values, 0, value_bytes);
  return node;
}

static inline int turbo_bplus_tree_init_with_order(turbo_bplus_tree_t *tree, size_t key_size,
                                                  size_t value_size, turbo_bplus_tree_compare_fn compare,
                                                  void *compare_ctx, size_t min_degree) {
  if (tree == NULL || compare == NULL || key_size == 0U || value_size == 0U || min_degree < 2U) {
    return TURBO_EINVAL;
  }

  if (min_degree > (SIZE_MAX / 2U)) return TURBO_ENOMEM;
  tree->max_children = 2U * min_degree;
  tree->min_degree = min_degree;
  tree->max_keys = tree->max_children - 1U;
  tree->min_keys = min_degree - 1U;
  if (turbo_bplus_tree_align_up(key_size, TURBO_BPLUS_TREE_ALIGNOF(turbo_bplus_tree_max_align_t), &tree->key_stride) !=
      TURBO_OK) {
    return TURBO_ENOMEM;
  }
  if (turbo_bplus_tree_align_up(value_size, TURBO_BPLUS_TREE_ALIGNOF(turbo_bplus_tree_max_align_t), &tree->value_stride) !=
      TURBO_OK) {
    return TURBO_ENOMEM;
  }

  tree->align = TURBO_BPLUS_TREE_ALIGNOF(turbo_bplus_tree_max_align_t);
  tree->key_size = key_size;
  tree->value_size = value_size;
  tree->compare = compare;
  tree->compare_ctx = compare_ctx;
  tree->root = NULL;
  tree->size = 0U;
  return TURBO_OK;
}

static inline int turbo_bplus_tree_init(turbo_bplus_tree_t *tree, size_t key_size, size_t value_size,
                                       turbo_bplus_tree_compare_fn compare, void *compare_ctx) {
  return turbo_bplus_tree_init_with_order(tree, key_size, value_size, compare, compare_ctx,
                                          TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE);
}

static inline void turbo_bplus_tree_destroy(turbo_bplus_tree_t *tree) {
  if (tree == NULL) return;
  turbo_bplus_tree_node_destroy(tree->root);
  memset(tree, 0, sizeof(*tree));
}

static inline void turbo_bplus_tree_clear(turbo_bplus_tree_t *tree) {
  if (tree == NULL) return;
  turbo_bplus_tree_node_destroy(tree->root);
  tree->root = NULL;
  tree->size = 0U;
}

static inline int turbo_bplus_tree_reserve(turbo_bplus_tree_t *tree, size_t min_capacity) {
  (void)min_capacity;
  if (!turbo_bplus_tree_is_valid(tree)) return TURBO_EINVAL;
  return TURBO_OK;
}

static inline size_t turbo_bplus_tree_size(const turbo_bplus_tree_t *tree) {
  return tree == NULL ? 0U : tree->size;
}

static inline size_t turbo_bplus_tree_capacity(const turbo_bplus_tree_t *tree) {
  return tree == NULL ? 0U : tree->size;
}

static inline bool turbo_bplus_tree_empty(const turbo_bplus_tree_t *tree) {
  return tree == NULL || tree->size == 0U;
}

static inline size_t turbo_bplus_tree_lower_bound(const turbo_bplus_tree_t *tree, const turbo_bplus_tree_node_t *node,
                                                 const void *key) {
  size_t left = 0U;
  size_t right = node == NULL ? 0U : node->num_keys;
  while (left < right) {
    size_t mid = (left + right) / 2U;
    int cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, node, mid), tree->compare_ctx);
    if (cmp <= 0) {
      right = mid;
    } else {
      left = mid + 1U;
    }
  }
  return left;
}

static inline turbo_bplus_tree_node_t *turbo_bplus_tree_search_leaf(const turbo_bplus_tree_t *tree, const void *key,
                                                                  size_t *slot, bool *found) {
  turbo_bplus_tree_node_t *node;
  int cmp;
  size_t index;

  if (!turbo_bplus_tree_is_valid(tree) || key == NULL) return NULL;
  node = tree->root;
  if (slot != NULL) *slot = 0U;
  if (found != NULL) *found = false;

  if (node == NULL) return NULL;
  while (!node->is_leaf) {
    index = turbo_bplus_tree_lower_bound(tree, node, key);
    node = node->children[index];
  }
  index = turbo_bplus_tree_lower_bound(tree, node, key);
  if (slot != NULL) *slot = index;
  if (index >= node->num_keys) return node;
  cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, node, index), tree->compare_ctx);
  if (cmp == 0 && found != NULL) *found = true;
  return node;
}

static inline turbo_bplus_tree_node_t *turbo_bplus_tree_leftmost_leaf(const turbo_bplus_tree_t *tree) {
  turbo_bplus_tree_node_t *node;
  if (!turbo_bplus_tree_is_valid(tree)) return NULL;
  node = tree->root;
  while (node != NULL && !node->is_leaf) node = node->children[0];
  return node;
}

static inline int turbo_bplus_tree_parent_insert(turbo_bplus_tree_t *tree, turbo_bplus_tree_node_t *parent,
                                                size_t child_index, const void *sep_key,
                                                turbo_bplus_tree_node_t *right) {
  size_t move_keys;
  size_t move_children;

  if (tree == NULL || parent == NULL || sep_key == NULL || right == NULL) return TURBO_EINVAL;
  if (!parent->is_leaf && parent->num_keys < tree->max_keys) {
    move_keys = parent->num_keys - child_index;
    move_children = (parent->num_keys + 1U) - (child_index + 1U);
    memmove(parent->keys + turbo_bplus_tree_node_key_index_offset(tree, child_index + 1U),
            parent->keys + turbo_bplus_tree_node_key_index_offset(tree, child_index),
            move_keys * tree->key_stride);
    memmove(parent->children + (child_index + 2U), parent->children + (child_index + 1U),
            move_children * sizeof(turbo_bplus_tree_node_t *));
    memcpy(parent->keys + turbo_bplus_tree_node_key_index_offset(tree, child_index), sep_key, tree->key_size);
    parent->children[child_index + 1U] = right;
    right->parent = parent;
    ++parent->num_keys;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static inline int turbo_bplus_tree_split_child(turbo_bplus_tree_t *tree, turbo_bplus_tree_node_t *parent,
                                              size_t index) {
  turbo_bplus_tree_node_t *left;
  turbo_bplus_tree_node_t *right;
  size_t split_index;
  size_t slot;

  if (tree == NULL || parent == NULL || !turbo_bplus_tree_is_valid(tree)) return TURBO_EINVAL;
  if (index > parent->num_keys) return TURBO_EINVAL;

  left = parent->children[index];
  if (left == NULL || left->num_keys != tree->max_keys || parent->num_keys >= tree->max_keys) return TURBO_EINVAL;

  right = turbo_bplus_tree_node_alloc(tree, left->is_leaf);
  if (right == NULL) return TURBO_ENOMEM;

  split_index = tree->min_degree;
  if (!left->is_leaf) {
    const void *promoted = turbo_bplus_tree_node_key(tree, left, split_index);
    right->num_keys = left->num_keys - split_index - 1U;
    for (slot = 0U; slot < right->num_keys; ++slot) {
      memcpy(turbo_bplus_tree_node_key(tree, right, slot),
             turbo_bplus_tree_node_key(tree, left, split_index + 1U + slot), tree->key_size);
      if (left->children[split_index + 1U + slot] != NULL) {
        right->children[slot] = left->children[split_index + 1U + slot];
        right->children[slot]->parent = right;
      }
    }
    right->children[right->num_keys] = left->children[left->num_keys];
    if (right->children[right->num_keys] != NULL) {
      right->children[right->num_keys]->parent = right;
    }
    left->num_keys = split_index;
    right->parent = parent;
    if (turbo_bplus_tree_parent_insert(tree, parent, index, promoted, right) != TURBO_OK) {
      turbo_bplus_tree_node_destroy(right);
      return TURBO_EINVAL;
    }
  } else {
    size_t move_keys = left->num_keys - split_index;

    for (slot = 0U; slot < move_keys; ++slot) {
      memcpy(turbo_bplus_tree_node_key(tree, right, slot),
             turbo_bplus_tree_node_key(tree, left, split_index + slot), tree->key_size);
      memcpy(turbo_bplus_tree_node_value(tree, right, slot),
             turbo_bplus_tree_node_value(tree, left, split_index + slot), tree->value_size);
    }
    left->num_keys = split_index;
    right->num_keys = move_keys;
    right->next = left->next;
    left->next = right;
    right->parent = parent;
    if (turbo_bplus_tree_parent_insert(
            tree, parent, index, turbo_bplus_tree_node_key(tree, right, 0U), right) != TURBO_OK) {
      turbo_bplus_tree_node_destroy(right);
      return TURBO_EINVAL;
    }
  }
  return TURBO_OK;
}

static inline int turbo_bplus_tree_put(turbo_bplus_tree_t *tree, const void *key, const void *value) {
  turbo_bplus_tree_node_t *node;
  turbo_bplus_tree_node_t *new_root;
  turbo_bplus_tree_node_t *child;
  size_t index;
  int cmp;
  size_t move_keys;
  size_t move_values;

  if (!turbo_bplus_tree_is_valid(tree) || key == NULL || value == NULL) return TURBO_EINVAL;
  if (tree->root == NULL) {
    tree->root = turbo_bplus_tree_node_alloc(tree, true);
    if (tree->root == NULL) return TURBO_ENOMEM;
    memcpy(turbo_bplus_tree_node_key(tree, tree->root, 0U), key, tree->key_size);
    memcpy(turbo_bplus_tree_node_value(tree, tree->root, 0U), value, tree->value_size);
    tree->root->num_keys = 1U;
    tree->size = 1U;
    return TURBO_OK;
  }

  if (tree->root->num_keys == tree->max_keys) {
    new_root = turbo_bplus_tree_node_alloc(tree, false);
    if (new_root == NULL) return TURBO_ENOMEM;
    new_root->children[0U] = tree->root;
    tree->root->parent = new_root;
    if (turbo_bplus_tree_split_child(tree, new_root, 0U) != TURBO_OK) {
      turbo_bplus_tree_node_destroy(new_root);
      return TURBO_ENOMEM;
    }
    tree->root = new_root;
  }

  node = tree->root;
  while (!node->is_leaf) {
    index = turbo_bplus_tree_lower_bound(tree, node, key);
    cmp = 0;
    if (index < node->num_keys) {
      cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, node, index), tree->compare_ctx);
    }
    if (index < node->num_keys && cmp >= 0) {
      ++index;
    }
    child = node->children[index];
    if (child->num_keys == tree->max_keys) {
      if (turbo_bplus_tree_split_child(tree, node, index) != TURBO_OK) return TURBO_ENOMEM;
      if (tree->compare(key, turbo_bplus_tree_node_key(tree, node, index), tree->compare_ctx) >= 0) {
        ++index;
      }
      child = node->children[index];
    }
    node = child;
  }

  index = turbo_bplus_tree_lower_bound(tree, node, key);
  if (index < node->num_keys) {
    cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, node, index), tree->compare_ctx);
    if (cmp == 0) {
      memcpy(turbo_bplus_tree_node_value(tree, node, index), value, tree->value_size);
      return TURBO_OK;
    }
  }

  if (node->num_keys == tree->max_keys) return TURBO_ENOMEM;

  move_keys = node->num_keys - index;
  if (move_keys > 0U) {
    memmove(turbo_bplus_tree_node_key(tree, node, index + 1U),
            turbo_bplus_tree_node_key(tree, node, index), move_keys * tree->key_stride);
    move_values = move_keys;
    memmove(turbo_bplus_tree_node_value(tree, node, index + 1U),
            turbo_bplus_tree_node_value(tree, node, index), move_values * tree->value_stride);
  }
  memcpy(turbo_bplus_tree_node_key(tree, node, index), key, tree->key_size);
  memcpy(turbo_bplus_tree_node_value(tree, node, index), value, tree->value_size);
  ++node->num_keys;
  ++tree->size;
  return TURBO_OK;
}

static inline void *turbo_bplus_tree_get(turbo_bplus_tree_t *tree, const void *key) {
  size_t slot = 0U;
  turbo_bplus_tree_node_t *leaf;
  int cmp;
  if (!turbo_bplus_tree_is_valid(tree) || key == NULL) return NULL;
  leaf = turbo_bplus_tree_search_leaf(tree, key, &slot, NULL);
  if (leaf == NULL || slot >= leaf->num_keys) return NULL;
  cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, leaf, slot), tree->compare_ctx);
  if (cmp != 0) return NULL;
  return turbo_bplus_tree_node_value(tree, leaf, slot);
}

static inline const void *turbo_bplus_tree_get_const(const turbo_bplus_tree_t *tree, const void *key) {
  return turbo_bplus_tree_get((turbo_bplus_tree_t *)tree, key);
}

static inline bool turbo_bplus_tree_contains(const turbo_bplus_tree_t *tree, const void *key) {
  return turbo_bplus_tree_search_leaf(tree, key, NULL, NULL) != NULL &&
         turbo_bplus_tree_get((turbo_bplus_tree_t *)tree, key) != NULL;
}

static inline const void *turbo_bplus_tree_key_at(const turbo_bplus_tree_t *tree, size_t index) {
  turbo_bplus_tree_node_t *node;
  if (!turbo_bplus_tree_is_valid(tree)) return NULL;
  node = turbo_bplus_tree_leftmost_leaf(tree);
  while (node != NULL) {
    if (index < node->num_keys) {
      return turbo_bplus_tree_node_key(tree, node, index);
    }
    index -= node->num_keys;
    node = node->next;
  }
  return NULL;
}

static inline const void *turbo_bplus_tree_value_at(const turbo_bplus_tree_t *map, size_t index) {
  turbo_bplus_tree_node_t *node;
  if (!turbo_bplus_tree_is_valid(map)) return NULL;
  node = turbo_bplus_tree_leftmost_leaf(map);
  while (node != NULL) {
    if (index < node->num_keys) {
      return turbo_bplus_tree_node_value(map, node, index);
    }
    index -= node->num_keys;
    node = node->next;
  }
  return NULL;
}

static inline const void *turbo_bplus_tree_key_at_const(const turbo_bplus_tree_t *map, size_t index) {
  return turbo_bplus_tree_key_at(map, index);
}

static inline const void *turbo_bplus_tree_value_at_const(const turbo_bplus_tree_t *map, size_t index) {
  return turbo_bplus_tree_value_at(map, index);
}

static inline size_t turbo_bplus_tree_find_slot(const turbo_bplus_tree_t *tree, const void *key, bool *found) {
  turbo_bplus_tree_node_t *node;
  size_t slot = 0U;
  size_t i;
  int cmp;

  if (!turbo_bplus_tree_is_valid(tree) || key == NULL) {
    if (found != NULL) *found = false;
    return 0U;
  }
  node = turbo_bplus_tree_leftmost_leaf(tree);
  while (node != NULL) {
    for (i = 0U; i < node->num_keys; ++i) {
      cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, node, i), tree->compare_ctx);
      if (cmp == 0) {
        if (found != NULL) *found = true;
        return slot;
      }
      if (cmp < 0) {
        if (found != NULL) *found = false;
        return slot;
      }
      ++slot;
    }
    node = node->next;
  }
  if (found != NULL) *found = false;
  return slot;
}

static inline int turbo_bplus_tree_remove_rebuild(turbo_bplus_tree_t *tree, const void *key, void *out_value) {
  turbo_bplus_tree_t replacement;
  turbo_bplus_tree_node_t *node;
  size_t slot;
  bool removed = false;
  int cmp;
  int rc;

  if (!turbo_bplus_tree_is_valid(tree) || key == NULL) return TURBO_EINVAL;
  if (tree->root == NULL) return TURBO_ENOENT;
  if (turbo_bplus_tree_init_with_order(&replacement, tree->key_size, tree->value_size, tree->compare,
                                       tree->compare_ctx, tree->min_degree) != TURBO_OK) {
    return TURBO_ENOMEM;
  }
  node = turbo_bplus_tree_leftmost_leaf(tree);
  while (node != NULL) {
    for (slot = 0U; slot < node->num_keys; ++slot) {
      cmp = tree->compare(key, turbo_bplus_tree_node_key(tree, node, slot), tree->compare_ctx);
      if (!removed && cmp == 0) {
        removed = true;
        if (out_value != NULL) {
          memcpy(out_value, turbo_bplus_tree_node_value(tree, node, slot), tree->value_size);
        }
        continue;
      }
      rc = turbo_bplus_tree_put(&replacement, turbo_bplus_tree_node_key(tree, node, slot),
                               turbo_bplus_tree_node_value(tree, node, slot));
      if (rc != TURBO_OK) {
        turbo_bplus_tree_destroy(&replacement);
        return rc;
      }
    }
    node = node->next;
  }
  if (!removed) {
    turbo_bplus_tree_destroy(&replacement);
    return TURBO_ENOENT;
  }
  turbo_bplus_tree_destroy(tree);
  memcpy(tree, &replacement, sizeof(*tree));
  return TURBO_OK;
}

static inline int turbo_bplus_tree_remove(turbo_bplus_tree_t *tree, const void *key, void *out_value) {
  return turbo_bplus_tree_remove_rebuild(tree, key, out_value);
}

#define TURBO_BPLUS_TREE_DEFINE(name, key_type, value_type, compare_fn)                                   \
  typedef struct {                                                                                         \
    key_type key;                                                                                          \
    value_type value;                                                                                      \
  } name##_entry;                                                                                          \
  typedef struct {                                                                                         \
    turbo_bplus_tree_t raw;                                                                              \
  } name;                                                                                                 \
  static inline int name##_init(name *map) {                                                              \
    return turbo_bplus_tree_init(&map->raw, sizeof(key_type), sizeof(value_type), compare_fn, NULL);       \
  }                                                                                                       \
  static inline int name##_from(name *map, const name##_entry *entries, size_t count) {                   \
    size_t i;                                                                                              \
    int rc;                                                                                                \
    if (!map || (count > 0 && !entries)) return TURBO_EINVAL;                                              \
    rc = name##_init(map);                                                                                 \
    if (rc != TURBO_OK) return rc;                                                                         \
    rc = turbo_bplus_tree_reserve(&map->raw, count);                                                       \
    if (rc != TURBO_OK) {                                                                                  \
      turbo_bplus_tree_destroy(&map->raw);                                                                 \
      return rc;                                                                                           \
    }                                                                                                      \
    for (i = 0; i < count; ++i) {                                                                          \
      rc = turbo_bplus_tree_put(&map->raw, &entries[i].key, &entries[i].value);                            \
      if (rc != TURBO_OK) {                                                                                \
        turbo_bplus_tree_destroy(&map->raw);                                                               \
        return rc;                                                                                         \
      }                                                                                                    \
    }                                                                                                      \
    return TURBO_OK;                                                                                       \
  }                                                                                                        \
  static inline int name##_init_with_order(name *map, size_t min_degree) {                                 \
    return turbo_bplus_tree_init_with_order(&map->raw, sizeof(key_type), sizeof(value_type), compare_fn,     \
                                          NULL, min_degree);                                               \
  }                                                                                                       \
  static inline void name##_destroy(name *map) { turbo_bplus_tree_destroy(&map->raw); }                   \
  static inline void name##_clear(name *map) { turbo_bplus_tree_clear(&map->raw); }                       \
  static inline int name##_reserve(name *map, size_t capacity) {                                           \
    return turbo_bplus_tree_reserve(&map->raw, capacity);                                                  \
  }                                                                                                       \
  static inline int name##_put(name *map, key_type key, value_type value) {                                \
    return turbo_bplus_tree_put(&map->raw, &key, &value);                                                 \
  }                                                                                                       \
  static inline value_type *name##_get(name *map, key_type key) {                                          \
    return (value_type *)turbo_bplus_tree_get(&map->raw, &key);                                           \
  }                                                                                                       \
  static inline const value_type *name##_get_const(const name *map, key_type key) {                         \
    return (const value_type *)turbo_bplus_tree_get_const(&map->raw, &key);                               \
  }                                                                                                       \
  static inline bool name##_remove(name *map, key_type key, value_type *out_value) {                       \
    return turbo_bplus_tree_remove(&map->raw, &key, out_value) == TURBO_OK;                               \
  }                                                                                                       \
  static inline bool name##_contains(const name *map, key_type key) {                                      \
    return turbo_bplus_tree_contains(&map->raw, &key);                                                     \
  }                                                                                                       \
  static inline size_t name##_size(const name *map) { return turbo_bplus_tree_size(&map->raw); }            \
  static inline size_t name##_capacity(const name *map) { return turbo_bplus_tree_capacity(&map->raw); }    \
  static inline bool name##_empty(const name *map) { return turbo_bplus_tree_empty(&map->raw); }            \
  static inline key_type *name##_key_at(name *map, size_t index) {                                         \
    return (key_type *)turbo_bplus_tree_key_at(&map->raw, index);                                         \
  }                                                                                                       \
  static inline const key_type *name##_key_at_const(const name *map, size_t index) {                        \
    return (const key_type *)turbo_bplus_tree_key_at_const(&map->raw, index);                             \
  }                                                                                                       \
  static inline size_t name##_find_slot(name *map, key_type key, bool *found) {                            \
    return turbo_bplus_tree_find_slot(&map->raw, &key, found);                                            \
  }                                                                                                       \
  static inline value_type *name##_value_at(name *map, size_t index) {                                     \
    return (value_type *)turbo_bplus_tree_value_at(&map->raw, index);                                     \
  }                                                                                                       \
  static inline const value_type *name##_value_at_const(const name *map, size_t index) {                   \
    return (const value_type *)turbo_bplus_tree_value_at_const(&map->raw, index);                         \
  }

#undef TURBO_BPLUS_TREE_ALIGNOF

#ifdef __cplusplus
}
#endif

#endif /* TURBO_BPLUS_TREE_H */
