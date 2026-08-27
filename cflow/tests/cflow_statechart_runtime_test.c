#include <cflow/executor.h>
#include <cflow/statechart.h>

#include "statechart_runtime_internal.h"
#include "tinytest.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

bool cflow_executor_is_current_internal(const cflow_executor *executor);

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
enum {
    SELECTION_ROOT = 700,
    SELECTION_LEFT_REGION = 40,
    SELECTION_LEFT_INITIAL = 900,
    SELECTION_LEFT_LEAF = 800,
    SELECTION_RIGHT_REGION = 300,
    SELECTION_RIGHT_INITIAL = 5,
    SELECTION_RIGHT_LEAF = 10,
    SELECTION_LEFT_FINAL = 600
};
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

static bool selection_nullable_event_guard(
    void *user, const void *state, const cflow_event_view *event,
    bool *out_enabled, const char **out_error) {
    selection_guard_probe *probe = (selection_guard_probe *)user;
    if (probe == NULL || state == NULL || event != NULL ||
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
        SELECTION_RIGHT_LEAF, SELECTION_RIGHT_REGION,
        CFLOW_STATECHART_ATOMIC, 6u};
    fixture->states[1] = (cflow_statechart_state){
        SELECTION_LEFT_INITIAL, SELECTION_LEFT_REGION,
        CFLOW_STATECHART_INITIAL, 2u};
    fixture->states[2] = (cflow_statechart_state){
        SELECTION_ROOT, 0u, CFLOW_STATECHART_PARALLEL, 0u};
    fixture->states[3] = (cflow_statechart_state){
        SELECTION_LEFT_FINAL, SELECTION_LEFT_REGION,
        CFLOW_STATECHART_FINAL, 7u};
    fixture->states[4] = (cflow_statechart_state){
        SELECTION_RIGHT_REGION, SELECTION_ROOT,
        CFLOW_STATECHART_COMPOUND, 4u};
    fixture->states[5] = (cflow_statechart_state){
        SELECTION_LEFT_REGION, SELECTION_ROOT,
        CFLOW_STATECHART_COMPOUND, 1u};
    fixture->states[6] = (cflow_statechart_state){
        SELECTION_RIGHT_INITIAL, SELECTION_RIGHT_REGION,
        CFLOW_STATECHART_INITIAL, 5u};
    fixture->states[7] = (cflow_statechart_state){
        SELECTION_LEFT_LEAF, SELECTION_LEFT_REGION,
        CFLOW_STATECHART_ATOMIC, 3u};
    fixture->events[0] = (cflow_event_type){100u, &cmeta_type_int};
    fixture->transitions[0] = (cflow_statechart_transition){
        100u, SELECTION_LEFT_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, SELECTION_LEFT_LEAF,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        101u, SELECTION_RIGHT_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, SELECTION_RIGHT_LEAF,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 8u, fixture->events, 1u,
        NULL, 0u, NULL, 0u, fixture->transitions, 2u,
        NULL, 0u, NULL, 0u};
    fixture->initial_state = 41;
}

static void mixed_conflict_fixture(runtime_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        500u, 0u, CFLOW_STATECHART_PARALLEL, 0u};
    fixture->states[1] = (cflow_statechart_state){
        400u, 500u, CFLOW_STATECHART_COMPOUND, 1u};
    fixture->states[2] = (cflow_statechart_state){
        401u, 400u, CFLOW_STATECHART_INITIAL, 2u};
    fixture->states[3] = (cflow_statechart_state){
        300u, 400u, CFLOW_STATECHART_PARALLEL, 3u};
    fixture->states[4] = (cflow_statechart_state){
        302u, 300u, CFLOW_STATECHART_ATOMIC, 4u};
    fixture->states[5] = (cflow_statechart_state){
        200u, 500u, CFLOW_STATECHART_COMPOUND, 5u};
    fixture->states[6] = (cflow_statechart_state){
        201u, 200u, CFLOW_STATECHART_INITIAL, 6u};
    fixture->states[7] = (cflow_statechart_state){
        202u, 200u, CFLOW_STATECHART_ATOMIC, 7u};
    fixture->states[8] = (cflow_statechart_state){
        102u, 300u, CFLOW_STATECHART_ATOMIC, 8u};
    fixture->events[0] = (cflow_event_type){100u, &cmeta_type_int};
    fixture->transitions[0] = (cflow_statechart_transition){
        100u, 401u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 300u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        101u, 201u, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u, 202u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->transitions[2] = (cflow_statechart_transition){
        200u, 400u, CFLOW_STATECHART_TRIGGER_EVENT, 100u, 0u, 0u, 302u,
        CFLOW_STATECHART_TRANSITION_INTERNAL, 0u, 2u};
    fixture->transitions[3] = (cflow_statechart_transition){
        201u, 202u, CFLOW_STATECHART_TRIGGER_EVENT, 100u, 0u, 0u, 202u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 3u};
    fixture->transitions[4] = (cflow_statechart_transition){
        202u, 102u, CFLOW_STATECHART_TRIGGER_EVENT, 100u, 0u, 0u, 202u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 4u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 9u, fixture->events, 1u,
        NULL, 0u, NULL, 0u, fixture->transitions, 5u,
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

static void add_eventless_transition(
    runtime_fixture *fixture, cflow_statechart_transition_id id,
    cflow_machine_state_id source, cflow_statechart_guard_id guard,
    cflow_machine_state_id target, uint32_t priority) {
    const size_t index = fixture->definition.transition_count++;
    fixture->transitions[index] = (cflow_statechart_transition){
        id, source, CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, guard, target,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, priority, (uint32_t)index};
}

static void add_completion_transition(
    runtime_fixture *fixture, cflow_statechart_transition_id id,
    cflow_machine_state_id source, cflow_machine_state_id completion,
    cflow_statechart_guard_id guard, cflow_machine_state_id target,
    uint32_t priority) {
    const size_t index = fixture->definition.transition_count++;
    fixture->transitions[index] = (cflow_statechart_transition){
        id, source, CFLOW_STATECHART_TRIGGER_COMPLETION, 0u, completion,
        guard, target, CFLOW_STATECHART_TRANSITION_EXTERNAL, priority,
        (uint32_t)index};
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

static cflow_statechart_runtime_status select_eventless(
    runtime_fixture *fixture, const cflow_event_view *event,
    cflow_statechart_selection_snapshot *out) {
    const cflow_statechart_selection_trigger trigger = {
        CFLOW_STATECHART_TRIGGER_EVENTLESS, event, 0u};
    return cflow_statechart_instance_select_internal(
        &fixture->instance, &trigger, out);
}

static cflow_statechart_runtime_status select_completion(
    runtime_fixture *fixture, cflow_machine_state_id completion,
    cflow_statechart_selection_snapshot *out) {
    const cflow_statechart_selection_trigger trigger = {
        CFLOW_STATECHART_TRIGGER_COMPLETION, NULL, completion};
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
                    requirements.index_work_bytes +
                    requirements.action_scratch_bytes +
                    requirements.internal_event_bytes);
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

    it("rejects a nontrivial Event schema before publishing an instance") {
        static const cmeta_type_desc nontrivial_event_type = {
            "nontrivial_event", sizeof(int), _Alignof(int),
            CMETA_T_OBJECT, NULL, NULL, NULL};
        runtime_fixture fixture;
        cflow_statechart_instance_config config;
        nested_compound_fixture(&fixture);
        fixture.events[0] = (cflow_event_type){
            100u, &nontrivial_event_type};
        fixture.transitions[2] = (cflow_statechart_transition){
            12u, 90u, CFLOW_STATECHART_TRIGGER_EVENT, 100u,
            0u, 0u, 0u, CFLOW_STATECHART_TRANSITION_INTERNAL, 0u, 2u};
        fixture.definition.events = fixture.events;
        fixture.definition.event_count = 1u;
        fixture.definition.transition_count = 3u;
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
        cflow_executor_destroy(&fixture.executor);
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
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF, 0u,
                             SELECTION_LEFT_LEAF, 0u);
        add_event_transition(&fixture, 201u, SELECTION_RIGHT_LEAF, 0u,
                             SELECTION_RIGHT_LEAF, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)2u);
        check_equal(selected.transition_ids, expected, sizeof(expected));
        check_true(cflow_statechart_selection_exits_internal(
            &fixture.instance, &selected, 0u, SELECTION_LEFT_LEAF));
        check_false(cflow_statechart_selection_exits_internal(
            &fixture.instance, &selected, 0u, SELECTION_RIGHT_LEAF));
        check_true(cflow_statechart_selection_exits_internal(
            &fixture.instance, &selected, 1u, SELECTION_RIGHT_LEAF));
        runtime_fixture_destroy(&fixture);
    }

    it("bubbles only after every same-source guard is disabled") {
        runtime_fixture fixture;
        selection_guard_probe first_child = {
            NULL, 41, 0u, false, false, NULL};
        selection_guard_probe second_child = {
            NULL, 41, 0u, false, false, NULL};
        selection_guard_probe parent = {NULL, 41, 0u, true, false, NULL};
        cflow_statechart_guard_binding bindings[] = {
            {300u, selection_guard, &first_child},
            {301u, selection_guard, &second_child},
            {302u, selection_guard, &parent}};
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.guards[1] = (cflow_statechart_guard){
            301u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.guards[2] = (cflow_statechart_guard){
            302u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 3u;
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             300u, 0u, 0u);
        add_event_transition(&fixture, 201u, SELECTION_LEFT_LEAF,
                             301u, 0u, 1u);
        add_event_transition(&fixture, 202u, SELECTION_LEFT_REGION,
                             302u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, bindings, 3u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)202u);
        check_equal(first_child.calls, (size_t)1u);
        check_equal(second_child.calls, (size_t)1u);
        check_equal(parent.calls, (size_t)1u);
        check_equal(first_child.first_state, second_child.first_state);
        check_equal(first_child.first_state, parent.first_state);
        runtime_fixture_destroy(&fixture);
    }

    it("lets a child candidate hide its ancestor candidate") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF, 0u,
                             SELECTION_LEFT_LEAF, 0u);
        add_event_transition(&fixture, 201u, SELECTION_LEFT_REGION, 0u,
                             SELECTION_LEFT_LEAF, 0u);
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
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             0u, 0u, 5u);
        add_event_transition(&fixture, 201u, SELECTION_LEFT_LEAF,
                             0u, 0u, 1u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        runtime_fixture_destroy(&fixture);
    }

    it("continues within one source after a higher-priority guard disables") {
        runtime_fixture fixture;
        selection_guard_probe higher = {
            NULL, 41, 0u, false, false, NULL};
        selection_guard_probe lower = {
            NULL, 41, 0u, true, false, NULL};
        selection_guard_probe ancestor = {
            NULL, 41, 0u, true, false, NULL};
        cflow_statechart_guard_binding bindings[] = {
            {300u, selection_guard, &higher},
            {301u, selection_guard, &lower},
            {302u, selection_guard, &ancestor}};
        cflow_statechart_selection_snapshot selected = {0};
        size_t index;
        selection_fixture(&fixture);
        for (index = 0u; index < 3u; ++index) {
            fixture.guards[index] = (cflow_statechart_guard){
                (cflow_statechart_guard_id)(300u + index), &cmeta_type_int,
                CMETA_EFFECT_PURE,
                CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        }
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 3u;
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             300u, 0u, 0u);
        add_event_transition(&fixture, 201u, SELECTION_LEFT_LEAF,
                             301u, 0u, 1u);
        add_event_transition(&fixture, 202u, SELECTION_LEFT_REGION,
                             302u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, bindings, 3u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        check_equal(higher.calls, (size_t)1u);
        check_equal(lower.calls, (size_t)1u);
        check_equal(ancestor.calls, (size_t)0u);
        runtime_fixture_destroy(&fixture);
    }

    it("selects an eventless transition with a null guard event") {
        runtime_fixture fixture;
        selection_guard_probe guard = {
            NULL, 41, 0u, true, false, NULL};
        const cflow_statechart_guard_binding binding = {
            300u, selection_nullable_event_guard, &guard};
        const int payload = 7;
        const cflow_event_view unexpected_event = {
            100u, &cmeta_type_int, &payload};
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 1u;
        add_eventless_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                                 300u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, &binding, 1u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_eventless(&fixture, NULL, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)200u);
        check_equal(guard.calls, (size_t)1u);
        check_equal(select_eventless(
                        &fixture, &unexpected_event, &selected),
                    CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT);
        runtime_fixture_destroy(&fixture);
    }

    it("selects compound and parallel completion with a null guard event") {
        runtime_fixture fixture;
        selection_guard_probe guard = {
            NULL, 41, 0u, true, false, NULL};
        const cflow_statechart_guard_binding binding = {
            300u, selection_nullable_event_guard, &guard};
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 1u;
        add_completion_transition(
            &fixture, 200u, SELECTION_LEFT_LEAF,
            SELECTION_LEFT_REGION, 300u, 0u, 0u);
        add_completion_transition(
            &fixture, 201u, SELECTION_RIGHT_LEAF,
            SELECTION_ROOT, 300u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, &binding, 1u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_completion(
                        &fixture, SELECTION_LEFT_REGION, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)200u);
        check_equal(select_completion(
                        &fixture, SELECTION_ROOT, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        check_equal(guard.calls, (size_t)2u);
        runtime_fixture_destroy(&fixture);
    }

    it("rejects completion for atomic final pseudo and unknown states") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        const cflow_machine_state_id invalid[] = {
            SELECTION_LEFT_LEAF, SELECTION_LEFT_FINAL,
            SELECTION_LEFT_INITIAL, 999999u};
        size_t index;
        selection_fixture(&fixture);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
            check_equal(select_completion(
                            &fixture, invalid[index], &selected),
                        CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT);
        runtime_fixture_destroy(&fixture);
    }

    it("deduplicates one ancestor candidate reached from both leaves") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, SELECTION_ROOT, 0u, 0u, 0u);
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
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF, 0u,
                             SELECTION_RIGHT_LEAF, 0u);
        add_event_transition(&fixture, 201u, SELECTION_RIGHT_LEAF, 0u,
                             SELECTION_LEFT_LEAF, 0u);
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
        add_event_transition(&fixture, 200u, SELECTION_ROOT, 0u,
                             SELECTION_LEFT_LEAF, 0u);
        add_event_transition(&fixture, 201u, SELECTION_RIGHT_LEAF, 0u,
                             SELECTION_LEFT_LEAF, 0u);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)1u);
        check_equal(selected.transition_ids[0],
                    (cflow_statechart_transition_id)201u);
        runtime_fixture_destroy(&fixture);
    }

    it("keeps the full prefix when a descendant also conflicts unrelated") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        const cflow_statechart_transition_id expected[] = {200u, 201u};
        mixed_conflict_fixture(&fixture);
        check_equal(selection_fixture_init(&fixture, NULL, 0u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(selected.transition_count, (size_t)2u);
        check_equal(selected.transition_ids, expected, sizeof(expected));
        runtime_fixture_destroy(&fixture);
    }

    it("keeps targetless candidates from both regions") {
        runtime_fixture fixture;
        cflow_statechart_selection_snapshot selected = {0};
        const cflow_statechart_transition_id expected[] = {200u, 201u};
        size_t byte;
        selection_fixture(&fixture);
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             0u, 0u, 0u);
        add_event_transition(&fixture, 201u, SELECTION_RIGHT_LEAF,
                             0u, 0u, 0u);
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
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF, 0u,
                             SELECTION_LEFT_LEAF, 0u);
        add_event_transition(&fixture, 201u, SELECTION_RIGHT_LEAF, 0u,
                             SELECTION_RIGHT_LEAF, 0u);
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

    it("owns the first user error from an independently fallible guard") {
        runtime_fixture fixture;
        char callback_error[64] = "fallible guard failed";
        selection_guard_probe guard = {
            NULL, 41, 0u, false, true, callback_error};
        const cflow_statechart_guard_binding binding = {
            300u, selection_guard, &guard};
        cflow_statechart_selection_snapshot selected = {0};
        cflow_machine_state_id states[5] = {0};
        const cflow_machine_state_id expected[] = {
            SELECTION_ROOT, SELECTION_LEFT_REGION, SELECTION_LEFT_LEAF,
            SELECTION_RIGHT_REGION, SELECTION_RIGHT_LEAF};
        size_t state_count = 0u;
        uint64_t version = 0u;
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 1u;
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             300u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, &binding, 1u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_GUARD_FAILED);
        memcpy(callback_error, "mutated callback bytes", 23u);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "fallible guard failed");
        check_equal(selected.transition_count, (size_t)0u);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_GUARD_FAILED);
        check_equal(guard.calls, (size_t)1u);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, states, 5u,
                        &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(version, UINT64_C(1));
        check_equal(states, expected, sizeof(expected));
        runtime_fixture_destroy(&fixture);
    }

    it("uses a stable default when a fallible guard returns no error") {
        runtime_fixture fixture;
        selection_guard_probe guard = {
            NULL, 41, 0u, false, true, NULL};
        const cflow_statechart_guard_binding binding = {
            300u, selection_guard, &guard};
        cflow_statechart_selection_snapshot selected = {0};
        selection_fixture(&fixture);
        fixture.guards[0] = (cflow_statechart_guard){
            300u, &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
        fixture.definition.guards = fixture.guards;
        fixture.definition.guard_count = 1u;
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             300u, 0u, 0u);
        check_equal(selection_fixture_init(&fixture, &binding, 1u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(select_event(&fixture, &selected),
                    CFLOW_STATECHART_RUNTIME_GUARD_FAILED);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "Statechart guard failed");
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
        const cflow_machine_state_id expected[] = {
            SELECTION_ROOT, SELECTION_LEFT_REGION, SELECTION_LEFT_LEAF,
            SELECTION_RIGHT_REGION, SELECTION_RIGHT_LEAF};
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
        add_event_transition(&fixture, 200u, SELECTION_LEFT_LEAF,
                             300u, 0u, 0u);
        add_event_transition(&fixture, 201u, SELECTION_RIGHT_LEAF,
                             301u, 0u, 0u);
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

typedef struct microstep_fixture {
    cflow_statechart_state states[12];
    cflow_event_type events[2];
    cflow_statechart_executable executables[1];
    cflow_statechart_transition transitions[8];
    cflow_statechart_state_action state_actions[8];
    cflow_statechart_transition_action transition_actions[4];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_statechart_instance instance;
    int initial_state;
} microstep_fixture;

typedef struct microstep_action_probe {
    cflow_executor *executor;
    int trace[24];
    int input_states[24];
    size_t calls;
    size_t fail_call;
    size_t raise_call;
    bool raise_twice;
    bool executor_only;
    bool no_alias;
    bool block_first;
    atomic_bool block_entered;
    atomic_bool block_release;
    int observed_event_payload;
} microstep_action_probe;

enum {
    MICRO_ROOT = 990u,
    MICRO_LEFT = 900u,
    MICRO_LEFT_INITIAL = 890u,
    MICRO_LEFT_NESTED = 800u,
    MICRO_NESTED_INITIAL = 790u,
    MICRO_LEFT_A = 700u,
    MICRO_LEFT_B = 690u,
    MICRO_RIGHT = 400u,
    MICRO_RIGHT_INITIAL = 390u,
    MICRO_RIGHT_C = 300u,
    MICRO_RIGHT_D = 290u,
    MICRO_EVENT = 10u,
    MICRO_RAISED_EVENT = 11u,
    MICRO_LEFT_TRANSITION = 1000u,
    MICRO_RIGHT_TRANSITION = 1001u,
    MICRO_EXECUTABLE = 500u
};

static int microstep_trace_code(cflow_statechart_action_phase phase,
                                cflow_machine_state_id owner) {
    return (int)phase * 10000 + (int)owner;
}

static bool microstep_action(void *user,
                             cflow_statechart_action_phase phase,
                             cflow_machine_state_id owner,
                             const void *state,
                             const cflow_event_view *event,
                             void *out_state,
                             cflow_statechart_raise_fn raise_internal,
                             void *raise_user,
                             const char **out_error) {
    microstep_action_probe *probe = (microstep_action_probe *)user;
    const size_t call = probe != NULL ? probe->calls + 1u : 0u;
    int raised_value;
    cflow_event_view raised;
    const char *raise_error = NULL;
    bool raised_ok = true;
    if (probe == NULL || state == NULL || out_state == NULL ||
        out_error == NULL || event == NULL ||
        event->payload_type == NULL ||
        !cmeta_type_equal(event->payload_type, &cmeta_type_int))
        return false;
    probe->executor_only = probe->executor_only &&
        cflow_executor_is_current_internal(probe->executor);
    probe->no_alias = probe->no_alias && state != out_state;
    probe->trace[probe->calls] = microstep_trace_code(phase, owner);
    probe->input_states[probe->calls] = *(const int *)state;
    probe->observed_event_payload = *(const int *)event->payload;
    ++probe->calls;
    if (probe->block_first && call == 1u) {
        atomic_store(&probe->block_entered, true);
        while (!atomic_load(&probe->block_release)) turbo_thread_yield();
    }
    *(int *)out_state = *(const int *)state + 1;
    *out_error = NULL;
    if (call == probe->raise_call) {
        raised_value = 700 + (int)call;
        raised = (cflow_event_view){
            MICRO_RAISED_EVENT, &cmeta_type_int, &raised_value};
        raised_ok = raise_internal != NULL &&
            raise_internal(raise_user, &raised, &raise_error);
        raised_value = -1;
        if (probe->raise_twice && raised_ok) {
            raised_value = 800 + (int)call;
            raised_ok = raise_internal(
                raise_user,
                &(cflow_event_view){
                    MICRO_RAISED_EVENT, &cmeta_type_int, &raised_value},
                &raise_error);
            raised_value = -2;
        }
        if (!raised_ok) {
            *out_error = raise_error;
            return false;
        }
    }
    if (call == probe->fail_call) {
        *out_error = "first action failure";
        return false;
    }
    return true;
}

static void microstep_fixture_definition(microstep_fixture *fixture,
                                         bool cross_regions,
                                         bool targetless,
                                         cflow_statechart_transition_kind kind) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        MICRO_ROOT, 0u, CFLOW_STATECHART_PARALLEL, 0u};
    fixture->states[1] = (cflow_statechart_state){
        MICRO_LEFT, MICRO_ROOT, CFLOW_STATECHART_COMPOUND, 1u};
    fixture->states[2] = (cflow_statechart_state){
        MICRO_LEFT_INITIAL, MICRO_LEFT, CFLOW_STATECHART_INITIAL, 2u};
    fixture->states[3] = (cflow_statechart_state){
        MICRO_LEFT_NESTED, MICRO_LEFT, CFLOW_STATECHART_COMPOUND, 3u};
    fixture->states[4] = (cflow_statechart_state){
        MICRO_NESTED_INITIAL, MICRO_LEFT_NESTED,
        CFLOW_STATECHART_INITIAL, 4u};
    fixture->states[5] = (cflow_statechart_state){
        MICRO_LEFT_A, MICRO_LEFT_NESTED, CFLOW_STATECHART_ATOMIC, 5u};
    fixture->states[6] = (cflow_statechart_state){
        MICRO_LEFT_B, MICRO_LEFT_NESTED, CFLOW_STATECHART_ATOMIC, 6u};
    fixture->states[7] = (cflow_statechart_state){
        MICRO_RIGHT, MICRO_ROOT, CFLOW_STATECHART_COMPOUND, 7u};
    fixture->states[8] = (cflow_statechart_state){
        MICRO_RIGHT_INITIAL, MICRO_RIGHT, CFLOW_STATECHART_INITIAL, 8u};
    fixture->states[9] = (cflow_statechart_state){
        MICRO_RIGHT_C, MICRO_RIGHT, CFLOW_STATECHART_ATOMIC, 9u};
    fixture->states[10] = (cflow_statechart_state){
        MICRO_RIGHT_D, MICRO_RIGHT, CFLOW_STATECHART_ATOMIC, 10u};
    fixture->events[0] = (cflow_event_type){MICRO_EVENT, &cmeta_type_int};
    fixture->events[1] = (cflow_event_type){
        MICRO_RAISED_EVENT, &cmeta_type_int};
    fixture->transitions[0] = (cflow_statechart_transition){
        1u, MICRO_LEFT_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, MICRO_LEFT_NESTED,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        2u, MICRO_NESTED_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, MICRO_LEFT_A,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->transitions[2] = (cflow_statechart_transition){
        3u, MICRO_RIGHT_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, MICRO_RIGHT_C,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 2u};
    fixture->transitions[3] = (cflow_statechart_transition){
        MICRO_LEFT_TRANSITION, MICRO_LEFT_A,
        CFLOW_STATECHART_TRIGGER_EVENT, MICRO_EVENT, 0u, 0u,
        targetless ? 0u : MICRO_LEFT_B, kind, 0u, 3u};
    fixture->transitions[4] = (cflow_statechart_transition){
        MICRO_RIGHT_TRANSITION, MICRO_RIGHT_C,
        CFLOW_STATECHART_TRIGGER_EVENT, MICRO_EVENT, 0u, 0u,
        MICRO_RIGHT_D, CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 4u};
    fixture->executables[0] = (cflow_statechart_executable){
        MICRO_EXECUTABLE, &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    fixture->state_actions[0] = (cflow_statechart_state_action){
        MICRO_LEFT_A, CFLOW_STATECHART_STATE_ACTION_EXIT,
        MICRO_EXECUTABLE, 0u};
    fixture->state_actions[1] = (cflow_statechart_state_action){
        MICRO_LEFT_B, CFLOW_STATECHART_STATE_ACTION_ENTRY,
        MICRO_EXECUTABLE, 0u};
    fixture->state_actions[2] = (cflow_statechart_state_action){
        MICRO_RIGHT_C, CFLOW_STATECHART_STATE_ACTION_EXIT,
        MICRO_EXECUTABLE, 0u};
    fixture->state_actions[3] = (cflow_statechart_state_action){
        MICRO_RIGHT_C, CFLOW_STATECHART_STATE_ACTION_ENTRY,
        MICRO_EXECUTABLE, 0u};
    fixture->state_actions[4] = (cflow_statechart_state_action){
        MICRO_RIGHT_D, CFLOW_STATECHART_STATE_ACTION_ENTRY,
        MICRO_EXECUTABLE, 0u};
    fixture->transition_actions[0] = (cflow_statechart_transition_action){
        MICRO_LEFT_TRANSITION, MICRO_EXECUTABLE, 0u};
    fixture->transition_actions[1] = (cflow_statechart_transition_action){
        MICRO_RIGHT_TRANSITION, MICRO_EXECUTABLE, 0u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 11u, fixture->events, 2u,
        NULL, 0u, fixture->executables, 1u, fixture->transitions,
        cross_regions ? 5u : 4u, fixture->state_actions, 5u,
        fixture->transition_actions, cross_regions ? 2u : 1u};
    fixture->initial_state = 10;
}

static cflow_statechart_runtime_status microstep_fixture_init(
    microstep_fixture *fixture, microstep_action_probe *probe,
    size_t internal_event_capacity) {
    cflow_statechart_instance_config config;
    const cflow_statechart_executable_binding binding = {
        MICRO_EXECUTABLE, microstep_action, probe};
    check_equal(cflow_statechart_build(
                    &fixture->statechart, &fixture->definition),
                CFLOW_STATECHART_OK);
    check_true(cflow_executor_serial_init(&fixture->executor));
    probe->executor = &fixture->executor;
    probe->executor_only = true;
    probe->no_alias = true;
    config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .executables = &binding,
        .executable_count = 1u,
        .internal_event_capacity = internal_event_capacity,
        .executor = &fixture->executor};
    return cflow_statechart_instance_init(&fixture->instance, &config);
}

static cflow_statechart_runtime_status microstep_fixture_init_with_binding(
    microstep_fixture *fixture,
    const cflow_statechart_executable_binding *binding,
    size_t internal_event_capacity,
    size_t executor_capacity) {
    cflow_statechart_instance_config config;
    check_equal(cflow_statechart_build(
                    &fixture->statechart, &fixture->definition),
                CFLOW_STATECHART_OK);
    check_true(executor_capacity == 0u
        ? cflow_executor_serial_init(&fixture->executor)
        : cflow_executor_serial_init_with_capacity(
              &fixture->executor, executor_capacity));
    config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .executables = binding,
        .executable_count = 1u,
        .internal_event_capacity = internal_event_capacity,
        .executor = &fixture->executor};
    return cflow_statechart_instance_init(&fixture->instance, &config);
}

static void microstep_fixture_destroy(microstep_fixture *fixture) {
    check_equal(cflow_statechart_instance_destroy(&fixture->instance),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&fixture->executor);
    cflow_statechart_destroy(&fixture->statechart);
}

static cflow_admission_status microstep_submit_event(
    microstep_fixture *fixture,
    cflow_statechart_selection_snapshot *selection) {
    static const int payload = 77;
    const cflow_event_view event = {
        MICRO_EVENT, &cmeta_type_int, &payload};
    const cflow_statechart_selection_trigger trigger = {
        CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
    check_equal(cflow_statechart_instance_select_internal(
                    &fixture->instance, &trigger, selection),
                CFLOW_STATECHART_RUNTIME_OK);
    return cflow_statechart_instance_try_microstep_internal(
        &fixture->instance, &trigger, selection);
}

static void check_microstep_unchanged(
    microstep_fixture *fixture,
    const cflow_machine_state_id *expected,
    size_t expected_count) {
    cflow_machine_state_id actual[8] = {0};
    const cmeta_type_desc *state_type = NULL;
    size_t count = 0u;
    uint64_t version = 0u;
    int state = 0;
    check_equal(cflow_statechart_instance_copy_configuration(
                    &fixture->instance, actual, 8u, &count, &version),
                CFLOW_STATECHART_SNAPSHOT_OK);
    check_equal(count, expected_count);
    check_equal(actual, expected,
                expected_count * sizeof(*expected));
    check_equal(version, UINT64_C(1));
    check_true(cflow_statechart_instance_copy_state(
        &fixture->instance, &state_type, &state, sizeof(state)));
    check_equal(state, 10);
}

typedef struct microstep_executor_blocker {
    atomic_bool entered;
    atomic_bool release;
} microstep_executor_blocker;

typedef enum microstep_raise_case {
    MICROSTEP_RAISE_VALID = 0,
    MICROSTEP_RAISE_UNKNOWN,
    MICROSTEP_RAISE_TYPE_MISMATCH
} microstep_raise_case;

typedef struct microstep_raise_probe {
    microstep_raise_case sequence[4];
    bool results[4];
    const char *errors[4];
    size_t count;
} microstep_raise_probe;

static bool microstep_raise_sequence_action(
    void *user, cflow_statechart_action_phase phase,
    cflow_machine_state_id owner, const void *state,
    const cflow_event_view *event, void *out_state,
    cflow_statechart_raise_fn raise_internal, void *raise_user,
    const char **out_error) {
    microstep_raise_probe *probe = (microstep_raise_probe *)user;
    size_t index;
    (void)phase;
    (void)owner;
    if (probe == NULL || state == NULL || event == NULL || out_state == NULL ||
        raise_internal == NULL || out_error == NULL)
        return false;
    *(int *)out_state = *(const int *)state;
    for (index = 0u; index < probe->count; ++index) {
        int int_payload = 100 + (int)index;
        long long_payload = 200 + (long)index;
        cflow_event_view raised = {
            MICRO_RAISED_EVENT, &cmeta_type_int, &int_payload};
        const char *raise_error = NULL;
        if (probe->sequence[index] == MICROSTEP_RAISE_UNKNOWN)
            raised.id = 999999u;
        else if (probe->sequence[index] == MICROSTEP_RAISE_TYPE_MISMATCH) {
            raised.payload_type = &cmeta_type_long;
            raised.payload = &long_payload;
        }
        probe->results[index] = raise_internal(
            raise_user, &raised, &raise_error);
        probe->errors[index] = raise_error;
    }
    *out_error = probe->errors[probe->count - 1u];
    return false;
}

static void microstep_block_executor(void *user) {
    microstep_executor_blocker *blocker =
        (microstep_executor_blocker *)user;
    atomic_store(&blocker->entered, true);
    while (!atomic_load(&blocker->release)) turbo_thread_yield();
}

static void microstep_noop(void *user) {
    (void)user;
}

static void check_raise_latch_case(
    const microstep_raise_case *sequence, size_t sequence_count,
    const bool *expected_results,
    cflow_statechart_runtime_status expected_status,
    const char *expected_error) {
    microstep_fixture fixture;
    microstep_raise_probe probe = {0};
    cflow_statechart_selection_snapshot selection = {0};
    cflow_statechart_microstep_stats stats = {0};
    const cflow_statechart_executable_binding binding = {
        MICRO_EXECUTABLE, microstep_raise_sequence_action, &probe};
    size_t index;
    microstep_fixture_definition(
        &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
    for (index = 0u; index < sequence_count; ++index)
        probe.sequence[index] = sequence[index];
    probe.count = sequence_count;
    check_equal(microstep_fixture_init_with_binding(
                    &fixture, &binding, 1u, 0u),
                CFLOW_STATECHART_RUNTIME_OK);
    check_equal(microstep_submit_event(&fixture, &selection),
                CFLOW_ADMISSION_ACCEPTED);
    check_true(cflow_executor_wait_idle(&fixture.executor));
    for (index = 0u; index < sequence_count; ++index) {
        check_equal(probe.results[index], expected_results[index]);
        if (expected_results[index])
            check_null(probe.errors[index]);
        else
            check_equal(probe.errors[index], expected_error);
    }
    check_true(cflow_statechart_instance_get_microstep_stats_internal(
        &fixture.instance, &stats));
    check_equal(stats.last_status, expected_status);
    check_equal(stats.internal_pending, (size_t)0u);
    check_equal(cflow_statechart_instance_error(&fixture.instance),
                expected_error);
    microstep_fixture_destroy(&fixture);
}

typedef struct parallel_entry_fixture {
    cflow_statechart_state states[10];
    cflow_event_type events[1];
    cflow_statechart_executable executables[1];
    cflow_statechart_transition transitions[4];
    cflow_statechart_transition_action transition_actions[2];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_statechart_instance instance;
    int initial_state;
} parallel_entry_fixture;

enum {
    PARALLEL_ENTRY_ROOT = 900u,
    PARALLEL_ENTRY_ROOT_INITIAL = 800u,
    PARALLEL_ENTRY_OUTSIDE = 700u,
    PARALLEL_ENTRY_PARALLEL = 600u,
    PARALLEL_ENTRY_FIRST_REGION = 500u,
    PARALLEL_ENTRY_FIRST_INITIAL = 950u,
    PARALLEL_ENTRY_FIRST_LEAF = 400u,
    PARALLEL_ENTRY_SECOND_REGION = 300u,
    PARALLEL_ENTRY_SECOND_INITIAL = 50u,
    PARALLEL_ENTRY_SECOND_LEAF = 200u
};

static void parallel_entry_fixture_init(
    parallel_entry_fixture *fixture, microstep_action_probe *probe) {
    cflow_statechart_instance_config config;
    const cflow_statechart_executable_binding binding = {
        MICRO_EXECUTABLE, microstep_action, probe};
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        PARALLEL_ENTRY_ROOT, 0u, CFLOW_STATECHART_COMPOUND, 0u};
    fixture->states[1] = (cflow_statechart_state){
        PARALLEL_ENTRY_ROOT_INITIAL, PARALLEL_ENTRY_ROOT,
        CFLOW_STATECHART_INITIAL, 1u};
    fixture->states[2] = (cflow_statechart_state){
        PARALLEL_ENTRY_OUTSIDE, PARALLEL_ENTRY_ROOT,
        CFLOW_STATECHART_ATOMIC, 2u};
    fixture->states[3] = (cflow_statechart_state){
        PARALLEL_ENTRY_PARALLEL, PARALLEL_ENTRY_ROOT,
        CFLOW_STATECHART_PARALLEL, 3u};
    fixture->states[4] = (cflow_statechart_state){
        PARALLEL_ENTRY_FIRST_REGION, PARALLEL_ENTRY_PARALLEL,
        CFLOW_STATECHART_COMPOUND, 4u};
    fixture->states[5] = (cflow_statechart_state){
        PARALLEL_ENTRY_FIRST_INITIAL, PARALLEL_ENTRY_FIRST_REGION,
        CFLOW_STATECHART_INITIAL, 5u};
    fixture->states[6] = (cflow_statechart_state){
        PARALLEL_ENTRY_FIRST_LEAF, PARALLEL_ENTRY_FIRST_REGION,
        CFLOW_STATECHART_ATOMIC, 6u};
    fixture->states[7] = (cflow_statechart_state){
        PARALLEL_ENTRY_SECOND_REGION, PARALLEL_ENTRY_PARALLEL,
        CFLOW_STATECHART_COMPOUND, 7u};
    fixture->states[8] = (cflow_statechart_state){
        PARALLEL_ENTRY_SECOND_INITIAL, PARALLEL_ENTRY_SECOND_REGION,
        CFLOW_STATECHART_INITIAL, 8u};
    fixture->states[9] = (cflow_statechart_state){
        PARALLEL_ENTRY_SECOND_LEAF, PARALLEL_ENTRY_SECOND_REGION,
        CFLOW_STATECHART_ATOMIC, 9u};
    fixture->events[0] = (cflow_event_type){
        MICRO_EVENT, &cmeta_type_int};
    fixture->executables[0] = (cflow_statechart_executable){
        MICRO_EXECUTABLE, &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    fixture->transitions[0] = (cflow_statechart_transition){
        1u, PARALLEL_ENTRY_ROOT_INITIAL,
        CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u,
        PARALLEL_ENTRY_OUTSIDE, CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        2u, PARALLEL_ENTRY_OUTSIDE, CFLOW_STATECHART_TRIGGER_EVENT,
        MICRO_EVENT, 0u, 0u, PARALLEL_ENTRY_PARALLEL,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->transitions[2] = (cflow_statechart_transition){
        3u, PARALLEL_ENTRY_FIRST_INITIAL,
        CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u,
        PARALLEL_ENTRY_FIRST_LEAF,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 2u};
    fixture->transitions[3] = (cflow_statechart_transition){
        4u, PARALLEL_ENTRY_SECOND_INITIAL,
        CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u,
        PARALLEL_ENTRY_SECOND_LEAF,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 3u};
    fixture->transition_actions[0] =
        (cflow_statechart_transition_action){3u, MICRO_EXECUTABLE, 0u};
    fixture->transition_actions[1] =
        (cflow_statechart_transition_action){4u, MICRO_EXECUTABLE, 0u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 10u, fixture->events, 1u,
        NULL, 0u, fixture->executables, 1u, fixture->transitions, 4u,
        NULL, 0u, fixture->transition_actions, 2u};
    fixture->initial_state = 10;
    check_equal(cflow_statechart_build(
                    &fixture->statechart, &fixture->definition),
                CFLOW_STATECHART_OK);
    check_true(cflow_executor_serial_init(&fixture->executor));
    probe->executor = &fixture->executor;
    probe->executor_only = true;
    probe->no_alias = true;
    config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .executables = &binding,
        .executable_count = 1u,
        .internal_event_capacity = 2u,
        .executor = &fixture->executor};
    check_equal(cflow_statechart_instance_init(&fixture->instance, &config),
                CFLOW_STATECHART_RUNTIME_OK);
}

static void parallel_entry_fixture_destroy(parallel_entry_fixture *fixture) {
    check_equal(cflow_statechart_instance_destroy(&fixture->instance),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&fixture->executor);
    cflow_statechart_destroy(&fixture->statechart);
}

suite("CFlow Statechart ordered atomic microsteps") {
    it("runs cross-region exits transitions and entries in exact global order") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int expected_trace[] = {
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_RIGHT_C),
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_TRANSITION,
                                 MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_TRANSITION,
                                 MICRO_RIGHT_C),
            microstep_trace_code(CFLOW_STATECHART_ACTION_ENTRY, MICRO_LEFT_B),
            microstep_trace_code(CFLOW_STATECHART_ACTION_ENTRY, MICRO_RIGHT_D)};
        const int expected_states[] = {10, 11, 12, 13, 14, 15};
        const cflow_machine_state_id expected_configuration[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_B,
            MICRO_RIGHT, MICRO_RIGHT_D};
        cflow_machine_state_id actual[6] = {0};
        const cmeta_type_desc *state_type = NULL;
        cflow_statechart_microstep_stats stats = {0};
        size_t count = 0u;
        uint64_t version = 0u;
        int state = 0;
        microstep_fixture_definition(
            &fixture, true, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 4u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.calls, (size_t)6u);
        check_equal(probe.trace, expected_trace, sizeof(expected_trace));
        check_equal(probe.input_states, expected_states,
                    sizeof(expected_states));
        check_true(probe.executor_only);
        check_true(probe.no_alias);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, actual, 6u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(actual, expected_configuration,
                    sizeof(expected_configuration));
        check_equal(version, UINT64_C(2));
        check_true(cflow_statechart_instance_copy_state(
            &fixture.instance, &state_type, &state, sizeof(state)));
        check_equal(state, 16);
        check_true(cflow_statechart_instance_get_microstep_stats_internal(
            &fixture.instance, &stats));
        check_equal(stats.accepted, UINT64_C(1));
        check_equal(stats.completed, UINT64_C(1));
        check_equal(stats.failed, UINT64_C(0));
        check_equal(stats.cancelled, UINT64_C(0));
        check_equal(stats.finalized, UINT64_C(1));
        microstep_fixture_destroy(&fixture);
    }

    it("runs a targetless transition action without exit or entry") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int expected_trace[] = {
            microstep_trace_code(CFLOW_STATECHART_ACTION_TRANSITION,
                                 MICRO_LEFT_A)};
        const cflow_machine_state_id expected_configuration[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_A,
            MICRO_RIGHT, MICRO_RIGHT_C};
        cflow_machine_state_id actual[6] = {0};
        size_t count = 0u;
        uint64_t version = 0u;
        microstep_fixture_definition(
            &fixture, false, true, CFLOW_STATECHART_TRANSITION_INTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.calls, (size_t)1u);
        check_equal(probe.trace, expected_trace, sizeof(expected_trace));
        check_equal(cflow_statechart_instance_copy_configuration(
                        &fixture.instance, actual, 6u, &count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(actual, expected_configuration,
                    sizeof(expected_configuration));
        check_equal(version, UINT64_C(2));
        microstep_fixture_destroy(&fixture);
    }

    it("keeps compound source on internal descendant transition but reenters external siblings") {
        microstep_fixture internal_fixture, external_fixture;
        microstep_action_probe internal_probe = {0}, external_probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int internal_trace[] = {
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_TRANSITION,
                                 MICRO_LEFT),
            microstep_trace_code(CFLOW_STATECHART_ACTION_ENTRY, MICRO_LEFT_B)};
        const int external_trace[] = {
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_RIGHT_C),
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_TRANSITION,
                                 MICRO_LEFT),
            microstep_trace_code(CFLOW_STATECHART_ACTION_ENTRY, MICRO_LEFT_B),
            microstep_trace_code(CFLOW_STATECHART_ACTION_ENTRY, MICRO_RIGHT_C)};
        const cflow_machine_state_id expected[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_B,
            MICRO_RIGHT, MICRO_RIGHT_C};
        microstep_fixture_definition(
            &internal_fixture, false, false,
            CFLOW_STATECHART_TRANSITION_INTERNAL);
        internal_fixture.transitions[3].source = MICRO_LEFT;
        check_equal(microstep_fixture_init(
                        &internal_fixture, &internal_probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&internal_fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&internal_fixture.executor));
        check_equal(internal_probe.trace, internal_trace,
                    sizeof(internal_trace));
        microstep_fixture_destroy(&internal_fixture);

        microstep_fixture_definition(
            &external_fixture, false, false,
            CFLOW_STATECHART_TRANSITION_EXTERNAL);
        external_fixture.transitions[3].source = MICRO_LEFT;
        check_equal(microstep_fixture_init(
                        &external_fixture, &external_probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&external_fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&external_fixture.executor));
        check_equal(external_probe.trace, external_trace,
                    sizeof(external_trace));
        {
            cflow_machine_state_id actual[6] = {0};
            size_t count = 0u;
            uint64_t version = 0u;
            check_equal(cflow_statechart_instance_copy_configuration(
                            &external_fixture.instance, actual, 6u,
                            &count, &version),
                        CFLOW_STATECHART_SNAPSHOT_OK);
            check_equal(actual, expected, sizeof(expected));
            check_equal(version, UINT64_C(2));
        }
        microstep_fixture_destroy(&external_fixture);
    }

    it("copies raised event payload before callback return and commits once") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        cflow_event_id id = 0u;
        const cmeta_type_desc *type = NULL;
        cflow_statechart_microstep_stats stats = {0};
        int payload = 0;
        probe.raise_call = 1u;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        {
            const int trigger_payload = 77;
            const cflow_event_view event = {
                MICRO_EVENT, &cmeta_type_int, &trigger_payload};
            const cflow_statechart_selection_trigger trigger = {
                CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
            check_equal(cflow_statechart_instance_select_internal(
                            &fixture.instance, &trigger, &selection),
                        CFLOW_STATECHART_RUNTIME_OK);
            check_equal(cflow_statechart_instance_try_microstep_internal(
                            &fixture.instance, &trigger, &selection),
                        CFLOW_ADMISSION_ACCEPTED);
        }
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(cflow_statechart_instance_copy_internal_event_internal(
                        &fixture.instance, 0u, &id, &type,
                        &payload, sizeof(payload)),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(id, (cflow_event_id)MICRO_RAISED_EVENT);
        check_true(cmeta_type_equal(type, &cmeta_type_int));
        check_equal(payload, 701);
        check_true(cflow_statechart_instance_get_microstep_stats_internal(
            &fixture.instance, &stats));
        check_equal(stats.internal_pending, (size_t)1u);
        microstep_fixture_destroy(&fixture);
    }

    it("owns the trigger and freezes selection while accepted work is pending") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        cflow_statechart_selection_snapshot rejected = {0};
        int payload = 77;
        cflow_event_view event = {MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        probe.block_first = true;
        atomic_init(&probe.block_entered, false);
        atomic_init(&probe.block_release, false);
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&probe.block_entered)) turbo_thread_yield();
        payload = 99;
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &rejected),
                    CFLOW_STATECHART_RUNTIME_WOULD_BLOCK);
        check_equal(rejected.transition_count, (size_t)0u);
        atomic_store(&probe.block_release, true);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.observed_event_payload, 77);
        microstep_fixture_destroy(&fixture);
    }

    it("binds the selection-time payload before reservation") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        int payload = 77;
        cflow_event_view event = {MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        payload = 99;
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.observed_event_payload, 77);
        microstep_fixture_destroy(&fixture);
    }

    it("rejects an overwritten selection generation") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot old_selection = {0};
        cflow_statechart_selection_snapshot current_selection = {0};
        const int payload = 77;
        const cflow_event_view event = {
            MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        cflow_admission_status admission;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &old_selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &current_selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        admission = cflow_statechart_instance_try_microstep_internal(
            &fixture.instance, &trigger, &old_selection);
        check_equal(admission, CFLOW_ADMISSION_INVALID_ARGUMENT);
        if (admission == CFLOW_ADMISSION_ACCEPTED)
            check_true(cflow_executor_wait_idle(&fixture.executor));
        else {
            check_equal(cflow_statechart_instance_try_microstep_internal(
                            &fixture.instance, &trigger, &current_selection),
                        CFLOW_ADMISSION_ACCEPTED);
            check_true(cflow_executor_wait_idle(&fixture.executor));
        }
        microstep_fixture_destroy(&fixture);
    }

    it("rejects selection reuse after its commit") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int payload = 77;
        const cflow_event_view event = {
            MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        cflow_admission_status admission;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        admission = cflow_statechart_instance_try_microstep_internal(
            &fixture.instance, &trigger, &selection);
        check_equal(admission, CFLOW_ADMISSION_INVALID_ARGUMENT);
        if (admission == CFLOW_ADMISSION_ACCEPTED)
            check_true(cflow_executor_wait_idle(&fixture.executor));
        microstep_fixture_destroy(&fixture);
    }

    it("rejects a selection submitted with a different event ID") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int payload = 77;
        const cflow_event_view selected_event = {
            MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_event_view different_event = {
            MICRO_RAISED_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger selected_trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &selected_event, 0u};
        const cflow_statechart_selection_trigger different_trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &different_event, 0u};
        cflow_admission_status admission;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &selected_trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        admission = cflow_statechart_instance_try_microstep_internal(
            &fixture.instance, &different_trigger, &selection);
        check_equal(admission, CFLOW_ADMISSION_INVALID_ARGUMENT);
        if (admission == CFLOW_ADMISSION_ACCEPTED)
            check_true(cflow_executor_wait_idle(&fixture.executor));
        microstep_fixture_destroy(&fixture);
    }

    it("rejects a selection token from another instance") {
        microstep_fixture first, second;
        microstep_action_probe first_probe = {0}, second_probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int payload = 77;
        const cflow_event_view event = {
            MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        microstep_fixture_definition(
            &first, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        microstep_fixture_definition(
            &second, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&first, &first_probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_fixture_init(&second, &second_probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_select_internal(
                        &first.instance, &trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &second.instance, &trigger, &selection),
                    CFLOW_ADMISSION_INVALID_ARGUMENT);
        microstep_fixture_destroy(&second);
        microstep_fixture_destroy(&first);
    }

    it("retries the same generation after executor FULL rejection") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        microstep_executor_blocker blocker;
        cflow_statechart_selection_snapshot selection = {0};
        const cflow_statechart_executable_binding binding = {
            MICRO_EXECUTABLE, microstep_action, &probe};
        const cflow_executor_task blocking_task = {
            microstep_block_executor, NULL, NULL, &blocker};
        const cflow_executor_task queued_task = {
            microstep_noop, NULL, NULL, NULL};
        const int payload = 77;
        const cflow_event_view event = {
            MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        atomic_init(&blocker.entered, false);
        atomic_init(&blocker.release, false);
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init_with_binding(
                        &fixture, &binding, 2u, 1u),
                    CFLOW_STATECHART_RUNTIME_OK);
        probe.executor = &fixture.executor;
        probe.executor_only = true;
        probe.no_alias = true;
        check_equal(cflow_executor_try_post_task(
                        &fixture.executor, &blocking_task),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&blocker.entered)) turbo_thread_yield();
        check_equal(cflow_executor_try_post_task(
                        &fixture.executor, &queued_task),
                    CFLOW_ADMISSION_ACCEPTED);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_ADMISSION_FULL);
        atomic_store(&blocker.release, true);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.observed_event_payload, 77);
        microstep_fixture_destroy(&fixture);
    }

    it("runs multiple actions on one owner in row order") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int expected_trace[] = {
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_EXIT, MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_TRANSITION,
                                 MICRO_LEFT_A),
            microstep_trace_code(CFLOW_STATECHART_ACTION_ENTRY, MICRO_LEFT_B)};
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        fixture.state_actions[5] = (cflow_statechart_state_action){
            MICRO_LEFT_A, CFLOW_STATECHART_STATE_ACTION_EXIT,
            MICRO_EXECUTABLE, 1u};
        fixture.definition.state_action_count = 6u;
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.trace, expected_trace, sizeof(expected_trace));
        check_equal(probe.calls, (size_t)4u);
        microstep_fixture_destroy(&fixture);
    }

    it("runs newly entered parallel defaults in document order") {
        parallel_entry_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const int payload = 77;
        const cflow_event_view event = {
            MICRO_EVENT, &cmeta_type_int, &payload};
        const cflow_statechart_selection_trigger trigger = {
            CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
        const int expected_trace[] = {
            microstep_trace_code(
                CFLOW_STATECHART_ACTION_INITIAL,
                PARALLEL_ENTRY_FIRST_INITIAL),
            microstep_trace_code(
                CFLOW_STATECHART_ACTION_INITIAL,
                PARALLEL_ENTRY_SECOND_INITIAL)};
        parallel_entry_fixture_init(&fixture, &probe);
        check_equal(cflow_statechart_instance_select_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_try_microstep_internal(
                        &fixture.instance, &trigger, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.trace, expected_trace, sizeof(expected_trace));
        check_true(probe.executor_only);
        check_true(probe.no_alias);
        parallel_entry_fixture_destroy(&fixture);
    }

    it("keeps the first raise failure across later raise attempts") {
        const microstep_raise_case unknown_first[] = {
            MICROSTEP_RAISE_UNKNOWN, MICROSTEP_RAISE_TYPE_MISMATCH,
            MICROSTEP_RAISE_VALID};
        const microstep_raise_case mismatch_first[] = {
            MICROSTEP_RAISE_TYPE_MISMATCH, MICROSTEP_RAISE_UNKNOWN,
            MICROSTEP_RAISE_VALID};
        const microstep_raise_case full_first[] = {
            MICROSTEP_RAISE_VALID, MICROSTEP_RAISE_VALID,
            MICROSTEP_RAISE_TYPE_MISMATCH, MICROSTEP_RAISE_UNKNOWN};
        const bool all_failed[] = {false, false, false};
        const bool full_results[] = {true, false, false, false};
        check_raise_latch_case(
            unknown_first, 3u, all_failed,
            CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_INVALID,
            "Statechart internal event is unknown");
        check_raise_latch_case(
            mismatch_first, 3u, all_failed,
            CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_TYPE_MISMATCH,
            "Statechart internal event type mismatch");
        check_raise_latch_case(
            full_first, 4u, full_results,
            CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL,
            "Statechart internal event queue is full");
    }

    it("rolls back exit failure and settles accepted work once") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        cflow_statechart_microstep_stats stats = {0};
        const cflow_machine_state_id expected[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_A,
            MICRO_RIGHT, MICRO_RIGHT_C};
        probe.fail_call = 1u;
        probe.raise_call = 1u;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.calls, (size_t)1u);
        check_microstep_unchanged(&fixture, expected, 6u);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "first action failure");
        check_true(cflow_statechart_instance_get_microstep_stats_internal(
            &fixture.instance, &stats));
        check_equal(stats.accepted, UINT64_C(1));
        check_equal(stats.failed, UINT64_C(1));
        check_equal(stats.completed, UINT64_C(0));
        check_equal(stats.finalized, UINT64_C(1));
        check_equal(stats.internal_pending, (size_t)0u);
        microstep_fixture_destroy(&fixture);
    }

    it("rolls back transition failure before later actions") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const cflow_machine_state_id expected[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_A,
            MICRO_RIGHT, MICRO_RIGHT_C};
        probe.fail_call = 2u;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.calls, (size_t)2u);
        check_microstep_unchanged(&fixture, expected, 6u);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "first action failure");
        microstep_fixture_destroy(&fixture);
    }

    it("rolls back entry failure after staging the target configuration") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        const cflow_machine_state_id expected[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_A,
            MICRO_RIGHT, MICRO_RIGHT_C};
        probe.fail_call = 3u;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.calls, (size_t)3u);
        check_microstep_unchanged(&fixture, expected, 6u);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "first action failure");
        microstep_fixture_destroy(&fixture);
    }

    it("reports staged internal queue full and publishes nothing") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        cflow_statechart_microstep_stats stats = {0};
        const cflow_machine_state_id expected[] = {
            MICRO_ROOT, MICRO_LEFT, MICRO_LEFT_NESTED, MICRO_LEFT_A,
            MICRO_RIGHT, MICRO_RIGHT_C};
        probe.raise_call = 1u;
        probe.raise_twice = true;
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 1u),
                    CFLOW_STATECHART_RUNTIME_OK);
        {
            const int trigger_payload = 77;
            const cflow_event_view event = {
                MICRO_EVENT, &cmeta_type_int, &trigger_payload};
            const cflow_statechart_selection_trigger trigger = {
                CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
            check_equal(cflow_statechart_instance_select_internal(
                            &fixture.instance, &trigger, &selection),
                        CFLOW_STATECHART_RUNTIME_OK);
            check_equal(cflow_statechart_instance_try_microstep_internal(
                            &fixture.instance, &trigger, &selection),
                        CFLOW_ADMISSION_ACCEPTED);
        }
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_microstep_unchanged(&fixture, expected, 6u);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "Statechart internal event queue is full");
        check_true(cflow_statechart_instance_get_microstep_stats_internal(
            &fixture.instance, &stats));
        check_equal(stats.last_status,
                    CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL);
        check_equal(stats.internal_pending, (size_t)0u);
        microstep_fixture_destroy(&fixture);
    }

    it("cancels and finalizes one accepted queued microstep exactly once") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        microstep_executor_blocker blocker;
        cflow_statechart_selection_snapshot selection = {0};
        cflow_statechart_microstep_stats stats = {0};
        cflow_executor_control control = {0};
        const cflow_executor_task blocking_task = {
            microstep_block_executor, NULL, NULL, &blocker};
        atomic_init(&blocker.entered, false);
        atomic_init(&blocker.release, false);
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_executor_try_post_task(
                        &fixture.executor, &blocking_task),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&blocker.entered)) turbo_thread_yield();
        check_equal(microstep_submit_event(&fixture, &selection),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(cflow_executor_as_control(&fixture.executor, &control));
        check_true(cflow_executor_control_shutdown(
            &control, CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        atomic_store(&blocker.release, true);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(probe.calls, (size_t)0u);
        check_true(cflow_statechart_instance_get_microstep_stats_internal(
            &fixture.instance, &stats));
        check_equal(stats.accepted, UINT64_C(1));
        check_equal(stats.completed, UINT64_C(0));
        check_equal(stats.failed, UINT64_C(0));
        check_equal(stats.cancelled, UINT64_C(1));
        check_equal(stats.finalized, UINT64_C(1));
        check_equal(stats.last_status,
                    CFLOW_STATECHART_RUNTIME_TASK_CANCELLED);
        microstep_fixture_destroy(&fixture);
    }

    it("rolls back reservation when executor rejects without callbacks") {
        microstep_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_statechart_selection_snapshot selection = {0};
        cflow_statechart_microstep_stats stats = {0};
        cflow_executor_control control = {0};
        microstep_fixture_definition(
            &fixture, false, false, CFLOW_STATECHART_TRANSITION_EXTERNAL);
        check_equal(microstep_fixture_init(&fixture, &probe, 2u),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_true(cflow_executor_as_control(&fixture.executor, &control));
        check_true(cflow_executor_control_shutdown(
            &control, CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check_true(cflow_executor_wait_idle(&fixture.executor));
        {
            const int payload = 77;
            const cflow_event_view event = {
                MICRO_EVENT, &cmeta_type_int, &payload};
            const cflow_statechart_selection_trigger trigger = {
                CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
            check_equal(cflow_statechart_instance_select_internal(
                            &fixture.instance, &trigger, &selection),
                        CFLOW_STATECHART_RUNTIME_OK);
            check_equal(cflow_statechart_instance_try_microstep_internal(
                            &fixture.instance, &trigger, &selection),
                        CFLOW_ADMISSION_CLOSED);
        }
        check_equal(probe.calls, (size_t)0u);
        check_true(cflow_statechart_instance_get_microstep_stats_internal(
            &fixture.instance, &stats));
        check_equal(stats.accepted, UINT64_C(0));
        check_equal(stats.completed, UINT64_C(0));
        check_equal(stats.cancelled, UINT64_C(0));
        check_equal(stats.finalized, UINT64_C(0));
        microstep_fixture_destroy(&fixture);
    }
}

typedef struct history_fixture {
    cflow_statechart_state states[11];
    cflow_event_type events[6];
    cflow_statechart_executable executables[1];
    cflow_statechart_transition transitions[12];
    cflow_statechart_state_action state_actions[1];
    cflow_statechart_transition_action transition_actions[2];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_statechart_instance instance;
    int initial_state;
} history_fixture;

enum {
    HISTORY_ROOT = 100u,
    HISTORY_ROOT_INITIAL = 101u,
    HISTORY_PARENT = 200u,
    HISTORY_PARENT_INITIAL = 201u,
    HISTORY_CHILD = 300u,
    HISTORY_CHILD_INITIAL = 301u,
    HISTORY_LEAF_ONE = 310u,
    HISTORY_LEAF_TWO = 311u,
    HISTORY_OUTSIDE = 400u,
    HISTORY_SHALLOW = 500u,
    HISTORY_DEEP = 501u,
    HISTORY_MOVE_EVENT = 20u,
    HISTORY_EXIT_EVENT = 21u,
    HISTORY_SHALLOW_EVENT = 22u,
    HISTORY_DEEP_EVENT = 23u,
    HISTORY_BACK_EVENT = 24u,
    HISTORY_FAIL_EXIT_EVENT = 25u
};

static void history_fixture_definition(history_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        HISTORY_ROOT, 0u, CFLOW_STATECHART_COMPOUND, 0u};
    fixture->states[1] = (cflow_statechart_state){
        HISTORY_ROOT_INITIAL, HISTORY_ROOT, CFLOW_STATECHART_INITIAL, 1u};
    fixture->states[2] = (cflow_statechart_state){
        HISTORY_PARENT, HISTORY_ROOT, CFLOW_STATECHART_COMPOUND, 2u};
    fixture->states[3] = (cflow_statechart_state){
        HISTORY_PARENT_INITIAL, HISTORY_PARENT,
        CFLOW_STATECHART_INITIAL, 3u};
    fixture->states[4] = (cflow_statechart_state){
        HISTORY_CHILD, HISTORY_PARENT, CFLOW_STATECHART_COMPOUND, 4u};
    fixture->states[5] = (cflow_statechart_state){
        HISTORY_CHILD_INITIAL, HISTORY_CHILD,
        CFLOW_STATECHART_INITIAL, 5u};
    fixture->states[6] = (cflow_statechart_state){
        HISTORY_LEAF_ONE, HISTORY_CHILD, CFLOW_STATECHART_ATOMIC, 6u};
    fixture->states[7] = (cflow_statechart_state){
        HISTORY_LEAF_TWO, HISTORY_CHILD, CFLOW_STATECHART_ATOMIC, 7u};
    fixture->states[8] = (cflow_statechart_state){
        HISTORY_SHALLOW, HISTORY_PARENT,
        CFLOW_STATECHART_HISTORY_SHALLOW, 8u};
    fixture->states[9] = (cflow_statechart_state){
        HISTORY_DEEP, HISTORY_PARENT, CFLOW_STATECHART_HISTORY_DEEP, 9u};
    fixture->states[10] = (cflow_statechart_state){
        HISTORY_OUTSIDE, HISTORY_ROOT, CFLOW_STATECHART_ATOMIC, 10u};
    fixture->events[0] = (cflow_event_type){
        HISTORY_MOVE_EVENT, &cmeta_type_int};
    fixture->events[1] = (cflow_event_type){
        HISTORY_EXIT_EVENT, &cmeta_type_int};
    fixture->events[2] = (cflow_event_type){
        HISTORY_SHALLOW_EVENT, &cmeta_type_int};
    fixture->events[3] = (cflow_event_type){
        HISTORY_DEEP_EVENT, &cmeta_type_int};
    fixture->events[4] = (cflow_event_type){
        HISTORY_BACK_EVENT, &cmeta_type_int};
    fixture->events[5] = (cflow_event_type){
        HISTORY_FAIL_EXIT_EVENT, &cmeta_type_int};
    fixture->transitions[0] = (cflow_statechart_transition){
        1u, HISTORY_ROOT_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, HISTORY_PARENT,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        2u, HISTORY_PARENT_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, HISTORY_CHILD,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->transitions[2] = (cflow_statechart_transition){
        3u, HISTORY_CHILD_INITIAL, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, HISTORY_LEAF_ONE,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 2u};
    fixture->transitions[3] = (cflow_statechart_transition){
        4u, HISTORY_SHALLOW, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, HISTORY_CHILD,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 3u};
    fixture->transitions[4] = (cflow_statechart_transition){
        5u, HISTORY_DEEP, CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u, 0u, 0u, HISTORY_CHILD,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 4u};
    fixture->transitions[5] = (cflow_statechart_transition){
        6u, HISTORY_LEAF_ONE, CFLOW_STATECHART_TRIGGER_EVENT,
        HISTORY_MOVE_EVENT, 0u, 0u, HISTORY_LEAF_TWO,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 5u};
    fixture->transitions[6] = (cflow_statechart_transition){
        7u, HISTORY_LEAF_TWO, CFLOW_STATECHART_TRIGGER_EVENT,
        HISTORY_EXIT_EVENT, 0u, 0u, HISTORY_OUTSIDE,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 6u};
    fixture->transitions[7] = (cflow_statechart_transition){
        8u, HISTORY_OUTSIDE, CFLOW_STATECHART_TRIGGER_EVENT,
        HISTORY_SHALLOW_EVENT, 0u, 0u, HISTORY_SHALLOW,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 7u};
    fixture->transitions[8] = (cflow_statechart_transition){
        9u, HISTORY_OUTSIDE, CFLOW_STATECHART_TRIGGER_EVENT,
        HISTORY_DEEP_EVENT, 0u, 0u, HISTORY_DEEP,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 8u};
    fixture->transitions[9] = (cflow_statechart_transition){
        10u, HISTORY_LEAF_TWO, CFLOW_STATECHART_TRIGGER_EVENT,
        HISTORY_BACK_EVENT, 0u, 0u, HISTORY_LEAF_ONE,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 9u};
    fixture->transitions[10] = (cflow_statechart_transition){
        11u, HISTORY_LEAF_ONE, CFLOW_STATECHART_TRIGGER_EVENT,
        HISTORY_FAIL_EXIT_EVENT, 0u, 0u, HISTORY_OUTSIDE,
        CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 10u};
    fixture->executables[0] = (cflow_statechart_executable){
        MICRO_EXECUTABLE, &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    fixture->state_actions[0] = (cflow_statechart_state_action){
        HISTORY_LEAF_ONE, CFLOW_STATECHART_STATE_ACTION_EXIT,
        MICRO_EXECUTABLE, 0u};
    fixture->transition_actions[0] =
        (cflow_statechart_transition_action){
            4u, MICRO_EXECUTABLE, 0u};
    fixture->transition_actions[1] =
        (cflow_statechart_transition_action){
            3u, MICRO_EXECUTABLE, 0u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 11u,
        fixture->events, 6u, NULL, 0u,
        fixture->executables, 1u,
        fixture->transitions, 11u,
        fixture->state_actions, 1u,
        fixture->transition_actions, 2u};
    fixture->initial_state = 17;
}

static void history_fixture_init(history_fixture *fixture,
                                 microstep_action_probe *probe) {
    const cflow_statechart_executable_binding binding = {
        MICRO_EXECUTABLE, microstep_action, probe};
    cflow_statechart_instance_config config = {
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .executables = &binding,
        .executable_count = 1u,
        .internal_event_capacity = 2u,
        .executor = &fixture->executor};
    check_equal(cflow_statechart_build(
                    &fixture->statechart, &fixture->definition),
                CFLOW_STATECHART_OK);
    check_true(cflow_executor_serial_init(&fixture->executor));
    probe->executor = &fixture->executor;
    probe->executor_only = true;
    probe->no_alias = true;
    check_equal(cflow_statechart_instance_init(&fixture->instance, &config),
                CFLOW_STATECHART_RUNTIME_OK);
}

static void history_fixture_destroy(history_fixture *fixture) {
    check_equal(cflow_statechart_instance_destroy(&fixture->instance),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&fixture->executor);
    cflow_statechart_destroy(&fixture->statechart);
}

static void history_microstep(history_fixture *fixture, cflow_event_id id) {
    const int payload = 1;
    const cflow_event_view event = {id, &cmeta_type_int, &payload};
    const cflow_statechart_selection_trigger trigger = {
        CFLOW_STATECHART_TRIGGER_EVENT, &event, 0u};
    cflow_statechart_selection_snapshot selection = {0};
    check_equal(cflow_statechart_instance_select_internal(
                    &fixture->instance, &trigger, &selection),
                CFLOW_STATECHART_RUNTIME_OK);
    check_equal(selection.transition_count, (size_t)1u);
    check_equal(cflow_statechart_instance_try_microstep_internal(
                    &fixture->instance, &trigger, &selection),
                CFLOW_ADMISSION_ACCEPTED);
    check_true(cflow_executor_wait_idle(&fixture->executor));
}

static void check_history_configuration(
    history_fixture *fixture,
    const cflow_machine_state_id *expected,
    size_t expected_count,
    uint64_t expected_version) {
    cflow_machine_state_id actual[5] = {0};
    size_t count = 0u;
    uint64_t version = 0u;
    check_equal(cflow_statechart_instance_copy_configuration(
                    &fixture->instance, actual, 5u, &count, &version),
                CFLOW_STATECHART_SNAPSHOT_OK);
    check_equal(count, expected_count);
    check_equal(actual, expected, expected_count * sizeof(*expected));
    check_equal(version, expected_version);
}

suite("CFlow Statechart history restoration") {
    it("uses the declared history default when the slot is unset") {
        history_fixture fixture;
        microstep_action_probe probe = {0};
        const cflow_machine_state_id expected[] = {
            HISTORY_ROOT, HISTORY_PARENT, HISTORY_CHILD, HISTORY_LEAF_ONE};
        const int expected_trace[] = {
            microstep_trace_code(
                CFLOW_STATECHART_ACTION_HISTORY, HISTORY_SHALLOW),
            microstep_trace_code(
                CFLOW_STATECHART_ACTION_INITIAL, HISTORY_CHILD_INITIAL)};
        history_fixture_definition(&fixture);
        fixture.transitions[0].target = HISTORY_OUTSIDE;
        history_fixture_init(&fixture, &probe);
        history_microstep(&fixture, HISTORY_SHALLOW_EVENT);
        check_history_configuration(&fixture, expected, 4u, UINT64_C(2));
        check_equal(probe.trace, expected_trace, sizeof(expected_trace));
        history_fixture_destroy(&fixture);
    }

    it("restores shallow history child then enters its default leaf") {
        history_fixture fixture;
        microstep_action_probe probe = {0};
        const cflow_machine_state_id expected[] = {
            HISTORY_ROOT, HISTORY_PARENT, HISTORY_CHILD, HISTORY_LEAF_ONE};
        history_fixture_definition(&fixture);
        history_fixture_init(&fixture, &probe);
        history_microstep(&fixture, HISTORY_MOVE_EVENT);
        history_microstep(&fixture, HISTORY_EXIT_EVENT);
        history_microstep(&fixture, HISTORY_SHALLOW_EVENT);
        check_history_configuration(&fixture, expected, 4u, UINT64_C(4));
        history_fixture_destroy(&fixture);
    }

    it("restores deep history leaf and all of its real ancestors") {
        history_fixture fixture;
        microstep_action_probe probe = {0};
        const cflow_machine_state_id expected[] = {
            HISTORY_ROOT, HISTORY_PARENT, HISTORY_CHILD, HISTORY_LEAF_TWO};
        history_fixture_definition(&fixture);
        history_fixture_init(&fixture, &probe);
        history_microstep(&fixture, HISTORY_MOVE_EVENT);
        history_microstep(&fixture, HISTORY_EXIT_EVENT);
        history_microstep(&fixture, HISTORY_DEEP_EVENT);
        check_history_configuration(&fixture, expected, 4u, UINT64_C(4));
        history_fixture_destroy(&fixture);
    }

    it("does not publish a staged history overwrite when an exit action fails") {
        history_fixture fixture;
        microstep_action_probe probe = {0};
        cflow_machine_state_id before[1] = {0}, after[1] = {0};
        size_t before_count = 0u, after_count = 0u;
        history_fixture_definition(&fixture);
        history_fixture_init(&fixture, &probe);
        history_microstep(&fixture, HISTORY_MOVE_EVENT);
        history_microstep(&fixture, HISTORY_EXIT_EVENT);
        check_equal(cflow_statechart_instance_copy_history_internal(
                        &fixture.instance, HISTORY_DEEP,
                        before, 1u, &before_count),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(before_count, (size_t)1u);
        check_equal(before[0], (cflow_machine_state_id)HISTORY_LEAF_TWO);
        history_microstep(&fixture, HISTORY_DEEP_EVENT);
        history_microstep(&fixture, HISTORY_BACK_EVENT);
        probe.calls = 0u;
        probe.fail_call = 1u;
        history_microstep(&fixture, HISTORY_FAIL_EXIT_EVENT);
        check_equal(cflow_statechart_instance_copy_history_internal(
                        &fixture.instance, HISTORY_DEEP,
                        after, 1u, &after_count),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(after_count, (size_t)1u);
        check_equal(after[0], (cflow_machine_state_id)HISTORY_LEAF_TWO);
        check_equal(cflow_statechart_instance_error(&fixture.instance),
                    "first action failure");
        history_fixture_destroy(&fixture);
    }
}
