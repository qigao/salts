#include <cflow/cflow.h>

#include "tinytest.h"

#include <stdatomic.h>
#include <string.h>

enum {
    ADAPTER_ROOT = 10,
    ADAPTER_INITIAL = 20,
    ADAPTER_ACTIVE = 30,
    ADAPTER_FINAL = 40,
    ADAPTER_EVENT = 100,
    ADAPTER_INITIAL_TRANSITION = 200,
    ADAPTER_FINAL_TRANSITION = 201
};

typedef struct adapter_fixture {
    cflow_statechart_state states[4];
    cflow_event_type events[1];
    cflow_statechart_transition transitions[2];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_statechart_instance instance;
    int initial_state;
} adapter_fixture;

typedef struct adapter_sink_probe {
    size_t values;
    size_t dones;
    const char *error;
} adapter_sink_probe;

static void adapter_fixture_define(adapter_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->states[0] = (cflow_statechart_state){
        ADAPTER_ROOT, 0u, CFLOW_STATECHART_COMPOUND, 0u};
    fixture->states[1] = (cflow_statechart_state){
        ADAPTER_INITIAL, ADAPTER_ROOT, CFLOW_STATECHART_INITIAL, 1u};
    fixture->states[2] = (cflow_statechart_state){
        ADAPTER_ACTIVE, ADAPTER_ROOT, CFLOW_STATECHART_ATOMIC, 2u};
    fixture->states[3] = (cflow_statechart_state){
        ADAPTER_FINAL, ADAPTER_ROOT, CFLOW_STATECHART_FINAL, 3u};
    fixture->events[0] = (cflow_event_type){
        ADAPTER_EVENT, &cmeta_type_int};
    fixture->transitions[0] = (cflow_statechart_transition){
        ADAPTER_INITIAL_TRANSITION, ADAPTER_INITIAL,
        CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u,
        ADAPTER_ACTIVE, CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        ADAPTER_FINAL_TRANSITION, ADAPTER_ACTIVE,
        CFLOW_STATECHART_TRIGGER_EVENT, ADAPTER_EVENT, 0u, 0u,
        ADAPTER_FINAL, CFLOW_STATECHART_TRANSITION_EXTERNAL, 0u, 1u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int,
        fixture->states, 4u,
        fixture->events, 1u,
        NULL, 0u,
        NULL, 0u,
        fixture->transitions, 2u,
        NULL, 0u,
        NULL, 0u};
    fixture->initial_state = 41;
}

static bool adapter_fixture_init(adapter_fixture *fixture) {
    cflow_statechart_instance_config config;
    adapter_fixture_define(fixture);
    if (cflow_statechart_build(&fixture->statechart,
                               &fixture->definition) !=
        CFLOW_STATECHART_OK)
        return false;
    if (!cflow_executor_serial_init(&fixture->executor)) {
        cflow_statechart_destroy(&fixture->statechart);
        return false;
    }
    config = (cflow_statechart_instance_config){
        .statechart = &fixture->statechart,
        .initial_state = &fixture->initial_state,
        .external_event_capacity = 4u,
        .internal_event_capacity = 4u,
        .completion_capacity = 4u,
        .microstep_limit = 16u,
        .executor = &fixture->executor};
    if (cflow_statechart_instance_init(&fixture->instance, &config) !=
        CFLOW_STATECHART_RUNTIME_OK) {
        cflow_executor_destroy(&fixture->executor);
        cflow_statechart_destroy(&fixture->statechart);
        return false;
    }
    return true;
}

static void adapter_fixture_destroy(adapter_fixture *fixture) {
    check_equal(cflow_statechart_instance_destroy(&fixture->instance),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&fixture->executor);
    cflow_statechart_destroy(&fixture->statechart);
}

static void destroy_resumable(cflow_resumable *resumable) {
    if (resumable == NULL) return;
    if (resumable->ops != NULL && resumable->ops->destroy != NULL)
        resumable->ops->destroy(resumable->state);
    *resumable = (cflow_resumable){0};
}

static void destroy_source(cflow_source *source) {
    if (source == NULL) return;
    if (cflow_source_valid(source)) cflow_source_destroy(source);
    *source = (cflow_source){0};
}

static void adapter_wake(void *user) {
    atomic_size_t *wakes = (atomic_size_t *)user;
    if (wakes != NULL) atomic_fetch_add(wakes, 1u);
}

static bool adapter_sink_value(void *user,
                               const cmeta_type_desc *type,
                               const void *value) {
    adapter_sink_probe *probe = (adapter_sink_probe *)user;
    (void)type;
    (void)value;
    if (probe != NULL) ++probe->values;
    return true;
}

static void adapter_sink_error(void *user, const char *message) {
    adapter_sink_probe *probe = (adapter_sink_probe *)user;
    if (probe != NULL) probe->error = message;
}

static void adapter_sink_done(void *user) {
    adapter_sink_probe *probe = (adapter_sink_probe *)user;
    if (probe != NULL) ++probe->dones;
}

spec("CFlow Statechart terminal Runtime adapter") {
    it("projects an active instance as one WAIT-to-DONE Resumable") {
        adapter_fixture fixture;
        cflow_resumable resumable = {0};
        cflow_resumable rejected = {0};
        cflow_resumable occupied = {
            "occupied", &cmeta_type_int, NULL, &fixture};
        cflow_resume_ctx context = {0};
        cflow_step step;
        atomic_size_t wakes;
        const int payload = 7;
        const cflow_event_view event = {
            ADAPTER_EVENT, &cmeta_type_int, &payload};
        int output = 73;

        atomic_init(&wakes, 0u);
        check_true(adapter_fixture_init(&fixture));
        check_false(cflow_statechart_instance_as_terminal_resumable(
            &fixture.instance, &occupied));
        check_equal(occupied.state, &fixture);
        check_true(cflow_statechart_instance_as_terminal_resumable(
            &fixture.instance, &resumable));
        check_true(cmeta_type_equal(resumable.output_type, &cmeta_type_int));
        check_false(cflow_statechart_instance_as_terminal_resumable(
            &fixture.instance, &rejected));
        check_null(rejected.ops);
        check_null(rejected.state);

        step = resumable.ops->resume(resumable.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_equal(output, 73);
        check_true(cflow_waitable_arm(
            &step.waitable, (cflow_waker){adapter_wake, &wakes}));

        check_equal(cflow_statechart_instance_try_send(
                        &fixture.instance, &event),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_equal(atomic_load(&wakes), (size_t)1u);
        step = resumable.ops->resume(resumable.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_DONE);
        check_equal(output, 73);

        destroy_resumable(&resumable);
        adapter_fixture_destroy(&fixture);
    }

    it("reports stable Source terminal errors and wakes both observers") {
        adapter_fixture fixture;
        cflow_source source = {0};
        cflow_source occupied = {0};
        cflow_resume_ctx context = {0};
        cflow_step step;
        const char *terminal_error = NULL;
        const int payload = 7;
        const cflow_event_view event = {
            ADAPTER_EVENT, &cmeta_type_int, &payload};
        atomic_size_t resume_wakes;
        atomic_size_t terminal_wakes;
        const int occupied_input = 1;
        void *occupied_self;
        const cflow_source_vtable *occupied_vtable;
        int output = 91;

        atomic_init(&resume_wakes, 0u);
        atomic_init(&terminal_wakes, 0u);
        check_true(adapter_fixture_init(&fixture));
        check_true(cflow_source_from_array(
            &occupied, &cmeta_type_int, &occupied_input, 1u));
        occupied_self = occupied.self;
        occupied_vtable = occupied.vtable;
        check_false(cflow_statechart_instance_as_terminal_source(
            &fixture.instance, &occupied));
        check_true(occupied.self == occupied_self);
        check_true(occupied.vtable == occupied_vtable);
        destroy_source(&occupied);
        check_true(cflow_statechart_instance_as_terminal_source(
            &fixture.instance, &source));
        check_true(cmeta_type_equal(
            cflow_source_output_type(&source), &cmeta_type_int));
        check_equal(cflow_source_poll_terminal(&source, &terminal_error),
                    CFLOW_SOURCE_OPEN);
        check_null(terminal_error);
        cflow_source_bind_terminal_waker(
            &source, (cflow_waker){adapter_wake, &terminal_wakes});
        step = cflow_source_resume(&source, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        check_true(cflow_waitable_arm(
            &step.waitable,
            (cflow_waker){adapter_wake, &resume_wakes}));

        check_true(cflow_executor_shutdown(&fixture.executor));
        check_equal(cflow_statechart_instance_try_send(
                        &fixture.instance, &event),
                    CFLOW_MAILBOX_OK);
        check_equal(atomic_load(&resume_wakes), (size_t)1u);
        check_equal(atomic_load(&terminal_wakes), (size_t)1u);

        step = cflow_source_resume(&source, &context, &output);
        check_equal(step.kind, CFLOW_STEP_ERROR);
        check_not_null(step.error);
        check_equal(output, 91);
        check_equal(cflow_source_poll_terminal(&source, &terminal_error),
                    CFLOW_SOURCE_ERROR);
        check_equal(terminal_error, step.error);

        destroy_source(&source);
        adapter_fixture_destroy(&fixture);
    }

    it("keeps the instance alive until the borrowed adapter detaches") {
        adapter_fixture fixture;
        cflow_resumable resumable = {0};

        check_true(adapter_fixture_init(&fixture));
        check_true(cflow_statechart_instance_as_terminal_resumable(
            &fixture.instance, &resumable));
        check_equal(cflow_statechart_instance_destroy(&fixture.instance),
                    CFLOW_STATECHART_RUNTIME_WOULD_BLOCK);
        check_not_null(fixture.instance.impl);

        destroy_resumable(&resumable);
        adapter_fixture_destroy(&fixture);
    }

    it("moves the terminal Source into Run without emitting values") {
        adapter_fixture fixture;
        cflow_source source = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        adapter_sink_probe probe = {0};
        cflow_sink_callbacks callbacks = {
            adapter_sink_value,
            adapter_sink_error,
            adapter_sink_done,
            &probe};
        cflow_sink sink = cflow_sink_from_callbacks(&callbacks);

        normalized.root = CMETA_INVALID_ID;
        check_true(adapter_fixture_init(&fixture));
        check_true(cflow_statechart_instance_as_terminal_source(
            &fixture.instance, &source));
        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_false(cflow_source_valid(&source));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        cflow_statechart_instance_close(&fixture.instance);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(probe.values, (size_t)0u);
        check_equal(probe.dones, (size_t)1u);
        check_null(probe.error);
        check_true(cflow_run_is_done(&run));

        cflow_run_close(&run);
        adapter_fixture_destroy(&fixture);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }
}
