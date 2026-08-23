#ifndef TURBOSTL_DETAIL_TYPED_INITIALIZERS_H
#define TURBOSTL_DETAIL_TYPED_INITIALIZERS_H

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

#endif /* TURBOSTL_DETAIL_TYPED_INITIALIZERS_H */
