#include "stream_container.h"

#include <string.h>

_Static_assert(STREAM_MAX_CURSOR_SIZE > 0, "STREAM_MAX_CURSOR_SIZE must be positive");

typedef struct {
    const void *container;
    const stream_container_ops_t *ops;

    _Alignas(stream_max_align_t)
    unsigned char cursor[STREAM_MAX_CURSOR_SIZE];
} container_source_state_t;

_Static_assert(sizeof(container_source_state_t) <= STREAM_MAX_SOURCE_CONTEXT_SIZE,
               "STREAM_MAX_SOURCE_CONTEXT_SIZE too small for container source");

static stream_result_t container_source_next(
    stream_source_t *source,
    stream_item_t *out)
{
    container_source_state_t *state;

    if (!source || !source->context || !out || !out->data) {
        return STREAM_ERROR;
    }

    state = (container_source_state_t *)source->context;
    return state->ops->cursor_next(state->container, state->cursor, out);
}

static stream_result_t container_source_reset(stream_source_t *source)
{
    container_source_state_t *state;

    if (!source || !source->context) {
        return STREAM_ERROR;
    }

    state = (container_source_state_t *)source->context;

    if (state->ops->cursor_reset) {
        return state->ops->cursor_reset(state->container, state->cursor);
    }

    return state->ops->cursor_init(state->container, state->cursor);
}

static void container_source_close(stream_source_t *source)
{
    (void)source;
}

stream_result_t stream_from_container(
    stream_t *stream,
    const void *container,
    const stream_container_ops_t *ops)
{
    stream_source_t source;
    container_source_state_t *state;
    size_t element_size;
    stream_result_t r;

    if (!stream || !container || !ops || !ops->element_size ||
        !ops->cursor_init || !ops->cursor_next ||
        ops->cursor_size == 0 || ops->cursor_size > STREAM_MAX_CURSOR_SIZE) {
        return STREAM_ERROR;
    }

    element_size = ops->element_size(container);
    if (element_size == 0 || element_size > STREAM_MAX_ITEM_SIZE) {
        return STREAM_ERROR;
    }

    source.context = NULL; /* Installed after stream_init clears stream storage. */
    source.element_size = element_size;
    source.next = container_source_next;
    source.reset = container_source_reset;
    source.close = container_source_close;

    r = stream_init(stream, &source);
    if (r != STREAM_OK) {
        return r;
    }

    state = (container_source_state_t *)stream->source_context;
    memset(state, 0, sizeof(*state));
    state->container = container;
    state->ops = ops;

    r = ops->cursor_init(container, state->cursor);
    if (r != STREAM_OK) {
        stream->error = r == STREAM_MODIFIED
            ? STREAM_ERR_SOURCE_MODIFIED
            : STREAM_ERR_SOURCE_FAILED;
        return r == STREAM_MODIFIED ? r : STREAM_ERROR;
    }

    stream->source.context = state;
    return STREAM_OK;
}

/* ------------------------- Array view ------------------------- */

typedef struct {
    size_t index;
    uint64_t sequence;
} array_cursor_t;

static size_t array_element_size(const void *container)
{
    return ((const stream_array_view_t *)container)->element_size;
}

static stream_result_t array_cursor_init(const void *container, void *cursor)
{
    array_cursor_t *c = (array_cursor_t *)cursor;
    (void)container;
    c->index = 0;
    c->sequence = 0;
    return STREAM_OK;
}

static stream_result_t array_cursor_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const stream_array_view_t *array = (const stream_array_view_t *)container;
    array_cursor_t *c = (array_cursor_t *)cursor;

    if (!array || !c || !out || !out->data) {
        return STREAM_ERROR;
    }

    if (c->index >= array->count) {
        return STREAM_END;
    }

    out->data = (void *)(array->data + c->index * array->element_size);
    out->size = array->element_size;
    out->timestamp_ns = 0;
    out->sequence = c->sequence++;
    ++c->index;
    return STREAM_OK;
}

const stream_container_ops_t stream_array_container_ops = {
    sizeof(array_cursor_t),
    array_element_size,
    array_cursor_init,
    array_cursor_next,
    array_cursor_init
};

stream_array_view_t stream_array_view(
    const void *data,
    size_t count,
    size_t element_size)
{
    stream_array_view_t view;
    view.data = (const unsigned char *)data;
    view.count = count;
    view.element_size = element_size;
    return view;
}

stream_result_t stream_from_array_view(
    stream_t *stream,
    const stream_array_view_t *array)
{
    if (!array || (!array->data && array->count != 0) ||
        array->element_size == 0 ||
        array->count > SIZE_MAX / array->element_size) {
        return STREAM_ERROR;
    }

    return stream_from_container(stream, array, &stream_array_container_ops);
}

/* --------------------------- Vector --------------------------- */

typedef struct {
    size_t index;
    uint64_t expected_version;
    uint64_t sequence;
} vector_cursor_t;

void stream_vector_init(
    stream_vector_t *vector,
    void *storage,
    size_t capacity,
    size_t element_size)
{
    if (!vector) {
        return;
    }

    vector->data = (unsigned char *)storage;
    vector->size = 0;
    vector->capacity = capacity;
    vector->element_size = element_size;
    vector->version = 0;
}

stream_result_t stream_vector_push_back(
    stream_vector_t *vector,
    const void *value)
{
    if (!vector || !vector->data || !value || vector->element_size == 0) {
        return STREAM_ERROR;
    }

    if (vector->size >= vector->capacity) {
        return STREAM_ERROR;
    }

    memcpy(vector->data + vector->size * vector->element_size,
           value,
           vector->element_size);
    ++vector->size;
    ++vector->version;
    return STREAM_OK;
}

stream_result_t stream_vector_erase(stream_vector_t *vector, size_t index)
{
    size_t tail_count;

    if (!vector || index >= vector->size) {
        return STREAM_ERROR;
    }

    tail_count = vector->size - index - 1;
    if (tail_count != 0) {
        memmove(vector->data + index * vector->element_size,
                vector->data + (index + 1) * vector->element_size,
                tail_count * vector->element_size);
    }

    --vector->size;
    ++vector->version;
    return STREAM_OK;
}

void stream_vector_clear(stream_vector_t *vector)
{
    if (!vector) {
        return;
    }

    vector->size = 0;
    ++vector->version;
}

static size_t vector_element_size(const void *container)
{
    return ((const stream_vector_t *)container)->element_size;
}

static stream_result_t vector_cursor_init(const void *container, void *cursor)
{
    const stream_vector_t *vector = (const stream_vector_t *)container;
    vector_cursor_t *c = (vector_cursor_t *)cursor;

    if (!vector || !c) {
        return STREAM_ERROR;
    }

    c->index = 0;
    c->expected_version = vector->version;
    c->sequence = 0;
    return STREAM_OK;
}

static stream_result_t vector_cursor_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const stream_vector_t *vector = (const stream_vector_t *)container;
    vector_cursor_t *c = (vector_cursor_t *)cursor;

    if (!vector || !c || !out || !out->data) {
        return STREAM_ERROR;
    }

    if (c->expected_version != vector->version) {
        return STREAM_MODIFIED;
    }

    if (c->index >= vector->size) {
        return STREAM_END;
    }

    out->data = (void *)(vector->data + c->index * vector->element_size);
    out->size = vector->element_size;
    out->timestamp_ns = 0;
    out->sequence = c->sequence++;
    ++c->index;
    return STREAM_OK;
}

const stream_container_ops_t stream_vector_container_ops = {
    sizeof(vector_cursor_t),
    vector_element_size,
    vector_cursor_init,
    vector_cursor_next,
    vector_cursor_init
};

stream_result_t stream_from_vector(
    stream_t *stream,
    const stream_vector_t *vector)
{
    if (!vector || (!vector->data && vector->capacity != 0) ||
        vector->element_size == 0 || vector->size > vector->capacity ||
        vector->capacity > SIZE_MAX / vector->element_size) {
        return STREAM_ERROR;
    }

    return stream_from_container(stream, vector, &stream_vector_container_ops);
}

/* ---------------------------- List ---------------------------- */

typedef struct {
    const stream_list_node_t *current;
    uint64_t expected_version;
    uint64_t sequence;
} list_cursor_t;

void stream_list_init(stream_list_t *list, size_t element_size)
{
    if (!list) {
        return;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->element_size = element_size;
    list->version = 0;
}

void stream_list_node_init(stream_list_node_t *node, const void *value)
{
    if (!node) {
        return;
    }

    node->next = NULL;
    node->value = value;
}

stream_result_t stream_list_push_back(stream_list_t *list, stream_list_node_t *node)
{
    const stream_list_node_t *current;

    if (!list || !node || !node->value || list->element_size == 0) {
        return STREAM_ERROR;
    }

    /* Re-inserting a linked node would create a cycle or corrupt list ownership. */
    for (current = list->head; current; current = current->next) {
        if (current == node) {
            return STREAM_ERROR;
        }
    }

    node->next = NULL;
    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    ++list->size;
    ++list->version;
    return STREAM_OK;
}

void stream_list_clear(stream_list_t *list)
{
    if (!list) {
        return;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    ++list->version;
}

static size_t list_element_size(const void *container)
{
    return ((const stream_list_t *)container)->element_size;
}

static stream_result_t list_cursor_init(const void *container, void *cursor)
{
    const stream_list_t *list = (const stream_list_t *)container;
    list_cursor_t *c = (list_cursor_t *)cursor;

    if (!list || !c) {
        return STREAM_ERROR;
    }

    c->current = list->head;
    c->expected_version = list->version;
    c->sequence = 0;
    return STREAM_OK;
}

static stream_result_t list_cursor_next(
    const void *container,
    void *cursor,
    stream_item_t *out)
{
    const stream_list_t *list = (const stream_list_t *)container;
    list_cursor_t *c = (list_cursor_t *)cursor;

    if (!list || !c || !out || !out->data) {
        return STREAM_ERROR;
    }

    if (c->expected_version != list->version) {
        return STREAM_MODIFIED;
    }

    if (!c->current) {
        return STREAM_END;
    }

    out->data = (void *)c->current->value;
    out->size = list->element_size;
    out->timestamp_ns = 0;
    out->sequence = c->sequence++;
    c->current = c->current->next;
    return STREAM_OK;
}

const stream_container_ops_t stream_list_container_ops = {
    sizeof(list_cursor_t),
    list_element_size,
    list_cursor_init,
    list_cursor_next,
    list_cursor_init
};

stream_result_t stream_from_list(
    stream_t *stream,
    const stream_list_t *list)
{
    return stream_from_container(stream, list, &stream_list_container_ops);
}
