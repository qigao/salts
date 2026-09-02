#include <rocida/stl/multimap.h>

#include "rbtree_internal.h"
#include "sequence_internal.h"

static rbtree_t *multimap_tree(multimap_t *map) {
  return map == NULL ? NULL : (rbtree_t *)map->impl;
}

static const rbtree_t *multimap_tree_const(
    const multimap_t *map) {
  return map == NULL ? NULL : (const rbtree_t *)map->impl;
}

static stl_status multimap_initialize(
    multimap_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    multimap_compare_fn compare, void *context) {
  rbtree_t *tree;
  stl_status status;
  if (map == NULL || map->impl != NULL) return STL_INVALID_ARGUMENT;
  status = rbtree_create(
      &tree, key_type, value_type, key_size, key_align, value_size,
      value_align, element_limit, compare, context, true);
  if (status != STL_OK) return status;
  map->impl = tree;
  ++map->generation;
  return STL_OK;
}

stl_status multimap_raw_init(
    multimap_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t element_limit) {
  stl_status status;
  if (map == NULL || map->impl != NULL) return STL_INVALID_ARGUMENT;
  status = sequence_require_type(key_type, true);
  if (status != STL_OK) return status;
  status = sequence_require_type(value_type, false);
  if (status != STL_OK) return status;
  return multimap_initialize(
      map, key_type, value_type, key_type->size, key_type->align,
      value_type->size, value_type->align, element_limit, NULL, NULL);
}

stl_status multimap_init_bytes(
    multimap_t *map, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t element_limit,
    multimap_compare_fn compare, void *context) {
  if (compare == NULL) return STL_INVALID_ARGUMENT;
  return multimap_initialize(map, NULL, NULL, key_size, key_align,
                                   value_size, value_align, element_limit,
                                   compare, context);
}

static stl_status multimap_from_common(
    multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, multimap_compare_fn compare, void *context) {
  multimap_t temporary = {0};
  stl_status status;
  size_t index;

  if (map == NULL || (count != 0u && (keys == NULL || values == NULL)))
    return STL_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return STL_CAPACITY_EXCEEDED;
  status = key_type != NULL
               ? multimap_raw_init(&temporary, key_type, value_type,
                                     element_limit)
               : multimap_init_bytes(
                     &temporary, key_size, key_align, value_size, value_align,
                     element_limit, compare, context);
  if (status != STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = multimap_put(
        &temporary, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != STL_OK) {
      multimap_raw_destroy_storage(&temporary);
      return status;
    }
  }
  temporary.generation = map->generation + UINT64_C(1);
  if (map->impl != NULL) rbtree_destroy(multimap_tree(map));
  *map = temporary;
  return STL_OK;
}

stl_status multimap_raw_from_arrays(
    multimap_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t element_limit) {
  if (key_type == NULL || value_type == NULL)
    return STL_INVALID_ARGUMENT;
  return multimap_from_common(
      map, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, element_limit,
      NULL, NULL);
}

stl_status multimap_from_arrays_bytes(
    multimap_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t element_limit, multimap_compare_fn compare, void *context) {
  return multimap_from_common(
      map, keys, values, count, NULL, NULL, key_size, key_align, value_size,
      value_align, element_limit, compare, context);
}

void multimap_raw_destroy_storage(multimap_t *map) {
  rbtree_t *tree = multimap_tree(map);
  if (tree == NULL) return;
  rbtree_destroy(tree);
  map->impl = NULL;
  ++map->generation;
}

void multimap_clear(multimap_t *map) {
  rbtree_t *tree = multimap_tree(map);
  if (tree == NULL || tree->size == 0u) return;
  rbtree_clear(tree);
  ++map->generation;
}

stl_status multimap_put(multimap_t *map, const void *key,
                                    const void *value) {
  rbtree_t *tree = multimap_tree(map);
  rbtree_put_result result;
  stl_status status;
  if (tree == NULL) return STL_INVALID_ARGUMENT;
  status = rbtree_put(tree, key, value, &result);
  (void)result;
  if (status == STL_OK) ++map->generation;
  return status;
}

bool multimap_contains(const multimap_t *map, const void *key) {
  return rbtree_find(multimap_tree_const(map), key) != NULL;
}

size_t multimap_count(const multimap_t *map, const void *key) {
  const rbtree_t *tree = multimap_tree_const(map);
  rbtree_node_t *node;
  rbtree_node_t *end;
  size_t count = 0u;
  if (tree == NULL || key == NULL) return 0u;
  node = rbtree_lower_bound(tree, key);
  end = rbtree_upper_bound(tree, key);
  while (node != end) {
    ++count;
    node = node->next;
  }
  return count;
}

bool multimap_remove(multimap_t *map, const void *key,
                           void *out_value) {
  rbtree_t *tree = multimap_tree(map);
  rbtree_node_t *first;
  rbtree_node_t *end;
  rbtree_node_t *node;
  if (tree == NULL || key == NULL) return false;
  first = rbtree_lower_bound(tree, key);
  end = rbtree_upper_bound(tree, key);
  if (first == end) return false;
  node = end == NULL ? tree->tail : end->previous;
  if (rbtree_remove_node(tree, node, out_value) != STL_OK)
    return false;
  ++map->generation;
  return true;
}

size_t multimap_erase(multimap_t *map, const void *key) {
  rbtree_t *tree = multimap_tree(map);
  rbtree_node_t *node;
  rbtree_node_t *end;
  size_t removed = 0u;
  if (tree == NULL || key == NULL) return 0u;
  node = rbtree_lower_bound(tree, key);
  end = rbtree_upper_bound(tree, key);
  while (node != end) {
    rbtree_node_t *next = node->next;
    if (rbtree_remove_node(tree, node, NULL) != STL_OK) break;
    ++removed;
    node = next;
  }
  if (removed != 0u) ++map->generation;
  return removed;
}

size_t multimap_size(const multimap_t *map) {
  const rbtree_t *tree = multimap_tree_const(map);
  return tree == NULL ? 0u : tree->size;
}

size_t multimap_element_limit(const multimap_t *map) {
  const rbtree_t *tree = multimap_tree_const(map);
  return tree == NULL ? 0u : tree->element_limit;
}

uint64_t multimap_generation(const multimap_t *map) {
  return map == NULL ? UINT64_C(0) : map->generation;
}

bool multimap_empty(const multimap_t *map) {
  return multimap_size(map) == 0u;
}

static multimap_iter_t multimap_iterator(
    const multimap_t *map, rbtree_node_t *node) {
  multimap_iter_t result = {map, node};
  return result;
}

multimap_iter_t multimap_begin(const multimap_t *map) {
  const rbtree_t *tree = multimap_tree_const(map);
  return multimap_iterator(map, tree == NULL ? NULL : tree->head);
}

multimap_iter_t multimap_end(const multimap_t *map) {
  return multimap_iterator(map, NULL);
}

multimap_iter_t multimap_lower_bound(
    const multimap_t *map, const void *key) {
  return multimap_iterator(
      map, rbtree_lower_bound(multimap_tree_const(map), key));
}

multimap_iter_t multimap_upper_bound(
    const multimap_t *map, const void *key) {
  return multimap_iterator(
      map, rbtree_upper_bound(multimap_tree_const(map), key));
}

stl_status multimap_iter_next(multimap_iter_t *iterator) {
  rbtree_node_t *node;
  if (iterator == NULL || multimap_tree_const(iterator->owner) == NULL ||
      iterator->node == NULL)
    return STL_NOT_FOUND;
  node = (rbtree_node_t *)iterator->node;
  iterator->node = node->next;
  return STL_OK;
}

stl_status multimap_iter_prev(multimap_iter_t *iterator) {
  const rbtree_t *tree;
  rbtree_node_t *node;
  if (iterator == NULL ||
      (tree = multimap_tree_const(iterator->owner)) == NULL)
    return STL_INVALID_ARGUMENT;
  if (iterator->node == NULL) {
    if (tree->tail == NULL) return STL_NOT_FOUND;
    iterator->node = tree->tail;
    return STL_OK;
  }
  node = (rbtree_node_t *)iterator->node;
  if (node->previous == NULL) return STL_NOT_FOUND;
  iterator->node = node->previous;
  return STL_OK;
}

bool multimap_iter_equal(multimap_iter_t left,
                               multimap_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

const void *multimap_iter_key_const(multimap_iter_t iterator) {
  rbtree_node_t *node;
  if (multimap_tree_const(iterator.owner) == NULL ||
      iterator.node == NULL)
    return NULL;
  node = (rbtree_node_t *)iterator.node;
  return node->key;
}

void *multimap_iter_value(multimap_iter_t iterator) {
  rbtree_node_t *node;
  if (multimap_tree_const(iterator.owner) == NULL ||
      iterator.node == NULL)
    return NULL;
  node = (rbtree_node_t *)iterator.node;
  return node->value;
}

const void *multimap_iter_value_const(
    multimap_iter_t iterator) {
  return multimap_iter_value(iterator);
}

bool multimap_range_next(const multimap_t *map,
                               cmeta_range_cursor *cursor,
                               const void **out_key,
                               const void **out_value) {
  const rbtree_t *tree = multimap_tree_const(map);
  rbtree_node_t *node;
  if (tree == NULL || cursor == NULL || out_key == NULL || out_value == NULL)
    return false;
  if (cursor->state[1] == NULL) {
    cursor->state[1] = (void *)map;
    node = tree->head;
  } else {
    if (cursor->state[1] != (void *)map) return false;
    node = (rbtree_node_t *)cursor->state[0];
  }
  if (node == NULL) return false;
  *out_key = node->key;
  *out_value = node->value;
  cursor->state[0] = node->next;
  return true;
}
