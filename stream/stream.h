#ifndef C11_STREAM_H
#define C11_STREAM_H

#if !defined(__cplusplus)
#if defined(__has_include)
#if __has_include(<stdbool.h>)
#include <stdbool.h>
#else
typedef _Bool bool;
#ifndef __bool_true_false_are_defined
#define true ((_Bool)1)
#define false ((_Bool)0)
#define __bool_true_false_are_defined 1
#endif
#endif
#else
#include <stdbool.h>
#endif
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <initializer_list>
#define STREAM_ALIGNAS(Type) alignas(Type)
#else
#define STREAM_ALIGNAS(Type) _Alignas(Type)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_MAX_OPS 32
#define STREAM_MAX_ITEM_SIZE 512
#define STREAM_MAX_SOURCE_CONTEXT_SIZE 256
#define STREAM_MAX_STATE_SIZE 8192

typedef enum {
    STREAM_OK = 0,
    STREAM_END,
    STREAM_AGAIN,
    STREAM_MODIFIED,
    STREAM_ERROR,
    STREAM_FULL
} stream_result_t;

/*
 * Backpressure policy used by live/push-integrated sources.
 * It is kept in stream.h so both stream_live and stream_spsc modules
 * can use a single policy contract.
 */
typedef enum {
    STREAM_BP_REJECT_NEW = 0,
    STREAM_BP_DROP_NEWEST,
    STREAM_BP_DROP_OLDEST,
    STREAM_BP_LATEST_ONLY
} stream_backpressure_policy_t;

typedef enum {
    STREAM_PUSH_OK = 0,
    STREAM_PUSH_DROPPED,
    STREAM_PUSH_FULL,
    STREAM_PUSH_ERROR
} stream_push_result_t;

typedef enum {
    STREAM_ERR_NONE = 0,
    STREAM_ERR_PIPELINE_FULL,
    STREAM_ERR_ITEM_TOO_LARGE,
    STREAM_ERR_BAD_ARGUMENT,
    STREAM_ERR_UNSUPPORTED_SOURCE,
    STREAM_ERR_NOT_RESETTABLE,
    STREAM_ERR_MAP_FAILED,
    STREAM_ERR_SOURCE_MODIFIED,
    STREAM_ERR_SOURCE_FAILED,
    STREAM_ERR_STATE_FULL,
    STREAM_ERR_BAD_OPERATOR_RESULT,
    STREAM_ERR_REDUCE_FAILED,
    STREAM_ERR_COUNT_OVERFLOW,
    STREAM_ERR_DISTINCT_FULL,
    STREAM_ERR_COLLECT_FAILED,
    STREAM_ERR_SORT_FULL,
    STREAM_ERR_FLAT_MAP_FULL,
    STREAM_ERR_FLAT_MAP_FAILED,
    STREAM_ERR_NEEDS_FINITE_SOURCE,
    STREAM_ERR_CONCAT_FULL
} stream_error_t;

typedef struct stream_item {
    void *data;
    size_t size;
    uint64_t timestamp_ns;
    uint64_t sequence;
} stream_item_t;

typedef struct stream_source stream_source_t;
typedef struct stream stream_t;

/* MSVC's C11 mode does not expose max_align_t; mirror TurboUtils containers. */
typedef union stream_max_align {
    long double long_double_value;
    long long long_long_value;
    void *pointer_value;
    size_t size_value;
} stream_max_align_t;

typedef stream_result_t (*stream_source_next_fn)(stream_source_t *self, stream_item_t *out);
typedef stream_result_t (*stream_source_reset_fn)(stream_source_t *self);
typedef void (*stream_source_close_fn)(stream_source_t *self);

struct stream_source {
    void *context;
    size_t element_size;
    stream_source_next_fn next;
    stream_source_reset_fn reset;
    stream_source_close_fn close;
};

typedef bool (*stream_predicate_fn)(const void *value);
typedef stream_result_t (*stream_mapper_fn)(const void *input, void *output);
typedef void (*stream_consumer_fn)(const void *value);
typedef bool (*stream_equal_fn)(const void *a, const void *b);
typedef stream_result_t (*stream_reducer_fn)(void *accumulator, const void *value);
typedef int (*stream_compare_fn)(const void *a, const void *b);
typedef stream_result_t (*stream_unary_operator_fn)(const void *input, void *output);
typedef stream_result_t (*stream_generator_fn)(size_t index, void *output);

typedef struct stream_emitter stream_emitter_t;
typedef stream_result_t (*stream_emit_fn)(stream_emitter_t *self, const void *value);

struct stream_emitter {
    void *context;
    stream_emit_fn emit;
};

typedef stream_result_t (*stream_flat_mapper_fn)(
    const void *input,
    stream_emitter_t *emitter);

/*
 * Sliding window view. data points to stream-owned state storage and remains
 * valid only until the next operation/reset/clear on the same stream.
 */
typedef struct stream_window {
    const void *data;
    size_t count;
    size_t element_size;
    uint64_t first_sequence;
    uint64_t last_sequence;
    uint64_t first_timestamp_ns;
    uint64_t last_timestamp_ns;
} stream_window_t;

typedef enum {
    STREAM_OP_FILTER = 0,
    STREAM_OP_MAP,
    STREAM_OP_TAKE,
    STREAM_OP_SKIP,
    STREAM_OP_WINDOW,
    STREAM_OP_DEBOUNCE,
    STREAM_OP_DEBOUNCE_TIME,
    STREAM_OP_PEEK,
    STREAM_OP_DISTINCT,
    STREAM_OP_TAKE_WHILE,
    STREAM_OP_DROP_WHILE
} stream_op_type_t;

typedef struct stream_operation {
    stream_op_type_t type;

    union {
        struct {
            stream_predicate_fn predicate;
        } filter;

        struct {
            stream_mapper_fn mapper;
            size_t input_size;
            size_t output_size;
        } map;

        struct {
            stream_consumer_fn consumer;
        } peek;

        struct {
            size_t max_unique;
            size_t value_size;
            size_t values_offset;
            size_t count;
            stream_equal_fn equals;
        } distinct;

        struct {
            size_t count;
            size_t seen;
        } take;

        struct {
            stream_predicate_fn predicate;
            bool done;
        } take_while;

        struct {
            stream_predicate_fn predicate;
            bool dropping;
        } drop_while;

        struct {
            size_t count;
            size_t skipped;
        } skip;

        struct {
            size_t count;
            size_t input_size;
            size_t buffer_offset;
            size_t sequence_offset;
            size_t timestamp_offset;
            size_t filled;
        } window;

        struct {
            size_t threshold;
            size_t value_size;
            size_t candidate_offset;
            size_t emitted_offset;
            size_t stable_count;
            bool has_candidate;
            bool has_emitted;
            stream_equal_fn equals;
        } debounce;

        struct {
            uint64_t duration_ns;
            uint64_t candidate_since_ns;
            size_t value_size;
            size_t candidate_offset;
            size_t emitted_offset;
            bool has_candidate;
            bool has_emitted;
            stream_equal_fn equals;
        } debounce_time;
    } as;
} stream_operation_t;

struct stream {
    stream_source_t source;

    stream_operation_t ops[STREAM_MAX_OPS];
    size_t op_count;

    size_t source_element_size;
    size_t current_element_size;

    stream_error_t error;

    STREAM_ALIGNAS(stream_max_align_t) unsigned char source_context[STREAM_MAX_SOURCE_CONTEXT_SIZE];

    STREAM_ALIGNAS(stream_max_align_t) unsigned char scratch_a[STREAM_MAX_ITEM_SIZE];
    STREAM_ALIGNAS(stream_max_align_t) unsigned char scratch_b[STREAM_MAX_ITEM_SIZE];

    /* Stateful operator arena: window/debounce allocate from here. */
    STREAM_ALIGNAS(stream_max_align_t) unsigned char state_storage[STREAM_MAX_STATE_SIZE];
    size_t state_used;

    stream_t *(*filter)(stream_t *self, stream_predicate_fn predicate);
    stream_t *(*map)(stream_t *self, size_t output_size, stream_mapper_fn mapper);
    stream_t *(*take)(stream_t *self, size_t n);
    stream_t *(*skip)(stream_t *self, size_t n);
    stream_t *(*window)(stream_t *self, size_t count);
    stream_t *(*debounce)(stream_t *self, size_t stable_count, stream_equal_fn equals);
    stream_t *(*debounce_ms)(stream_t *self, uint64_t stable_ms, stream_equal_fn equals);

    stream_result_t (*next)(stream_t *self, stream_item_t *out);
    stream_result_t (*next_view)(stream_t *self, stream_item_t *out);
    stream_result_t (*for_each)(stream_t *self, stream_consumer_fn consumer);
    stream_result_t (*reset)(stream_t *self);
    void (*clear)(stream_t *self);
    void (*close)(stream_t *self);

    /* Appended Java-style operations; append-only placement preserves older field offsets. */
    stream_t *(*peek)(stream_t *self, stream_consumer_fn consumer);
    stream_t *(*boxed)(stream_t *self);
    stream_result_t (*count)(stream_t *self, size_t *out_count);
    stream_result_t (*reduce)(stream_t *self, void *accumulator, stream_reducer_fn reducer);
    stream_result_t (*find_first)(stream_t *self, stream_item_t *out);
    stream_result_t (*any_match)(stream_t *self, stream_predicate_fn predicate, bool *out_matches);
    stream_result_t (*all_match)(stream_t *self, stream_predicate_fn predicate, bool *out_matches);
    stream_result_t (*none_match)(stream_t *self, stream_predicate_fn predicate, bool *out_matches);
    stream_result_t (*contains)(stream_t *self, const void *value, stream_equal_fn equals, bool *out_contains);
    stream_t *(*limit)(stream_t *self, size_t n);
    stream_t *(*distinct)(stream_t *self, size_t max_unique, stream_equal_fn equals);
    /* min/max collide with Windows SDK macros at call sites. */
    stream_result_t (*min_value)(
        stream_t *self, stream_compare_fn compare, stream_item_t *out, bool *out_found);
    stream_result_t (*max_value)(
        stream_t *self, stream_compare_fn compare, stream_item_t *out, bool *out_found);
    stream_t *(*take_while)(stream_t *self, stream_predicate_fn predicate);
    stream_t *(*drop_while)(stream_t *self, stream_predicate_fn predicate);
    stream_result_t (*find_any)(stream_t *self, stream_item_t *out);
    stream_result_t (*to_array)(
        stream_t *self,
        void *out_values,
        size_t capacity,
        size_t element_size,
        size_t *out_count);
    stream_t *(*sorted)(stream_t *self, size_t max_items, stream_compare_fn compare);
    stream_t *(*flat_map)(
        stream_t *self,
        size_t output_size,
        size_t max_outputs,
        stream_flat_mapper_fn mapper);
    stream_result_t (*collect)(
        stream_t *self,
        void *result,
        stream_reducer_fn accumulator);
    stream_t *(*concat)(stream_t *self, stream_t *other, size_t max_items);
};

/* Generic source-based initialization. */
stream_result_t stream_init(stream_t *self, const stream_source_t *source);

/*
 * Snapshot an existing stream state into `snapshot` for independent traversal.
 *
 * The snapshot gets a cloned source cursor/state, operator chain, and state storage.
 * It is independent from the original stream for subsequent traversal operations.
 *
 * If the source uses a non-clonable state model (for example, external mutable
 * sources that own opaque state outside stream-owned buffers), this fails with
 * STREAM_ERR_UNSUPPORTED_SOURCE.
 */
stream_result_t stream_snapshot_init(
    stream_t *snapshot,
    const stream_t *source);

/* Built-in finite array source. */
typedef struct {
    const unsigned char *data;
    size_t count;
    size_t element_size;
    size_t pos;
    uint64_t sequence;
} stream_array_source_state_t;

stream_result_t stream_from_array(
    stream_t *self,
    stream_array_source_state_t *source_state,
    const void *data,
    size_t count,
    size_t element_size);

/*
 * Initialize from a stream-owned copy of count values. Values and stateful
 * operators share STREAM_MAX_STATE_SIZE; exhaustion reports
 * STREAM_ERR_STATE_FULL. Pointer-bearing elements retain shallow-copy
 * semantics for their pointed-to payloads.
 */
stream_result_t stream_from_values(
    stream_t *self,
    const void *values,
    size_t count,
    size_t element_size);

/* IntStream.range-style resettable int64_t source with an explicit step. */
stream_result_t stream_range(
    stream_t *self,
    int64_t start,
    int64_t end_exclusive,
    int64_t step);

/* Finite, resettable equivalents of Java iterate/generate sources. */
stream_result_t stream_iterate(
    stream_t *self,
    const void *seed,
    size_t element_size,
    size_t max_items,
    stream_unary_operator_fn next);
stream_result_t stream_generate(
    stream_t *self,
    size_t element_size,
    size_t max_items,
    stream_generator_fn generator);

/* Direct function API, also installed into stream_t function pointers. */
stream_t *stream_filter(stream_t *self, stream_predicate_fn predicate);
stream_t *stream_map(stream_t *self, size_t output_size, stream_mapper_fn mapper);
stream_t *stream_peek(stream_t *self, stream_consumer_fn consumer);
/* Maps each element to a borrowed pointer token (`const void*`) for the current view. */
stream_t *stream_boxed(stream_t *self);
stream_t *stream_take(stream_t *self, size_t n);
/* Java-compatible name for take. */
stream_t *stream_limit(stream_t *self, size_t n);
stream_t *stream_take_while(stream_t *self, stream_predicate_fn predicate);
stream_t *stream_drop_while(stream_t *self, stream_predicate_fn predicate);
stream_t *stream_skip(stream_t *self, size_t n);
/*
 * Eager bounded materialization barriers. Successful calls replace the
 * current pipeline with a resettable stream-owned source. They require finite
 * input and copy values; pointer-bearing values retain shallow semantics.
 */
stream_t *stream_sorted(
    stream_t *self,
    size_t max_items,
    stream_compare_fn compare);
stream_t *stream_flat_map(
    stream_t *self,
    size_t output_size,
    size_t max_outputs,
    stream_flat_mapper_fn mapper);
stream_t *stream_concat(
    stream_t *self,
    stream_t *other,
    size_t max_items);
/*
 * Stable, bounded distinct. It stores at most max_unique copied value slots in
 * stream-owned state. A new value beyond the limit fails with
 * STREAM_ERR_DISTINCT_FULL; pointer-bearing values retain shallow semantics.
 */
stream_t *stream_distinct(
    stream_t *self,
    size_t max_unique,
    stream_equal_fn equals);
stream_t *stream_window(stream_t *self, size_t count);
stream_t *stream_debounce(stream_t *self, size_t stable_count, stream_equal_fn equals);
stream_t *stream_debounce_ms(stream_t *self, uint64_t stable_ms, stream_equal_fn equals);
stream_result_t stream_next(stream_t *self, stream_item_t *out);
/*
 * Zero-copy/view-oriented next. On STREAM_OK, out->data points either to
 * borrowed source storage or to stream-owned scratch/state storage. Treat it
 * as read-only and valid only until the next operation on this stream or a
 * structural mutation of the underlying container.
 */
stream_result_t stream_next_view(stream_t *self, stream_item_t *out);
/*
 * Drains currently available values. For finite sources it normally returns
 * STREAM_END. For live/non-blocking sources it returns STREAM_AGAIN when the
 * source is temporarily empty; it never busy-spins waiting for new data.
 */
stream_result_t stream_for_each(stream_t *self, stream_consumer_fn consumer);
/*
 * Terminal operations drain currently available values. count/reduce return
 * STREAM_END for a completed finite source and STREAM_AGAIN for a temporarily
 * empty live source. Their output contains the work completed by that call.
 */
stream_result_t stream_count(stream_t *self, size_t *out_count);
stream_result_t stream_reduce(
    stream_t *self,
    void *accumulator,
    stream_reducer_fn reducer);
/*
 * find_first returns STREAM_OK when it writes one value, STREAM_END when a
 * finite stream has none, and STREAM_AGAIN when a live stream has none now.
 */
stream_result_t stream_find_first(stream_t *self, stream_item_t *out);
/* Sequential streams make find_any equivalent to find_first. */
stream_result_t stream_find_any(stream_t *self, stream_item_t *out);
/*
 * Match operations return STREAM_OK when they can short-circuit and STREAM_END
 * when finite exhaustion determines the answer. STREAM_AGAIN means the value
 * in out_matches is provisional because a live source may produce more data.
 */
stream_result_t stream_any_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches);
stream_result_t stream_all_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches);
stream_result_t stream_none_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches);
stream_result_t stream_contains(
    stream_t *self,
    const void *value,
    stream_equal_fn equals,
    bool *out_contains);
/*
 * min/max drain currently available values. out uses stream_next capacity
 * semantics. out_found distinguishes an empty finite stream; with
 * STREAM_AGAIN, a found value is provisional for a live source.
 */
stream_result_t stream_min(
    stream_t *self,
    stream_compare_fn compare,
    stream_item_t *out,
    bool *out_found);
stream_result_t stream_max(
    stream_t *self,
    stream_compare_fn compare,
    stream_item_t *out,
    bool *out_found);
/*
 * Copy at most capacity values into caller-owned contiguous storage. A full
 * output returns STREAM_FULL without pulling another item. out_count is the
 * number copied by this call.
 */
stream_result_t stream_to_array(
    stream_t *self,
    void *out_values,
    size_t capacity,
    size_t element_size,
    size_t *out_count);
/* Sequential collect into a caller-owned, caller-initialized result object. */
stream_result_t stream_collect(
    stream_t *self,
    void *result,
    stream_reducer_fn accumulator);
stream_result_t stream_reset(stream_t *self);
void stream_clear(stream_t *self);
void stream_close(stream_t *self);

const char *stream_error_string(stream_error_t error);

/* Optional ergonomic helpers. Core semantics do not depend on these macros. */
#define STREAM_ARRAY_INIT(stream_ptr, state_ptr, array) \
    stream_from_array((stream_ptr), (state_ptr), (array), \
                      sizeof(array) / sizeof((array)[0]), sizeof((array)[0]))

#ifndef __cplusplus
/* Values are evaluated once and copied before this expression returns. */
#define STREAM_OF(stream_ptr, Type, ...) \
    stream_from_values(                    \
        (stream_ptr),                      \
        (Type[]){__VA_ARGS__},             \
        sizeof((Type[]){__VA_ARGS__}) / sizeof(Type), \
        sizeof(Type))
#endif

#define STREAM_EMPTY(stream_ptr, Type) \
    stream_from_values((stream_ptr), NULL, 0, sizeof(Type))

#define STREAM_RANGE(stream_ptr, start, end_exclusive) \
    stream_range((stream_ptr), (int64_t)(start), (int64_t)(end_exclusive), INT64_C(1))

#define STREAM_RANGE_STEP(stream_ptr, start, end_exclusive, step) \
    stream_range(                                                      \
        (stream_ptr),                                                  \
        (int64_t)(start),                                              \
        (int64_t)(end_exclusive),                                      \
        (int64_t)(step))

#ifndef __cplusplus
#define STREAM_ITERATE(stream_ptr, Type, seed, max_items, next_fn) \
    stream_iterate(                                                   \
        (stream_ptr), &(Type){(seed)}, sizeof(Type), (max_items), (next_fn))
#endif

#define STREAM_GENERATE(stream_ptr, Type, max_items, generator_fn) \
    stream_generate(                                                 \
        (stream_ptr), sizeof(Type), (max_items), (generator_fn))

#define STREAM_TO_ARRAY(stream_ptr, array, out_count_ptr) \
    stream_to_array(                                      \
        (stream_ptr),                                     \
        (array),                                          \
        sizeof(array) / sizeof((array)[0]),               \
        sizeof((array)[0]),                               \
        (out_count_ptr))

#define STREAM_MAP_TO(stream_ptr, Type, mapper_fn) \
    ((stream_ptr)->map((stream_ptr), sizeof(Type), (mapper_fn)))

#ifdef __cplusplus
}

template <typename T>
static inline stream_result_t stream_of(
    stream_t *self,
    std::initializer_list<T> values)
{
    return stream_from_values(self, values.begin(), values.size(), sizeof(T));
}

template <typename T>
static inline stream_result_t stream_iterate_of(
    stream_t *self,
    T seed,
    size_t max_items,
    stream_unary_operator_fn next)
{
    return stream_iterate(self, &seed, sizeof(T), max_items, next);
}

#define STREAM_OF(stream_ptr, Type, ...) \
    stream_of<Type>((stream_ptr), {__VA_ARGS__})

#define STREAM_ITERATE(stream_ptr, Type, seed, max_items, next_fn) \
    stream_iterate_of<Type>((stream_ptr), (seed), (max_items), (next_fn))
#endif

#undef STREAM_ALIGNAS

#endif
