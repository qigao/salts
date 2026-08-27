#include <cflow/statechart.h>
#include "tinytest.h"

#include <string.h>

typedef struct statechart_fixture {
    cflow_statechart_state states[4];
    cflow_event_type events[1];
    cflow_statechart_transition transitions[3];
    cflow_statechart_definition definition;
} statechart_fixture;

static void valid_fixture(statechart_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        1u, 0u, CFLOW_STATECHART_COMPOUND, 0u};
    fixture->states[1] = (cflow_statechart_state){
        2u, 1u, CFLOW_STATECHART_INITIAL, 1u};
    fixture->states[2] = (cflow_statechart_state){
        3u, 1u, CFLOW_STATECHART_ATOMIC, 2u};
    fixture->states[3] = (cflow_statechart_state){
        4u, 1u, CFLOW_STATECHART_FINAL, 3u};
    fixture->events[0] = (cflow_event_type){100u, &cmeta_type_bool};
    fixture->transitions[0] = (cflow_statechart_transition){
        10u, 2u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 3u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        11u, 3u, CFLOW_STATECHART_TRIGGER_EVENT, 100u, 0u, 0u, 4u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int,
        fixture->states, 4u,
        fixture->events, 1u,
        NULL, 0u,
        NULL, 0u,
        fixture->transitions, 2u,
        NULL, 0u,
        NULL, 0u};
}

static cflow_statechart_status build_status(
    const cflow_statechart_definition *definition) {
    cflow_statechart statechart = {0};
    const cflow_statechart_status status =
        cflow_statechart_build(&statechart, definition);
    cflow_statechart_destroy(&statechart);
    return status;
}

suite("CFlow format-neutral Statechart IR") {
    it("copies and publishes a valid compound declaration atomically") {
        statechart_fixture fixture;
        cflow_statechart statechart = {0};
        valid_fixture(&fixture);

        check_equal(cflow_statechart_build(&statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);
        check_not_null(statechart.impl);
        check_equal(cflow_statechart_state_count(&statechart), (size_t)4u);
        check_equal(cflow_statechart_event_count(&statechart), (size_t)1u);
        check_equal(cflow_statechart_transition_count(&statechart),
                    (size_t)2u);
        check_equal(cflow_statechart_state_at(&statechart, 0u)->id,
                    (cflow_machine_state_id)1u);
        check_equal(cflow_statechart_transition_at(&statechart, 1u)->id,
                    (cflow_statechart_transition_id)11u);

        fixture.states[0].id = 99u;
        fixture.transitions[1].target = 3u;
        check_equal(cflow_statechart_state_at(&statechart, 0u)->id,
                    (cflow_machine_state_id)1u);
        check_equal(cflow_statechart_transition_at(&statechart, 1u)->target,
                    (cflow_machine_state_id)4u);

        cflow_statechart_destroy(&statechart);
        check_null(statechart.impl);
        cflow_statechart_destroy(&statechart);
    }

    it("accepts parallel regions and shallow and deep history defaults") {
        cflow_statechart_state states[] = {
            {1u, 0u, CFLOW_STATECHART_PARALLEL, 0u},
            {2u, 1u, CFLOW_STATECHART_COMPOUND, 1u},
            {3u, 2u, CFLOW_STATECHART_INITIAL, 2u},
            {4u, 2u, CFLOW_STATECHART_ATOMIC, 3u},
            {5u, 2u, CFLOW_STATECHART_HISTORY_SHALLOW, 4u},
            {6u, 1u, CFLOW_STATECHART_COMPOUND, 5u},
            {7u, 6u, CFLOW_STATECHART_INITIAL, 6u},
            {8u, 6u, CFLOW_STATECHART_ATOMIC, 7u},
            {9u, 6u, CFLOW_STATECHART_HISTORY_DEEP, 8u}
        };
        cflow_statechart_transition transitions[] = {
            {10u, 3u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 4u,
             CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u},
            {11u, 5u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 4u,
             CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u},
            {12u, 7u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 8u,
             CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 2u},
            {13u, 9u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 8u,
             CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 3u}
        };
        const cflow_statechart_definition definition = {
            &cmeta_type_int, states, 9u, NULL, 0u, NULL, 0u, NULL, 0u,
            transitions, 4u, NULL, 0u, NULL, 0u};

        check_equal(build_status(&definition), CFLOW_STATECHART_OK);
    }

    it("rejects invalid input without publishing a partial object") {
        statechart_fixture fixture;
        cflow_statechart statechart = {0};
        valid_fixture(&fixture);

        fixture.definition.state_type = NULL;
        check_equal(cflow_statechart_build(&statechart, &fixture.definition),
                    CFLOW_STATECHART_INVALID_TYPE);
        check_null(statechart.impl);

        valid_fixture(&fixture);
        fixture.definition.state_count = 0u;
        check_equal(cflow_statechart_build(&statechart, &fixture.definition),
                    CFLOW_STATECHART_EMPTY);
        check_null(statechart.impl);

        valid_fixture(&fixture);
        fixture.definition.states = NULL;
        check_equal(cflow_statechart_build(&statechart, &fixture.definition),
                    CFLOW_STATECHART_INVALID_ARGUMENT);
        check_null(statechart.impl);
    }

    it("rejects duplicate IDs and document order") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.states[3].id = fixture.states[2].id;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_DUPLICATE_ID);

        valid_fixture(&fixture);
        fixture.states[3].document_order = fixture.states[2].document_order;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_DUPLICATE_ORDER);

        valid_fixture(&fixture);
        fixture.transitions[1].document_order =
            fixture.transitions[0].document_order;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_DUPLICATE_ORDER);
    }

    it("rejects missing parents cycles and multiple roots") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.states[2].parent = 99u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_PARENT);

        valid_fixture(&fixture);
        fixture.states[0].parent = 3u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_TREE);

        valid_fixture(&fixture);
        fixture.states[3].parent = 0u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_TREE);
    }

    it("rejects illegal compound parallel and pseudo-state children") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.states[1].kind = CFLOW_STATECHART_ATOMIC;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_INITIAL);

        valid_fixture(&fixture);
        fixture.states[0].kind = CFLOW_STATECHART_PARALLEL;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_INITIAL);

        valid_fixture(&fixture);
        fixture.states[3].parent = 3u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_STATE_KIND);
    }

    it("requires one unguarded eventless target for initial and history") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.transitions[0].trigger = CFLOW_STATECHART_TRIGGER_EVENT;
        fixture.transitions[0].event = 100u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_INITIAL);

        valid_fixture(&fixture);
        fixture.transitions[0].target = 0u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_INITIAL);

        valid_fixture(&fixture);
        fixture.transitions[1] = fixture.transitions[0];
        fixture.transitions[1].id = 12u;
        fixture.transitions[1].document_order = 1u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_INITIAL);

        valid_fixture(&fixture);
        fixture.transitions[0].target = 1u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_INITIAL);
    }

    it("reports duplicate transition IDs before duplicate document order") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.transitions[1].id = fixture.transitions[0].id;
        fixture.transitions[1].document_order =
            fixture.transitions[0].document_order;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_DUPLICATE_ID);
    }

    it("rejects configured row limits before reading declaration storage") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.definition.state_action_count =
            (size_t)CFLOW_STATECHART_MAX_ACTION_REFS + 1u;
        fixture.definition.state_actions =
            (const cflow_statechart_state_action *)(uintptr_t)1u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_LIMIT_EXCEEDED);
    }

    it("validates Event and completion triggers") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.transitions[1].event = 999u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_UNKNOWN_EVENT);

        valid_fixture(&fixture);
        fixture.transitions[1].trigger =
            CFLOW_STATECHART_TRIGGER_COMPLETION;
        fixture.transitions[1].event = 0u;
        fixture.transitions[1].completion = 3u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_INVALID_COMPLETION);

        fixture.transitions[1].completion = 1u;
        check_equal(build_status(&fixture.definition), CFLOW_STATECHART_OK);
    }

    it("rejects unknown guards executables and ambiguous transitions") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        fixture.transitions[1].guard = 77u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_UNKNOWN_GUARD);

        valid_fixture(&fixture);
        {
            const cflow_statechart_state_action state_action = {
                3u, CFLOW_STATECHART_STATE_ACTION_ENTRY, 88u, 0u};
            fixture.definition.state_actions = &state_action;
            fixture.definition.state_action_count = 1u;
            check_equal(build_status(&fixture.definition),
                        CFLOW_STATECHART_UNKNOWN_EXECUTABLE);
        }

        valid_fixture(&fixture);
        fixture.transitions[2] = fixture.transitions[1];
        fixture.transitions[2].id = 12u;
        fixture.transitions[2].document_order = 2u;
        fixture.definition.transition_count = 3u;
        check_equal(build_status(&fixture.definition),
                    CFLOW_STATECHART_AMBIGUOUS_TRANSITION);
    }

    it("normalizes typed guard executable and ordered action references") {
        statechart_fixture fixture;
        valid_fixture(&fixture);
        const cflow_statechart_guard guards[] = {
            {20u, &cmeta_type_int, CMETA_EFFECT_PURE,
             CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS}
        };
        const cflow_statechart_executable executables[] = {
            {30u, &cmeta_type_int, CMETA_EFFECT_PURE,
             CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS}
        };
        const cflow_statechart_state_action state_actions[] = {
            {3u, CFLOW_STATECHART_STATE_ACTION_ENTRY, 30u, 1u},
            {3u, CFLOW_STATECHART_STATE_ACTION_ENTRY, 30u, 0u}
        };
        const cflow_statechart_transition_action transition_actions[] = {
            {11u, 30u, 0u}
        };
        cflow_statechart statechart = {0};

        fixture.transitions[1].guard = 20u;
        fixture.definition.guards = guards;
        fixture.definition.guard_count = 1u;
        fixture.definition.executables = executables;
        fixture.definition.executable_count = 1u;
        fixture.definition.state_actions = state_actions;
        fixture.definition.state_action_count = 2u;
        fixture.definition.transition_actions = transition_actions;
        fixture.definition.transition_action_count = 1u;
        check_equal(cflow_statechart_build(&statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);
        check_equal(cflow_statechart_guard_count(&statechart), (size_t)1u);
        check_equal(cflow_statechart_executable_count(&statechart),
                    (size_t)1u);
        check_equal(cflow_statechart_state_action_at(&statechart, 0u)->order,
                    (uint32_t)0u);
        check_equal(cflow_statechart_transition_action_at(
                        &statechart, 0u)->transition,
                    (cflow_statechart_transition_id)11u);
        cflow_statechart_destroy(&statechart);
    }
}
