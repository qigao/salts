#include <cmeta/infer.h>

static cmeta_infer_status cmeta_infer_build_fail(
    cmeta_infer_dfa *dfa, cmeta_infer_status status) {
    if (dfa != NULL) {
        dfa->state_count = 0u;
        dfa->transition_count = 0u;
        dfa->arity = 0u;
        dfa->built = false;
    }
    return status;
}

static bool cmeta_infer_find_transition_linear(
    const cmeta_infer_dfa *dfa,
    size_t from,
    cmeta_infer_symbol symbol,
    size_t *out_to) {
    size_t index;

    for (index = 0u; index < dfa->transition_count; ++index) {
        const cmeta_infer_transition *transition = &dfa->transitions[index];
        if (transition->from == from && transition->symbol == symbol) {
            if (out_to != NULL) *out_to = transition->to;
            return true;
        }
    }
    return false;
}

static bool cmeta_infer_transition_less(
    const cmeta_infer_transition *left,
    const cmeta_infer_transition *right) {
    return left->from < right->from ||
           (left->from == right->from && left->symbol < right->symbol);
}

/* T <= rule_count * 3 for macro-declared relations. In-place insertion sort
 * keeps the no-allocation contract; time O(T^2), auxiliary space O(1). */
static void cmeta_infer_sort_transitions(cmeta_infer_dfa *dfa) {
    size_t index;

    for (index = 1u; index < dfa->transition_count; ++index) {
        cmeta_infer_transition value = dfa->transitions[index];
        size_t position = index;
        while (position > 0u && cmeta_infer_transition_less(
                   &value, &dfa->transitions[position - 1u])) {
            dfa->transitions[position] = dfa->transitions[position - 1u];
            --position;
        }
        dfa->transitions[position] = value;
    }
}

static bool cmeta_infer_find_transition(
    const cmeta_infer_dfa *dfa,
    size_t from,
    cmeta_infer_symbol symbol,
    size_t *out_to) {
    size_t low = 0u;
    size_t high = dfa->transition_count;

    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const cmeta_infer_transition *transition = &dfa->transitions[middle];
        if (transition->from < from ||
            (transition->from == from && transition->symbol < symbol)) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low >= dfa->transition_count || dfa->transitions[low].from != from ||
        dfa->transitions[low].symbol != symbol)
        return false;
    if (out_to != NULL) *out_to = dfa->transitions[low].to;
    return true;
}

void cmeta_infer_dfa_init(cmeta_infer_dfa *dfa,
                          cmeta_infer_state *states,
                          size_t state_capacity,
                          cmeta_infer_transition *transitions,
                          size_t transition_capacity) {
    if (dfa == NULL) return;
    dfa->states = states;
    dfa->state_capacity = state_capacity;
    dfa->state_count = 0u;
    dfa->transitions = transitions;
    dfa->transition_capacity = transition_capacity;
    dfa->transition_count = 0u;
    dfa->arity = 0u;
    dfa->built = false;
}

/* R is rule_count and k <= 3. Prefix insertion is O(R^2*k), final in-place
 * sorting is O((R*k)^2), and auxiliary space beyond caller workspace is O(1). */
cmeta_infer_status cmeta_infer_dfa_build(
    cmeta_infer_dfa *dfa, const cmeta_infer_relation *relation) {
    size_t rule_index;

    if (dfa == NULL || relation == NULL || relation->rules == NULL ||
        relation->rule_count == 0u || relation->arity == 0u ||
        relation->arity > CMETA_INFER_MAX_ARITY || dfa->states == NULL ||
        dfa->transitions == NULL || dfa->state_capacity == 0u)
        return cmeta_infer_build_fail(dfa, CMETA_INFER_INVALID_ARGUMENT);

    dfa->state_count = 1u;
    dfa->transition_count = 0u;
    dfa->arity = relation->arity;
    dfa->built = false;
    dfa->states[0].result = 0u;
    dfa->states[0].accepting = false;

    for (rule_index = 0u; rule_index < relation->rule_count; ++rule_index) {
        const cmeta_infer_rule *rule = &relation->rules[rule_index];
        size_t state = 0u;
        size_t symbol_index;

        for (symbol_index = 0u; symbol_index < relation->arity;
             ++symbol_index) {
            size_t next;
            if (cmeta_infer_find_transition_linear(
                    dfa, state, rule->symbols[symbol_index], &next)) {
                state = next;
                continue;
            }
            if (dfa->state_count >= dfa->state_capacity ||
                dfa->transition_count >= dfa->transition_capacity)
                return cmeta_infer_build_fail(
                    dfa, CMETA_INFER_CAPACITY_EXCEEDED);

            next = dfa->state_count++;
            dfa->states[next].result = 0u;
            dfa->states[next].accepting = false;
            dfa->transitions[dfa->transition_count++] =
                (cmeta_infer_transition){
                    state, rule->symbols[symbol_index], next
                };
            state = next;
        }

        if (dfa->states[state].accepting) {
            cmeta_infer_status status =
                dfa->states[state].result == rule->result
                    ? CMETA_INFER_DUPLICATE_RULE
                    : CMETA_INFER_AMBIGUOUS_RULE;
            return cmeta_infer_build_fail(dfa, status);
        }
        dfa->states[state].result = rule->result;
        dfa->states[state].accepting = true;
    }

    cmeta_infer_sort_transitions(dfa);
    dfa->built = true;
    return CMETA_INFER_OK;
}

/* T is transition_count and k is the fixed relation arity. Time
 * O(k*log(T)); auxiliary space O(1). */
cmeta_infer_status cmeta_infer_dfa_eval(
    const cmeta_infer_dfa *dfa,
    const cmeta_infer_symbol *symbols,
    size_t symbol_count,
    cmeta_infer_value *out_result) {
    size_t state = 0u;
    size_t index;

    if (dfa == NULL || !dfa->built || dfa->states == NULL ||
        dfa->transitions == NULL || symbols == NULL || out_result == NULL ||
        symbol_count != dfa->arity)
        return CMETA_INFER_INVALID_ARGUMENT;

    for (index = 0u; index < symbol_count; ++index) {
        if (!cmeta_infer_find_transition(dfa, state, symbols[index], &state))
            return CMETA_INFER_NO_RULE;
    }
    if (state >= dfa->state_count || !dfa->states[state].accepting)
        return CMETA_INFER_NO_RULE;

    *out_result = dfa->states[state].result;
    return CMETA_INFER_OK;
}

const char *cmeta_infer_status_string(cmeta_infer_status status) {
    switch (status) {
        case CMETA_INFER_OK: return "ok";
        case CMETA_INFER_INVALID_ARGUMENT: return "invalid_argument";
        case CMETA_INFER_CAPACITY_EXCEEDED: return "capacity_exceeded";
        case CMETA_INFER_DUPLICATE_RULE: return "duplicate_rule";
        case CMETA_INFER_AMBIGUOUS_RULE: return "ambiguous_rule";
        case CMETA_INFER_NO_RULE: return "no_rule";
        default: return "unknown";
    }
}
