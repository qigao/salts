#include <turbostl/map.h>

#include "rbtree_internal.h"
#include "sequence_internal.h"

static rbtree_t *map_tree(map_t *map) {
  return map == NULL ? NULL : (rbtree_t *)map->impl;
}

static const rbtree_t *map_tree_const(const map_t *map) {
  return map == NULL ? NULL : (const rbtree_t *)map->impl;
}

static stl_status map_initialize(
    map_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    map_compare_fn compare, void *context) {
  rbtree_t *tree;
  stl_status status;
  if (map == NULL || map->impl != NULL) return STL_INVALID_ARGUMENT;
  status = rbtree_create(
      &tree, key_type, value_type, key_size, key_align, value_size,
      value_align, entry_limit, compare, context, false);
  if (status != STL_OK) return status;
  map->impl = tree;
  ++map->generation;
  return STL_OK;
}

stl_status map_raw_init(map_t *map,
                                const cmeta_type_desc *key_type,
                                const cmeta_type_desc *value_type,
                                size_t entry_limit) {
  stl_status status;
  if (map == NULL || map->impl != NULL) return STL_INVALID_ARGUMENT;
  status = sequence_require_type(key_type, true);
  if (status != STL_OK) return status;
  status = sequence_require_type(value_type, false);
  if (status != STL_OK) return status;
  return map_initialize(map, key_type, value_type, key_type->size,
                              key_type->align, value_type->size,
                              value_type->align, entry_limit, NULL, NULL);
}

stl_status map_init_bytes(
    map_t *map, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit, map_compare_fn compare,
    void *context) {
  if (compare == NULL) return STL_INVALID_ARGUMENT;
  return map_initialize(map, NULL, NULL, key_size, key_align,
                              value_size, value_align, entry_limit, compare,
                              context);
}

static stl_status map_from_common(
    map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, map_compare_fn compare, void *context) {
  map_t temporary = {0};
  stl_status status;
  size_t index;

  if (map == NULL) return STL_INVALID_ARGUMENT;
  if (count != 0u && (keys == NULL || values == NULL))
    return STL_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return STL_CAPACITY_EXCEEDED;
  status = key_type != NULL
               ? map_raw_init(&temporary, key_type, value_type, entry_limit)
               : map_init_bytes(&temporary, key_size, key_align,
                                      value_size, value_align, entry_limit,
                                      compare, context);
  if (status != STL_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = map_put(
        &temporary, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != STL_OK) {
      map_raw_destroy_storage(&temporary);
      return status;
    }
  }
  temporary.generation = map->generation + UINT64_C(1);
  if (map->impl != NULL) rbtree_destroy(map_tree(map));
  *map = temporary;
  return STL_OK;
}

stl_status map_raw_from_arrays(
    map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit) {
  if (key_type == NULL || value_type == NULL)
    return STL_INVALID_ARGUMENT;
  return map_from_common(
      map, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, entry_limit,
      NULL, NULL);
}

stl_status map_from_arrays_bytes(
    map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, map_compare_fn compare, void *context) {
  return map_from_common(map, keys, values, count, NULL, NULL,
                               key_size, key_align, value_size, value_align,
                               entry_limit, compare, context);
}

void map_raw_destroy_storage(map_t *map) {
  rbtree_t *tree = map_tree(map);
  if (tree == NULL) return;
  rbtree_destroy(tree);
  map->impl = NULL;
  ++map->generation;
}

void map_clear(map_t *map) {
  rbtree_t *tree = map_tree(map);
  if (tree == NULL || tree->size == 0u) return;
  rbtree_clear(tree);
  ++map->generation;
}

stl_status map_put(map_t *map, const void *key,
                               const void *value) {
  rbtree_t *tree = map_tree(map);
  rbtree_put_result result;
  stl_status status;
  if (tree == NULL) return STL_INVALID_ARGUMENT;
  status = rbtree_put(tree, key, value, &result);
  (void)result;
  if (status == STL_OK) ++map->generation;
  return status;
}

void *map_get(map_t *map, const void *key) {
  rbtree_node_t *node = rbtree_find(map_tree(map), key);
  return node == NULL ? NULL : node->value;
}

const void *map_get_const(const map_t *map, const void *key) {
  rbtree_node_t *node = rbtree_find(map_tree_const(map), key);
  return node == NULL ? NULL : node->value;
}

bool map_contains(const map_t *map, const void *key) {
  return rbtree_find(map_tree_const(map), key) != NULL;
}

stl_status map_remove(map_t *map, const void *key,
                                  void *out_value) {
  rbtree_t *tree = map_tree(map);
  rbtree_node_t *node;
  stl_status status;
  if (tree == NULL || key == NULL) return STL_INVALID_ARGUMENT;
  node = rbtree_find(tree, key);
  if (node == NULL) return STL_NOT_FOUND;
  status = rbtree_remove_node(tree, node, out_value);
  if (status == STL_OK) ++map->generation;
  return status;
}

size_t map_size(const map_t *map) {
  const rbtree_t *tree = map_tree_const(map);
  return tree == NULL ? 0u : tree->size;
}

size_t map_entry_limit(const map_t *map) {
  const rbtree_t *tree = map_tree_const(map);
  return tree == NULL ? 0u : tree->element_limit;
}

uint64_t map_generation(const map_t *map) {
  return map == NULL ? UINT64_C(0) : map->generation;
}

bool map_empty(const map_t *map) {
  return map_size(map) == 0u;
}

static map_iter_t map_iterator(const map_t *map,
                                           rbtree_node_t *node) {
  map_iter_t result = {map, node};
  return result;
}

map_iter_t map_begin(const map_t *map) {
  const rbtree_t *tree = map_tree_const(map);
  return map_iterator(map, tree == NULL ? NULL : tree->head);
}

map_iter_t map_end(const map_t *map) {
  return map_iterator(map, NULL);
}

map_iter_t map_lower_bound(const map_t *map,
                                       const void *key) {
  return map_iterator(
      map, rbtree_lower_bound(map_tree_const(map), key));
}

map_iter_t map_upper_bound(const map_t *map,
                                       const void *key) {
  return map_iterator(
      map, rbtree_upper_bound(map_tree_const(map), key));
}

stl_status map_iter_next(map_iter_t *iterator) {
  rbtree_node_t *node;
  if (iterator == NULL || map_tree_const(iterator->owner) == NULL ||
      iterator->node == NULL)
    return STL_NOT_FOUND;
  node = (rbtree_node_t *)iterator->node;
  iterator->node = node->next;
  return STL_OK;
}

stl_status map_iter_prev(map_iter_t *iterator) {
  const rbtree_t *tree;
  rbtree_node_t *node;
  if (iterator == NULL ||
      (tree = map_tree_const(iterator->owner)) == NULL)
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

bool map_iter_equal(map_iter_t left, map_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

const void *map_iter_key_const(map_iter_t iterator) {
  rbtree_node_t *node;
  if (map_tree_const(iterator.owner) == NULL || iterator.node == NULL)
    return NULL;
  node = (rbtree_node_t *)iterator.node;
  return node->key;
}

void *map_iter_value(map_iter_t iterator) {
  rbtree_node_t *node;
  if (map_tree_const(iterator.owner) == NULL || iterator.node == NULL)
    return NULL;
  node = (rbtree_node_t *)iterator.node;
  return node->value;
}

const void *map_iter_value_const(map_iter_t iterator) {
  return map_iter_value(iterator);
}

bool map_range_next(const map_t *map,
                          cmeta_range_cursor *cursor,
                          const void **out_key, const void **out_value) {
  const rbtree_t *tree = map_tree_const(map);
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
