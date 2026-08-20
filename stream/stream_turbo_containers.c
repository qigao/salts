#include "stream_turbo_containers.h"

#include <string.h>

typedef struct {
    size_t index;
    const void *data;
    size_t size;
    size_t capacity;
    size_t element_size;
    size_t head;
} sequence_cursor_t;

static stream_result_t sequence_cursor_init_values(
    const void *data,
    size_t size,
    size_t capacity,
    size_t element_size,
    size_t head,
    sequence_cursor_t *state)
{
    if (!state || element_size == 0 || size > capacity ||
        (capacity != 0 && !data)) {
        return STREAM_ERROR;
    }

    memset(state, 0, sizeof(*state));
    state->data = data;
    state->size = size;
    state->capacity = capacity;
    state->element_size = element_size;
    state->head = head;
    return STREAM_OK;
}

static bool sequence_modified_values(
    const void *data,
    size_t size,
    size_t capacity,
    size_t element_size,
    size_t head,
    const sequence_cursor_t *state)
{
    return data != state->data || size != state->size ||
           capacity != state->capacity || element_size != state->element_size ||
           head != state->head;
}

static size_t vec_element_size(const void *container)
{
    return ((const turbo_vec_t *)container)->elem_size;
}

static stream_result_t vec_cursor_init(const void *container, void *cursor)
{
    const turbo_vec_t *vec = (const turbo_vec_t *)container;

    if (!vec) {
        return STREAM_ERROR;
    }
    return sequence_cursor_init_values(
        vec->data, vec->size, vec->capacity, vec->elem_size, 0,
        (sequence_cursor_t *)cursor);
}

static stream_result_t vec_cursor_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_vec_t *vec = (const turbo_vec_t *)container;
    sequence_cursor_t *state = (sequence_cursor_t *)cursor;
    const void *value;

    if (!vec || !state || !out) {
        return STREAM_ERROR;
    }
    if (sequence_modified_values(
            vec->data, vec->size, vec->capacity, vec->elem_size, 0, state)) {
        return STREAM_MODIFIED;
    }
    if (state->index == state->size) {
        return STREAM_END;
    }

    value = turbo_vec_at_const(vec, state->index);
    if (!value) {
        return STREAM_ERROR;
    }
    out->data = (void *)value;
    out->size = state->element_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static const stream_container_ops_t turbo_vec_stream_ops = {
    sizeof(sequence_cursor_t),
    vec_element_size,
    vec_cursor_init,
    vec_cursor_next,
    vec_cursor_init
};

stream_result_t stream_from_turbo_vec(stream_t *stream, const turbo_vec_t *vec)
{
    return stream_from_container(stream, vec, &turbo_vec_stream_ops);
}

static size_t deque_element_size(const void *container)
{
    return ((const turbo_deque_t *)container)->elem_size;
}

static stream_result_t deque_cursor_init(const void *container, void *cursor)
{
    const turbo_deque_t *deque = (const turbo_deque_t *)container;

    if (!deque) {
        return STREAM_ERROR;
    }
    return sequence_cursor_init_values(
        deque->data, deque->size, deque->capacity, deque->elem_size, deque->head,
        (sequence_cursor_t *)cursor);
}

static stream_result_t deque_cursor_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_deque_t *deque = (const turbo_deque_t *)container;
    sequence_cursor_t *state = (sequence_cursor_t *)cursor;
    const void *value;

    if (!deque || !state || !out) {
        return STREAM_ERROR;
    }
    if (sequence_modified_values(
            deque->data, deque->size, deque->capacity,
            deque->elem_size, deque->head, state)) {
        return STREAM_MODIFIED;
    }
    if (state->index == state->size) {
        return STREAM_END;
    }

    value = turbo_deque_at_const(deque, state->index);
    if (!value) {
        return STREAM_ERROR;
    }
    out->data = (void *)value;
    out->size = state->element_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static const stream_container_ops_t turbo_deque_stream_ops = {
    sizeof(sequence_cursor_t),
    deque_element_size,
    deque_cursor_init,
    deque_cursor_next,
    deque_cursor_init
};

stream_result_t stream_from_turbo_deque(stream_t *stream, const turbo_deque_t *deque)
{
    return stream_from_container(stream, deque, &turbo_deque_stream_ops);
}

stream_result_t stream_from_turbo_list(stream_t *stream, const turbo_list_t *list)
{
    return list ? stream_from_turbo_deque(stream, &list->raw) : STREAM_ERROR;
}

typedef struct {
    size_t slot;
    const void *states;
    const void *keys;
    const void *values;
    size_t size;
    size_t capacity;
    size_t tombstones;
    size_t key_size;
    size_t value_size;
    uint64_t sequence;
} hash_cursor_t;

static stream_result_t hash_cursor_init(const void *container, void *cursor)
{
    const turbo_hash_map_t *map = (const turbo_hash_map_t *)container;
    hash_cursor_t *state = (hash_cursor_t *)cursor;

    if (!map || !state || map->key_size == 0 || map->value_size == 0 ||
        map->size > map->capacity) {
        return STREAM_ERROR;
    }

    memset(state, 0, sizeof(*state));
    state->states = map->states;
    state->keys = map->keys;
    state->values = map->values;
    state->size = map->size;
    state->capacity = map->capacity;
    state->tombstones = map->tombstones;
    state->key_size = map->key_size;
    state->value_size = map->value_size;
    return STREAM_OK;
}

static bool hash_modified(const turbo_hash_map_t *map, const hash_cursor_t *state)
{
    return map->states != state->states || map->keys != state->keys ||
           map->values != state->values || map->size != state->size ||
           map->capacity != state->capacity || map->tombstones != state->tombstones ||
           map->key_size != state->key_size || map->value_size != state->value_size;
}

static stream_result_t hash_cursor_next(
    const void *container,
    void *cursor,
    stream_item_t *out,
    bool keys)
{
    const turbo_hash_map_t *map = (const turbo_hash_map_t *)container;
    hash_cursor_t *state = (hash_cursor_t *)cursor;
    const void *value;

    if (!map || !state || !out) {
        return STREAM_ERROR;
    }
    if (hash_modified(map, state)) {
        return STREAM_MODIFIED;
    }

    while (state->slot < state->capacity) {
        size_t slot = state->slot++;
        value = keys ? turbo_hash_map_key_at(map, slot)
                     : turbo_hash_map_value_at_const(map, slot);
        if (value) {
            out->data = (void *)value;
            out->size = keys ? state->key_size : state->value_size;
            out->timestamp_ns = 0;
            out->sequence = state->sequence++;
            return STREAM_OK;
        }
    }

    return STREAM_END;
}

static size_t hash_key_size(const void *container)
{
    return ((const turbo_hash_map_t *)container)->key_size;
}

static size_t hash_value_size(const void *container)
{
    return ((const turbo_hash_map_t *)container)->value_size;
}

static stream_result_t hash_key_next(const void *container, void *cursor, stream_item_t *out)
{
    return hash_cursor_next(container, cursor, out, true);
}

static stream_result_t hash_value_next(const void *container, void *cursor, stream_item_t *out)
{
    return hash_cursor_next(container, cursor, out, false);
}

static const stream_container_ops_t turbo_hash_key_stream_ops = {
    sizeof(hash_cursor_t),
    hash_key_size,
    hash_cursor_init,
    hash_key_next,
    hash_cursor_init
};

static const stream_container_ops_t turbo_hash_value_stream_ops = {
    sizeof(hash_cursor_t),
    hash_value_size,
    hash_cursor_init,
    hash_value_next,
    hash_cursor_init
};

stream_result_t stream_from_turbo_hash_keys(
    stream_t *stream,
    const turbo_hash_map_t *map)
{
    return stream_from_container(stream, map, &turbo_hash_key_stream_ops);
}

stream_result_t stream_from_turbo_hash_values(
    stream_t *stream,
    const turbo_hash_map_t *map)
{
    return stream_from_container(stream, map, &turbo_hash_value_stream_ops);
}

stream_result_t stream_from_turbo_map_keys(stream_t *stream, const turbo_map_t *map)
{
    return stream_from_turbo_hash_keys(stream, map);
}

stream_result_t stream_from_turbo_map_values(stream_t *stream, const turbo_map_t *map)
{
    return stream_from_turbo_hash_values(stream, map);
}

stream_result_t stream_from_turbo_set(stream_t *stream, const turbo_set_t *set)
{
    return set ? stream_from_turbo_hash_keys(stream, &set->map) : STREAM_ERROR;
}

typedef struct {
    size_t slot;
    size_t slot_index;
    size_t size;
    size_t capacity;
    size_t tombstones;
    const void *states;
    const void *keys;
    const void *values;
    size_t key_size;
    size_t value_size;
    turbo_hash_fn hash;
    turbo_hash_equal_fn equal;
    void *ctx;
    uint64_t sequence;
} turbo_multimap_cursor_t;

static bool turbo_multimap_modified(
    const turbo_multimap_t *map,
    const turbo_multimap_cursor_t *state)
{
    return !map || !state || map->size != state->size ||
           map->map.capacity != state->capacity ||
           map->map.tombstones != state->tombstones ||
           map->map.states != state->states || map->map.keys != state->keys ||
           map->map.values != state->values ||
           map->map.key_size != state->key_size ||
           map->value_size != state->value_size ||
           map->map.hash != state->hash ||
           map->map.equal != state->equal ||
           map->map.ctx != state->ctx;
}

static stream_result_t turbo_multimap_cursor_init(const void *container, void *cursor)
{
    const turbo_multimap_t *map = (const turbo_multimap_t *)container;
    turbo_multimap_cursor_t *state = (turbo_multimap_cursor_t *)cursor;

    if (!map || !state || map->map.key_size == 0U || map->value_size == 0U ||
        map->map.value_size != sizeof(turbo_vec_t) || map->map.size > map->map.capacity) {
        return STREAM_ERROR;
    }

    memset(state, 0, sizeof(*state));
    state->size = map->size;
    state->capacity = map->map.capacity;
    state->tombstones = map->map.tombstones;
    state->states = map->map.states;
    state->keys = map->map.keys;
    state->values = map->map.values;
    state->key_size = map->map.key_size;
    state->value_size = map->value_size;
    state->hash = map->map.hash;
    state->equal = map->map.equal;
    state->ctx = map->map.ctx;
    return STREAM_OK;
}

static size_t turbo_multimap_key_size(const void *container)
{
    return ((const turbo_multimap_t *)container)->map.key_size;
}

static size_t turbo_multimap_value_size(const void *container)
{
    return ((const turbo_multimap_t *)container)->value_size;
}

static stream_result_t turbo_multimap_key_value_next(
    const void *container,
    void *cursor,
    stream_item_t *out,
    bool keys)
{
    const turbo_multimap_t *map = (const turbo_multimap_t *)container;
    turbo_multimap_cursor_t *state = (turbo_multimap_cursor_t *)cursor;
    const turbo_vec_t *values;
    size_t value_count;

    if (!map || !state || !out) {
        return STREAM_ERROR;
    }
    if (turbo_multimap_modified(map, state)) {
        return STREAM_MODIFIED;
    }
    if (state->sequence == map->size) {
        return STREAM_END;
    }

    while (state->slot < state->capacity) {
        values = (const turbo_vec_t *)turbo_hash_map_value_at_const(&map->map, state->slot);
        value_count = values == NULL ? 0U : turbo_vec_size(values);

        if (state->slot_index >= value_count) {
            ++state->slot;
            state->slot_index = 0;
            continue;
        }

        if (keys) {
            out->data = (void *)turbo_hash_map_key_at(&map->map, state->slot);
            if (!out->data) {
                return STREAM_ERROR;
            }
            out->size = state->key_size;
        } else {
            out->data = (void *)turbo_vec_at_const(values, state->slot_index);
            if (!out->data) {
                return STREAM_ERROR;
            }
            out->size = state->value_size;
        }

        out->timestamp_ns = 0;
        out->sequence = state->sequence++;
        ++state->slot_index;
        return STREAM_OK;
    }

    return STREAM_ERROR;
}

static stream_result_t turbo_multimap_key_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    return turbo_multimap_key_value_next(container, cursor, out, true);
}

static stream_result_t turbo_multimap_value_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    return turbo_multimap_key_value_next(container, cursor, out, false);
}

static const stream_container_ops_t turbo_multimap_key_stream_ops = {
    sizeof(turbo_multimap_cursor_t),
    turbo_multimap_key_size,
    turbo_multimap_cursor_init,
    turbo_multimap_key_next,
    turbo_multimap_cursor_init,
};

static const stream_container_ops_t turbo_multimap_value_stream_ops = {
    sizeof(turbo_multimap_cursor_t),
    turbo_multimap_value_size,
    turbo_multimap_cursor_init,
    turbo_multimap_value_next,
    turbo_multimap_cursor_init,
};

stream_result_t stream_from_turbo_multimap_keys(
    stream_t *stream,
    const turbo_multimap_t *map)
{
    return stream_from_container(stream, map, &turbo_multimap_key_stream_ops);
}

stream_result_t stream_from_turbo_multimap_values(
    stream_t *stream,
    const turbo_multimap_t *map)
{
    return stream_from_container(stream, map, &turbo_multimap_value_stream_ops);
}

typedef struct {
    size_t index;
    size_t size;
    const void *data;
    size_t capacity;
    size_t elem_size;
    turbo_heap_compare_fn compare;
    void *compare_ctx;
    uint64_t sequence;
} turbo_heap_cursor_t;

static bool turbo_heap_modified(
    const turbo_heap_t *heap,
    const turbo_heap_cursor_t *state)
{
    return !heap || !state || heap->size != state->size ||
           heap->capacity != state->capacity ||
           heap->data != state->data || heap->elem_size != state->elem_size ||
           heap->compare != state->compare ||
           heap->compare_ctx != state->compare_ctx;
}

static stream_result_t turbo_heap_cursor_init(const void *container, void *cursor)
{
    const turbo_heap_t *heap = (const turbo_heap_t *)container;
    turbo_heap_cursor_t *state = (turbo_heap_cursor_t *)cursor;

    if (!heap || !state || heap->elem_size == 0U || heap->size > heap->capacity ||
        heap->compare == NULL) {
        return STREAM_ERROR;
    }

    if (heap->size > 0U && !heap->data) {
        return STREAM_ERROR;
    }

    memset(state, 0, sizeof(*state));
    state->size = heap->size;
    state->capacity = heap->capacity;
    state->data = heap->data;
    state->elem_size = heap->elem_size;
    state->compare = heap->compare;
    state->compare_ctx = heap->compare_ctx;
    return STREAM_OK;
}

static size_t turbo_heap_element_size(const void *container)
{
    return ((const turbo_heap_t *)container)->elem_size;
}

static stream_result_t turbo_heap_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_heap_t *heap = (const turbo_heap_t *)container;
    const turbo_heap_cursor_t *state = (const turbo_heap_cursor_t *)cursor;
    turbo_heap_cursor_t *mutable_state = (turbo_heap_cursor_t *)cursor;
    const unsigned char *value;

    if (!heap || !state || !out) {
        return STREAM_ERROR;
    }
    if (turbo_heap_modified(heap, state)) {
        return STREAM_MODIFIED;
    }

    if (mutable_state->index == mutable_state->size) {
        return STREAM_END;
    }

    value = (const unsigned char *)mutable_state->data +
            mutable_state->index * mutable_state->elem_size;
    if (value == NULL) {
        return STREAM_ERROR;
    }

    out->data = (void *)value;
    out->size = mutable_state->elem_size;
    out->timestamp_ns = 0;
    out->sequence = mutable_state->sequence++;
    ++mutable_state->index;
    return STREAM_OK;
}

static const stream_container_ops_t turbo_heap_stream_ops = {
    sizeof(turbo_heap_cursor_t),
    turbo_heap_element_size,
    turbo_heap_cursor_init,
    turbo_heap_next,
    turbo_heap_cursor_init,
};

stream_result_t stream_from_turbo_heap(stream_t *stream, const turbo_heap_t *heap)
{
    return stream_from_container(stream, heap, &turbo_heap_stream_ops);
}

typedef struct {
    size_t index;
    size_t size;
    const turbo_tree_map_node_t *root;
    size_t key_size;
    size_t value_size;
    turbo_tree_map_compare_fn compare;
    void *compare_ctx;
} tree_map_cursor_t;

static bool tree_map_modified(const turbo_tree_map_t *map, const tree_map_cursor_t *state)
{
    return !map || !state || map->size != state->size ||
           map->key_size != state->key_size || map->value_size != state->value_size ||
           map->compare != state->compare || map->compare_ctx != state->compare_ctx ||
           map->root != state->root;
}

static stream_result_t tree_map_cursor_init(const void *container, void *cursor)
{
    const turbo_tree_map_t *map = (const turbo_tree_map_t *)container;
    tree_map_cursor_t *state = (tree_map_cursor_t *)cursor;

    if (!map || !state || map->key_size == 0U || map->value_size == 0U || !map->compare) {
        return STREAM_ERROR;
    }

    memset(state, 0, sizeof(*state));
    state->size = map->size;
    state->root = map->root;
    state->key_size = map->key_size;
    state->value_size = map->value_size;
    state->compare = map->compare;
    state->compare_ctx = map->compare_ctx;
    return STREAM_OK;
}

static size_t tree_map_key_size(const void *container)
{
    return ((const turbo_tree_map_t *)container)->key_size;
}

static size_t tree_map_value_size(const void *container)
{
    return ((const turbo_tree_map_t *)container)->value_size;
}

static stream_result_t tree_map_key_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_tree_map_t *map = (const turbo_tree_map_t *)container;
    tree_map_cursor_t *state = (tree_map_cursor_t *)cursor;
    const void *value;

    if (!map || !state || !out) {
        return STREAM_ERROR;
    }
    if (tree_map_modified(map, state)) {
        return STREAM_MODIFIED;
    }
    if (state->index == state->size) {
        return STREAM_END;
    }
    value = turbo_tree_map_key_at_const(map, state->index);
    if (!value) {
        return STREAM_ERROR;
    }
    out->data = (void *)value;
    out->size = state->key_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static stream_result_t tree_map_value_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_tree_map_t *map = (const turbo_tree_map_t *)container;
    tree_map_cursor_t *state = (tree_map_cursor_t *)cursor;
    const void *value;

    if (!map || !state || !out) {
        return STREAM_ERROR;
    }
    if (tree_map_modified(map, state)) {
        return STREAM_MODIFIED;
    }
    if (state->index == state->size) {
        return STREAM_END;
    }
    value = turbo_tree_map_value_at_const(map, state->index);
    if (!value) {
        return STREAM_ERROR;
    }
    out->data = (void *)value;
    out->size = state->value_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static const stream_container_ops_t turbo_tree_map_key_stream_ops = {
    sizeof(tree_map_cursor_t),
    tree_map_key_size,
    tree_map_cursor_init,
    tree_map_key_next,
    tree_map_cursor_init
};

static const stream_container_ops_t turbo_tree_map_value_stream_ops = {
    sizeof(tree_map_cursor_t),
    tree_map_value_size,
    tree_map_cursor_init,
    tree_map_value_next,
    tree_map_cursor_init
};

stream_result_t stream_from_turbo_tree_map_keys(stream_t *stream, const turbo_tree_map_t *map)
{
    return stream_from_container(stream, map, &turbo_tree_map_key_stream_ops);
}

stream_result_t stream_from_turbo_tree_map_values(stream_t *stream, const turbo_tree_map_t *map)
{
    return stream_from_container(stream, map, &turbo_tree_map_value_stream_ops);
}

typedef struct {
    size_t index;
    size_t size;
    const turbo_bplus_tree_node_t *root;
    size_t key_size;
    size_t value_size;
    turbo_bplus_tree_compare_fn compare;
    void *compare_ctx;
} bplus_tree_cursor_t;

static bool bplus_tree_modified(
    const turbo_bplus_tree_t *tree,
    const bplus_tree_cursor_t *state)
{
    return !tree || !state || tree->size != state->size ||
           tree->key_size != state->key_size || tree->value_size != state->value_size ||
           tree->compare != state->compare || tree->compare_ctx != state->compare_ctx ||
           tree->root != state->root;
}

static stream_result_t bplus_tree_cursor_init(const void *container, void *cursor)
{
    const turbo_bplus_tree_t *tree = (const turbo_bplus_tree_t *)container;
    bplus_tree_cursor_t *state = (bplus_tree_cursor_t *)cursor;

    if (!tree || !state || tree->key_size == 0U || tree->value_size == 0U || !tree->compare) {
        return STREAM_ERROR;
    }

    memset(state, 0, sizeof(*state));
    state->size = tree->size;
    state->root = tree->root;
    state->key_size = tree->key_size;
    state->value_size = tree->value_size;
    state->compare = tree->compare;
    state->compare_ctx = tree->compare_ctx;
    return STREAM_OK;
}

static size_t bplus_tree_key_size(const void *container)
{
    return ((const turbo_bplus_tree_t *)container)->key_size;
}

static size_t bplus_tree_value_size(const void *container)
{
    return ((const turbo_bplus_tree_t *)container)->value_size;
}

static stream_result_t bplus_tree_key_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_bplus_tree_t *tree = (const turbo_bplus_tree_t *)container;
    bplus_tree_cursor_t *state = (bplus_tree_cursor_t *)cursor;
    const void *value;

    if (!tree || !state || !out) {
        return STREAM_ERROR;
    }
    if (bplus_tree_modified(tree, state)) {
        return STREAM_MODIFIED;
    }
    if (state->index == state->size) {
        return STREAM_END;
    }
    value = turbo_bplus_tree_key_at_const(tree, state->index);
    if (!value) {
        return STREAM_ERROR;
    }
    out->data = (void *)value;
    out->size = state->key_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static stream_result_t bplus_tree_value_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const turbo_bplus_tree_t *tree = (const turbo_bplus_tree_t *)container;
    bplus_tree_cursor_t *state = (bplus_tree_cursor_t *)cursor;
    const void *value;

    if (!tree || !state || !out) {
        return STREAM_ERROR;
    }
    if (bplus_tree_modified(tree, state)) {
        return STREAM_MODIFIED;
    }
    if (state->index == state->size) {
        return STREAM_END;
    }
    value = turbo_bplus_tree_value_at_const(tree, state->index);
    if (!value) {
        return STREAM_ERROR;
    }
    out->data = (void *)value;
    out->size = state->value_size;
    out->timestamp_ns = 0;
    out->sequence = state->index++;
    return STREAM_OK;
}

static const stream_container_ops_t turbo_bplus_tree_key_stream_ops = {
    sizeof(bplus_tree_cursor_t),
    bplus_tree_key_size,
    bplus_tree_cursor_init,
    bplus_tree_key_next,
    bplus_tree_cursor_init
};

static const stream_container_ops_t turbo_bplus_tree_value_stream_ops = {
    sizeof(bplus_tree_cursor_t),
    bplus_tree_value_size,
    bplus_tree_cursor_init,
    bplus_tree_value_next,
    bplus_tree_cursor_init
};

stream_result_t stream_from_turbo_bplus_tree_keys(stream_t *stream, const turbo_bplus_tree_t *tree)
{
    return stream_from_container(stream, tree, &turbo_bplus_tree_key_stream_ops);
}

stream_result_t stream_from_turbo_bplus_tree_values(stream_t *stream, const turbo_bplus_tree_t *tree)
{
    return stream_from_container(stream, tree, &turbo_bplus_tree_value_stream_ops);
}

stream_result_t stream_collect_turbo_vec(
    stream_t *stream,
    turbo_vec_t *out,
    size_t max_items)
{
    stream_item_t item;

    if (!stream || !out || out->elem_size == 0 ||
        out->size > out->capacity || (out->capacity != 0 && !out->data) ||
        out->elem_size != stream->current_element_size || out->size > max_items) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    if (turbo_vec_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r;

        if (out->size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }
        if (item.size != out->elem_size) {
            stream->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }
        if (turbo_vec_push(out, item.data) != TURBO_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }
    }
}

stream_result_t stream_collect_turbo_list(
    stream_t *stream,
    turbo_list_t *out,
    size_t max_items)
{
    stream_item_t item;
    size_t current_size;
    const size_t elem_size = out ? out->raw.elem_size : 0;

    if (!stream || !out || elem_size == 0 || out->raw.size > out->raw.capacity ||
        (out->raw.capacity != 0 && !out->raw.data) ||
        elem_size != stream->current_element_size) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_size = turbo_list_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (turbo_list_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r;

        if (current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }
        if (item.size != elem_size) {
            stream->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }
        if (turbo_list_push_back(out, item.data) != TURBO_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }
        current_size = turbo_list_size(out);
    }
}

stream_result_t stream_collect_turbo_set(
    stream_t *stream,
    turbo_set_t *out,
    size_t max_items)
{
    stream_item_t item;
    size_t current_size;

    if (!stream || !out || out->map.key_size == 0 ||
        out->map.key_size != stream->current_element_size) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_size = turbo_set_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r;

        if (current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }
        if (item.size != out->map.key_size) {
            stream->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }
        if (turbo_set_add(out, item.data) != TURBO_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }
        current_size = turbo_set_size(out);
    }
}

static stream_result_t stream_collect_turbo_map_internal(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_turbo_map_conflict_mode_t conflict_mode,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer)
{
    unsigned char key_buffer[STREAM_MAX_ITEM_SIZE];
    unsigned char value_buffer[STREAM_MAX_ITEM_SIZE];
    stream_item_t item;
    size_t current_size;
    stream_result_t r;
    void *stored;
    const void *existing;

    if (!stream || !out || !key_selector || !value_mapper ||
        key_size == 0 || value_size == 0 || key_size > STREAM_MAX_ITEM_SIZE ||
        value_size > STREAM_MAX_ITEM_SIZE || out->key_size != key_size ||
        out->value_size != value_size) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    if (conflict_mode != STREAM_TURBO_MAP_KEEP_LAST &&
        conflict_mode != STREAM_TURBO_MAP_KEEP_FIRST &&
        conflict_mode != STREAM_TURBO_MAP_REJECT &&
        conflict_mode != STREAM_TURBO_MAP_MERGE) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (conflict_mode == STREAM_TURBO_MAP_MERGE && !value_reducer) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    current_size = turbo_map_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (turbo_map_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        if (current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }

        r = key_selector(item.data, key_buffer);
        if (r != STREAM_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }

        r = value_mapper(item.data, value_buffer);
        if (r != STREAM_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }

        existing = turbo_map_get(out, key_buffer);
        if (!existing) {
            if (turbo_map_put(out, key_buffer, value_buffer) != TURBO_OK) {
                stream->error = STREAM_ERR_COLLECT_FAILED;
                return STREAM_ERROR;
            }
            ++current_size;
            continue;
        }

        if (conflict_mode == STREAM_TURBO_MAP_KEEP_FIRST) {
            continue;
        }

        if (conflict_mode == STREAM_TURBO_MAP_REJECT) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
            return STREAM_ERROR;
        }

        if (conflict_mode == STREAM_TURBO_MAP_MERGE) {
            stored = (void *)existing;
            if (value_reducer(stored, value_buffer) != STREAM_OK) {
                stream->error = STREAM_ERR_REDUCE_FAILED;
                return STREAM_ERROR;
            }
            continue;
        }

        if (turbo_map_put(out, key_buffer, value_buffer) != TURBO_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }
    }
}

stream_result_t stream_collect_turbo_map_with_conflict(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_turbo_map_conflict_mode_t conflict_mode,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer)
{
    return stream_collect_turbo_map_internal(
        stream,
        out,
        max_items,
        key_size,
        value_size,
        conflict_mode,
        key_selector,
        value_mapper,
        value_reducer);
}

stream_result_t stream_collect_turbo_map(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer)
{
    const stream_turbo_map_conflict_mode_t conflict_mode =
        value_reducer == NULL ? STREAM_TURBO_MAP_KEEP_LAST : STREAM_TURBO_MAP_MERGE;

    return stream_collect_turbo_map_internal(
        stream,
        out,
        max_items,
        key_size,
        value_size,
        conflict_mode,
        key_selector,
        value_mapper,
        value_reducer);
}

stream_result_t stream_collect_turbo_map_count(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t key_size,
    stream_mapper_fn key_selector)
{
    unsigned char key_buffer[STREAM_MAX_ITEM_SIZE];
    stream_item_t item;
    size_t current_size;
    size_t new_count;
    const void *stored_count_ptr;

    if (!stream || !out || !key_selector || key_size == 0 ||
        key_size > STREAM_MAX_ITEM_SIZE || out->key_size != key_size ||
        out->value_size != sizeof(size_t)) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_size = turbo_map_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }
    if (turbo_map_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        stream_result_t r;
        size_t *count;

        if (current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }

        r = key_selector(item.data, key_buffer);
        if (r != STREAM_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }

        stored_count_ptr = turbo_map_get(out, key_buffer);
        if (!stored_count_ptr) {
            new_count = 1;
            if (turbo_map_put(out, key_buffer, &new_count) != TURBO_OK) {
                stream->error = STREAM_ERR_COLLECT_FAILED;
                return STREAM_ERROR;
            }
            ++current_size;
            continue;
        }

        count = (size_t *)stored_count_ptr;
        ++(*count);
    }
}

stream_result_t stream_collect_turbo_partition(
    stream_t *stream,
    turbo_list_t *true_dest,
    size_t max_true_items,
    turbo_list_t *false_dest,
    size_t max_false_items,
    stream_predicate_fn predicate)
{
    stream_item_t item;
    size_t current_true_size;
    size_t current_false_size;

    if (!stream || !true_dest || !false_dest || !predicate ||
        true_dest == false_dest || true_dest->raw.elem_size == 0 ||
        false_dest->raw.elem_size == 0 ||
        true_dest->raw.elem_size != false_dest->raw.elem_size ||
        stream->current_element_size != true_dest->raw.elem_size ||
        stream->current_element_size != false_dest->raw.elem_size) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_true_size = turbo_list_size(true_dest);
    current_false_size = turbo_list_size(false_dest);
    if (current_true_size > max_true_items || current_false_size > max_false_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (turbo_list_reserve(true_dest, max_true_items) != TURBO_OK ||
        turbo_list_reserve(false_dest, max_false_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    if (max_true_items == 0 && max_false_items == 0) {
        return STREAM_FULL;
    }

    for (;;) {
        stream_result_t r;
        turbo_list_t *destination;
        size_t *destination_size;
        size_t destination_max;

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }

        if (item.size != stream->current_element_size) {
            stream->error = STREAM_ERR_BAD_OPERATOR_RESULT;
            return STREAM_ERROR;
        }

        if (predicate(item.data)) {
            destination = true_dest;
            destination_size = &current_true_size;
            destination_max = max_true_items;
        } else {
            destination = false_dest;
            destination_size = &current_false_size;
            destination_max = max_false_items;
        }

        if (*destination_size == destination_max) {
            return STREAM_FULL;
        }

        if (turbo_list_push_back(destination, item.data) != TURBO_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }
        ++*destination_size;
    }
}

stream_result_t stream_collect_turbo_partition_count(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    stream_predicate_fn predicate)
{
    uint8_t key_buffer[1];
    stream_item_t item;
    size_t current_size;
    const void *stored_count_ptr;
    stream_result_t r;
    size_t *count;

    if (!stream || !out || !predicate || out->key_size != sizeof(uint8_t) ||
        out->value_size != sizeof(size_t)) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_size = turbo_map_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (turbo_map_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        if (max_items <= 1 && current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }

        key_buffer[0] = predicate(item.data) ? 1 : 0;
        stored_count_ptr = turbo_map_get(out, key_buffer);
        if (!stored_count_ptr) {
            if (current_size == max_items) {
                return STREAM_FULL;
            }
            size_t new_count = 1;
            if (turbo_map_put(out, key_buffer, &new_count) != TURBO_OK) {
                stream->error = STREAM_ERR_COLLECT_FAILED;
                return STREAM_ERROR;
            }
            ++current_size;
            continue;
        }

        count = (size_t *)stored_count_ptr;
        ++(*count);
    }
}

stream_result_t stream_collect_turbo_partition_reduce(
    stream_t *stream,
    turbo_map_t *out,
    size_t max_items,
    size_t value_size,
    stream_predicate_fn predicate,
    stream_mapper_fn value_mapper,
    stream_reducer_fn value_reducer)
{
    uint8_t key_buffer[1];
    unsigned char value_buffer[STREAM_MAX_ITEM_SIZE];
    stream_item_t item;
    size_t current_size;
    stream_result_t r;
    const void *stored_ptr;

    if (!stream || !out || !predicate || !value_mapper || value_size == 0 ||
        value_size > STREAM_MAX_ITEM_SIZE || out->key_size != sizeof(uint8_t) ||
        out->value_size != value_size) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_size = turbo_map_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (turbo_map_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        if (max_items <= 1 && current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }

        key_buffer[0] = predicate(item.data) ? 1 : 0;
        r = value_mapper(item.data, value_buffer);
        if (r != STREAM_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }

        stored_ptr = turbo_map_get(out, key_buffer);
        if (!stored_ptr) {
            if (current_size == max_items) {
                return STREAM_FULL;
            }
            if (turbo_map_put(out, key_buffer, value_buffer) != TURBO_OK) {
                stream->error = STREAM_ERR_COLLECT_FAILED;
                return STREAM_ERROR;
            }
            ++current_size;
            continue;
        }

        if (!value_reducer) {
            if (turbo_map_put(out, key_buffer, value_buffer) != TURBO_OK) {
                stream->error = STREAM_ERR_COLLECT_FAILED;
                return STREAM_ERROR;
            }
            continue;
        }

        if (value_reducer((void *)stored_ptr, value_buffer) != STREAM_OK) {
            stream->error = STREAM_ERR_REDUCE_FAILED;
            return STREAM_ERROR;
        }
    }
}

stream_result_t stream_collect_turbo_multimap(
    stream_t *stream,
    turbo_multimap_t *out,
    size_t max_items,
    size_t key_size,
    size_t value_size,
    stream_mapper_fn key_selector,
    stream_mapper_fn value_mapper)
{
    unsigned char key_buffer[STREAM_MAX_ITEM_SIZE];
    unsigned char value_buffer[STREAM_MAX_ITEM_SIZE];
    stream_item_t item;
    size_t current_size;
    stream_result_t r;

    if (!stream || !out || !key_selector || !value_mapper || key_size == 0 ||
        value_size == 0 || key_size > STREAM_MAX_ITEM_SIZE ||
        value_size > STREAM_MAX_ITEM_SIZE ||
        out->map.key_size != key_size || out->value_size != value_size) {
        if (stream) {
            stream->error = STREAM_ERR_BAD_ARGUMENT;
        }
        return STREAM_ERROR;
    }

    current_size = turbo_multimap_size(out);
    if (current_size > max_items) {
        stream->error = STREAM_ERR_BAD_ARGUMENT;
        return STREAM_ERROR;
    }

    if (turbo_multimap_reserve(out, max_items) != TURBO_OK) {
        stream->error = STREAM_ERR_COLLECT_FAILED;
        return STREAM_ERROR;
    }

    for (;;) {
        if (current_size == max_items) {
            return STREAM_FULL;
        }

        r = stream_next_view(stream, &item);
        if (r != STREAM_OK) {
            return r;
        }

        r = key_selector(item.data, key_buffer);
        if (r != STREAM_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }

        r = value_mapper(item.data, value_buffer);
        if (r != STREAM_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }

        if (turbo_multimap_put(out, key_buffer, value_buffer) != TURBO_OK) {
            stream->error = STREAM_ERR_COLLECT_FAILED;
            return STREAM_ERROR;
        }
        ++current_size;
    }
}
