#include <cflow/actor.h>

#include "tinytest.h"

#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

enum {
    STATECHART_ACTOR_ROOT = 1u,
    STATECHART_ACTOR_INITIAL = 2u,
    STATECHART_ACTOR_ACTIVE = 3u,
    STATECHART_ACTOR_FINAL = 4u,
    STATECHART_ACTOR_GO = 100u,
    STATECHART_ACTOR_EXECUTABLE = 200u
};

typedef struct statechart_actor_probe {
    atomic_int action_calls;
    atomic_int done_calls;
    atomic_int error_calls;
    atomic_bool blocker_entered;
    atomic_bool blocker_release;
    bool fail_action;
    char error[128];
} statechart_actor_probe;

typedef struct statechart_actor_fixture {
    cflow_statechart_state states[4];
    cflow_event_type events[1];
    cflow_statechart_executable executables[1];
    cflow_statechart_transition transitions[2];
    cflow_statechart_transition_action transition_actions[1];
    cflow_statechart_definition definition;
    cflow_statechart statechart;
    cflow_executor executor;
    cflow_actor actor;
    statechart_actor_probe probe;
    int initial_state;
} statechart_actor_fixture;

enum { STATECHART_ACTOR_WAIT_ATTEMPTS = 1000 };

static bool statechart_actor_wait_bool(atomic_bool *value) {
    size_t attempt;
    for (attempt = 0u; attempt < STATECHART_ACTOR_WAIT_ATTEMPTS; ++attempt) {
        if (atomic_load(value)) return true;
        turbo_sleep_ms(1u);
    }
    return atomic_load(value);
}

static bool statechart_actor_wait_int(atomic_int *value, int expected) {
    size_t attempt;
    for (attempt = 0u; attempt < STATECHART_ACTOR_WAIT_ATTEMPTS; ++attempt) {
        if (atomic_load(value) == expected) return true;
        turbo_sleep_ms(1u);
    }
    return atomic_load(value) == expected;
}

static void statechart_actor_block_executor(void *user) {
    statechart_actor_probe *probe = (statechart_actor_probe *)user;
    atomic_store(&probe->blocker_entered, true);
    while (!atomic_load(&probe->blocker_release)) turbo_thread_yield();
}

static bool statechart_actor_action(
    void *user, cflow_statechart_action_phase phase,
    cflow_machine_state_id owner, const void *state,
    const cflow_event_view *event, void *out_state,
    cflow_statechart_raise_fn raise_internal, void *raise_user,
    const char **out_error) {
    statechart_actor_probe *probe = (statechart_actor_probe *)user;
    (void)phase;
    (void)owner;
    (void)raise_internal;
    (void)raise_user;
    if (probe == NULL || state == NULL || event == NULL ||
        out_state == NULL || out_error == NULL)
        return false;
    atomic_fetch_add(&probe->action_calls, 1);
    if (probe->fail_action) {
        *out_error = "statechart actor action failure";
        return false;
    }
    *(int *)out_state = *(const int *)state + *(const int *)event->payload;
    *out_error = NULL;
    return true;
}

static void statechart_actor_done(void *user) {
    statechart_actor_probe *probe = (statechart_actor_probe *)user;
    atomic_fetch_add(&probe->done_calls, 1);
}

static void statechart_actor_error(void *user, const char *message) {
    statechart_actor_probe *probe = (statechart_actor_probe *)user;
    atomic_fetch_add(&probe->error_calls, 1);
    if (message != NULL) {
        strncpy(probe->error, message, sizeof(probe->error) - 1u);
        probe->error[sizeof(probe->error) - 1u] = '\0';
    }
}

static void statechart_actor_definition(statechart_actor_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    atomic_init(&fixture->probe.action_calls, 0);
    atomic_init(&fixture->probe.done_calls, 0);
    atomic_init(&fixture->probe.error_calls, 0);
    atomic_init(&fixture->probe.blocker_entered, false);
    atomic_init(&fixture->probe.blocker_release, false);
    fixture->states[0] = (cflow_statechart_state){
        STATECHART_ACTOR_ROOT, 0u, CFLOW_STATECHART_COMPOUND, 0u};
    fixture->states[1] = (cflow_statechart_state){
        STATECHART_ACTOR_INITIAL, STATECHART_ACTOR_ROOT,
        CFLOW_STATECHART_INITIAL, 1u};
    fixture->states[2] = (cflow_statechart_state){
        STATECHART_ACTOR_ACTIVE, STATECHART_ACTOR_ROOT,
        CFLOW_STATECHART_ATOMIC, 2u};
    fixture->states[3] = (cflow_statechart_state){
        STATECHART_ACTOR_FINAL, STATECHART_ACTOR_ROOT,
        CFLOW_STATECHART_FINAL, 3u};
    fixture->events[0] = (cflow_event_type){
        STATECHART_ACTOR_GO, &cmeta_type_int};
    fixture->executables[0] = (cflow_statechart_executable){
        STATECHART_ACTOR_EXECUTABLE, &cmeta_type_int,
        CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    fixture->transitions[0] = (cflow_statechart_transition){
        1u, STATECHART_ACTOR_INITIAL,
        CFLOW_STATECHART_TRIGGER_EVENTLESS, 0u, 0u, 0u,
        STATECHART_ACTOR_ACTIVE, CFLOW_STATECHART_TRANSITION_EXTERNAL,
        0u, 0u};
    fixture->transitions[1] = (cflow_statechart_transition){
        2u, STATECHART_ACTOR_ACTIVE,
        CFLOW_STATECHART_TRIGGER_EVENT, STATECHART_ACTOR_GO, 0u, 0u,
        STATECHART_ACTOR_FINAL, CFLOW_STATECHART_TRANSITION_EXTERNAL,
        0u, 1u};
    fixture->transition_actions[0] =
        (cflow_statechart_transition_action){
            2u, STATECHART_ACTOR_EXECUTABLE, 0u};
    fixture->definition = (cflow_statechart_definition){
        &cmeta_type_int, fixture->states, 4u,
        fixture->events, 1u, NULL, 0u,
        fixture->executables, 1u, fixture->transitions, 2u,
        NULL, 0u, fixture->transition_actions, 1u};
}

static cflow_statechart_actor_init_result statechart_actor_fixture_init(
    statechart_actor_fixture *fixture, bool fail_action) {
    cflow_statechart_executable_binding binding;
    cflow_statechart_actor_config config;
    statechart_actor_definition(fixture);
    fixture->probe.fail_action = fail_action;
    check_equal(cflow_statechart_build(
                    &fixture->statechart, &fixture->definition),
                CFLOW_STATECHART_OK);
    check_true(cflow_executor_serial_init_with_capacity(
        &fixture->executor, 8u));
    binding = (cflow_statechart_executable_binding){
        STATECHART_ACTOR_EXECUTABLE,
        statechart_actor_action, &fixture->probe};
    config = (cflow_statechart_actor_config){
        .statechart = {
            .statechart = &fixture->statechart,
            .initial_state = &fixture->initial_state,
            .executables = &binding,
            .executable_count = 1u,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .executor = &fixture->executor},
        .callbacks = {
            .on_error = statechart_actor_error,
            .on_done = statechart_actor_done,
            .user = &fixture->probe}};
    return cflow_actor_init_statechart(&fixture->actor, &config);
}

static void statechart_actor_fixture_destroy(
    statechart_actor_fixture *fixture) {
    cflow_actor_destroy(&fixture->actor);
    if (cflow_executor_valid(&fixture->executor))
        cflow_executor_destroy(&fixture->executor);
    cflow_statechart_destroy(&fixture->statechart);
}

suite("CFlow Statechart Actor facade") {
    it("preserves exact Statechart initialization rejection") {
        cflow_actor actor = {0};
        cflow_statechart_actor_config config = {0};
        cflow_statechart_actor_init_result result =
            cflow_actor_init_statechart(NULL, &config);

        check_equal(result.status, CFLOW_ACTOR_INVALID_ARGUMENT);
        check_equal(result.statechart_status,
                    CFLOW_STATECHART_RUNTIME_OK);
        result = cflow_actor_init_statechart(&actor, NULL);
        check_equal(result.status, CFLOW_ACTOR_INVALID_ARGUMENT);
        result = cflow_actor_init_statechart(&actor, &config);
        check_equal(result.status, CFLOW_ACTOR_STATECHART_REJECTED);
        check_equal(result.statechart_status,
                    CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT);
        check_null(actor.impl);
    }

    it("maps bounded admission then stops on clean final completion") {
        statechart_actor_fixture fixture;
        cflow_actor_ref ref = {0};
        cflow_actor_ref retained = {0};
        cflow_statechart_actor_stats stats = {0};
        const int payload = 5;
        const long wrong_payload = 5;
        const cflow_event_view go = {
            STATECHART_ACTOR_GO, &cmeta_type_int, &payload};
        const cflow_event_view wrong = {
            STATECHART_ACTOR_GO, &cmeta_type_long, &wrong_payload};
        cflow_statechart_actor_init_result result =
            statechart_actor_fixture_init(&fixture, false);

        check_equal(result.status, CFLOW_ACTOR_OK);
        check_equal(result.statechart_status, CFLOW_STATECHART_RUNTIME_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_true(cflow_actor_ref_retain(&ref, &retained));
        check_equal(cflow_actor_ref_try_send(&ref, &go),
                    CFLOW_ACTOR_SEND_NOT_STARTED);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_ref_try_send(&ref, &wrong),
                    CFLOW_ACTOR_SEND_TYPE_MISMATCH);

        check_equal(cflow_executor_try_post(
                        &fixture.executor,
                        statechart_actor_block_executor, &fixture.probe),
                    CFLOW_ADMISSION_ACCEPTED);
        check_true(statechart_actor_wait_bool(
            &fixture.probe.blocker_entered));
        check_equal(cflow_actor_ref_try_send(&ref, &go),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(cflow_actor_ref_try_send(&ref, &go),
                    CFLOW_ACTOR_SEND_FULL);
        atomic_store(&fixture.probe.blocker_release, true);

        check_equal(cflow_actor_wait(&fixture.actor),
                    CFLOW_ACTOR_STATE_STOPPED);
        check_equal(atomic_load(&fixture.probe.action_calls), 1);
        check_true(statechart_actor_wait_int(
            &fixture.probe.done_calls, 1));
        check_equal(atomic_load(&fixture.probe.error_calls), 0);
        check_true(cflow_actor_get_statechart_stats(
            &fixture.actor, &stats));
        check_equal(stats.state, CFLOW_ACTOR_STATE_STOPPED);
        check_equal(stats.statechart.external_accepted, UINT64_C(1));
        check_equal(stats.statechart.external_completed, UINT64_C(1));
        check_equal(stats.rejected_not_started, UINT64_C(1));
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_STOPPED);

        cflow_actor_destroy(&fixture.actor);
        check_equal(cflow_actor_ref_try_send(&retained, &go),
                    CFLOW_ACTOR_SEND_STALE);
        cflow_actor_ref_release(&retained);
        cflow_actor_ref_release(&ref);
        statechart_actor_fixture_destroy(&fixture);
    }

    it("stops from START and RUNNING without publishing an error") {
        statechart_actor_fixture start_fixture;
        statechart_actor_fixture running_fixture;
        cflow_statechart_actor_init_result result =
            statechart_actor_fixture_init(&start_fixture, false);

        check_equal(result.status, CFLOW_ACTOR_OK);
        check_equal(cflow_actor_request_stop(&start_fixture.actor),
                    CFLOW_ACTOR_OK);
        check_equal(cflow_actor_current_state(&start_fixture.actor),
                    CFLOW_ACTOR_STATE_STOPPED);
        check_equal(atomic_load(&start_fixture.probe.done_calls), 0);
        check_equal(cflow_actor_start(&start_fixture.actor),
                    CFLOW_ACTOR_STOPPED);
        statechart_actor_fixture_destroy(&start_fixture);

        result = statechart_actor_fixture_init(&running_fixture, false);
        check_equal(result.status, CFLOW_ACTOR_OK);
        check_equal(cflow_actor_start(&running_fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_request_stop(&running_fixture.actor),
                    CFLOW_ACTOR_OK);
        check_equal(cflow_actor_wait(&running_fixture.actor),
                    CFLOW_ACTOR_STATE_STOPPED);
        check_true(statechart_actor_wait_int(
            &running_fixture.probe.done_calls, 1));
        check_equal(atomic_load(&running_fixture.probe.error_calls), 0);
        statechart_actor_fixture_destroy(&running_fixture);
    }

    it("preserves Statechart runtime failure and failed accounting") {
        statechart_actor_fixture fixture;
        cflow_actor_ref ref = {0};
        cflow_statechart_actor_stats stats = {0};
        const int payload = 1;
        const cflow_event_view go = {
            STATECHART_ACTOR_GO, &cmeta_type_int, &payload};
        cflow_statechart_actor_init_result result =
            statechart_actor_fixture_init(&fixture, true);

        check_equal(result.status, CFLOW_ACTOR_OK);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &go),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(cflow_actor_wait(&fixture.actor),
                    CFLOW_ACTOR_STATE_FAILED);
        check_equal(cflow_actor_error(&fixture.actor),
                    "statechart actor action failure");
        check_equal(atomic_load(&fixture.probe.done_calls), 0);
        check_true(statechart_actor_wait_int(
            &fixture.probe.error_calls, 1));
        check_equal(fixture.probe.error,
                    "statechart actor action failure");
        check_true(cflow_actor_get_statechart_stats(
            &fixture.actor, &stats));
        check_equal(stats.statechart.external_failed, UINT64_C(1));
        check_true(stats.statechart.errored);

        cflow_actor_ref_release(&ref);
        statechart_actor_fixture_destroy(&fixture);
    }
}
