#ifndef TURBO_TYPED_H
#define TURBO_TYPED_H

#include <cmeta/meta.h>

#include <turbo/container/vec.h>
#include <turbo/container/deque.h>
#include "turbo_list.h"
#include "turbo_stack.h"
#include "turbo_queue.h"
#include <turbo/container/heap.h>
#include <turbo/container/set.h>
#include "turbo_hash_set.h"
#include <turbo/container/hash_map.h>
#include "turbo_map.h"
#include "turbo_multimap.h"
#include <turbo/container/btree.h>
#include "turbo_bplus_tree.h"

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

#define CMETA_TYPED_Heap(name, type, compare_fn) TURBO_HEAP_DEFINE(name, type, compare_fn) enum { name##_cmeta_typed = 1 }

/* Associative kinds ------------------------------------------------------ */
#define CMETA_TYPED_Set(name, type) TURBO_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_HashSet(name, type) TURBO_HASH_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_HashMap(name, key_type, value_type) \
  TURBO_HASH_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_Map(name, key_type, value_type) \
  TURBO_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_MultiMap(name, key_type, value_type) \
  TURBO_MULTI_MAP_DEFINE(name, key_type, value_type) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_BTree(name, key_type, value_type, compare_fn) \
  TURBO_BTREE_DEFINE(name, key_type, value_type, compare_fn) enum { name##_cmeta_typed = 1 }

#define CMETA_TYPED_BPlusTree(name, key_type, value_type, compare_fn) \
  TURBO_BPLUS_TREE_DEFINE(name, key_type, value_type, compare_fn) enum { name##_cmeta_typed = 1 }

#endif /* TURBO_TYPED_H */
