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

#define CMETA_DECLARED_STORAGE_Stack stack_t
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

/* Self-describing initializer facts shared by declaration and expression DSLs.
 * They bind CMeta metadata but perform no allocation. */
#define TURBO_STL_VEC_INITIALIZER(T) \
  { .cmeta = { &stl_vec_container_desc }, .element_type = CMETA_TYPEOF(T) }
#define TURBO_STL_DEQUE_INITIALIZER(T) \
  { .cmeta = { &stl_deque_container_desc }, .element_type = CMETA_TYPEOF(T) }
#define TURBO_STL_LIST_INITIALIZER(T) \
  { { &stl_list_container_desc }, CMETA_TYPEOF(T), NULL, UINT64_C(0) }
#define TURBO_STL_STACK_INITIALIZER(T) \
  { .raw = { .cmeta = { &stl_stack_container_desc }, \
             .element_type = CMETA_TYPEOF(T) } }
#define TURBO_STL_QUEUE_INITIALIZER(T) \
  { .raw = { .cmeta = { &stl_queue_container_desc }, \
             .element_type = CMETA_TYPEOF(T) } }
#define TURBO_STL_HEAP_INITIALIZER(T) \
  { .cmeta = { &stl_heap_container_desc }, .element_type = CMETA_TYPEOF(T) }
#define TURBO_STL_SET_INITIALIZER(T) \
  { .cmeta = { &stl_set_container_desc }, .element_type = CMETA_TYPEOF(T) }
#define TURBO_STL_HASH_SET_INITIALIZER(T) \
  { .cmeta = { &stl_hash_set_container_desc }, \
    .element_type = CMETA_TYPEOF(T) }
#define TURBO_STL_HASH_MAP_INITIALIZER(K, V) \
  { .cmeta = { &stl_hash_map_container_desc }, \
    .key_type = CMETA_TYPEOF(K), \
    .value_type = CMETA_TYPEOF(V) }
#define TURBO_STL_MAP_INITIALIZER(K, V) \
  { { &stl_map_container_desc }, CMETA_TYPEOF(K), CMETA_TYPEOF(V), NULL, \
    UINT64_C(0) }
#define TURBO_STL_MULTIMAP_INITIALIZER(K, V) \
  { .cmeta = { &stl_multimap_container_desc }, \
    .key_type = CMETA_TYPEOF(K), \
    .value_type = CMETA_TYPEOF(V) }
#define TURBO_STL_BTREE_INITIALIZER(K, V) \
  { .cmeta = { &stl_btree_container_desc }, \
    .key_type = CMETA_TYPEOF(K), \
    .value_type = CMETA_TYPEOF(V) }
#define TURBO_STL_BPLUS_TREE_INITIALIZER(K, V) \
  { .cmeta = { &stl_bplus_tree_container_desc }, \
    .key_type = CMETA_TYPEOF(K), \
    .value_type = CMETA_TYPEOF(V) }

/* Expression DSL. Compound literals allow initialization, assignment, return,
 * and argument passing without introducing generated user-visible C types. */
#ifndef VecOf
#define VecOf(T) ((vec_t)TURBO_STL_VEC_INITIALIZER(T))
#endif
#ifndef DequeOf
#define DequeOf(T) ((deque_t)TURBO_STL_DEQUE_INITIALIZER(T))
#endif
#ifndef ListOf
#define ListOf(T) ((list_t)TURBO_STL_LIST_INITIALIZER(T))
#endif
#ifndef StackOf
#define StackOf(T) ((stack_t)TURBO_STL_STACK_INITIALIZER(T))
#endif
#ifndef QueueOf
#define QueueOf(T) ((queue_t)TURBO_STL_QUEUE_INITIALIZER(T))
#endif
#ifndef HeapOf
#define HeapOf(T) ((heap_t)TURBO_STL_HEAP_INITIALIZER(T))
#endif
#ifndef SetOf
#define SetOf(T) ((set_t)TURBO_STL_SET_INITIALIZER(T))
#endif
#ifndef HashSetOf
#define HashSetOf(T) ((hash_set_t)TURBO_STL_HASH_SET_INITIALIZER(T))
#endif
#ifndef HashMapOf
#define HashMapOf(K, V) ((hash_map_t)TURBO_STL_HASH_MAP_INITIALIZER(K, V))
#endif
#ifndef MapOf
#define MapOf(K, V) ((map_t)TURBO_STL_MAP_INITIALIZER(K, V))
#endif
#ifndef MultiMapOf
#define MultiMapOf(K, V) ((multimap_t)TURBO_STL_MULTIMAP_INITIALIZER(K, V))
#endif
#ifndef BTreeOf
#define BTreeOf(K, V) ((btree_t)TURBO_STL_BTREE_INITIALIZER(K, V))
#endif
#ifndef BPlusTreeOf
#define BPlusTreeOf(K, V) \
  ((bplus_tree_t)TURBO_STL_BPLUS_TREE_INITIALIZER(K, V))
#endif

/* Declaration DSL retained for source compatibility. */
#ifndef Vec
#define Vec(T, name) vec_t name = TURBO_STL_VEC_INITIALIZER(T)
#endif
#ifndef Deque
#define Deque(T, name) deque_t name = TURBO_STL_DEQUE_INITIALIZER(T)
#endif
#ifndef List
#define List(T, name) list_t name = TURBO_STL_LIST_INITIALIZER(T)
#endif
#ifndef Stack
#define Stack(T, name) stack_t name = TURBO_STL_STACK_INITIALIZER(T)
#endif
#ifndef Queue
#define Queue(T, name) queue_t name = TURBO_STL_QUEUE_INITIALIZER(T)
#endif
#ifndef Heap
#define Heap(T, name) heap_t name = TURBO_STL_HEAP_INITIALIZER(T)
#endif
#ifndef Set
#define Set(T, name) set_t name = TURBO_STL_SET_INITIALIZER(T)
#endif
#ifndef HashSet
#define HashSet(T, name) hash_set_t name = TURBO_STL_HASH_SET_INITIALIZER(T)
#endif
#ifndef HashMap
#define HashMap(K, V, name) \
  hash_map_t name = TURBO_STL_HASH_MAP_INITIALIZER(K, V)
#endif
#ifndef Map
#define Map(K, V, name) map_t name = TURBO_STL_MAP_INITIALIZER(K, V)
#endif
#ifndef MultiMap
#define MultiMap(K, V, name) \
  multimap_t name = TURBO_STL_MULTIMAP_INITIALIZER(K, V)
#endif
#ifndef BTree
#define BTree(K, V, name) btree_t name = TURBO_STL_BTREE_INITIALIZER(K, V)
#endif
#ifndef BPlusTree
#define BPlusTree(K, V, name) \
  bplus_tree_t name = TURBO_STL_BPLUS_TREE_INITIALIZER(K, V)
#endif

#endif /* TURBO_TYPED_H */
