#ifndef CSTL_DETAIL_TYPED_INITIALIZERS_H
#define CSTL_DETAIL_TYPED_INITIALIZERS_H

/* Self-describing initializer facts shared by declaration and expression DSLs.
 * They bind CMeta metadata but perform no allocation. */
#define SALTS_STL_VEC_INITIALIZER_WITH_TYPE(T, D) \
  { .cmeta = { &stl_vec_container_desc }, .element_type = (D) }
#define SALTS_STL_DEQUE_INITIALIZER_WITH_TYPE(T, D) \
  { .cmeta = { &stl_deque_container_desc }, .element_type = (D) }
#define SALTS_STL_LIST_INITIALIZER_WITH_TYPE(T, D) \
  { { &stl_list_container_desc }, (D), NULL, UINT64_C(0) }
#define SALTS_STL_STACK_INITIALIZER_WITH_TYPE(T, D) \
  { .raw = { .cmeta = { &stl_stack_container_desc }, \
             .element_type = (D) } }
#define SALTS_STL_QUEUE_INITIALIZER_WITH_TYPE(T, D) \
  { .raw = { .cmeta = { &stl_queue_container_desc }, \
             .element_type = (D) } }
#define SALTS_STL_HEAP_INITIALIZER_WITH_TYPE(T, D) \
  { .cmeta = { &stl_heap_container_desc }, .element_type = (D) }
#define SALTS_STL_SET_INITIALIZER_WITH_TYPE(T, D) \
  { .cmeta = { &stl_set_container_desc }, .element_type = (D) }
#define SALTS_STL_HASH_SET_INITIALIZER_WITH_TYPE(T, D) \
  { .cmeta = { &stl_hash_set_container_desc }, \
    .element_type = (D) }
#define SALTS_STL_HASH_MAP_INITIALIZER_WITH_TYPES(K, V, KD, VD) \
  { .cmeta = { &stl_hash_map_container_desc }, \
    .key_type = (KD), .value_type = (VD) }
#define SALTS_STL_MAP_INITIALIZER_WITH_TYPES(K, V, KD, VD) \
  { { &stl_map_container_desc }, (KD), (VD), NULL, UINT64_C(0) }
#define SALTS_STL_MULTIMAP_INITIALIZER_WITH_TYPES(K, V, KD, VD) \
  { .cmeta = { &stl_multimap_container_desc }, \
    .key_type = (KD), .value_type = (VD) }
#define SALTS_STL_BTREE_INITIALIZER_WITH_TYPES(K, V, KD, VD) \
  { .cmeta = { &stl_btree_container_desc }, \
    .key_type = (KD), .value_type = (VD) }
#define SALTS_STL_BPLUS_TREE_INITIALIZER_WITH_TYPES(K, V, KD, VD) \
  { .cmeta = { &stl_bplus_tree_container_desc }, \
    .key_type = (KD), .value_type = (VD) }

#define SALTS_STL_VEC_INITIALIZER(T) \
  SALTS_STL_VEC_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_DEQUE_INITIALIZER(T) \
  SALTS_STL_DEQUE_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_LIST_INITIALIZER(T) \
  SALTS_STL_LIST_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_STACK_INITIALIZER(T) \
  SALTS_STL_STACK_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_QUEUE_INITIALIZER(T) \
  SALTS_STL_QUEUE_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_HEAP_INITIALIZER(T) \
  SALTS_STL_HEAP_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_SET_INITIALIZER(T) \
  SALTS_STL_SET_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_HASH_SET_INITIALIZER(T) \
  SALTS_STL_HASH_SET_INITIALIZER_WITH_TYPE(T, CMETA_TYPEOF(T))
#define SALTS_STL_HASH_MAP_INITIALIZER(K, V) \
  SALTS_STL_HASH_MAP_INITIALIZER_WITH_TYPES(K, V, CMETA_TYPEOF(K), CMETA_TYPEOF(V))
#define SALTS_STL_MAP_INITIALIZER(K, V) \
  SALTS_STL_MAP_INITIALIZER_WITH_TYPES(K, V, CMETA_TYPEOF(K), CMETA_TYPEOF(V))
#define SALTS_STL_MULTIMAP_INITIALIZER(K, V) \
  SALTS_STL_MULTIMAP_INITIALIZER_WITH_TYPES(K, V, CMETA_TYPEOF(K), CMETA_TYPEOF(V))
#define SALTS_STL_BTREE_INITIALIZER(K, V) \
  SALTS_STL_BTREE_INITIALIZER_WITH_TYPES(K, V, CMETA_TYPEOF(K), CMETA_TYPEOF(V))
#define SALTS_STL_BPLUS_TREE_INITIALIZER(K, V) \
  SALTS_STL_BPLUS_TREE_INITIALIZER_WITH_TYPES(K, V, CMETA_TYPEOF(K), CMETA_TYPEOF(V))

/* Expression DSL. Compound literals allow initialization, assignment, return,
 * and argument passing without introducing generated user-visible C types. */
#ifndef VecOf
#define VecOf(T) ((vec_t)SALTS_STL_VEC_INITIALIZER(T))
#endif
#ifndef DequeOf
#define DequeOf(T) ((deque_t)SALTS_STL_DEQUE_INITIALIZER(T))
#endif
#ifndef ListOf
#define ListOf(T) ((list_t)SALTS_STL_LIST_INITIALIZER(T))
#endif
#ifndef StackOf
#define StackOf(T) ((cstl_stack_t)SALTS_STL_STACK_INITIALIZER(T))
#endif
#ifndef QueueOf
#define QueueOf(T) ((queue_t)SALTS_STL_QUEUE_INITIALIZER(T))
#endif
#ifndef HeapOf
#define HeapOf(T) ((heap_t)SALTS_STL_HEAP_INITIALIZER(T))
#endif
#ifndef SetOf
#define SetOf(T) ((set_t)SALTS_STL_SET_INITIALIZER(T))
#endif
#ifndef HashSetOf
#define HashSetOf(T) ((hash_set_t)SALTS_STL_HASH_SET_INITIALIZER(T))
#endif
#ifndef HashMapOf
#define HashMapOf(K, V) ((hash_map_t)SALTS_STL_HASH_MAP_INITIALIZER(K, V))
#endif
#ifndef MapOf
#define MapOf(K, V) ((map_t)SALTS_STL_MAP_INITIALIZER(K, V))
#endif
#ifndef MultiMapOf
#define MultiMapOf(K, V) ((multimap_t)SALTS_STL_MULTIMAP_INITIALIZER(K, V))
#endif
#ifndef BTreeOf
#define BTreeOf(K, V) ((btree_t)SALTS_STL_BTREE_INITIALIZER(K, V))
#endif
#ifndef BPlusTreeOf
#define BPlusTreeOf(K, V) \
  ((bplus_tree_t)SALTS_STL_BPLUS_TREE_INITIALIZER(K, V))
#endif

/* Declaration DSL retained for source compatibility. */
#ifndef Vec
#define Vec(T, name) vec_t name = SALTS_STL_VEC_INITIALIZER(T)
#endif
#ifndef Deque
#define Deque(T, name) deque_t name = SALTS_STL_DEQUE_INITIALIZER(T)
#endif
#ifndef List
#define List(T, name) list_t name = SALTS_STL_LIST_INITIALIZER(T)
#endif
#ifndef Stack
#define Stack(T, name) cstl_stack_t name = SALTS_STL_STACK_INITIALIZER(T)
#endif
#ifndef Queue
#define Queue(T, name) queue_t name = SALTS_STL_QUEUE_INITIALIZER(T)
#endif
#ifndef Heap
#define Heap(T, name) heap_t name = SALTS_STL_HEAP_INITIALIZER(T)
#endif
#ifndef Set
#define Set(T, name) set_t name = SALTS_STL_SET_INITIALIZER(T)
#endif
#ifndef HashSet
#define HashSet(T, name) hash_set_t name = SALTS_STL_HASH_SET_INITIALIZER(T)
#endif
#ifndef HashMap
#define HashMap(K, V, name) \
  hash_map_t name = SALTS_STL_HASH_MAP_INITIALIZER(K, V)
#endif
#ifndef Map
#define Map(K, V, name) map_t name = SALTS_STL_MAP_INITIALIZER(K, V)
#endif
#ifndef MultiMap
#define MultiMap(K, V, name) \
  multimap_t name = SALTS_STL_MULTIMAP_INITIALIZER(K, V)
#endif
#ifndef BTree
#define BTree(K, V, name) btree_t name = SALTS_STL_BTREE_INITIALIZER(K, V)
#endif
#ifndef BPlusTree
#define BPlusTree(K, V, name) \
  bplus_tree_t name = SALTS_STL_BPLUS_TREE_INITIALIZER(K, V)
#endif

#endif /* CSTL_DETAIL_TYPED_INITIALIZERS_H */
