#include <cflow/machine.h>

#include <stdlib.h>
#include <string.h>

typedef struct cflow_machine_impl {
    cflow_machine_state *states;
    size_t state_count;
    cflow_machine_state_id initial_state;
    cflow_event_type *events;
    size_t event_count;
    cflow_machine_guard *guards;
    size_t guard_count;
    cflow_machine_action *actions;
    size_t action_count;
    cflow_machine_transition *transitions;
    size_t transition_count;
} cflow_machine_impl;

static bool checked_bytes(size_t count, size_t width, size_t *bytes) {
    if (bytes == NULL || (width != 0u && count > SIZE_MAX / width)) return false;
    *bytes = count * width;
    return true;
}

static void machine_impl_destroy(cflow_machine_impl *impl) {
    if (impl == NULL) return;
    free(impl->transitions);
    free(impl->actions);
    free(impl->guards);
    free(impl->events);
    free(impl->states);
    free(impl);
}

static int compare_state(const void *left, const void *right) {
    const cflow_machine_state *a = (const cflow_machine_state *)left;
    const cflow_machine_state *b = (const cflow_machine_state *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_event(const void *left, const void *right) {
    const cflow_event_type *a = (const cflow_event_type *)left;
    const cflow_event_type *b = (const cflow_event_type *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_guard(const void *left, const void *right) {
    const cflow_machine_guard *a = (const cflow_machine_guard *)left;
    const cflow_machine_guard *b = (const cflow_machine_guard *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_action(const void *left, const void *right) {
    const cflow_machine_action *a = (const cflow_machine_action *)left;
    const cflow_machine_action *b = (const cflow_machine_action *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_transition(const void *left, const void *right) {
    const cflow_machine_transition *a =
        (const cflow_machine_transition *)left;
    const cflow_machine_transition *b =
        (const cflow_machine_transition *)right;
    if (a->source != b->source) return a->source < b->source ? -1 : 1;
    if (a->event != b->event) return a->event < b->event ? -1 : 1;
    if (a->priority != b->priority) return a->priority < b->priority ? -1 : 1;
    if (a->target != b->target) return a->target < b->target ? -1 : 1;
    return 0;
}

static bool copy_rows(void **out, const void *rows, size_t bytes) {
    void *copy;
    if (bytes == 0u) {
        *out = NULL;
        return true;
    }
    copy = malloc(bytes);
    if (copy == NULL) return false;
    memcpy(copy, rows, bytes);
    *out = copy;
    return true;
}

static const cflow_machine_state *find_state(
    const cflow_machine_impl *impl, cflow_machine_state_id id) {
    size_t left = 0u, right = impl->state_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_state *state = &impl->states[middle];
        if (state->id == id) return state;
        if (state->id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_event_type *find_event(
    const cflow_machine_impl *impl, cflow_event_id id) {
    size_t left = 0u, right = impl->event_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_event_type *event = &impl->events[middle];
        if (event->id == id) return event;
        if (event->id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_machine_guard *find_guard(
    const cflow_machine_impl *impl, cflow_machine_guard_id id) {
    size_t left = 0u, right = impl->guard_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_guard *guard = &impl->guards[middle];
        if (guard->id == id) return guard;
        if (guard->id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_machine_action *find_action(
    const cflow_machine_impl *impl, cflow_machine_action_id id) {
    size_t left = 0u, right = impl->action_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_action *action = &impl->actions[middle];
        if (action->id == id) return action;
        if (action->id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static bool valid_value_type(const cmeta_type_desc *type) {
    return cmeta_type_desc_valid(type) && type->size != 0u;
}

static cflow_machine_status validate_state_domain(
    const cflow_machine_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->state_count; ++index) {
        const cflow_machine_state *state = &impl->states[index];
        if (state->id == 0u) return CFLOW_MACHINE_INVALID_ID;
        if (index != 0u && impl->states[index - 1u].id == state->id)
            return CFLOW_MACHINE_DUPLICATE_ID;
        if (!valid_value_type(state->value_type))
            return CFLOW_MACHINE_INVALID_TYPE;
        if (state->kind != CFLOW_MACHINE_STATE_ACTIVE &&
            state->kind != CFLOW_MACHINE_STATE_DONE &&
            state->kind != CFLOW_MACHINE_STATE_ERROR)
            return CFLOW_MACHINE_INVALID_CONTRACT;
    }
    return find_state(impl, impl->initial_state) == NULL
        ? CFLOW_MACHINE_UNKNOWN_STATE : CFLOW_MACHINE_OK;
}

static cflow_machine_status validate_event_domain(
    const cflow_machine_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->event_count; ++index) {
        const cflow_event_type *event = &impl->events[index];
        if (event->id == 0u) return CFLOW_MACHINE_INVALID_ID;
        if (index != 0u && impl->events[index - 1u].id == event->id)
            return CFLOW_MACHINE_DUPLICATE_ID;
        if (!valid_value_type(event->payload_type))
            return CFLOW_MACHINE_INVALID_TYPE;
    }
    return CFLOW_MACHINE_OK;
}

static cflow_machine_status validate_guard_domain(
    const cflow_machine_impl *impl) {
    const cmeta_properties required =
        CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS;
    size_t index;
    for (index = 0u; index < impl->guard_count; ++index) {
        const cflow_machine_guard *guard = &impl->guards[index];
        const cflow_event_type *event;
        if (guard->id == 0u || guard->event_id == 0u)
            return CFLOW_MACHINE_INVALID_ID;
        if (index != 0u && impl->guards[index - 1u].id == guard->id)
            return CFLOW_MACHINE_DUPLICATE_ID;
        if (!valid_value_type(guard->state_type) ||
            !valid_value_type(guard->event_type))
            return CFLOW_MACHINE_INVALID_TYPE;
        if (!cmeta_effects_valid(guard->effects) ||
            !cmeta_properties_valid(guard->properties) ||
            !cmeta_effects_are_pure(guard->effects) ||
            !cmeta_properties_include(guard->properties, required))
            return CFLOW_MACHINE_INVALID_CONTRACT;
        event = find_event(impl, guard->event_id);
        if (event == NULL) return CFLOW_MACHINE_UNKNOWN_EVENT;
        if (!cmeta_type_equal(event->payload_type, guard->event_type))
            return CFLOW_MACHINE_TYPE_MISMATCH;
    }
    return CFLOW_MACHINE_OK;
}

static cflow_machine_status validate_action_observation(
    const cflow_machine_impl *impl, const cflow_machine_action *action) {
    const cflow_event_type *event;
    switch (action->observation) {
        case CFLOW_MACHINE_ACTION_NONE:
            return action->output_type == NULL && action->output_event_id == 0u
                ? CFLOW_MACHINE_OK : CFLOW_MACHINE_INVALID_OBSERVATION;
        case CFLOW_MACHINE_ACTION_VALUE:
            return valid_value_type(action->output_type) &&
                   action->output_event_id == 0u
                ? CFLOW_MACHINE_OK : CFLOW_MACHINE_INVALID_OBSERVATION;
        case CFLOW_MACHINE_ACTION_EVENT:
            if (!valid_value_type(action->output_type) ||
                action->output_event_id == 0u)
                return CFLOW_MACHINE_INVALID_OBSERVATION;
            event = find_event(impl, action->output_event_id);
            if (event == NULL) return CFLOW_MACHINE_UNKNOWN_EVENT;
            return cmeta_type_equal(event->payload_type, action->output_type)
                ? CFLOW_MACHINE_OK : CFLOW_MACHINE_TYPE_MISMATCH;
        default:
            return CFLOW_MACHINE_INVALID_OBSERVATION;
    }
}

static cflow_machine_status validate_action_domain(
    const cflow_machine_impl *impl) {
    const cmeta_properties required =
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS;
    size_t index;
    for (index = 0u; index < impl->action_count; ++index) {
        const cflow_machine_action *action = &impl->actions[index];
        const cflow_event_type *event;
        cflow_machine_status status;
        if (action->id == 0u || action->event_id == 0u)
            return CFLOW_MACHINE_INVALID_ID;
        if (index != 0u && impl->actions[index - 1u].id == action->id)
            return CFLOW_MACHINE_DUPLICATE_ID;
        if (!valid_value_type(action->source_type) ||
            !valid_value_type(action->event_type) ||
            !valid_value_type(action->target_type))
            return CFLOW_MACHINE_INVALID_TYPE;
        if (!cmeta_effects_valid(action->effects) ||
            !cmeta_properties_valid(action->properties) ||
            !cmeta_properties_include(action->properties, required))
            return CFLOW_MACHINE_INVALID_CONTRACT;
        event = find_event(impl, action->event_id);
        if (event == NULL) return CFLOW_MACHINE_UNKNOWN_EVENT;
        if (!cmeta_type_equal(event->payload_type, action->event_type))
            return CFLOW_MACHINE_TYPE_MISMATCH;
        status = validate_action_observation(impl, action);
        if (status != CFLOW_MACHINE_OK) return status;
    }
    return CFLOW_MACHINE_OK;
}

static cflow_machine_status validate_transitions(
    const cflow_machine_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->transition_count; ++index) {
        const cflow_machine_transition *transition = &impl->transitions[index];
        const cflow_machine_state *source = find_state(impl, transition->source);
        const cflow_machine_state *target = find_state(impl, transition->target);
        const cflow_event_type *event = find_event(impl, transition->event);
        const cflow_machine_guard *guard = NULL;
        const cflow_machine_action *action = NULL;
        if (source == NULL || target == NULL) return CFLOW_MACHINE_UNKNOWN_STATE;
        if (event == NULL) return CFLOW_MACHINE_UNKNOWN_EVENT;
        if (transition->guard != 0u) {
            guard = find_guard(impl, transition->guard);
            if (guard == NULL) return CFLOW_MACHINE_UNKNOWN_GUARD;
        }
        if (transition->action != 0u) {
            action = find_action(impl, transition->action);
            if (action == NULL) return CFLOW_MACHINE_UNKNOWN_ACTION;
        }
        if (source->kind != CFLOW_MACHINE_STATE_ACTIVE)
            return CFLOW_MACHINE_TERMINAL_TRANSITION;
        if (guard != NULL &&
            (guard->event_id != transition->event ||
             !cmeta_type_equal(guard->state_type, source->value_type) ||
             !cmeta_type_equal(guard->event_type, event->payload_type)))
            return CFLOW_MACHINE_TYPE_MISMATCH;
        if (action != NULL &&
            (action->event_id != transition->event ||
             !cmeta_type_equal(action->source_type, source->value_type) ||
             !cmeta_type_equal(action->event_type, event->payload_type) ||
             !cmeta_type_equal(action->target_type, target->value_type)))
            return CFLOW_MACHINE_TYPE_MISMATCH;
        if (action == NULL &&
            !cmeta_type_equal(source->value_type, target->value_type))
            return CFLOW_MACHINE_TYPE_MISMATCH;
        if (index != 0u) {
            const cflow_machine_transition *previous =
                &impl->transitions[index - 1u];
            if (previous->source == transition->source &&
                previous->event == transition->event &&
                previous->priority == transition->priority)
                return CFLOW_MACHINE_AMBIGUOUS_TRANSITION;
        }
    }
    return CFLOW_MACHINE_OK;
}

/* O(states * transitions) over immutable build-time rows; storage is exactly
 * one Boolean per state and no data-path allocation survives construction. */
static cflow_machine_status validate_reachability(
    const cflow_machine_impl *impl) {
    bool *reachable = (bool *)calloc(impl->state_count, sizeof(*reachable));
    bool changed = true;
    size_t index;
    if (reachable == NULL) return CFLOW_MACHINE_ALLOCATION_FAILED;
    for (index = 0u; index < impl->state_count; ++index)
        if (impl->states[index].id == impl->initial_state) reachable[index] = true;
    while (changed) {
        changed = false;
        for (index = 0u; index < impl->transition_count; ++index) {
            const cflow_machine_transition *transition = &impl->transitions[index];
            size_t source_index, target_index;
            for (source_index = 0u; source_index < impl->state_count;
                 ++source_index)
                if (impl->states[source_index].id == transition->source) break;
            for (target_index = 0u; target_index < impl->state_count;
                 ++target_index)
                if (impl->states[target_index].id == transition->target) break;
            if (source_index < impl->state_count && target_index < impl->state_count &&
                reachable[source_index] && !reachable[target_index]) {
                reachable[target_index] = true;
                changed = true;
            }
        }
    }
    for (index = 0u; index < impl->state_count; ++index) {
        if (!reachable[index]) {
            free(reachable);
            return CFLOW_MACHINE_UNREACHABLE_STATE;
        }
    }
    free(reachable);
    return CFLOW_MACHINE_OK;
}

static cflow_machine_status validate_declaration_use(
    const cflow_machine_impl *impl) {
    size_t declaration, transition;
    for (declaration = 0u; declaration < impl->guard_count; ++declaration) {
        for (transition = 0u; transition < impl->transition_count; ++transition)
            if (impl->transitions[transition].guard == impl->guards[declaration].id)
                break;
        if (transition == impl->transition_count)
            return CFLOW_MACHINE_UNUSED_DECLARATION;
    }
    for (declaration = 0u; declaration < impl->action_count; ++declaration) {
        for (transition = 0u; transition < impl->transition_count; ++transition)
            if (impl->transitions[transition].action == impl->actions[declaration].id)
                break;
        if (transition == impl->transition_count)
            return CFLOW_MACHINE_UNUSED_DECLARATION;
    }
    return CFLOW_MACHINE_OK;
}

static cflow_machine_status validate_machine(const cflow_machine_impl *impl) {
    cflow_machine_status status = validate_state_domain(impl);
    if (status != CFLOW_MACHINE_OK) return status;
    status = validate_event_domain(impl);
    if (status != CFLOW_MACHINE_OK) return status;
    status = validate_guard_domain(impl);
    if (status != CFLOW_MACHINE_OK) return status;
    status = validate_action_domain(impl);
    if (status != CFLOW_MACHINE_OK) return status;
    status = validate_transitions(impl);
    if (status != CFLOW_MACHINE_OK) return status;
    status = validate_reachability(impl);
    if (status != CFLOW_MACHINE_OK) return status;
    return validate_declaration_use(impl);
}

cflow_machine_status cflow_machine_build(
    cflow_machine *out, const cflow_machine_definition *definition) {
    cflow_machine_impl *impl;
    size_t state_bytes, event_bytes, guard_bytes, action_bytes;
    size_t transition_bytes;
    cflow_machine_status status;

    if (out == NULL || definition == NULL || out->impl != NULL)
        return CFLOW_MACHINE_INVALID_ARGUMENT;
    if (definition->state_count == 0u) return CFLOW_MACHINE_EMPTY;
    if (definition->states == NULL ||
        (definition->event_count != 0u && definition->events == NULL) ||
        (definition->guard_count != 0u && definition->guards == NULL) ||
        (definition->action_count != 0u && definition->actions == NULL) ||
        (definition->transition_count != 0u &&
         definition->transitions == NULL))
        return CFLOW_MACHINE_INVALID_ARGUMENT;
    if (!checked_bytes(definition->state_count, sizeof(*definition->states),
                       &state_bytes) ||
        !checked_bytes(definition->event_count, sizeof(*definition->events),
                       &event_bytes) ||
        !checked_bytes(definition->guard_count, sizeof(*definition->guards),
                       &guard_bytes) ||
        !checked_bytes(definition->action_count, sizeof(*definition->actions),
                       &action_bytes) ||
        !checked_bytes(definition->transition_count,
                       sizeof(*definition->transitions), &transition_bytes))
        return CFLOW_MACHINE_INVALID_ARGUMENT;

    impl = (cflow_machine_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_MACHINE_ALLOCATION_FAILED;
    if (!copy_rows((void **)&impl->states, definition->states, state_bytes) ||
        !copy_rows((void **)&impl->events, definition->events, event_bytes) ||
        !copy_rows((void **)&impl->guards, definition->guards, guard_bytes) ||
        !copy_rows((void **)&impl->actions, definition->actions, action_bytes) ||
        !copy_rows((void **)&impl->transitions, definition->transitions,
                   transition_bytes)) {
        machine_impl_destroy(impl);
        return CFLOW_MACHINE_ALLOCATION_FAILED;
    }

    impl->state_count = definition->state_count;
    impl->initial_state = definition->initial_state;
    impl->event_count = definition->event_count;
    impl->guard_count = definition->guard_count;
    impl->action_count = definition->action_count;
    impl->transition_count = definition->transition_count;
    if (impl->state_count > 1u)
        qsort(impl->states, impl->state_count, sizeof(*impl->states), compare_state);
    if (impl->event_count > 1u)
        qsort(impl->events, impl->event_count, sizeof(*impl->events), compare_event);
    if (impl->guard_count > 1u)
        qsort(impl->guards, impl->guard_count, sizeof(*impl->guards), compare_guard);
    if (impl->action_count > 1u)
        qsort(impl->actions, impl->action_count, sizeof(*impl->actions), compare_action);
    if (impl->transition_count > 1u)
        qsort(impl->transitions, impl->transition_count,
              sizeof(*impl->transitions), compare_transition);
    status = validate_machine(impl);
    if (status != CFLOW_MACHINE_OK) {
        machine_impl_destroy(impl);
        return status;
    }
    out->impl = impl;
    return CFLOW_MACHINE_OK;
}

void cflow_machine_destroy(cflow_machine *machine) {
    if (machine == NULL) return;
    machine_impl_destroy((cflow_machine_impl *)machine->impl);
    machine->impl = NULL;
}

static const cflow_machine_impl *machine_impl(const cflow_machine *machine) {
    return machine == NULL ? NULL : (const cflow_machine_impl *)machine->impl;
}

cflow_machine_state_id cflow_machine_initial_state(
    const cflow_machine *machine) {
    const cflow_machine_impl *impl = machine_impl(machine);
    return impl == NULL ? 0u : impl->initial_state;
}

#define CFLOW_MACHINE_COUNT_FN(name, member) \
    size_t cflow_machine_##name##_count(const cflow_machine *machine) { \
        const cflow_machine_impl *impl = machine_impl(machine); \
        return impl == NULL ? 0u : impl->member##_count; \
    }
CFLOW_MACHINE_COUNT_FN(state, state)
CFLOW_MACHINE_COUNT_FN(event, event)
CFLOW_MACHINE_COUNT_FN(guard, guard)
CFLOW_MACHINE_COUNT_FN(action, action)
CFLOW_MACHINE_COUNT_FN(transition, transition)
#undef CFLOW_MACHINE_COUNT_FN

#define CFLOW_MACHINE_AT_FN(name, type, member) \
    const type *cflow_machine_##name##_at( \
        const cflow_machine *machine, size_t index) { \
        const cflow_machine_impl *impl = machine_impl(machine); \
        return impl == NULL || index >= impl->member##_count \
            ? NULL : &impl->member##s[index]; \
    }
CFLOW_MACHINE_AT_FN(state, cflow_machine_state, state)
CFLOW_MACHINE_AT_FN(event, cflow_event_type, event)
CFLOW_MACHINE_AT_FN(guard, cflow_machine_guard, guard)
CFLOW_MACHINE_AT_FN(action, cflow_machine_action, action)
CFLOW_MACHINE_AT_FN(transition, cflow_machine_transition, transition)
#undef CFLOW_MACHINE_AT_FN
