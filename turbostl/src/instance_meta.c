#include <turbostl/typed.h>

#include <string.h>

static cmeta_status stl_instance_cmeta_status(stl_status status) {
    switch (status) {
        case STL_OK: return CMETA_OK;
        case STL_INVALID_ARGUMENT: return CMETA_INVALID_ARGUMENT;
        case STL_OUT_OF_MEMORY: return CMETA_OUT_OF_MEMORY;
        case STL_CAPACITY_EXCEEDED: return CMETA_CAPACITY_EXCEEDED;
        case STL_TYPE_MISMATCH: return CMETA_TYPE_MISMATCH;
        case STL_TRAIT_MISSING: return CMETA_TRAIT_MISSING;
        case STL_EMPTY:
        case STL_NOT_FOUND:
        default: return CMETA_CALLBACK_ERROR;
    }
}

#define STL_DEFINE_INDEXED_INSTANCE_META(                                      \
    prefix, handle_type, element_expr, size_expr, at_expr, generation_expr,     \
    init_expr, accept_expr, destroy_expr, range_flags)                          \
static const cmeta_type_desc *stl_##prefix##_element_type(                      \
    const handle_type *self) {                                                   \
    return self == NULL ? NULL : (element_expr);                                \
}                                                                                \
static size_t stl_##prefix##_range_size(const void *object) {                   \
    const handle_type *self = (const handle_type *)object;                      \
    return self == NULL ? 0u : (size_expr);                                     \
}                                                                                \
static uint64_t stl_##prefix##_range_version(const void *object) {              \
    const handle_type *self = (const handle_type *)object;                      \
    return self == NULL ? UINT64_C(0) : (generation_expr);                      \
}                                                                                \
static cmeta_gen_status stl_##prefix##_range_next(                              \
    const void *object, cmeta_range_cursor *cursor, void *out_value) {           \
    const handle_type *self = (const handle_type *)object;                      \
    const cmeta_type_desc *type = stl_##prefix##_element_type(self);            \
    const void *value;                                                           \
    size_t count;                                                                \
    if (self == NULL || type == NULL || cursor == NULL || out_value == NULL)    \
        return CMETA_GEN_ERROR;                                                  \
    count = (size_expr);                                                         \
    if (cursor->index >= count)                                                  \
        return CMETA_GEN_DONE;                                                   \
    value = (at_expr);                                                           \
    if (value == NULL)                                                           \
        return CMETA_GEN_ERROR;                                                  \
    if (!stl_cmeta_range_construct(type, out_value, value))                     \
        return CMETA_GEN_ERROR;                                                  \
    ++cursor->index;                                                             \
    return cursor->index == count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE; \
}                                                                                \
static cmeta_range stl_##prefix##_range_factory(const void *object) {           \
    const handle_type *self = (const handle_type *)object;                      \
    cmeta_range range = {                                                        \
        object,                                                                  \
        stl_##prefix##_element_type(self),                                       \
        stl_cmeta_range_flags_for(stl_##prefix##_element_type(self),             \
                                  (range_flags)),                                \
        stl_##prefix##_range_size,                                               \
        stl_##prefix##_range_next,                                               \
        stl_##prefix##_range_version(object),                                    \
        stl_##prefix##_range_version};                                           \
    return range;                                                                \
}                                                                                \
static cmeta_status stl_##prefix##_collector_begin(                             \
    void *context, const cmeta_type_desc *input, size_t limit) {                \
    handle_type *output = (handle_type *)context;                               \
    const cmeta_type_desc *bound = stl_##prefix##_element_type(output);         \
    if (output == NULL || bound == NULL || input == NULL)                       \
        return CMETA_INVALID_ARGUMENT;                                           \
    if (!cmeta_type_equal(bound, input))                                         \
        return CMETA_TYPE_MISMATCH;                                              \
    return stl_instance_cmeta_status((init_expr));                              \
}                                                                                \
static cmeta_status stl_##prefix##_collector_accept(                            \
    void *context, const void *value) {                                          \
    handle_type *output = (handle_type *)context;                               \
    if (output == NULL || value == NULL)                                         \
        return CMETA_INVALID_ARGUMENT;                                           \
    return stl_instance_cmeta_status((accept_expr));                            \
}                                                                                \
static cmeta_status stl_##prefix##_collector_finish(void *context) {            \
    (void)context;                                                               \
    return CMETA_OK;                                                             \
}                                                                                \
static void stl_##prefix##_collector_abort(void *context) {                     \
    handle_type *output = (handle_type *)context;                               \
    if (output != NULL)                                                          \
        destroy_expr;                                                            \
}                                                                                \
static const cmeta_collector_ops stl_##prefix##_collector_ops = {               \
    stl_##prefix##_collector_begin,                                              \
    stl_##prefix##_collector_accept,                                             \
    stl_##prefix##_collector_finish,                                             \
    stl_##prefix##_collector_abort};                                             \
static cmeta_collector stl_##prefix##_collector_factory(                        \
    void *zero_output, size_t limit) {                                           \
    handle_type *output = (handle_type *)zero_output;                           \
    cmeta_collector collector = {                                                \
        &stl_##prefix##_collector_ops,                                           \
        output,                                                                  \
        output,                                                                  \
        stl_##prefix##_element_type(output),                                     \
        limit,                                                                   \
        0u,                                                                      \
        CMETA_COLLECTOR_ZERO,                                                    \
        CMETA_OK};                                                               \
    return collector;                                                            \
}                                                                                \
const cmeta_container_desc stl_##prefix##_container_desc = {                    \
    #prefix,                                                                     \
    NULL,                                                                        \
    NULL,                                                                        \
    NULL,                                                                        \
    NULL,                                                                        \
    stl_##prefix##_range_factory,                                                \
    NULL,                                                                        \
    NULL,                                                                        \
    NULL,                                                                        \
    stl_##prefix##_collector_factory,                                            \
    &stl_##prefix##_container_ext};

STL_DEFINE_INDEXED_INSTANCE_META(
    vec, vec_t, self->element_type, vec_size(self),
    vec_at_const(self, cursor->index), vec_generation(self),
    vec_init(output, limit), vec_push(output, value), vec_destroy(output),
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_CONTIGUOUS |
        CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

STL_DEFINE_INDEXED_INSTANCE_META(
    deque, deque_t, self->element_type, deque_size(self),
    deque_at_const(self, cursor->index), deque_generation(self),
    deque_init(output, limit), deque_push_back(output, value),
    deque_destroy(output),
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS |
        CMETA_RANGE_REUSABLE)

STL_DEFINE_INDEXED_INSTANCE_META(
    stack, turbostl_stack_t, self->raw.element_type, stack_size(self),
    stack_at_const(self, cursor->index), stack_generation(self),
    stack_init(output, limit), stack_push(output, value), stack_destroy(output),
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS |
        CMETA_RANGE_REUSABLE)

STL_DEFINE_INDEXED_INSTANCE_META(
    queue, queue_t, self->raw.element_type, queue_size(self),
    queue_at_const(self, cursor->index), queue_generation(self),
    queue_init(output, limit), queue_push(output, value), queue_destroy(output),
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS |
        CMETA_RANGE_REUSABLE)

STL_DEFINE_INDEXED_INSTANCE_META(
    heap, heap_t, self->element_type, heap_size(self),
    heap_at_const(self, cursor->index), heap_generation(self),
    heap_init(output, limit), heap_push(output, value), heap_destroy(output),
    CMETA_RANGE_SIZED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#undef STL_DEFINE_INDEXED_INSTANCE_META

static const cmeta_type_desc *stl_set_element_type(const set_t *set) {
    return set == NULL ? NULL : set->element_type;
}

static size_t stl_set_range_size(const void *object) {
    return set_size((const set_t *)object);
}

static uint64_t stl_set_range_version(const void *object) {
    return set_generation((const set_t *)object);
}

static cmeta_gen_status stl_set_range_next_cb(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    const set_t *set = (const set_t *)object;
    const cmeta_type_desc *type = stl_set_element_type(set);
    const void *value = NULL;
    cmeta_range_cursor next_cursor;
    if (set == NULL || type == NULL || cursor == NULL || out_value == NULL)
        return CMETA_GEN_ERROR;
    next_cursor = *cursor;
    if (!set_range_next(set, &next_cursor, &value))
        return CMETA_GEN_DONE;
    if (value == NULL)
        return CMETA_GEN_ERROR;
    if (!stl_cmeta_range_construct(type, out_value, value))
        return CMETA_GEN_ERROR;
    *cursor = next_cursor;
    return cursor->state[0] == NULL ? CMETA_GEN_VALUE_AND_DONE :
                                      CMETA_GEN_VALUE;
}

static cmeta_range stl_set_range_factory(const void *object) {
    const set_t *set = (const set_t *)object;
    cmeta_range range = {
        object,
        stl_set_element_type(set),
        stl_cmeta_range_flags_for(
            stl_set_element_type(set),
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE),
        stl_set_range_size,
        stl_set_range_next_cb,
        stl_set_range_version(object),
        stl_set_range_version};
    return range;
}

static cmeta_status stl_set_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
    set_t *output = (set_t *)context;
    if (output == NULL || output->element_type == NULL || input == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_type_equal(output->element_type, input))
        return CMETA_TYPE_MISMATCH;
    return stl_instance_cmeta_status(set_init(output, limit));
}

static cmeta_status stl_set_collector_accept(void *context, const void *value) {
    if (context == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    return stl_instance_cmeta_status(set_add((set_t *)context, value));
}

static cmeta_status stl_set_collector_finish(void *context) {
    (void)context;
    return CMETA_OK;
}

static void stl_set_collector_abort(void *context) {
    if (context != NULL)
        set_destroy((set_t *)context);
}

static const cmeta_collector_ops stl_set_collector_ops = {
    stl_set_collector_begin,
    stl_set_collector_accept,
    stl_set_collector_finish,
    stl_set_collector_abort};

static cmeta_collector stl_set_collector_factory(
    void *zero_output, size_t limit) {
    set_t *output = (set_t *)zero_output;
    cmeta_collector collector = {
        &stl_set_collector_ops,
        output,
        output,
        stl_set_element_type(output),
        limit,
        0u,
        CMETA_COLLECTOR_ZERO,
        CMETA_OK};
    return collector;
}

const cmeta_container_desc stl_set_container_desc = {
    "Set", NULL, NULL, NULL, NULL,
    stl_set_range_factory, NULL, NULL, NULL, stl_set_collector_factory,
    &stl_set_container_ext};

static const cmeta_type_desc *stl_hash_set_element_type(
    const hash_set_t *set) {
    return set == NULL ? NULL : set->element_type;
}

static size_t stl_hash_set_range_size(const void *object) {
    return hash_set_size((const hash_set_t *)object);
}

static uint64_t stl_hash_set_range_version(const void *object) {
    return hash_set_generation((const hash_set_t *)object);
}

static cmeta_gen_status stl_hash_set_range_next_cb(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    const hash_set_t *set = (const hash_set_t *)object;
    const cmeta_type_desc *type = stl_hash_set_element_type(set);
    cmeta_range_cursor start_cursor;
    size_t capacity;
    if (set == NULL || type == NULL || cursor == NULL || out_value == NULL)
        return CMETA_GEN_ERROR;
    start_cursor = *cursor;
    capacity = hash_set_capacity(set);
    while (cursor->index < capacity) {
        const void *value = hash_set_key_at(set, cursor->index);
        ++cursor->index;
        if (value != NULL) {
            if (!stl_cmeta_range_construct(type, out_value, value))
            {
                *cursor = start_cursor;
                return CMETA_GEN_ERROR;
            }
            return CMETA_GEN_VALUE;
        }
    }
    return CMETA_GEN_DONE;
}

static cmeta_range stl_hash_set_range_factory(const void *object) {
    const hash_set_t *set = (const hash_set_t *)object;
    cmeta_range range = {
        object,
        stl_hash_set_element_type(set),
        stl_cmeta_range_flags_for(
            stl_hash_set_element_type(set),
            CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE),
        stl_hash_set_range_size,
        stl_hash_set_range_next_cb,
        stl_hash_set_range_version(object),
        stl_hash_set_range_version};
    return range;
}

static cmeta_status stl_hash_set_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
    hash_set_t *output = (hash_set_t *)context;
    if (output == NULL || output->element_type == NULL || input == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_type_equal(output->element_type, input))
        return CMETA_TYPE_MISMATCH;
    return stl_instance_cmeta_status(hash_set_init(output, limit));
}

static cmeta_status stl_hash_set_collector_accept(
    void *context, const void *value) {
    if (context == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    return stl_instance_cmeta_status(
        hash_set_add((hash_set_t *)context, value));
}

static cmeta_status stl_hash_set_collector_finish(void *context) {
    (void)context;
    return CMETA_OK;
}

static void stl_hash_set_collector_abort(void *context) {
    if (context != NULL)
        hash_set_destroy((hash_set_t *)context);
}

static const cmeta_collector_ops stl_hash_set_collector_ops = {
    stl_hash_set_collector_begin,
    stl_hash_set_collector_accept,
    stl_hash_set_collector_finish,
    stl_hash_set_collector_abort};

static cmeta_collector stl_hash_set_collector_factory(
    void *zero_output, size_t limit) {
    hash_set_t *output = (hash_set_t *)zero_output;
    cmeta_collector collector = {
        &stl_hash_set_collector_ops,
        output,
        output,
        stl_hash_set_element_type(output),
        limit,
        0u,
        CMETA_COLLECTOR_ZERO,
        CMETA_OK};
    return collector;
}

const cmeta_container_desc stl_hash_set_container_desc = {
    "HashSet", NULL, NULL, NULL, NULL,
    stl_hash_set_range_factory, NULL, NULL, NULL,
    stl_hash_set_collector_factory, &stl_hash_set_container_ext};
