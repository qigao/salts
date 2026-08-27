#include "statechart_runtime_internal.h"

#include "executor_internal.h"
#include "statechart_internal.h"
#include "value_storage.h"

#include <turbo/thread.h>

#include <stdlib.h>
#include <string.h>

typedef struct statechart_configuration_buffer {
    unsigned char *bits;
    size_t *states;
    size_t state_count;
} statechart_configuration_buffer;

typedef struct statechart_internal_event_slot {
    size_t type_index;
} statechart_internal_event_slot;

typedef struct cflow_statechart_instance_impl {
    const cflow_statechart *statechart;
    const cflow_statechart_impl *ir;
    cflow_executor *executor;
    cflow_statechart_guard_binding *guards;
    size_t guard_count;
    cflow_statechart_executable_binding *executables;
    size_t executable_count;
    statechart_configuration_buffer configurations[2];
    unsigned char *history_bits[2];
    size_t *history_counts[2];
    size_t *history_slots;
    size_t history_count;
    unsigned char *extended_states[2];
    size_t published;
    size_t bitset_bytes;
    size_t *entry_stack;
    size_t *path_stack;
    size_t *exit_order;
    size_t *entry_order;
    size_t *pseudo_transition_order;
    cflow_statechart_transition_id *selected_transition_ids;
    size_t *selected_transition_indices;
    size_t *selected_sources;
    size_t *selected_leaf_orders;
    unsigned char *selected_exit_sets;
    unsigned char *candidate_exit_set;
    unsigned char *candidate_seen;
    size_t selected_count;
    size_t *request_transition_indices;
    unsigned char *request_exit_sets;
    size_t request_transition_count;
    cflow_statechart_selection_trigger request_trigger;
    cflow_event_view request_event;
    unsigned char *request_event_payload;
    unsigned char *exit_union;
    unsigned char *entry_bits;
    unsigned char *action_scratch;
    statechart_internal_event_slot *staged_event_slots;
    unsigned char *staged_event_payloads;
    size_t staged_event_count;
    statechart_internal_event_slot *internal_event_slots;
    unsigned char *internal_event_payloads;
    size_t internal_event_head;
    size_t internal_event_count;
    size_t internal_event_capacity;
    size_t event_payload_stride;
    bool microstep_pending;
    cflow_statechart_runtime_status microstep_result;
    uint64_t microstep_accepted;
    uint64_t microstep_completed;
    uint64_t microstep_failed;
    uint64_t microstep_cancelled;
    uint64_t microstep_finalized;
    char *error_storage;
    turbo_mutex_t lock;
    const char *error;
    bool error_owned;
    uint64_t configuration_version;
    uint64_t macrosteps;
    uint64_t microsteps;
    uint64_t actions;
    bool closed;
    bool cancelled;
    bool done;
} cflow_statechart_instance_impl;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right)) return false;
    *out = left * right;
    return true;
}

static bool checked_accumulate(size_t value, size_t *total) {
    return checked_add(*total, value, total);
}

static bool bitset_bytes_for(size_t bit_count, size_t *out) {
    size_t rounded;
    return checked_add(bit_count, 7u, &rounded) && ((*out = rounded / 8u), true);
}

static bool align_size(size_t size, size_t alignment, size_t *out) {
    const size_t padding = alignment - 1u;
    return out != NULL && size != 0u && alignment != 0u &&
        (alignment & padding) == 0u && size <= SIZE_MAX - padding &&
        ((*out = (size + padding) & ~padding), true);
}

static cflow_statechart_runtime_status measure_event_storage(
    const cflow_statechart_impl *ir, size_t *out_stride) {
    size_t max_size = 0u, max_align = 1u, index;
    if (ir == NULL || out_stride == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    for (index = 0u; index < ir->event_count; ++index) {
        const cmeta_type_desc *type = ir->events[index].payload_type;
        if (!cmeta_type_desc_valid(type) || type->size == 0u ||
            type->align == 0u ||
            (type->align & (type->align - 1u)) != 0u ||
            type->align > _Alignof(cmeta_capture_storage) ||
            !cflow_value_storage_type_supported(type))
            return CFLOW_STATECHART_RUNTIME_UNSUPPORTED_TYPE;
        if (type->size > max_size) max_size = type->size;
        if (type->align > max_align) max_align = type->align;
    }
    if (ir->event_count == 0u) {
        *out_stride = 0u;
        return CFLOW_STATECHART_RUNTIME_OK;
    }
    return align_size(max_size, max_align, out_stride)
        ? CFLOW_STATECHART_RUNTIME_OK
        : CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
}

static bool bit_test(const unsigned char *bits, size_t index) {
    return (bits[index / 8u] & (unsigned char)(1u << (index % 8u))) != 0u;
}

static void bit_set(unsigned char *bits, size_t index) {
    bits[index / 8u] |= (unsigned char)(1u << (index % 8u));
}

static void bit_clear(unsigned char *bits, size_t index) {
    bits[index / 8u] &= (unsigned char)~(1u << (index % 8u));
}

static bool pseudo_kind(cflow_statechart_state_kind kind) {
    return kind == CFLOW_STATECHART_INITIAL ||
           kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
           kind == CFLOW_STATECHART_HISTORY_DEEP;
}

static bool leaf_kind(cflow_statechart_state_kind kind) {
    return kind == CFLOW_STATECHART_ATOMIC || kind == CFLOW_STATECHART_FINAL;
}

static cflow_statechart_runtime_status calculate_storage_requirements(
    const cflow_statechart_impl *ir, size_t internal_event_capacity,
    cflow_statechart_storage_requirements *out) {
    cflow_statechart_storage_requirements requirements = {0};
    size_t bitset_bytes, transition_bitset_bytes, state_index_bytes;
    size_t selection_exit_bytes, selection_id_bytes;
    size_t history_count = 0u;
    size_t history_table_bytes, one_binding_kind, index;
    size_t event_stride, event_slot_bytes, event_payload_bytes;
    cflow_statechart_runtime_status event_status;
    event_status = measure_event_storage(ir, &event_stride);
    if (event_status != CFLOW_STATECHART_RUNTIME_OK) return event_status;
    if (ir == NULL || out == NULL ||
        !bitset_bytes_for(ir->state_count, &bitset_bytes) ||
        !bitset_bytes_for(ir->transition_count, &transition_bitset_bytes) ||
        !checked_multiply(ir->state_count, sizeof(size_t),
                          &state_index_bytes) ||
        !checked_multiply(ir->state_count,
                          sizeof(cflow_statechart_transition_id),
                          &selection_id_bytes) ||
        !checked_multiply(ir->state_count, bitset_bytes,
                          &selection_exit_bytes))
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    for (index = 0u; index < ir->state_count; ++index) {
        const cflow_statechart_state_kind kind = ir->states[index].kind;
        if (kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
            kind == CFLOW_STATECHART_HISTORY_DEEP)
            ++history_count;
    }

    requirements.control_bytes = sizeof(cflow_statechart_instance_impl);
    if (!checked_multiply(ir->guard_count,
                          sizeof(cflow_statechart_guard_binding),
                          &requirements.binding_bytes) ||
        !checked_multiply(ir->executable_count,
                          sizeof(cflow_statechart_executable_binding),
                          &one_binding_kind) ||
        !checked_accumulate(one_binding_kind, &requirements.binding_bytes) ||
        !checked_add(bitset_bytes, state_index_bytes,
                     &requirements.configuration_bytes) ||
        !checked_multiply(requirements.configuration_bytes, 2u,
                          &requirements.configuration_bytes) ||
        !checked_multiply(history_count, bitset_bytes,
                          &history_table_bytes) ||
        !checked_multiply(history_table_bytes, 2u,
                          &requirements.history_bitset_bytes) ||
        !checked_multiply(history_count, sizeof(size_t),
                          &requirements.history_count_bytes) ||
        !checked_multiply(requirements.history_count_bytes, 2u,
                          &requirements.history_count_bytes) ||
        !checked_multiply(ir->state_type->size, 2u,
                          &requirements.extended_state_bytes) ||
        !checked_multiply(state_index_bytes, 10u,
                          &requirements.index_work_bytes) ||
        !checked_accumulate(selection_id_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate(selection_exit_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate(selection_exit_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate(bitset_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate(bitset_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate(bitset_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate(transition_bitset_bytes,
                            &requirements.index_work_bytes) ||
        !checked_accumulate((size_t)CFLOW_STATECHART_ERROR_CAPACITY,
                            &requirements.index_work_bytes))
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;

    requirements.action_scratch_bytes = ir->state_type->size;
    if (ir->event_count != 0u) {
        if (!checked_multiply(internal_event_capacity,
                              sizeof(statechart_internal_event_slot),
                              &event_slot_bytes) ||
            !checked_multiply(internal_event_capacity, event_stride,
                              &event_payload_bytes) ||
            !checked_multiply(event_slot_bytes, 2u,
                              &requirements.internal_event_bytes) ||
            !checked_multiply(event_payload_bytes, 2u,
                              &one_binding_kind) ||
            !checked_accumulate(one_binding_kind,
                                &requirements.internal_event_bytes) ||
            !checked_accumulate(event_stride,
                                &requirements.internal_event_bytes))
            return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    }

    requirements.total_bytes = requirements.control_bytes;
    if (!checked_accumulate(requirements.binding_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.configuration_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.history_bitset_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.history_count_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.extended_state_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.index_work_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.action_scratch_bytes,
                            &requirements.total_bytes) ||
        !checked_accumulate(requirements.internal_event_bytes,
                            &requirements.total_bytes))
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    *out = requirements;
    return CFLOW_STATECHART_RUNTIME_OK;
}

cflow_statechart_runtime_status
cflow_statechart_instance_storage_requirements_internal(
    const cflow_statechart *statechart,
    cflow_statechart_storage_requirements *out) {
    const cflow_statechart_impl *ir = cflow_statechart_internal_get(statechart);
    if (ir == NULL || out == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    return calculate_storage_requirements(
        ir, (size_t)CFLOW_STATECHART_DEFAULT_INTERNAL_EVENT_CAPACITY, out);
}

static size_t find_state_index(const cflow_statechart_impl *ir,
                               cflow_machine_state_id id) {
    size_t left = 0u;
    size_t right = ir->state_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_state_id current = ir->states[middle].id;
        if (current == id) return middle;
        if (current < id) left = middle + 1u;
        else right = middle;
    }
    return SIZE_MAX;
}

static int compare_guard_binding(const void *left, const void *right) {
    const cflow_statechart_guard_binding *a =
        (const cflow_statechart_guard_binding *)left;
    const cflow_statechart_guard_binding *b =
        (const cflow_statechart_guard_binding *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_executable_binding(const void *left, const void *right) {
    const cflow_statechart_executable_binding *a =
        (const cflow_statechart_executable_binding *)left;
    const cflow_statechart_executable_binding *b =
        (const cflow_statechart_executable_binding *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static void instance_impl_free(cflow_statechart_instance_impl *impl) {
    size_t index;
    if (impl == NULL) return;
    if (impl->lock != NULL) turbo_mutex_destroy(&impl->lock);
    if (impl->error_owned) free((void *)impl->error);
    free(impl->internal_event_payloads);
    free(impl->internal_event_slots);
    free(impl->staged_event_payloads);
    free(impl->staged_event_slots);
    free(impl->action_scratch);
    free(impl->entry_bits);
    free(impl->exit_union);
    free(impl->request_event_payload);
    free(impl->request_exit_sets);
    free(impl->request_transition_indices);
    free(impl->error_storage);
    free(impl->candidate_seen);
    free(impl->candidate_exit_set);
    free(impl->selected_exit_sets);
    free(impl->selected_leaf_orders);
    free(impl->selected_sources);
    free(impl->selected_transition_indices);
    free(impl->selected_transition_ids);
    free(impl->path_stack);
    free(impl->entry_stack);
    free(impl->pseudo_transition_order);
    free(impl->entry_order);
    free(impl->exit_order);
    for (index = 0u; index < 2u; ++index) {
        free(impl->extended_states[index]);
        free(impl->history_counts[index]);
        free(impl->history_bits[index]);
        free(impl->configurations[index].states);
        free(impl->configurations[index].bits);
    }
    free(impl->history_slots);
    free(impl->executables);
    free(impl->guards);
    free(impl);
}

static bool binding_rows_shape_valid(
    const cflow_statechart_impl *ir,
    const cflow_statechart_instance_config *config) {
    return config->guard_count == ir->guard_count &&
        config->executable_count == ir->executable_count &&
        (config->guard_count == 0u || config->guards != NULL) &&
        (config->executable_count == 0u || config->executables != NULL);
}

static cflow_statechart_runtime_status copy_bindings(
    cflow_statechart_instance_impl *impl,
    const cflow_statechart_instance_config *config) {
    size_t index, bytes;
    if (!binding_rows_shape_valid(impl->ir, config))
        return CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH;

    if (config->guard_count != 0u) {
        if (!checked_multiply(config->guard_count, sizeof(*impl->guards),
                              &bytes))
            return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
        impl->guards = (cflow_statechart_guard_binding *)malloc(bytes);
        if (impl->guards == NULL)
            return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
        memcpy(impl->guards, config->guards, bytes);
        if (config->guard_count > 1u)
            qsort(impl->guards, config->guard_count, sizeof(*impl->guards),
                  compare_guard_binding);
        for (index = 0u; index < config->guard_count; ++index) {
            if (impl->guards[index].id != impl->ir->guards[index].id ||
                impl->guards[index].fn == NULL)
                return CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH;
        }
    }

    if (config->executable_count != 0u) {
        if (!checked_multiply(config->executable_count,
                              sizeof(*impl->executables), &bytes))
            return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
        impl->executables =
            (cflow_statechart_executable_binding *)malloc(bytes);
        if (impl->executables == NULL)
            return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
        memcpy(impl->executables, config->executables, bytes);
        if (config->executable_count > 1u)
            qsort(impl->executables, config->executable_count,
                  sizeof(*impl->executables), compare_executable_binding);
        for (index = 0u; index < config->executable_count; ++index) {
            if (impl->executables[index].id !=
                    impl->ir->executables[index].id ||
                impl->executables[index].fn == NULL)
                return CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH;
        }
    }
    impl->guard_count = config->guard_count;
    impl->executable_count = config->executable_count;
    return CFLOW_STATECHART_RUNTIME_OK;
}

static cflow_statechart_configuration_status validate_dense_configuration(
    const cflow_statechart_impl *ir,
    const unsigned char *bits,
    const size_t *states,
    size_t state_count) {
    size_t position, leaf_count = 0u;
    if (ir == NULL || bits == NULL || states == NULL || state_count == 0u ||
        !bit_test(bits, ir->root))
        return CFLOW_STATECHART_CONFIGURATION_INVALID_ARGUMENT;

    for (position = 0u; position < state_count; ++position) {
        const size_t state = states[position];
        const size_t parent = state < ir->state_count
            ? ir->parents[state] : SIZE_MAX;
        if (state >= ir->state_count)
            return CFLOW_STATECHART_CONFIGURATION_UNKNOWN_STATE;
        if (pseudo_kind(ir->states[state].kind))
            return CFLOW_STATECHART_CONFIGURATION_PSEUDO_STATE;
        if (!bit_test(bits, state))
            return CFLOW_STATECHART_CONFIGURATION_INVALID_ARGUMENT;
        if (position != 0u &&
            ir->states[states[position - 1u]].document_order >=
                ir->states[state].document_order)
            return states[position - 1u] == state
                ? CFLOW_STATECHART_CONFIGURATION_DUPLICATE_STATE
                : CFLOW_STATECHART_CONFIGURATION_INVALID_ORDER;
        if (parent != SIZE_MAX && !bit_test(bits, parent))
            return CFLOW_STATECHART_CONFIGURATION_MISSING_ANCESTOR;
        if (leaf_kind(ir->states[state].kind)) ++leaf_count;
    }

    for (position = 0u; position < state_count; ++position) {
        const size_t state = states[position];
        const cflow_statechart_state_kind kind = ir->states[state].kind;
        size_t child, active_real_children = 0u, real_children = 0u;
        if (kind != CFLOW_STATECHART_COMPOUND &&
            kind != CFLOW_STATECHART_PARALLEL)
            continue;
        for (child = ir->child_offsets[state];
             child < ir->child_offsets[state + 1u]; ++child) {
            const size_t child_state = ir->children[child];
            if (pseudo_kind(ir->states[child_state].kind)) continue;
            ++real_children;
            if (bit_test(bits, child_state)) ++active_real_children;
        }
        if ((kind == CFLOW_STATECHART_COMPOUND &&
             active_real_children != 1u) ||
            (kind == CFLOW_STATECHART_PARALLEL &&
             active_real_children != real_children))
            return CFLOW_STATECHART_CONFIGURATION_WRONG_CHILD;
    }
    return leaf_count != 0u
        ? CFLOW_STATECHART_CONFIGURATION_OK
        : CFLOW_STATECHART_CONFIGURATION_MISSING_LEAF;
}

cflow_statechart_configuration_status
cflow_statechart_configuration_validate_internal(
    const cflow_statechart *statechart,
    const cflow_machine_state_id *states,
    size_t state_count,
    unsigned char *scratch,
    size_t scratch_capacity) {
    const cflow_statechart_impl *ir = cflow_statechart_internal_get(statechart);
    size_t required, index;
    if (ir == NULL || states == NULL || scratch == NULL || state_count == 0u ||
        !bitset_bytes_for(ir->state_count, &required) ||
        scratch_capacity < required)
        return CFLOW_STATECHART_CONFIGURATION_INVALID_ARGUMENT;
    memset(scratch, 0, (ir->state_count + 7u) / 8u);
    for (index = 0u; index < state_count; ++index) {
        const size_t state = find_state_index(ir, states[index]);
        if (state == SIZE_MAX)
            return CFLOW_STATECHART_CONFIGURATION_UNKNOWN_STATE;
        if (pseudo_kind(ir->states[state].kind))
            return CFLOW_STATECHART_CONFIGURATION_PSEUDO_STATE;
        if (bit_test(scratch, state))
            return CFLOW_STATECHART_CONFIGURATION_DUPLICATE_STATE;
        if (index != 0u &&
            ir->states[find_state_index(ir, states[index - 1u])].document_order >=
                ir->states[state].document_order)
            return CFLOW_STATECHART_CONFIGURATION_INVALID_ORDER;
        bit_set(scratch, state);
    }
    if (!bit_test(scratch, ir->root))
        return CFLOW_STATECHART_CONFIGURATION_MISSING_ANCESTOR;
    for (index = 0u; index < state_count; ++index) {
        const size_t state = find_state_index(ir, states[index]);
        const size_t parent = ir->parents[state];
        if (parent != SIZE_MAX && !bit_test(scratch, parent))
            return CFLOW_STATECHART_CONFIGURATION_MISSING_ANCESTOR;
    }
    {
        size_t leaf_count = 0u;
        for (index = 0u; index < state_count; ++index) {
            const size_t state = find_state_index(ir, states[index]);
            const cflow_statechart_state_kind kind = ir->states[state].kind;
            size_t child, active_real_children = 0u, real_children = 0u;
            if (leaf_kind(kind)) ++leaf_count;
            if (kind != CFLOW_STATECHART_COMPOUND &&
                kind != CFLOW_STATECHART_PARALLEL)
                continue;
            for (child = ir->child_offsets[state];
                 child < ir->child_offsets[state + 1u]; ++child) {
                const size_t child_state = ir->children[child];
                if (pseudo_kind(ir->states[child_state].kind)) continue;
                ++real_children;
                if (bit_test(scratch, child_state)) ++active_real_children;
            }
            if ((kind == CFLOW_STATECHART_COMPOUND &&
                 active_real_children != 1u) ||
                (kind == CFLOW_STATECHART_PARALLEL &&
                 active_real_children != real_children))
                return CFLOW_STATECHART_CONFIGURATION_WRONG_CHILD;
        }
        return leaf_count != 0u
            ? CFLOW_STATECHART_CONFIGURATION_OK
            : CFLOW_STATECHART_CONFIGURATION_MISSING_LEAF;
    }
}

static cflow_statechart_runtime_status allocate_storage(
    cflow_statechart_instance_impl *impl,
    const cflow_statechart_storage_requirements *requirements) {
    size_t state_bytes, transition_id_bytes, selected_exit_bytes;
    size_t transition_bitset_bytes, history_bytes, history_count_bytes;
    size_t index, history_slot = 0u;
    if (requirements == NULL ||
        !bitset_bytes_for(impl->ir->state_count, &impl->bitset_bytes) ||
        !checked_multiply(impl->ir->state_count, sizeof(size_t),
                          &state_bytes) ||
        !checked_multiply(impl->ir->state_count,
                          sizeof(cflow_statechart_transition_id),
                          &transition_id_bytes) ||
        !checked_multiply(impl->ir->state_count, impl->bitset_bytes,
                          &selected_exit_bytes) ||
        !bitset_bytes_for(impl->ir->transition_count,
                          &transition_bitset_bytes))
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    for (index = 0u; index < impl->ir->state_count; ++index) {
        const cflow_statechart_state_kind kind = impl->ir->states[index].kind;
        if (kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
            kind == CFLOW_STATECHART_HISTORY_DEEP)
            ++impl->history_count;
    }
    history_bytes = requirements->history_bitset_bytes / 2u;
    history_count_bytes = requirements->history_count_bytes / 2u;

    impl->history_slots = (size_t *)malloc(state_bytes);
    impl->entry_stack = (size_t *)malloc(state_bytes);
    impl->path_stack = (size_t *)malloc(state_bytes);
    impl->exit_order = (size_t *)malloc(state_bytes);
    impl->entry_order = (size_t *)malloc(state_bytes);
    impl->pseudo_transition_order = (size_t *)malloc(state_bytes);
    impl->selected_transition_ids =
        (cflow_statechart_transition_id *)malloc(transition_id_bytes);
    impl->selected_transition_indices = (size_t *)malloc(state_bytes);
    impl->selected_sources = (size_t *)malloc(state_bytes);
    impl->selected_leaf_orders = (size_t *)malloc(state_bytes);
    impl->selected_exit_sets =
        (unsigned char *)calloc(1u, selected_exit_bytes);
    impl->candidate_exit_set =
        (unsigned char *)calloc(1u, impl->bitset_bytes);
    impl->request_transition_indices = (size_t *)malloc(state_bytes);
    impl->request_exit_sets =
        (unsigned char *)calloc(1u, selected_exit_bytes);
    impl->exit_union = (unsigned char *)calloc(1u, impl->bitset_bytes);
    impl->entry_bits = (unsigned char *)calloc(1u, impl->bitset_bytes);
    impl->action_scratch = (unsigned char *)malloc(
        requirements->action_scratch_bytes);
    impl->candidate_seen =
        transition_bitset_bytes != 0u
            ? (unsigned char *)calloc(1u, transition_bitset_bytes)
            : NULL;
    impl->error_storage =
        (char *)malloc((size_t)CFLOW_STATECHART_ERROR_CAPACITY);
    if (impl->ir->event_count != 0u) {
        size_t slot_bytes, payload_bytes;
        if (measure_event_storage(impl->ir, &impl->event_payload_stride) !=
                CFLOW_STATECHART_RUNTIME_OK ||
            !checked_multiply(impl->internal_event_capacity,
                              sizeof(*impl->staged_event_slots),
                              &slot_bytes) ||
            !checked_multiply(impl->internal_event_capacity,
                              impl->event_payload_stride, &payload_bytes))
            return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
        impl->staged_event_slots =
            (statechart_internal_event_slot *)malloc(slot_bytes);
        impl->internal_event_slots =
            (statechart_internal_event_slot *)malloc(slot_bytes);
        impl->staged_event_payloads =
            (unsigned char *)malloc(payload_bytes);
        impl->internal_event_payloads =
            (unsigned char *)malloc(payload_bytes);
        impl->request_event_payload =
            (unsigned char *)malloc(impl->event_payload_stride);
    }
    for (index = 0u; index < 2u; ++index) {
        impl->configurations[index].bits =
            (unsigned char *)calloc(1u, impl->bitset_bytes);
        impl->configurations[index].states = (size_t *)malloc(state_bytes);
        impl->extended_states[index] =
            (unsigned char *)malloc(impl->ir->state_type->size);
        if (impl->history_count != 0u) {
            impl->history_bits[index] =
                (unsigned char *)calloc(1u, history_bytes);
            impl->history_counts[index] =
                (size_t *)calloc(1u, history_count_bytes);
        }
    }
    if (impl->history_slots == NULL || impl->entry_stack == NULL ||
        impl->path_stack == NULL || impl->exit_order == NULL ||
        impl->entry_order == NULL || impl->pseudo_transition_order == NULL ||
        impl->selected_transition_ids == NULL ||
        impl->selected_transition_indices == NULL ||
        impl->selected_sources == NULL || impl->selected_leaf_orders == NULL ||
        impl->selected_exit_sets == NULL || impl->candidate_exit_set == NULL ||
        impl->request_transition_indices == NULL ||
        impl->request_exit_sets == NULL || impl->exit_union == NULL ||
        impl->entry_bits == NULL || impl->action_scratch == NULL ||
        (transition_bitset_bytes != 0u && impl->candidate_seen == NULL) ||
        impl->error_storage == NULL ||
        (impl->ir->event_count != 0u &&
         (impl->staged_event_slots == NULL ||
          impl->internal_event_slots == NULL ||
          impl->staged_event_payloads == NULL ||
          impl->internal_event_payloads == NULL ||
          impl->request_event_payload == NULL)) ||
        impl->configurations[0].bits == NULL ||
        impl->configurations[1].bits == NULL ||
        impl->configurations[0].states == NULL ||
        impl->configurations[1].states == NULL ||
        impl->extended_states[0] == NULL ||
        impl->extended_states[1] == NULL ||
        (impl->history_count != 0u &&
         (impl->history_bits[0] == NULL || impl->history_bits[1] == NULL ||
          impl->history_counts[0] == NULL ||
          impl->history_counts[1] == NULL)))
        return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    for (index = 0u; index < impl->ir->state_count; ++index) {
        const cflow_statechart_state_kind kind = impl->ir->states[index].kind;
        impl->history_slots[index] = SIZE_MAX;
        if (kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
            kind == CFLOW_STATECHART_HISTORY_DEEP)
            impl->history_slots[index] = history_slot++;
    }
    return CFLOW_STATECHART_RUNTIME_OK;
}

static bool activate_state(cflow_statechart_instance_impl *impl,
                           statechart_configuration_buffer *configuration,
                           size_t state, size_t *stack_count) {
    if (state >= impl->ir->state_count ||
        pseudo_kind(impl->ir->states[state].kind))
        return false;
    if (bit_test(configuration->bits, state)) return true;
    if (*stack_count >= impl->ir->state_count) return false;
    bit_set(configuration->bits, state);
    impl->entry_stack[(*stack_count)++] = state;
    return true;
}

static bool activate_target_path(
    cflow_statechart_instance_impl *impl,
    statechart_configuration_buffer *configuration,
    size_t compound,
    size_t target,
    size_t *stack_count) {
    size_t path_count = 0u, current = target;
    while (current != compound) {
        if (current == SIZE_MAX || path_count >= impl->ir->state_count ||
            pseudo_kind(impl->ir->states[current].kind))
            return false;
        impl->path_stack[path_count++] = current;
        current = impl->ir->parents[current];
    }
    if (path_count == 0u) return false;
    while (path_count != 0u) {
        if (!activate_state(impl, configuration,
                            impl->path_stack[--path_count], stack_count))
            return false;
    }
    return true;
}

static cflow_statechart_runtime_status build_initial_configuration(
    cflow_statechart_instance_impl *impl, size_t staged) {
    statechart_configuration_buffer *configuration =
        &impl->configurations[staged];
    size_t stack_count = 0u, index;
    cflow_statechart_configuration_status configuration_status;
    memset(configuration->bits, 0, impl->bitset_bytes);
    configuration->state_count = 0u;
    if (!activate_state(impl, configuration, impl->ir->root, &stack_count))
        return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;

    while (stack_count != 0u) {
        const size_t state = impl->entry_stack[--stack_count];
        const cflow_statechart_state_kind kind = impl->ir->states[state].kind;
        size_t child;
        if (kind == CFLOW_STATECHART_COMPOUND) {
            size_t active_child_count = 0u;
            size_t initial = SIZE_MAX;
            for (child = impl->ir->child_offsets[state];
                 child < impl->ir->child_offsets[state + 1u]; ++child) {
                const size_t child_state = impl->ir->children[child];
                if (impl->ir->states[child_state].kind ==
                    CFLOW_STATECHART_INITIAL)
                    initial = child_state;
                else if (!pseudo_kind(impl->ir->states[child_state].kind) &&
                         bit_test(configuration->bits, child_state))
                    ++active_child_count;
            }
            if (active_child_count > 1u || initial == SIZE_MAX)
                return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
            if (active_child_count == 0u &&
                !activate_target_path(
                    impl, configuration, state,
                    impl->ir->default_target_indices[initial], &stack_count))
                return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
        } else if (kind == CFLOW_STATECHART_PARALLEL) {
            for (child = impl->ir->child_offsets[state];
                 child < impl->ir->child_offsets[state + 1u]; ++child) {
                const size_t child_state = impl->ir->children[child];
                if (!pseudo_kind(impl->ir->states[child_state].kind) &&
                    !activate_state(impl, configuration, child_state,
                                    &stack_count))
                    return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
            }
        }
    }

    for (index = 0u; index < impl->ir->state_count; ++index) {
        const size_t state = impl->ir->document_order_indices[index];
        if (bit_test(configuration->bits, state))
            configuration->states[configuration->state_count++] = state;
    }
    configuration_status = validate_dense_configuration(
        impl->ir, configuration->bits, configuration->states,
        configuration->state_count);
    return configuration_status == CFLOW_STATECHART_CONFIGURATION_OK
        ? CFLOW_STATECHART_RUNTIME_OK
        : CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
}

static const cflow_event_type *find_event_type(
    const cflow_statechart_impl *ir, cflow_event_id id) {
    size_t left = 0u, right = ir->event_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (ir->events[middle].id == id) return &ir->events[middle];
        if (ir->events[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_statechart_guard *find_guard_declaration(
    const cflow_statechart_instance_impl *impl,
    cflow_statechart_guard_id id) {
    size_t left = 0u, right = impl->ir->guard_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->ir->guards[middle].id == id)
            return &impl->ir->guards[middle];
        if (impl->ir->guards[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_statechart_guard_binding *find_statechart_guard_binding(
    const cflow_statechart_instance_impl *impl,
    cflow_statechart_guard_id id) {
    size_t left = 0u, right = impl->guard_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->guards[middle].id == id) return &impl->guards[middle];
        if (impl->guards[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static void preserve_first_error(cflow_statechart_instance_impl *impl,
                                 const char *message) {
    const char *source = message != NULL && message[0] != '\0'
        ? message : "Statechart guard failed";
    size_t length;
    turbo_mutex_lock(&impl->lock);
    if (impl->error != NULL) {
        turbo_mutex_unlock(&impl->lock);
        return;
    }
    length = strlen(source);
    if (length >= (size_t)CFLOW_STATECHART_ERROR_CAPACITY)
        length = (size_t)CFLOW_STATECHART_ERROR_CAPACITY - 1u;
    memcpy(impl->error_storage, source, length);
    impl->error_storage[length] = '\0';
    impl->error = impl->error_storage;
    turbo_mutex_unlock(&impl->lock);
}

static bool instance_has_error(cflow_statechart_instance_impl *impl) {
    bool has_error;
    turbo_mutex_lock(&impl->lock);
    has_error = impl->error != NULL;
    turbo_mutex_unlock(&impl->lock);
    return has_error;
}

static bool instance_microstep_pending(cflow_statechart_instance_impl *impl) {
    bool pending;
    turbo_mutex_lock(&impl->lock);
    pending = impl->microstep_pending;
    turbo_mutex_unlock(&impl->lock);
    return pending;
}

static bool source_is_proper_descendant(const cflow_statechart_impl *ir,
                                        size_t descendant,
                                        size_t ancestor) {
    size_t current = descendant;
    if (descendant == ancestor) return false;
    while (current != SIZE_MAX) {
        current = ir->parents[current];
        if (current == ancestor) return true;
    }
    return false;
}

static bool trigger_valid(const cflow_statechart_instance_impl *impl,
                          const cflow_statechart_selection_trigger *trigger) {
    const cflow_event_type *event_type;
    if (trigger->kind == CFLOW_STATECHART_TRIGGER_EVENTLESS)
        return trigger->event == NULL && trigger->completion == 0u;
    if (trigger->kind == CFLOW_STATECHART_TRIGGER_COMPLETION) {
        const size_t completed = find_state_index(
            impl->ir, trigger->completion);
        return trigger->event == NULL && trigger->completion != 0u &&
            completed != SIZE_MAX &&
            cflow_statechart_internal_state_can_complete(
                impl->ir, completed);
    }
    if (trigger->kind != CFLOW_STATECHART_TRIGGER_EVENT ||
        trigger->event == NULL || trigger->completion != 0u ||
        trigger->event->id == 0u || trigger->event->payload_type == NULL ||
        trigger->event->payload == NULL)
        return false;
    event_type = find_event_type(impl->ir, trigger->event->id);
    return event_type != NULL &&
        cmeta_type_equal(event_type->payload_type,
                         trigger->event->payload_type);
}

static bool trigger_matches(const cflow_statechart_transition *transition,
                            const cflow_statechart_selection_trigger *trigger) {
    if (transition->trigger != trigger->kind) return false;
    if (trigger->kind == CFLOW_STATECHART_TRIGGER_EVENT)
        return transition->event == trigger->event->id;
    if (trigger->kind == CFLOW_STATECHART_TRIGGER_COMPLETION)
        return transition->completion == trigger->completion;
    return true;
}

static cflow_statechart_runtime_status guard_enabled(
    cflow_statechart_instance_impl *impl,
    const cflow_statechart_transition *transition,
    const cflow_statechart_selection_trigger *trigger,
    bool *out_enabled) {
    const cflow_statechart_guard *declaration;
    const cflow_statechart_guard_binding *binding;
    const char *error = NULL;
    bool enabled = false;
    if (transition->guard == 0u) {
        *out_enabled = true;
        return CFLOW_STATECHART_RUNTIME_OK;
    }
    declaration = find_guard_declaration(impl, transition->guard);
    binding = find_statechart_guard_binding(impl, transition->guard);
    if (declaration == NULL || binding == NULL) {
        preserve_first_error(impl, "Statechart guard binding is missing");
        return CFLOW_STATECHART_RUNTIME_GUARD_FAILED;
    }
    if (!binding->fn(binding->user,
                     impl->extended_states[impl->published],
                     trigger->kind == CFLOW_STATECHART_TRIGGER_EVENT
                         ? trigger->event : NULL,
                     &enabled, &error)) {
        preserve_first_error(
            impl, (declaration->effects & CMETA_EFFECT_MAY_FAIL) != 0u
                ? error : "Statechart guard contract violation");
        return CFLOW_STATECHART_RUNTIME_GUARD_FAILED;
    }
    *out_enabled = enabled;
    return CFLOW_STATECHART_RUNTIME_OK;
}

static void compute_exit_set(cflow_statechart_instance_impl *impl,
                             size_t transition_index) {
    const cflow_statechart_transition *transition =
        &impl->ir->transitions[transition_index];
    const statechart_configuration_buffer *configuration =
        &impl->configurations[impl->published];
    const size_t domain = impl->ir->transition_domains[transition_index];
    size_t position;
    memset(impl->candidate_exit_set, 0, impl->bitset_bytes);
    if (transition->target == 0u) return;
    for (position = 0u; position < configuration->state_count; ++position) {
        const size_t state = configuration->states[position];
        if (domain == SIZE_MAX ||
            source_is_proper_descendant(impl->ir, state, domain))
            bit_set(impl->candidate_exit_set, state);
    }
}

static bool exit_sets_intersect(const unsigned char *left,
                                const unsigned char *right,
                                size_t bytes) {
    size_t index;
    for (index = 0u; index < bytes; ++index)
        if ((left[index] & right[index]) != 0u) return true;
    return false;
}

static void remove_selected(cflow_statechart_instance_impl *impl,
                            size_t position) {
    const size_t tail = impl->selected_count - position - 1u;
    if (tail != 0u) {
        memmove(&impl->selected_transition_ids[position],
                &impl->selected_transition_ids[position + 1u],
                tail * sizeof(*impl->selected_transition_ids));
        memmove(&impl->selected_transition_indices[position],
                &impl->selected_transition_indices[position + 1u],
                tail * sizeof(*impl->selected_transition_indices));
        memmove(&impl->selected_sources[position],
                &impl->selected_sources[position + 1u],
                tail * sizeof(*impl->selected_sources));
        memmove(&impl->selected_leaf_orders[position],
                &impl->selected_leaf_orders[position + 1u],
                tail * sizeof(*impl->selected_leaf_orders));
        memmove(impl->selected_exit_sets + position * impl->bitset_bytes,
                impl->selected_exit_sets +
                    (position + 1u) * impl->bitset_bytes,
                tail * impl->bitset_bytes);
    }
    --impl->selected_count;
}

static void filter_candidate(cflow_statechart_instance_impl *impl,
                             size_t transition_index,
                             size_t source,
                             size_t leaf_order) {
    size_t position;
    bool accepted = true;
    for (position = 0u; position < impl->selected_count; ++position) {
        const unsigned char *selected_exit = impl->selected_exit_sets +
            position * impl->bitset_bytes;
        if (!exit_sets_intersect(impl->candidate_exit_set, selected_exit,
                                 impl->bitset_bytes))
            continue;
        if (!source_is_proper_descendant(
                impl->ir, source, impl->selected_sources[position])) {
            accepted = false;
            break;
        }
    }
    if (!accepted) return;
    position = 0u;
    while (position < impl->selected_count) {
        const unsigned char *selected_exit = impl->selected_exit_sets +
            position * impl->bitset_bytes;
        if (exit_sets_intersect(impl->candidate_exit_set, selected_exit,
                                impl->bitset_bytes))
            remove_selected(impl, position);
        else
            ++position;
    }
    position = impl->selected_count++;
    impl->selected_transition_ids[position] =
        impl->ir->transitions[transition_index].id;
    impl->selected_transition_indices[position] = transition_index;
    impl->selected_sources[position] = source;
    impl->selected_leaf_orders[position] = leaf_order;
    memcpy(impl->selected_exit_sets + position * impl->bitset_bytes,
           impl->candidate_exit_set, impl->bitset_bytes);
}

cflow_statechart_runtime_status cflow_statechart_instance_select_internal(
    cflow_statechart_instance *instance,
    const cflow_statechart_selection_trigger *trigger,
    cflow_statechart_selection_snapshot *out) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    const statechart_configuration_buffer *configuration;
    size_t position, leaf_order = 0u;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (impl == NULL || trigger == NULL || out == NULL ||
        !trigger_valid(impl, trigger))
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    if (instance_has_error(impl))
        return CFLOW_STATECHART_RUNTIME_GUARD_FAILED;
    if (instance_microstep_pending(impl))
        return CFLOW_STATECHART_RUNTIME_WOULD_BLOCK;
    impl->selected_count = 0u;
    if (impl->ir->transition_count != 0u)
        memset(impl->candidate_seen, 0,
               (impl->ir->transition_count + 7u) / 8u);
    configuration = &impl->configurations[impl->published];
    for (position = 0u; position < configuration->state_count; ++position) {
        size_t source, span;
        const size_t leaf = configuration->states[position];
        bool candidate_found = false;
        if (!leaf_kind(impl->ir->states[leaf].kind)) continue;
        source = leaf;
        while (source != SIZE_MAX && !candidate_found) {
            for (span = impl->ir->transition_offsets[source];
                 span < impl->ir->transition_offsets[source + 1u]; ++span) {
                const size_t transition_index =
                    impl->ir->transition_indices[span];
                const cflow_statechart_transition *transition =
                    &impl->ir->transitions[transition_index];
                cflow_statechart_runtime_status status;
                bool enabled = false;
                if (!trigger_matches(transition, trigger)) continue;
                status = guard_enabled(impl, transition, trigger, &enabled);
                if (status != CFLOW_STATECHART_RUNTIME_OK) {
                    impl->selected_count = 0u;
                    return status;
                }
                if (!enabled) continue;
                candidate_found = true;
                if (!bit_test(impl->candidate_seen, transition_index)) {
                    bit_set(impl->candidate_seen, transition_index);
                    compute_exit_set(impl, transition_index);
                    filter_candidate(impl, transition_index, source,
                                     leaf_order);
                }
                break;
            }
            source = impl->ir->parents[source];
        }
        ++leaf_order;
    }
    out->transition_ids = impl->selected_transition_ids;
    out->transition_count = impl->selected_count;
    out->exit_sets = impl->selected_exit_sets;
    out->exit_set_stride = impl->bitset_bytes;
    return CFLOW_STATECHART_RUNTIME_OK;
}

bool cflow_statechart_selection_exits_internal(
    const cflow_statechart_instance *instance,
    const cflow_statechart_selection_snapshot *selection,
    size_t transition_position,
    cflow_machine_state_id state) {
    const cflow_statechart_instance_impl *impl = instance != NULL
        ? (const cflow_statechart_instance_impl *)instance->impl : NULL;
    size_t state_index;
    if (impl == NULL || selection == NULL ||
        selection->transition_ids != impl->selected_transition_ids ||
        selection->exit_sets != impl->selected_exit_sets ||
        selection->exit_set_stride != impl->bitset_bytes ||
        selection->transition_count != impl->selected_count ||
        transition_position >= selection->transition_count)
        return false;
    state_index = find_state_index(impl->ir, state);
    return state_index != SIZE_MAX && bit_test(
        selection->exit_sets + transition_position * selection->exit_set_stride,
        state_index);
}

static const cflow_statechart_executable *find_executable_declaration(
    const cflow_statechart_instance_impl *impl,
    cflow_statechart_executable_id id) {
    size_t left = 0u, right = impl->ir->executable_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->ir->executables[middle].id == id)
            return &impl->ir->executables[middle];
        if (impl->ir->executables[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_statechart_executable_binding *find_executable_binding(
    const cflow_statechart_instance_impl *impl,
    cflow_statechart_executable_id id) {
    size_t left = 0u, right = impl->executable_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->executables[middle].id == id)
            return &impl->executables[middle];
        if (impl->executables[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static size_t find_event_index(const cflow_statechart_impl *ir,
                               cflow_event_id id) {
    size_t left = 0u, right = ir->event_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (ir->events[middle].id == id) return middle;
        if (ir->events[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return SIZE_MAX;
}

typedef struct statechart_action_context {
    cflow_statechart_instance_impl *impl;
    const cflow_event_view *event;
    unsigned char *current_state;
    unsigned char *next_state;
    size_t invoked;
    cflow_statechart_runtime_status raise_status;
    const char *raise_error;
} statechart_action_context;

static bool stage_internal_event(void *user, const cflow_event_view *event,
                                 const char **out_error) {
    statechart_action_context *context =
        (statechart_action_context *)user;
    cflow_statechart_instance_impl *impl =
        context != NULL ? context->impl : NULL;
    size_t type_index;
    const cmeta_type_desc *type;
    if (out_error != NULL) *out_error = NULL;
    if (impl == NULL || event == NULL || out_error == NULL ||
        event->payload_type == NULL || event->payload == NULL) {
        if (context != NULL) {
            context->raise_status =
                CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_INVALID;
            context->raise_error = "Statechart internal event is invalid";
        }
        if (out_error != NULL)
            *out_error = "Statechart internal event is invalid";
        return false;
    }
    type_index = find_event_index(impl->ir, event->id);
    if (type_index == SIZE_MAX) {
        context->raise_status =
            CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_INVALID;
        context->raise_error = "Statechart internal event is unknown";
        *out_error = context->raise_error;
        return false;
    }
    type = impl->ir->events[type_index].payload_type;
    if (!cmeta_type_equal(type, event->payload_type)) {
        context->raise_status =
            CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_TYPE_MISMATCH;
        context->raise_error = "Statechart internal event type mismatch";
        *out_error = context->raise_error;
        return false;
    }
    if (impl->internal_event_count > impl->internal_event_capacity ||
        impl->staged_event_count >=
            impl->internal_event_capacity - impl->internal_event_count) {
        context->raise_status = CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL;
        context->raise_error = "Statechart internal event queue is full";
        *out_error = context->raise_error;
        return false;
    }
    impl->staged_event_slots[impl->staged_event_count].type_index = type_index;
    memcpy(impl->staged_event_payloads +
               impl->staged_event_count * impl->event_payload_stride,
           event->payload, type->size);
    ++impl->staged_event_count;
    return true;
}

static cflow_statechart_runtime_status invoke_executable(
    statechart_action_context *context,
    cflow_statechart_executable_id executable_id,
    cflow_statechart_action_phase phase,
    cflow_machine_state_id owner,
    const char **out_error) {
    cflow_statechart_instance_impl *impl = context->impl;
    const cflow_statechart_executable *declaration =
        find_executable_declaration(impl, executable_id);
    const cflow_statechart_executable_binding *binding =
        find_executable_binding(impl, executable_id);
    const char *callback_error = NULL;
    bool succeeded;
    unsigned char *previous;
    if (out_error != NULL) *out_error = NULL;
    if (declaration == NULL || binding == NULL ||
        !cflow_executor_is_current_internal(impl->executor)) {
        if (out_error != NULL)
            *out_error = "Statechart executable context is invalid";
        return CFLOW_STATECHART_RUNTIME_ACTION_FAILED;
    }
    context->raise_status = CFLOW_STATECHART_RUNTIME_OK;
    context->raise_error = NULL;
    succeeded = binding->fn(
        binding->user, phase, owner, context->current_state, context->event,
        context->next_state, stage_internal_event, context, &callback_error);
    ++context->invoked;
    if (context->raise_status != CFLOW_STATECHART_RUNTIME_OK) {
        if (out_error != NULL) *out_error = context->raise_error;
        return context->raise_status;
    }
    if (!succeeded) {
        if (out_error != NULL) {
            *out_error = (declaration->effects & CMETA_EFFECT_MAY_FAIL) != 0u
                ? (callback_error != NULL && callback_error[0] != '\0'
                       ? callback_error : "Statechart action failed")
                : "Statechart executable contract violation";
        }
        return CFLOW_STATECHART_RUNTIME_ACTION_FAILED;
    }
    previous = context->current_state;
    context->current_state = context->next_state;
    context->next_state = previous;
    return CFLOW_STATECHART_RUNTIME_OK;
}

static bool state_is_descendant_or_self(const cflow_statechart_impl *ir,
                                        size_t state, size_t ancestor) {
    size_t current = state;
    while (current != SIZE_MAX) {
        if (current == ancestor) return true;
        current = ir->parents[current];
    }
    return false;
}

static bool exit_before(const cflow_statechart_impl *ir,
                        size_t left, size_t right) {
    if (source_is_proper_descendant(ir, left, right)) return true;
    if (source_is_proper_descendant(ir, right, left)) return false;
    return ir->states[left].document_order >
        ir->states[right].document_order;
}

static bool entry_before(const cflow_statechart_impl *ir,
                         size_t left, size_t right) {
    if (source_is_proper_descendant(ir, right, left)) return true;
    if (source_is_proper_descendant(ir, left, right)) return false;
    return ir->states[left].document_order <
        ir->states[right].document_order;
}

static void stable_order_states(const cflow_statechart_impl *ir,
                                size_t *states, size_t count,
                                bool (*before)(const cflow_statechart_impl *,
                                               size_t, size_t)) {
    size_t index;
    for (index = 1u; index < count; ++index) {
        const size_t value = states[index];
        size_t position = index;
        while (position != 0u && before(ir, value, states[position - 1u])) {
            states[position] = states[position - 1u];
            --position;
        }
        states[position] = value;
    }
}

static void copy_staging_buffers(cflow_statechart_instance_impl *impl,
                                 size_t staged) {
    const size_t published = impl->published;
    const size_t history_bytes = impl->history_count * impl->bitset_bytes;
    const size_t history_count_bytes =
        impl->history_count * sizeof(*impl->history_counts[staged]);
    memcpy(impl->configurations[staged].bits,
           impl->configurations[published].bits, impl->bitset_bytes);
    memcpy(impl->configurations[staged].states,
           impl->configurations[published].states,
           impl->configurations[published].state_count * sizeof(size_t));
    impl->configurations[staged].state_count =
        impl->configurations[published].state_count;
    memcpy(impl->extended_states[staged], impl->extended_states[published],
           impl->ir->state_type->size);
    if (history_bytes != 0u) {
        memcpy(impl->history_bits[staged], impl->history_bits[published],
               history_bytes);
        memcpy(impl->history_counts[staged],
               impl->history_counts[published], history_count_bytes);
    }
    impl->staged_event_count = 0u;
}

static void compute_exit_union(cflow_statechart_instance_impl *impl,
                               size_t *out_count) {
    const statechart_configuration_buffer *published =
        &impl->configurations[impl->published];
    size_t transition, position;
    *out_count = 0u;
    memset(impl->exit_union, 0, impl->bitset_bytes);
    for (transition = 0u; transition < impl->request_transition_count;
         ++transition) {
        const unsigned char *exit_set = impl->request_exit_sets +
            transition * impl->bitset_bytes;
        size_t byte;
        for (byte = 0u; byte < impl->bitset_bytes; ++byte)
            impl->exit_union[byte] |= exit_set[byte];
    }
    for (position = 0u; position < published->state_count; ++position) {
        const size_t state = published->states[position];
        if (bit_test(impl->exit_union, state))
            impl->exit_order[(*out_count)++] = state;
    }
    stable_order_states(impl->ir, impl->exit_order, *out_count, exit_before);
}

static void save_affected_history(cflow_statechart_instance_impl *impl,
                                  size_t staged) {
    const statechart_configuration_buffer *published =
        &impl->configurations[impl->published];
    size_t history;
    for (history = 0u; history < impl->ir->state_count; ++history) {
        const cflow_statechart_state_kind kind = impl->ir->states[history].kind;
        const size_t parent = impl->ir->parents[history];
        const size_t slot = impl->history_slots[history];
        unsigned char *bits;
        size_t position, count = 0u;
        if (slot == SIZE_MAX || parent == SIZE_MAX ||
            !bit_test(impl->exit_union, parent))
            continue;
        bits = impl->history_bits[staged] + slot * impl->bitset_bytes;
        memset(bits, 0, impl->bitset_bytes);
        for (position = 0u; position < published->state_count; ++position) {
            const size_t state = published->states[position];
            const bool remember =
                kind == CFLOW_STATECHART_HISTORY_SHALLOW
                    ? impl->ir->parents[state] == parent &&
                          !pseudo_kind(impl->ir->states[state].kind)
                    : leaf_kind(impl->ir->states[state].kind) &&
                          source_is_proper_descendant(
                              impl->ir, state, parent);
            if (remember) {
                bit_set(bits, state);
                ++count;
            }
        }
        impl->history_counts[staged][slot] = count;
    }
}

static bool activate_staged(cflow_statechart_instance_impl *impl,
                            statechart_configuration_buffer *configuration,
                            size_t state, size_t *stack_count) {
    if (state >= impl->ir->state_count ||
        pseudo_kind(impl->ir->states[state].kind))
        return false;
    if (bit_test(configuration->bits, state)) return true;
    if (*stack_count >= impl->ir->state_count) return false;
    bit_set(configuration->bits, state);
    bit_set(impl->entry_bits, state);
    impl->entry_stack[(*stack_count)++] = state;
    return true;
}

static bool activate_path_to(cflow_statechart_instance_impl *impl,
                             statechart_configuration_buffer *configuration,
                             size_t target, size_t *stack_count) {
    size_t current = target, path_count = 0u;
    while (current != SIZE_MAX && !bit_test(configuration->bits, current)) {
        if (path_count >= impl->ir->state_count ||
            pseudo_kind(impl->ir->states[current].kind))
            return false;
        impl->path_stack[path_count++] = current;
        current = impl->ir->parents[current];
    }
    while (path_count != 0u) {
        if (!activate_staged(impl, configuration,
                             impl->path_stack[--path_count], stack_count))
            return false;
    }
    return true;
}

static bool collect_pseudo_transition(cflow_statechart_instance_impl *impl,
                                      size_t pseudo,
                                      size_t *pseudo_count) {
    const size_t transition = impl->ir->default_transition_indices[pseudo];
    if (transition == SIZE_MAX || *pseudo_count >= impl->ir->state_count)
        return false;
    impl->pseudo_transition_order[(*pseudo_count)++] = transition;
    return true;
}

static bool enter_default_descendants(
    cflow_statechart_instance_impl *impl,
    statechart_configuration_buffer *configuration,
    size_t *stack_count,
    size_t *pseudo_count) {
    while (*stack_count != 0u) {
        const size_t state = impl->entry_stack[--(*stack_count)];
        const cflow_statechart_state_kind kind = impl->ir->states[state].kind;
        size_t child;
        if (kind == CFLOW_STATECHART_COMPOUND) {
            size_t active_children = 0u, initial = SIZE_MAX;
            for (child = impl->ir->child_offsets[state];
                 child < impl->ir->child_offsets[state + 1u]; ++child) {
                const size_t child_state = impl->ir->children[child];
                if (impl->ir->states[child_state].kind ==
                    CFLOW_STATECHART_INITIAL)
                    initial = child_state;
                else if (!pseudo_kind(impl->ir->states[child_state].kind) &&
                         bit_test(configuration->bits, child_state))
                    ++active_children;
            }
            if (active_children > 1u || initial == SIZE_MAX) return false;
            if (active_children == 0u) {
                if (!collect_pseudo_transition(
                        impl, initial, pseudo_count) ||
                    !activate_path_to(
                        impl, configuration,
                        impl->ir->default_target_indices[initial],
                        stack_count))
                    return false;
            }
        } else if (kind == CFLOW_STATECHART_PARALLEL) {
            for (child = impl->ir->child_offsets[state];
                 child < impl->ir->child_offsets[state + 1u]; ++child) {
                const size_t child_state = impl->ir->children[child];
                if (!pseudo_kind(impl->ir->states[child_state].kind) &&
                    !activate_staged(
                        impl, configuration, child_state, stack_count))
                    return false;
            }
        }
    }
    return true;
}

static bool restore_history_target(cflow_statechart_instance_impl *impl,
                                   statechart_configuration_buffer *configuration,
                                   size_t staged, size_t history,
                                   size_t *stack_count,
                                   size_t *pseudo_count) {
    const size_t parent = impl->ir->parents[history];
    const size_t slot = impl->history_slots[history];
    const size_t count = impl->history_counts[staged][slot];
    const unsigned char *bits =
        impl->history_bits[staged] + slot * impl->bitset_bytes;
    size_t index;
    if (!activate_path_to(impl, configuration, parent, stack_count))
        return false;
    if (count == 0u) {
        if (!collect_pseudo_transition(impl, history, pseudo_count))
            return false;
        return activate_path_to(
            impl, configuration,
            impl->ir->default_target_indices[history], stack_count) &&
            enter_default_descendants(
                impl, configuration, stack_count, pseudo_count);
    }
    for (index = 0u; index < impl->ir->state_count; ++index) {
        const size_t state = impl->ir->document_order_indices[index];
        if (bit_test(bits, state) &&
            !activate_path_to(impl, configuration, state, stack_count))
            return false;
    }
    return enter_default_descendants(
        impl, configuration, stack_count, pseudo_count);
}

static bool build_target_configuration(
    cflow_statechart_instance_impl *impl, size_t staged,
    size_t *out_pseudo_count, size_t *out_entry_count) {
    statechart_configuration_buffer *configuration =
        &impl->configurations[staged];
    size_t transition, stack_count = 0u, pseudo_count = 0u, index;
    memset(impl->entry_bits, 0, impl->bitset_bytes);
    for (index = 0u; index < impl->ir->state_count; ++index) {
        if (bit_test(impl->exit_union, index))
            bit_clear(configuration->bits, index);
    }
    for (transition = 0u; transition < impl->request_transition_count;
         ++transition) {
        const cflow_statechart_transition *row =
            &impl->ir->transitions[impl->request_transition_indices[transition]];
        size_t target;
        if (row->target == 0u) continue;
        target = find_state_index(impl->ir, row->target);
        if (target == SIZE_MAX) return false;
        if (impl->ir->states[target].kind ==
                CFLOW_STATECHART_HISTORY_SHALLOW ||
            impl->ir->states[target].kind == CFLOW_STATECHART_HISTORY_DEEP) {
            if (!restore_history_target(
                    impl, configuration, staged, target,
                    &stack_count, &pseudo_count))
                return false;
        } else {
            if (!activate_path_to(
                    impl, configuration, target, &stack_count) ||
                !enter_default_descendants(
                    impl, configuration, &stack_count, &pseudo_count))
                return false;
        }
    }
    configuration->state_count = 0u;
    *out_entry_count = 0u;
    for (index = 0u; index < impl->ir->state_count; ++index) {
        const size_t state = impl->ir->document_order_indices[index];
        if (bit_test(configuration->bits, state))
            configuration->states[configuration->state_count++] = state;
        if (bit_test(impl->entry_bits, state))
            impl->entry_order[(*out_entry_count)++] = state;
    }
    stable_order_states(
        impl->ir, impl->entry_order, *out_entry_count, entry_before);
    *out_pseudo_count = pseudo_count;
    return validate_dense_configuration(
               impl->ir, configuration->bits, configuration->states,
               configuration->state_count) ==
        CFLOW_STATECHART_CONFIGURATION_OK;
}

static cflow_statechart_runtime_status run_state_action_span(
    statechart_action_context *context, size_t state,
    cflow_statechart_state_action_kind kind, const char **out_error) {
    cflow_statechart_instance_impl *impl = context->impl;
    const size_t bucket = state * 2u + (size_t)kind;
    size_t action;
    for (action = impl->ir->state_action_offsets[bucket];
         action < impl->ir->state_action_offsets[bucket + 1u]; ++action) {
        const cflow_statechart_state_action *row =
            &impl->ir->state_actions[impl->ir->state_action_indices[action]];
        const cflow_statechart_runtime_status status = invoke_executable(
            context, row->executable,
            kind == CFLOW_STATECHART_STATE_ACTION_EXIT
                ? CFLOW_STATECHART_ACTION_EXIT
                : CFLOW_STATECHART_ACTION_ENTRY,
            impl->ir->states[state].id, out_error);
        if (status != CFLOW_STATECHART_RUNTIME_OK) return status;
    }
    return CFLOW_STATECHART_RUNTIME_OK;
}

static cflow_statechart_runtime_status run_transition_action_span(
    statechart_action_context *context, size_t transition,
    cflow_statechart_action_phase phase, const char **out_error) {
    cflow_statechart_instance_impl *impl = context->impl;
    size_t action;
    for (action = impl->ir->transition_action_offsets[transition];
         action < impl->ir->transition_action_offsets[transition + 1u];
         ++action) {
        const cflow_statechart_transition_action *row =
            &impl->ir->transition_actions[
                impl->ir->transition_action_indices[action]];
        const cflow_statechart_runtime_status status = invoke_executable(
            context, row->executable, phase,
            impl->ir->transitions[transition].source, out_error);
        if (status != CFLOW_STATECHART_RUNTIME_OK) return status;
    }
    return CFLOW_STATECHART_RUNTIME_OK;
}

static void microstep_fail(cflow_statechart_instance_impl *impl,
                           cflow_statechart_runtime_status status,
                           const char *error) {
    preserve_first_error(
        impl, error != NULL && error[0] != '\0'
            ? error : "Statechart action failed");
    impl->staged_event_count = 0u;
    turbo_mutex_lock(&impl->lock);
    impl->microstep_result = status;
    if (impl->microstep_failed != UINT64_MAX) ++impl->microstep_failed;
    turbo_mutex_unlock(&impl->lock);
}

static void commit_internal_events(cflow_statechart_instance_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->staged_event_count; ++index) {
        const size_t tail =
            (impl->internal_event_head + impl->internal_event_count) %
            impl->internal_event_capacity;
        const size_t type_index = impl->staged_event_slots[index].type_index;
        const cmeta_type_desc *type = impl->ir->events[type_index].payload_type;
        impl->internal_event_slots[tail].type_index = type_index;
        memcpy(impl->internal_event_payloads +
                   tail * impl->event_payload_stride,
               impl->staged_event_payloads +
                   index * impl->event_payload_stride,
               type->size);
        ++impl->internal_event_count;
    }
    impl->staged_event_count = 0u;
}

static cflow_statechart_runtime_status execute_microstep(
    cflow_statechart_instance_impl *impl) {
    const size_t staged = 1u - impl->published;
    statechart_action_context context;
    size_t exit_count, entry_count = 0u, pseudo_count = 0u, position;
    const char *error = NULL;
    cflow_statechart_runtime_status status;
    copy_staging_buffers(impl, staged);
    compute_exit_union(impl, &exit_count);
    save_affected_history(impl, staged);
    context = (statechart_action_context){
        impl,
        impl->request_trigger.kind == CFLOW_STATECHART_TRIGGER_EVENT
            ? &impl->request_event : NULL,
        impl->extended_states[staged], impl->action_scratch, 0u,
        CFLOW_STATECHART_RUNTIME_OK, NULL};
    for (position = 0u; position < exit_count; ++position) {
        status = run_state_action_span(
            &context, impl->exit_order[position],
            CFLOW_STATECHART_STATE_ACTION_EXIT, &error);
        if (status != CFLOW_STATECHART_RUNTIME_OK) {
            microstep_fail(impl, status, error);
            return status;
        }
    }
    for (position = 0u; position < impl->request_transition_count; ++position) {
        status = run_transition_action_span(
            &context, impl->request_transition_indices[position],
            CFLOW_STATECHART_ACTION_TRANSITION, &error);
        if (status != CFLOW_STATECHART_RUNTIME_OK) {
            microstep_fail(impl, status, error);
            return status;
        }
    }
    if (!build_target_configuration(
            impl, staged, &pseudo_count, &entry_count)) {
        microstep_fail(
            impl, CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION,
            "Statechart staged configuration is invalid");
        return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
    }
    for (position = 0u; position < pseudo_count; ++position) {
        const size_t transition = impl->pseudo_transition_order[position];
        const size_t source = find_state_index(
            impl->ir, impl->ir->transitions[transition].source);
        const cflow_statechart_action_phase phase =
            impl->ir->states[source].kind == CFLOW_STATECHART_INITIAL
                ? CFLOW_STATECHART_ACTION_INITIAL
                : CFLOW_STATECHART_ACTION_HISTORY;
        status = run_transition_action_span(
            &context, transition, phase, &error);
        if (status != CFLOW_STATECHART_RUNTIME_OK) {
            microstep_fail(impl, status, error);
            return status;
        }
    }
    for (position = 0u; position < entry_count; ++position) {
        status = run_state_action_span(
            &context, impl->entry_order[position],
            CFLOW_STATECHART_STATE_ACTION_ENTRY, &error);
        if (status != CFLOW_STATECHART_RUNTIME_OK) {
            microstep_fail(impl, status, error);
            return status;
        }
    }
    if (context.current_state != impl->extended_states[staged])
        memcpy(impl->extended_states[staged], context.current_state,
               impl->ir->state_type->size);
    turbo_mutex_lock(&impl->lock);
    commit_internal_events(impl);
    impl->published = staged;
    if (impl->configuration_version != UINT64_MAX)
        ++impl->configuration_version;
    if (impl->microsteps != UINT64_MAX) ++impl->microsteps;
    if (UINT64_MAX - impl->actions < (uint64_t)context.invoked)
        impl->actions = UINT64_MAX;
    else
        impl->actions += (uint64_t)context.invoked;
    impl->microstep_result = CFLOW_STATECHART_RUNTIME_OK;
    if (impl->microstep_completed != UINT64_MAX)
        ++impl->microstep_completed;
    turbo_mutex_unlock(&impl->lock);
    return CFLOW_STATECHART_RUNTIME_OK;
}

static void statechart_microstep_run(void *user) {
    cflow_statechart_instance_impl *impl =
        (cflow_statechart_instance_impl *)user;
    (void)execute_microstep(impl);
}

static void statechart_microstep_cancel(void *user) {
    cflow_statechart_instance_impl *impl =
        (cflow_statechart_instance_impl *)user;
    turbo_mutex_lock(&impl->lock);
    impl->microstep_result = CFLOW_STATECHART_RUNTIME_TASK_CANCELLED;
    if (impl->microstep_cancelled != UINT64_MAX)
        ++impl->microstep_cancelled;
    turbo_mutex_unlock(&impl->lock);
}

static void statechart_microstep_finalize(void *user) {
    cflow_statechart_instance_impl *impl =
        (cflow_statechart_instance_impl *)user;
    turbo_mutex_lock(&impl->lock);
    if (impl->microstep_finalized != UINT64_MAX)
        ++impl->microstep_finalized;
    impl->microstep_pending = false;
    turbo_mutex_unlock(&impl->lock);
}

cflow_admission_status cflow_statechart_instance_try_microstep_internal(
    cflow_statechart_instance *instance,
    const cflow_statechart_selection_trigger *trigger,
    const cflow_statechart_selection_snapshot *selection) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    const cflow_executor_task task = {
        statechart_microstep_run, statechart_microstep_cancel,
        statechart_microstep_finalize, impl};
    cflow_admission_status admission;
    size_t index;
    if (impl == NULL || trigger == NULL || selection == NULL ||
        !trigger_valid(impl, trigger) || selection->transition_count == 0u ||
        selection->transition_ids != impl->selected_transition_ids ||
        selection->exit_sets != impl->selected_exit_sets ||
        selection->exit_set_stride != impl->bitset_bytes ||
        selection->transition_count != impl->selected_count)
        return CFLOW_ADMISSION_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->lock);
    if (impl->microstep_pending || impl->error != NULL) {
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_ADMISSION_FULL;
    }
    impl->microstep_pending = true;
    impl->request_transition_count = impl->selected_count;
    for (index = 0u; index < impl->selected_count; ++index)
        impl->request_transition_indices[index] =
            impl->selected_transition_indices[index];
    memcpy(impl->request_exit_sets, impl->selected_exit_sets,
           impl->selected_count * impl->bitset_bytes);
    impl->request_trigger = *trigger;
    if (trigger->kind == CFLOW_STATECHART_TRIGGER_EVENT) {
        const size_t type_index = find_event_index(
            impl->ir, trigger->event->id);
        const cmeta_type_desc *type = impl->ir->events[type_index].payload_type;
        memcpy(impl->request_event_payload, trigger->event->payload,
               type->size);
        impl->request_event = *trigger->event;
        impl->request_event.payload_type = type;
        impl->request_event.payload = impl->request_event_payload;
        impl->request_trigger.event = &impl->request_event;
    }
    if (impl->microstep_accepted != UINT64_MAX)
        ++impl->microstep_accepted;
    turbo_mutex_unlock(&impl->lock);

    admission = cflow_executor_try_post_task(impl->executor, &task);
    if (admission != CFLOW_ADMISSION_ACCEPTED) {
        turbo_mutex_lock(&impl->lock);
        impl->microstep_pending = false;
        if (impl->microstep_accepted != 0u) --impl->microstep_accepted;
        turbo_mutex_unlock(&impl->lock);
    }
    return admission;
}

bool cflow_statechart_instance_get_microstep_stats_internal(
    const cflow_statechart_instance *instance,
    cflow_statechart_microstep_stats *out) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    cflow_statechart_microstep_stats snapshot;
    if (impl == NULL || out == NULL) return false;
    turbo_mutex_lock(&impl->lock);
    snapshot = (cflow_statechart_microstep_stats){
        impl->microstep_accepted, impl->microstep_completed,
        impl->microstep_failed, impl->microstep_cancelled,
        impl->microstep_finalized, impl->microstep_result,
        impl->internal_event_capacity, impl->internal_event_count};
    turbo_mutex_unlock(&impl->lock);
    *out = snapshot;
    return true;
}

cflow_statechart_runtime_status
cflow_statechart_instance_copy_internal_event_internal(
    const cflow_statechart_instance *instance,
    size_t position,
    cflow_event_id *out_id,
    const cmeta_type_desc **out_type,
    void *out_payload,
    size_t payload_capacity) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    size_t slot, type_index;
    const cmeta_type_desc *type;
    if (out_id != NULL) *out_id = 0u;
    if (out_type != NULL) *out_type = NULL;
    if (impl == NULL || out_id == NULL || out_type == NULL ||
        out_payload == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->lock);
    if (position >= impl->internal_event_count) {
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_INVALID;
    }
    slot = (impl->internal_event_head + position) %
        impl->internal_event_capacity;
    type_index = impl->internal_event_slots[slot].type_index;
    type = impl->ir->events[type_index].payload_type;
    if (payload_capacity < type->size ||
        ((uintptr_t)out_payload % type->align) != 0u) {
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    memcpy(out_payload,
           impl->internal_event_payloads + slot * impl->event_payload_stride,
           type->size);
    *out_id = impl->ir->events[type_index].id;
    *out_type = type;
    turbo_mutex_unlock(&impl->lock);
    return CFLOW_STATECHART_RUNTIME_OK;
}

cflow_statechart_snapshot_status
cflow_statechart_instance_copy_history_internal(
    const cflow_statechart_instance *instance,
    cflow_machine_state_id history,
    cflow_machine_state_id *out_states,
    size_t state_capacity,
    size_t *out_state_count) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    size_t history_index, slot, required, index, written = 0u;
    const unsigned char *bits;
    if (impl == NULL || out_state_count == NULL) {
        return CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT;
    }
    history_index = find_state_index(impl->ir, history);
    if (history_index == SIZE_MAX ||
        (impl->ir->states[history_index].kind !=
             CFLOW_STATECHART_HISTORY_SHALLOW &&
         impl->ir->states[history_index].kind !=
             CFLOW_STATECHART_HISTORY_DEEP))
        return CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT;
    slot = impl->history_slots[history_index];
    turbo_mutex_lock(&impl->lock);
    required = impl->history_counts[impl->published][slot];
    if (state_capacity < required) {
        *out_state_count = required;
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_STATECHART_SNAPSHOT_TOO_SMALL;
    }
    if (required != 0u && out_states == NULL) {
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT;
    }
    bits = impl->history_bits[impl->published] + slot * impl->bitset_bytes;
    for (index = 0u; index < impl->ir->state_count; ++index) {
        const size_t state = impl->ir->document_order_indices[index];
        if (bit_test(bits, state))
            out_states[written++] = impl->ir->states[state].id;
    }
    *out_state_count = written;
    turbo_mutex_unlock(&impl->lock);
    return written == required
        ? CFLOW_STATECHART_SNAPSHOT_OK
        : CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT;
}

cflow_statechart_runtime_status cflow_statechart_instance_init(
    cflow_statechart_instance *instance,
    const cflow_statechart_instance_config *config) {
    cflow_statechart_instance_impl *impl;
    const cflow_statechart_impl *ir;
    cflow_statechart_storage_requirements requirements;
    cflow_statechart_runtime_status status;
    size_t effective_storage_limit, internal_event_capacity;
    if (instance == NULL || config == NULL || instance->impl != NULL ||
        config->statechart == NULL || config->initial_state == NULL ||
        config->executor == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    if (!cflow_executor_valid(config->executor) ||
        !cflow_executor_has(config->executor, CMETA_EXEC_CAP_SERIAL) ||
        cflow_executor_has(config->executor, CMETA_EXEC_CAP_MANUAL))
        return CFLOW_STATECHART_RUNTIME_INVALID_EXECUTOR;

    ir = cflow_statechart_internal_get(config->statechart);
    if (ir == NULL || !cmeta_type_desc_valid(ir->state_type) ||
        ir->state_type->size == 0u || ir->state_type->align == 0u ||
        (ir->state_type->align & (ir->state_type->align - 1u)) !=
            0u ||
        ir->state_type->align > _Alignof(cmeta_capture_storage) ||
        !cflow_value_storage_type_supported(ir->state_type))
        return CFLOW_STATECHART_RUNTIME_UNSUPPORTED_TYPE;
    if (!binding_rows_shape_valid(ir, config))
        return CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH;
    internal_event_capacity = config->internal_event_capacity != 0u
        ? config->internal_event_capacity
        : (size_t)CFLOW_STATECHART_DEFAULT_INTERNAL_EVENT_CAPACITY;
    if (internal_event_capacity == 0u)
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    if (config->max_storage_bytes >
        (size_t)CFLOW_STATECHART_MAX_INSTANCE_BYTES)
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    status = calculate_storage_requirements(
        ir, internal_event_capacity, &requirements);
    if (status != CFLOW_STATECHART_RUNTIME_OK) return status;
    effective_storage_limit = config->max_storage_bytes != 0u
        ? config->max_storage_bytes
        : (size_t)CFLOW_STATECHART_MAX_INSTANCE_BYTES;
    if (requirements.total_bytes > effective_storage_limit)
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;

    impl = (cflow_statechart_instance_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    impl->statechart = config->statechart;
    impl->ir = ir;
    impl->executor = config->executor;
    impl->internal_event_capacity = internal_event_capacity;
    impl->microstep_result = CFLOW_STATECHART_RUNTIME_OK;
    status = copy_bindings(impl, config);
    if (status != CFLOW_STATECHART_RUNTIME_OK) {
        instance_impl_free(impl);
        return status;
    }
    status = allocate_storage(impl, &requirements);
    if (status != CFLOW_STATECHART_RUNTIME_OK) {
        instance_impl_free(impl);
        return status;
    }
    turbo_mutex_init(&impl->lock);
    if (impl->lock == NULL) {
        instance_impl_free(impl);
        return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    }
    memcpy(impl->extended_states[0], config->initial_state,
           impl->ir->state_type->size);
    memcpy(impl->extended_states[1], config->initial_state,
           impl->ir->state_type->size);
    status = build_initial_configuration(impl, 1u);
    if (status != CFLOW_STATECHART_RUNTIME_OK) {
        instance_impl_free(impl);
        return status;
    }
    impl->published = 1u;
    impl->configuration_version = 1u;
    instance->impl = impl;
    return CFLOW_STATECHART_RUNTIME_OK;
}

cflow_statechart_snapshot_status
cflow_statechart_instance_copy_configuration(
    const cflow_statechart_instance *instance,
    cflow_machine_state_id *out_states,
    size_t state_capacity,
    size_t *out_state_count,
    uint64_t *out_version) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    size_t index, required, published;
    if (impl == NULL || out_state_count == NULL || out_version == NULL)
        return CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->lock);
    published = impl->published;
    required = impl->configurations[published].state_count;
    if (state_capacity < required) {
        *out_state_count = required;
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_STATECHART_SNAPSHOT_TOO_SMALL;
    }
    if (required != 0u && out_states == NULL) {
        turbo_mutex_unlock(&impl->lock);
        return CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT;
    }
    for (index = 0u; index < required; ++index) {
        const size_t state = impl->configurations[published].states[index];
        out_states[index] = impl->ir->states[state].id;
    }
    *out_state_count = required;
    *out_version = impl->configuration_version;
    turbo_mutex_unlock(&impl->lock);
    return CFLOW_STATECHART_SNAPSHOT_OK;
}

cflow_machine_state_id cflow_statechart_instance_current_state(
    const cflow_statechart_instance *instance) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    cflow_machine_state_id result = 0u;
    size_t index, leaf_count = 0u;
    if (impl == NULL) return 0u;
    turbo_mutex_lock(&impl->lock);
    for (index = 0u;
         index < impl->configurations[impl->published].state_count; ++index) {
        const size_t state =
            impl->configurations[impl->published].states[index];
        if (!leaf_kind(impl->ir->states[state].kind)) continue;
        result = impl->ir->states[state].id;
        if (++leaf_count > 1u) {
            result = 0u;
            break;
        }
    }
    turbo_mutex_unlock(&impl->lock);
    return result;
}

bool cflow_statechart_instance_copy_state(
    const cflow_statechart_instance *instance,
    const cmeta_type_desc **out_type,
    void *out_state,
    size_t state_capacity) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    if (out_type != NULL) *out_type = NULL;
    if (impl == NULL || out_type == NULL || out_state == NULL) return false;
    turbo_mutex_lock(&impl->lock);
    if (state_capacity < impl->ir->state_type->size) {
        turbo_mutex_unlock(&impl->lock);
        return false;
    }
    memcpy(out_state, impl->extended_states[impl->published],
           impl->ir->state_type->size);
    *out_type = impl->ir->state_type;
    turbo_mutex_unlock(&impl->lock);
    return true;
}

bool cflow_statechart_instance_get_stats(
    const cflow_statechart_instance *instance,
    cflow_statechart_instance_stats *out) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    cflow_statechart_instance_stats snapshot = {0};
    size_t index;
    if (impl == NULL || out == NULL) return false;
    turbo_mutex_lock(&impl->lock);
    snapshot.configuration_version = impl->configuration_version;
    snapshot.macrosteps = impl->macrosteps;
    snapshot.microsteps = impl->microsteps;
    snapshot.actions = impl->actions;
    snapshot.active_state_count =
        impl->configurations[impl->published].state_count;
    for (index = 0u; index < snapshot.active_state_count; ++index) {
        const size_t state =
            impl->configurations[impl->published].states[index];
        if (leaf_kind(impl->ir->states[state].kind))
            ++snapshot.active_leaf_count;
    }
    snapshot.closed = impl->closed;
    snapshot.cancelled = impl->cancelled;
    snapshot.done = impl->done;
    snapshot.errored = impl->error != NULL;
    turbo_mutex_unlock(&impl->lock);
    *out = snapshot;
    return true;
}

const char *cflow_statechart_instance_error(
    const cflow_statechart_instance *instance) {
    cflow_statechart_instance_impl *impl = instance != NULL
        ? (cflow_statechart_instance_impl *)instance->impl : NULL;
    const char *error;
    if (impl == NULL) return NULL;
    turbo_mutex_lock(&impl->lock);
    error = impl->error;
    turbo_mutex_unlock(&impl->lock);
    return error;
}

cflow_statechart_runtime_status cflow_statechart_instance_destroy(
    cflow_statechart_instance *instance) {
    cflow_statechart_instance_impl *impl;
    if (instance == NULL) return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    impl = (cflow_statechart_instance_impl *)instance->impl;
    if (impl == NULL) return CFLOW_STATECHART_RUNTIME_OK;
    if (!cflow_executor_wait_idle(impl->executor))
        return CFLOW_STATECHART_RUNTIME_WOULD_BLOCK;
    instance->impl = NULL;
    instance_impl_free(impl);
    return CFLOW_STATECHART_RUNTIME_OK;
}
