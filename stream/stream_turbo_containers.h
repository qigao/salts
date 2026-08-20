#ifndef C11_STREAM_TURBO_CONTAINERS_H
#define C11_STREAM_TURBO_CONTAINERS_H

#include "stream_container.h"
#include "turbo_containers.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Borrowed, read-only adapters for TurboUtils containers.
 * The container must outlive the stream and remain structurally unchanged
 * between cursor initialization/reset and traversal completion.
 */
stream_result_t stream_from_turbo_vec(stream_t *stream, const turbo_vec_t *vec);
stream_result_t stream_from_turbo_deque(stream_t *stream, const turbo_deque_t *deque);
stream_result_t stream_from_turbo_list(stream_t *stream, const turbo_list_t *list);

stream_result_t stream_from_turbo_hash_keys(
    stream_t *stream,
    const turbo_hash_map_t *map);
stream_result_t stream_from_turbo_hash_values(
    stream_t *stream,
    const turbo_hash_map_t *map);

stream_result_t stream_from_turbo_map_keys(stream_t *stream, const turbo_map_t *map);
stream_result_t stream_from_turbo_map_values(stream_t *stream, const turbo_map_t *map);
stream_result_t stream_from_turbo_set(stream_t *stream, const turbo_set_t *set);
stream_result_t stream_from_turbo_multimap_keys(
    stream_t *stream,
    const turbo_multimap_t *map);
stream_result_t stream_from_turbo_multimap_values(
    stream_t *stream,
    const turbo_multimap_t *map);
stream_result_t stream_from_turbo_heap(stream_t *stream, const turbo_heap_t *heap);
stream_result_t stream_from_turbo_tree_map_keys(
    stream_t *stream,
    const turbo_tree_map_t *map);
stream_result_t stream_from_turbo_tree_map_values(
    stream_t *stream,
    const turbo_tree_map_t *map);
stream_result_t stream_from_turbo_bplus_tree_keys(
    stream_t *stream,
    const turbo_bplus_tree_t *map);
stream_result_t stream_from_turbo_bplus_tree_values(
    stream_t *stream,
    const turbo_bplus_tree_t *map);

/*
 * Append pipeline values to an initialized vector whose elem_size matches the
 * current stream value. max_items is a hard final-size limit. Reaching it
 * returns STREAM_FULL without pulling another source item. Pointer-bearing
 * values retain their normal shallow-copy semantics.
 */
stream_result_t stream_collect_turbo_vec(
    stream_t *stream,
    turbo_vec_t *out,
    size_t max_items);

/*
 * Append pipeline values to an initialized list whose elem_size matches the
 * current stream value. max_items is a hard final-size limit. Reaching it
 * returns STREAM_FULL without pulling another source item. Pointer-bearing
 * values retain their normal shallow-copy semantics.
 */
stream_result_t stream_collect_turbo_list(
    stream_t *stream,
    turbo_list_t *out,
    size_t max_items);

/*
 * Append pipeline values to an initialized set whose key size matches the current
 * stream value. Duplicates are deduplicated by turbo_set semantics and do not
 * increase element count. max_items is a hard final-size limit.
 */
stream_result_t stream_collect_turbo_set(
    stream_t *stream,
    turbo_set_t *out,
    size_t max_items);

/*
 * Bucketed collect into an initialized map whose key type is `key_size` bytes and
 * whose value type is `size_t`.
 *
 * `key_selector` maps each stream value into a temporary key buffer of size
 * `key_size`. If it returns anything other than `STREAM_OK`, collection fails.
 * Duplicate keys increment the existing count; new keys are inserted with count 1.
 * `max_items` is a hard final-size limit for distinct keys.
 */
stream_result_t stream_collect_turbo_map_count(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    stream_mapper_fn key_selector);

/*
 * Partition stream values into caller-initialized lists by predicate.
 *
 * `true_dest` receives values where `predicate(value)` returns true,
 * `false_dest` receives the remainder.
 *
 * `max_true_items` and `max_false_items` are hard final-size limits for
 * each destination list. A destination reaching its limit returns
 * STREAM_FULL when the next produced value belongs to that bucket.
 * If both limits are zero, no source item is consumed.
 */
stream_result_t stream_collect_turbo_partition(
    stream_t *stream,
    turbo_list_t *true_dest,
    size_t max_true_items,
    turbo_list_t *false_dest,
    size_t max_false_items,
    stream_predicate_fn predicate);

/*
 * Partition stream values by predicate and count each bucket in an initialized map.
 *
 * `out` must be initialized with:
 * - key_size == 1 (bucket key is a one-byte marker)
 * - value_size == sizeof(size_t)
 *
 * Bucket keys are:
 * - 0 => predicate(value) == false
 * - 1 => predicate(value) == true
 *
 * `max_items` is a hard final-size limit for distinct buckets. If the next
 * source value would require a new bucket while already at limit, STREAM_FULL is
 * returned.
 */
stream_result_t stream_collect_turbo_partition_count(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    stream_predicate_fn predicate);

/*
 * Partition stream values by predicate and reduce each bucket through an adapter.
 *
 * `out` must be initialized with:
 * - key_size == 1 (bucket key is a one-byte marker)
 * - value_size == `value_size`
 *
 * Bucket keys are:
 * - 0 => predicate(value) == false
 * - 1 => predicate(value) == true
 *
 * `value_mapper` maps each source value into a temporary value buffer of size
 * `value_size` before storing or reducing.
 *
 * `value_reducer` merges a mapped value into an existing mapped value.
 * If `value_reducer == NULL`, duplicates replace the existing bucket value.
 * If `max_items` is already reached and the next value would create a new
 * bucket, STREAM_FULL is returned.
 */
stream_result_t stream_collect_turbo_partition_reduce(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t value_size,
    stream_predicate_fn predicate,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer);

/*
 * Bucketed collect into an initialized multimap where key/value extraction is
 * delegated to callbacks. The callback results are copied into temporary buffers
 * of size `key_size` and `value_size` respectively.
 *
 * `max_items` is the hard final-size limit for total entries in the output
 * multimap (duplicate keys count as separate entries). Reaching it returns
 * STREAM_FULL without consuming another source item.
 */
stream_result_t stream_collect_turbo_multimap(
    stream_t *stream,
    turbo_multimap_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper);

typedef enum {
    STREAM_TURBO_MAP_KEEP_LAST = 0,
    STREAM_TURBO_MAP_KEEP_FIRST,
    STREAM_TURBO_MAP_REJECT,
    STREAM_TURBO_MAP_MERGE
} stream_turbo_map_conflict_mode_t;

/*
 * General map collector.
 *
 * `key_selector` maps each stream value into a temporary key buffer of
 * `key_size` bytes.
 *
 * `value_mapper` maps each stream value into a temporary value buffer of
 * `value_size` bytes.
 *
 * By default, duplicate handling is:
 * - if `value_reducer == NULL`: replace (`KEEP_LAST`);
 * - if `value_reducer != NULL`: reduce (`MERGE`).
 *
 * `conflict_mode` can also be used through
 * `stream_collect_turbo_map_with_conflict(...)` for KEEP_FIRST/REJECT/MERGE/
 * KEEP_LAST with/without a reducer.
 *
 * Duplicate keys still count toward `max_items`; `max_items` is the hard final-size
 * limit for distinct keys.
 */
stream_result_t stream_collect_turbo_map_with_conflict(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_turbo_map_conflict_mode_t conflict_mode,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer);

stream_result_t stream_collect_turbo_map(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer);

/*
 * Fluent bootstrap helpers:
 * each call initializes a stream and returns the same stream pointer on success,
 * or NULL on failure.
 */
static inline stream_t *stream_from_turbo_vec_p(stream_t *stream, const turbo_vec_t *vec)
{
    return stream_from_turbo_vec(stream, vec) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_deque_p(
    stream_t *stream,
    const turbo_deque_t *deque)
{
    return stream_from_turbo_deque(stream, deque) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_list_p(
    stream_t *stream,
    const turbo_list_t *list)
{
    return stream_from_turbo_list(stream, list) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_hash_keys_p(
    stream_t *stream,
    const turbo_hash_map_t *map)
{
    return stream_from_turbo_hash_keys(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_hash_values_p(
    stream_t *stream,
    const turbo_hash_map_t *map)
{
    return stream_from_turbo_hash_values(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_map_keys_p(
    stream_t *stream,
    const turbo_map_t *map)
{
    return stream_from_turbo_map_keys(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_map_values_p(
    stream_t *stream,
    const turbo_map_t *map)
{
    return stream_from_turbo_map_values(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_set_p(
    stream_t *stream,
    const turbo_set_t *set)
{
    return stream_from_turbo_set(stream, set) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_multimap_keys_p(
    stream_t *stream,
    const turbo_multimap_t *map)
{
    return stream_from_turbo_multimap_keys(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_multimap_values_p(
    stream_t *stream,
    const turbo_multimap_t *map)
{
    return stream_from_turbo_multimap_values(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_heap_p(
    stream_t *stream,
    const turbo_heap_t *heap)
{
    return stream_from_turbo_heap(stream, heap) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_tree_map_keys_p(
    stream_t *stream,
    const turbo_tree_map_t *map)
{
    return stream_from_turbo_tree_map_keys(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_tree_map_values_p(
    stream_t *stream,
    const turbo_tree_map_t *map)
{
    return stream_from_turbo_tree_map_values(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_bplus_tree_keys_p(
    stream_t *stream,
    const turbo_bplus_tree_t *map)
{
    return stream_from_turbo_bplus_tree_keys(stream, map) == STREAM_OK ? stream : NULL;
}

static inline stream_t *stream_from_turbo_bplus_tree_values_p(
    stream_t *stream,
    const turbo_bplus_tree_t *map)
{
    return stream_from_turbo_bplus_tree_values(stream, map) == STREAM_OK ? stream : NULL;
}

/* Backward-friendly aliases following streamable naming conventions in examples. */
#define STREAM_FROM_TURBO_VEC(stream_ptr, vec_ptr)     stream_from_turbo_vec_p((stream_ptr), (vec_ptr))
#define STREAM_FROM_TURBO_DEQUE(stream_ptr, deque_ptr) stream_from_turbo_deque_p((stream_ptr), (deque_ptr))
#define STREAM_FROM_TURBO_LIST(stream_ptr, list_ptr)   stream_from_turbo_list_p((stream_ptr), (list_ptr))
#define STREAM_FROM_TURBO_HASH_KEYS(stream_ptr, map_ptr) \
    stream_from_turbo_hash_keys_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_HASH_VALUES(stream_ptr, map_ptr) \
    stream_from_turbo_hash_values_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_MAP_KEYS(stream_ptr, map_ptr) \
    stream_from_turbo_map_keys_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_MAP_VALUES(stream_ptr, map_ptr) \
    stream_from_turbo_map_values_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_SET(stream_ptr, set_ptr)     stream_from_turbo_set_p((stream_ptr), (set_ptr))
#define STREAM_FROM_TURBO_MULTIMAP_KEYS(stream_ptr, map_ptr) \
    stream_from_turbo_multimap_keys_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_MULTIMAP_VALUES(stream_ptr, map_ptr) \
    stream_from_turbo_multimap_values_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_HEAP(stream_ptr, heap_ptr)   stream_from_turbo_heap_p((stream_ptr), (heap_ptr))
#define STREAM_FROM_TURBO_TREE_MAP_KEYS(stream_ptr, map_ptr) \
    stream_from_turbo_tree_map_keys_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_TREE_MAP_VALUES(stream_ptr, map_ptr) \
    stream_from_turbo_tree_map_values_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_BPLUS_TREE_KEYS(stream_ptr, map_ptr) \
    stream_from_turbo_bplus_tree_keys_p((stream_ptr), (map_ptr))
#define STREAM_FROM_TURBO_BPLUS_TREE_VALUES(stream_ptr, map_ptr) \
    stream_from_turbo_bplus_tree_values_p((stream_ptr), (map_ptr))

#ifdef __cplusplus
}
#endif

#endif
