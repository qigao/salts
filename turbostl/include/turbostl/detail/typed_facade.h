#ifndef TURBOSTL_DETAIL_TYPED_FACADE_H
#define TURBOSTL_DETAIL_TYPED_FACADE_H

/* Internal typed-facade generation. Raw public headers never include this. */
#include <cmeta/container.h>
#include <turbostl/status.h>

#include <string.h>

CMETA_INLINE cmeta_status turbo_stl_cmeta_status(stl_status status) {
 switch (status) {
  case STL_OK: return CMETA_OK;
  case STL_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
  case STL_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
  case STL_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
  case STL_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
  case STL_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
  case STL_EMPTY:
  case STL_NOT_FOUND: return CMETA_CALLBACK_ERROR;
  default: return CMETA_CALLBACK_ERROR;
 }
}

#define TURBO_META_C1_COLLECTOR(name,type,accept_method) \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,CMETA_TYPEOF(type)))return CMETA_TYPE_MISMATCH;return turbo_stl_cmeta_status((stl_status)name##_init((name*)context,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){return turbo_stl_cmeta_status((stl_status)name##_##accept_method((name*)context,*(const type*)value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(name *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,CMETA_TYPEOF(type),limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;} \
 CMETA_INLINE cmeta_collector name##_collector_erased(void *zero_output,size_t limit){return name##_collector((name*)zero_output,limit);}

#define TURBO_META_C2_COLLECTOR(name) \
 CMETA_LOCAL cmeta_type_desc name##_entry_cmeta_type; \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,&name##_entry_cmeta_type))return CMETA_TYPE_MISMATCH;if(cmeta_type_require_traits(input,CMETA_TRAIT_COPY|CMETA_TRAIT_MOVE|CMETA_TRAIT_DESTROY)!=CMETA_OK)return CMETA_TRAIT_MISSING;return turbo_stl_cmeta_status((stl_status)name##_init((name*)context,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){const name##_entry *entry=(const name##_entry*)value;return turbo_stl_cmeta_status((stl_status)name##_put((name*)context,entry->key,entry->value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(name *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,&name##_entry_cmeta_type,limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;} \
 CMETA_INLINE cmeta_collector name##_collector_erased(void *zero_output,size_t limit){return name##_collector((name*)zero_output,limit);}

/* A multimap collector's limit bounds total retained key/value pairs. */
#define TURBO_META_MULTIMAP_COLLECTOR(name) \
 CMETA_LOCAL cmeta_type_desc name##_entry_cmeta_type; \
 CMETA_INLINE cmeta_status name##_collector_begin_cb(void *context,const cmeta_type_desc *input,size_t limit){if(!cmeta_type_equal(input,&name##_entry_cmeta_type))return CMETA_TYPE_MISMATCH;if(cmeta_type_require_traits(input,CMETA_TRAIT_COPY|CMETA_TRAIT_MOVE|CMETA_TRAIT_DESTROY)!=CMETA_OK)return CMETA_TRAIT_MISSING;return turbo_stl_cmeta_status((stl_status)name##_init((name*)context,limit));} \
 CMETA_INLINE cmeta_status name##_collector_accept_cb(void *context,const void *value){const name##_entry *entry=(const name##_entry*)value;return turbo_stl_cmeta_status((stl_status)name##_put((name*)context,entry->key,entry->value));} \
 CMETA_INLINE cmeta_status name##_collector_finish_cb(void *context){(void)context;return CMETA_OK;} \
 CMETA_INLINE void name##_collector_abort_cb(void *context){name *output=(name*)context;if(output){name##_destroy(output);memset(output,0,sizeof(*output));}} \
 CMETA_LOCAL const cmeta_collector_ops name##_collector_ops={name##_collector_begin_cb,name##_collector_accept_cb,name##_collector_finish_cb,name##_collector_abort_cb}; \
 CMETA_INLINE cmeta_collector name##_collector(name *zero_output,size_t limit){cmeta_collector result={&name##_collector_ops,zero_output,zero_output,&name##_entry_cmeta_type,limit,0u,CMETA_COLLECTOR_ZERO,CMETA_OK};return result;} \
 CMETA_INLINE cmeta_collector name##_collector_erased(void *zero_output,size_t limit){return name##_collector((name*)zero_output,limit);}

#define TURBO_META_VEC_METHODS(M,C) \
 M(INIT_SIZE,init,raw_init,_,C) M(FROM_ARRAY_SIZE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(PTR_INDEX,at,at,_,C) \
 M(CONST_PTR_INDEX,at_const,at_const,_,C) M(PTR,data,data,_,C) \
 M(CONST_PTR,data_const,data_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_DEQUE_METHODS(M,C) \
 M(INIT_SIZE,init,raw_init,_,C) M(FROM_ARRAY_SIZE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push_back,push_back,_,C) \
 M(PUSH_VALUE,push_front,push_front,_,C) M(POP_BOOL,pop_back,pop_back,_,C) \
 M(POP_BOOL,pop_front,pop_front,_,C) M(PTR,front,front,_,C) \
 M(CONST_PTR,front_const,front_const,_,C) M(PTR,back,back,_,C) \
 M(CONST_PTR,back_const,back_const,_,C) M(PTR_INDEX,at,at,_,C) \
 M(CONST_PTR_INDEX,at_const,at_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_LIST_METHODS(M,C) \
 M(INIT_SIZE,init,raw_init,_,C) M(FROM_ARRAY_SIZE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(PUSH_VALUE_ITER,push_back,push_back,_,C) \
 M(PUSH_VALUE_ITER,push_front,push_front,_,C) \
 M(POP_BOOL,pop_back,pop_back,_,C) M(POP_BOOL,pop_front,pop_front,_,C) \
 M(PTR,front,front,_,C) M(CONST_PTR,front_const,front_const,_,C) \
 M(PTR,back,back,_,C) M(CONST_PTR,back_const,back_const,_,C) \
 M(SIZE,size,size,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_STACK_METHODS(M,C) \
 M(INIT_SIZE,init,raw_init,_,C) M(FROM_ARRAY_SIZE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(PTR,top,top,_,C) \
 M(CONST_PTR,top_const,top_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_QUEUE_METHODS(M,C) \
 M(INIT_SIZE,init,raw_init,_,C) M(FROM_ARRAY_SIZE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,destroy,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(PTR,front,front,_,C) \
 M(CONST_PTR,front_const,front_const,_,C) M(PTR,back,back,_,C) \
 M(CONST_PTR,back_const,back_const,_,C) M(SIZE,size,size,_,C) \
 M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_HEAP_METHODS(M,C) \
 M(INIT_SIZE_COMPARE,init,raw_init,_,C) M(FROM_ARRAY_SIZE_COMPARE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUSH_VALUE,push,push,_,C) \
 M(POP_BOOL,pop,pop,_,C) M(CONST_PTR,peek,peek,_,C) \
 M(SIZE,size,size,_,C) M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_SET_METHODS(M,C) \
 M(INIT_KEY_COMPARE,init,raw_init,_,C) M(FROM_KEYS_COMPARE,from,raw_from_array,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(KEY_VALUE,add,add,_,C) \
 M(KEY_CONTAINS,contains,contains,_,C) M(KEY_REMOVE_BOOL,remove,remove,_,C) \
 M(SIZE,size,size,_,C) M(BOOL,empty,empty,_,C)
#define TURBO_META_HASH_SET_METHODS(M,C) \
 M(INIT_KEY_HASH,init,raw_init,_,C) M(FROM_KEYS_HASH,from,raw_from_array,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(KEY_VALUE,add,add,_,C) \
 M(KEY_CONTAINS,contains,contains,_,C) M(KEY_REMOVE_BOOL,remove,remove,_,C) \
 M(SIZE,size,size,_,C) M(SIZE,capacity,capacity,_,C) M(BOOL,empty,empty,_,C)

#define TURBO_META_C1_GENERATION(name,prefix) \
 CMETA_INLINE uint64_t name##_cmeta_generation(const void *object) { \
   const name *self = (const name *)object; \
   return self == NULL ? UINT64_C(0) : prefix##_generation(&self->raw); \
 }
#define TURBO_META_C2_GENERATION(name,prefix) TURBO_META_C1_GENERATION(name,prefix)

#define TURBO_META_HASH_MAP_METHODS(M,C) \
 M(INIT_KV_HASH,init,raw_init,_,C) M(FROM_ENTRIES,from,raw_from_arrays,STL_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(CAPACITY,capacity,capacity,_,C) M(EMPTY,empty,empty,_,C) \
 M(KEY_AT,key_at,key_at,_,C) M(KEY_AT_CONST,key_at_const,key_at_const,_,C) \
 M(VALUE_AT,value_at,value_at,_,C) M(VALUE_AT_CONST,value_at_const,value_at_const,_,C)

#define TURBO_META_MAP_METHODS(M,C) \
 M(INIT_KV_COMPARE,init,raw_init,_,C) M(FROM_ENTRIES_LINK,from,raw_from_arrays,STL_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(EMPTY,empty,empty,_,C)

#define TURBO_META_MULTIMAP_METHODS(M,C) \
 M(INIT_KV_COMPARE,init,raw_init,_,C) \
 M(FROM_ENTRIES_LINK,from,raw_from_arrays,STL_INVALID_ARGUMENT,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(PUT,put,put,_,C) \
 M(CONTAINS,contains,contains,_,C) M(COUNT_KEY,count,count,_,C) \
 M(REMOVE_BOOL,remove,remove,_,C) M(ERASE_KEY,erase,erase,_,C) \
 M(SIZE,size,size,_,C) M(EMPTY,empty,empty,_,C)

#define TURBO_META_TREE_METHODS(M,C) \
 M(INIT_KV_COMPARE,init,raw_init,_,C) M(FROM_ENTRIES,from,raw_from_arrays,STL_INVALID_ARGUMENT,C) \
 M(INIT_WITH_ORDER_COMPARE,init_with_order,raw_init_with_order,_,C) \
 M(DESTROY,destroy,raw_destroy_storage,_,C) M(CLEAR,clear,clear,_,C) \
 M(RESERVE,reserve,reserve,_,C) M(PUT,put,put,_,C) M(GET,get,get,_,C) \
 M(GET_CONST,get_const,get_const,_,C) M(CONTAINS,contains,contains,_,C) \
 M(REMOVE_STATUS_BOOL,remove,remove,_,C) M(SIZE,size,size,_,C) \
 M(CAPACITY,capacity,capacity,_,C) M(EMPTY,empty,empty,_,C) \
 M(KEY_AT,key_at,key_at,_,C) M(KEY_AT_CONST,key_at_const,key_at_const,_,C) \
 M(VALUE_AT,value_at,value_at,_,C) M(VALUE_AT_CONST,value_at_const,value_at_const,_,C)
#define TURBO_META_BTREE_METHODS(M,C) TURBO_META_TREE_METHODS(M,C)
#define TURBO_META_BPLUS_TREE_METHODS(M,C) TURBO_META_TREE_METHODS(M,C)

/* list_* and map_* are also semantic public macro names in typed.h. Keep the
 * generator on an internal prefix so preprocessing a later typed(...) call
 * cannot accidentally re-enter those semantic macros. */
CMETA_INLINE stl_status turbo_stl_typed_list_raw_init(
    list_t *self, const cmeta_type_desc *type, size_t limit) {
  return list_raw_init(self, type, limit);
}
CMETA_INLINE stl_status turbo_stl_typed_list_raw_from_array(
    list_t *self, const void *values, size_t count,
    const cmeta_type_desc *type, size_t limit) {
  return list_raw_from_array(self, values, count, type, limit);
}
CMETA_INLINE void turbo_stl_typed_list_raw_destroy_storage(list_t *self) {
  list_raw_destroy_storage(self);
}
CMETA_INLINE void turbo_stl_typed_list_clear(list_t *self) {
  list_clear(self);
}
CMETA_INLINE stl_status turbo_stl_typed_list_push_back(
    list_t *self, const void *value, list_iter_t *iterator) {
  return list_push_back(self, value, iterator);
}
CMETA_INLINE stl_status turbo_stl_typed_list_push_front(
    list_t *self, const void *value, list_iter_t *iterator) {
  return list_push_front(self, value, iterator);
}
CMETA_INLINE stl_status turbo_stl_typed_list_pop_back(
    list_t *self, void *value) {
  return list_pop_back(self, value);
}
CMETA_INLINE stl_status turbo_stl_typed_list_pop_front(
    list_t *self, void *value) {
  return list_pop_front(self, value);
}
CMETA_INLINE void *turbo_stl_typed_list_front(list_t *self) {
  return list_front(self);
}
CMETA_INLINE const void *turbo_stl_typed_list_front_const(
    const list_t *self) {
  return list_front_const(self);
}
CMETA_INLINE void *turbo_stl_typed_list_back(list_t *self) {
  return list_back(self);
}
CMETA_INLINE const void *turbo_stl_typed_list_back_const(
    const list_t *self) {
  return list_back_const(self);
}
CMETA_INLINE size_t turbo_stl_typed_list_size(const list_t *self) {
  return list_size(self);
}
CMETA_INLINE uint64_t turbo_stl_typed_list_generation(const list_t *self) {
  return list_generation(self);
}
CMETA_INLINE bool turbo_stl_typed_list_empty(const list_t *self) {
  return list_empty(self);
}
CMETA_INLINE bool turbo_stl_typed_list_range_next(
    const list_t *self, cmeta_range_cursor *cursor, const void **value) {
  return list_range_next(self, cursor, value);
}

CMETA_INLINE stl_status turbo_stl_typed_map_raw_init(
    map_t *self, const cmeta_type_desc *key_type,
    const cmeta_type_desc *value_type, size_t limit) {
  return map_raw_init(self, key_type, value_type, limit);
}
CMETA_INLINE stl_status turbo_stl_typed_map_raw_from_arrays(
    map_t *self, const void *keys, const void *values, size_t count,
    const cmeta_type_desc *key_type, const cmeta_type_desc *value_type,
    size_t limit) {
  return map_raw_from_arrays(self, keys, values, count, key_type, value_type,
                             limit);
}
CMETA_INLINE void turbo_stl_typed_map_raw_destroy_storage(map_t *self) {
  map_raw_destroy_storage(self);
}
CMETA_INLINE void turbo_stl_typed_map_destroy(map_t *self) {
  map_raw_destroy_storage(self);
}
CMETA_INLINE void turbo_stl_typed_map_clear(map_t *self) {
  map_clear(self);
}
CMETA_INLINE stl_status turbo_stl_typed_map_put(
    map_t *self, const void *key, const void *value) {
  return map_put(self, key, value);
}
CMETA_INLINE void *turbo_stl_typed_map_get(map_t *self, const void *key) {
  return map_get(self, key);
}
CMETA_INLINE const void *turbo_stl_typed_map_get_const(
    const map_t *self, const void *key) {
  return map_get_const(self, key);
}
CMETA_INLINE bool turbo_stl_typed_map_contains(
    const map_t *self, const void *key) {
  return map_contains(self, key);
}
CMETA_INLINE stl_status turbo_stl_typed_map_remove(
    map_t *self, const void *key, void *value) {
  return map_remove(self, key, value);
}
CMETA_INLINE size_t turbo_stl_typed_map_size(const map_t *self) {
  return map_size(self);
}
CMETA_INLINE uint64_t turbo_stl_typed_map_generation(const map_t *self) {
  return map_generation(self);
}
CMETA_INLINE bool turbo_stl_typed_map_empty(const map_t *self) {
  return map_empty(self);
}
CMETA_INLINE bool turbo_stl_typed_map_range_next(
    const map_t *self, cmeta_range_cursor *cursor, const void **key,
    const void **value) {
  return map_range_next(self, cursor, key, value);
}

/* The row macros are the sole semantic mapping for every standard kind.
 * C cannot emit #define directives from Replay, so the public kind markers and
 * adapters remain thin language-policy shims that reference these rows. */
#define TURBO_STL_KIND_ROW_Vec (Vec,1,C1_INDEX,vec_t,vec,TURBO_META_VEC_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_CONTIGUOUS|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_Deque (Deque,1,C1_INDEX,deque_t,deque,TURBO_META_DEQUE_METHODS,push_back,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_List (List,1,C1_LINK,list_t,turbo_stl_typed_list,TURBO_META_LIST_METHODS,push_back,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_Stack (Stack,1,C1_INDEX,stack_t,stack,TURBO_META_STACK_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_Queue (Queue,1,C1_INDEX,queue_t,queue,TURBO_META_QUEUE_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_Heap (Heap,1,C1_INDEX,heap_t,heap,TURBO_META_HEAP_METHODS,push,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_RANDOM_ACCESS|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_Set (Set,1,C1_LINK,set_t,set,TURBO_META_SET_METHODS,add,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_HashSet (HashSet,1,C1_SLOT,hash_set_t,hash_set,TURBO_META_HASH_SET_METHODS,add,_,_,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,0,0,0)
#define TURBO_STL_KIND_ROW_HashMap (HashMap,2,C2_HASH,hash_map_t,hash_map,TURBO_META_HASH_MAP_METHODS,_,key_at,value_at_const,0,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)
#define TURBO_STL_KIND_ROW_Map (Map,2,C2_LINK,map_t,turbo_stl_typed_map,TURBO_META_MAP_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)
#define TURBO_STL_KIND_ROW_MultiMap (MultiMap,2,C2_LINK,multimap_t,multimap,TURBO_META_MULTIMAP_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_REUSABLE)
#define TURBO_STL_KIND_ROW_BTree (BTree,2,C2_LINK,btree_t,btree,TURBO_META_BTREE_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)
#define TURBO_STL_KIND_ROW_BPlusTree (BPlusTree,2,C2_LINK,bplus_tree_t,bplus_tree,TURBO_META_BPLUS_TREE_METHODS,_,_,_,0,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_REUSABLE,CMETA_RANGE_SIZED|CMETA_RANGE_ORDERED|CMETA_RANGE_SORTED|CMETA_RANGE_UNIQUE|CMETA_RANGE_REUSABLE)

#define TURBO_STL_KIND_SCHEMA(M) Schema(M,TURBO_STL_KIND_ROW_Vec,TURBO_STL_KIND_ROW_Deque,TURBO_STL_KIND_ROW_List,TURBO_STL_KIND_ROW_Stack,TURBO_STL_KIND_ROW_Queue,TURBO_STL_KIND_ROW_Heap,TURBO_STL_KIND_ROW_Set,TURBO_STL_KIND_ROW_HashSet,TURBO_STL_KIND_ROW_HashMap,TURBO_STL_KIND_ROW_Map,TURBO_STL_KIND_ROW_MultiMap,TURBO_STL_KIND_ROW_BTree,TURBO_STL_KIND_ROW_BPlusTree)

#define TURBO_STL_KIND_APPLY(row,name,...) TURBO_STL_KIND_APPLY_E(row,name,__VA_ARGS__)
#define TURBO_STL_KIND_APPLY_E(row,name,...) TURBO_STL_KIND_APPLY_EXPAND(CMETA_PP_UNPAREN row,name,__VA_ARGS__)
#define TURBO_STL_KIND_APPLY_EXPAND(...) TURBO_STL_KIND_APPLY_I(__VA_ARGS__)
#define TURBO_STL_KIND_APPLY_I(kind,arity,family,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags,name,...) CMETA_PP_CAT(TURBO_STL_KIND_DEFINE_,family)(name,__VA_ARGS__,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags)

#define TURBO_STL_KIND_DEFINE_C1_INDEX(name,type,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER1_DEFINE(name,type,raw,prefix,STL_OK,_,methods) TURBO_META_C1_GENERATION(name,prefix) TURBO_META_C1_COLLECTOR(name,type,accept) CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name,type,prefix,range_flags,name##_cmeta_generation,name##_collector_erased)
#define TURBO_STL_KIND_DEFINE_C1_LINK(name,type,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER1_DEFINE(name,type,raw,prefix,STL_OK,_,methods) TURBO_META_C1_GENERATION(name,prefix) TURBO_META_C1_COLLECTOR(name,type,accept) CMETA_CONTAINER1_LINK_RANGE_DEFINE(name,type,prefix,range_flags,name##_cmeta_generation,name##_collector_erased)
#define TURBO_STL_KIND_DEFINE_C1_SLOT(name,type,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER1_DEFINE(name,type,raw,prefix,STL_OK,_,methods) TURBO_META_C1_GENERATION(name,prefix) TURBO_META_C1_COLLECTOR(name,type,accept) CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name,type,prefix,range_flags,name##_cmeta_generation,name##_collector_erased)
#define TURBO_STL_KIND_DEFINE_C2_HASH(name,k,v,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER2_DEFINE(name,k,v,raw,prefix,STL_OK,_,methods) TURBO_META_C2_GENERATION(name,prefix) TURBO_META_C2_COLLECTOR(name) CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,prefix,key_at_op,value_at_op,key_flags,value_flags,entry_flags,name##_cmeta_generation,name##_collector_erased)
#define TURBO_STL_KIND_DEFINE_C2_MULTIMAP(name,k,v,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER2_DEFINE(name,k,v,raw,prefix,STL_OK,_,methods) TURBO_META_C2_GENERATION(name,prefix) TURBO_META_MULTIMAP_COLLECTOR(name) CMETA_CONTAINER2_RANGES_DEFINE(name,k,v,multimap,key_at_op,value_at_op,key_flags,value_flags,entry_flags,name##_cmeta_generation,name##_collector_erased)
#define TURBO_STL_KIND_DEFINE_C2_LINK(name,k,v,raw,prefix,methods,accept,key_at_op,value_at_op,range_flags,key_flags,value_flags,entry_flags) \
 CMETA_CONTAINER2_DEFINE(name,k,v,raw,prefix,STL_OK,_,methods) TURBO_META_C2_GENERATION(name,prefix) TURBO_META_C2_COLLECTOR(name) CMETA_CONTAINER2_LINK_RANGES_DEFINE(name,k,v,prefix,key_flags,value_flags,entry_flags,name##_cmeta_generation,name##_collector_erased)

#define TURBO_VEC_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Vec,name,type)
#define TURBO_DEQUE_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Deque,name,type)
#define TURBO_LIST_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_List,name,type)
#define TURBO_STACK_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Stack,name,type)
#define TURBO_QUEUE_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Queue,name,type)
#define TURBO_HEAP_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Heap,name,type)
#define TURBO_SET_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Set,name,type)
#define TURBO_HASH_SET_DEFINE(name,type) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_HashSet,name,type)
#define TURBO_HASH_MAP_DEFINE(name,k,v) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_HashMap,name,k,v)
#define TURBO_MAP_DEFINE(name,k,v) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_Map,name,k,v)
#define TURBO_MULTI_MAP_DEFINE(name,k,v) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_MultiMap,name,k,v)
#define TURBO_BTREE_DEFINE(name,k,v) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_BTree,name,k,v)
#define TURBO_BPLUS_TREE_DEFINE(name,k,v) TURBO_STL_KIND_APPLY(TURBO_STL_KIND_ROW_BPlusTree,name,k,v)

#endif /* TURBOSTL_DETAIL_TYPED_FACADE_H */
