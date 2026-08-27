#include <cflow/statechart_hierarchy_adapter.h>

#include <stdint.h>
#include <stdlib.h>

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_allocate(size_t count, size_t width, void **out) {
    if (out == NULL || (width != 0u && count > SIZE_MAX / width))
        return false;
    *out = count != 0u ? calloc(count, width) : NULL;
    return count == 0u || *out != NULL;
}

static size_t hierarchy_find_state(
    const cflow_machine_hierarchy *hierarchy,
    cflow_machine_state_id id) {
    size_t left = 0u;
    size_t right = cflow_machine_hierarchy_state_count(hierarchy);
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_hierarchy_state *state =
            cflow_machine_hierarchy_state_at(hierarchy, middle);
        if (state->id == id) return middle;
        if (state->id < id) left = middle + 1u;
        else right = middle;
    }
    return SIZE_MAX;
}

static size_t hierarchy_resolve_initial_leaf(
    const cflow_machine_hierarchy *hierarchy, size_t state_index) {
    const size_t state_count =
        cflow_machine_hierarchy_state_count(hierarchy);
    size_t hops = 0u;
    while (state_index != SIZE_MAX && hops++ < state_count) {
        const cflow_machine_hierarchy_state *state =
            cflow_machine_hierarchy_state_at(hierarchy, state_index);
        if (state->initial_child == 0u) return state_index;
        state_index = hierarchy_find_state(hierarchy, state->initial_child);
    }
    return SIZE_MAX;
}

static cflow_statechart_hierarchy_adapter_result adapter_result(
    cflow_statechart_hierarchy_adapter_status status,
    cflow_statechart_status statechart_status) {
    const cflow_statechart_hierarchy_adapter_result result = {
        status, statechart_status};
    return result;
}

static bool assign_preorder(
    cflow_statechart_state *states,
    const size_t *parents,
    size_t state_count) {
    size_t *child_counts = NULL;
    size_t *child_offsets = NULL;
    size_t *cursors = NULL;
    size_t *children = NULL;
    size_t *stack = NULL;
    size_t root = SIZE_MAX, root_count = 0u, index, total = 0u, top = 0u;
    bool ok = false;
    if (states == NULL || parents == NULL || state_count == 0u ||
        !checked_allocate(state_count, sizeof(*child_counts),
                          (void **)&child_counts) ||
        !checked_allocate(state_count + 1u, sizeof(*child_offsets),
                          (void **)&child_offsets) ||
        !checked_allocate(state_count, sizeof(*cursors),
                          (void **)&cursors) ||
        !checked_allocate(state_count - 1u, sizeof(*children),
                          (void **)&children) ||
        !checked_allocate(state_count, sizeof(*stack), (void **)&stack))
        goto cleanup;
    for (index = 0u; index < state_count; ++index) {
        if (parents[index] == SIZE_MAX) {
            root = index;
            ++root_count;
        } else if (parents[index] >= state_count) {
            goto cleanup;
        } else {
            ++child_counts[parents[index]];
        }
    }
    if (root_count != 1u) goto cleanup;
    for (index = 0u; index < state_count; ++index) {
        child_offsets[index] = total;
        if (!checked_add(total, child_counts[index], &total)) goto cleanup;
        cursors[index] = child_offsets[index];
    }
    child_offsets[state_count] = total;
    if (total != state_count - 1u) goto cleanup;
    for (index = 0u; index < state_count; ++index) {
        if (parents[index] != SIZE_MAX)
            children[cursors[parents[index]]++] = index;
    }
    stack[top++] = root;
    total = 0u;
    while (top != 0u) {
        const size_t state = stack[--top];
        size_t child = child_offsets[state + 1u];
        states[state].document_order = (uint32_t)total++;
        while (child != child_offsets[state])
            stack[top++] = children[--child];
    }
    ok = total == state_count;

cleanup:
    free(stack);
    free(children);
    free(cursors);
    free(child_offsets);
    free(child_counts);
    return ok;
}

cflow_statechart_hierarchy_adapter_result
cflow_statechart_hierarchy_adapter_build(
    cflow_statechart *out,
    const cflow_machine_hierarchy *hierarchy) {
    const cflow_machine *flat;
    cflow_statechart_state *states = NULL;
    cflow_event_type *events = NULL;
    cflow_statechart_guard *guards = NULL;
    cflow_statechart_executable *executables = NULL;
    cflow_statechart_transition *transitions = NULL;
    cflow_statechart_transition_action *transition_actions = NULL;
    cflow_machine_state_id *initial_ids = NULL;
    size_t *parent_indices = NULL;
    cflow_statechart_definition definition;
    cflow_statechart_status build_status;
    size_t hierarchy_state_count, flat_transition_count;
    size_t state_count, transition_count, compound_count = 0u;
    size_t transition_action_count = 0u;
    size_t root = SIZE_MAX, initial_leaf, index, write;
    uint32_t generated_state_id = UINT32_MAX;

    if (out == NULL || out->impl != NULL || hierarchy == NULL ||
        cflow_machine_hierarchy_state_count(hierarchy) == 0u ||
        cflow_machine_hierarchy_flat_machine(hierarchy) == NULL)
        return adapter_result(
            CFLOW_STATECHART_HIERARCHY_ADAPTER_INVALID_ARGUMENT,
            CFLOW_STATECHART_INVALID_ARGUMENT);

    flat = cflow_machine_hierarchy_flat_machine(hierarchy);
    hierarchy_state_count = cflow_machine_hierarchy_state_count(hierarchy);
    flat_transition_count = cflow_machine_transition_count(flat);
    for (index = 0u; index < hierarchy_state_count; ++index) {
        const cflow_machine_hierarchy_state *state =
            cflow_machine_hierarchy_state_at(hierarchy, index);
        if (state == NULL)
            return adapter_result(
                CFLOW_STATECHART_HIERARCHY_ADAPTER_INVALID_ARGUMENT,
                CFLOW_STATECHART_INVALID_ARGUMENT);
        if (state->parent == 0u) root = index;
        if (state->initial_child != 0u) ++compound_count;
        if (state->kind == CFLOW_MACHINE_STATE_ERROR)
            return adapter_result(
                CFLOW_STATECHART_HIERARCHY_ADAPTER_ERROR_STATE_UNSUPPORTED,
                CFLOW_STATECHART_INVALID_STATE_KIND);
    }
    if (root == SIZE_MAX ||
        !checked_add(hierarchy_state_count, compound_count, &state_count) ||
        !checked_add(flat_transition_count, compound_count,
                     &transition_count) ||
        state_count > CFLOW_MACHINE_MAX_STATES ||
        transition_count > CFLOW_MACHINE_MAX_TRANSITIONS ||
        transition_count > UINT32_MAX)
        return adapter_result(
            CFLOW_STATECHART_HIERARCHY_ADAPTER_LIMIT_EXCEEDED,
            CFLOW_STATECHART_LIMIT_EXCEEDED);

    initial_leaf = hierarchy_resolve_initial_leaf(hierarchy, root);
    if (initial_leaf == SIZE_MAX ||
        cflow_machine_hierarchy_state_at(hierarchy, initial_leaf)->id !=
            cflow_machine_initial_state(flat))
        return adapter_result(
            CFLOW_STATECHART_HIERARCHY_ADAPTER_INITIAL_STATE_UNSUPPORTED,
            CFLOW_STATECHART_INVALID_INITIAL);

    for (index = 0u; index < cflow_machine_action_count(flat); ++index) {
        const cflow_machine_action *action =
            cflow_machine_action_at(flat, index);
        if (action == NULL ||
            action->observation != CFLOW_MACHINE_ACTION_NONE ||
            action->output_type != NULL || action->output_event_id != 0u)
            return adapter_result(
                CFLOW_STATECHART_HIERARCHY_ADAPTER_OBSERVATION_UNSUPPORTED,
                CFLOW_STATECHART_INVALID_CONTRACT);
    }
    for (index = 0u; index < flat_transition_count; ++index) {
        const cflow_machine_transition *transition =
            cflow_machine_transition_at(flat, index);
        if (transition != NULL && transition->action != 0u)
            ++transition_action_count;
    }
    if (transition_action_count > CFLOW_STATECHART_MAX_ACTION_REFS)
        return adapter_result(
            CFLOW_STATECHART_HIERARCHY_ADAPTER_LIMIT_EXCEEDED,
            CFLOW_STATECHART_LIMIT_EXCEEDED);

    if (!checked_allocate(state_count, sizeof(*states), (void **)&states) ||
        !checked_allocate(cflow_machine_event_count(flat), sizeof(*events),
                          (void **)&events) ||
        !checked_allocate(cflow_machine_guard_count(flat), sizeof(*guards),
                          (void **)&guards) ||
        !checked_allocate(cflow_machine_action_count(flat),
                          sizeof(*executables), (void **)&executables) ||
        !checked_allocate(transition_count, sizeof(*transitions),
                          (void **)&transitions) ||
        !checked_allocate(transition_action_count,
                          sizeof(*transition_actions),
                          (void **)&transition_actions) ||
        !checked_allocate(hierarchy_state_count, sizeof(*initial_ids),
                          (void **)&initial_ids) ||
        !checked_allocate(state_count, sizeof(*parent_indices),
                          (void **)&parent_indices)) {
        build_status = CFLOW_STATECHART_ALLOCATION_FAILED;
        goto allocation_failed;
    }

    write = hierarchy_state_count;
    for (index = 0u; index < hierarchy_state_count; ++index) {
        const cflow_machine_hierarchy_state *source =
            cflow_machine_hierarchy_state_at(hierarchy, index);
        cflow_statechart_state_kind kind;
        if (source->initial_child != 0u)
            kind = CFLOW_STATECHART_COMPOUND;
        else if (source->kind == CFLOW_MACHINE_STATE_DONE)
            kind = CFLOW_STATECHART_FINAL;
        else
            kind = CFLOW_STATECHART_ATOMIC;
        states[index] = (cflow_statechart_state){
            source->id, source->parent, kind, 0u};
        parent_indices[index] = source->parent == 0u
            ? SIZE_MAX : hierarchy_find_state(hierarchy, source->parent);
        if (source->initial_child == 0u) continue;
        while (generated_state_id != 0u &&
               hierarchy_find_state(hierarchy, generated_state_id) !=
                   SIZE_MAX)
            --generated_state_id;
        if (generated_state_id == 0u) {
            build_status = CFLOW_STATECHART_LIMIT_EXCEEDED;
            goto build_failed;
        }
        initial_ids[index] = generated_state_id--;
        states[write] = (cflow_statechart_state){
            initial_ids[index], source->id, CFLOW_STATECHART_INITIAL,
            0u};
        parent_indices[write] = index;
        ++write;
    }
    if (!assign_preorder(states, parent_indices, state_count)) {
        build_status = CFLOW_STATECHART_ALLOCATION_FAILED;
        goto allocation_failed;
    }

    for (index = 0u; index < cflow_machine_event_count(flat); ++index)
        events[index] = *cflow_machine_event_at(flat, index);
    for (index = 0u; index < cflow_machine_guard_count(flat); ++index) {
        const cflow_machine_guard *source =
            cflow_machine_guard_at(flat, index);
        guards[index] = (cflow_statechart_guard){
            source->id, source->state_type,
            source->effects, source->properties};
    }
    for (index = 0u; index < cflow_machine_action_count(flat); ++index) {
        const cflow_machine_action *source =
            cflow_machine_action_at(flat, index);
        executables[index] = (cflow_statechart_executable){
            source->id, source->source_type,
            source->effects, source->properties};
    }

    write = 0u;
    for (index = 0u; index < hierarchy_state_count; ++index) {
        const cflow_machine_hierarchy_state *source =
            cflow_machine_hierarchy_state_at(hierarchy, index);
        if (source->initial_child == 0u) continue;
        transitions[write] = (cflow_statechart_transition){
            (cflow_statechart_transition_id)(write + 1u),
            initial_ids[index], CFLOW_STATECHART_TRIGGER_EVENTLESS,
            0u, 0u, 0u, source->initial_child,
            CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, (uint32_t)write};
        ++write;
    }
    {
        size_t action_write = 0u;
        for (index = 0u; index < flat_transition_count; ++index) {
            const cflow_machine_transition *source =
                cflow_machine_transition_at(flat, index);
            transitions[write] = (cflow_statechart_transition){
                (cflow_statechart_transition_id)(write + 1u),
                source->source, CFLOW_STATECHART_TRIGGER_EVENT,
                source->event, 0u, source->guard, source->target,
                CFLOW_STATECHART_TRANSITION_EXTERNAL,
                source->priority, (uint32_t)write};
            if (source->action != 0u) {
                transition_actions[action_write++] =
                    (cflow_statechart_transition_action){
                        transitions[write].id,
                        source->action,
                        0u};
            }
            ++write;
        }
    }

    definition = (cflow_statechart_definition){
        cflow_machine_hierarchy_state_at(hierarchy, 0u)->value_type,
        states, state_count,
        events, cflow_machine_event_count(flat),
        guards, cflow_machine_guard_count(flat),
        executables, cflow_machine_action_count(flat),
        transitions, transition_count,
        NULL, 0u,
        transition_actions, transition_action_count};
    build_status = cflow_statechart_build(out, &definition);
    if (build_status != CFLOW_STATECHART_OK) goto build_failed;

    free(parent_indices);
    free(initial_ids);
    free(transition_actions);
    free(transitions);
    free(executables);
    free(guards);
    free(events);
    free(states);
    return adapter_result(
        CFLOW_STATECHART_HIERARCHY_ADAPTER_OK, CFLOW_STATECHART_OK);

allocation_failed:
    free(parent_indices);
    free(initial_ids);
    free(transition_actions);
    free(transitions);
    free(executables);
    free(guards);
    free(events);
    free(states);
    return adapter_result(
        CFLOW_STATECHART_HIERARCHY_ADAPTER_ALLOCATION_FAILED,
        build_status);

build_failed:
    free(parent_indices);
    free(initial_ids);
    free(transition_actions);
    free(transitions);
    free(executables);
    free(guards);
    free(events);
    free(states);
    return adapter_result(
        build_status == CFLOW_STATECHART_LIMIT_EXCEEDED
            ? CFLOW_STATECHART_HIERARCHY_ADAPTER_LIMIT_EXCEEDED
            : build_status == CFLOW_STATECHART_ALLOCATION_FAILED
                ? CFLOW_STATECHART_HIERARCHY_ADAPTER_ALLOCATION_FAILED
                : CFLOW_STATECHART_HIERARCHY_ADAPTER_STATECHART_REJECTED,
        build_status);
}

static bool hierarchy_guard_bridge(
    void *user, const void *state, const cflow_event_view *event,
    bool *out_enabled, const char **out_error) {
    const cflow_statechart_hierarchy_guard_context *context =
        (const cflow_statechart_hierarchy_guard_context *)user;
    if (context == NULL || context->fn == NULL || event == NULL)
        return false;
    return context->fn(
        context->user, state, event->payload, out_enabled, out_error);
}

static bool hierarchy_action_bridge(
    void *user,
    cflow_statechart_action_phase phase,
    cflow_machine_state_id owner,
    const void *state,
    const cflow_event_view *event,
    void *out_state,
    cflow_statechart_raise_fn raise_internal,
    void *raise_user,
    const char **out_error) {
    const cflow_statechart_hierarchy_action_context *context =
        (const cflow_statechart_hierarchy_action_context *)user;
    (void)owner;
    (void)raise_internal;
    (void)raise_user;
    if (context == NULL || context->fn == NULL || event == NULL ||
        phase != CFLOW_STATECHART_ACTION_TRANSITION) {
        if (out_error != NULL)
            *out_error = "hierarchy action requires a transition Event";
        return false;
    }
    return context->fn(
        context->user, state, event->payload, out_state, NULL, out_error);
}

bool cflow_statechart_hierarchy_adapt_guard_binding(
    const cflow_machine_guard_binding *source,
    cflow_statechart_hierarchy_guard_context *context,
    cflow_statechart_guard_binding *out) {
    if (source == NULL || source->id == 0u || source->fn == NULL ||
        context == NULL || out == NULL)
        return false;
    *context = (cflow_statechart_hierarchy_guard_context){
        source->fn, source->user};
    *out = (cflow_statechart_guard_binding){
        source->id, hierarchy_guard_bridge, context};
    return true;
}

bool cflow_statechart_hierarchy_adapt_action_binding(
    const cflow_machine_action_binding *source,
    cflow_statechart_hierarchy_action_context *context,
    cflow_statechart_executable_binding *out) {
    if (source == NULL || source->id == 0u || source->fn == NULL ||
        context == NULL || out == NULL)
        return false;
    *context = (cflow_statechart_hierarchy_action_context){
        source->fn, source->user};
    *out = (cflow_statechart_executable_binding){
        source->id, hierarchy_action_bridge, context};
    return true;
}
