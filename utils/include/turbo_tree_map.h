#ifndef TURBO_TREE_MAP_H
#define TURBO_TREE_MAP_H

#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*turbo_tree_map_compare_fn)(const void *left, const void *right, void *ctx);

typedef struct turbo_tree_map_node_t {
  struct turbo_tree_map_node_t *left;
  struct turbo_tree_map_node_t *right;
  struct turbo_tree_map_node_t *parent;
  bool red;
  size_t size;
  unsigned char data[];
} turbo_tree_map_node_t;

typedef union turbo_tree_map_max_align {
  long double long_double_value;
  long long long_long_value;
  void *pointer_value;
  size_t size_value;
} turbo_tree_map_max_align_t;

typedef struct {
  turbo_tree_map_node_t *root;
  size_t key_size;
  size_t value_size;
  size_t value_offset;
  size_t align;
  turbo_tree_map_compare_fn compare;
  void *compare_ctx;
  size_t size;
} turbo_tree_map_t;

/* NOTE: Ordered map implemented with a red-black tree. */

static inline size_t turbo_tree_map_node_size(const turbo_tree_map_node_t *node) {
  return node == NULL ? 0U : node->size;
}

static inline void turbo_tree_map_update_size(turbo_tree_map_node_t *node) {
  if (!node) return;
  node->size = 1U + turbo_tree_map_node_size(node->left) + turbo_tree_map_node_size(node->right);
}

static inline void turbo_tree_map_recompute_upward(turbo_tree_map_node_t *node) {
  while (node) {
    turbo_tree_map_update_size(node);
    node = node->parent;
  }
}

static inline int turbo_tree_map_align_up(size_t value, size_t alignment, size_t *out) {
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

static inline bool turbo_tree_map_is_red(const turbo_tree_map_node_t *node) {
  return node != NULL && node->red;
}

static inline bool turbo_tree_map_is_valid(const turbo_tree_map_t *map) {
  return map != NULL && map->compare != NULL && map->key_size > 0U && map->value_size > 0U &&
         map->align > 0U && map->value_offset > 0U;
}

static inline unsigned char *turbo_tree_map_node_key(const turbo_tree_map_t *map,
                                                    turbo_tree_map_node_t *node) {
  (void)map;
  return node == NULL ? NULL : (unsigned char *)(node->data);
}

static inline unsigned char *turbo_tree_map_node_value(const turbo_tree_map_t *map,
                                                      turbo_tree_map_node_t *node) {
  if (node == NULL || map == NULL) return NULL;
  return (unsigned char *)(node->data) + map->value_offset;
}

static inline turbo_tree_map_node_t *turbo_tree_map_node_alloc(const turbo_tree_map_t *map,
                                                              const void *key,
                                                              const void *value) {
  turbo_tree_map_node_t *node;
  size_t node_size;
  if (map == NULL || key == NULL || value == NULL) return NULL;
  if (map->value_offset > SIZE_MAX - map->value_size) return NULL;
  node_size = sizeof(*node) + map->value_offset + map->value_size;
  node = (turbo_tree_map_node_t *)malloc(node_size);
  if (node == NULL) return NULL;
  memset(node, 0, sizeof(*node));
  node->red = true;
  node->size = 1U;
  memcpy(turbo_tree_map_node_key(map, node), key, map->key_size);
  memcpy(turbo_tree_map_node_value(map, node), value, map->value_size);
  return node;
}

static inline void turbo_tree_map_node_destroy(turbo_tree_map_node_t *node) {
  if (!node) return;
  turbo_tree_map_node_destroy(node->left);
  turbo_tree_map_node_destroy(node->right);
  free(node);
}

static inline turbo_tree_map_node_t *turbo_tree_map_min_node(turbo_tree_map_node_t *node) {
  while (node != NULL && node->left != NULL) node = node->left;
  return node;
}

static inline turbo_tree_map_node_t *turbo_tree_map_search_node(const turbo_tree_map_t *map,
                                                              const void *key) {
  int cmp;
  turbo_tree_map_node_t *node = map == NULL ? NULL : map->root;
  if (key == NULL) return NULL;
  while (node != NULL) {
    cmp = map->compare(key, turbo_tree_map_node_key(map, node), map->compare_ctx);
    if (cmp == 0) return node;
    node = (cmp < 0) ? node->left : node->right;
  }
  return NULL;
}

static inline void turbo_tree_map_rotate_left(turbo_tree_map_t *map, turbo_tree_map_node_t *node) {
  turbo_tree_map_node_t *right;
  turbo_tree_map_node_t *parent;

  if (map == NULL || node == NULL) return;
  right = node->right;
  if (right == NULL) return;

  node->right = right->left;
  if (right->left != NULL) right->left->parent = node;
  parent = node->parent;
  right->parent = parent;
  if (parent == NULL) {
    map->root = right;
  } else if (node == parent->left) {
    parent->left = right;
  } else {
    parent->right = right;
  }
  right->left = node;
  node->parent = right;

  turbo_tree_map_update_size(node);
  turbo_tree_map_update_size(right);
}

static inline void turbo_tree_map_rotate_right(turbo_tree_map_t *map, turbo_tree_map_node_t *node) {
  turbo_tree_map_node_t *left;
  turbo_tree_map_node_t *parent;

  if (map == NULL || node == NULL) return;
  left = node->left;
  if (left == NULL) return;

  node->left = left->right;
  if (left->right != NULL) left->right->parent = node;
  parent = node->parent;
  left->parent = parent;
  if (parent == NULL) {
    map->root = left;
  } else if (node == parent->left) {
    parent->left = left;
  } else {
    parent->right = left;
  }
  left->right = node;
  node->parent = left;

  turbo_tree_map_update_size(node);
  turbo_tree_map_update_size(left);
}

static inline void turbo_tree_map_fix_insert(turbo_tree_map_t *map, turbo_tree_map_node_t *node) {
  turbo_tree_map_node_t *parent;
  turbo_tree_map_node_t *grandparent;
  turbo_tree_map_node_t *uncle;

  while (node != NULL && node != map->root && turbo_tree_map_is_red(node->parent)) {
    parent = node->parent;
    grandparent = parent->parent;
    if (grandparent == NULL) break;
    if (parent == grandparent->left) {
      uncle = grandparent->right;
      if (turbo_tree_map_is_red(uncle)) {
        parent->red = false;
        uncle->red = false;
        grandparent->red = true;
        node = grandparent;
        continue;
      }
      if (node == parent->right) {
        node = parent;
        turbo_tree_map_rotate_left(map, node);
        parent = node->parent;
        grandparent = parent->parent;
      }
      parent->red = false;
      if (grandparent != NULL) {
        grandparent->red = true;
        turbo_tree_map_rotate_right(map, grandparent);
      }
    } else {
      uncle = grandparent->left;
      if (turbo_tree_map_is_red(uncle)) {
        parent->red = false;
        uncle->red = false;
        grandparent->red = true;
        node = grandparent;
        continue;
      }
      if (node == parent->left) {
        node = parent;
        turbo_tree_map_rotate_right(map, node);
        parent = node->parent;
        grandparent = parent->parent;
      }
      parent->red = false;
      if (grandparent != NULL) {
        grandparent->red = true;
        turbo_tree_map_rotate_left(map, grandparent);
      }
    }
  }
  if (map->root != NULL) {
    map->root->red = false;
  }
  if (node != NULL) turbo_tree_map_recompute_upward(node);
}

static inline void turbo_tree_map_transplant(turbo_tree_map_t *map, turbo_tree_map_node_t *target,
                                            turbo_tree_map_node_t *replacement) {
  if (map == NULL || target == NULL) return;
  if (target->parent == NULL) {
    map->root = replacement;
  } else if (target == target->parent->left) {
    target->parent->left = replacement;
  } else {
    target->parent->right = replacement;
  }
  if (replacement != NULL) replacement->parent = target->parent;
}

static inline void turbo_tree_map_fix_remove(turbo_tree_map_t *map, turbo_tree_map_node_t *node,
                                            turbo_tree_map_node_t *parent) {
  turbo_tree_map_node_t *sibling;
  while (node != map->root && !turbo_tree_map_is_red(node)) {
    if (node == (parent == NULL ? NULL : parent->left)) {
      sibling = parent == NULL ? NULL : parent->right;
      if (turbo_tree_map_is_red(sibling)) {
        sibling->red = false;
        if (parent != NULL) parent->red = true;
        turbo_tree_map_rotate_left(map, parent);
        sibling = parent == NULL ? NULL : parent->right;
      }
      if (!turbo_tree_map_is_red(sibling == NULL ? NULL : sibling->left) &&
          !turbo_tree_map_is_red(sibling == NULL ? NULL : sibling->right)) {
        if (sibling != NULL) sibling->red = true;
        node = parent;
        parent = node == NULL ? NULL : node->parent;
      } else {
        if (!turbo_tree_map_is_red(sibling == NULL ? NULL : sibling->right)) {
          if (sibling != NULL && sibling->left != NULL) sibling->left->red = false;
          if (sibling != NULL) sibling->red = true;
          turbo_tree_map_rotate_right(map, sibling);
          sibling = parent == NULL ? NULL : parent->right;
        }
        if (sibling != NULL) sibling->red = parent == NULL ? false : parent->red;
        if (parent != NULL) parent->red = false;
        if (sibling != NULL && sibling->right != NULL) sibling->right->red = false;
        turbo_tree_map_rotate_left(map, parent);
        node = map->root;
        parent = NULL;
      }
    } else {
      sibling = parent == NULL ? NULL : parent->left;
      if (turbo_tree_map_is_red(sibling)) {
        sibling->red = false;
        if (parent != NULL) parent->red = true;
        turbo_tree_map_rotate_right(map, parent);
        sibling = parent == NULL ? NULL : parent->left;
      }
      if (!turbo_tree_map_is_red(sibling == NULL ? NULL : sibling->left) &&
          !turbo_tree_map_is_red(sibling == NULL ? NULL : sibling->right)) {
        if (sibling != NULL) sibling->red = true;
        node = parent;
        parent = node == NULL ? NULL : node->parent;
      } else {
        if (!turbo_tree_map_is_red(sibling == NULL ? NULL : sibling->left)) {
          if (sibling != NULL && sibling->right != NULL) sibling->right->red = false;
          if (sibling != NULL) sibling->red = true;
          turbo_tree_map_rotate_left(map, sibling);
          sibling = parent == NULL ? NULL : parent->left;
        }
        if (sibling != NULL) sibling->red = parent == NULL ? false : parent->red;
        if (parent != NULL) parent->red = false;
        if (sibling != NULL && sibling->left != NULL) sibling->left->red = false;
        turbo_tree_map_rotate_right(map, parent);
        node = map->root;
        parent = NULL;
      }
    }
  }
  if (node != NULL) node->red = false;
  if (map->root != NULL) map->root->red = false;
}

static inline int turbo_tree_map_init(turbo_tree_map_t *map, size_t key_size, size_t value_size,
                                     turbo_tree_map_compare_fn compare, void *compare_ctx) {
  size_t value_offset;

  if (map == NULL || key_size == 0U || value_size == 0U || compare == NULL) return TURBO_EINVAL;
  value_offset = 0U;
  if (turbo_tree_map_align_up(key_size, _Alignof(turbo_tree_map_max_align_t), &value_offset) !=
      TURBO_OK) {
    return TURBO_ENOMEM;
  }
  if (value_offset > SIZE_MAX - value_size) return TURBO_ENOMEM;
  memset(map, 0, sizeof(*map));
  map->key_size = key_size;
  map->value_size = value_size;
  map->value_offset = value_offset;
  map->align = _Alignof(turbo_tree_map_max_align_t);
  map->compare = compare;
  map->compare_ctx = compare_ctx;
  return TURBO_OK;
}

static inline void turbo_tree_map_destroy(turbo_tree_map_t *map) {
  if (!map) return;
  turbo_tree_map_node_destroy(map->root);
  memset(map, 0, sizeof(*map));
}

static inline void turbo_tree_map_clear(turbo_tree_map_t *map) {
  if (!map) return;
  turbo_tree_map_node_destroy(map->root);
  map->root = NULL;
  map->size = 0U;
}

static inline int turbo_tree_map_reserve(turbo_tree_map_t *map, size_t min_capacity) {
  (void)min_capacity;
  if (!turbo_tree_map_is_valid(map)) return TURBO_EINVAL;
  return TURBO_OK;
}

static inline size_t turbo_tree_map_size(const turbo_tree_map_t *map) {
  return map == NULL ? 0U : map->size;
}

static inline size_t turbo_tree_map_capacity(const turbo_tree_map_t *map) {
  return map == NULL ? 0U : map->size;
}

static inline bool turbo_tree_map_empty(const turbo_tree_map_t *map) {
  return map == NULL || map->size == 0U;
}

static inline size_t turbo_tree_map_find_slot(const turbo_tree_map_t *map, const void *key, bool *found) {
  size_t slot = 0U;
  size_t left_size = 0U;
  int cmp;
  turbo_tree_map_node_t *node;

  if (!turbo_tree_map_is_valid(map) || key == NULL) {
    if (found != NULL) *found = false;
    return 0U;
  }

  node = map->root;
  while (node != NULL) {
    cmp = map->compare(key, turbo_tree_map_node_key(map, node), map->compare_ctx);
    if (cmp == 0) {
      if (found != NULL) *found = true;
      return slot + turbo_tree_map_node_size(node->left);
    }

    left_size = turbo_tree_map_node_size(node->left);
    if (cmp < 0) {
      node = node->left;
    } else {
      slot += left_size + 1U;
      node = node->right;
    }
  }
  if (found != NULL) *found = false;
  return slot;
}

static inline turbo_tree_map_node_t *turbo_tree_map_node_at(turbo_tree_map_t *map, size_t index) {
  turbo_tree_map_node_t *node;
  size_t left_size;

  if (!turbo_tree_map_is_valid(map) || map->root == NULL || index >= map->size) return NULL;
  node = map->root;
  while (node != NULL) {
    left_size = turbo_tree_map_node_size(node->left);
    if (index < left_size) {
      node = node->left;
      continue;
    }
    if (index == left_size) return node;
    index -= left_size + 1U;
    node = node->right;
  }
  return NULL;
}

static inline void *turbo_tree_map_key_at(turbo_tree_map_t *map, size_t index) {
  if (!turbo_tree_map_is_valid(map) || map->root == NULL || index >= map->size) return NULL;
  return turbo_tree_map_node_key(map, turbo_tree_map_node_at(map, index));
}

static inline const void *turbo_tree_map_key_at_const(const turbo_tree_map_t *map, size_t index) {
  return turbo_tree_map_key_at((turbo_tree_map_t *)map, index);
}

static inline void *turbo_tree_map_value_at(turbo_tree_map_t *map, size_t index) {
  if (!turbo_tree_map_is_valid(map) || map->root == NULL || index >= map->size) return NULL;
  return turbo_tree_map_node_value(map, turbo_tree_map_node_at(map, index));
}

static inline const void *turbo_tree_map_value_at_const(const turbo_tree_map_t *map, size_t index) {
  return turbo_tree_map_value_at((turbo_tree_map_t *)map, index);
}

static inline int turbo_tree_map_put(turbo_tree_map_t *map, const void *key, const void *value) {
  int cmp;
  turbo_tree_map_node_t *node;
  turbo_tree_map_node_t *cursor;
  turbo_tree_map_node_t *parent;

  if (!turbo_tree_map_is_valid(map) || key == NULL || value == NULL) return TURBO_EINVAL;

  node = turbo_tree_map_search_node(map, key);
  if (node != NULL) {
    memcpy(turbo_tree_map_node_value(map, node), value, map->value_size);
    return TURBO_OK;
  }

  node = turbo_tree_map_node_alloc(map, key, value);
  if (node == NULL) return TURBO_ENOMEM;

  cursor = map->root;
  parent = NULL;
  while (cursor != NULL) {
    parent = cursor;
    cmp = map->compare(key, turbo_tree_map_node_key(map, cursor), map->compare_ctx);
    if (cmp < 0) {
      cursor = cursor->left;
    } else {
      cursor = cursor->right;
    }
  }
  node->parent = parent;
  if (parent == NULL) {
    map->root = node;
  } else if (map->compare(key, turbo_tree_map_node_key(map, parent), map->compare_ctx) < 0) {
    parent->left = node;
  } else {
    parent->right = node;
  }
  turbo_tree_map_fix_insert(map, node);
  turbo_tree_map_recompute_upward(node);
  map->size += 1U;
  return TURBO_OK;
}

static inline void *turbo_tree_map_get(turbo_tree_map_t *map, const void *key) {
  return turbo_tree_map_node_value(map, turbo_tree_map_search_node(map, key));
}

static inline const void *turbo_tree_map_get_const(const turbo_tree_map_t *map, const void *key) {
  return turbo_tree_map_get((turbo_tree_map_t *)map, key);
}

static inline bool turbo_tree_map_contains(const turbo_tree_map_t *map, const void *key) {
  return turbo_tree_map_search_node(map, key) != NULL;
}

static inline int turbo_tree_map_remove(turbo_tree_map_t *map, const void *key, void *out_value) {
  int cmp;
  turbo_tree_map_node_t *node;
  turbo_tree_map_node_t *successor;
  turbo_tree_map_node_t *parent;
  turbo_tree_map_node_t *replacement;
  bool original_color;

  if (!turbo_tree_map_is_valid(map) || key == NULL) return TURBO_EINVAL;
  if (map->root == NULL) return TURBO_ENOENT;

  node = map->root;
  while (node != NULL) {
    cmp = map->compare(key, turbo_tree_map_node_key(map, node), map->compare_ctx);
    if (cmp == 0) break;
    node = cmp < 0 ? node->left : node->right;
  }
  if (node == NULL) return TURBO_ENOENT;
  if (out_value != NULL) {
    memcpy(out_value, turbo_tree_map_node_value(map, node), map->value_size);
  }

  replacement = NULL;
  original_color = node->red;
  if (node->left == NULL) {
    replacement = node->right;
    parent = node->parent;
    turbo_tree_map_transplant(map, node, node->right);
  } else if (node->right == NULL) {
    replacement = node->left;
    parent = node->parent;
    turbo_tree_map_transplant(map, node, node->left);
  } else {
    successor = turbo_tree_map_min_node(node->right);
    original_color = successor->red;
    replacement = successor->right;
    if (successor->parent == node) {
      parent = successor;
      if (replacement != NULL) replacement->parent = successor;
    } else {
      parent = successor->parent;
      turbo_tree_map_transplant(map, successor, successor->right);
      successor->right = node->right;
      if (successor->right != NULL) successor->right->parent = successor;
    }
    turbo_tree_map_transplant(map, node, successor);
    successor->left = node->left;
    successor->left->parent = successor;
    successor->red = node->red;
    turbo_tree_map_update_size(successor);
  }
  if (!original_color) {
    if (!turbo_tree_map_is_red(replacement)) {
      turbo_tree_map_fix_remove(map, replacement, parent);
    } else if (replacement != NULL) {
      replacement->red = false;
    }
  }
  if (map->root != NULL && map->root->parent != NULL) map->root->parent = NULL;
  if (replacement != NULL) {
    turbo_tree_map_recompute_upward(replacement);
  } else if (parent != NULL) {
    turbo_tree_map_recompute_upward(parent);
  } else if (map->root != NULL) {
    turbo_tree_map_recompute_upward(map->root);
  }
  free(node);
  map->size -= 1U;
  if (map->root != NULL) {
    turbo_tree_map_recompute_upward(map->root);
  }
  if (map->root != NULL) map->root->red = false;
  return TURBO_OK;
}

#define TURBO_TREE_MAP_DEFINE(name, key_type, value_type, compare_fn)                                  \
  typedef struct {                                                                                     \
    turbo_tree_map_t raw;                                                                             \
  } name;                                                                                             \
  static inline int name##_init(name *map) {                                                           \
    return turbo_tree_map_init(&map->raw, sizeof(key_type), sizeof(value_type), compare_fn, NULL);      \
  }                                                                                                   \
  static inline void name##_destroy(name *map) { turbo_tree_map_destroy(&map->raw); }                   \
  static inline void name##_clear(name *map) { turbo_tree_map_clear(&map->raw); }                      \
  static inline int name##_reserve(name *map, size_t capacity) {                                       \
    return turbo_tree_map_reserve(&map->raw, capacity);                                               \
  }                                                                                                   \
  static inline int name##_put(name *map, key_type key, value_type value) {                            \
    return turbo_tree_map_put(&map->raw, &key, &value);                                               \
  }                                                                                                   \
  static inline value_type *name##_get(name *map, key_type key) {                                      \
    return (value_type *)turbo_tree_map_get(&map->raw, &key);                                         \
  }                                                                                                   \
  static inline const value_type *name##_get_const(const name *map, key_type key) {                    \
    return (const value_type *)turbo_tree_map_get_const(&map->raw, &key);                              \
  }                                                                                                   \
  static inline bool name##_remove(name *map, key_type key, value_type *out_value) {                   \
    return turbo_tree_map_remove(&map->raw, &key, out_value) == TURBO_OK;                            \
  }                                                                                                   \
  static inline bool name##_contains(const name *map, key_type key) {                                  \
    return turbo_tree_map_contains(&map->raw, &key);                                                   \
  }                                                                                                   \
  static inline size_t name##_size(const name *map) { return turbo_tree_map_size(&map->raw); }          \
  static inline size_t name##_capacity(const name *map) { return turbo_tree_map_capacity(&map->raw); }  \
  static inline bool name##_empty(const name *map) { return turbo_tree_map_empty(&map->raw); }          \
  static inline key_type *name##_key_at(name *map, size_t index) {                                     \
    return (key_type *)turbo_tree_map_key_at(&map->raw, index);                                       \
  }                                                                                                   \
  static inline const key_type *name##_key_at_const(const name *map, size_t index) {                    \
    return (const key_type *)turbo_tree_map_key_at_const(&map->raw, index);                            \
  }                                                                                                   \
  static inline size_t name##_find_slot(name *map, key_type key, bool *found) {                        \
    return turbo_tree_map_find_slot(&map->raw, &key, found);                                           \
  }                                                                                                   \
  static inline value_type *name##_value_at(name *map, size_t index) {                                 \
    return (value_type *)turbo_tree_map_value_at(&map->raw, index);                                   \
  }                                                                                                   \
  static inline const value_type *name##_value_at_const(const name *map, size_t index) {                \
    return (const value_type *)turbo_tree_map_value_at_const(&map->raw, index);                        \
  }

#ifdef __cplusplus
}
#endif

#endif /* TURBO_TREE_MAP_H */
