#ifndef TURBOSTL_NATURAL_BRIDGE_H
#define TURBOSTL_NATURAL_BRIDGE_H

/* Temporary repository-migration bridge.
 *
 * This file exists only while implementation symbols and repository consumers
 * are being renamed. It must be deleted before the natural-API refactor is
 * complete; installed headers must not retain turbo_* aliases in either
 * direction.
 */

typedef turbo_vec_t vec_t;
#define vec_init turbo_vec_init
#define vec_init_bytes turbo_vec_init_bytes
#define vec_from_array turbo_vec_from_array
#define vec_from_array_bytes turbo_vec_from_array_bytes
#define vec_destroy turbo_vec_destroy
#define vec_clear turbo_vec_clear
#define vec_reserve turbo_vec_reserve
#define vec_resize turbo_vec_resize
#define vec_push turbo_vec_push
#define vec_pop turbo_vec_pop
#define vec_insert turbo_vec_insert
#define vec_set turbo_vec_set
#define vec_erase turbo_vec_erase
#define vec_swap_remove turbo_vec_swap_remove
#define vec_at turbo_vec_at
#define vec_at_const turbo_vec_at_const
#define vec_data turbo_vec_data
#define vec_data_const turbo_vec_data_const
#define vec_size turbo_vec_size
#define vec_capacity turbo_vec_capacity
#define vec_generation turbo_vec_generation
#define vec_empty turbo_vec_empty

typedef turbo_deque_t deque_t;
#define deque_init turbo_deque_init
#define deque_init_bytes turbo_deque_init_bytes
#define deque_from_array turbo_deque_from_array
#define deque_from_array_bytes turbo_deque_from_array_bytes
#define deque_destroy turbo_deque_destroy
#define deque_clear turbo_deque_clear
#define deque_reserve turbo_deque_reserve
#define deque_push_back turbo_deque_push_back
#define deque_push_front turbo_deque_push_front
#define deque_pop_back turbo_deque_pop_back
#define deque_pop_front turbo_deque_pop_front
#define deque_set turbo_deque_set
#define deque_front turbo_deque_front
#define deque_front_const turbo_deque_front_const
#define deque_back turbo_deque_back
#define deque_back_const turbo_deque_back_const
#define deque_at turbo_deque_at
#define deque_at_const turbo_deque_at_const
#define deque_size turbo_deque_size
#define deque_capacity turbo_deque_capacity
#define deque_generation turbo_deque_generation
#define deque_empty turbo_deque_empty

typedef turbo_list_t list_t;
typedef turbo_list_iter_t list_iter_t;
#define list_init turbo_list_init
#define list_init_bytes turbo_list_init_bytes
#define list_from_array turbo_list_from_array
#define list_from_array_bytes turbo_list_from_array_bytes
#define list_destroy turbo_list_destroy
#define list_clear turbo_list_clear
#define list_push_front turbo_list_push_front
#define list_push_back turbo_list_push_back
#define list_insert_before turbo_list_insert_before
#define list_insert_after turbo_list_insert_after
#define list_erase turbo_list_erase
#define list_pop_front turbo_list_pop_front
#define list_pop_back turbo_list_pop_back
#define list_begin turbo_list_begin
#define list_end turbo_list_end
#define list_iter_next turbo_list_iter_next
#define list_iter_prev turbo_list_iter_prev
#define list_iter_equal turbo_list_iter_equal
#define list_iter_value turbo_list_iter_value
#define list_iter_value_const turbo_list_iter_value_const
#define list_front turbo_list_front
#define list_front_const turbo_list_front_const
#define list_back turbo_list_back
#define list_back_const turbo_list_back_const
#define list_size turbo_list_size
#define list_generation turbo_list_generation
#define list_empty turbo_list_empty
#define list_range_next turbo_list_range_next

typedef turbo_stack_t stack_t;
#define stack_init turbo_stack_init
#define stack_init_bytes turbo_stack_init_bytes
#define stack_from_array turbo_stack_from_array
#define stack_from_array_bytes turbo_stack_from_array_bytes
#define stack_destroy turbo_stack_destroy
#define stack_clear turbo_stack_clear
#define stack_reserve turbo_stack_reserve
#define stack_push turbo_stack_push
#define stack_pop turbo_stack_pop
#define stack_top turbo_stack_top
#define stack_top_const turbo_stack_top_const
#define stack_at_const turbo_stack_at_const
#define stack_size turbo_stack_size
#define stack_capacity turbo_stack_capacity
#define stack_generation turbo_stack_generation
#define stack_empty turbo_stack_empty

typedef turbo_queue_t queue_t;
#define queue_init turbo_queue_init
#define queue_init_bytes turbo_queue_init_bytes
#define queue_from_array turbo_queue_from_array
#define queue_from_array_bytes turbo_queue_from_array_bytes
#define queue_destroy turbo_queue_destroy
#define queue_clear turbo_queue_clear
#define queue_reserve turbo_queue_reserve
#define queue_push turbo_queue_push
#define queue_pop turbo_queue_pop
#define queue_front turbo_queue_front
#define queue_front_const turbo_queue_front_const
#define queue_back turbo_queue_back
#define queue_back_const turbo_queue_back_const
#define queue_at_const turbo_queue_at_const
#define queue_size turbo_queue_size
#define queue_capacity turbo_queue_capacity
#define queue_generation turbo_queue_generation
#define queue_empty turbo_queue_empty

typedef turbo_heap_compare_fn heap_compare_fn;
typedef turbo_heap_t heap_t;
#define heap_init turbo_heap_init
#define heap_init_bytes turbo_heap_init_bytes
#define heap_from_array turbo_heap_from_array
#define heap_from_array_bytes turbo_heap_from_array_bytes
#define heap_destroy turbo_heap_destroy
#define heap_clear turbo_heap_clear
#define heap_reserve turbo_heap_reserve
#define heap_push turbo_heap_push
#define heap_pop turbo_heap_pop
#define heap_peek turbo_heap_peek
#define heap_at_const turbo_heap_at_const
#define heap_size turbo_heap_size
#define heap_capacity turbo_heap_capacity
#define heap_generation turbo_heap_generation
#define heap_empty turbo_heap_empty

#define stable_sort turbo_stable_sort

typedef turbo_hash_fn hash_fn;
typedef turbo_hash_equal_fn hash_equal_fn;
typedef turbo_hash_map_t hash_map_t;
#define hash_bytes turbo_hash_bytes
#define hash_key_equal turbo_hash_key_equal
#define hash_map_init turbo_hash_map_init
#define hash_map_init_bytes turbo_hash_map_init_bytes
#define hash_map_from_arrays turbo_hash_map_from_arrays
#define hash_map_from_arrays_bytes turbo_hash_map_from_arrays_bytes
#define hash_map_destroy turbo_hash_map_destroy
#define hash_map_clear turbo_hash_map_clear
#define hash_map_reserve turbo_hash_map_reserve
#define hash_map_put turbo_hash_map_put
#define hash_map_get turbo_hash_map_get
#define hash_map_get_const turbo_hash_map_get_const
#define hash_map_contains turbo_hash_map_contains
#define hash_map_remove turbo_hash_map_remove
#define hash_map_size turbo_hash_map_size
#define hash_map_capacity turbo_hash_map_capacity
#define hash_map_entry_limit turbo_hash_map_entry_limit
#define hash_map_generation turbo_hash_map_generation
#define hash_map_empty turbo_hash_map_empty
#define hash_map_key_at turbo_hash_map_key_at
#define hash_map_key_at_const turbo_hash_map_key_at_const
#define hash_map_value_at turbo_hash_map_value_at
#define hash_map_value_at_const turbo_hash_map_value_at_const

#endif /* TURBOSTL_NATURAL_BRIDGE_H */
