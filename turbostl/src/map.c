#include <cmeta/meta.h>
#include <turbostl/map.h>
#include <turbostl/detail/instance_meta.h>

#include "rbtree_internal.h"
#include "sequence_internal.h"

#include <string.h>

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

typedef enum stl_map_view {
  STL_MAP_KEYS = 0,
  STL_MAP_VALUES = 1,
  STL_MAP_ENTRIES = 2
} stl_map_view;

static cmeta_status stl_map_cmeta_status(stl_status status) {
  switch (status) {
    case STL_OK: return CMETA_OK;
    case STL_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
    case STL_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
    case STL_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
    case STL_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
    case STL_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
    case STL_EMPTY:
    case STL_NOT_FOUND:
    default: return CMETA_CALLBACK_ERROR;
  }
}

static bool stl_map_entry_binding_valid(const cmeta_type_desc *key_type,
                                        const cmeta_type_desc *value_type) {
  return key_type != NULL && value_type != NULL &&
         cmeta_type_require_traits(
             key_type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY |
                           CMETA_TRAIT_COMPARE) == CMETA_OK &&
         cmeta_type_require_traits(
             value_type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK;
}

static cmeta_status stl_map_validate_input(
    const cmeta_type_desc *input, const cmeta_type_desc *expected) {
  cmeta_status status;
  if (input == NULL || expected == NULL || expected->traits == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (!cmeta_type_equal(input, expected))
    return CMETA_TYPE_MISMATCH;
  status = cmeta_type_require_traits(input, expected->traits->flags);
  return status == CMETA_OK ? CMETA_OK : CMETA_TRAIT_MISSING;
}

static cmeta_status stl_map_validate_entry_binding(
    const cmeta_entry *entry, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type) {
  if (entry == NULL || entry->key_type == NULL || entry->value_type == NULL ||
      entry->key == NULL || entry->value == NULL || key_type == NULL ||
      value_type == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (!cmeta_type_equal(entry->key_type, key_type) ||
      !cmeta_type_equal(entry->value_type, value_type))
    return CMETA_TYPE_MISMATCH;
  return CMETA_OK;
}

static size_t stl_map_range_size(const void *object) {
  return map_size((const map_t *)object);
}

static uint64_t stl_map_range_version(const void *object) {
  return map_generation((const map_t *)object);
}

static cmeta_gen_status stl_map_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value,
    stl_map_view view) {
  const map_t *map = (const map_t *)object;
  const void *key = NULL;
  const void *value = NULL;
  cmeta_range_cursor next_cursor;
  if (map == NULL || cursor == NULL || out_value == NULL ||
      map->key_type == NULL || map->value_type == NULL)
    return CMETA_GEN_ERROR;
  next_cursor = *cursor;
  if (!map_range_next(map, &next_cursor, &key, &value))
    return CMETA_GEN_DONE;
  if (key == NULL || value == NULL)
    return CMETA_GEN_ERROR;
  if (view == STL_MAP_KEYS) {
    if (!stl_cmeta_range_construct(map->key_type, out_value, key))
      return CMETA_GEN_ERROR;
  } else if (view == STL_MAP_VALUES) {
    if (!stl_cmeta_range_construct(map->value_type, out_value, value))
      return CMETA_GEN_ERROR;
  } else {
    const cmeta_entry entry = {
        .key_type = map->key_type,
        .value_type = map->value_type,
        .key = key,
        .value = value,
        .key_storage = NULL,
        .value_storage = NULL};
    if (!stl_cmeta_range_construct(
            &cmeta_type_ordered_entry, out_value, &entry))
      return CMETA_GEN_ERROR;
  }
  *cursor = next_cursor;
  return cursor->state[0] == NULL ? CMETA_GEN_VALUE_AND_DONE :
                                    CMETA_GEN_VALUE;
}

static cmeta_gen_status stl_map_keys_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
  return stl_map_next(object, cursor, out_value, STL_MAP_KEYS);
}

static cmeta_gen_status stl_map_values_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
  return stl_map_next(object, cursor, out_value, STL_MAP_VALUES);
}

static cmeta_gen_status stl_map_entries_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
  return stl_map_next(object, cursor, out_value, STL_MAP_ENTRIES);
}

static cmeta_range stl_map_keys_range_factory(const void *object) {
  const map_t *map = (const map_t *)object;
  cmeta_range range = {0};
  if (map == NULL || map->key_type == NULL)
    return range;
  range = (cmeta_range){
      object,
      map->key_type,
      stl_cmeta_range_flags_for(
          map->key_type,
          CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
              CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE),
      stl_map_range_size,
      stl_map_keys_next,
      stl_map_range_version(object),
      stl_map_range_version};
  return range;
}

static cmeta_range stl_map_values_range_factory(const void *object) {
  const map_t *map = (const map_t *)object;
  cmeta_range range = {0};
  if (map == NULL || map->value_type == NULL)
    return range;
  range = (cmeta_range){
      object,
      map->value_type,
      stl_cmeta_range_flags_for(
          map->value_type,
          CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE),
      stl_map_range_size,
      stl_map_values_next,
      stl_map_range_version(object),
      stl_map_range_version};
  return range;
}

static cmeta_range stl_map_entries_range_factory(const void *object) {
  const map_t *map = (const map_t *)object;
  cmeta_range range = {0};
  if (map == NULL ||
      !stl_map_entry_binding_valid(map->key_type, map->value_type))
    return range;
  range = (cmeta_range){
      object,
      &cmeta_type_ordered_entry,
      stl_cmeta_range_flags_for(
          &cmeta_type_ordered_entry,
          CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
              CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE),
      stl_map_range_size,
      stl_map_entries_next,
      stl_map_range_version(object),
      stl_map_range_version};
  return range;
}

static cmeta_status stl_map_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
  map_t *output = (map_t *)context;
  cmeta_status status =
      stl_map_validate_input(input, &cmeta_type_ordered_entry);
  if (status != CMETA_OK)
    return status;
  if (output == NULL ||
      !stl_map_entry_binding_valid(output->key_type, output->value_type))
    return CMETA_TRAIT_MISSING;
  return stl_map_cmeta_status(map_init(output, limit));
}

static cmeta_status stl_map_collector_accept(
    void *context, const void *value) {
  map_t *output = (map_t *)context;
  const cmeta_entry *entry = (const cmeta_entry *)value;
  cmeta_status status;
  if (output == NULL)
    return CMETA_INVALID_ARGUMENT;
  status = stl_map_validate_entry_binding(
      entry, output->key_type, output->value_type);
  if (status != CMETA_OK)
    return status;
  return stl_map_cmeta_status(
      map_put(output, entry->key, entry->value));
}

static cmeta_status stl_map_collector_finish(void *context) {
  (void)context;
  return CMETA_OK;
}

static void stl_map_collector_abort(void *context) {
  if (context != NULL)
    map_destroy((map_t *)context);
}

static const cmeta_collector_ops stl_map_collector_ops = {
    stl_map_collector_begin,
    stl_map_collector_accept,
    stl_map_collector_finish,
    stl_map_collector_abort};

static cmeta_collector stl_map_collector_factory(
    void *zero_output, size_t limit) {
  cmeta_collector collector = {
      &stl_map_collector_ops,
      zero_output,
      zero_output,
      &cmeta_type_ordered_entry,
      limit,
      0u,
      CMETA_COLLECTOR_ZERO,
      CMETA_OK};
  return collector;
}

const cmeta_container_desc stl_map_container_desc = {
    "Map",
    NULL,
    NULL,
    NULL,
    NULL,
    stl_map_entries_range_factory,
    stl_map_keys_range_factory,
    stl_map_values_range_factory,
    stl_map_entries_range_factory,
    stl_map_collector_factory,
    &stl_map_container_ext};
