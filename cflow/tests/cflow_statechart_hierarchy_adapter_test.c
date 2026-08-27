#include <cflow/statechart_hierarchy_adapter.h>

#include "tinytest.h"

#include <string.h>

typedef struct adapter_callback_probe {
    size_t guards;
    size_t actions;
    bool fail_action;
} adapter_callback_probe;

typedef struct adapter_definition_fixture {
    cflow_machine_hierarchy_state states[5];
    cflow_event_type events[2];
    cflow_machine_guard guards[1];
    cflow_machine_action actions[1];
    cflow_machine_transition transitions[2];
    cflow_machine_hierarchy_definition definition;
} adapter_definition_fixture;

typedef struct adapter_runtime_fixture {
    adapter_definition_fixture declaration;
    cflow_machine_hierarchy hierarchy;
    cflow_statechart statechart;
    cflow_executor machine_executor;
    cflow_executor statechart_executor;
    cflow_clock machine_clock;
    cflow_clock statechart_clock;
    cflow_machine_hierarchy_instance machine;
    cflow_resumable machine_resumable;
    cflow_resume_ctx machine_resume_context;
    cflow_statechart_instance statechart_instance;
    adapter_callback_probe machine_probe;
    adapter_callback_probe statechart_probe;
    cflow_machine_guard_binding machine_guard;
    cflow_machine_action_binding machine_action;
    cflow_statechart_hierarchy_guard_context guard_context;
    cflow_statechart_hierarchy_action_context action_context;
    cflow_statechart_guard_binding statechart_guard;
    cflow_statechart_executable_binding statechart_action;
} adapter_runtime_fixture;

enum {
    ADAPTER_ROOT = 1u,
    ADAPTER_GROUP = 2u,
    ADAPTER_FIRST = 3u,
    ADAPTER_SECOND = 4u,
    ADAPTER_DONE = 5u,
    ADAPTER_NEXT = 10u,
    ADAPTER_FINISH = 11u,
    ADAPTER_GUARD = 20u,
    ADAPTER_ACTION = 30u
};

static void adapter_definition_init(adapter_definition_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_machine_hierarchy_state){
        ADAPTER_ROOT, 0u, ADAPTER_GROUP, &cmeta_type_int,
        CFLOW_MACHINE_STATE_ACTIVE};
    fixture->states[1] = (cflow_machine_hierarchy_state){
        ADAPTER_GROUP, ADAPTER_ROOT, ADAPTER_FIRST, &cmeta_type_int,
        CFLOW_MACHINE_STATE_ACTIVE};
    fixture->states[2] = (cflow_machine_hierarchy_state){
        ADAPTER_FIRST, ADAPTER_GROUP, 0u, &cmeta_type_int,
        CFLOW_MACHINE_STATE_ACTIVE};
    fixture->states[3] = (cflow_machine_hierarchy_state){
        ADAPTER_SECOND, ADAPTER_GROUP, 0u, &cmeta_type_int,
        CFLOW_MACHINE_STATE_ACTIVE};
    fixture->states[4] = (cflow_machine_hierarchy_state){
        ADAPTER_DONE, ADAPTER_ROOT, 0u, &cmeta_type_int,
        CFLOW_MACHINE_STATE_DONE};
    fixture->events[0] = (cflow_event_type){ADAPTER_NEXT, &cmeta_type_bool};
    fixture->events[1] = (cflow_event_type){ADAPTER_FINISH, &cmeta_type_bool};
    fixture->guards[0] = (cflow_machine_guard){
        ADAPTER_GUARD, &cmeta_type_int, ADAPTER_NEXT, &cmeta_type_bool,
        CMETA_EFFECT_PURE, CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
    fixture->actions[0] = (cflow_machine_action){
        ADAPTER_ACTION, &cmeta_type_int, ADAPTER_NEXT, &cmeta_type_bool,
        &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_NONE, NULL, 0u};
    fixture->transitions[0] = (cflow_machine_transition){
        ADAPTER_FIRST, ADAPTER_NEXT, ADAPTER_GUARD, ADAPTER_ACTION,
        ADAPTER_SECOND, 9u};
    fixture->transitions[1] = (cflow_machine_transition){
        ADAPTER_GROUP, ADAPTER_FINISH, 0u, 0u, ADAPTER_DONE, 0u};
    fixture->definition = (cflow_machine_hierarchy_definition){
        fixture->states, 5u, ADAPTER_ROOT,
        fixture->events, 2u,
        fixture->guards, 1u,
        fixture->actions, 1u,
        fixture->transitions, 2u};
}

static bool adapter_guard_callback(
    void *user, const void *state, const void *event,
    bool *out_enabled, const char **out_error) {
    adapter_callback_probe *probe = (adapter_callback_probe *)user;
    (void)state;
    (void)out_error;
    ++probe->guards;
    *out_enabled = *(const bool *)event;
    return true;
}

static bool adapter_action_callback(
    void *user, const void *state, const void *event,
    void *out_target_state, void *out_observation,
    const char **out_error) {
    adapter_callback_probe *probe = (adapter_callback_probe *)user;
    (void)out_observation;
    ++probe->actions;
    if (probe->fail_action) {
        *out_error = "adapter action failed";
        return false;
    }
    *(int *)out_target_state = *(const int *)state +
        (*(const bool *)event ? 1 : 0);
    return true;
}

static bool adapter_runtime_init(
    adapter_runtime_fixture *fixture, bool fail_action) {
    cflow_machine_hierarchy_instance_config machine_config;
    cflow_machine_hierarchy_instance_init_result machine_result;
    cflow_statechart_instance_config statechart_config;
    cflow_statechart_hierarchy_adapter_result adapter_result;
    cflow_step initial_step;
    const int initial_state = 7;
    int machine_output = 0;
    memset(fixture, 0, sizeof(*fixture));
    adapter_definition_init(&fixture->declaration);
    if (cflow_machine_hierarchy_build(
            &fixture->hierarchy, &fixture->declaration.definition) !=
            CFLOW_MACHINE_HIERARCHY_OK)
        return false;
    adapter_result = cflow_statechart_hierarchy_adapter_build(
        &fixture->statechart, &fixture->hierarchy);
    if (adapter_result.status != CFLOW_STATECHART_HIERARCHY_ADAPTER_OK)
        return false;
    fixture->machine_probe.fail_action = fail_action;
    fixture->statechart_probe.fail_action = fail_action;
    fixture->machine_guard = (cflow_machine_guard_binding){
        ADAPTER_GUARD, adapter_guard_callback, &fixture->machine_probe};
    fixture->machine_action = (cflow_machine_action_binding){
        ADAPTER_ACTION, adapter_action_callback, &fixture->machine_probe};
    {
        const cflow_machine_guard_binding source = {
            ADAPTER_GUARD, adapter_guard_callback, &fixture->statechart_probe};
        if (!cflow_statechart_hierarchy_adapt_guard_binding(
                &source, &fixture->guard_context,
                &fixture->statechart_guard))
            return false;
    }
    {
        const cflow_machine_action_binding source = {
            ADAPTER_ACTION, adapter_action_callback,
            &fixture->statechart_probe};
        if (!cflow_statechart_hierarchy_adapt_action_binding(
                &source, &fixture->action_context,
                &fixture->statechart_action))
            return false;
    }
    if (!cflow_executor_serial_init(&fixture->machine_executor) ||
        !cflow_executor_serial_init(&fixture->statechart_executor) ||
        !cflow_clock_virtual_init(
            &fixture->machine_clock, (cflow_instant){0u}) ||
        !cflow_clock_virtual_init(
            &fixture->statechart_clock, (cflow_instant){0u}))
        return false;
    machine_config = (cflow_machine_hierarchy_instance_config){
        &fixture->hierarchy, &initial_state, &cmeta_type_int,
        &fixture->machine_guard, 1u,
        &fixture->machine_action, 1u,
        2u, &fixture->machine_executor, &fixture->machine_clock, 2u};
    machine_result = cflow_machine_hierarchy_instance_init(
        &fixture->machine, &machine_config);
    if (machine_result.status != CFLOW_MACHINE_HIERARCHY_INSTANCE_OK)
        return false;
    if (!cflow_machine_hierarchy_instance_as_resumable(
            &fixture->machine, &fixture->machine_resumable))
        return false;
    initial_step = fixture->machine_resumable.ops->resume(
        fixture->machine_resumable.state,
        &fixture->machine_resume_context,
        &machine_output);
    if (initial_step.kind != CFLOW_STEP_WAIT) return false;
    statechart_config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &initial_state,
        .guards = &fixture->statechart_guard,
        .guard_count = 1u,
        .executables = &fixture->statechart_action,
        .executable_count = 1u,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = 32u,
        .executor = &fixture->statechart_executor,
        .clock = &fixture->statechart_clock,
        .timer_capacity = 2u};
    return cflow_statechart_instance_init(
               &fixture->statechart_instance, &statechart_config) ==
        CFLOW_STATECHART_RUNTIME_OK;
}

static void adapter_runtime_destroy(adapter_runtime_fixture *fixture) {
    check_equal(cflow_statechart_instance_destroy(
                    &fixture->statechart_instance),
                CFLOW_STATECHART_RUNTIME_OK);
    if (fixture->machine_resumable.ops != NULL) {
        fixture->machine_resumable.ops->destroy(
            fixture->machine_resumable.state);
        fixture->machine_resumable = (cflow_resumable){0};
    }
    cflow_machine_hierarchy_instance_destroy(&fixture->machine);
    cflow_clock_destroy(&fixture->statechart_clock);
    cflow_clock_destroy(&fixture->machine_clock);
    cflow_executor_destroy(&fixture->statechart_executor);
    cflow_executor_destroy(&fixture->machine_executor);
    cflow_statechart_destroy(&fixture->statechart);
    cflow_machine_hierarchy_destroy(&fixture->hierarchy);
}

static void adapter_send_both(
    adapter_runtime_fixture *fixture, cflow_event_id id, bool payload) {
    const cflow_event_view event = {id, &cmeta_type_bool, &payload};
    check_equal(cflow_machine_hierarchy_instance_try_send(
                    &fixture->machine, &event),
                CFLOW_MAILBOX_OK);
    check_equal(cflow_statechart_instance_try_send(
                    &fixture->statechart_instance, &event),
                CFLOW_MAILBOX_OK);
    check_true(cflow_executor_wait_idle(&fixture->machine_executor));
    check_true(cflow_executor_wait_idle(&fixture->statechart_executor));
}

suite("CFlow exclusive hierarchy Statechart adapter") {
    it("projects normalized declarations without a second mutable state") {
        adapter_definition_fixture declaration;
        cflow_machine_hierarchy hierarchy = {0};
        cflow_statechart statechart = {0};
        cflow_statechart_hierarchy_adapter_result result;
        size_t index, initial_count = 0u, compound_count = 0u;
        bool found_leaf_transition = false;
        adapter_definition_init(&declaration);
        check_equal(cflow_machine_hierarchy_build(
                        &hierarchy, &declaration.definition),
                    CFLOW_MACHINE_HIERARCHY_OK);
        result = cflow_statechart_hierarchy_adapter_build(
            &statechart, &hierarchy);
        check_equal(result.statechart_status, CFLOW_STATECHART_OK);
        check_equal(result.status,
                    CFLOW_STATECHART_HIERARCHY_ADAPTER_OK);
        check_equal(cflow_statechart_state_count(&statechart), (size_t)7u);
        check_equal(cflow_statechart_transition_count(&statechart),
                    (size_t)5u);
        check_equal(cflow_statechart_guard_count(&statechart), (size_t)1u);
        check_equal(cflow_statechart_executable_count(&statechart),
                    (size_t)1u);
        check_equal(cflow_statechart_transition_action_count(&statechart),
                    (size_t)1u);
        for (index = 0u; index < cflow_statechart_state_count(&statechart);
             ++index) {
            const cflow_statechart_state *state =
                cflow_statechart_state_at(&statechart, index);
            if (state->kind == CFLOW_STATECHART_INITIAL) ++initial_count;
            if (state->kind == CFLOW_STATECHART_COMPOUND) ++compound_count;
        }
        for (index = 0u;
             index < cflow_statechart_transition_count(&statechart);
             ++index) {
            const cflow_statechart_transition *transition =
                cflow_statechart_transition_at(&statechart, index);
            if (transition->source == ADAPTER_FIRST &&
                transition->event == ADAPTER_NEXT) {
                found_leaf_transition = true;
                check_equal(transition->target,
                            (cflow_machine_state_id)ADAPTER_SECOND);
                check_equal(transition->guard,
                            (cflow_statechart_guard_id)ADAPTER_GUARD);
            }
        }
        check_equal(initial_count, (size_t)2u);
        check_equal(compound_count, (size_t)2u);
        check_true(found_leaf_transition);
        cflow_statechart_destroy(&statechart);
        cflow_machine_hierarchy_destroy(&hierarchy);
    }

    it("matches initial leaf selection route state timer and final result") {
        adapter_runtime_fixture fixture;
        cflow_machine_hierarchy_instance_stats machine_stats = {0};
        cflow_statechart_instance_stats statechart_stats = {0};
        cflow_timer_event_schedule_result machine_timer, statechart_timer;
        const cmeta_type_desc *machine_type = NULL;
        const cmeta_type_desc *statechart_type = NULL;
        const bool payload = true;
        const cflow_event_view timer_event = {
            ADAPTER_NEXT, &cmeta_type_bool, &payload};
        int machine_state = 0, statechart_state = 0;
        check_true(adapter_runtime_init(&fixture, false));
        check_equal(cflow_machine_hierarchy_instance_current_state(
                        &fixture.machine),
                    (cflow_machine_state_id)ADAPTER_FIRST);
        check_equal(cflow_statechart_instance_current_state(
                        &fixture.statechart_instance),
                    (cflow_machine_state_id)ADAPTER_FIRST);
        machine_timer = cflow_machine_hierarchy_instance_try_schedule_at(
            &fixture.machine, ADAPTER_FIRST,
            (cflow_deadline){100u}, &timer_event);
        statechart_timer = cflow_statechart_instance_try_schedule_at(
            &fixture.statechart_instance, ADAPTER_FIRST,
            (cflow_deadline){100u}, &timer_event);
        check_equal(machine_timer.status, CFLOW_TIMER_EVENT_OK);
        check_equal(statechart_timer.status, CFLOW_TIMER_EVENT_OK);

        adapter_send_both(&fixture, ADAPTER_NEXT, true);
        check_equal(fixture.machine_probe.guards, (size_t)1u);
        check_equal(fixture.statechart_probe.guards, (size_t)1u);
        check_equal(fixture.machine_probe.actions, (size_t)1u);
        check_equal(fixture.statechart_probe.actions, (size_t)1u);
        check_equal(cflow_machine_hierarchy_instance_current_state(
                        &fixture.machine),
                    (cflow_machine_state_id)ADAPTER_SECOND);
        check_equal(cflow_statechart_instance_current_state(
                        &fixture.statechart_instance),
                    (cflow_machine_state_id)ADAPTER_SECOND);
        check_true(cflow_machine_hierarchy_instance_copy_state(
            &fixture.machine, &machine_type, &machine_state,
            sizeof(machine_state)));
        check_true(cflow_statechart_instance_copy_state(
            &fixture.statechart_instance, &statechart_type,
            &statechart_state, sizeof(statechart_state)));
        check(machine_type == statechart_type);
        check_equal(machine_state, statechart_state);
        check_true(cflow_machine_hierarchy_instance_get_stats(
            &fixture.machine, &machine_stats));
        check_true(cflow_statechart_instance_get_stats(
            &fixture.statechart_instance, &statechart_stats));
        check_equal(machine_stats.timers.cancelled, (uint64_t)1u);
        check_equal(statechart_stats.timers.cancelled, (uint64_t)1u);

        adapter_send_both(&fixture, ADAPTER_FINISH, true);
        check_true(cflow_machine_hierarchy_instance_get_stats(
            &fixture.machine, &machine_stats));
        check_true(cflow_statechart_instance_get_stats(
            &fixture.statechart_instance, &statechart_stats));
        check_true(machine_stats.machine.done);
        check_true(statechart_stats.done);
        check_false(machine_stats.machine.errored);
        check_false(statechart_stats.errored);
        adapter_runtime_destroy(&fixture);
    }

    it("preserves the first fallible action error and rollback") {
        adapter_runtime_fixture fixture;
        cflow_machine_hierarchy_instance_stats machine_stats = {0};
        cflow_statechart_instance_stats statechart_stats = {0};
        const char *machine_error;
        const char *statechart_error;
        check_true(adapter_runtime_init(&fixture, true));
        adapter_send_both(&fixture, ADAPTER_NEXT, true);
        check_equal(cflow_machine_hierarchy_instance_current_state(
                        &fixture.machine),
                    (cflow_machine_state_id)ADAPTER_FIRST);
        check_equal(cflow_statechart_instance_current_state(
                        &fixture.statechart_instance),
                    (cflow_machine_state_id)ADAPTER_FIRST);
        machine_error = cflow_machine_hierarchy_instance_error(
            &fixture.machine);
        statechart_error = cflow_statechart_instance_error(
            &fixture.statechart_instance);
        check_not_null(machine_error);
        check_not_null(statechart_error);
        check_equal(strcmp(machine_error, statechart_error), 0);
        check_true(cflow_machine_hierarchy_instance_get_stats(
            &fixture.machine, &machine_stats));
        check_true(cflow_statechart_instance_get_stats(
            &fixture.statechart_instance, &statechart_stats));
        check_true(machine_stats.machine.errored);
        check_true(statechart_stats.errored);
        adapter_runtime_destroy(&fixture);
    }

    it("rejects hierarchy semantics that cannot be preserved exactly") {
        adapter_definition_fixture declaration;
        cflow_machine_hierarchy hierarchy = {0};
        cflow_statechart statechart = {0};
        cflow_statechart_hierarchy_adapter_result result;
        adapter_definition_init(&declaration);
        declaration.definition.initial_state = ADAPTER_SECOND;
        declaration.transitions[0].source = ADAPTER_SECOND;
        declaration.transitions[0].target = ADAPTER_FIRST;
        check_equal(cflow_machine_hierarchy_build(
                        &hierarchy, &declaration.definition),
                    CFLOW_MACHINE_HIERARCHY_OK);
        result = cflow_statechart_hierarchy_adapter_build(
            &statechart, &hierarchy);
        check_equal(result.status,
                    CFLOW_STATECHART_HIERARCHY_ADAPTER_INITIAL_STATE_UNSUPPORTED);
        cflow_machine_hierarchy_destroy(&hierarchy);

        adapter_definition_init(&declaration);
        declaration.states[4].kind = CFLOW_MACHINE_STATE_ERROR;
        check_equal(cflow_machine_hierarchy_build(
                        &hierarchy, &declaration.definition),
                    CFLOW_MACHINE_HIERARCHY_OK);
        result = cflow_statechart_hierarchy_adapter_build(
            &statechart, &hierarchy);
        check_equal(result.status,
                    CFLOW_STATECHART_HIERARCHY_ADAPTER_ERROR_STATE_UNSUPPORTED);
        cflow_machine_hierarchy_destroy(&hierarchy);

        adapter_definition_init(&declaration);
        declaration.actions[0].observation = CFLOW_MACHINE_ACTION_VALUE;
        declaration.actions[0].output_type = &cmeta_type_int;
        check_equal(cflow_machine_hierarchy_build(
                        &hierarchy, &declaration.definition),
                    CFLOW_MACHINE_HIERARCHY_OK);
        result = cflow_statechart_hierarchy_adapter_build(
            &statechart, &hierarchy);
        check_equal(result.status,
                    CFLOW_STATECHART_HIERARCHY_ADAPTER_OBSERVATION_UNSUPPORTED);
        cflow_machine_hierarchy_destroy(&hierarchy);
    }
}
