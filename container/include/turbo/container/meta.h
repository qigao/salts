#ifndef TURBO_CONTAINER_META_H
#define TURBO_CONTAINER_META_H

/* Internal typed-facade generation. Raw public headers never include this. */
#include <cmeta/container.h>
#include <turbo/container/status.h>

#include <string.h>

CMETA_INLINE cmeta_status turbo_container_cmeta_status(container_status status) {
 switch (status) {
  case CONTAINER_OK: return CMETA_OK;
  case CONTAINER_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
  case CONTAINER_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
  case CONTAINER_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
  case CONTAINER_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
  case CONTAINER_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
  case CONTAINER_EMPTY:
  case CONTAINER_NOT_FOUND: return CMETA_CALLBACK_ERROR;
  default: return CMETA_CALLBACK_ERROR;
 }
}

#define TURBO_META_C1_COLLECTOR(name,type,accept_method) \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,CMETA_TYPEOF(type)))return CMETA_TYPE_MISMATCH;return turbo_container_cmeta_status((container_status)name##_init((name*)context,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){return turbo_container_cmeta_status((container_status)name##_##accept_method((name*)context,*(const type*)value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(void *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,CMETA_TYPEOF(type),limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;}

#define TURBO_META_C2_COLLECTOR(name) \
 CMETA_LOCAL cmeta_type_desc name##_entry_cmeta_type; \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,&name##_entry_cmeta_type))return CMETA_TYPE_MISMATCH;if(cmeta_type_require_traits(input,CMETA_TRAIT_COPY|CMETA_TRAIT_MOVE|CMETA_TRAIT_DESTROY)!=CMETA_OK)return CMETA_TRAIT_MISSING;return turbo_container_cmeta_status((container_status)name##_init((name*)context,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){const name##_entry *entry=(const name##_entry*)value;return turbo_container_cmeta_status((container_status)name##_put((name*)context,entry->key,entry->value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(void *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,&name##_entry_cmeta_type,limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;}

/* A multimap collector's single factory limit bounds both distinct keys and
 * values retained for any one key. */
#define TURBO_META_MULTIMAP_COLLECTOR(name) \
 CMETA_LOCAL cmeta_type_desc name##_entry_cmeta_type; \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,&name##_entry_cmeta_type))return CMETA_TYPE_MISMATCH;if(cmeta_type_require_traits(input,CMETA_TRAIT_COPY|CMETA_TRAIT_MOVE|CMETA_TRAIT_DESTROY)!=CMETA_OK)return CMETA_TRAIT_MISSING;return turbo_container_cmeta_status((container_status)name##_init((name*)context,limit,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){const name##_entry *entry=(const name##_entry*)value;return turbo_container_cmeta_status((container_status)name##_put((name*)context,entry->key,entry->value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(void *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,&name##_entry_cmeta_type,limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;}

#define TURBO_META_VEC_METHODS(M,C) \
 M(INIT_SIZE,init,init,_,C) M(FROM_ARRAY_SIZE,from,from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(PTR_INDEX,at,at,_,C) \
 M(CONST_PTR_INDEX,at_const,at_const,_,C) M(PTR,data,data,_,C) \
 M(CONST_PTR,data_const,data_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_DEQUE_METHODS(M,C) \
 M(INIT_SIZE,init,init,_,C) M(FROM_ARRAY_SIZE,from,from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push_back,push_back,_,C) \
 M(PUSH_VALUE,push_front,push_front,_,C) M(POP_BOOL,pop_back,pop_back,_,C) \
 M(POP_BOOL,pop_front,pop_front,_,C) M(PTR,front,front,_,C) \
 M(CONST_PTR,front_const,front_const,_,C) M(PTR,back,back,_,C) \
 M(CONST_PTR,back_const,back_const,_,C) M(PTR_INDEX,at,at,_,C) \
 M(CONST_PTR_INDEX,at_const,at_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_LIST_METHODS(M,C) TURBO_META_DEQUE_METHODS(M,C)

#define TURBO_META_STACK_METHODS(M,C) \
 M(INIT_SIZE,init,init,_,C) M(FROM_ARRAY_SIZE,from,from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(PTR,top,top,_,C) \
 M(CONST_PTR,top_const,top_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_QUEUE_METHODS(M,C) \
 M(INIT_SIZE,init,init,_,C) M(FROM_ARRAY_SIZE,from,from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(PTR,front,front,_,C) \
 M(CONST_PTR,front_const,front_const,_,C) M(PTR,back,back,_,C) \
 M(CONST_PTR,back_const,back_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_HEAP_METHODS(M,C) \
 M(INIT_SIZE_COMPARE,init,init,_,C) M(FROM_ARRAY_SIZE_COMPARE,from,from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(CONST_PTR,peek,peek,_,C) \
 M(SIZE,size,size,_,C) M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_SET_METHODS(M,C) \
 M(INIT_KEY_HASH,init,init,_,C) M(FROM_KEYS_HASH,from,from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(KEY_VALUE,add,add,_,C) \
 M(KEY_CONTAINS,contains,contains,_,C) M(KEY_REMOVE_BOOL,remove,remove,_,C) \
 M(SIZE,size,size,_,C) M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)
#define TURBO_META_HASH_SET_METHODS(M,C) TURBO_META_SET_METHODS(M,C)

#define TURBO_META_C1_GENERATION(name,prefix) \
 CMETA_INLINE uint64_t name##_cmeta_generation(const void *object) { \
   const name *self = (const name *)object; \
   return self == NULL ? UINT64_C(0) : prefix##_generation(&self->raw); \
 }
#define TURBO_META_C2_GENERATION(name,prefix) TURBO_META_C1_GENERATION(name,prefix)

#define TURBO_META_HASH_MAP_METHODS(M,C) \
 M(INIT_KV_HASH,init,init,_,C) M(FROM_ENTRIES,from,from,CONTAINER_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(CAPACITY,capacity,capacity,_,C) M(EMPTY,empty,empty,_,C) \
 M(KEY_AT,key_at,key_at,_,C) M(KEY_AT_CONST,key_at_const,key_at_const,_,C) \
 M(VALUE_AT,value_at,value_at,_,C) M(VALUE_AT_CONST,value_at_const,value_at_const,_,C)

#define TURBO_META_MAP_METHODS(M,C) \
 M(INIT_KV_COMPARE,init,init,_,C) M(FROM_ENTRIES,from,from,CONTAINER_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(CAPACITY,capacity,capacity,_,C) M(EMPTY,empty,empty,_,C) \
 M(KEY_AT,key_at,key_at,_,C) M(KEY_AT_CONST,key_at_const,key_at_const,_,C) \
 M(VALUE_AT,value_at,value_at,_,C) M(VALUE_AT_CONST,value_at_const,value_at_const,_,C)

#define TURBO_META_MULTIMAP_METHODS(M,C) \
 M(INIT_KV_MULTIMAP,init,init,_,C) \
 M(FROM_ENTRIES_MULTIMAP,from,from,CONTAINER_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) \
 M(RAW_PTR_KEY,get_values,get_values,turbo_vec_t,C) \
 M(RAW_CONST_PTR_KEY,get_values_const,get_values_const,turbo_vec_t,C) \
 M(CONTAINS,contains,contains,_,C) M(COUNT_KEY,count,key_count,_,C) \
 M(REMOVE_BOOL,remove,remove,_,C) M(ERASE_KEY,erase,erase,_,C) \
 M(SIZE,size,size,_,C) M(CAPACITY,capacity,capacity,_,C) \
 M(EMPTY,empty,empty,_,C)

#define TURBO_META_TREE_METHODS(M,C) \
 M(INIT_KV_COMPARE,init,init,_,C) M(FROM_ENTRIES,from,from,CONTAINER_INVALID_ARGUMENT,C) \
 M(INIT_WITH_ORDER_COMPARE,init_with_order,init_with_order,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(CAPACITY,capacity,capacity,_,C) M(EMPTY,empty,empty,_,C) \
 M(KEY_AT,key_at,key_at,_,C) M(KEY_AT_CONST,key_at_const,key_at_const,_,C) \
 M(VALUE_AT,value_at,value_at,_,C) M(VALUE_AT_CONST,value_at_const,value_at_const,_,C)
#define TURBO_META_BTREE_METHODS(M,C) TURBO_META_TREE_METHODS(M,C)
#define TURBO_META_BPLUS_TREE_METHODS(M,C) TURBO_META_TREE_METHODS(M,C)

/* The row macros are the sole semantic mapping for every standard kind.
 * C cannot emit #define directives from Replay, so the public kind markers and
 * adapters remain thin language-policy shims that reference these rows. */
#define TURBO_CONTAINER_KIND_ROW_Vec (Vec,1,C1_INDEX,turbo_vec_t,turbo_vec,TURBO_META_VEC_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_CONTIGUOUS|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_Deque (Deque,1,C1_INDEX,turbo_deque_t,turbo_deque,TURBO_META_DEQUE_METHODS,push_back,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_List (List,1,C1_LINK,turbo_list_t,turbo_list,TURBO_META_LIST_METHODS,push_back,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_Stack (Stack,1,C1_INDEX,turbo_stack_t,turbo_stack,TURBO_META_STACK_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_Queue (Queue,1,C1_INDEX,turbo_queue_t,turbo_queue,TURBO_META_QUEUE_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_Heap (Heap,1,C1_INDEX,turbo_heap_t,turbo_heap,TURBO_META_HEAP_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_Set (Set,1,C1_SLOT,turbo_set_t,turbo_set,TURBO_META_SET_METHODS,add,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_HashSet (HashSet,1,C1_SLOT,turbo_hash_set_t,turbo_hash_set,TURBO_META_HASH_SET_METHODS,add,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_CONTAINER_KIND_ROW_HashMap (HashMap,2,C2_HASH,turbo_hash_map_t,turbo_hash_map,TURBO_META_HASH_MAP_METHODS,_,key_at,value_at_const,0,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)
#define TURBO_CONTAINER_KIND_ROW_Map (Map,2,C2_LINK,turbo_map_t,turbo_map,TURBO_META_MAP_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)
#define TURBO_CONTAINER_KIND_ROW_MultiMap (MultiMap,2,C2_MULTIMAP,turbo_multimap_t,turbo_multimap,TURBO_META_MULTIMAP_METHODS,_,key_at_const,value_at_const,0,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE)
#define TURBO_CONTAINER_KIND_ROW_BTree (BTree,2,C2_LINK,turbo_btree_t,turbo_btree,TURBO_META_BTREE_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)
#define TURBO_CONTAINER_KIND_ROW_BPlusTree (BPlusTree,2,C2_LINK,turbo_bplus_tree_t,turbo_bplus_tree,TURBO_META_BPLUS_TREE_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)

#define TURBO_CONTAINER_KIND_SCHEMA(M) Schema(M,TURBO_CONTAINER_KIND_ROW_Vec,TURBO_CONTAINER_KIND_ROW_Deque,TURBO_CONTAINER_KIND_ROW_List,TURBO_CONTAINER_KIND_ROW_Stack,TURBO_CONTAINER_KIND_ROW_Queue,TURBO_CONTAINER_KIND_ROW_Heap,TURBO_CONTAINER_KIND_ROW_Set,TURBO_CONTAINER_KIND_ROW_HashSet,TURBO_CONTAINER_KIND_ROW_HashMap,TURBO_CONTAINER_KIND_ROW_Map,TURBO_CONTAINER_KIND_ROW_MultiMap,TURBO_CONTAINER_KIND_ROW_BTree,TURBO_CONTAINER_KIND_ROW_BPlusTree)

#define TURBO_CONTAINER_KIND_APPLY(row,name,...) TURBO_CONTAINER_KIND_APPLY_E(row,name,__VA_ARGS__)
#define TURBO_CONTAINER_KIND_APPLY_E(row,name,...) TURBO_CONTAINER_KIND_APPLY_EXPAND(CMETA_PP_UNPAREN row,name,__VA_ARGS__)
#define TURBO_CONTAINER_KIND_APPLY_EXPAND(...) TURBO_CONTAINER_KIND_APPLY_I(__VA_ARGS__)
#define TURBO_CONTAINER_KIND_APPLY_I(kind,arity,family,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags,name,...) CMETA_PP_CAT(TURBO_CONTAINER_KIND_DEFINE_,family)(name,__VA_ARGS__,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags)

#define TURBO_CONTAINER_KIND_DEFINE_C1_INDEX(name,type,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER1_DEFINE(name,type,raw,prefix,CONTAINER_OK,_,methods) TURBO_META_C1_GENERATION(name,prefix) TURBO_META_C1_COLLECTOR(name,type,accept) CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,prefix,range_flags,name##_cmeta_generation,name##_collector)
#define TURBO_CONTAINER_KIND_DEFINE_C1_LINK(name,type,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER1_DEFINE(name,type,raw,prefix,CONTAINER_OK,_,methods) TURBO_META_C1_GENERATION(name,prefix) TURBO_META_C1_COLLECTOR(name,type,accept) CMETA_CONTAINER1_LINK_RANGE_DEFINE(name,type,prefix,range_flags,name##_cmeta_generation,name##_collector)
#define TURBO_CONTAINER_KIND_DEFINE_C1_SLOT(name,type,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER1_DEFINE(name,type,raw,prefix,CONTAINER_OK,_,methods) TURBO_META_C1_GENERATION(name,prefix) TURBO_META_C1_COLLECTOR(name,type,accept) CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name,type,prefix,range_flags,name##_cmeta_generation,name##_collector)
#define TURBO_CONTAINER_KIND_DEFINE_C2_HASH(name,k,v,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER2_DEFINE(name,k,v,raw,prefix,CONTAINER_OK,_,methods) TURBO_META_C2_GENERATION(name,prefix) TURBO_META_C2_COLLECTOR(name) CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,prefix,key_at_op,value_at_op,key_flags,value_flags,entry_flags,name##_cmeta_generation,name##_collector)
#define TURBO_CONTAINER_KIND_DEFINE_C2_MULTIMAP(name,k,v,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER2_DEFINE(name,k,v,raw,prefix,CONTAINER_OK,_,methods) TURBO_META_C2_GENERATION(name,prefix) TURBO_META_MULTIMAP_COLLECTOR(name) CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,turbo_multimap_range,key_at_op,value_at_op,key_flags,value_flags,entry_flags,name##_cmeta_generation,name##_collector)
#define TURBO_CONTAINER_KIND_DEFINE_C2_LINK(name,k,v,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER2_DEFINE(name,k,v,raw,prefix,CONTAINER_OK,_,methods) TURBO_META_C2_GENERATION(name,prefix) TURBO_META_C2_COLLECTOR(name) CMETA_CONTAINER2_LINK_RANGES_DEFINE(name,k,v,prefix,key_flags,value_flags,entry_flags,name##_cmeta_generation,name##_collector)

#define TURBO_VEC_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Vec,name,type)
#define TURBO_DEQUE_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Deque,name,type)
#define TURBO_LIST_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_List,name,type)
#define TURBO_STACK_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Stack,name,type)
#define TURBO_QUEUE_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Queue,name,type)
#define TURBO_HEAP_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Heap,name,type)
#define TURBO_SET_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Set,name,type)
#define TURBO_HASH_SET_DEFINE(name,type) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_HashSet,name,type)
#define TURBO_HASH_MAP_DEFINE(name,k,v) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_HashMap,name,k,v)
#define TURBO_MAP_DEFINE(name,k,v) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_Map,name,k,v)
#define TURBO_MULTI_MAP_DEFINE(name,k,v) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_MultiMap,name,k,v)
#define TURBO_BTREE_DEFINE(name,k,v) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_BTree,name,k,v)
#define TURBO_BPLUS_TREE_DEFINE(name,k,v) TURBO_CONTAINER_KIND_APPLY(TURBO_CONTAINER_KIND_ROW_BPlusTree,name,k,v)

#endif
