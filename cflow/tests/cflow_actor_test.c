#include <cflow/actor.h>

#include <turbo/thread.h>

#include "tinytest.h"

#include <stdatomic.h>

typedef struct actor_probe {
    atomic_int action_calls;
    atomic_int values;
    atomic_int errors;
    atomic_int dones;
    atomic_long last_value;
} actor_probe;

typedef struct actor_blocker {
    atomic_bool entered;
    atomic_bool release;
} actor_blocker;

typedef struct actor_fixture {
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_machine_action_binding action_binding;
    actor_probe probe;
    int initial_state;
} actor_fixture;

static bool wait_until_true(atomic_bool *value) {
    const size_t timeout_ms = 5000u;
    for (size_t elapsed = 0u; elapsed < timeout_ms; ++elapsed) {
        if (atomic_load(value)) return true;
        turbo_sleep_ms(1u);
    }
    return atomic_load(value);
}

static bool wait_until_at_least(atomic_int *value, int expected) {
    const size_t timeout_ms = 5000u;
    for (size_t elapsed = 0u; elapsed < timeout_ms; ++elapsed) {
        if (atomic_load(value) >= expected) return true;
        turbo_sleep_ms(1u);
    }
    return atomic_load(value) >= expected;
}

static void block_scheduler(void *user) {
    actor_blocker *blocker = (actor_blocker *)user;
    const size_t timeout_ms = 5000u;
    atomic_store(&blocker->entered, true);
    for (size_t elapsed = 0u;
         elapsed < timeout_ms && !atomic_load(&blocker->release);
         ++elapsed)
        turbo_sleep_ms(1u);
}

static bool actor_action(void *user,
                         const void *state,
                         const void *event,
                         void *out_target_state,
                         void *out_observation,
                         const char **out_error) {
    actor_probe *probe = (actor_probe *)user;
    if (probe == NULL || state == NULL || event == NULL ||
        out_target_state == NULL || out_observation == NULL ||
        out_error == NULL)
        return false;
    *(long *)out_target_state = (long)*(const int *)state +
                                (*(const bool *)event ? 1L : 0L);
    *(long *)out_observation = 42L;
    *out_error = NULL;
    atomic_fetch_add(&probe->action_calls, 1);
    return true;
}

static bool actor_on_value(void *user,
                           const cmeta_type_desc *type,
                           const void *value) {
    actor_probe *probe = (actor_probe *)user;
    if (probe == NULL || !cmeta_type_equal(type, &cmeta_type_long) ||
        value == NULL)
        return false;
    atomic_store(&probe->last_value, *(const long *)value);
    atomic_fetch_add(&probe->values, 1);
    return true;
}

static void actor_on_error(void *user, const char *message) {
    actor_probe *probe = (actor_probe *)user;
    if (probe != NULL && message != NULL)
        atomic_fetch_add(&probe->errors, 1);
}

static void actor_on_done(void *user) {
    actor_probe *probe = (actor_probe *)user;
    if (probe != NULL) atomic_fetch_add(&probe->dones, 1);
}

static cflow_machine_definition actor_definition(
    cflow_machine_state *states,
    cflow_event_type *events,
    cflow_machine_action *actions,
    cflow_machine_transition *transitions) {
    states[0] = (cflow_machine_state){
        10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE};
    states[1] = (cflow_machine_state){
        20u, &cmeta_type_long, CFLOW_MACHINE_STATE_ACTIVE};
    events[0] = (cflow_event_type){100u, &cmeta_type_bool};
    actions[0] = (cflow_machine_action){
        300u, &cmeta_type_int, 100u, &cmeta_type_bool,
        &cmeta_type_long, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_long, 0u};
    transitions[0] = (cflow_machine_transition){
        10u, 100u, 0u, 300u, 20u, 1u};
    return (cflow_machine_definition){
        states, 2u, 10u,
        events, 1u,
        NULL, 0u,
        actions, 1u,
        transitions, 1u};
}

static bool actor_fixture_init(actor_fixture *fixture) {
    cflow_machine_state states[2];
    cflow_event_type events[1];
    cflow_machine_action actions[1];
    cflow_machine_transition transitions[1];
    const cflow_machine_definition definition = actor_definition(
        states, events, actions, transitions);
    cflow_actor_config config = {0};

    *fixture = (actor_fixture){0};
    fixture->initial_state = 7;
    fixture->action_binding = (cflow_machine_action_binding){
        300u, actor_action, &fixture->probe};
    if (cflow_machine_build(&fixture->machine, &definition) !=
        CFLOW_MACHINE_OK)
        return false;
    if (!cflow_executor_serial_init(&fixture->executor)) return false;
    if (!cflow_scheduler_worker_init(&fixture->scheduler, 1u)) return false;

    config.machine = (cflow_machine_instance_config){
        &fixture->machine,
        &fixture->initial_state,
        &cmeta_type_long,
        NULL,
        0u,
        &fixture->action_binding,
        1u,
        1u,
        &fixture->executor};
    config.scheduler = &fixture->scheduler;
    config.callbacks = (cflow_sink_callbacks){
        actor_on_value, actor_on_error, actor_on_done, &fixture->probe};
    return cflow_actor_init(&fixture->actor, &config).status == CFLOW_ACTOR_OK;
}

static void actor_fixture_destroy(actor_fixture *fixture) {
    cflow_actor_destroy(&fixture->actor);
    if (cflow_scheduler_valid(&fixture->scheduler))
        cflow_scheduler_destroy(&fixture->scheduler);
    if (cflow_executor_valid(&fixture->executor))
        cflow_executor_destroy(&fixture->executor);
    cflow_machine_destroy(&fixture->machine);
}

suite("CFlow Actor lifecycle") {
    it("rejects invalid owners and preserves Machine initialization status") {
        cflow_actor actor = {0};
        cflow_actor_config config = {0};
        cflow_actor_init_result result = cflow_actor_init(NULL, &config);

        check_equal(result.status, CFLOW_ACTOR_INVALID_ARGUMENT);
        check_equal(result.machine_status, CFLOW_MACHINE_RUNTIME_OK);
        result = cflow_actor_init(&actor, NULL);
        check_equal(result.status, CFLOW_ACTOR_INVALID_ARGUMENT);
        check_equal(result.machine_status, CFLOW_MACHINE_RUNTIME_OK);

        cflow_scheduler scheduler = {0};
        check_true(cflow_scheduler_worker_init(&scheduler, 1u));
        config.scheduler = &scheduler;
        result = cflow_actor_init(&actor, &config);
        check_equal(result.status, CFLOW_ACTOR_MACHINE_REJECTED);
        check_equal(result.machine_status,
                    CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT);
        check_null(actor.impl);
        cflow_scheduler_destroy(&scheduler);
    }

    it("rejects a valid manual scheduler without publishing an Actor") {
        actor_fixture fixture = {0};
        cflow_scheduler manual = {0};
        cflow_actor_config config = {0};
        cflow_actor_init_result result;

        check_true(actor_fixture_init(&fixture));
        cflow_actor_destroy(&fixture.actor);
        check_true(cflow_scheduler_test_init(&manual));
        config.machine = (cflow_machine_instance_config){
            &fixture.machine,
            &fixture.initial_state,
            &cmeta_type_long,
            NULL,
            0u,
            &fixture.action_binding,
            1u,
            1u,
            &fixture.executor};
        config.scheduler = &manual;
        result = cflow_actor_init(&fixture.actor, &config);
        check_equal(result.status, CFLOW_ACTOR_INVALID_SCHEDULER);
        check_equal(result.machine_status, CFLOW_MACHINE_RUNTIME_OK);
        check_null(fixture.actor.impl);

        cflow_scheduler_destroy(&manual);
        actor_fixture_destroy(&fixture);
    }

    it("stops directly from START and reports exact pre-start and terminal sends") {
        actor_fixture fixture;
        cflow_actor_ref ref = {0};
        cflow_actor_ref retained = {0};
        const bool payload = true;
        const cflow_event_view event = {
            100u, &cmeta_type_bool, &payload};
        cflow_actor_stats stats = {0};

        check_true(actor_fixture_init(&fixture));
        check_equal(cflow_actor_current_state(&fixture.actor),
                    CFLOW_ACTOR_STATE_START);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_true(cflow_actor_ref_retain(&ref, &retained));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_NOT_STARTED);
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_current_state(&fixture.actor),
                    CFLOW_ACTOR_STATE_STOPPED);
        check_equal(cflow_actor_wait(&fixture.actor), CFLOW_ACTOR_STATE_STOPPED);
        check_equal(cflow_actor_ref_try_send(&retained, &event),
                    CFLOW_ACTOR_SEND_STOPPED);
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_STOPPED);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_STOPPED);
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.rejected_not_started, (uint64_t)1u);
        check_equal(stats.rejected_stopped, (uint64_t)1u);

        cflow_actor_ref_release(&retained);
        cflow_actor_ref_release(&ref);
        actor_fixture_destroy(&fixture);
    }

    it("maps type mismatch and full while one accepted Event transitions once") {
        actor_fixture fixture;
        actor_blocker blocker = {0};
        cflow_actor_ref ref = {0};
        const bool payload = true;
        const int wrong_payload = 1;
        const cflow_event_view event = {
            100u, &cmeta_type_bool, &payload};
        const cflow_event_view wrong_type = {
            100u, &cmeta_type_int, &wrong_payload};
        cflow_actor_stats stats = {0};

        check_true(actor_fixture_init(&fixture));
        check_not_equal(cflow_scheduler_post(
                            &fixture.scheduler, block_scheduler, &blocker),
                        (cflow_task_id)0u);
        check_true(wait_until_true(&blocker.entered));
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_current_state(&fixture.actor),
                    CFLOW_ACTOR_STATE_RUNNING);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &wrong_type),
                    CFLOW_ACTOR_SEND_TYPE_MISMATCH);
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_FULL);

        atomic_store(&blocker.release, true);
        check_true(wait_until_at_least(&fixture.probe.values, 1));
        check_equal(atomic_load(&fixture.probe.action_calls), 1);
        check_equal(atomic_load(&fixture.probe.values), 1);
        check_equal(atomic_load(&fixture.probe.last_value), 42L);
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)1u);
        check_equal(stats.machine.completed, (uint64_t)1u);
        check_equal(stats.machine.current_state,
                    (cflow_machine_state_id)20u);
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_wait(&fixture.actor), CFLOW_ACTOR_STATE_STOPPED);
        check_equal(atomic_load(&fixture.probe.errors), 0);
        check_equal(atomic_load(&fixture.probe.dones), 1);

        cflow_actor_ref_release(&ref);
        actor_fixture_destroy(&fixture);
    }

    it("keeps STOPPING observable and reports repeated lifecycle calls exactly") {
        actor_fixture fixture;
        actor_blocker blocker = {0};
        cflow_actor_ref ref = {0};
        const bool payload = true;
        const cflow_event_view event = {
            100u, &cmeta_type_bool, &payload};

        check_true(actor_fixture_init(&fixture));
        check_not_equal(cflow_scheduler_post(
                            &fixture.scheduler, block_scheduler, &blocker),
                        (cflow_task_id)0u);
        check_true(wait_until_true(&blocker.entered));
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_start(&fixture.actor),
                    CFLOW_ACTOR_ALREADY_STARTED);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_current_state(&fixture.actor),
                    CFLOW_ACTOR_STATE_STOPPING);
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_STOPPING);
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_STOPPING);

        atomic_store(&blocker.release, true);
        check_equal(cflow_actor_wait(&fixture.actor), CFLOW_ACTOR_STATE_STOPPED);
        check_equal(cflow_actor_current_state(&fixture.actor),
                    CFLOW_ACTOR_STATE_STOPPED);
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_STOPPED);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_STOPPED);

        cflow_actor_ref_release(&ref);
        actor_fixture_destroy(&fixture);
    }
}
