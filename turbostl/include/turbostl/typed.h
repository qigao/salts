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

/* Self-describing declaration DSL. These declarations bind CMeta metadata but
 * perform no allocation and do not create generated user-visible C types. */
#ifndef Vec
#define Vec(T, name) \
  vec_t name = { .cmeta = { &stl_vec_container_desc }, \
                 .element_type = CMETA_TYPEOF(T) }
#endif
#ifndef Deque
#define Deque(T, name) \
  deque_t name = { .cmeta = { &stl_deque_container_desc }, \
                   .element_type = CMETA_TYPEOF(T) }
#endif
#ifndef List
#define List(T, name) \
  list_t name = { { &stl_list_container_desc }, CMETA_TYPEOF(T), NULL, UINT64_C(0) }
#endif
#ifndef Stack
#define Stack(T, name) \
  stack_t name = { .raw = { .cmeta = { &stl_stack_container_desc }, \
                            .element_type = CMETA_TYPEOF(T) } }
#endif
#ifndef Queue
#define Queue(T, name) \
  queue_t name = { .raw = { .cmeta = { &stl_queue_container_desc }, \
                            .element_type = CMETA_TYPEOF(T) } }
#endif
#ifndef Heap
#define Heap(T, name) \
  heap_t name = { .cmeta = { &stl_heap_container_desc }, \
                  .element_type = CMETA_TYPEOF(T) }
#endif
#ifndef Set
#define Set(T, name) \
  set_t name = { .cmeta = { &stl_set_container_desc }, \
                 .element_type = CMETA_TYPEOF(T) }
#endif
#ifndef HashSet
#define HashSet(T, name) \
  hash_set_t name = { .cmeta = { &stl_hash_set_container_desc }, \
                      .element_type = CMETA_TYPEOF(T) }
#endif
#ifndef HashMap
#define HashMap(K, V, name) \
  hash_map_t name = { .cmeta = { &stl_hash_map_container_desc }, \
                      .key_type = CMETA_TYPEOF(K), \
                      .value_type = CMETA_TYPEOF(V) }
#endif
#ifndef Map
#define Map(K, V, name) \
  map_t name = { { &stl_map_container_desc }, CMETA_TYPEOF(K), \
                 CMETA_TYPEOF(V), NULL, UINT64_C(0) }
#endif
#ifndef MultiMap
#define MultiMap(K, V, name) \
  multimap_t name = { .cmeta = { &stl_multimap_container_desc }, \
                      .key_type = CMETA_TYPEOF(K), \
                      .value_type = CMETA_TYPEOF(V) }
#endif
#ifndef BTree
#define BTree(K, V, name) \
  btree_t name = { .cmeta = { &stl_btree_container_desc }, \
                   .key_type = CMETA_TYPEOF(K), \
                   .value_type = CMETA_TYPEOF(V) }
#endif
#ifndef BPlusTree
#define BPlusTree(K, V, name) \
  bplus_tree_t name = { .cmeta = { &stl_bplus_tree_container_desc }, \
                        .key_type = CMETA_TYPEOF(K), \
                        .value_type = CMETA_TYPEOF(V) }
#endif

#endif /* TURBO_TYPED_H */
