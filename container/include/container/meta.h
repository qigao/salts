#ifndef CONTAINER_META_H
#define CONTAINER_META_H

#include <cmeta/container.h>

#define CONTAINER_META_VEC_METHODS(M,C) \
    M(INIT_SIZE, init, init, _, C) \
    M(FROM_ARRAY_SIZE, from, from_array, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUSH_VALUE, push, push, _, C) \
    M(POP_BOOL, pop, pop, _, C) \
    M(PTR_INDEX, at, at, _, C) \
    M(CONST_PTR_INDEX, at_const, at_const, _, C) \
    M(PTR, data, data, _, C) \
    M(CONST_PTR, data_const, data_const, _, C) \
    M(SIZE, size, size, _, C) \
    M(SIZE, capacity, capacity, _, C) \
    M(BOOL, empty, empty, _, C)

#define CONTAINER_META_DEQUE_METHODS(M,C) \
    M(INIT_SIZE, init, init, _, C) \
    M(FROM_ARRAY_SIZE, from, from_array, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUSH_VALUE, push_back, push_back, _, C) \
    M(PUSH_VALUE, push_front, push_front, _, C) \
    M(POP_BOOL, pop_back, pop_back, _, C) \
    M(POP_BOOL, pop_front, pop_front, _, C) \
    M(PTR, front, front, _, C) \
    M(CONST_PTR, front_const, front_const, _, C) \
    M(PTR, back, back, _, C) \
    M(CONST_PTR, back_const, back_const, _, C) \
    M(PTR_INDEX, at, at, _, C) \
    M(CONST_PTR_INDEX, at_const, at_const, _, C) \
    M(SIZE, size, size, _, C) \
    M(SIZE, capacity, capacity, _, C) \
    M(BOOL, empty, empty, _, C)

#define CONTAINER_META_LIST_METHODS(M,C) CONTAINER_META_DEQUE_METHODS(M,C)

#define CONTAINER_META_HEAP_METHODS(M,C) \
    M(INIT_SIZE_COMPARE, init, init, _, C) \
    M(FROM_ARRAY_SIZE_COMPARE, from, from_array, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(PUSH_VALUE, push, push, _, C) \
    M(POP_BOOL, pop, pop, _, C) \
    M(CONST_PTR, peek, peek, _, C) \
    M(SIZE, size, size, _, C) \
    M(BOOL, empty, empty, _, C)

#define CONTAINER_META_SET_METHODS(M,C) \
    M(INIT_KEY_HASH, init, init, _, C) \
    M(FROM_KEYS_HASH, from, from_array, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(KEY_VALUE, add, add, _, C) \
    M(KEY_CONTAINS, contains, contains, _, C) \
    M(KEY_REMOVE_BOOL, remove, remove, _, C) \
    M(SIZE, size, size, _, C) \
    M(SIZE, capacity, capacity, _, C) \
    M(BOOL, empty, empty, _, C)

#define CONTAINER_META_HASH_MAP_METHODS(M,C) \
    M(INIT_KV_HASH, init, init, _, C) \
    M(FROM_ENTRIES, from, from, TURBO_EINVAL, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(PUT, put, put, _, C) \
    M(GET, get, get, _, C) \
    M(GET_CONST, get_const, get_const, _, C) \
    M(CONTAINS, contains, contains, _, C) \
    M(REMOVE_STATUS_BOOL, remove, remove, _, C) \
    M(SIZE, size, size, _, C) \
    M(EMPTY, empty, empty, _, C)

#define CONTAINER_META_MAP_METHODS(M,C) \
    M(INIT_KV_HASH, init, init, _, C) \
    M(FROM_ENTRIES, from, from, TURBO_EINVAL, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUT, put, put, _, C) \
    M(GET, get, get, _, C) \
    M(GET_CONST, get_const, get_const, _, C) \
    M(CONTAINS, contains, contains, _, C) \
    M(REMOVE_STATUS_BOOL, remove, remove, _, C) \
    M(SIZE, size, size, _, C) \
    M(CAPACITY, capacity, capacity, _, C) \
    M(EMPTY, empty, empty, _, C) \
    M(KEY_AT, key_at, key_at, _, C) \
    M(VALUE_AT, value_at, value_at, _, C) \
    M(VALUE_AT_CONST, value_at_const, value_at_const, _, C)

#define CONTAINER_META_MULTIMAP_METHODS(M,C) \
    M(INIT_KV_HASH, init, init, _, C) \
    M(FROM_ENTRIES, from, from, TURBO_EINVAL, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUT, put, put, _, C) \
    M(RAW_CONST_PTR_KEY, values_const, get_values_const, container_vec_t, C) \
    M(RAW_PTR_KEY, values, get_values, container_vec_t, C) \
    M(COUNT_KEY, count, key_count, _, C) \
    M(REMOVE_BOOL, remove, remove, _, C) \
    M(ERASE_KEY, erase, erase, _, C) \
    M(SIZE, size, size, _, C) \
    M(CAPACITY, capacity, capacity, _, C) \
    M(EMPTY, empty, empty, _, C) \
    M(CONTAINS, contains, contains, _, C)

#define CONTAINER_META_BPLUS_TREE_METHODS(M,C) \
    M(INIT_KV_COMPARE, init, init, _, C) \
    M(FROM_ENTRIES, from, from, TURBO_EINVAL, C) \
    M(INIT_WITH_ORDER_COMPARE, init_with_order, init_with_order, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUT, put, put, _, C) \
    M(GET, get, get, _, C) \
    M(GET_CONST, get_const, get_const, _, C) \
    M(REMOVE_STATUS_BOOL, remove, remove, _, C) \
    M(CONTAINS, contains, contains, _, C) \
    M(SIZE, size, size, _, C) \
    M(CAPACITY, capacity, capacity, _, C) \
    M(EMPTY, empty, empty, _, C) \
    M(KEY_AT, key_at, key_at, _, C) \
    M(KEY_AT_CONST, key_at_const, key_at_const, _, C) \
    M(FIND_SLOT, find_slot, find_slot, _, C) \
    M(VALUE_AT, value_at, value_at, _, C) \
    M(VALUE_AT_CONST, value_at_const, value_at_const, _, C)

#define CONTAINER_META_STACK_METHODS(M,C) \
    M(INIT_SIZE, init, init, _, C) \
    M(FROM_ARRAY_SIZE, from, from_array, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUSH_VALUE, push, push, _, C) \
    M(POP_BOOL, pop, pop, _, C) \
    M(PTR, top, top, _, C) \
    M(CONST_PTR, top_const, top_const, _, C) \
    M(SIZE, size, size, _, C) \
    M(SIZE, capacity, capacity, _, C) \
    M(BOOL, empty, empty, _, C)

#define CONTAINER_META_QUEUE_METHODS(M,C) \
    M(INIT_SIZE, init, init, _, C) \
    M(FROM_ARRAY_SIZE, from, from_array, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUSH_VALUE, push, push, _, C) \
    M(POP_BOOL, pop, pop, _, C) \
    M(PTR, front, front, _, C) \
    M(CONST_PTR, front_const, front_const, _, C) \
    M(PTR, back, back, _, C) \
    M(CONST_PTR, back_const, back_const, _, C) \
    M(SIZE, size, size, _, C) \
    M(SIZE, capacity, capacity, _, C) \
    M(BOOL, empty, empty, _, C)

#define CONTAINER_META_HASH_SET_METHODS(M,C) CONTAINER_META_SET_METHODS(M,C)

#define CONTAINER_META_BTREE_METHODS(M,C) \
    M(INIT_KV_COMPARE, init, init, _, C) \
    M(FROM_ENTRIES, from, from, TURBO_EINVAL, C) \
    M(INIT_WITH_ORDER_COMPARE, init_with_order, init_with_order, _, C) \
    M(DESTROY, destroy, destroy, _, C) \
    M(CLEAR, clear, clear, _, C) \
    M(RESERVE, reserve, reserve, _, C) \
    M(PUT, put, put, _, C) \
    M(GET, get, get, _, C) \
    M(GET_CONST, get_const, get_const, _, C) \
    M(REMOVE_STATUS_BOOL, remove, remove, _, C) \
    M(CONTAINS, contains, contains, _, C) \
    M(SIZE, size, size, _, C) \
    M(CAPACITY, capacity, capacity, _, C) \
    M(EMPTY, empty, empty, _, C) \
    M(KEY_AT, key_at, key_at, _, C) \
    M(KEY_AT_CONST, key_at_const, key_at_const, _, C) \
    M(VALUE_AT, value_at, value_at, _, C) \
    M(VALUE_AT_CONST, value_at_const, value_at_const, _, C)

#endif /* CONTAINER_META_H */
