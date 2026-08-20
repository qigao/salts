#include "stream.h"

#include <string.h>

_Static_assert(STREAM_MAX_OPS > 0, "STREAM_MAX_OPS must be positive");
_Static_assert(STREAM_MAX_ITEM_SIZE > 0, "STREAM_MAX_ITEM_SIZE must be positive");
_Static_assert(STREAM_MAX_SOURCE_CONTEXT_SIZE > 0, "STREAM_MAX_SOURCE_CONTEXT_SIZE must be positive");
_Static_assert(STREAM_MAX_STATE_SIZE > 0, "STREAM_MAX_STATE_SIZE must be positive");
_Static_assert(sizeof(stream_array_source_state_t) <= STREAM_MAX_SOURCE_CONTEXT_SIZE,
               "STREAM_MAX_SOURCE_CONTEXT_SIZE too small for values source");

typedef struct {
    int64_t start;
    int64_t current;
    int64_t end_exclusive;
    int64_t step;
    uint64_t sequence;
    bool done;
} stream_range_source_state_t;

typedef struct {
    unsigned char *storage;
    size_t element_size;
    size_t max_items;
    size_t emitted;
    stream_unary_operator_fn next;
} stream_iterate_source_state_t;

typedef struct {
    size_t element_size;
    size_t max_items;
    size_t index;
    stream_generator_fn generator;
} stream_generate_source_state_t;

_Static_assert(sizeof(stream_range_source_state_t) <= STREAM_MAX_SOURCE_CONTEXT_SIZE,
               "STREAM_MAX_SOURCE_CONTEXT_SIZE too small for range source");
_Static_assert(sizeof(stream_iterate_source_state_t) <= STREAM_MAX_SOURCE_CONTEXT_SIZE,
               "STREAM_MAX_SOURCE_CONTEXT_SIZE too small for iterate source");
_Static_assert(sizeof(stream_generate_source_state_t) <= STREAM_MAX_SOURCE_CONTEXT_SIZE,
               "STREAM_MAX_SOURCE_CONTEXT_SIZE too small for generate source");

typedef enum {
    PIPELINE_PASS = 0,
    PIPELINE_DROP,
    PIPELINE_STOP,
    PIPELINE_ERROR
} pipeline_flow_t;

static stream_result_t array_source_next(stream_source_t *source, stream_item_t *out)
{
    stream_array_source_state_t *s;

    if (!source || !out || !out->data) {
        return STREAM_ERROR;
    }

    s = (stream_array_source_state_t *)source->context;
    if (!s) {
        return STREAM_ERROR;
    }

    if (s->pos >= s->count) {
        return STREAM_END;
    }

    memcpy(out->data,
           s->data + (s->pos * s->element_size),
           s->element_size);

    out->size = s->element_size;
    out->timestamp_ns = 0;
    out->sequence = s->sequence++;

    ++s->pos;
    return STREAM_OK;
}

static stream_result_t array_source_reset(stream_source_t *source)
{
    stream_array_source_state_t *s;

    if (!source || !source->context) {
        return STREAM_ERROR;
    }

    s = (stream_array_source_state_t *)source->context;
    s->pos = 0;
    s->sequence = 0;
    return STREAM_OK;
}

static void array_source_close(stream_source_t *source)
{
    (void)source;
}

static bool range_has_value(const stream_range_source_state_t *state)
{
    return state->step > 0
        ? state->current < state->end_exclusive
        : state->current > state->end_exclusive;
}

static uint64_t negative_step_magnitude(int64_t step)
{
    return (uint64_t)(-(step + 1)) + UINT64_C(1);
}

static stream_result_t range_source_next(stream_source_t *source, stream_item_t *out)
{
    stream_range_source_state_t *state;
    int64_t value;
    uint64_t remaining;
    uint64_t magnitude;

    if (!source || !source->context || !out || !out->data) {
        return STREAM_ERROR;
    }

    state = (stream_range_source_state_t *)source->context;
    if (state->done || !range_has_value(state)) {
        state->done = true;
        return STREAM_END;
    }

    value = state->current;
    memcpy(out->data, &value, sizeof(value));
    out->size = sizeof(value);
    out->timestamp_ns = 0;
    out->sequence = state->sequence++;

    if (state->step > 0) {
        remaining = (uint64_t)state->end_exclusive - (uint64_t)state->current;
        magnitude = (uint64_t)state->step;
    } else {
        remaining = (uint64_t)state->current - (uint64_t)state->end_exclusive;
        magnitude = negative_step_magnitude(state->step);
    }

    if (magnitude >= remaining) {
        state->done = true;
    } else {
        state->current += state->step;
    }
    return STREAM_OK;
}

static stream_result_t range_source_reset(stream_source_t *source)
{
    stream_range_source_state_t *state;

    if (!source || !source->context) {
        return STREAM_ERROR;
    }

    state = (stream_range_source_state_t *)source->context;
    state->current = state->start;
    state->sequence = 0;
    state->done = !range_has_value(state);
    return STREAM_OK;
}

static stream_result_t iterate_source_next(stream_source_t *source, stream_item_t *out)
{
    stream_iterate_source_state_t *state;
    _Alignas(stream_max_align_t) unsigned char next_value[STREAM_MAX_ITEM_SIZE];
    unsigned char *current;
    stream_result_t r;

    if (!source || !source->context || !out || !out->data) {
        return STREAM_ERROR;
    }

    state = (stream_iterate_source_state_t *)source->context;
    if (state->emitted >= state->max_items) {
        return STREAM_END;
    }

    current = state->storage + state->element_size;
    if (state->emitted > 0) {
        r = state->next(current, next_value);
        if (r != STREAM_OK) {
            return STREAM_ERROR;
        }
        memcpy(current, next_value, state->element_size);
    }

    memcpy(out->data, current, state->element_size);
    out->size = state->element_size;
    out->timestamp_ns = 0;
    out->sequence = state->emitted++;
    return STREAM_OK;
}

static stream_result_t iterate_source_reset(stream_source_t *source)
{
    stream_iterate_source_state_t *state;

    if (!source || !source->context) {
        return STREAM_ERROR;
    }

    state = (stream_iterate_source_state_t *)source->context;
    memcpy(
        state->storage + state->element_size,
        state->storage,
        state->element_size);
    state->emitted = 0;
    return STREAM_OK;
}

static stream_result_t generate_source_next(stream_source_t *source, stream_item_t *out)
{
    stream_generate_source_state_t *state;
    stream_result_t r;

    if (!source || !source->context || !out || !out->data) {
        return STREAM_ERROR;
    }

    state = (stream_generate_source_state_t *)source->context;
    if (state->index >= state->max_items) {
        return STREAM_END;
    }

    r = state->generator(state->index, out->data);
    if (r != STREAM_OK) {
        return STREAM_ERROR;
    }
    out->size = state->element_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static stream_result_t generate_source_reset(stream_source_t *source)
{
    stream_generate_source_state_t *state;

    if (!source || !source->context) {
        return STREAM_ERROR;
    }

    state = (stream_generate_source_state_t *)source->context;
    state->index = 0;
    return STREAM_OK;
}

static size_t source_owned_state_size(const stream_t *self)
{
    const stream_array_source_state_t *state;
    const stream_iterate_source_state_t *iterate_state;

    if (!self || self->source.context != (const void *)self->source_context) {
        return 0;
    }

    if (self->source.next == iterate_source_next) {
        iterate_state = (const stream_iterate_source_state_t *)self->source.context;
        if (iterate_state->storage != self->state_storage ||
            iterate_state->element_size > STREAM_MAX_STATE_SIZE / 2U) {
            return 0;
        }
        return iterate_state->element_size * 2U;
    }

    if (self->source.next != array_source_next) {
        return 0;
    }

    state = (const stream_array_source_state_t *)self->source.context;
    if (state->data != self->state_storage || state->element_size == 0 ||
        state->count > SIZE_MAX / state->element_size) {
        return 0;
    }
    if (state->count * state->element_size > STREAM_MAX_STATE_SIZE) {
        return 0;
    }
    return state->count * state->element_size;
}

static stream_error_t stream_snapshot_copy_source_state(
    const stream_t *source,
    stream_t *snapshot)
{
    const stream_array_source_state_t *array_state;
    const stream_iterate_source_state_t *iterate_state;
    stream_array_source_state_t *snapshot_array_state;
    stream_iterate_source_state_t *snapshot_iterate_state;

    if (!source || !snapshot || !source->source.next || !source->source.context) {
        return STREAM_ERR_BAD_ARGUMENT;
    }

    if (source->source.context == source->source_context) {
        memcpy(snapshot->source_context, source->source_context, sizeof(snapshot->source_context));
        snapshot->source.context = snapshot->source_context;

        if (source->source.next == iterate_source_next) {
            iterate_state = (const stream_iterate_source_state_t *)source->source.context;
            if (iterate_state->storage != source->state_storage ||
                iterate_state->element_size > STREAM_MAX_STATE_SIZE / 2U) {
                return STREAM_ERR_BAD_ARGUMENT;
            }
            snapshot_iterate_state = (stream_iterate_source_state_t *)snapshot->source.context;
            snapshot_iterate_state->storage = snapshot->state_storage;
        } else if (source->source.next == array_source_next) {
            array_state = (const stream_array_source_state_t *)source->source.context;
            snapshot_array_state = (stream_array_source_state_t *)snapshot->source.context;
            if (array_state->data == source->state_storage) {
                snapshot_array_state->data = snapshot->state_storage;
            }
        }
        return STREAM_ERR_NONE;
    }

    if (source->source.next != array_source_next &&
        source->source.next != iterate_source_next) {
        return STREAM_ERR_UNSUPPORTED_SOURCE;
    }

    if (source->source.next == array_source_next) {
        array_state = (const stream_array_source_state_t *)source->source.context;
        if (!array_state || array_state->element_size == 0 ||
            (array_state->count != 0 && array_state->data == NULL) ||
            array_state->count > SIZE_MAX / array_state->element_size) {
            return STREAM_ERR_BAD_ARGUMENT;
        }
        snapshot_array_state = (stream_array_source_state_t *)snapshot->source_context;
        memcpy(snapshot_array_state, array_state, sizeof(*snapshot_array_state));
        if (array_state->data == source->state_storage) {
            snapshot_array_state->data = snapshot->state_storage;
        }
        snapshot->source.context = snapshot_array_state;
        return STREAM_ERR_NONE;
    }

    iterate_state = (const stream_iterate_source_state_t *)source->source.context;
    if (!iterate_state || iterate_state->storage != source->state_storage ||
        iterate_state->element_size > STREAM_MAX_STATE_SIZE / 2U) {
        return STREAM_ERR_UNSUPPORTED_SOURCE;
    }
    snapshot_iterate_state = (stream_iterate_source_state_t *)snapshot->source_context;
    memcpy(snapshot_iterate_state, iterate_state, sizeof(*snapshot_iterate_state));
    snapshot_iterate_state->storage = snapshot->state_storage;
    snapshot->source.context = snapshot_iterate_state;
    return STREAM_ERR_NONE;
}

static bool append_op(stream_t *self, stream_operation_t **out)
{
    if (!self || !out) {
        return false;
    }

    if (self->op_count >= STREAM_MAX_OPS) {
        self->error = STREAM_ERR_PIPELINE_FULL;
        return false;
    }

    *out = &self->ops[self->op_count++];
    memset(*out, 0, sizeof(**out));
    return true;
}

static bool reserve_state(stream_t *self, size_t size, size_t *offset)
{
    size_t align = _Alignof(stream_max_align_t);
    size_t start;

    if (!self || !offset || size == 0) {
        return false;
    }

    start = (self->state_used + align - 1u) / align * align;
    if (start > STREAM_MAX_STATE_SIZE || size > STREAM_MAX_STATE_SIZE - start) {
        self->error = STREAM_ERR_STATE_FULL;
        return false;
    }

    *offset = start;
    memset(self->state_storage + start, 0, size);
    self->state_used = start + size;
    return true;
}

static bool current_value_is_borrowed_window(const stream_t *self)
{
    size_t i;
    bool borrowed_window = false;

    for (i = 0; i < self->op_count; ++i) {
        if (self->ops[i].type == STREAM_OP_WINDOW) {
            borrowed_window = true;
        } else if (self->ops[i].type == STREAM_OP_MAP) {
            /* Mapper output is a new value copied into stream scratch storage. */
            borrowed_window = false;
        }
    }

    return borrowed_window;
}

stream_result_t stream_init(stream_t *self, const stream_source_t *source)
{
    if (!self || !source || !source->next || source->element_size == 0) {
        return STREAM_ERROR;
    }

    if (source->element_size > STREAM_MAX_ITEM_SIZE) {
        return STREAM_ERROR;
    }

    memset(self, 0, sizeof(*self));
    self->source = *source;
    self->source_element_size = source->element_size;
    self->current_element_size = source->element_size;
    self->error = STREAM_ERR_NONE;

    self->filter = stream_filter;
    self->map = stream_map;
    self->take = stream_take;
    self->skip = stream_skip;
    self->window = stream_window;
    self->debounce = stream_debounce;
    self->debounce_ms = stream_debounce_ms;
    self->next = stream_next;
    self->next_view = stream_next_view;
    self->for_each = stream_for_each;
    self->reset = stream_reset;
    self->clear = stream_clear;
    self->close = stream_close;
    self->peek = stream_peek;
    self->boxed = stream_boxed;
    self->count = stream_count;
    self->reduce = stream_reduce;
    self->find_first = stream_find_first;
    self->any_match = stream_any_match;
    self->all_match = stream_all_match;
    self->none_match = stream_none_match;
    self->contains = stream_contains;
    self->limit = stream_limit;
    self->distinct = stream_distinct;
    self->min_value = stream_min;
    self->max_value = stream_max;
    self->take_while = stream_take_while;
    self->drop_while = stream_drop_while;
    self->find_any = stream_find_any;
    self->to_array = stream_to_array;
    self->sorted = stream_sorted;
    self->flat_map = stream_flat_map;
    self->collect = stream_collect;
    self->concat = stream_concat;

    return STREAM_OK;
}

stream_result_t stream_snapshot_init(stream_t *snapshot, const stream_t *source)
{
    stream_error_t snapshot_error;

    if (!snapshot || !source || snapshot == source) {
        if (snapshot) {
            snapshot->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    if (source->error != STREAM_ERR_NONE) {
        snapshot->error = source->error;
        return STREAM_ERROR;
    }

    memcpy(snapshot, source, sizeof(*snapshot));
    snapshot->error = STREAM_ERR_NONE;

    snapshot_error = stream_snapshot_copy_source_state(source, snapshot);
    if (snapshot_error != STREAM_ERR_NONE) {
        snapshot->error = snapshot_error;
        return STREAM_ERROR;
    }

    return STREAM_OK;
}

stream_result_t stream_from_array(
    stream_t *self,
    stream_array_source_state_t *source_state,
    const void *data,
    size_t count,
    size_t element_size)
{
    stream_source_t source;

    if (!self || !source_state || (!data && count != 0) || element_size == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    if (element_size > STREAM_MAX_ITEM_SIZE) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return STREAM_ERROR;
    }

    if (count > SIZE_MAX / element_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    source_state->data = (const unsigned char *)data;
    source_state->count = count;
    source_state->element_size = element_size;
    source_state->pos = 0;
    source_state->sequence = 0;

    source.context = source_state;
    source.element_size = element_size;
    source.next = array_source_next;
    source.reset = array_source_reset;
    source.close = array_source_close;

    return stream_init(self, &source);
}

stream_result_t stream_from_values(
    stream_t *self,
    const void *values,
    size_t count,
    size_t element_size)
{
    stream_source_t source;
    stream_array_source_state_t *source_state;
    size_t values_size;
    stream_result_t r;

    if (!self || (!values && count != 0) || element_size == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }
    if (element_size > STREAM_MAX_ITEM_SIZE) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return STREAM_ERROR;
    }
    if (count > SIZE_MAX / element_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    values_size = count * element_size;
    if (values_size > STREAM_MAX_STATE_SIZE) {
        self->error = STREAM_ERR_STATE_FULL;
        return STREAM_ERROR;
    }

    source.context = NULL;
    source.element_size = element_size;
    source.next = array_source_next;
    source.reset = array_source_reset;
    source.close = array_source_close;

    r = stream_init(self, &source);
    if (r != STREAM_OK) {
        return r;
    }

    if (values_size > 0) {
        memcpy(self->state_storage, values, values_size);
    }
    self->state_used = values_size;

    source_state = (stream_array_source_state_t *)self->source_context;
    source_state->data = values_size > 0 ? self->state_storage : NULL;
    source_state->count = count;
    source_state->element_size = element_size;
    source_state->pos = 0;
    source_state->sequence = 0;
    self->source.context = source_state;
    return STREAM_OK;
}

stream_result_t stream_range(
    stream_t *self,
    int64_t start,
    int64_t end_exclusive,
    int64_t step)
{
    stream_source_t source;
    stream_range_source_state_t *state;
    stream_result_t r;

    if (!self || step == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    source.context = NULL;
    source.element_size = sizeof(int64_t);
    source.next = range_source_next;
    source.reset = range_source_reset;
    source.close = array_source_close;

    r = stream_init(self, &source);
    if (r != STREAM_OK) {
        return r;
    }

    state = (stream_range_source_state_t *)self->source_context;
    state->start = start;
    state->current = start;
    state->end_exclusive = end_exclusive;
    state->step = step;
    state->sequence = 0;
    state->done = !range_has_value(state);
    self->source.context = state;
    return STREAM_OK;
}

stream_result_t stream_iterate(
    stream_t *self,
    const void *seed,
    size_t element_size,
    size_t max_items,
    stream_unary_operator_fn next)
{
    stream_source_t source;
    stream_iterate_source_state_t *state;
    stream_result_t r;

    if (!self || !seed || !next || element_size == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }
    if (element_size > STREAM_MAX_ITEM_SIZE) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return STREAM_ERROR;
    }
    if (element_size > STREAM_MAX_STATE_SIZE / 2U) {
        self->error = STREAM_ERR_STATE_FULL;
        return STREAM_ERROR;
    }

    source.context = NULL;
    source.element_size = element_size;
    source.next = iterate_source_next;
    source.reset = iterate_source_reset;
    source.close = array_source_close;
    r = stream_init(self, &source);
    if (r != STREAM_OK) {
        return r;
    }

    memcpy(self->state_storage, seed, element_size);
    memcpy(self->state_storage + element_size, seed, element_size);
    self->state_used = element_size * 2U;

    state = (stream_iterate_source_state_t *)self->source_context;
    state->storage = self->state_storage;
    state->element_size = element_size;
    state->max_items = max_items;
    state->emitted = 0;
    state->next = next;
    self->source.context = state;
    return STREAM_OK;
}

stream_result_t stream_generate(
    stream_t *self,
    size_t element_size,
    size_t max_items,
    stream_generator_fn generator)
{
    stream_source_t source;
    stream_generate_source_state_t *state;
    stream_result_t r;

    if (!self || !generator || element_size == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }
    if (element_size > STREAM_MAX_ITEM_SIZE) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return STREAM_ERROR;
    }

    source.context = NULL;
    source.element_size = element_size;
    source.next = generate_source_next;
    source.reset = generate_source_reset;
    source.close = array_source_close;
    r = stream_init(self, &source);
    if (r != STREAM_OK) {
        return r;
    }

    state = (stream_generate_source_state_t *)self->source_context;
    state->element_size = element_size;
    state->max_items = max_items;
    state->index = 0;
    state->generator = generator;
    self->source.context = state;
    return STREAM_OK;
}

stream_t *stream_filter(stream_t *self, stream_predicate_fn predicate)
{
    stream_operation_t *op;

    if (!self || !predicate) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_FILTER;
    op->as.filter.predicate = predicate;
    return self;
}

stream_t *stream_map(stream_t *self, size_t output_size, stream_mapper_fn mapper)
{
    stream_operation_t *op;

    if (!self || !mapper || output_size == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    if (output_size > STREAM_MAX_ITEM_SIZE) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return self;
    }

    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_MAP;
    op->as.map.mapper = mapper;
    op->as.map.input_size = self->current_element_size;
    op->as.map.output_size = output_size;
    self->current_element_size = output_size;
    return self;
}

stream_t *stream_peek(stream_t *self, stream_consumer_fn consumer)
{
    stream_operation_t *op;

    if (!self || !consumer) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_PEEK;
    op->as.peek.consumer = consumer;
    return self;
}

static stream_result_t boxed_mapper(const void *input, void *output)
{
    *(const void **)output = input;
    return STREAM_OK;
}

stream_t *stream_boxed(stream_t *self)
{
    return stream_map(self, sizeof(const void *), boxed_mapper);
}

stream_t *stream_take(stream_t *self, size_t n)
{
    stream_operation_t *op;

    if (!self) {
        return NULL;
    }

    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_TAKE;
    op->as.take.count = n;
    op->as.take.seen = 0;
    return self;
}

stream_t *stream_limit(stream_t *self, size_t n)
{
    return stream_take(self, n);
}

stream_t *stream_take_while(stream_t *self, stream_predicate_fn predicate)
{
    stream_operation_t *op;

    if (!self || !predicate) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }
    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_TAKE_WHILE;
    op->as.take_while.predicate = predicate;
    return self;
}

stream_t *stream_drop_while(stream_t *self, stream_predicate_fn predicate)
{
    stream_operation_t *op;

    if (!self || !predicate) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }
    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_DROP_WHILE;
    op->as.drop_while.predicate = predicate;
    op->as.drop_while.dropping = true;
    return self;
}

stream_t *stream_skip(stream_t *self, size_t n)
{
    stream_operation_t *op;

    if (!self) {
        return NULL;
    }

    if (!append_op(self, &op)) {
        return self;
    }

    op->type = STREAM_OP_SKIP;
    op->as.skip.count = n;
    op->as.skip.skipped = 0;
    return self;
}

stream_t *stream_distinct(
    stream_t *self,
    size_t max_unique,
    stream_equal_fn equals)
{
    stream_operation_t *op;
    size_t state_mark;
    size_t values_offset;
    size_t value_size;

    if (!self || max_unique == 0 || !equals ||
        self->current_element_size == 0 || current_value_is_borrowed_window(self)) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    value_size = self->current_element_size;
    if (max_unique > STREAM_MAX_STATE_SIZE / value_size) {
        self->error = STREAM_ERR_STATE_FULL;
        return self;
    }

    state_mark = self->state_used;
    if (!reserve_state(self, max_unique * value_size, &values_offset)) {
        return self;
    }
    if (!append_op(self, &op)) {
        self->state_used = state_mark;
        return self;
    }

    op->type = STREAM_OP_DISTINCT;
    op->as.distinct.max_unique = max_unique;
    op->as.distinct.value_size = value_size;
    op->as.distinct.values_offset = values_offset;
    op->as.distinct.equals = equals;
    return self;
}

stream_t *stream_window(stream_t *self, size_t count)
{
    stream_operation_t *op;
    size_t bytes;
    size_t offset;
    size_t sequence_offset;
    size_t timestamp_offset;

    if (!self || count == 0 || self->current_element_size == 0 ||
        current_value_is_borrowed_window(self)) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    if (count > STREAM_MAX_STATE_SIZE / self->current_element_size) {
        self->error = STREAM_ERR_STATE_FULL;
        return self;
    }
    bytes = count * self->current_element_size;

    if (!reserve_state(self, bytes, &offset)) {
        return self;
    }
    if (!reserve_state(self, count * sizeof(uint64_t), &sequence_offset)) {
        self->state_used = offset;
        return self;
    }
    if (!reserve_state(self, count * sizeof(uint64_t), &timestamp_offset)) {
        self->state_used = offset;
        return self;
    }

    if (!append_op(self, &op)) {
        self->state_used = offset;
        return self;
    }

    op->type = STREAM_OP_WINDOW;
    op->as.window.count = count;
    op->as.window.input_size = self->current_element_size;
    op->as.window.buffer_offset = offset;
    op->as.window.sequence_offset = sequence_offset;
    op->as.window.timestamp_offset = timestamp_offset;
    op->as.window.filled = 0;
    self->current_element_size = sizeof(stream_window_t);
    return self;
}

stream_t *stream_debounce(stream_t *self, size_t stable_count, stream_equal_fn equals)
{
    stream_operation_t *op;
    size_t candidate_offset;
    size_t emitted_offset;
    size_t size;

    if (!self || stable_count == 0 || !equals || self->current_element_size == 0 ||
        current_value_is_borrowed_window(self)) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    size = self->current_element_size;
    if (!reserve_state(self, size, &candidate_offset)) {
        return self;
    }
    if (!reserve_state(self, size, &emitted_offset)) {
        self->state_used = candidate_offset;
        return self;
    }

    if (!append_op(self, &op)) {
        self->state_used = candidate_offset;
        return self;
    }

    op->type = STREAM_OP_DEBOUNCE;
    op->as.debounce.threshold = stable_count;
    op->as.debounce.value_size = size;
    op->as.debounce.candidate_offset = candidate_offset;
    op->as.debounce.emitted_offset = emitted_offset;
    op->as.debounce.equals = equals;
    return self;
}


stream_t *stream_debounce_ms(stream_t *self, uint64_t stable_ms, stream_equal_fn equals)
{
    stream_operation_t *op;
    size_t candidate_offset;
    size_t emitted_offset;
    size_t size;

    if (!self || !equals || self->current_element_size == 0 ||
        current_value_is_borrowed_window(self) ||
        stable_ms > UINT64_MAX / UINT64_C(1000000)) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }

    size = self->current_element_size;
    if (!reserve_state(self, size, &candidate_offset)) {
        return self;
    }
    if (!reserve_state(self, size, &emitted_offset)) {
        self->state_used = candidate_offset;
        return self;
    }

    if (!append_op(self, &op)) {
        self->state_used = candidate_offset;
        return self;
    }

    op->type = STREAM_OP_DEBOUNCE_TIME;
    op->as.debounce_time.duration_ns = stable_ms * UINT64_C(1000000);
    op->as.debounce_time.value_size = size;
    op->as.debounce_time.candidate_offset = candidate_offset;
    op->as.debounce_time.emitted_offset = emitted_offset;
    op->as.debounce_time.equals = equals;
    return self;
}

static void replace_with_owned_values(
    stream_t *self,
    size_t values_offset,
    size_t count,
    size_t element_size)
{
    stream_source_close_fn close = self->source.close;
    stream_array_source_state_t *state;
    size_t values_size = count * element_size;

    if (values_size > 0 && values_offset != 0) {
        memmove(
            self->state_storage,
            self->state_storage + values_offset,
            values_size);
    }

    self->source.close = NULL;
    if (close) {
        close(&self->source);
    }

    memset(self->source_context, 0, sizeof(self->source_context));
    state = (stream_array_source_state_t *)self->source_context;
    state->data = count > 0 ? self->state_storage : NULL;
    state->count = count;
    state->element_size = element_size;
    state->pos = 0;
    state->sequence = 0;

    self->source.context = state;
    self->source.element_size = element_size;
    self->source.next = array_source_next;
    self->source.reset = array_source_reset;
    self->source.close = array_source_close;
    self->source_element_size = element_size;
    self->current_element_size = element_size;
    self->op_count = 0;
    self->state_used = values_size;
    if (values_size < sizeof(self->state_storage)) {
        memset(
            self->state_storage + values_size,
            0,
            sizeof(self->state_storage) - values_size);
    }
    self->error = STREAM_ERR_NONE;
}

static void stable_merge_sort(
    unsigned char *values,
    unsigned char *workspace,
    size_t count,
    size_t element_size,
    stream_compare_fn compare)
{
    unsigned char *source = values;
    unsigned char *destination = workspace;
    size_t width;

    for (width = 1; width < count;) {
        size_t left;

        for (left = 0; left < count;) {
            size_t middle = left + (count - left < width ? count - left : width);
            size_t right = middle + (count - middle < width ? count - middle : width);
            size_t i = left;
            size_t j = middle;
            size_t out = left;

            while (i < middle && j < right) {
                const unsigned char *left_value = source + i * element_size;
                const unsigned char *right_value = source + j * element_size;

                if (compare(left_value, right_value) <= 0) {
                    memcpy(destination + out * element_size, left_value, element_size);
                    ++i;
                } else {
                    memcpy(destination + out * element_size, right_value, element_size);
                    ++j;
                }
                ++out;
            }
            while (i < middle) {
                memcpy(
                    destination + out * element_size,
                    source + i * element_size,
                    element_size);
                ++i;
                ++out;
            }
            while (j < right) {
                memcpy(
                    destination + out * element_size,
                    source + j * element_size,
                    element_size);
                ++j;
                ++out;
            }
            left = right;
        }

        {
            unsigned char *swap = source;
            source = destination;
            destination = swap;
        }
        if (width > count / 2U) {
            width = count;
        } else {
            width *= 2U;
        }
    }

    if (source != values) {
        memcpy(values, source, count * element_size);
    }
}

static stream_result_t append_materialized_values(
    stream_t *source,
    unsigned char *destination,
    size_t element_size,
    size_t max_items,
    size_t *io_count)
{
    stream_item_t item;

    for (;;) {
        stream_result_t r;

        if (*io_count == max_items) {
            return STREAM_FULL;
        }

        r = source->next_view(source, &item);
        if (r != STREAM_OK) {
            return r;
        }
        if (item.size != element_size) {
            source->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }

        memmove(
            destination + *io_count * element_size,
            item.data,
            element_size);
        ++*io_count;
    }
}

stream_t *stream_sorted(
    stream_t *self,
    size_t max_items,
    stream_compare_fn compare)
{
    stream_item_t item;
    size_t previous_state_used;
    size_t values_offset = 0;
    size_t workspace_offset = 0;
    size_t values_size;
    size_t count = 0;

    if (!self || !compare || max_items == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }
    if (self->error != STREAM_ERR_NONE) {
        return self;
    }
    if (max_items > SIZE_MAX / self->current_element_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return self;
    }

    values_size = max_items * self->current_element_size;
    previous_state_used = self->state_used;
    if (!reserve_state(self, values_size, &values_offset) ||
        !reserve_state(self, values_size, &workspace_offset)) {
        self->state_used = previous_state_used;
        return self;
    }

    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r == STREAM_END) {
            break;
        }
        if (r == STREAM_AGAIN) {
            self->error = STREAM_ERR_NEEDS_FINITE_SOURCE;
            self->state_used = previous_state_used;
            return self;
        }
        if (r != STREAM_OK) {
            self->state_used = previous_state_used;
            return self;
        }
        if (count == max_items) {
            self->error = STREAM_ERR_SORT_FULL;
            self->state_used = previous_state_used;
            return self;
        }
        memcpy(
            self->state_storage + values_offset + count * self->current_element_size,
            item.data,
            self->current_element_size);
        ++count;
    }

    stable_merge_sort(
        self->state_storage + values_offset,
        self->state_storage + workspace_offset,
        count,
        self->current_element_size,
        compare);
    replace_with_owned_values(
        self,
        values_offset,
        count,
        self->current_element_size);
    return self;
}

typedef struct {
    stream_t *stream;
    size_t values_offset;
    size_t output_size;
    size_t max_outputs;
    size_t count;
} stream_flat_map_emitter_state_t;

static stream_result_t flat_map_emit(stream_emitter_t *emitter, const void *value)
{
    stream_flat_map_emitter_state_t *state;

    if (!emitter || !emitter->context || !value) {
        return STREAM_ERROR;
    }

    state = (stream_flat_map_emitter_state_t *)emitter->context;
    if (state->count == state->max_outputs) {
        state->stream->error = STREAM_ERR_FLAT_MAP_FULL;
        return STREAM_FULL;
    }

    memmove(
        state->stream->state_storage +
            state->values_offset + state->count * state->output_size,
        value,
        state->output_size);
    ++state->count;
    return STREAM_OK;
}

stream_t *stream_flat_map(
    stream_t *self,
    size_t output_size,
    size_t max_outputs,
    stream_flat_mapper_fn mapper)
{
    stream_flat_map_emitter_state_t emitter_state;
    stream_emitter_t emitter;
    stream_item_t item;
    size_t previous_state_used;
    size_t values_offset = 0;
    size_t values_size;

    if (!self || !mapper || output_size == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }
    if (self->error != STREAM_ERR_NONE) {
        return self;
    }
    if (output_size > STREAM_MAX_ITEM_SIZE) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return self;
    }
    if (max_outputs > SIZE_MAX / output_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return self;
    }

    values_size = max_outputs * output_size;
    previous_state_used = self->state_used;
    if (values_size > 0 && !reserve_state(self, values_size, &values_offset)) {
        return self;
    }

    emitter_state.stream = self;
    emitter_state.values_offset = values_offset;
    emitter_state.output_size = output_size;
    emitter_state.max_outputs = max_outputs;
    emitter_state.count = 0;
    emitter.context = &emitter_state;
    emitter.emit = flat_map_emit;

    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r == STREAM_END) {
            break;
        }
        if (r == STREAM_AGAIN) {
            self->error = STREAM_ERR_NEEDS_FINITE_SOURCE;
            self->state_used = previous_state_used;
            return self;
        }
        if (r != STREAM_OK) {
            self->state_used = previous_state_used;
            return self;
        }

        r = mapper(item.data, &emitter);
        if (r != STREAM_OK) {
            if (self->error == STREAM_ERR_NONE) {
                self->error = (r == STREAM_ERROR)
                    ? STREAM_ERR_FLAT_MAP_FAILED
                    : STREAM_ERR_BAD_OPERATOR_RESULT;
            }
            self->state_used = previous_state_used;
            return self;
        }
        if (self->error != STREAM_ERR_NONE) {
            self->state_used = previous_state_used;
            return self;
        }
    }

    replace_with_owned_values(
        self,
        values_offset,
        emitter_state.count,
        output_size);
    return self;
}

stream_t *stream_concat(
    stream_t *self,
    stream_t *other,
    size_t max_items)
{
    size_t previous_state_used;
    size_t values_offset = 0;
    size_t element_size;
    size_t values_size;
    size_t count = 0;
    stream_result_t r;

    if (!self || !other || self == other || max_items == 0) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return self;
    }
    if (self->error != STREAM_ERR_NONE) {
        return self;
    }
    if (other->error != STREAM_ERR_NONE) {
        self->error = other->error;
        return self;
    }
    if (self->current_element_size != other->current_element_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return self;
    }

    element_size = self->current_element_size;
    if (max_items > SIZE_MAX / element_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return self;
    }
    values_size = max_items * element_size;
    previous_state_used = self->state_used;
    if (!reserve_state(self, values_size, &values_offset)) {
        return self;
    }

    r = append_materialized_values(
        self,
        self->state_storage + values_offset,
        element_size,
        max_items,
        &count);
    if (r == STREAM_END) {
        r = append_materialized_values(
            other,
            self->state_storage + values_offset,
            element_size,
            max_items,
            &count);
    }

    if (r != STREAM_END) {
        self->state_used = previous_state_used;
        if (r == STREAM_FULL) {
            self->error = STREAM_ERR_CONCAT_FULL;
        } else if (r == STREAM_AGAIN) {
            self->error = STREAM_ERR_NEEDS_FINITE_SOURCE;
        } else if (self->error == STREAM_ERR_NONE) {
            self->error = other->error != STREAM_ERR_NONE
                ? other->error
                : STREAM_ERR_SOURCE_FAILED;
        }
        return self;
    }

    replace_with_owned_values(self, values_offset, count, element_size);
    return self;
}

static pipeline_flow_t execute_pipeline(stream_t *self, stream_item_t *item)
{
    size_t i;
    unsigned char *current = (unsigned char *)item->data;
    size_t current_size = item->size;

    for (i = 0; i < self->op_count; ++i) {
        stream_operation_t *op = &self->ops[i];

        switch (op->type) {
        case STREAM_OP_FILTER:
            if (!op->as.filter.predicate(current)) {
                return PIPELINE_DROP;
            }
            break;

        case STREAM_OP_MAP: {
            stream_result_t r;
            unsigned char *output;

            if (current_size != op->as.map.input_size) {
                self->error = STREAM_ERR_MAP_FAILED;
                return PIPELINE_ERROR;
            }

            /* Never write into borrowed source/state storage. */
            output = (current == self->scratch_a) ? self->scratch_b : self->scratch_a;

            r = op->as.map.mapper(current, output);
            if (r != STREAM_OK) {
                self->error = (r == STREAM_ERROR)
                    ? STREAM_ERR_MAP_FAILED
                    : STREAM_ERR_BAD_OPERATOR_RESULT;
                return PIPELINE_ERROR;
            }

            current = output;
            current_size = op->as.map.output_size;
            break;
        }

        case STREAM_OP_PEEK:
            op->as.peek.consumer(current);
            break;

        case STREAM_OP_DISTINCT: {
            unsigned char *values;
            size_t j;

            if (current_size != op->as.distinct.value_size) {
                self->error = STREAM_ERR_BAD_OPERATOR_RESULT;
                return PIPELINE_ERROR;
            }

            values = self->state_storage + op->as.distinct.values_offset;
            for (j = 0; j < op->as.distinct.count; ++j) {
                if (op->as.distinct.equals(
                        values + j * current_size, current)) {
                    return PIPELINE_DROP;
                }
            }

            if (op->as.distinct.count == op->as.distinct.max_unique) {
                self->error = STREAM_ERR_DISTINCT_FULL;
                return PIPELINE_ERROR;
            }

            memcpy(values + op->as.distinct.count * current_size,
                   current,
                   current_size);
            ++op->as.distinct.count;
            break;
        }

        case STREAM_OP_TAKE:
            if (op->as.take.seen >= op->as.take.count) {
                return PIPELINE_STOP;
            }
            ++op->as.take.seen;
            break;

        case STREAM_OP_TAKE_WHILE:
            if (!op->as.take_while.predicate(current)) {
                op->as.take_while.done = true;
                return PIPELINE_STOP;
            }
            break;

        case STREAM_OP_DROP_WHILE:
            if (op->as.drop_while.dropping) {
                if (op->as.drop_while.predicate(current)) {
                    return PIPELINE_DROP;
                }
                op->as.drop_while.dropping = false;
            }
            break;

        case STREAM_OP_SKIP:
            if (op->as.skip.skipped < op->as.skip.count) {
                ++op->as.skip.skipped;
                return PIPELINE_DROP;
            }
            break;

        case STREAM_OP_WINDOW: {
            unsigned char *buffer;
            uint64_t *sequences;
            uint64_t *timestamps;
            stream_window_t window;
            size_t count = op->as.window.count;
            size_t input_size = op->as.window.input_size;

            if (current_size != input_size) {
                self->error = STREAM_ERR_BAD_OPERATOR_RESULT;
                return PIPELINE_ERROR;
            }

            buffer = self->state_storage + op->as.window.buffer_offset;
            sequences = (uint64_t *)(void *)(self->state_storage + op->as.window.sequence_offset);
            timestamps = (uint64_t *)(void *)(self->state_storage + op->as.window.timestamp_offset);

            if (op->as.window.filled < count) {
                size_t pos = op->as.window.filled;
                memcpy(buffer + pos * input_size, current, input_size);
                sequences[pos] = item->sequence;
                timestamps[pos] = item->timestamp_ns;
                ++op->as.window.filled;
            } else {
                if (count > 1) {
                    memmove(buffer,
                            buffer + input_size,
                            (count - 1) * input_size);
                    memmove(sequences, sequences + 1, (count - 1) * sizeof(*sequences));
                    memmove(timestamps, timestamps + 1, (count - 1) * sizeof(*timestamps));
                }
                memcpy(buffer + (count - 1) * input_size, current, input_size);
                sequences[count - 1] = item->sequence;
                timestamps[count - 1] = item->timestamp_ns;
            }

            if (op->as.window.filled < count) {
                return PIPELINE_DROP;
            }

            window.data = buffer;
            window.count = count;
            window.element_size = input_size;
            window.first_sequence = sequences[0];
            window.last_sequence = sequences[count - 1];
            window.first_timestamp_ns = timestamps[0];
            window.last_timestamp_ns = timestamps[count - 1];

            /* stream_window_t always fits scratch by construction. */
            if (current == self->scratch_a) {
                memcpy(self->scratch_b, &window, sizeof(window));
                current = self->scratch_b;
            } else {
                memcpy(self->scratch_a, &window, sizeof(window));
                current = self->scratch_a;
            }
            current_size = sizeof(window);
            break;
        }

        case STREAM_OP_DEBOUNCE: {
            unsigned char *candidate;
            unsigned char *emitted;
            size_t value_size = op->as.debounce.value_size;

            if (current_size != value_size) {
                self->error = STREAM_ERR_BAD_OPERATOR_RESULT;
                return PIPELINE_ERROR;
            }

            candidate = self->state_storage + op->as.debounce.candidate_offset;
            emitted = self->state_storage + op->as.debounce.emitted_offset;

            if (!op->as.debounce.has_candidate ||
                !op->as.debounce.equals(candidate, current)) {
                memcpy(candidate, current, value_size);
                op->as.debounce.has_candidate = true;
                op->as.debounce.stable_count = 1;
            } else if (op->as.debounce.stable_count < op->as.debounce.threshold) {
                ++op->as.debounce.stable_count;
            }

            if (op->as.debounce.stable_count < op->as.debounce.threshold) {
                return PIPELINE_DROP;
            }

            if (op->as.debounce.has_emitted &&
                op->as.debounce.equals(emitted, current)) {
                return PIPELINE_DROP;
            }

            memcpy(emitted, current, value_size);
            op->as.debounce.has_emitted = true;
            break;
        }

        case STREAM_OP_DEBOUNCE_TIME: {
            unsigned char *candidate;
            unsigned char *emitted;
            size_t value_size = op->as.debounce_time.value_size;
            uint64_t now = item->timestamp_ns;

            if (current_size != value_size) {
                self->error = STREAM_ERR_BAD_OPERATOR_RESULT;
                return PIPELINE_ERROR;
            }

            candidate = self->state_storage + op->as.debounce_time.candidate_offset;
            emitted = self->state_storage + op->as.debounce_time.emitted_offset;

            if (!op->as.debounce_time.has_candidate ||
                !op->as.debounce_time.equals(candidate, current)) {
                memcpy(candidate, current, value_size);
                op->as.debounce_time.has_candidate = true;
                op->as.debounce_time.candidate_since_ns = now;
            } else if (now < op->as.debounce_time.candidate_since_ns) {
                /* Timestamp discontinuity: restart stability measurement. */
                op->as.debounce_time.candidate_since_ns = now;
            }

            if (now - op->as.debounce_time.candidate_since_ns <
                op->as.debounce_time.duration_ns) {
                return PIPELINE_DROP;
            }

            if (op->as.debounce_time.has_emitted &&
                op->as.debounce_time.equals(emitted, current)) {
                return PIPELINE_DROP;
            }

            memcpy(emitted, current, value_size);
            op->as.debounce_time.has_emitted = true;
            break;
        }
        }
    }

    item->data = current;
    item->size = current_size;
    return PIPELINE_PASS;
}

static bool pipeline_is_exhausted(const stream_t *self)
{
    size_t i;

    for (i = 0; i < self->op_count; ++i) {
        const stream_operation_t *op = &self->ops[i];

        if (op->type == STREAM_OP_TAKE &&
            op->as.take.seen >= op->as.take.count) {
            return true;
        }
        if (op->type == STREAM_OP_TAKE_WHILE && op->as.take_while.done) {
            return true;
        }
    }

    return false;
}

stream_result_t stream_next_view(stream_t *self, stream_item_t *out)
{
    stream_item_t item;

    if (!self || !out) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    if (self->error != STREAM_ERR_NONE) {
        return STREAM_ERROR;
    }

    /* A terminal take must not pull and discard one additional source item. */
    if (pipeline_is_exhausted(self)) {
        return STREAM_END;
    }

    for (;;) {
        stream_result_t r;
        pipeline_flow_t flow;

        item.data = self->scratch_a;
        item.size = self->source_element_size;
        item.timestamp_ns = 0;
        item.sequence = 0;

        r = self->source.next(&self->source, &item);
        if (r == STREAM_MODIFIED) {
            self->error = STREAM_ERR_SOURCE_MODIFIED;
            return r;
        }
        if (r == STREAM_END || r == STREAM_AGAIN) {
            /* STREAM_AGAIN here has exactly one public meaning: source empty now. */
            return r;
        }
        if (r != STREAM_OK) {
            self->error = STREAM_ERR_SOURCE_FAILED;
            return STREAM_ERROR;
        }

        if (!item.data || item.size == 0 || item.size > STREAM_MAX_ITEM_SIZE) {
            self->error = STREAM_ERR_ITEM_TOO_LARGE;
            return STREAM_ERROR;
        }

        flow = execute_pipeline(self, &item);
        if (flow == PIPELINE_DROP) {
            continue;
        }
        if (flow == PIPELINE_STOP) {
            return STREAM_END;
        }
        if (flow == PIPELINE_ERROR) {
            return STREAM_ERROR;
        }

        *out = item;
        return STREAM_OK;
    }
}

stream_result_t stream_next(stream_t *self, stream_item_t *out)
{
    stream_item_t view;
    stream_result_t r;

    if (!self || !out || !out->data) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    r = stream_next_view(self, &view);
    if (r != STREAM_OK) {
        return r;
    }

    /* out->size is the caller-provided capacity on input. */
    if (out->size < view.size) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return STREAM_ERROR;
    }

    memcpy(out->data, view.data, view.size);
    out->size = view.size;
    out->timestamp_ns = view.timestamp_ns;
    out->sequence = view.sequence;
    return STREAM_OK;
}

stream_result_t stream_for_each(stream_t *self, stream_consumer_fn consumer)
{
    stream_item_t item;

    if (!self || !consumer) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r == STREAM_OK) {
            consumer(item.data);
            continue;
        }

        /* For live sources, give control back to the event loop on AGAIN. */
        return r;
    }
}

stream_result_t stream_count(stream_t *self, size_t *out_count)
{
    stream_item_t item;

    if (!self || !out_count) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    *out_count = 0;
    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r != STREAM_OK) {
            return r;
        }
        if (*out_count == SIZE_MAX) {
            self->error = STREAM_ERR_COUNT_OVERFLOW;
            return STREAM_ERROR;
        }
        ++*out_count;
    }
}

stream_result_t stream_reduce(
    stream_t *self,
    void *accumulator,
    stream_reducer_fn reducer)
{
    stream_item_t item;

    if (!self || !accumulator || !reducer) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r != STREAM_OK) {
            return r;
        }

        r = reducer(accumulator, item.data);
        if (r == STREAM_OK) {
            continue;
        }

        self->error = (r == STREAM_ERROR)
            ? STREAM_ERR_REDUCE_FAILED
            : STREAM_ERR_BAD_OPERATOR_RESULT;
        return STREAM_ERROR;
    }
}

stream_result_t stream_find_first(stream_t *self, stream_item_t *out)
{
    if (!self) {
        return STREAM_ERROR;
    }
    return self->next(self, out);
}

stream_result_t stream_find_any(stream_t *self, stream_item_t *out)
{
    return stream_find_first(self, out);
}

typedef enum {
    STREAM_MATCH_ANY = 0,
    STREAM_MATCH_ALL,
    STREAM_MATCH_NONE
} stream_match_mode_t;

static stream_result_t stream_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches,
    stream_match_mode_t mode)
{
    stream_item_t item;
    bool result = mode != STREAM_MATCH_ANY;

    if (!self || !predicate || !out_matches) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    *out_matches = result;
    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r != STREAM_OK) {
            return r;
        }

        result = predicate(item.data);
        if ((mode == STREAM_MATCH_ANY && result) ||
            (mode == STREAM_MATCH_ALL && !result) ||
            (mode == STREAM_MATCH_NONE && result)) {
            *out_matches = mode == STREAM_MATCH_ANY;
            return STREAM_OK;
        }
    }
}

stream_result_t stream_any_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches)
{
    return stream_match(self, predicate, out_matches, STREAM_MATCH_ANY);
}

stream_result_t stream_all_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches)
{
    return stream_match(self, predicate, out_matches, STREAM_MATCH_ALL);
}

stream_result_t stream_none_match(
    stream_t *self,
    stream_predicate_fn predicate,
    bool *out_matches)
{
    return stream_match(self, predicate, out_matches, STREAM_MATCH_NONE);
}

stream_result_t stream_contains(
    stream_t *self,
    const void *value,
    stream_equal_fn equals,
    bool *out_contains)
{
    stream_item_t item;

    if (!self || !value || !equals || !out_contains) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    *out_contains = false;
    for (;;) {
        stream_result_t r = self->next_view(self, &item);
        if (r != STREAM_OK) {
            return r;
        }

        if (item.size != self->current_element_size) {
            self->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }
        if (equals(item.data, value)) {
            *out_contains = true;
            return STREAM_OK;
        }
    }
}

static stream_result_t stream_extreme(
    stream_t *self,
    stream_compare_fn compare,
    stream_item_t *out,
    bool *out_found,
    bool want_max)
{
    stream_item_t item;
    size_t capacity;

    if (!self || !compare || !out || !out->data || !out_found) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    capacity = out->size;
    if (capacity < self->current_element_size) {
        self->error = STREAM_ERR_ITEM_TOO_LARGE;
        return STREAM_ERROR;
    }

    *out_found = false;
    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r != STREAM_OK) {
            return r;
        }
        if (!item.data || item.size != self->current_element_size ||
            item.size > capacity) {
            self->error = (item.size > capacity)
                ? STREAM_ERR_ITEM_TOO_LARGE
                : STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }

        if (!*out_found ||
            (want_max ? compare(item.data, out->data) > 0
                      : compare(item.data, out->data) < 0)) {
            memcpy(out->data, item.data, item.size);
            out->size = item.size;
            out->timestamp_ns = item.timestamp_ns;
            out->sequence = item.sequence;
            *out_found = true;
        }
    }
}

stream_result_t stream_min(
    stream_t *self,
    stream_compare_fn compare,
    stream_item_t *out,
    bool *out_found)
{
    return stream_extreme(self, compare, out, out_found, false);
}

stream_result_t stream_max(
    stream_t *self,
    stream_compare_fn compare,
    stream_item_t *out,
    bool *out_found)
{
    return stream_extreme(self, compare, out, out_found, true);
}

stream_result_t stream_to_array(
    stream_t *self,
    void *out_values,
    size_t capacity,
    size_t element_size,
    size_t *out_count)
{
    stream_item_t item;
    unsigned char *output = (unsigned char *)out_values;

    if (!self || !out_count) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    *out_count = 0;
    if (element_size == 0 || element_size != self->current_element_size ||
        (capacity != 0 && !out_values) || capacity > SIZE_MAX / element_size) {
        self->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r;

        if (*out_count == capacity) {
            return STREAM_FULL;
        }

        r = self->next_view(self, &item);
        if (r != STREAM_OK) {
            return r;
        }
        if (item.size != element_size) {
            self->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }

        memcpy(output + *out_count * element_size, item.data, element_size);
        ++*out_count;
    }
}

stream_result_t stream_collect(
    stream_t *self,
    void *result,
    stream_reducer_fn accumulator)
{
    stream_item_t item;

    if (!self || !result || !accumulator) {
        if (self) {
            self->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r = self->next_view(self, &item);

        if (r != STREAM_OK) {
            return r;
        }

        r = accumulator(result, item.data);
        if (r == STREAM_OK) {
            continue;
        }
        self->error = (r == STREAM_ERROR)
            ? STREAM_ERR_COLLECT_FAILED
            : STREAM_ERR_BAD_OPERATOR_RESULT;
        return STREAM_ERROR;
    }
}

stream_result_t stream_reset(stream_t *self)
{
    size_t i;
    stream_result_t r;

    if (!self) {
        return STREAM_ERROR;
    }

    if (!self->source.reset) {
        self->error = STREAM_ERR_NOT_RESETTABLE;
        return STREAM_ERROR;
    }

    r = self->source.reset(&self->source);
    if (r == STREAM_MODIFIED) {
        self->error = STREAM_ERR_SOURCE_MODIFIED;
        return r;
    }
    if (r != STREAM_OK) {
        self->error = STREAM_ERR_SOURCE_FAILED;
        return STREAM_ERROR;
    }

    for (i = 0; i < self->op_count; ++i) {
        stream_operation_t *op = &self->ops[i];

        switch (op->type) {
        case STREAM_OP_TAKE:
            op->as.take.seen = 0;
            break;
        case STREAM_OP_SKIP:
            op->as.skip.skipped = 0;
            break;
        case STREAM_OP_TAKE_WHILE:
            op->as.take_while.done = false;
            break;
        case STREAM_OP_DROP_WHILE:
            op->as.drop_while.dropping = true;
            break;
        case STREAM_OP_WINDOW:
            op->as.window.filled = 0;
            memset(self->state_storage + op->as.window.buffer_offset,
                   0,
                   op->as.window.count * op->as.window.input_size);
            memset(self->state_storage + op->as.window.sequence_offset,
                   0,
                   op->as.window.count * sizeof(uint64_t));
            memset(self->state_storage + op->as.window.timestamp_offset,
                   0,
                   op->as.window.count * sizeof(uint64_t));
            break;
        case STREAM_OP_DEBOUNCE:
            op->as.debounce.stable_count = 0;
            op->as.debounce.has_candidate = false;
            op->as.debounce.has_emitted = false;
            memset(self->state_storage + op->as.debounce.candidate_offset,
                   0,
                   op->as.debounce.value_size);
            memset(self->state_storage + op->as.debounce.emitted_offset,
                   0,
                   op->as.debounce.value_size);
            break;
        case STREAM_OP_DEBOUNCE_TIME:
            op->as.debounce_time.candidate_since_ns = 0;
            op->as.debounce_time.has_candidate = false;
            op->as.debounce_time.has_emitted = false;
            memset(self->state_storage + op->as.debounce_time.candidate_offset,
                   0,
                   op->as.debounce_time.value_size);
            memset(self->state_storage + op->as.debounce_time.emitted_offset,
                   0,
                   op->as.debounce_time.value_size);
            break;
        case STREAM_OP_DISTINCT:
            op->as.distinct.count = 0;
            memset(self->state_storage + op->as.distinct.values_offset,
                   0,
                   op->as.distinct.max_unique * op->as.distinct.value_size);
            break;
        default:
            break;
        }
    }

    self->error = STREAM_ERR_NONE;
    return STREAM_OK;
}

void stream_clear(stream_t *self)
{
    size_t source_state_used;

    if (!self) {
        return;
    }

    source_state_used = source_owned_state_size(self);
    self->op_count = 0;
    self->current_element_size = self->source_element_size;
    self->state_used = source_state_used;
    if (source_state_used < sizeof(self->state_storage)) {
        memset(self->state_storage + source_state_used,
               0,
               sizeof(self->state_storage) - source_state_used);
    }
    self->error = STREAM_ERR_NONE;
}

void stream_close(stream_t *self)
{
    stream_source_close_fn close;

    if (!self) {
        return;
    }

    close = self->source.close;
    self->source.close = NULL;
    if (close) {
        close(&self->source);
    }
}

const char *stream_error_string(stream_error_t error)
{
    switch (error) {
    case STREAM_ERR_NONE:
        return "no error";
    case STREAM_ERR_PIPELINE_FULL:
        return "pipeline is full";
    case STREAM_ERR_ITEM_TOO_LARGE:
        return "item exceeds STREAM_MAX_ITEM_SIZE";
    case STREAM_ERR_BAD_ARGUMENT:
        return "bad argument";
    case STREAM_ERR_UNSUPPORTED_SOURCE:
        return "source state is unsupported for snapshot";
    case STREAM_ERR_NOT_RESETTABLE:
        return "source is not resettable";
    case STREAM_ERR_MAP_FAILED:
        return "map failed";
    case STREAM_ERR_SOURCE_MODIFIED:
        return "source container modified during iteration";
    case STREAM_ERR_SOURCE_FAILED:
        return "source operation failed";
    case STREAM_ERR_STATE_FULL:
        return "stateful operator storage is full";
    case STREAM_ERR_BAD_OPERATOR_RESULT:
        return "operator returned an unsupported result or type";
    case STREAM_ERR_REDUCE_FAILED:
        return "reduce callback failed";
    case STREAM_ERR_COUNT_OVERFLOW:
        return "stream count overflow";
    case STREAM_ERR_DISTINCT_FULL:
        return "distinct unique-value limit reached";
    case STREAM_ERR_COLLECT_FAILED:
        return "stream collection failed";
    case STREAM_ERR_SORT_FULL:
        return "sorted item limit reached";
    case STREAM_ERR_FLAT_MAP_FULL:
        return "flat_map output limit reached";
    case STREAM_ERR_FLAT_MAP_FAILED:
        return "flat_map callback failed";
    case STREAM_ERR_NEEDS_FINITE_SOURCE:
        return "operation requires a finite source";
    case STREAM_ERR_CONCAT_FULL:
        return "concat item limit reached";
    default:
        return "unknown error";
    }
}
