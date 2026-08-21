#ifndef TURBO_TYPED_H
#define TURBO_TYPED_H

#include <cmeta/meta.h>

#include <turbo/stl/vec.h>
#include <turbo/stl/deque.h>
#include <turbo/stl/list.h>
#include <turbo/stl/stack.h>
#include <turbo/stl/queue.h>
#include <turbo/stl/heap.h>
#include <turbo/stl/set.h>
#include <turbo/stl/hash_set.h>
#include <turbo/stl/hash_map.h>
#include <turbo/stl/map.h>
#include <turbo/stl/multimap.h>
#include <turbo/stl/btree.h>
#include <turbo/stl/bplus_tree.h>
#include <turbo/stl/meta.h>

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

#endif /* TURBO_TYPED_H */
