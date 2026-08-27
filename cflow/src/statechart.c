#include <cflow/statechart.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_statechart_impl {
    const cmeta_type_desc *state_type;
    cflow_statechart_state *states;
    size_t state_count;
    size_t *parents;
    size_t *depths;
    size_t root;
    cflow_event_type *events;
    size_t event_count;
    cflow_statechart_guard *guards;
    size_t guard_count;
    cflow_statechart_executable *executables;
    size_t executable_count;
    cflow_statechart_transition *transitions;
    size_t transition_count;
    cflow_statechart_state_action *state_actions;
    size_t state_action_count;
    cflow_statechart_transition_action *transition_actions;
    size_t transition_action_count;
} cflow_statechart_impl;

typedef struct statechart_transition_key {
    cflow_machine_state_id source;
    cflow_statechart_trigger_kind trigger;
    uint32_t trigger_id;
    uint32_t priority;
} statechart_transition_key;

_Static_assert(CFLOW_MACHINE_MAX_STATES <=
                   SIZE_MAX / sizeof(cflow_statechart_state),
               "configured Statechart state limit overflows size_t bytes");
_Static_assert(CFLOW_MACHINE_MAX_GUARDS <=
                   SIZE_MAX / sizeof(cflow_statechart_guard),
               "configured Statechart guard limit overflows size_t bytes");
_Static_assert(CFLOW_MACHINE_MAX_ACTIONS <=
                   SIZE_MAX / sizeof(cflow_statechart_executable),
               "configured executable limit overflows size_t bytes");
_Static_assert(CFLOW_MACHINE_MAX_TRANSITIONS <=
                   SIZE_MAX / sizeof(cflow_statechart_transition),
               "configured Statechart transition limit overflows size_t bytes");
_Static_assert(CFLOW_STATECHART_MAX_ACTION_REFS <=
                   SIZE_MAX / sizeof(cflow_statechart_state_action),
               "configured Statechart action-ref limit overflows size_t bytes");

static bool checked_bytes(size_t count, size_t width, size_t *out) {
    if (out == NULL || (width != 0u && count > SIZE_MAX / width)) return false;
    *out = count * width;
    return true;
}

static bool valid_value_type(const cmeta_type_desc *type) {
    return cmeta_type_desc_valid(type) && type->size != 0u;
}

static void *copy_rows(const void *rows, size_t count, size_t width) {
    size_t bytes;
    void *copy;
    if (count == 0u) return NULL;
    if (rows == NULL || !checked_bytes(count, width, &bytes)) return NULL;
    copy = malloc(bytes);
    if (copy != NULL) memcpy(copy, rows, bytes);
    return copy;
}

static void statechart_impl_destroy(cflow_statechart_impl *impl) {
    if (impl == NULL) return;
    free(impl->transition_actions);
    free(impl->state_actions);
    free(impl->transitions);
    free(impl->executables);
    free(impl->guards);
    free(impl->events);
    free(impl->depths);
    free(impl->parents);
    free(impl->states);
    free(impl);
}

static int compare_state_id(const void *left, const void *right) {
    const cflow_statechart_state *a = (const cflow_statechart_state *)left;
    const cflow_statechart_state *b = (const cflow_statechart_state *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_event_id(const void *left, const void *right) {
    const cflow_event_type *a = (const cflow_event_type *)left;
    const cflow_event_type *b = (const cflow_event_type *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_guard_id(const void *left, const void *right) {
    const cflow_statechart_guard *a = (const cflow_statechart_guard *)left;
    const cflow_statechart_guard *b = (const cflow_statechart_guard *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_executable_id(const void *left, const void *right) {
    const cflow_statechart_executable *a =
        (const cflow_statechart_executable *)left;
    const cflow_statechart_executable *b =
        (const cflow_statechart_executable *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_transition_id(const void *left, const void *right) {
    const cflow_statechart_transition *a =
        (const cflow_statechart_transition *)left;
    const cflow_statechart_transition *b =
        (const cflow_statechart_transition *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_u32(const void *left, const void *right) {
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return a < b ? -1 : a > b;
}

static int compare_transition_key(const void *left, const void *right) {
    const statechart_transition_key *a =
        (const statechart_transition_key *)left;
    const statechart_transition_key *b =
        (const statechart_transition_key *)right;
    if (a->source != b->source) return a->source < b->source ? -1 : 1;
    if (a->trigger != b->trigger) return a->trigger < b->trigger ? -1 : 1;
    if (a->trigger_id != b->trigger_id)
        return a->trigger_id < b->trigger_id ? -1 : 1;
    if (a->priority != b->priority)
        return a->priority < b->priority ? -1 : 1;
    return 0;
}

static int compare_state_action(const void *left, const void *right) {
    const cflow_statechart_state_action *a =
        (const cflow_statechart_state_action *)left;
    const cflow_statechart_state_action *b =
        (const cflow_statechart_state_action *)right;
    if (a->state != b->state) return a->state < b->state ? -1 : 1;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->order != b->order) return a->order < b->order ? -1 : 1;
    if (a->executable != b->executable)
        return a->executable < b->executable ? -1 : 1;
    return 0;
}

static int compare_transition_action(const void *left, const void *right) {
    const cflow_statechart_transition_action *a =
        (const cflow_statechart_transition_action *)left;
    const cflow_statechart_transition_action *b =
        (const cflow_statechart_transition_action *)right;
    if (a->transition != b->transition)
        return a->transition < b->transition ? -1 : 1;
    if (a->order != b->order) return a->order < b->order ? -1 : 1;
    if (a->executable != b->executable)
        return a->executable < b->executable ? -1 : 1;
    return 0;
}

static size_t find_state_index(
    const cflow_statechart_impl *impl, cflow_machine_state_id id) {
    size_t left = 0u, right = impl->state_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_state_id current = impl->states[middle].id;
        if (current == id) return middle;
        if (current < id) left = middle + 1u;
        else right = middle;
    }
    return SIZE_MAX;
}

static const cflow_event_type *find_event(
    const cflow_statechart_impl *impl, cflow_event_id id) {
    size_t left = 0u, right = impl->event_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->events[middle].id == id) return &impl->events[middle];
        if (impl->events[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_statechart_guard *find_guard(
    const cflow_statechart_impl *impl, cflow_statechart_guard_id id) {
    size_t left = 0u, right = impl->guard_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->guards[middle].id == id) return &impl->guards[middle];
        if (impl->guards[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static const cflow_statechart_executable *find_executable(
    const cflow_statechart_impl *impl, cflow_statechart_executable_id id) {
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

static const cflow_statechart_transition *find_transition(
    const cflow_statechart_impl *impl, cflow_statechart_transition_id id) {
    size_t left = 0u, right = impl->transition_count;
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        if (impl->transitions[middle].id == id)
            return &impl->transitions[middle];
        if (impl->transitions[middle].id < id) left = middle + 1u;
        else right = middle;
    }
    return NULL;
}

static bool pseudo_kind(cflow_statechart_state_kind kind) {
    return kind == CFLOW_STATECHART_INITIAL ||
           kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
           kind == CFLOW_STATECHART_HISTORY_DEEP;
}

static bool completion_kind(cflow_statechart_state_kind kind) {
    return kind == CFLOW_STATECHART_COMPOUND ||
           kind == CFLOW_STATECHART_PARALLEL;
}

static bool state_kind_valid(cflow_statechart_state_kind kind) {
    return kind >= CFLOW_STATECHART_ATOMIC &&
           kind <= CFLOW_STATECHART_HISTORY_DEEP;
}

static bool is_descendant(
    const cflow_statechart_impl *impl, size_t node, size_t ancestor) {
    while (node != SIZE_MAX) {
        if (node == ancestor) return true;
        node = impl->parents[node];
    }
    return false;
}

static cflow_statechart_status validate_unique_orders(
    const cflow_statechart_impl *impl) {
    uint32_t *orders;
    size_t bytes, index;
    if (!checked_bytes(impl->state_count, sizeof(*orders), &bytes))
        return CFLOW_STATECHART_LIMIT_EXCEEDED;
    orders = (uint32_t *)malloc(bytes);
    if (orders == NULL) return CFLOW_STATECHART_ALLOCATION_FAILED;
    for (index = 0u; index < impl->state_count; ++index)
        orders[index] = impl->states[index].document_order;
    qsort(orders, impl->state_count, sizeof(*orders), compare_u32);
    for (index = 1u; index < impl->state_count; ++index) {
        if (orders[index - 1u] == orders[index]) {
            free(orders);
            return CFLOW_STATECHART_DUPLICATE_ORDER;
        }
    }
    free(orders);

    if (impl->transition_count == 0u) return CFLOW_STATECHART_OK;
    if (!checked_bytes(impl->transition_count, sizeof(*orders), &bytes))
        return CFLOW_STATECHART_LIMIT_EXCEEDED;
    orders = (uint32_t *)malloc(bytes);
    if (orders == NULL) return CFLOW_STATECHART_ALLOCATION_FAILED;
    for (index = 0u; index < impl->transition_count; ++index)
        orders[index] = impl->transitions[index].document_order;
    qsort(orders, impl->transition_count, sizeof(*orders), compare_u32);
    for (index = 1u; index < impl->transition_count; ++index) {
        if (orders[index - 1u] == orders[index]) {
            free(orders);
            return CFLOW_STATECHART_DUPLICATE_ORDER;
        }
    }
    free(orders);
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_state_ids(
    cflow_statechart_impl *impl) {
    size_t index;
    qsort(impl->states, impl->state_count, sizeof(*impl->states),
          compare_state_id);
    for (index = 0u; index < impl->state_count; ++index) {
        if (impl->states[index].id == 0u)
            return CFLOW_STATECHART_INVALID_ID;
        if (index != 0u &&
            impl->states[index - 1u].id == impl->states[index].id)
            return CFLOW_STATECHART_DUPLICATE_ID;
        if (!state_kind_valid(impl->states[index].kind))
            return CFLOW_STATECHART_INVALID_STATE_KIND;
    }
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_transition_ids(
    cflow_statechart_impl *impl) {
    size_t index;
    if (impl->transition_count == 0u) return CFLOW_STATECHART_OK;
    if (impl->transition_count > 1u)
        qsort(impl->transitions, impl->transition_count,
              sizeof(*impl->transitions), compare_transition_id);
    for (index = 0u; index < impl->transition_count; ++index) {
        if (impl->transitions[index].id == 0u)
            return CFLOW_STATECHART_INVALID_ID;
        if (index != 0u &&
            impl->transitions[index - 1u].id == impl->transitions[index].id)
            return CFLOW_STATECHART_DUPLICATE_ID;
    }
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status initialize_tree(cflow_statechart_impl *impl) {
    unsigned char *colors = NULL;
    size_t *stack = NULL;
    size_t root_count = 0u, index;
    size_t bytes;
    if (!checked_bytes(impl->state_count, sizeof(*impl->parents), &bytes))
        return CFLOW_STATECHART_LIMIT_EXCEEDED;
    impl->parents = (size_t *)malloc(bytes);
    impl->depths = (size_t *)calloc(impl->state_count, sizeof(*impl->depths));
    colors = (unsigned char *)calloc(impl->state_count, sizeof(*colors));
    stack = (size_t *)malloc(bytes);
    if (impl->parents == NULL || impl->depths == NULL || colors == NULL ||
        stack == NULL) {
        free(stack);
        free(colors);
        return CFLOW_STATECHART_ALLOCATION_FAILED;
    }
    for (index = 0u; index < impl->state_count; ++index) {
        if (impl->states[index].parent == 0u) {
            impl->parents[index] = SIZE_MAX;
            impl->root = index;
            ++root_count;
        } else {
            impl->parents[index] =
                find_state_index(impl, impl->states[index].parent);
            if (impl->parents[index] == SIZE_MAX) {
                free(stack);
                free(colors);
                return CFLOW_STATECHART_INVALID_PARENT;
            }
        }
    }
    if (root_count != 1u || pseudo_kind(impl->states[impl->root].kind) ||
        !completion_kind(impl->states[impl->root].kind)) {
        free(stack);
        free(colors);
        return CFLOW_STATECHART_INVALID_TREE;
    }
    for (index = 0u; index < impl->state_count; ++index) {
        size_t current = index, count = 0u;
        if (colors[index] == 2u) continue;
        while (current != SIZE_MAX && colors[current] == 0u) {
            colors[current] = 1u;
            stack[count++] = current;
            current = impl->parents[current];
        }
        if (current != SIZE_MAX && colors[current] == 1u) {
            free(stack);
            free(colors);
            return CFLOW_STATECHART_INVALID_TREE;
        }
        while (count != 0u) {
            const size_t node = stack[--count];
            const size_t parent = impl->parents[node];
            impl->depths[node] = parent == SIZE_MAX
                ? 0u : impl->depths[parent] + 1u;
            colors[node] = 2u;
        }
    }
    free(stack);
    free(colors);
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_child_kinds(
    const cflow_statechart_impl *impl) {
    size_t *real_children, *initial_children, *all_children;
    size_t bytes, index;
    if (!checked_bytes(impl->state_count, sizeof(*real_children), &bytes))
        return CFLOW_STATECHART_LIMIT_EXCEEDED;
    real_children = (size_t *)calloc(impl->state_count,
                                     sizeof(*real_children));
    initial_children = (size_t *)calloc(impl->state_count,
                                        sizeof(*initial_children));
    all_children = (size_t *)calloc(impl->state_count,
                                    sizeof(*all_children));
    if (real_children == NULL || initial_children == NULL ||
        all_children == NULL) {
        free(all_children);
        free(initial_children);
        free(real_children);
        return CFLOW_STATECHART_ALLOCATION_FAILED;
    }
    for (index = 0u; index < impl->state_count; ++index) {
        const size_t parent = impl->parents[index];
        if (parent == SIZE_MAX) continue;
        ++all_children[parent];
        if (impl->states[index].kind == CFLOW_STATECHART_INITIAL)
            ++initial_children[parent];
        else if (!pseudo_kind(impl->states[index].kind))
            ++real_children[parent];
        if (impl->states[index].kind == CFLOW_STATECHART_INITIAL &&
            impl->states[parent].kind != CFLOW_STATECHART_COMPOUND) {
            free(all_children);
            free(initial_children);
            free(real_children);
            return CFLOW_STATECHART_INVALID_INITIAL;
        }
        if ((impl->states[index].kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
             impl->states[index].kind == CFLOW_STATECHART_HISTORY_DEEP) &&
            !completion_kind(impl->states[parent].kind)) {
            free(all_children);
            free(initial_children);
            free(real_children);
            return CFLOW_STATECHART_INVALID_HISTORY;
        }
    }
    for (index = 0u; index < impl->state_count; ++index) {
        cflow_statechart_status status = CFLOW_STATECHART_OK;
        switch (impl->states[index].kind) {
            case CFLOW_STATECHART_COMPOUND:
                if (real_children[index] == 0u || initial_children[index] != 1u)
                    status = CFLOW_STATECHART_INVALID_INITIAL;
                break;
            case CFLOW_STATECHART_PARALLEL:
                if (real_children[index] == 0u || initial_children[index] != 0u)
                    status = CFLOW_STATECHART_INVALID_INITIAL;
                break;
            case CFLOW_STATECHART_ATOMIC:
            case CFLOW_STATECHART_FINAL:
            case CFLOW_STATECHART_INITIAL:
            case CFLOW_STATECHART_HISTORY_SHALLOW:
            case CFLOW_STATECHART_HISTORY_DEEP:
                if (all_children[index] != 0u)
                    status = CFLOW_STATECHART_INVALID_STATE_KIND;
                break;
            default:
                status = CFLOW_STATECHART_INVALID_STATE_KIND;
                break;
        }
        if (status != CFLOW_STATECHART_OK) {
            free(all_children);
            free(initial_children);
            free(real_children);
            return status;
        }
    }
    free(all_children);
    free(initial_children);
    free(real_children);
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_events(cflow_statechart_impl *impl) {
    size_t index;
    if (impl->event_count == 0u) return CFLOW_STATECHART_OK;
    qsort(impl->events, impl->event_count, sizeof(*impl->events),
          compare_event_id);
    for (index = 0u; index < impl->event_count; ++index) {
        if (impl->events[index].id == 0u)
            return CFLOW_STATECHART_INVALID_ID;
        if (index != 0u &&
            impl->events[index - 1u].id == impl->events[index].id)
            return CFLOW_STATECHART_DUPLICATE_ID;
        if (!valid_value_type(impl->events[index].payload_type))
            return CFLOW_STATECHART_INVALID_TYPE;
    }
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_guards(cflow_statechart_impl *impl) {
    const cmeta_properties required = CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS;
    size_t index;
    if (impl->guard_count == 0u) return CFLOW_STATECHART_OK;
    qsort(impl->guards, impl->guard_count, sizeof(*impl->guards),
          compare_guard_id);
    for (index = 0u; index < impl->guard_count; ++index) {
        const cflow_statechart_guard *guard = &impl->guards[index];
        if (guard->id == 0u) return CFLOW_STATECHART_INVALID_ID;
        if (index != 0u && impl->guards[index - 1u].id == guard->id)
            return CFLOW_STATECHART_DUPLICATE_ID;
        if (!valid_value_type(guard->state_type))
            return CFLOW_STATECHART_INVALID_TYPE;
        if (!cmeta_type_equal(guard->state_type, impl->state_type))
            return CFLOW_STATECHART_TYPE_MISMATCH;
        if (!cmeta_effects_valid(guard->effects) ||
            !cmeta_properties_valid(guard->properties) ||
            !cmeta_effects_are_pure(guard->effects) ||
            !cmeta_properties_include(guard->properties, required))
            return CFLOW_STATECHART_INVALID_CONTRACT;
    }
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_executables(
    cflow_statechart_impl *impl) {
    const cmeta_properties required =
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS;
    size_t index;
    if (impl->executable_count == 0u) return CFLOW_STATECHART_OK;
    qsort(impl->executables, impl->executable_count,
          sizeof(*impl->executables), compare_executable_id);
    for (index = 0u; index < impl->executable_count; ++index) {
        const cflow_statechart_executable *executable =
            &impl->executables[index];
        if (executable->id == 0u) return CFLOW_STATECHART_INVALID_ID;
        if (index != 0u &&
            impl->executables[index - 1u].id == executable->id)
            return CFLOW_STATECHART_DUPLICATE_ID;
        if (!valid_value_type(executable->state_type))
            return CFLOW_STATECHART_INVALID_TYPE;
        if (!cmeta_type_equal(executable->state_type, impl->state_type))
            return CFLOW_STATECHART_TYPE_MISMATCH;
        if (!cmeta_effects_valid(executable->effects) ||
            !cmeta_properties_valid(executable->properties) ||
            !cmeta_properties_include(executable->properties, required) ||
            ((executable->properties & CMETA_PROP_TOTAL) != 0u &&
             (executable->effects & CMETA_EFFECT_MAY_FAIL) != 0u))
            return CFLOW_STATECHART_INVALID_CONTRACT;
    }
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_transition_trigger(
    const cflow_statechart_impl *impl,
    const cflow_statechart_transition *transition) {
    switch (transition->trigger) {
        case CFLOW_STATECHART_TRIGGER_EVENTLESS:
            return transition->event == 0u && transition->completion == 0u
                ? CFLOW_STATECHART_OK : CFLOW_STATECHART_INVALID_TRIGGER;
        case CFLOW_STATECHART_TRIGGER_EVENT:
            if (transition->event == 0u || transition->completion != 0u)
                return CFLOW_STATECHART_INVALID_TRIGGER;
            return find_event(impl, transition->event) != NULL
                ? CFLOW_STATECHART_OK : CFLOW_STATECHART_UNKNOWN_EVENT;
        case CFLOW_STATECHART_TRIGGER_COMPLETION: {
            const size_t completed = find_state_index(
                impl, transition->completion);
            if (transition->event != 0u || transition->completion == 0u)
                return CFLOW_STATECHART_INVALID_TRIGGER;
            return completed != SIZE_MAX &&
                   completion_kind(impl->states[completed].kind)
                ? CFLOW_STATECHART_OK
                : CFLOW_STATECHART_INVALID_COMPLETION;
        }
        default:
            return CFLOW_STATECHART_INVALID_TRIGGER;
    }
}

static cflow_statechart_status validate_default_transitions(
    const cflow_statechart_impl *impl) {
    size_t *counts;
    size_t index;
    counts = (size_t *)calloc(impl->state_count, sizeof(*counts));
    if (counts == NULL) return CFLOW_STATECHART_ALLOCATION_FAILED;
    for (index = 0u; index < impl->transition_count; ++index) {
        const cflow_statechart_transition *transition =
            &impl->transitions[index];
        const size_t source = find_state_index(impl, transition->source);
        if (source != SIZE_MAX && pseudo_kind(impl->states[source].kind)) {
            const size_t target = find_state_index(impl, transition->target);
            const size_t parent = impl->parents[source];
            ++counts[source];
            if (transition->trigger != CFLOW_STATECHART_TRIGGER_EVENTLESS ||
                transition->event != 0u || transition->completion != 0u ||
                transition->guard != 0u || transition->target == 0u ||
                transition->kind != CFLOW_STATECHART_TRANSITION_EXTERNAL ||
                target == SIZE_MAX || pseudo_kind(impl->states[target].kind) ||
                parent == SIZE_MAX || target == parent ||
                !is_descendant(impl, target, parent)) {
                const cflow_statechart_status status =
                    impl->states[source].kind == CFLOW_STATECHART_INITIAL
                    ? CFLOW_STATECHART_INVALID_INITIAL
                    : CFLOW_STATECHART_INVALID_HISTORY;
                free(counts);
                return status;
            }
        }
    }
    for (index = 0u; index < impl->state_count; ++index) {
        if (pseudo_kind(impl->states[index].kind) && counts[index] != 1u) {
            const cflow_statechart_status status =
                impl->states[index].kind == CFLOW_STATECHART_INITIAL
                ? CFLOW_STATECHART_INVALID_INITIAL
                : CFLOW_STATECHART_INVALID_HISTORY;
            free(counts);
            return status;
        }
    }
    free(counts);
    return CFLOW_STATECHART_OK;
}

static uint32_t transition_trigger_id(
    const cflow_statechart_transition *transition) {
    return transition->trigger == CFLOW_STATECHART_TRIGGER_EVENT
        ? transition->event
        : transition->trigger == CFLOW_STATECHART_TRIGGER_COMPLETION
            ? transition->completion : 0u;
}

static cflow_statechart_status validate_transitions(
    cflow_statechart_impl *impl) {
    statechart_transition_key *keys = NULL;
    size_t key_bytes, index;
    if (impl->transition_count != 0u) {
        if (!checked_bytes(impl->transition_count, sizeof(*keys), &key_bytes))
            return CFLOW_STATECHART_LIMIT_EXCEEDED;
        keys = (statechart_transition_key *)malloc(key_bytes);
        if (keys == NULL) return CFLOW_STATECHART_ALLOCATION_FAILED;
    }
    for (index = 0u; index < impl->transition_count; ++index) {
        const cflow_statechart_transition *transition =
            &impl->transitions[index];
        const size_t source = find_state_index(impl, transition->source);
        size_t target = SIZE_MAX;
        cflow_statechart_status status;
        if (transition->id == 0u || transition->source == 0u) {
            free(keys);
            return CFLOW_STATECHART_INVALID_ID;
        }
        if (index != 0u &&
            impl->transitions[index - 1u].id == transition->id) {
            free(keys);
            return CFLOW_STATECHART_DUPLICATE_ID;
        }
        if (source == SIZE_MAX) {
            free(keys);
            return CFLOW_STATECHART_UNKNOWN_STATE;
        }
        if (transition->target != 0u) {
            target = find_state_index(impl, transition->target);
            if (target == SIZE_MAX) {
                free(keys);
                return CFLOW_STATECHART_UNKNOWN_STATE;
            }
            if (impl->states[target].kind == CFLOW_STATECHART_INITIAL) {
                free(keys);
                return CFLOW_STATECHART_INVALID_INITIAL;
            }
        }
        if (transition->kind != CFLOW_STATECHART_TRANSITION_EXTERNAL &&
            transition->kind != CFLOW_STATECHART_TRANSITION_INTERNAL) {
            free(keys);
            return CFLOW_STATECHART_INVALID_CONTRACT;
        }
        if (impl->states[source].kind == CFLOW_STATECHART_FINAL) {
            free(keys);
            return CFLOW_STATECHART_INVALID_STATE_KIND;
        }
        status = validate_transition_trigger(impl, transition);
        if (status != CFLOW_STATECHART_OK) {
            free(keys);
            return status;
        }
        if (transition->guard != 0u &&
            find_guard(impl, transition->guard) == NULL) {
            free(keys);
            return CFLOW_STATECHART_UNKNOWN_GUARD;
        }
        keys[index] = (statechart_transition_key){
            transition->source, transition->trigger,
            transition_trigger_id(transition), transition->priority};
    }
    {
        const cflow_statechart_status status =
            validate_default_transitions(impl);
        if (status != CFLOW_STATECHART_OK) {
            free(keys);
            return status;
        }
    }
    if (impl->transition_count > 1u)
        qsort(keys, impl->transition_count, sizeof(*keys),
              compare_transition_key);
    for (index = 1u; index < impl->transition_count; ++index) {
        if (compare_transition_key(&keys[index - 1u], &keys[index]) == 0) {
            free(keys);
            return CFLOW_STATECHART_AMBIGUOUS_TRANSITION;
        }
    }
    free(keys);
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_action_rows(
    cflow_statechart_impl *impl) {
    size_t index;
    if (impl->state_action_count > 1u)
        qsort(impl->state_actions, impl->state_action_count,
              sizeof(*impl->state_actions), compare_state_action);
    for (index = 0u; index < impl->state_action_count; ++index) {
        const cflow_statechart_state_action *row = &impl->state_actions[index];
        const size_t state = find_state_index(impl, row->state);
        if (state == SIZE_MAX) return CFLOW_STATECHART_UNKNOWN_STATE;
        if (pseudo_kind(impl->states[state].kind))
            return CFLOW_STATECHART_INVALID_STATE_KIND;
        if (row->kind != CFLOW_STATECHART_STATE_ACTION_ENTRY &&
            row->kind != CFLOW_STATECHART_STATE_ACTION_EXIT)
            return CFLOW_STATECHART_INVALID_CONTRACT;
        if (find_executable(impl, row->executable) == NULL)
            return CFLOW_STATECHART_UNKNOWN_EXECUTABLE;
        if (index != 0u) {
            const cflow_statechart_state_action *previous =
                &impl->state_actions[index - 1u];
            if (previous->state == row->state &&
                previous->kind == row->kind &&
                previous->order == row->order)
                return CFLOW_STATECHART_DUPLICATE_ORDER;
        }
    }
    if (impl->transition_action_count > 1u)
        qsort(impl->transition_actions, impl->transition_action_count,
              sizeof(*impl->transition_actions), compare_transition_action);
    for (index = 0u; index < impl->transition_action_count; ++index) {
        const cflow_statechart_transition_action *row =
            &impl->transition_actions[index];
        if (find_transition(impl, row->transition) == NULL)
            return CFLOW_STATECHART_UNKNOWN_TRANSITION;
        if (find_executable(impl, row->executable) == NULL)
            return CFLOW_STATECHART_UNKNOWN_EXECUTABLE;
        if (index != 0u) {
            const cflow_statechart_transition_action *previous =
                &impl->transition_actions[index - 1u];
            if (previous->transition == row->transition &&
                previous->order == row->order)
                return CFLOW_STATECHART_DUPLICATE_ORDER;
        }
    }
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_declaration_use(
    const cflow_statechart_impl *impl) {
    bool *guard_used = NULL, *executable_used = NULL;
    size_t index;
    if (impl->guard_count != 0u) {
        guard_used = (bool *)calloc(impl->guard_count, sizeof(*guard_used));
        if (guard_used == NULL) return CFLOW_STATECHART_ALLOCATION_FAILED;
    }
    if (impl->executable_count != 0u) {
        executable_used = (bool *)calloc(impl->executable_count,
                                          sizeof(*executable_used));
        if (executable_used == NULL) {
            free(guard_used);
            return CFLOW_STATECHART_ALLOCATION_FAILED;
        }
    }
    for (index = 0u; index < impl->transition_count; ++index) {
        if (impl->transitions[index].guard != 0u) {
            const cflow_statechart_guard *guard =
                find_guard(impl, impl->transitions[index].guard);
            guard_used[(size_t)(guard - impl->guards)] = true;
        }
    }
    for (index = 0u; index < impl->state_action_count; ++index) {
        const cflow_statechart_executable *executable = find_executable(
            impl, impl->state_actions[index].executable);
        executable_used[(size_t)(executable - impl->executables)] = true;
    }
    for (index = 0u; index < impl->transition_action_count; ++index) {
        const cflow_statechart_executable *executable = find_executable(
            impl, impl->transition_actions[index].executable);
        executable_used[(size_t)(executable - impl->executables)] = true;
    }
    for (index = 0u; index < impl->guard_count; ++index) {
        if (!guard_used[index]) {
            free(executable_used);
            free(guard_used);
            return CFLOW_STATECHART_UNUSED_DECLARATION;
        }
    }
    for (index = 0u; index < impl->executable_count; ++index) {
        if (!executable_used[index]) {
            free(executable_used);
            free(guard_used);
            return CFLOW_STATECHART_UNUSED_DECLARATION;
        }
    }
    free(executable_used);
    free(guard_used);
    return CFLOW_STATECHART_OK;
}

static cflow_statechart_status validate_statechart(
    cflow_statechart_impl *impl) {
    cflow_statechart_status status = validate_state_ids(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_transition_ids(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_unique_orders(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = initialize_tree(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_child_kinds(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_events(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_guards(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_executables(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_transitions(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    status = validate_action_rows(impl);
    if (status != CFLOW_STATECHART_OK) return status;
    return validate_declaration_use(impl);
}

static bool definition_rows_valid(
    const cflow_statechart_definition *definition) {
    return definition->states != NULL &&
        (definition->event_count == 0u || definition->events != NULL) &&
        (definition->guard_count == 0u || definition->guards != NULL) &&
        (definition->executable_count == 0u ||
         definition->executables != NULL) &&
        (definition->transition_count == 0u ||
         definition->transitions != NULL) &&
        (definition->state_action_count == 0u ||
         definition->state_actions != NULL) &&
        (definition->transition_action_count == 0u ||
         definition->transition_actions != NULL);
}

static bool definition_within_limits(
    const cflow_statechart_definition *definition) {
    return definition->state_count <= CFLOW_MACHINE_MAX_STATES &&
        definition->event_count <= CFLOW_MACHINE_MAX_EVENTS &&
        definition->guard_count <= CFLOW_MACHINE_MAX_GUARDS &&
        definition->executable_count <= CFLOW_MACHINE_MAX_ACTIONS &&
        definition->transition_count <= CFLOW_MACHINE_MAX_TRANSITIONS &&
        definition->state_action_count <= CFLOW_STATECHART_MAX_ACTION_REFS &&
        definition->transition_action_count <=
            CFLOW_STATECHART_MAX_ACTION_REFS;
}

cflow_statechart_status cflow_statechart_build(
    cflow_statechart *out, const cflow_statechart_definition *definition) {
    cflow_statechart_impl *impl;
    cflow_statechart_status status;
    if (out == NULL || definition == NULL || out->impl != NULL)
        return CFLOW_STATECHART_INVALID_ARGUMENT;
    if (definition->state_count == 0u) return CFLOW_STATECHART_EMPTY;
    if (!definition_within_limits(definition))
        return CFLOW_STATECHART_LIMIT_EXCEEDED;
    if (!definition_rows_valid(definition))
        return CFLOW_STATECHART_INVALID_ARGUMENT;
    if (!valid_value_type(definition->state_type))
        return CFLOW_STATECHART_INVALID_TYPE;

    impl = (cflow_statechart_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_STATECHART_ALLOCATION_FAILED;
    impl->state_type = definition->state_type;
    impl->state_count = definition->state_count;
    impl->event_count = definition->event_count;
    impl->guard_count = definition->guard_count;
    impl->executable_count = definition->executable_count;
    impl->transition_count = definition->transition_count;
    impl->state_action_count = definition->state_action_count;
    impl->transition_action_count = definition->transition_action_count;
    impl->states = (cflow_statechart_state *)copy_rows(
        definition->states, definition->state_count,
        sizeof(*definition->states));
    impl->events = (cflow_event_type *)copy_rows(
        definition->events, definition->event_count,
        sizeof(*definition->events));
    impl->guards = (cflow_statechart_guard *)copy_rows(
        definition->guards, definition->guard_count,
        sizeof(*definition->guards));
    impl->executables = (cflow_statechart_executable *)copy_rows(
        definition->executables, definition->executable_count,
        sizeof(*definition->executables));
    impl->transitions = (cflow_statechart_transition *)copy_rows(
        definition->transitions, definition->transition_count,
        sizeof(*definition->transitions));
    impl->state_actions = (cflow_statechart_state_action *)copy_rows(
        definition->state_actions, definition->state_action_count,
        sizeof(*definition->state_actions));
    impl->transition_actions =
        (cflow_statechart_transition_action *)copy_rows(
            definition->transition_actions,
            definition->transition_action_count,
            sizeof(*definition->transition_actions));
    if (impl->states == NULL ||
        (impl->event_count != 0u && impl->events == NULL) ||
        (impl->guard_count != 0u && impl->guards == NULL) ||
        (impl->executable_count != 0u && impl->executables == NULL) ||
        (impl->transition_count != 0u && impl->transitions == NULL) ||
        (impl->state_action_count != 0u && impl->state_actions == NULL) ||
        (impl->transition_action_count != 0u &&
         impl->transition_actions == NULL)) {
        statechart_impl_destroy(impl);
        return CFLOW_STATECHART_ALLOCATION_FAILED;
    }
    status = validate_statechart(impl);
    if (status != CFLOW_STATECHART_OK) {
        statechart_impl_destroy(impl);
        return status;
    }
    out->impl = impl;
    return CFLOW_STATECHART_OK;
}

void cflow_statechart_destroy(cflow_statechart *statechart) {
    if (statechart == NULL) return;
    statechart_impl_destroy((cflow_statechart_impl *)statechart->impl);
    statechart->impl = NULL;
}

static const cflow_statechart_impl *statechart_impl(
    const cflow_statechart *statechart) {
    return statechart != NULL
        ? (const cflow_statechart_impl *)statechart->impl : NULL;
}

const cmeta_type_desc *cflow_statechart_state_type(
    const cflow_statechart *statechart) {
    const cflow_statechart_impl *impl = statechart_impl(statechart);
    return impl != NULL ? impl->state_type : NULL;
}

#define CFLOW_STATECHART_COUNT_QUERY(name, field)                         \
    size_t name(const cflow_statechart *statechart) {                     \
        const cflow_statechart_impl *impl = statechart_impl(statechart);  \
        return impl != NULL ? impl->field : 0u;                           \
    }

CFLOW_STATECHART_COUNT_QUERY(cflow_statechart_state_count, state_count)
CFLOW_STATECHART_COUNT_QUERY(cflow_statechart_event_count, event_count)
CFLOW_STATECHART_COUNT_QUERY(cflow_statechart_guard_count, guard_count)
CFLOW_STATECHART_COUNT_QUERY(
    cflow_statechart_executable_count, executable_count)
CFLOW_STATECHART_COUNT_QUERY(
    cflow_statechart_transition_count, transition_count)
CFLOW_STATECHART_COUNT_QUERY(
    cflow_statechart_state_action_count, state_action_count)
CFLOW_STATECHART_COUNT_QUERY(
    cflow_statechart_transition_action_count, transition_action_count)

#undef CFLOW_STATECHART_COUNT_QUERY

#define CFLOW_STATECHART_ROW_QUERY(type, name, field, count_field)        \
    const type *name(const cflow_statechart *statechart, size_t index) {   \
        const cflow_statechart_impl *impl = statechart_impl(statechart);  \
        return impl != NULL && index < impl->count_field                  \
            ? &impl->field[index] : NULL;                                 \
    }

CFLOW_STATECHART_ROW_QUERY(
    cflow_statechart_state, cflow_statechart_state_at, states, state_count)
CFLOW_STATECHART_ROW_QUERY(
    cflow_event_type, cflow_statechart_event_at, events, event_count)
CFLOW_STATECHART_ROW_QUERY(
    cflow_statechart_guard, cflow_statechart_guard_at, guards, guard_count)
CFLOW_STATECHART_ROW_QUERY(
    cflow_statechart_executable, cflow_statechart_executable_at,
    executables, executable_count)
CFLOW_STATECHART_ROW_QUERY(
    cflow_statechart_transition, cflow_statechart_transition_at,
    transitions, transition_count)
CFLOW_STATECHART_ROW_QUERY(
    cflow_statechart_state_action, cflow_statechart_state_action_at,
    state_actions, state_action_count)
CFLOW_STATECHART_ROW_QUERY(
    cflow_statechart_transition_action,
    cflow_statechart_transition_action_at,
    transition_actions, transition_action_count)

#undef CFLOW_STATECHART_ROW_QUERY
