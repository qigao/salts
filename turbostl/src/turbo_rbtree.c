#include "turbo_rbtree_internal.h"

#include "turbo_sequence_internal.h"

#include <stdlib.h>
#include <string.h>

static int turbo_rbtree_compare_key(const turbo_rbtree_t *tree,
                                    const void *left, const void *right) {
  return tree->key_type != NULL
             ? tree->key_type->traits->compare(left, right)
             : tree->compare(left, right, tree->compare_context);
}

static turbo_stl_status turbo_rbtree_copy_object(
    const cmeta_type_desc *type, size_t size, size_t stride, size_t alignment,
    const void *source, void **out_object) {
  turbo_stl_status status;
  if (source == NULL || out_object == NULL) return TURBO_STL_INVALID_ARGUMENT;
  *out_object = NULL;
  status = turbo_sequence_allocate(1u, stride, alignment, out_object);
  if (status != TURBO_STL_OK) return status;
  status = turbo_sequence_copy(type, size, *out_object, source);
  if (status != TURBO_STL_OK) {
    turbo_sequence_deallocate(*out_object);
    *out_object = NULL;
  }
  return status;
}

static void turbo_rbtree_destroy_object(const cmeta_type_desc *type,
                                        void *object) {
  if (object == NULL) return;
  (void)turbo_sequence_destroy_value(type, object);
  turbo_sequence_deallocate(object);
}

static turbo_rbtree_node_t *turbo_rbtree_node_create(
    const turbo_rbtree_t *tree, const void *key, const void *value,
    turbo_stl_status *out_status) {
  turbo_rbtree_node_t *node;
  turbo_stl_status status;

  node = (turbo_rbtree_node_t *)calloc(1u, sizeof(*node));
  if (node == NULL) {
    *out_status = TURBO_STL_OUT_OF_MEMORY;
    return NULL;
  }
  status = turbo_rbtree_copy_object(tree->key_type, tree->key_size,
                                    tree->key_stride, tree->key_align, key,
                                    &node->key);
  if (status == TURBO_STL_OK)
    status = turbo_rbtree_copy_object(
        tree->value_type, tree->value_size, tree->value_stride,
        tree->value_align, value, &node->value);
  if (status != TURBO_STL_OK) {
    turbo_rbtree_destroy_object(tree->key_type, node->key);
    free(node);
    *out_status = status;
    return NULL;
  }
  node->red = true;
  *out_status = TURBO_STL_OK;
  return node;
}

static bool turbo_rbtree_is_red(const turbo_rbtree_node_t *node) {
  return node != NULL && node->red;
}

static bool turbo_rbtree_is_black(const turbo_rbtree_node_t *node) {
  return node == NULL || !node->red;
}

static void turbo_rbtree_rotate_left(turbo_rbtree_t *tree,
                                     turbo_rbtree_node_t *node) {
  turbo_rbtree_node_t *pivot = node->right;
  node->right = pivot->left;
  if (pivot->left != NULL) pivot->left->parent = node;
  pivot->parent = node->parent;
  if (node->parent == NULL)
    tree->root = pivot;
  else if (node == node->parent->left)
    node->parent->left = pivot;
  else
    node->parent->right = pivot;
  pivot->left = node;
  node->parent = pivot;
}

static void turbo_rbtree_rotate_right(turbo_rbtree_t *tree,
                                      turbo_rbtree_node_t *node) {
  turbo_rbtree_node_t *pivot = node->left;
  node->left = pivot->right;
  if (pivot->right != NULL) pivot->right->parent = node;
  pivot->parent = node->parent;
  if (node->parent == NULL)
    tree->root = pivot;
  else if (node == node->parent->right)
    node->parent->right = pivot;
  else
    node->parent->left = pivot;
  pivot->right = node;
  node->parent = pivot;
}

static void turbo_rbtree_insert_fix(turbo_rbtree_t *tree,
                                    turbo_rbtree_node_t *node) {
  while (node->parent != NULL && node->parent->red) {
    turbo_rbtree_node_t *parent = node->parent;
    turbo_rbtree_node_t *grandparent = parent->parent;
    if (parent == grandparent->left) {
      turbo_rbtree_node_t *uncle = grandparent->right;
      if (turbo_rbtree_is_red(uncle)) {
        parent->red = false;
        uncle->red = false;
        grandparent->red = true;
        node = grandparent;
      } else {
        if (node == parent->right) {
          node = parent;
          turbo_rbtree_rotate_left(tree, node);
          parent = node->parent;
          grandparent = parent->parent;
        }
        parent->red = false;
        grandparent->red = true;
        turbo_rbtree_rotate_right(tree, grandparent);
      }
    } else {
      turbo_rbtree_node_t *uncle = grandparent->left;
      if (turbo_rbtree_is_red(uncle)) {
        parent->red = false;
        uncle->red = false;
        grandparent->red = true;
        node = grandparent;
      } else {
        if (node == parent->left) {
          node = parent;
          turbo_rbtree_rotate_right(tree, node);
          parent = node->parent;
          grandparent = parent->parent;
        }
        parent->red = false;
        grandparent->red = true;
        turbo_rbtree_rotate_left(tree, grandparent);
      }
    }
  }
  tree->root->red = false;
}

turbo_stl_status turbo_rbtree_create(
    turbo_rbtree_t **out_tree, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    turbo_rbtree_compare_fn compare, void *compare_context,
    bool allow_duplicates) {
  turbo_rbtree_t *tree;
  size_t key_stride;
  size_t value_stride;
  turbo_stl_status status;

  if (out_tree == NULL || (value_type == NULL && value_size == 0u) ||
      (key_type == NULL && compare == NULL))
    return TURBO_STL_INVALID_ARGUMENT;
  *out_tree = NULL;
  status = turbo_sequence_stride(key_size, key_align, &key_stride);
  if (status != TURBO_STL_OK) return status;
  status = turbo_sequence_stride(value_size, value_align, &value_stride);
  if (status != TURBO_STL_OK) return status;
  tree = (turbo_rbtree_t *)calloc(1u, sizeof(*tree));
  if (tree == NULL) return TURBO_STL_OUT_OF_MEMORY;
  tree->key_size = key_size;
  tree->key_stride = key_stride;
  tree->key_align = key_align;
  tree->value_size = value_size;
  tree->value_stride = value_stride;
  tree->value_align = value_align;
  tree->element_limit = element_limit;
  tree->key_type = key_type;
  tree->value_type = value_type;
  tree->compare = compare;
  tree->compare_context = compare_context;
  tree->allow_duplicates = allow_duplicates;
  *out_tree = tree;
  return TURBO_STL_OK;
}

void turbo_rbtree_clear(turbo_rbtree_t *tree) {
  turbo_rbtree_node_t *node;
  if (tree == NULL) return;
  node = tree->head;
  while (node != NULL) {
    turbo_rbtree_node_t *next = node->next;
    turbo_rbtree_destroy_object(tree->key_type, node->key);
    turbo_rbtree_destroy_object(tree->value_type, node->value);
    free(node);
    node = next;
  }
  tree->root = NULL;
  tree->head = NULL;
  tree->tail = NULL;
  tree->size = 0u;
}

void turbo_rbtree_destroy(turbo_rbtree_t *tree) {
  if (tree == NULL) return;
  turbo_rbtree_clear(tree);
  free(tree);
}

turbo_rbtree_node_t *turbo_rbtree_find(const turbo_rbtree_t *tree,
                                       const void *key) {
  turbo_rbtree_node_t *node;
  if (tree == NULL || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL) {
    int comparison = turbo_rbtree_compare_key(tree, key, node->key);
    if (comparison == 0) return node;
    node = comparison < 0 ? node->left : node->right;
  }
  return NULL;
}

turbo_rbtree_node_t *turbo_rbtree_lower_bound(const turbo_rbtree_t *tree,
                                              const void *key) {
  turbo_rbtree_node_t *node;
  turbo_rbtree_node_t *result = NULL;
  if (tree == NULL || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL) {
    int comparison = turbo_rbtree_compare_key(tree, node->key, key);
    if (comparison < 0) {
      node = node->right;
    } else {
      result = node;
      node = node->left;
    }
  }
  return result;
}

turbo_rbtree_node_t *turbo_rbtree_upper_bound(const turbo_rbtree_t *tree,
                                              const void *key) {
  turbo_rbtree_node_t *node;
  turbo_rbtree_node_t *result = NULL;
  if (tree == NULL || key == NULL) return NULL;
  node = tree->root;
  while (node != NULL) {
    int comparison = turbo_rbtree_compare_key(tree, node->key, key);
    if (comparison <= 0) {
      node = node->right;
    } else {
      result = node;
      node = node->left;
    }
  }
  return result;
}

turbo_stl_status turbo_rbtree_put(turbo_rbtree_t *tree, const void *key,
                                  const void *value,
                                  turbo_rbtree_put_result *out_result) {
  turbo_rbtree_node_t *node;
  turbo_rbtree_node_t *parent = NULL;
  int comparison = 0;
  turbo_stl_status status;

  if (tree == NULL || key == NULL || value == NULL || out_result == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  node = tree->root;
  while (node != NULL) {
    parent = node;
    comparison = turbo_rbtree_compare_key(tree, key, node->key);
    if (comparison == 0 && !tree->allow_duplicates) {
      void *replacement = NULL;
      status = turbo_rbtree_copy_object(
          tree->value_type, tree->value_size, tree->value_stride,
          tree->value_align, value, &replacement);
      if (status != TURBO_STL_OK) return status;
      turbo_rbtree_destroy_object(tree->value_type, node->value);
      node->value = replacement;
      *out_result = TURBO_RBTREE_REPLACED;
      return TURBO_STL_OK;
    }
    node = comparison < 0 ? node->left : node->right;
  }
  if (tree->size >= tree->element_limit)
    return TURBO_STL_CAPACITY_EXCEEDED;
  node = turbo_rbtree_node_create(tree, key, value, &status);
  if (node == NULL) return status;
  node->parent = parent;
  if (parent == NULL) {
    tree->root = node;
    tree->head = node;
    tree->tail = node;
  } else if (comparison < 0) {
    parent->left = node;
    node->next = parent;
    node->previous = parent->previous;
    parent->previous = node;
    if (node->previous != NULL)
      node->previous->next = node;
    else
      tree->head = node;
  } else {
    parent->right = node;
    node->previous = parent;
    node->next = parent->next;
    parent->next = node;
    if (node->next != NULL)
      node->next->previous = node;
    else
      tree->tail = node;
  }
  turbo_rbtree_insert_fix(tree, node);
  ++tree->size;
  *out_result = TURBO_RBTREE_INSERTED;
  return TURBO_STL_OK;
}

static turbo_rbtree_node_t *turbo_rbtree_minimum(turbo_rbtree_node_t *node) {
  while (node != NULL && node->left != NULL) node = node->left;
  return node;
}

static void turbo_rbtree_transplant(turbo_rbtree_t *tree,
                                    turbo_rbtree_node_t *removed,
                                    turbo_rbtree_node_t *replacement) {
  if (removed->parent == NULL)
    tree->root = replacement;
  else if (removed == removed->parent->left)
    removed->parent->left = replacement;
  else
    removed->parent->right = replacement;
  if (replacement != NULL) replacement->parent = removed->parent;
}

static void turbo_rbtree_delete_fix(turbo_rbtree_t *tree,
                                    turbo_rbtree_node_t *node,
                                    turbo_rbtree_node_t *parent) {
  while (node != tree->root && turbo_rbtree_is_black(node)) {
    turbo_rbtree_node_t *sibling;
    if (parent == NULL) break;
    if (node == parent->left) {
      sibling = parent->right;
      if (turbo_rbtree_is_red(sibling)) {
        sibling->red = false;
        parent->red = true;
        turbo_rbtree_rotate_left(tree, parent);
        sibling = parent->right;
      }
      if (sibling == NULL) {
        node = parent;
        parent = node->parent;
      } else if (turbo_rbtree_is_black(sibling->left) &&
                 turbo_rbtree_is_black(sibling->right)) {
        sibling->red = true;
        node = parent;
        parent = node->parent;
      } else {
        if (turbo_rbtree_is_black(sibling->right)) {
          if (sibling->left != NULL) sibling->left->red = false;
          sibling->red = true;
          turbo_rbtree_rotate_right(tree, sibling);
          sibling = parent->right;
        }
        sibling->red = parent->red;
        parent->red = false;
        if (sibling->right != NULL) sibling->right->red = false;
        turbo_rbtree_rotate_left(tree, parent);
        node = tree->root;
        parent = NULL;
      }
    } else {
      sibling = parent->left;
      if (turbo_rbtree_is_red(sibling)) {
        sibling->red = false;
        parent->red = true;
        turbo_rbtree_rotate_right(tree, parent);
        sibling = parent->left;
      }
      if (sibling == NULL) {
        node = parent;
        parent = node->parent;
      } else if (turbo_rbtree_is_black(sibling->right) &&
                 turbo_rbtree_is_black(sibling->left)) {
        sibling->red = true;
        node = parent;
        parent = node->parent;
      } else {
        if (turbo_rbtree_is_black(sibling->left)) {
          if (sibling->right != NULL) sibling->right->red = false;
          sibling->red = true;
          turbo_rbtree_rotate_left(tree, sibling);
          sibling = parent->left;
        }
        sibling->red = parent->red;
        parent->red = false;
        if (sibling->left != NULL) sibling->left->red = false;
        turbo_rbtree_rotate_right(tree, parent);
        node = tree->root;
        parent = NULL;
      }
    }
  }
  if (node != NULL) node->red = false;
}

turbo_stl_status turbo_rbtree_remove_node(turbo_rbtree_t *tree,
                                          turbo_rbtree_node_t *node,
                                          void *out_value) {
  turbo_rbtree_node_t *moved;
  turbo_rbtree_node_t *fix_node;
  turbo_rbtree_node_t *fix_parent;
  bool moved_was_red;
  turbo_stl_status status;

  if (tree == NULL || node == NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = out_value != NULL
               ? turbo_sequence_move_destroy(tree->value_type,
                                             tree->value_size, out_value,
                                             node->value)
               : turbo_sequence_destroy_value(tree->value_type,
                                              node->value);
  if (status != TURBO_STL_OK) return status;
  moved = node;
  moved_was_red = moved->red;
  if (node->left == NULL) {
    fix_node = node->right;
    fix_parent = node->parent;
    turbo_rbtree_transplant(tree, node, node->right);
  } else if (node->right == NULL) {
    fix_node = node->left;
    fix_parent = node->parent;
    turbo_rbtree_transplant(tree, node, node->left);
  } else {
    moved = turbo_rbtree_minimum(node->right);
    moved_was_red = moved->red;
    fix_node = moved->right;
    if (moved->parent == node) {
      fix_parent = moved;
      if (fix_node != NULL) fix_node->parent = moved;
    } else {
      fix_parent = moved->parent;
      turbo_rbtree_transplant(tree, moved, moved->right);
      moved->right = node->right;
      moved->right->parent = moved;
    }
    turbo_rbtree_transplant(tree, node, moved);
    moved->left = node->left;
    moved->left->parent = moved;
    moved->red = node->red;
  }
  if (!moved_was_red) turbo_rbtree_delete_fix(tree, fix_node, fix_parent);
  if (node->previous != NULL)
    node->previous->next = node->next;
  else
    tree->head = node->next;
  if (node->next != NULL)
    node->next->previous = node->previous;
  else
    tree->tail = node->previous;
  turbo_rbtree_destroy_object(tree->key_type, node->key);
  turbo_sequence_deallocate(node->value);
  free(node);
  --tree->size;
  return TURBO_STL_OK;
}
