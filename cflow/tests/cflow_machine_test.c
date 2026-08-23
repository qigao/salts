#include <cflow/machine.h>
#include "tinytest.h"

static cflow_machine_definition valid_definition(
    cflow_machine_state *states,
    cflow_event_type *events,
    cflow_machine_guard *guards,
    cflow_machine_action *actions,
    cflow_machine_transition *transitions) {
    states[0] = (cflow_machine_state){
        20u, &cmeta_type_long, CFLOW_MACHINE_STATE_DONE};
    states[1] = (cflow_machine_state){
        10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE};
    events[0] = (cflow_event_type){100u, &cmeta_type_bool};
    guards[0] = (cflow_machine_guard){
        200u, &cmeta_type_int, 100u, &cmeta_type_bool,
        CMETA_EFFECT_PURE, CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
    actions[0] = (cflow_machine_action){
        300u, &cmeta_type_int, 100u, &cmeta_type_bool,
        &cmeta_type_long, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_long, 0u};
    transitions[0] = (cflow_machine_transition){
        10u, 100u, 200u, 300u, 20u, 7u};
    return (cflow_machine_definition){
        states, 2u, 10u,
        events, 1u,
        guards, 1u,
        actions, 1u,
        transitions, 1u};
}

static cflow_machine_status build_status(
    const cflow_machine_definition *definition) {
    cflow_machine machine = {0};
    const cflow_machine_status status = cflow_machine_build(&machine, definition);
    if (status != CFLOW_MACHINE_OK) check_null(machine.impl);
    cflow_machine_destroy(&machine);
    return status;
}

suite("CFlow typed Machine IR") {
    it("builds one canonical immutable machine") {
        cflow_machine_state states[2];
        cflow_event_type events[1];
        cflow_machine_guard guards[1];
        cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        cflow_machine machine = {0};

        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_OK);
        check_equal(cflow_machine_initial_state(&machine),
                    (cflow_machine_state_id)10u);
        check_equal(cflow_machine_state_count(&machine), (size_t)2u);
        check_equal(cflow_machine_event_count(&machine), (size_t)1u);
        check_equal(cflow_machine_guard_count(&machine), (size_t)1u);
        check_equal(cflow_machine_action_count(&machine), (size_t)1u);
        check_equal(cflow_machine_transition_count(&machine), (size_t)1u);

        states[0].id = 99u;
        transitions[0].priority = 0u;
        check_equal(cflow_machine_state_at(&machine, 0u)->id,
                    (cflow_machine_state_id)10u);
        check_equal(cflow_machine_state_at(&machine, 1u)->id,
                    (cflow_machine_state_id)20u);
        check_equal(cflow_machine_transition_at(&machine, 0u)->priority,
                    (uint32_t)7u);
        check_equal(cflow_machine_event_at(&machine, 0u)->id,
                    (cflow_event_id)100u);
        check_equal(cflow_machine_guard_at(&machine, 0u)->id,
                    (cflow_machine_guard_id)200u);
        check_equal(cflow_machine_action_at(&machine, 0u)->effects,
                    (cmeta_effects)CMETA_EFFECT_MAY_FAIL);
        check_null(cflow_machine_state_at(&machine, 2u));
        check_null(cflow_machine_transition_at(&machine, 1u));

        cflow_machine_destroy(&machine);
        check_null(machine.impl);
    }

    it("rejects an empty state set without publishing a partial machine") {
        const cflow_machine_definition definition = {0};
        cflow_machine machine = {0};

        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_EMPTY);
        check_null(machine.impl);
        check_equal(cflow_machine_state_count(&machine), (size_t)0u);
    }

    it("leaves an existing machine unchanged instead of overwriting it") {
        cflow_machine_state states[2];
        cflow_event_type events[1];
        cflow_machine_guard guards[1];
        cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        cflow_machine machine = {0};

        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_OK);
        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_INVALID_ARGUMENT);
        check_equal(cflow_machine_initial_state(&machine),
                    (cflow_machine_state_id)10u);
        cflow_machine_destroy(&machine);
    }

    it("rejects a zero state identifier") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        states[0].id = 0u;
        check_equal(build_status(&definition), CFLOW_MACHINE_INVALID_ID);
    }

    it("rejects duplicate identifiers within a declaration domain") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        states[0].id = states[1].id;
        check_equal(build_status(&definition), CFLOW_MACHINE_DUPLICATE_ID);
    }

    it("rejects an invalid state value type") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        states[1].value_type = NULL;
        check_equal(build_status(&definition), CFLOW_MACHINE_INVALID_TYPE);
    }

    it("rejects an unknown initial state") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        definition.initial_state = 99u;
        check_equal(build_status(&definition), CFLOW_MACHINE_UNKNOWN_STATE);
    }

    it("rejects an unknown transition source") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0].source = 99u;
        check_equal(build_status(&definition), CFLOW_MACHINE_UNKNOWN_STATE);
    }

    it("rejects an unknown transition target") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0].target = 99u;
        check_equal(build_status(&definition), CFLOW_MACHINE_UNKNOWN_STATE);
    }

    it("rejects an unknown transition event") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0].event = 999u;
        check_equal(build_status(&definition), CFLOW_MACHINE_UNKNOWN_EVENT);
    }

    it("rejects an unknown transition guard") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0].guard = 999u;
        check_equal(build_status(&definition), CFLOW_MACHINE_UNKNOWN_GUARD);
    }

    it("rejects an unknown transition action") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0].action = 999u;
        check_equal(build_status(&definition), CFLOW_MACHINE_UNKNOWN_ACTION);
    }

    it("rejects guard types that do not match source and Event types") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        guards[0].state_type = &cmeta_type_long;
        check_equal(build_status(&definition), CFLOW_MACHINE_TYPE_MISMATCH);
    }

    it("rejects action types that do not match the transition") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        actions[0].target_type = &cmeta_type_int;
        check_equal(build_status(&definition), CFLOW_MACHINE_TYPE_MISMATCH);
    }

    it("rejects guards outside the pure stable admitted fragment") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        guards[0].effects = CMETA_EFFECT_MAY_FAIL;
        check_equal(build_status(&definition), CFLOW_MACHINE_INVALID_CONTRACT);
    }

    it("rejects a value observation without an output type") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        actions[0].output_type = NULL;
        check_equal(build_status(&definition),
                    CFLOW_MACHINE_INVALID_OBSERVATION);
    }

    it("rejects outgoing transitions from terminal states") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0] = (cflow_machine_transition){20u, 100u, 0u, 0u,
                                                    20u, 7u};
        check_equal(build_status(&definition),
                    CFLOW_MACHINE_TERMINAL_TRANSITION);
    }

    it("rejects equal-priority transitions for one state and Event") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[2];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[1] = transitions[0];
        definition.transition_count = 2u;
        check_equal(build_status(&definition),
                    CFLOW_MACHINE_AMBIGUOUS_TRANSITION);
    }

    it("rejects a declared state unreachable from the initial state") {
        cflow_machine_state states[3]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        states[2] = (cflow_machine_state){30u, &cmeta_type_bool,
                                          CFLOW_MACHINE_STATE_ACTIVE};
        definition.state_count = 3u;
        check_equal(build_status(&definition),
                    CFLOW_MACHINE_UNREACHABLE_STATE);
    }

    it("rejects an unreferenced guard declaration") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        transitions[0].guard = 0u;
        check_equal(build_status(&definition),
                    CFLOW_MACHINE_UNUSED_DECLARATION);
    }

    it("rejects an unreferenced action declaration") {
        cflow_machine_state states[2]; cflow_event_type events[1];
        cflow_machine_guard guards[1]; cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = valid_definition(
            states, events, guards, actions, transitions);
        states[0].value_type = &cmeta_type_int;
        transitions[0].action = 0u;
        check_equal(build_status(&definition),
                    CFLOW_MACHINE_UNUSED_DECLARATION);
    }

    it("admits an initial terminal machine with no transition domains") {
        const cflow_machine_state states[] = {{1u, &cmeta_type_int,
                                               CFLOW_MACHINE_STATE_DONE}};
        const cflow_machine_definition definition = {
            states, 1u, 1u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u};
        check_equal(build_status(&definition), CFLOW_MACHINE_OK);
    }

    it("keeps the C enum surface aligned with the Lean-owned schema") {
        check_equal((uint32_t)CFLOW_MACHINE_SCHEMA_VERSION, (uint32_t)1u);
        check_equal((uint32_t)CFLOW_MACHINE_STATE_KIND_COUNT, (uint32_t)3u);
        check_equal((uint32_t)CFLOW_MACHINE_ACTION_OBSERVATION_COUNT,
                    (uint32_t)3u);
        check_equal((int)CFLOW_MACHINE_STATE_ACTIVE, 0);
        check_equal((int)CFLOW_MACHINE_STATE_DONE, 1);
        check_equal((int)CFLOW_MACHINE_STATE_ERROR, 2);
        check_equal((int)CFLOW_MACHINE_ACTION_NONE, 0);
        check_equal((int)CFLOW_MACHINE_ACTION_VALUE, 1);
        check_equal((int)CFLOW_MACHINE_ACTION_EVENT, 2);
    }
}
