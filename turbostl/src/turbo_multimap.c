#include <turbo/stl/multimap.h>

#include "turbo_rbtree_internal.h"
#include "turbo_sequence_internal.h"

static turbo_rbtree_t *turbo_multimap_tree(turbo_multimap_t *map) {
  return map == NULL ? NULL : (turbo_rbtree_t *)map->impl;
}

static const turbo_rbtree_t *turbo_multimap_tree_const(
    const turbo_multimap_t *map) {
  return map == NULL ? NULL : (const turbo_rbtree_t *)map->impl;
}

static turbo_stl_status turbo_multimap_initialize(
    turbo_multimap_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    turbo_multimap_compare_fn compare, void *context) {
  turbo_rbtree_t *tree;
  turbo_stl_status status;
  if (map == NULL || map->impl != NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = turbo_rbtree_create(
      &tree, key_type, value_type, key_size, key_align, value_size,
      value_align, element_limit, compare, context, true);
  if (status != TURBO_STL_OK) return status;
  map->impl = tree;
  ++map->generation;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_multimap_init(
    turbo_multimap_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t element_limit) {
  turbo_stl_status status;
  if (map == NULL || map->impl != NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(key_type, true);
  if (status != TURBO_STL_OK) return status;
  status = turbo_sequence_require_type(value_type, false);
  if (status != TURBO_STL_OK) return status;
  return turbo_multimap_initialize(
      map, key_type, value_type, key_type->size, key_type->align,
      value_type->size, value_type->align, element_limit, NULL, NULL);
}

turbo_stl_status turbo_multimap_init_bytes(
    turbo_multimap_t *map, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    turbo_multimap_compare_fn compare, void *context) {
  if (compare == NULL) return TURBO_STL_INVALID_ARGUMENT;
  return turbo_multimap_initialize(map, NULL, NULL, key_size, key_align,
                                   value_size, value_align, element_limit,
                                   compare, context);
}

static turbo_stl_status turbo_multimap_from_common(
    turbo_multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, turbo_multimap_compare_fn compare, void *context) {
  turbo_multimap_t temporary = {0};
  turbo_stl_status status;
  size_t index;

  if (map == NULL || (count != 0u && (keys == NULL || values == NULL)))
    return TURBO_STL_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return TURBO_STL_CAPACITY_EXCEEDED;
  status = key_type != NULL
               ? turbo_multimap_init(&temporary, key_type, value_type,
                                     element_limit)
               : turbo_multimap_init_bytes(
                     &temporary, key_size, key_align, value_size, value_align,
                     element_limit, compare, context);
  if (status != TURBO_STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_multimap_put(
        &temporary, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != TURBO_STL_OK) {
      turbo_multimap_destroy(&temporary);
      return status;
    }
  }
  temporary.generation = map->generation + UINT64_C(1);
  if (map->impl != NULL) turbo_rbtree_destroy(turbo_multimap_tree(map));
  *map = temporary;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_multimap_from_arrays(
    turbo_multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t element_limit) {
  if (key_type == NULL || value_type == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  return turbo_multimap_from_common(
      map, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, element_limit,
      NULL, NULL);
}

turbo_stl_status turbo_multimap_from_arrays_bytes(
    turbo_multimap_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, turbo_multimap_compare_fn compare, void *context) {
  return turbo_multimap_from_common(
      map, keys, values, count, NULL, NULL, key_size, key_align, value_size,
      value_align, element_limit, compare, context);
}

void turbo_multimap_destroy(turbo_multimap_t *map) {
  turbo_rbtree_t *tree = turbo_multimap_tree(map);
  if (tree == NULL) return;
  turbo_rbtree_destroy(tree);
  map->impl = NULL;
  ++map->generation;
}

void turbo_multimap_clear(turbo_multimap_t *map) {
  turbo_rbtree_t *tree = turbo_multimap_tree(map);
  if (tree == NULL || tree->size == 0u) return;
  turbo_rbtree_clear(tree);
  ++map->generation;
}

turbo_stl_status turbo_multimap_put(turbo_multimap_t *map, const void *key,
                                    const void *value) {
  turbo_rbtree_t *tree = turbo_multimap_tree(map);
  turbo_rbtree_put_result result;
  turbo_stl_status status;
  if (tree == NULL) return TURBO_STL_INVALID_ARGUMENT;
  status = turbo_rbtree_put(tree, key, value, &result);
  (void)result;
  if (status == TURBO_STL_OK) ++map->generation;
  return status;
}

bool turbo_multimap_contains(const turbo_multimap_t *map, const void *key) {
  return turbo_rbtree_find(turbo_multimap_tree_const(map), key) != NULL;
}

size_t turbo_multimap_count(const turbo_multimap_t *map, const void *key) {
  const turbo_rbtree_t *tree = turbo_multimap_tree_const(map);
  turbo_rbtree_node_t *node;
  turbo_rbtree_node_t *end;
  size_t count = 0u;
  if (tree == NULL || key == NULL) return 0u;
  node = turbo_rbtree_lower_bound(tree, key);
  end = turbo_rbtree_upper_bound(tree, key);
  while (node != end) {
    ++count;
    node = node->next;
  }
  return count;
}

bool turbo_multimap_remove(turbo_multimap_t *map, const void *key,
                           void *out_value) {
  turbo_rbtree_t *tree = turbo_multimap_tree(map);
  turbo_rbtree_node_t *first;
  turbo_rbtree_node_t *end;
  turbo_rbtree_node_t *node;
  if (tree == NULL || key == NULL) return false;
  first = turbo_rbtree_lower_bound(tree, key);
  end = turbo_rbtree_upper_bound(tree, key);
  if (first == end) return false;
  node = end == NULL ? tree->tail : end->previous;
  if (turbo_rbtree_remove_node(tree, node, out_value) != TURBO_STL_OK)
    return false;
  ++map->generation;
  return true;
}

size_t turbo_multimap_erase(turbo_multimap_t *map, const void *key) {
  turbo_rbtree_t *tree = turbo_multimap_tree(map);
  turbo_rbtree_node_t *node;
  turbo_rbtree_node_t *end;
  size_t removed = 0u;
  if (tree == NULL || key == NULL) return 0u;
  node = turbo_rbtree_lower_bound(tree, key);
  end = turbo_rbtree_upper_bound(tree, key);
  while (node != end) {
    turbo_rbtree_node_t *next = node->next;
    if (turbo_rbtree_remove_node(tree, node, NULL) != TURBO_STL_OK) break;
    ++removed;
    node = next;
  }
  if (removed != 0u) ++map->generation;
  return removed;
}

size_t turbo_multimap_size(const turbo_multimap_t *map) {
  const turbo_rbtree_t *tree = turbo_multimap_tree_const(map);
  return tree == NULL ? 0u : tree->size;
}

size_t turbo_multimap_element_limit(const turbo_multimap_t *map) {
  const turbo_rbtree_t *tree = turbo_multimap_tree_const(map);
  return tree == NULL ? 0u : tree->element_limit;
}

uint64_t turbo_multimap_generation(const turbo_multimap_t *map) {
  return map == NULL ? UINT64_C(0) : map->generation;
}

bool turbo_multimap_empty(const turbo_multimap_t *map) {
  return turbo_multimap_size(map) == 0u;
}

static turbo_multimap_iter_t turbo_multimap_iterator(
    const turbo_multimap_t *map, turbo_rbtree_node_t *node) {
  turbo_multimap_iter_t result = {map, node};
  return result;
}

turbo_multimap_iter_t turbo_multimap_begin(const turbo_multimap_t *map) {
  const turbo_rbtree_t *tree = turbo_multimap_tree_const(map);
  return turbo_multimap_iterator(map, tree == NULL ? NULL : tree->head);
}

turbo_multimap_iter_t turbo_multimap_end(const turbo_multimap_t *map) {
  return turbo_multimap_iterator(map, NULL);
}

turbo_multimap_iter_t turbo_multimap_lower_bound(
    const turbo_multimap_t *map, const void *key) {
  return turbo_multimap_iterator(
      map, turbo_rbtree_lower_bound(turbo_multimap_tree_const(map), key));
}

turbo_multimap_iter_t turbo_multimap_upper_bound(
    const turbo_multimap_t *map, const void *key) {
  return turbo_multimap_iterator(
      map, turbo_rbtree_upper_bound(turbo_multimap_tree_const(map), key));
}

turbo_stl_status turbo_multimap_iter_next(turbo_multimap_iter_t *iterator) {
  turbo_rbtree_node_t *node;
  if (iterator == NULL || turbo_multimap_tree_const(iterator->owner) == NULL ||
      iterator->node == NULL)
    return TURBO_STL_NOT_FOUND;
  node = (turbo_rbtree_node_t *)iterator->node;
  iterator->node = node->next;
  return TURBO_STL_OK;
}

turbo_stl_status turbo_multimap_iter_prev(turbo_multimap_iter_t *iterator) {
  const turbo_rbtree_t *tree;
  turbo_rbtree_node_t *node;
  if (iterator == NULL ||
      (tree = turbo_multimap_tree_const(iterator->owner)) == NULL)
    return TURBO_STL_INVALID_ARGUMENT;
  if (iterator->node == NULL) {
    if (tree->tail == NULL) return TURBO_STL_NOT_FOUND;
    iterator->node = tree->tail;
    return TURBO_STL_OK;
  }
  node = (turbo_rbtree_node_t *)iterator->node;
  if (node->previous == NULL) return TURBO_STL_NOT_FOUND;
  iterator->node = node->previous;
  return TURBO_STL_OK;
}

bool turbo_multimap_iter_equal(turbo_multimap_iter_t left,
                               turbo_multimap_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

const void *turbo_multimap_iter_key_const(turbo_multimap_iter_t iterator) {
  turbo_rbtree_node_t *node;
  if (turbo_multimap_tree_const(iterator.owner) == NULL ||
      iterator.node == NULL)
    return NULL;
  node = (turbo_rbtree_node_t *)iterator.node;
  return node->key;
}

void *turbo_multimap_iter_value(turbo_multimap_iter_t iterator) {
  turbo_rbtree_node_t *node;
  if (turbo_multimap_tree_const(iterator.owner) == NULL ||
      iterator.node == NULL)
    return NULL;
  node = (turbo_rbtree_node_t *)iterator.node;
  return node->value;
}

const void *turbo_multimap_iter_value_const(
    turbo_multimap_iter_t iterator) {
  return turbo_multimap_iter_value(iterator);
}

bool turbo_multimap_range_next(const turbo_multimap_t *map,
                               cmeta_range_cursor *cursor,
                               const void **out_key,
                               const void **out_value) {
  const turbo_rbtree_t *tree = turbo_multimap_tree_const(map);
  turbo_rbtree_node_t *node;
  if (tree == NULL || cursor == NULL || out_key == NULL || out_value == NULL)
    return false;
  if (cursor->state[1] == NULL) {
    cursor->state[1] = (void *)map;
    node = tree->head;
  } else {
    if (cursor->state[1] != (void *)map) return false;
    node = (turbo_rbtree_node_t *)cursor->state[0];
  }
  if (node == NULL) return false;
  *out_key = node->key;
  *out_value = node->value;
  cursor->state[0] = node->next;
  return true;
}
