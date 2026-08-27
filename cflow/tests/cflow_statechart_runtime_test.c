#include <cflow/executor.h>
#include <cflow/statechart.h>

#include "statechart_runtime_internal.h"
#include "tinytest.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

typedef struct runtime_fixture {
    cflow_statechart_state states[7];
    cflow_statechart_transition transitions[4];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_statechart_instance instance;
    int initial_state;
} runtime_fixture;

typedef struct runtime_destroy_probe {
    cflow_statechart_instance *instance;
    atomic_int destroy_status;
    atomic_bool query_succeeded;
} runtime_destroy_probe;

static void destroy_from_executor_callback(void *user) {
    runtime_destroy_probe *probe = (runtime_destroy_probe *)user;
    cflow_machine_state_id states[3] = {0};
    size_t state_count = 0u;
    uint64_t version = 0u;
    cflow_statechart_runtime_status destroy_status;
    bool query_succeeded;
    destroy_status = cflow_statechart_instance_destroy(probe->instance);
    query_succeeded = destroy_status == CFLOW_STATECHART_RUNTIME_WOULD_BLOCK &&
        cflow_statechart_instance_copy_configuration(
            probe->instance, states, 3u, &state_count, &version) ==
            CFLOW_STATECHART_SNAPSHOT_OK &&
        state_count == 3u && version == UINT64_C(1) &&
        states[0] == 70u && states[1] == 20u && states[2] == 90u;
    atomic_store(&probe->destroy_status, (int)destroy_status);
    atomic_store(&probe->query_succeeded, query_succeeded);
}

static void nested_compound_fixture(runtime_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        90u, 20u, CFLOW_STATECHART_ATOMIC, 50u};
    fixture->states[1] = (cflow_statechart_state){
        40u, 70u, CFLOW_STATECHART_INITIAL, 30u};
    fixture->states[2] = (cflow_statechart_state){
        70u, 0u, CFLOW_STATECHART_COMPOUND, 10u};
    fixture->states[3] = (cflow_statechart_state){
        80u, 20u, CFLOW_STATECHART_INITIAL, 40u};
    fixture->states[4] = (cflow_statechart_state){
        20u, 70u, CFLOW_STATECHART_COMPOUND, 20u};
    fixture->transitions[0] = (cflow_statechart_transition){
        10u, 40u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 20u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        11u, 80u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 90u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 5u, NULL, 0u, NULL, 0u, NULL, 0u,
        fixture->transitions, 2u, NULL, 0u, NULL, 0u};
    fixture->initial_state = 41;
}

static void parallel_fixture(runtime_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        90u, 50u, CFLOW_STATECHART_ATOMIC, 40u};
    fixture->states[1] = (cflow_statechart_state){
        20u, 70u, CFLOW_STATECHART_COMPOUND, 50u};
    fixture->states[2] = (cflow_statechart_state){
        70u, 0u, CFLOW_STATECHART_PARALLEL, 10u};
    fixture->states[3] = (cflow_statechart_state){
        10u, 50u, CFLOW_STATECHART_INITIAL, 30u};
    fixture->states[4] = (cflow_statechart_state){
        50u, 70u, CFLOW_STATECHART_COMPOUND, 20u};
    fixture->states[5] = (cflow_statechart_state){
        80u, 20u, CFLOW_STATECHART_INITIAL, 60u};
    fixture->states[6] = (cflow_statechart_state){
        40u, 20u, CFLOW_STATECHART_ATOMIC, 70u};
    fixture->transitions[0] = (cflow_statechart_transition){
        10u, 10u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 90u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        11u, 80u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 40u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 7u, NULL, 0u, NULL, 0u, NULL, 0u,
        fixture->transitions, 2u, NULL, 0u, NULL, 0u};
    fixture->initial_state = 73;
}

static cflow_statechart_runtime_status runtime_fixture_init(
    runtime_fixture *fixture) {
    cflow_statechart_instance_config config;
    cflow_statechart_status build_status = cflow_statechart_build(
        &fixture->statechart, &fixture->definition);
    check_equal(build_status, CFLOW_STATECHART_OK);
    check_true(cflow_executor_serial_init(&fixture->executor));
    config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .executor = &fixture->executor};
    return cflow_statechart_instance_init(&fixture->instance, &config);
}

static void runtime_fixture_destroy(runtime_fixture *fixture) {
    check_equal(cflow_statechart_instance_destroy(&fixture->instance),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&fixture->executor);
    cflow_statechart_destroy(&fixture->statechart);
}

static bool guard_binding(void *user, const void *state,
                          const cflow_event_view *event,
                          bool *out_enabled, const char **out_error) {
    (void)user;
    (void)state;
    (void)event;
    if (out_enabled == NULL || out_error == NULL) return false;
    *out_enabled = true;
    *out_error = NULL;
    if (user != NULL) atomic_fetch_add((atomic_int *)user, 1);
    return true;
}

static bool executable_binding(void *user,
                               cflow_statechart_action_phase phase,
                               cflow_machine_state_id owner,
                               const void *state,
                               const cflow_event_view *event,
                               void *out_state,
                               cflow_statechart_raise_fn raise_internal,
                               void *raise_user,
                               const char **out_error) {
    (void)user;
    (void)phase;
    (void)owner;
    (void)event;
    (void)raise_internal;
    (void)raise_user;
    if (state == NULL || out_state == NULL || out_error == NULL) return false;
    *(int *)out_state = *(const int *)state;
    *out_error = NULL;
    if (user != NULL) atomic_fetch_add((atomic_int *)user, 1);
    return true;
}

suite("CFlow Statechart runtime initial configuration") {
    it("enters nested compound defaults and projects its sole leaf") {
        runtime_fixture fixture;
        cflow_machine_state_id actual[3] = {99u, 99u, 99u};
        const cflow_machine_state_id expected[] = {70u, 20u, 90u};
        size_t count = 0u;
        uint64_t version = 0u;
        nested_compound_fixture(&fixture);

        check_equal(runtime_fixture_init(&fixture),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, actual, 3u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(count, (size_t)3u);
        check_equal(version, UINT64_C(1));
        check_equal(actual, expected, sizeof(expected));
        check_equal(cflow_statechart_instance_current_state(&fixture.instance),
                    (cflow_machine_state_id)90u);
        runtime_fixture_destroy(&fixture);
    }

    it("enters all parallel regions without activating pseudo states") {
        runtime_fixture fixture;
        cflow_machine_state_id actual[5] = {0};
        const cflow_machine_state_id expected[] = {
            70u, 50u, 90u, 20u, 40u};
        size_t count = 0u;
        uint64_t version = 0u;
        parallel_fixture(&fixture);

        check_equal(runtime_fixture_init(&fixture),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, actual, 5u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(count, (size_t)5u);
        check_equal(actual, expected, sizeof(expected));
        check_equal(cflow_statechart_instance_current_state(&fixture.instance),
                    (cflow_machine_state_id)0u);
        runtime_fixture_destroy(&fixture);
    }

    it("reports required snapshot size without a partial list or version") {
        runtime_fixture fixture;
        cflow_machine_state_id actual[2] = {91u, 92u};
        size_t count = 17u;
        uint64_t version = UINT64_C(33);
        nested_compound_fixture(&fixture);

        check_equal(runtime_fixture_init(&fixture),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, actual, 2u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_TOO_SMALL);
        check_equal(count, (size_t)3u);
        check_equal(version, UINT64_C(33));
        check_equal(actual[0], (cflow_machine_state_id)91u);
        check_equal(actual[1], (cflow_machine_state_id)92u);
        runtime_fixture_destroy(&fixture);
    }

    it("copies extended state and exposes the initial publication stats") {
        runtime_fixture fixture;
        const cmeta_type_desc *type = NULL;
        cflow_statechart_instance_stats stats = {0};
        int state = 0;
        nested_compound_fixture(&fixture);

        check_equal(runtime_fixture_init(&fixture),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_true(cflow_statechart_instance_copy_state(
            &fixture.instance, &type, &state, sizeof(state)));
        check_true(cmeta_type_equal(type, &cmeta_type_int));
        check_equal(state, 41);
        check_true(cflow_statechart_instance_get_stats(
            &fixture.instance, &stats));
        check_equal(stats.configuration_version, UINT64_C(1));
        check_equal(stats.active_state_count, (size_t)3u);
        check_equal(stats.active_leaf_count, (size_t)1u);
        check_null(cflow_statechart_instance_error(&fixture.instance));
        runtime_fixture_destroy(&fixture);
    }

    it("allocates history storage without activating history pseudo states") {
        runtime_fixture fixture;
        cflow_machine_state_id actual[2] = {0};
        const cflow_machine_state_id expected[] = {1u, 3u};
        size_t count = 0u;
        uint64_t version = 0u;
        cflow_statechart_storage_requirements requirements = {0};
        cflow_statechart_instance_config config;
        memset(&fixture, 0, sizeof(fixture));
        fixture.states[0] = (cflow_statechart_state){
            1u, 0u, CFLOW_STATECHART_COMPOUND, 0u};
        fixture.states[1] = (cflow_statechart_state){
            2u, 1u, CFLOW_STATECHART_INITIAL, 1u};
        fixture.states[2] = (cflow_statechart_state){
            3u, 1u, CFLOW_STATECHART_ATOMIC, 2u};
        fixture.states[3] = (cflow_statechart_state){
            4u, 1u, CFLOW_STATECHART_HISTORY_SHALLOW, 3u};
        fixture.states[4] = (cflow_statechart_state){
            5u, 1u, CFLOW_STATECHART_HISTORY_DEEP, 4u};
        fixture.transitions[0] = (cflow_statechart_transition){
            10u, 2u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 3u,
            CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
        fixture.transitions[1] = (cflow_statechart_transition){
            11u, 4u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 3u,
            CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
        fixture.transitions[2] = (cflow_statechart_transition){
            12u, 5u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 3u,
            CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 2u};
        fixture.definition = (cflow_statechart_definition){
            &cmeta_type_int, fixture.states, 5u, NULL, 0u, NULL, 0u,
            NULL, 0u, fixture.transitions, 3u, NULL, 0u, NULL, 0u};
        fixture.initial_state = 19;

        check_equal(cflow_statechart_build(
                        &fixture.statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);
        check_equal(cflow_statechart_instance_storage_requirements_internal(
                        &fixture.statechart, &requirements),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(requirements.history_bitset_bytes, (size_t)4u);
        check_equal(requirements.history_count_bytes,
                    (size_t)(4u * sizeof(size_t)));
        check_equal(requirements.total_bytes,
                    requirements.control_bytes + requirements.binding_bytes +
                    requirements.configuration_bytes +
                    requirements.history_bitset_bytes +
                    requirements.history_count_bytes +
                    requirements.extended_state_bytes +
                    requirements.index_work_bytes);
        check_true(cflow_executor_serial_init(&fixture.executor));
        config = (cflow_statechart_instance_config){
            .statechart = &fixture.statechart,
            .initial_state = &fixture.initial_state,
            .max_storage_bytes = requirements.total_bytes - 1u,
            .executor = &fixture.executor};
        config.max_storage_bytes =
            (size_t)CFLOW_STATECHART_MAX_INSTANCE_BYTES + 1u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED);
        check_null(fixture.instance.impl);
        config.max_storage_bytes = requirements.total_bytes - 1u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED);
        check_null(fixture.instance.impl);
        config.max_storage_bytes = requirements.total_bytes;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, actual, 2u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(actual, expected, sizeof(expected));
        runtime_fixture_destroy(&fixture);
    }

    it("keeps storage live when destroy runs from its executor callback") {
        runtime_fixture fixture;
        runtime_destroy_probe probe;
        const cflow_executor_task task = {
            destroy_from_executor_callback, NULL, NULL, &probe};
        nested_compound_fixture(&fixture);
        check_equal(runtime_fixture_init(&fixture),
                    CFLOW_STATECHART_RUNTIME_OK);
        probe.instance = &fixture.instance;
        atomic_init(&probe.destroy_status, -1);
        atomic_init(&probe.query_succeeded, false);

        check_equal(cflow_executor_try_post_task(&fixture.executor, &task),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(atomic_load(&probe.destroy_status),
                    (int)CFLOW_STATECHART_RUNTIME_WOULD_BLOCK);
        check_true(atomic_load(&probe.query_succeeded));
        check_not_null(fixture.instance.impl);
        check_equal(cflow_statechart_instance_destroy(&fixture.instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_null(fixture.instance.impl);
        check_equal(cflow_statechart_instance_destroy(&fixture.instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        cflow_executor_destroy(&fixture.executor);
        cflow_statechart_destroy(&fixture.statechart);
    }

    it("rejects nontrivial extended state without publishing an instance") {
        static const cmeta_type_desc nontrivial_state_type = {
            "nontrivial_state", sizeof(int), _Alignof(int),
            CMETA_T_OBJECT, NULL, NULL, NULL};
        runtime_fixture fixture;
        cflow_statechart_instance_config config;
        nested_compound_fixture(&fixture);
        fixture.definition.state_type = &nontrivial_state_type;
        check_equal(cflow_statechart_build(
                        &fixture.statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);
        check_true(cflow_executor_serial_init(&fixture.executor));
        config = (cflow_statechart_instance_config){
            .statechart = &fixture.statechart,
            .initial_state = &fixture.initial_state,
            .executor = &fixture.executor};

        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_UNSUPPORTED_TYPE);
        check_null(fixture.instance.impl);
        runtime_fixture_destroy(&fixture);
    }

    it("rejects a manual executor without publishing an instance") {
        runtime_fixture fixture;
        cflow_statechart_instance_config config;
        nested_compound_fixture(&fixture);
        check_equal(cflow_statechart_build(
                        &fixture.statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);
        check_true(cflow_executor_manual_init(&fixture.executor));
        config = (cflow_statechart_instance_config){
            .statechart = &fixture.statechart,
            .initial_state = &fixture.initial_state,
            .executor = &fixture.executor};

        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_INVALID_EXECUTOR);
        check_null(fixture.instance.impl);
        runtime_fixture_destroy(&fixture);
    }

    it("validates wrong-child missing-ancestor and duplicate configurations") {
        runtime_fixture fixture;
        unsigned char scratch[1] = {0};
        const cflow_machine_state_id wrong_child[] = {70u, 50u, 90u};
        const cflow_machine_state_id missing_ancestor[] = {
            70u, 50u, 90u, 40u};
        const cflow_machine_state_id duplicate[] = {70u, 50u, 50u, 90u};
        parallel_fixture(&fixture);
        check_equal(cflow_statechart_build(
                        &fixture.statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);

        check_equal(cflow_statechart_configuration_validate_internal(
                        &fixture.statechart, wrong_child,
                        sizeof(wrong_child) / sizeof(wrong_child[0]),
                        scratch, sizeof(scratch)),
                    CFLOW_STATECHART_CONFIGURATION_WRONG_CHILD);
        check_equal(cflow_statechart_configuration_validate_internal(
                        &fixture.statechart, missing_ancestor,
                        sizeof(missing_ancestor) /
                            sizeof(missing_ancestor[0]),
                        scratch, sizeof(scratch)),
                    CFLOW_STATECHART_CONFIGURATION_MISSING_ANCESTOR);
        check_equal(cflow_statechart_configuration_validate_internal(
                        &fixture.statechart, duplicate, 4u,
                        scratch, sizeof(scratch)),
                    CFLOW_STATECHART_CONFIGURATION_DUPLICATE_STATE);
        cflow_statechart_destroy(&fixture.statechart);
    }

    it("normalizes exact bindings and rejects the binding mismatch matrix") {
        runtime_fixture fixture;
        const cflow_statechart_guard guard_declarations[] = {
            {20u, &cmeta_type_int, CMETA_EFFECT_PURE,
             CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS},
            {21u, &cmeta_type_int, CMETA_EFFECT_PURE,
             CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS}};
        const cflow_statechart_executable executable_declarations[] = {
            {30u, &cmeta_type_int, CMETA_EFFECT_PURE,
             CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS},
            {31u, &cmeta_type_int, CMETA_EFFECT_PURE,
             CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS}};
        const cflow_statechart_state_action state_actions[] = {
            {90u, CFLOW_STATECHART_STATE_ACTION_ENTRY, 30u, 0u},
            {90u, CFLOW_STATECHART_STATE_ACTION_ENTRY, 31u, 1u}};
        atomic_int guard_calls;
        atomic_int executable_calls;
        cflow_statechart_guard_binding guards[] = {
            {21u, guard_binding, &guard_calls},
            {20u, guard_binding, &guard_calls}};
        cflow_statechart_executable_binding executables[] = {
            {31u, executable_binding, &executable_calls},
            {30u, executable_binding, &executable_calls}};
        cflow_statechart_guard_binding invalid_guards[2];
        cflow_statechart_executable_binding invalid_executables[2];
        cflow_statechart_storage_requirements requirements = {0};
        cflow_statechart_instance_config config;
        atomic_init(&guard_calls, 0);
        atomic_init(&executable_calls, 0);
        nested_compound_fixture(&fixture);
        fixture.transitions[2] = (cflow_statechart_transition){
            12u, 90u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 20u, 0u,
            CFLOW_STATECHART_TRANSITION_INTERNAL, 0u, 2u};
        fixture.transitions[3] = (cflow_statechart_transition){
            13u, 90u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 21u, 0u,
            CFLOW_STATECHART_TRANSITION_INTERNAL, 1u, 3u};
        fixture.definition.guards = guard_declarations;
        fixture.definition.guard_count = 2u;
        fixture.definition.executables = executable_declarations;
        fixture.definition.executable_count = 2u;
        fixture.definition.transitions = fixture.transitions;
        fixture.definition.transition_count = 4u;
        fixture.definition.state_actions = state_actions;
        fixture.definition.state_action_count = 2u;
        check_equal(cflow_statechart_build(
                        &fixture.statechart, &fixture.definition),
                    CFLOW_STATECHART_OK);
        check_true(cflow_executor_serial_init(&fixture.executor));
        config = (cflow_statechart_instance_config){
            .statechart = &fixture.statechart,
            .initial_state = &fixture.initial_state,
            .guards = guards,
            .guard_count = 2u,
            .executables = executables,
            .executable_count = 2u,
            .executor = &fixture.executor};

        check_equal(cflow_statechart_instance_storage_requirements_internal(
                        &fixture.statechart, &requirements),
                    CFLOW_STATECHART_RUNTIME_OK);
        config.max_storage_bytes = 1u;
        config.guards =
            (const cflow_statechart_guard_binding *)(uintptr_t)1u;
        config.executables =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED);
        check_null(fixture.instance.impl);
        config.max_storage_bytes = requirements.total_bytes;
        config.guards = guards;
        config.executables = executables;

        config.guard_count = 1u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        check_null(fixture.instance.impl);
        config.guard_count = 2u;
        config.guards = NULL;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        config.guards = invalid_guards;
        invalid_guards[0] = guards[0];
        invalid_guards[1] = guards[0];
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        invalid_guards[0] = guards[0];
        invalid_guards[1] = guards[1];
        invalid_guards[0].id = 99u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        invalid_guards[0] = guards[0];
        invalid_guards[1] = guards[1];
        invalid_guards[1].fn = NULL;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);

        config.guards = guards;
        config.executable_count = 1u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        config.executable_count = 2u;
        config.executables = NULL;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        config.executables = invalid_executables;
        invalid_executables[0] = executables[0];
        invalid_executables[1] = executables[0];
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        invalid_executables[0] = executables[0];
        invalid_executables[1] = executables[1];
        invalid_executables[0].id = 99u;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        invalid_executables[0] = executables[0];
        invalid_executables[1] = executables[1];
        invalid_executables[1].fn = NULL;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH);
        check_equal(atomic_load(&guard_calls), 0);
        check_equal(atomic_load(&executable_calls), 0);

        config.executables = executables;
        check_equal(cflow_statechart_instance_init(&fixture.instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(atomic_load(&guard_calls), 0);
        check_equal(atomic_load(&executable_calls), 0);
        runtime_fixture_destroy(&fixture);
    }
}
