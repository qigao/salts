#include <cflow/executor.h>
#include <cflow/statechart.h>

#include "statechart_runtime_internal.h"
#include "tinytest.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

typedef struct runtime_fixture {
    cflow_statechart_state states[12];
    cflow_event_type events[2];
    cflow_statechart_guard guards[4];
    cflow_statechart_transition transitions[12];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_statechart_instance instance;
    int initial_state;
} runtime_fixture;

typedef struct selection_guard_probe {
    const void *first_state;
    int expected_state;
    size_t calls;
    bool enabled;
    bool fail;
    const char *error;
} selection_guard_probe;

typedef struct selection_error_reader_probe {
    const cflow_statechart_instance *instance;
    atomic_bool started;
    atomic_bool stop;
    atomic_bool observed;
} selection_error_reader_probe;

enum { SELECTION_ERROR_OBSERVE_ATTEMPTS = 1000 };
static const char selection_guard_contract_error[] =
    "Statechart guard contract violation";

static void selection_error_reader(void *user) {
    selection_error_reader_probe *probe =
        (selection_error_reader_probe *)user;
    atomic_store(&probe->started, true);
    while (!atomic_load(&probe->stop)) {
        const char *error = cflow_statechart_instance_error(probe->instance);
        if (error != NULL &&
            strcmp(error, selection_guard_contract_error) == 0) {
            atomic_store(&probe->observed, true);
            return;
        }
    }
}

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

static bool selection_guard(void *user, const void *state,
                            const cflow_event_view *event,
                            bool *out_enabled, const char **out_error) {
    selection_guard_probe *probe = (selection_guard_probe *)user;
    if (probe == NULL || state == NULL || event == NULL ||
        out_enabled == NULL || out_error == NULL ||
        *(const int *)state != probe->expected_state)
        return false;
    if (probe->first_state == NULL) probe->first_state = state;
    else if (probe->first_state != state) return false;
    ++probe->calls;
    *out_error = probe->error;
    *out_enabled = probe->enabled;
    return !probe->fail;
}

static void selection_fixture(runtime_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        1u, 0u, CFLOW_STATECHART_PARALLEL, 0u};
    fixture->states[1] = (cflow_statechart_state){
        10u, 1u, CFLOW_STATECHART_COMPOUND, 1u};
    fixture->states[2] = (cflow_statechart_state){
        11u, 10u, CFLOW_STATECHART_INITIAL, 2u};
    fixture->states[3] = (cflow_statechart_state){
        12u, 10u, CFLOW_STATECHART_ATOMIC, 3u};
    fixture->states[4] = (cflow_statechart_state){
        20u, 1u, CFLOW_STATECHART_COMPOUND, 4u};
    fixture->states[5] = (cflow_statechart_state){
        21u, 20u, CFLOW_STATECHART_INITIAL, 5u};
    fixture->states[6] = (cflow_statechart_state){
        22u, 20u, CFLOW_STATECHART_ATOMIC, 6u};
    fixture->events[0] = (cflow_event_type){100u, &cmeta_type_int};
    fixture->transitions[0] = (cflow_statechart_transition){
        100u, 11u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 12u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        101u, 21u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 22u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 7u, fixture->events, 1u,
        NULL, 0u, NULL, 0u, fixture->transitions, 2u,
        NULL, 0u, NULL, 0u};
    fixture->initial_state = 41;
}

static void add_event_transition(runtime_fixture *fixture,
                                 cflow_statechart_transition_id id,
                                 cflow_machine_state_id source,
                                 cflow_statechart_guard_id guard,
                                 cflow_machine_state_id target,
                                 uint32_t priority) {
    const size_t index = fixture->definition.transition_count++;
    fixture->transitions[index] = (cflow_statechart_transition){
        id, source, CFLOW_STATECHART_TRIGGER_EVENT, 100u, 0u, guard, target,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, priority, (uint32_t)index};
}

static cflow_statechart_runtime_status selection_fixture_init(
    runtime_fixture *fixture,
    const cflow_statechart_guard_binding *bindings,
    size_t binding_count) {
    cflow_statechart_instance_config config;
    cflow_statechart_status build_status;
    fixture->definition.transitions = fixture->transitions;
    build_status = cflow_statechart_build(
        &fixture->statechart, &fixture->definition);
    check_equal(build_status, CFLOW_STATECHART_OK);
    check_true(cflow_executor_serial_init(&fixture->executor));
    config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .guards = bindings,
        .guard_count = binding_count,
        .executor = &fixture->executor};
    return cflow_statechart_instance_init(&fixture->instance, &config);
}

static cflow_statechart_runtime_status select_event(
    runtime_fixture *fixture, cflow_statechart_selection_snapshot *out) {
    const int payload = 7;
    const cflow_event_view event = {100u, &cmeta_type_int, &payload};
    const cflow_statechart_selection_trigger trigger = {
        CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
    return cflow_statechart_instance_select_internal(
        &fixture->instance, &trigger, out);
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

suite("CFlow Statechart deterministic transition selection") {
    it("selects compatible transitions from both parallel regions") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        const cflow_statechart_transition_id expected[] = {200u, 201u};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 12u, 0u, 12u, 0u);
        add_event_transition(&fixture, 201u, 22u, 0u, 22u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)2u);
        check_equal(selected.transition_ids, expected, sizeof(expected));
        check_true(cflow_statechart_selection_exits_internal(
            &fixture.instance, &selected, 0u, 12u));
        check_false(cflow_statechart_selection_exits_internal(
            &fixture.instance, &selected, 0u, 22u));
        check_true(cflow_statechart_selection_exits_internal(
            &fixture.instance, &selected, 1u, 22u));
        runtime_fixture_destroy(&fixture);
    }

    it("bubbles only after disabled child guards and keeps one state snapshot") {
        runtime_fixture fixture;
        selection_guard_probe child = {NULL, 41, 0u, false, false, NULL};
        selection_guard_probe parent = {NULL, 41, 0u, true, false, NULL};
        cflow_statechart_guard_binding bindings[] = {
            {300u, selection_guard, &child},
            {301u, selection_guard, &parent}};
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.guards[1] = (cflow_statechart_guard){
            301u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 2u;
        add_event_transition(&fixture, 200u, 12u, 300u, 0u, 0u);
        add_event_transition(&fixture, 201u, 10u, 301u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, bindings, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        check_equal(child.calls, (size_t)1u);
        check_equal(parent.calls, (size_t)1u);
        check_equal(child.first_state, parent.first_state);
        runtime_fixture_destroy(&fixture);
    }

    it("lets a child candidate hide its ancestor candidate") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 12u, 0u, 12u, 0u);
        add_event_transition(&fixture, 201u, 10u, 0u, 12u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)200u);
        runtime_fixture_destroy(&fixture);
    }

    it("uses lower numeric priority before declaration order") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 12u, 0u, 0u, 5u);
        add_event_transition(&fixture, 201u, 12u, 0u, 0u, 1u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        runtime_fixture_destroy(&fixture);
    }

    it("deduplicates one ancestor candidate reached from both leaves") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 1u, 0u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)200u);
        runtime_fixture_destroy(&fixture);
    }

    it("keeps the earlier leaf when unrelated sources conflict") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 12u, 0u, 22u, 0u);
        add_event_transition(&fixture, 201u, 22u, 0u, 12u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)200u);
        runtime_fixture_destroy(&fixture);
    }

    it("lets a later descendant source preempt an earlier ancestor source") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 1u, 0u, 12u, 0u);
        add_event_transition(&fixture, 201u, 22u, 0u, 12u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        runtime_fixture_destroy(&fixture);
    }

    it("keeps targetless candidates from both regions") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        const cflow_statechart_transition_id expected[] = {200u, 201u};
        size_t byte;
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 12u, 0u, 0u, 0u);
        add_event_transition(&fixture, 201u, 22u, 0u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)2u);
        check_equal(selected.transition_ids, expected, sizeof(expected));
        for (byte = 0u;
             byte < selected.transition_count * selected.exit_set_stride;
             ++byte)
            check_equal(selected.exit_sets[byte], (unsigned char)0u);
        runtime_fixture_destroy(&fixture);
    }

    it("returns the same literal selection on repeated runs") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot first = {0}, second = {0};
        cflow_statechart_transition_id saved[2] = {0};
        const cflow_statechart_transition_id expected[] = {200u, 201u};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, 12u, 0u, 12u, 0u);
        add_event_transition(&fixture, 201u, 22u, 0u, 22u, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &first),
                    CFLOW_STATECHART_RUNTIME_OK);
        memcpy(saved, first.transition_ids, sizeof(saved));
        check_equal(select_event(&fixture, &second),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(saved, expected, sizeof(expected));
        check_equal(second.transition_ids, expected, sizeof(expected));
        runtime_fixture_destroy(&fixture);
    }

    it("preserves the first guard failure without publishing semantic state") {
        runtime_fixture fixture;
        selection_guard_probe first_guard = {
            NULL, 41, 0u, false, true, "first guard failed"};
        selection_guard_probe second_guard = {
            NULL, 41, 0u, false, true, "second guard failed"};
        cflow_statechart_guard_binding bindings[] = {
            {300u, selection_guard, &first_guard},
            {301u, selection_guard, &second_guard}};
        cflow_statechart_selection_snapshot selected = {0};
        cflow_machine_state_id states[5] = {0};
        const cflow_machine_state_id expected[] = {1u, 10u, 12u, 20u, 22u};
        size_t count = 0u;
        uint64_t version = 0u;
        selection_error_reader_probe reader_probe;
        turbo_thread_t reader = NULL;
        size_t attempt;
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.guards[1] = (cflow_statechart_guard){
            301u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 2u;
        add_event_transition(&fixture, 200u, 12u, 300u, 0u, 0u);
        add_event_transition(&fixture, 201u, 22u, 301u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, bindings, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        reader_probe.instance = &fixture.instance;
        atomic_init(&reader_probe.started, false);
        atomic_init(&reader_probe.stop, false);
        atomic_init(&reader_probe.observed, false);
        check_equal(turbo_thread_create(
                        &reader, selection_error_reader, &reader_probe),
                    TURBO_OK);
        while (!atomic_load(&reader_probe.started)) turbo_thread_yield();
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_GUARD_FAILED);
        for (attempt = 0u;
             attempt < (size_t)SELECTION_ERROR_OBSERVE_ATTEMPTS &&
                 !atomic_load(&reader_probe.observed);
             ++attempt)
            turbo_sleep_ms(1u);
        atomic_store(&reader_probe.stop, true);
        check_equal(turbo_thread_join(&reader), TURBO_OK);
        check_true(atomic_load(&reader_probe.observed));
        check_equal(selected.transition_count, (size_t)0u);
        check_equal(first_guard.calls, (size_t)1u);
        check_equal(second_guard.calls, (size_t)0u);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    selection_guard_contract_error);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, states, 5u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(version, UINT64_C(1));
        check_equal(states, expected, sizeof(expected));
        runtime_fixture_destroy(&fixture);
    }
}
