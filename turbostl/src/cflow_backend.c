#include <turbostl/stream.h>

#include <turbostl/hash_set.h>
#include <turbostl/sort.h>
#include <turbostl/vec.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct turbostl_distinct_state {
    hash_set_t values;
} turbostl_distinct_state;

typedef struct turbostl_sorted_state {
    vec_t values;
} turbostl_sorted_state;

static cflow_status turbostl_status_to_cflow(stl_status status) {
    switch (status) {
        case STL_OK: return CFLOW_STATUS_OK;
        case STL_INVALID_ARGUMENT: return CFLOW_STATUS_INVALID_ARGUMENT;
        case STL_TYPE_MISMATCH: return CFLOW_STATUS_TYPE_MISMATCH;
        case STL_TRAIT_MISSING: return CFLOW_STATUS_UNSUPPORTED;
        case STL_CAPACITY_EXCEEDED: return CFLOW_STATUS_CAPACITY_EXCEEDED;
        case STL_OUT_OF_MEMORY: return CFLOW_STATUS_ALLOCATION_FAILED;
        case STL_EMPTY:
        case STL_NOT_FOUND:
            return CFLOW_STATUS_EXECUTION_ERROR;
    }
    return CFLOW_STATUS_EXECUTION_ERROR;
}

static cflow_status turbostl_set_open(
    void **out_state, const cmeta_type_desc *type, size_t limit) {
    turbostl_distinct_state *state;
    stl_status status;
    if (!out_state || *out_state || !type || limit == 0u)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    state = (turbostl_distinct_state *)calloc(1u, sizeof(*state));
    if (!state) return CFLOW_STATUS_ALLOCATION_FAILED;
    status = hash_set_raw_init(&state->values, type, limit);
    if (status != STL_OK) {
        free(state);
        return turbostl_status_to_cflow(status);
    }
    *out_state = state;
    return CFLOW_STATUS_OK;
}

static cflow_status turbostl_set_insert_if_absent(
    void *state_ptr, const void *value, bool *inserted) {
    turbostl_distinct_state *state =
        (turbostl_distinct_state *)state_ptr;
    stl_status status;
    if (!state || !value || !inserted)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    if (hash_set_contains(&state->values, value)) {
        *inserted = false;
        return CFLOW_STATUS_OK;
    }
    status = hash_set_add(&state->values, value);
    if (status != STL_OK) return turbostl_status_to_cflow(status);
    *inserted = true;
    return CFLOW_STATUS_OK;
}

static void turbostl_set_close(void *state_ptr) {
    turbostl_distinct_state *state =
        (turbostl_distinct_state *)state_ptr;
    if (!state) return;
    hash_set_raw_destroy_storage(&state->values);
    free(state);
}

static const cflow_set_state_ops turbostl_set_ops = {
    turbostl_set_open,
    turbostl_set_insert_if_absent,
    turbostl_set_close
};

static cflow_status turbostl_sequence_open(
    void **out_state, const cmeta_type_desc *type, size_t limit) {
    turbostl_sorted_state *state;
    stl_status status;
    if (!out_state || *out_state || !type || limit == 0u)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    state = (turbostl_sorted_state *)calloc(1u, sizeof(*state));
    if (!state) return CFLOW_STATUS_ALLOCATION_FAILED;
    status = vec_raw_init(&state->values, type, limit);
    if (status != STL_OK) {
        free(state);
        return turbostl_status_to_cflow(status);
    }
    *out_state = state;
    return CFLOW_STATUS_OK;
}

static cflow_status turbostl_sequence_append(
    void *state_ptr, const void *value) {
    turbostl_sorted_state *state =
        (turbostl_sorted_state *)state_ptr;
    if (!state || !value) return CFLOW_STATUS_INVALID_ARGUMENT;
    return turbostl_status_to_cflow(vec_push(&state->values, value));
}

static cflow_status turbostl_sequence_sort(void *state_ptr) {
    turbostl_sorted_state *state =
        (turbostl_sorted_state *)state_ptr;
    size_t scratch_limit;
    if (!state || !state->values.element_type)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    if (state->values.size != 0u &&
        state->values.elem_stride > SIZE_MAX / state->values.size)
        return CFLOW_STATUS_CAPACITY_EXCEEDED;
    scratch_limit = state->values.size * state->values.elem_stride;
    return turbostl_status_to_cflow(stable_sort(
        state->values.data, state->values.size,
        state->values.element_type, scratch_limit));
}

static size_t turbostl_sequence_size(const void *object) {
    const turbostl_sorted_state *state =
        (const turbostl_sorted_state *)object;
    return state ? vec_size(&state->values) : 0u;
}

static uint64_t turbostl_sequence_version(const void *object) {
    const turbostl_sorted_state *state =
        (const turbostl_sorted_state *)object;
    return state ? vec_generation(&state->values) : UINT64_C(0);
}

static cmeta_gen_status turbostl_sequence_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    const turbostl_sorted_state *state =
        (const turbostl_sorted_state *)object;
    const void *source;
    const cmeta_type_desc *type;
    if (!state || !cursor || !out_value) return CMETA_GEN_ERROR;
    if (cursor->index >= state->values.size) return CMETA_GEN_DONE;
    type = state->values.element_type;
    source = vec_at_const(&state->values, cursor->index);
    if (!type || !source) return CMETA_GEN_ERROR;
    if ((type->traits->flags & CMETA_TRAIT_TRIVIAL_COPY) != 0u)
        memcpy(out_value, source, type->size);
    else if (!type->traits->copy_construct(out_value, source))
        return CMETA_GEN_ERROR;
    ++cursor->index;
    return cursor->index == state->values.size
        ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_range turbostl_sequence_range(const void *state_ptr) {
    const turbostl_sorted_state *state =
        (const turbostl_sorted_state *)state_ptr;
    if (!state || !state->values.element_type)
        return (cmeta_range){0};
    return (cmeta_range){
        .object = state,
        .element_type = state->values.element_type,
        .flags = CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                 CMETA_RANGE_SORTED | CMETA_RANGE_RANDOM_ACCESS |
                 CMETA_RANGE_REUSABLE | CMETA_RANGE_CONSTRUCTS_VALUES,
        .size = turbostl_sequence_size,
        .next = turbostl_sequence_next,
        .version = turbostl_sequence_version(state),
        .current_version = turbostl_sequence_version
    };
}

static void turbostl_sequence_close(void *state_ptr) {
    turbostl_sorted_state *state =
        (turbostl_sorted_state *)state_ptr;
    if (!state) return;
    vec_raw_destroy_storage(&state->values);
    free(state);
}

static const cflow_sequence_state_ops turbostl_sequence_ops = {
    turbostl_sequence_open,
    turbostl_sequence_append,
    turbostl_sequence_sort,
    turbostl_sequence_range,
    turbostl_sequence_close
};

static const cflow_eval_options turbostl_options = {
    &turbostl_set_ops,
    &turbostl_sequence_ops
};

const cflow_eval_options *turbostl_stream_eval_options(void) {
    return &turbostl_options;
}

turbostl_stream_t *turbostl_stream_from_object_view(
    turbostl_stream_t *stream_value,
    const void *object,
    cmeta_container_view view) {
    cmeta_range range;
    if (!stream_value ||
        !cmeta_container_range_view(object, view, &range))
        return NULL;
    return cflow_stream_from_range_with_options(
        stream_value, range, &turbostl_options);
}

turbostl_stream_t *turbostl_stream_from_object(
    turbostl_stream_t *stream_value, const void *object) {
    return turbostl_stream_from_object_view(
        stream_value, object, CMETA_CONTAINER_VIEW_DEFAULT);
}
