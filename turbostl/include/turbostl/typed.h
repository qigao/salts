#ifndef TURBO_TYPED_H
#define TURBO_TYPED_H

#include <cmeta/meta.h>

#include <turbostl/vec.h>
#include <turbostl/deque.h>
#include <turbostl/list.h>
#include <turbostl/stack.h>
#include <turbostl/queue.h>
#include <turbostl/heap.h>
#include <turbostl/set.h>
#include <turbostl/hash_set.h>
#include <turbostl/hash_map.h>
#include <turbostl/map.h>
#include <turbostl/multimap.h>
#include <turbostl/btree.h>
#include <turbostl/bplus_tree.h>
#include <turbostl/meta.h>

/* Turbo generic-kind registrations. */
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

/* Sequence kinds --------------------------------------------------------- */
#define CMETA_TYPED_Vec(name, type) TURBO_VEC_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Deque(name, type) TURBO_DEQUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_List(name, type) TURBO_LIST_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Stack(name, type) TURBO_STACK_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Queue(name, type) TURBO_QUEUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Heap(name, type) TURBO_HEAP_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

/* Associative kinds ------------------------------------------------------ */
#define CMETA_TYPED_Set(name, type) TURBO_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_HashSet(name, type) TURBO_HASH_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_HashMap(name, key_type, value_type) \
  TURBO_HASH_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Map(name, key_type, value_type) \
  TURBO_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_MultiMap(name, key_type, value_type) \
  TURBO_MULTI_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_BTree(name, key_type, value_type) \
  TURBO_BTREE_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_BPlusTree(name, key_type, value_type) \
  TURBO_BPLUS_TREE_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

/* Semantic front-end calls. The concrete type remains explicit because C11
 * cannot extend one _Generic association list from later typed(...) calls;
 * generated Type_method symbols remain an implementation detail. */
#define list_init(list_type, list_ptr, limit) \
  CMETA_TYPED_CALL(list_type, init, (list_ptr), (limit))
#define list_add(list_type, list_ptr, value) \
  CMETA_TYPED_CALL(list_type, push_back, (list_ptr), (value))
#define list_pop_front(list_type, list_ptr, output_ptr) \
  CMETA_TYPED_CALL(list_type, pop_front, (list_ptr), (output_ptr))
#define list_clear(list_type, list_ptr) \
  CMETA_TYPED_CALL(list_type, clear, (list_ptr))
#define list_destroy(list_type, list_ptr) \
  CMETA_TYPED_CALL(list_type, destroy, (list_ptr))

#define map_init(map_type, map_ptr, limit) \
  CMETA_TYPED_CALL(map_type, init, (map_ptr), (limit))
#define map_put(map_type, map_ptr, key, value) \
  CMETA_TYPED_CALL(map_type, put, (map_ptr), (key), (value))
#define map_clear(map_type, map_ptr) \
  CMETA_TYPED_CALL(map_type, clear, (map_ptr))
#define map_size(map_type, map_ptr) \
  CMETA_TYPED_CALL(map_type, size, (map_ptr))
#define map_destroy(map_type, map_ptr) \
  CMETA_TYPED_CALL(map_type, destroy, (map_ptr))

#endif /* TURBO_TYPED_H */
