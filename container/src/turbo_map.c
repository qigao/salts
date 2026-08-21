#include <turbo/container/map.h>

#include "turbo_rbtree_internal.h"
#include "turbo_sequence_internal.h"

static turbo_rbtree_t *turbo_map_tree(turbo_map_t *map) {
  return map == NULL ? NULL : (turbo_rbtree_t *)map->impl;
}

static const turbo_rbtree_t *turbo_map_tree_const(const turbo_map_t *map) {
  return map == NULL ? NULL : (const turbo_rbtree_t *)map->impl;
}

static container_status turbo_map_initialize(
    turbo_map_t *map, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t key_size, size_t key_align,
    size_t value_size, size_t value_align, size_t entry_limit,
    turbo_map_compare_fn compare, void *context) {
  turbo_rbtree_t *tree;
  container_status status;
  if (map == NULL || map->impl != NULL) return CONTAINER_INVALID_ARGUMENT;
  status = turbo_rbtree_create(
      &tree, key_type, value_type, key_size, key_align, value_size,
      value_align, entry_limit, compare, context, false);
  if (status != CONTAINER_OK) return status;
  map->impl = tree;
  ++map->generation;
  return CONTAINER_OK;
}

container_status turbo_map_init(turbo_map_t *map,
                                const cmeta_type_desc *key_type,
                                const cmeta_type_desc *value_type,
                                size_t entry_limit) {
  container_status status;
  if (map == NULL || map->impl != NULL) return CONTAINER_INVALID_ARGUMENT;
  status = turbo_sequence_require_type(key_type, true);
  if (status != CONTAINER_OK) return status;
  status = turbo_sequence_require_type(value_type, false);
  if (status != CONTAINER_OK) return status;
  return turbo_map_initialize(map, key_type, value_type, key_type->size,
                              key_type->align, value_type->size,
                              value_type->align, entry_limit, NULL, NULL);
}

container_status turbo_map_init_bytes(
    turbo_map_t *map, size_t key_size, size_t key_align, size_t value_size,
    size_t value_align, size_t entry_limit, turbo_map_compare_fn compare,
    void *context) {
  if (compare == NULL) return CONTAINER_INVALID_ARGUMENT;
  return turbo_map_initialize(map, NULL, NULL, key_size, key_align,
                              value_size, value_align, entry_limit, compare,
                              context);
}

static container_status turbo_map_from_common(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_map_compare_fn compare, void *context) {
  turbo_map_t temporary = {0};
  container_status status;
  size_t index;

  if (map == NULL) return CONTAINER_INVALID_ARGUMENT;
  if (count != 0u && (keys == NULL || values == NULL))
    return CONTAINER_INVALID_ARGUMENT;
  if (count != 0u &&
      (key_size > SIZE_MAX / count || value_size > SIZE_MAX / count))
    return CONTAINER_CAPACITY_EXCEEDED;
  status = key_type != NULL
               ? turbo_map_init(&temporary, key_type, value_type, entry_limit)
               : turbo_map_init_bytes(&temporary, key_size, key_align,
                                      value_size, value_align, entry_limit,
                                      compare, context);
  if (status != CONTAINER_OK) return status;
  for (index = 0u; index < count; ++index) {
    status = turbo_map_put(
        &temporary, (const unsigned char *)keys + index * key_size,
        (const unsigned char *)values + index * value_size);
    if (status != CONTAINER_OK) {
      turbo_map_destroy(&temporary);
      return status;
    }
  }
  temporary.generation = map->generation + UINT64_C(1);
  if (map->impl != NULL) turbo_rbtree_destroy(turbo_map_tree(map));
  *map = temporary;
  return CONTAINER_OK;
}

container_status turbo_map_from_arrays(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t entry_limit) {
  if (key_type == NULL || value_type == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  return turbo_map_from_common(
      map, keys, values, count, key_type, value_type, key_type->size,
      key_type->align, value_type->size, value_type->align, entry_limit,
      NULL, NULL);
}

container_status turbo_map_from_arrays_bytes(
    turbo_map_t *map, const void *keys, const void *values, size_t count,
    size_t key_size, size_t key_align, size_t value_size, size_t value_align,
    size_t entry_limit, turbo_map_compare_fn compare, void *context) {
  return turbo_map_from_common(map, keys, values, count, NULL, NULL,
                               key_size, key_align, value_size, value_align,
                               entry_limit, compare, context);
}

void turbo_map_destroy(turbo_map_t *map) {
  turbo_rbtree_t *tree = turbo_map_tree(map);
  if (tree == NULL) return;
  turbo_rbtree_destroy(tree);
  map->impl = NULL;
  ++map->generation;
}

void turbo_map_clear(turbo_map_t *map) {
  turbo_rbtree_t *tree = turbo_map_tree(map);
  if (tree == NULL || tree->size == 0u) return;
  turbo_rbtree_clear(tree);
  ++map->generation;
}

container_status turbo_map_put(turbo_map_t *map, const void *key,
                               const void *value) {
  turbo_rbtree_t *tree = turbo_map_tree(map);
  turbo_rbtree_put_result result;
  container_status status;
  if (tree == NULL) return CONTAINER_INVALID_ARGUMENT;
  status = turbo_rbtree_put(tree, key, value, &result);
  (void)result;
  if (status == CONTAINER_OK) ++map->generation;
  return status;
}

void *turbo_map_get(turbo_map_t *map, const void *key) {
  turbo_rbtree_node_t *node = turbo_rbtree_find(turbo_map_tree(map), key);
  return node == NULL ? NULL : node->value;
}

const void *turbo_map_get_const(const turbo_map_t *map, const void *key) {
  turbo_rbtree_node_t *node = turbo_rbtree_find(turbo_map_tree_const(map), key);
  return node == NULL ? NULL : node->value;
}

bool turbo_map_contains(const turbo_map_t *map, const void *key) {
  return turbo_rbtree_find(turbo_map_tree_const(map), key) != NULL;
}

container_status turbo_map_remove(turbo_map_t *map, const void *key,
                                  void *out_value) {
  turbo_rbtree_t *tree = turbo_map_tree(map);
  turbo_rbtree_node_t *node;
  container_status status;
  if (tree == NULL || key == NULL) return CONTAINER_INVALID_ARGUMENT;
  node = turbo_rbtree_find(tree, key);
  if (node == NULL) return CONTAINER_NOT_FOUND;
  status = turbo_rbtree_remove_node(tree, node, out_value);
  if (status == CONTAINER_OK) ++map->generation;
  return status;
}

size_t turbo_map_size(const turbo_map_t *map) {
  const turbo_rbtree_t *tree = turbo_map_tree_const(map);
  return tree == NULL ? 0u : tree->size;
}

size_t turbo_map_entry_limit(const turbo_map_t *map) {
  const turbo_rbtree_t *tree = turbo_map_tree_const(map);
  return tree == NULL ? 0u : tree->element_limit;
}

uint64_t turbo_map_generation(const turbo_map_t *map) {
  return map == NULL ? UINT64_C(0) : map->generation;
}

bool turbo_map_empty(const turbo_map_t *map) {
  return turbo_map_size(map) == 0u;
}

static turbo_map_iter_t turbo_map_iterator(const turbo_map_t *map,
                                           turbo_rbtree_node_t *node) {
  turbo_map_iter_t result = {map, node};
  return result;
}

turbo_map_iter_t turbo_map_begin(const turbo_map_t *map) {
  const turbo_rbtree_t *tree = turbo_map_tree_const(map);
  return turbo_map_iterator(map, tree == NULL ? NULL : tree->head);
}

turbo_map_iter_t turbo_map_end(const turbo_map_t *map) {
  return turbo_map_iterator(map, NULL);
}

turbo_map_iter_t turbo_map_lower_bound(const turbo_map_t *map,
                                       const void *key) {
  return turbo_map_iterator(
      map, turbo_rbtree_lower_bound(turbo_map_tree_const(map), key));
}

turbo_map_iter_t turbo_map_upper_bound(const turbo_map_t *map,
                                       const void *key) {
  return turbo_map_iterator(
      map, turbo_rbtree_upper_bound(turbo_map_tree_const(map), key));
}

container_status turbo_map_iter_next(turbo_map_iter_t *iterator) {
  turbo_rbtree_node_t *node;
  if (iterator == NULL || turbo_map_tree_const(iterator->owner) == NULL ||
      iterator->node == NULL)
    return CONTAINER_NOT_FOUND;
  node = (turbo_rbtree_node_t *)iterator->node;
  iterator->node = node->next;
  return CONTAINER_OK;
}

container_status turbo_map_iter_prev(turbo_map_iter_t *iterator) {
  const turbo_rbtree_t *tree;
  turbo_rbtree_node_t *node;
  if (iterator == NULL ||
      (tree = turbo_map_tree_const(iterator->owner)) == NULL)
    return CONTAINER_INVALID_ARGUMENT;
  if (iterator->node == NULL) {
    if (tree->tail == NULL) return CONTAINER_NOT_FOUND;
    iterator->node = tree->tail;
    return CONTAINER_OK;
  }
  node = (turbo_rbtree_node_t *)iterator->node;
  if (node->previous == NULL) return CONTAINER_NOT_FOUND;
  iterator->node = node->previous;
  return CONTAINER_OK;
}

bool turbo_map_iter_equal(turbo_map_iter_t left, turbo_map_iter_t right) {
  return left.owner == right.owner && left.node == right.node;
}

const void *turbo_map_iter_key_const(turbo_map_iter_t iterator) {
  turbo_rbtree_node_t *node;
  if (turbo_map_tree_const(iterator.owner) == NULL || iterator.node == NULL)
    return NULL;
  node = (turbo_rbtree_node_t *)iterator.node;
  return node->key;
}

void *turbo_map_iter_value(turbo_map_iter_t iterator) {
  turbo_rbtree_node_t *node;
  if (turbo_map_tree_const(iterator.owner) == NULL || iterator.node == NULL)
    return NULL;
  node = (turbo_rbtree_node_t *)iterator.node;
  return node->value;
}

const void *turbo_map_iter_value_const(turbo_map_iter_t iterator) {
  return turbo_map_iter_value(iterator);
}

bool turbo_map_range_next(const turbo_map_t *map,
                          cmeta_range_cursor *cursor,
                          const void **out_key, const void **out_value) {
  const turbo_rbtree_t *tree = turbo_map_tree_const(map);
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
