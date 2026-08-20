#ifndef CONTAINER_TYPED_H
#define CONTAINER_TYPED_H

#include <cmeta/meta.h>
#include <container/meta.h>

#include <container/vec.h>
#include <container/deque.h>
#include <container/list.h>
#include <container/stack.h>
#include <container/queue.h>
#include <container/heap.h>
#include <container/set.h>
#include <container/hash_set.h>
#include <container/hash_map.h>
#include <container/map.h>
#include <container/multimap.h>
#include <container/btree.h>
#include <container/bplus_tree.h>

/* Container generic-kind registrations. */
#define CMETA_GENERIC_KIND_Vec CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Deque CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_List CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Stack CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Queue CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Heap CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Set CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_HashSet CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_HashMap CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Map CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_MultiMap CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_BTree CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_BPlusTree CMETA_GENERIC_PROBE()

/* Typed facade generators ------------------------------------------------- */
#define CONTAINER_VEC_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, container_vec_t, container_vec, TURBO_OK, _, CONTAINER_META_VEC_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, container_vec, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_CONTIGUOUS | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#define CONTAINER_DEQUE_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, container_deque_t, container_deque, TURBO_OK, _, CONTAINER_META_DEQUE_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, container_deque, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#define CONTAINER_LIST_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, container_list_t, container_list, TURBO_OK, _, CONTAINER_META_LIST_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, container_list, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE)

#define CONTAINER_STACK_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, container_stack_t, container_stack, TURBO_OK, _, CONTAINER_META_STACK_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, container_stack, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#define CONTAINER_QUEUE_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, container_queue_t, container_queue, TURBO_OK, _, CONTAINER_META_QUEUE_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, container_queue, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#define CONTAINER_HEAP_DEFINE(name, type, compare_fn) \
  CMETA_CONTAINER1_DEFINE(name, type, container_heap_t, container_heap, TURBO_OK, compare_fn, CONTAINER_META_HEAP_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, container_heap, \
      CMETA_RANGE_SIZED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#define CONTAINER_SET_DEFINE(name, key_type) \
  CMETA_CONTAINER1_DEFINE(name, key_type, container_set_t, container_set, TURBO_OK, _, CONTAINER_META_SET_METHODS) \
  CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name, key_type, container_set, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#define CONTAINER_HASH_SET_DEFINE(name, key_type) \
  CMETA_CONTAINER1_DEFINE(name, key_type, container_hash_set_t, container_hash_set, TURBO_OK, _, CONTAINER_META_HASH_SET_METHODS) \
  CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name, key_type, container_hash_set, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#define CONTAINER_HASH_MAP_DEFINE(name, key_type, value_type) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, container_hash_map_t, container_hash_map, TURBO_OK, _, CONTAINER_META_HASH_MAP_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, container_hash_map, key_at, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#define CONTAINER_MAP_DEFINE(name, key_type, value_type) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, container_map_t, container_map, TURBO_OK, _, CONTAINER_META_MAP_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, container_map, key_at_const, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#define CONTAINER_MULTI_MAP_DEFINE(name, key_type, value_type) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, container_multimap_t, container_multimap, TURBO_OK, _, CONTAINER_META_MULTIMAP_METHODS) \
  CMETA_CONTAINER2_OPAQUE_DESCRIPTOR_DEFINE(name, key_type, value_type)

#define CONTAINER_BTREE_DEFINE(name, key_type, value_type, compare_fn) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, container_btree_t, container_btree, TURBO_OK, compare_fn, CONTAINER_META_BTREE_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, container_btree, key_at_const, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

#define CONTAINER_BPLUS_TREE_DEFINE(name, key_type, value_type, compare_fn) \
  CMETA_CONTAINER2_DEFINE(name, key_type, value_type, container_bplus_tree_t, container_bplus_tree, TURBO_OK, compare_fn, CONTAINER_META_BPLUS_TREE_METHODS) \
  CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, container_bplus_tree, key_at_const, value_at_const, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE)

/* Sequence kinds --------------------------------------------------------- */
#define CMETA_TYPED_Vec(name, type) CONTAINER_VEC_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Deque(name, type) CONTAINER_DEQUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_List(name, type) CONTAINER_LIST_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Stack(name, type) CONTAINER_STACK_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Queue(name, type) CONTAINER_QUEUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Heap(name, type, compare_fn) CONTAINER_HEAP_DEFINE(name, type, compare_fn) enum { name##_cmeta_typed = 1 }

/* Associative kinds ------------------------------------------------------ */
#define CMETA_TYPED_Set(name, type) CONTAINER_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_HashSet(name, type) CONTAINER_HASH_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_HashMap(name, key_type, value_type) \
  CONTAINER_HASH_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Map(name, key_type, value_type) \
  CONTAINER_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_MultiMap(name, key_type, value_type) \
  CONTAINER_MULTI_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_BTree(name, key_type, value_type, compare_fn) \
  CONTAINER_BTREE_DEFINE(name, key_type, value_type, compare_fn) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_BPlusTree(name, key_type, value_type, compare_fn) \
  CONTAINER_BPLUS_TREE_DEFINE(name, key_type, value_type, compare_fn) enum { name##_cmeta_typed = 1 }

#endif /* CONTAINER_TYPED_H */
