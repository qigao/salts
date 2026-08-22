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

/* Self-describing declaration DSL. These declarations bind CMeta metadata but
 * perform no allocation and do not create generated user-visible C types. */
#ifndef Vec
#define Vec(T, name) \
  vec_t name = { .element_type = CMETA_TYPEOF(T) }
#endif
#ifndef List
#define List(T, name) \
  list_t name = { { &stl_list_container_desc }, CMETA_TYPEOF(T), NULL, UINT64_C(0) }
#endif
#ifndef Map
#define Map(K, V, name) \
  map_t name = { { &stl_map_container_desc }, CMETA_TYPEOF(K), \
                 CMETA_TYPEOF(V), NULL, UINT64_C(0) }
#endif

/* Legacy generated-kind registrations remain temporarily for repository
 * migration only. They are removed once every TurboSTL consumer uses the
 * self-describing handle model. */
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

#define CMETA_TYPED_Vec(name, type) TURBO_VEC_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Deque(name, type) TURBO_DEQUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_List(name, type) TURBO_LIST_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Stack(name, type) TURBO_STACK_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Queue(name, type) TURBO_QUEUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Heap(name, type) TURBO_HEAP_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
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

#endif /* TURBO_TYPED_H */
