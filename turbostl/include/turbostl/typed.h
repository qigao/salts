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
#include <turbostl/detail/instance_meta.h>

#ifndef __cplusplus
#include <turbostl/detail/typed_facade.h>
#endif

/* TYPE(...) provider registrations. CMeta owns the generic declaration
 * protocol; TurboSTL supplies only storage, constructor and bind capability. */
#define CMETA_DECLARED_STORAGE_Vec vec_t
#define CMETA_DECLARED_STORAGE_DESC_Vec (&stl_vec_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Vec (&stl_vec_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Vec (&stl_vec_construct_ops)

#define CMETA_DECLARED_STORAGE_Deque deque_t
#define CMETA_DECLARED_STORAGE_DESC_Deque (&stl_deque_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Deque (&stl_deque_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Deque (&stl_deque_construct_ops)

#define CMETA_DECLARED_STORAGE_List list_t
#define CMETA_DECLARED_STORAGE_DESC_List (&stl_list_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_List (&stl_list_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_List (&stl_list_construct_ops)

#define CMETA_DECLARED_STORAGE_Stack turbostl_stack_t
#define CMETA_DECLARED_STORAGE_DESC_Stack (&stl_stack_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Stack (&stl_stack_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Stack (&stl_stack_construct_ops)

#define CMETA_DECLARED_STORAGE_Queue queue_t
#define CMETA_DECLARED_STORAGE_DESC_Queue (&stl_queue_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Queue (&stl_queue_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Queue (&stl_queue_construct_ops)

#define CMETA_DECLARED_STORAGE_Heap heap_t
#define CMETA_DECLARED_STORAGE_DESC_Heap (&stl_heap_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Heap (&stl_heap_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Heap (&stl_heap_construct_ops)

#define CMETA_DECLARED_STORAGE_Set set_t
#define CMETA_DECLARED_STORAGE_DESC_Set (&stl_set_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Set (&stl_set_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Set (&stl_set_construct_ops)

#define CMETA_DECLARED_STORAGE_HashSet hash_set_t
#define CMETA_DECLARED_STORAGE_DESC_HashSet (&stl_hash_set_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_HashSet (&stl_hash_set_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_HashSet (&stl_hash_set_construct_ops)

#define CMETA_DECLARED_STORAGE_HashMap hash_map_t
#define CMETA_DECLARED_STORAGE_DESC_HashMap (&stl_hash_map_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_HashMap (&stl_hash_map_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_HashMap (&stl_hash_map_construct_ops)

#define CMETA_DECLARED_STORAGE_Map map_t
#define CMETA_DECLARED_STORAGE_DESC_Map (&stl_map_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_Map (&stl_map_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_Map (&stl_map_construct_ops)

#define CMETA_DECLARED_STORAGE_MultiMap multimap_t
#define CMETA_DECLARED_STORAGE_DESC_MultiMap (&stl_multimap_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_MultiMap (&stl_multimap_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_MultiMap (&stl_multimap_construct_ops)

#define CMETA_DECLARED_STORAGE_BTree btree_t
#define CMETA_DECLARED_STORAGE_DESC_BTree (&stl_btree_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_BTree (&stl_btree_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_BTree (&stl_btree_construct_ops)

#define CMETA_DECLARED_STORAGE_BPlusTree bplus_tree_t
#define CMETA_DECLARED_STORAGE_DESC_BPlusTree (&stl_bplus_tree_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_BPlusTree (&stl_bplus_tree_generic_desc)
#define CMETA_DECLARED_CONSTRUCTION_BPlusTree (&stl_bplus_tree_construct_ops)

#include <turbostl/detail/typed_initializers.h>

#ifndef __cplusplus

/* TurboSTL is a finite CMeta Generic provider. One typed(...) declaration
 * emits the concrete wrapper type, typed ABI, metadata, Range views and
 * collector. The declaration/expression initializers above remain erased
 * handle construction forms and do not generate Generic types. */
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

#define CMETA_TYPED_Vec(name, type) \
  TURBO_VEC_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Deque(name, type) \
  TURBO_DEQUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_List(name, type) \
  TURBO_LIST_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Stack(name, type) \
  TURBO_STACK_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Queue(name, type) \
  TURBO_QUEUE_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Heap(name, type) \
  TURBO_HEAP_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Set(name, type) \
  TURBO_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_HashSet(name, type) \
  TURBO_HASH_SET_DEFINE(name, type) enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_HashMap(name, key_type, value_type) \
  TURBO_HASH_MAP_DEFINE(name, key_type, value_type) \
  enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_Map(name, key_type, value_type) \
  TURBO_MAP_DEFINE(name, key_type, value_type) \
  enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_MultiMap(name, key_type, value_type) \
  TURBO_MULTI_MAP_DEFINE(name, key_type, value_type) \
  enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_BTree(name, key_type, value_type) \
  TURBO_BTREE_DEFINE(name, key_type, value_type) \
  enum { name##_cmeta_typed = 1 }
#define CMETA_TYPED_BPlusTree(name, key_type, value_type) \
  TURBO_BPLUS_TREE_DEFINE(name, key_type, value_type) \
  enum { name##_cmeta_typed = 1 }

/* Generated Type_method functions are the typed calling convention. Raw
 * list_* and map_* names remain ordinary functions so every valid C argument
 * expression, including compound literals containing commas, is preserved. */

#endif

#endif /* TURBO_TYPED_H */
