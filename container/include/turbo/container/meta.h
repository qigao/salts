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
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,&name##_entry_cmeta_type))return CMETA_TYPE_MISMATCH;return turbo_container_cmeta_status((container_status)name##_init((name*)context,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){const name##_entry *entry=(const name##_entry*)value;return turbo_container_cmeta_status((container_status)name##_put((name*)context,entry->key,entry->value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(void *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,&name##_entry_cmeta_type,limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;}

/* A multimap collector's single factory limit bounds both distinct keys and
 * values retained for any one key. */
#define TURBO_META_MULTIMAP_COLLECTOR(name) \
 CMETA_LOCAL cmeta_type_desc name##_entry_cmeta_type; \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,&name##_entry_cmeta_type))return CMETA_TYPE_MISMATCH;return turbo_container_cmeta_status((container_status)name##_init((name*)context,limit,limit));} \
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

#define TURBO_META_MAP_METHODS(M,C) \
 M(INIT_KV_HASH,init,init,_,C) M(FROM_ENTRIES,from,from,CONTAINER_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(CAPACITY,capacity,capacity,_,C) M(EMPTY,empty,empty,_,C) \
 M(KEY_AT,key_at,key_at,_,C) M(KEY_AT_CONST,key_at_const,key_at_const,_,C) \
 M(VALUE_AT,value_at,value_at,_,C) M(VALUE_AT_CONST,value_at_const,value_at_const,_,C)
#define TURBO_META_HASH_MAP_METHODS(M,C) TURBO_META_MAP_METHODS(M,C)

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

#define TURBO_VEC_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_vec_t,turbo_vec,CONTAINER_OK,_,TURBO_META_VEC_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_vec) \
 TURBO_META_C1_COLLECTOR(name,type,push) \
 CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,turbo_vec,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_CONTIGUOUS|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_DEQUE_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_deque_t,turbo_deque,CONTAINER_OK,_,TURBO_META_DEQUE_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_deque) \
 TURBO_META_C1_COLLECTOR(name,type,push_back) \
 CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,turbo_deque,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_LIST_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_list_t,turbo_list,CONTAINER_OK,_,TURBO_META_LIST_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_list) \
 TURBO_META_C1_COLLECTOR(name,type,push_back) \
 CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,turbo_list,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_STACK_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_stack_t,turbo_stack,CONTAINER_OK,_,TURBO_META_STACK_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_stack) \
 TURBO_META_C1_COLLECTOR(name,type,push) \
 CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,turbo_stack,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_QUEUE_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_queue_t,turbo_queue,CONTAINER_OK,_,TURBO_META_QUEUE_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_queue) \
 TURBO_META_C1_COLLECTOR(name,type,push) \
 CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,turbo_queue,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_HEAP_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_heap_t,turbo_heap,CONTAINER_OK,_,TURBO_META_HEAP_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_heap) \
 TURBO_META_C1_COLLECTOR(name,type,push) \
 CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,turbo_heap,CMETA_RANGE_SIZED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_SET_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_set_t,turbo_set,CONTAINER_OK,_,TURBO_META_SET_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_set) \
 TURBO_META_C1_COLLECTOR(name,type,add) \
 CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name,type,turbo_set,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_HASH_SET_DEFINE(name,type) \
 CMETA_CONTAINER1_DEFINE(name,type,turbo_hash_set_t,turbo_hash_set,CONTAINER_OK,_,TURBO_META_HASH_SET_METHODS) \
 TURBO_META_C1_GENERATION(name,turbo_hash_set) \
 TURBO_META_C1_COLLECTOR(name,type,add) \
 CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name,type,turbo_hash_set,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_HASH_MAP_DEFINE(name,k,v) \
 CMETA_CONTAINER2_DEFINE(name,k,v,turbo_hash_map_t,turbo_hash_map,CONTAINER_OK,_,TURBO_META_HASH_MAP_METHODS) \
 TURBO_META_C2_GENERATION(name,turbo_hash_map) \
 TURBO_META_C2_COLLECTOR(name) \
 CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,turbo_hash_map,key_at,value_at_const,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_MAP_DEFINE(name,k,v) \
 CMETA_CONTAINER2_DEFINE(name,k,v,turbo_map_t,turbo_map,CONTAINER_OK,_,TURBO_META_MAP_METHODS) \
 TURBO_META_C2_GENERATION(name,turbo_map) \
 TURBO_META_C2_COLLECTOR(name) \
 CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,turbo_map,key_at_const,value_at_const,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_MULTI_MAP_DEFINE(name,k,v) \
 CMETA_CONTAINER2_DEFINE(name,k,v,turbo_multimap_t,turbo_multimap,CONTAINER_OK,_,TURBO_META_MULTIMAP_METHODS) \
 TURBO_META_C2_GENERATION(name,turbo_multimap) \
 TURBO_META_MULTIMAP_COLLECTOR(name) \
 CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,turbo_multimap_range,key_at_const,value_at_const,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_BTREE_DEFINE(name,k,v) \
 CMETA_CONTAINER2_DEFINE(name,k,v,turbo_btree_t,turbo_btree,CONTAINER_OK,_,TURBO_META_BTREE_METHODS) \
 TURBO_META_C2_GENERATION(name,turbo_btree) \
 TURBO_META_C2_COLLECTOR(name) \
 CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,turbo_btree,key_at_const,value_at_const,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)
#define TURBO_BPLUS_TREE_DEFINE(name,k,v) \
 CMETA_CONTAINER2_DEFINE(name,k,v,turbo_bplus_tree_t,turbo_bplus_tree,CONTAINER_OK,_,TURBO_META_BPLUS_TREE_METHODS) \
 TURBO_META_C2_GENERATION(name,turbo_bplus_tree) \
 TURBO_META_C2_COLLECTOR(name) \
 CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,turbo_bplus_tree,key_at_const,value_at_const,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,name##_cmeta_generation,name##_collector)

#endif
