#include <cflow/actor.h>

#include <turbo/thread.h>

#include "tinytest.h"

#include <stdatomic.h>
#include <string.h>

enum {
    ACTOR_EDGE_EVENT_TYPES = 2,
    ACTOR_EDGE_OBSERVATIONS = 256,
    ACTOR_TEST_TIMEOUT_MS = 5000
};

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

typedef struct actor_edge_probe {
    cflow_actor *actor;
    cflow_actor_ref *self_ref;
    actor_blocker *blocker;
    atomic_bool block_action;
    atomic_bool fail_guard;
    atomic_bool fail_action;
    atomic_bool self_send;
    atomic_bool self_sent;
    atomic_bool stop_on_value;
    atomic_bool stop_requested;
    atomic_bool reject_value;
    atomic_int action_calls;
    atomic_int values;
    atomic_int errors;
    atomic_int dones;
    atomic_int self_send_status;
    atomic_int stop_status;
    atomic_int seen[ACTOR_EDGE_OBSERVATIONS];
    int observations[ACTOR_EDGE_OBSERVATIONS];
} actor_edge_probe;

typedef struct actor_edge_fixture {
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_machine_guard_binding guard_bindings[ACTOR_EDGE_EVENT_TYPES];
    cflow_machine_action_binding action_bindings[ACTOR_EDGE_EVENT_TYPES];
    actor_edge_probe probe;
    int initial_state;
} actor_edge_fixture;

typedef struct actor_sender_context {
    const cflow_actor_ref *ref;
    cflow_event_id event_id;
    int first_payload;
    int count;
    atomic_bool *go;
    atomic_int *attempted;
    atomic_int accepted;
    atomic_int full;
    atomic_int stopping;
    atomic_int stopped;
    atomic_int failed;
    atomic_int stale;
    atomic_int unexpected;
} actor_sender_context;

static bool wait_until_true(atomic_bool *value) {
    const size_t timeout_ms = 5000u;
    for (size_t elapsed = 0u; elapsed < timeout_ms; ++elapsed) {
        if (atomic_load(value)) return true;
        turbo_sleep_ms(1u);
    }
    return atomic_load(value);
}

static bool wait_until_at_least(atomic_int *value, int expected) {
    const size_t timeout_ms = ACTOR_TEST_TIMEOUT_MS;
    for (size_t elapsed = 0u; elapsed < timeout_ms; ++elapsed) {
        if (atomic_load(value) >= expected) return true;
        turbo_sleep_ms(1u);
    }
    return atomic_load(value) >= expected;
}

static bool wait_actor_state(const cflow_actor *actor,
                             cflow_actor_state expected) {
    for (size_t elapsed = 0u; elapsed < ACTOR_TEST_TIMEOUT_MS; ++elapsed) {
        if (cflow_actor_current_state(actor) == expected) return true;
        turbo_sleep_ms(1u);
    }
    return cflow_actor_current_state(actor) == expected;
}

static bool wait_actor_terminal(const cflow_actor *actor) {
    for (size_t elapsed = 0u; elapsed < ACTOR_TEST_TIMEOUT_MS; ++elapsed) {
        const cflow_actor_state state = cflow_actor_current_state(actor);
        if (state == CFLOW_ACTOR_STATE_STOPPED ||
            state == CFLOW_ACTOR_STATE_FAILED)
            return true;
        turbo_sleep_ms(1u);
    }
    return cflow_actor_current_state(actor) == CFLOW_ACTOR_STATE_STOPPED ||
           cflow_actor_current_state(actor) == CFLOW_ACTOR_STATE_FAILED;
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

static bool actor_edge_guard(void *user,
                             const void *state,
                             const void *event,
                             bool *out_enabled,
                             const char **out_error) {
    actor_edge_probe *probe = (actor_edge_probe *)user;
    if (probe == NULL || state == NULL || event == NULL ||
        out_enabled == NULL || out_error == NULL)
        return false;
    if (atomic_load(&probe->fail_guard)) {
        *out_error = "actor edge guard failure";
        return false;
    }
    *out_enabled = true;
    *out_error = NULL;
    return true;
}

static bool actor_edge_action(void *user,
                              const void *state,
                              const void *event,
                              void *out_target_state,
                              void *out_observation,
                              const char **out_error) {
    actor_edge_probe *probe = (actor_edge_probe *)user;
    const int payload = event != NULL ? *(const int *)event : -1;
    if (probe == NULL || state == NULL || event == NULL ||
        out_target_state == NULL || out_observation == NULL ||
        out_error == NULL)
        return false;
    atomic_fetch_add(&probe->action_calls, 1);
    if (atomic_load(&probe->block_action) && probe->blocker != NULL) {
        atomic_store(&probe->blocker->entered, true);
        for (size_t elapsed = 0u;
             elapsed < ACTOR_TEST_TIMEOUT_MS &&
             !atomic_load(&probe->blocker->release);
             ++elapsed)
            turbo_sleep_ms(1u);
        if (!atomic_load(&probe->blocker->release)) {
            *out_error = "actor edge action timed out";
            return false;
        }
    }
    if (atomic_load(&probe->fail_action)) {
        *out_error = "actor edge action failure";
        return false;
    }
    if (atomic_load(&probe->self_send) && payload == 1 &&
        !atomic_exchange(&probe->self_sent, true)) {
        const int nested_payload = 2;
        const cflow_event_view nested = {
            101u, &cmeta_type_int, &nested_payload};
        atomic_store(&probe->self_send_status,
                     (int)cflow_actor_ref_try_send(probe->self_ref, &nested));
    }
    *(int *)out_target_state = *(const int *)state + 1;
    *(int *)out_observation = payload;
    *out_error = NULL;
    return true;
}

static bool actor_edge_on_value(void *user,
                                const cmeta_type_desc *type,
                                const void *value) {
    actor_edge_probe *probe = (actor_edge_probe *)user;
    int index;
    int payload;
    if (probe == NULL || !cmeta_type_equal(type, &cmeta_type_int) ||
        value == NULL)
        return false;
    payload = *(const int *)value;
    index = atomic_load(&probe->values);
    if (index >= 0 && index < ACTOR_EDGE_OBSERVATIONS)
        probe->observations[index] = payload;
    if (payload >= 0 && payload < ACTOR_EDGE_OBSERVATIONS)
        atomic_fetch_add(&probe->seen[payload], 1);
    atomic_fetch_add(&probe->values, 1);
    if (atomic_load(&probe->stop_on_value) &&
        !atomic_exchange(&probe->stop_requested, true)) {
        atomic_store(&probe->stop_status,
                     (int)cflow_actor_request_stop(probe->actor));
    }
    return !atomic_load(&probe->reject_value);
}

static void actor_edge_on_error(void *user, const char *message) {
    actor_edge_probe *probe = (actor_edge_probe *)user;
    if (probe != NULL && message != NULL)
        atomic_fetch_add(&probe->errors, 1);
}

static void actor_edge_on_done(void *user) {
    actor_edge_probe *probe = (actor_edge_probe *)user;
    if (probe != NULL) atomic_fetch_add(&probe->dones, 1);
}

static bool actor_edge_fixture_init(actor_edge_fixture *fixture,
                                    size_t mailbox_capacity) {
    cflow_machine_state states[1];
    cflow_event_type events[ACTOR_EDGE_EVENT_TYPES];
    cflow_machine_guard guards[ACTOR_EDGE_EVENT_TYPES];
    cflow_machine_action actions[ACTOR_EDGE_EVENT_TYPES];
    cflow_machine_transition transitions[ACTOR_EDGE_EVENT_TYPES];
    cflow_machine_definition definition;
    cflow_actor_config config = {0};
    size_t index;

    *fixture = (actor_edge_fixture){0};
    fixture->initial_state = 0;
    states[0] = (cflow_machine_state){
        10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE};
    for (index = 0u; index < ACTOR_EDGE_EVENT_TYPES; ++index) {
        const cflow_event_id event_id = (cflow_event_id)(100u + index);
        const cflow_machine_guard_id guard_id =
            (cflow_machine_guard_id)(200u + index);
        const cflow_machine_action_id action_id =
            (cflow_machine_action_id)(300u + index);
        events[index] = (cflow_event_type){event_id, &cmeta_type_int};
        guards[index] = (cflow_machine_guard){
            guard_id, &cmeta_type_int, event_id, &cmeta_type_int,
            CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        actions[index] = (cflow_machine_action){
            action_id, &cmeta_type_int, event_id, &cmeta_type_int,
            &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
            CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u};
        transitions[index] = (cflow_machine_transition){
            10u, event_id, guard_id, action_id, 10u, 1u};
        fixture->guard_bindings[index] = (cflow_machine_guard_binding){
            guard_id, actor_edge_guard, &fixture->probe};
        fixture->action_bindings[index] = (cflow_machine_action_binding){
            action_id, actor_edge_action, &fixture->probe};
    }
    definition = (cflow_machine_definition){
        states, 1u, 10u,
        events, ACTOR_EDGE_EVENT_TYPES,
        guards, ACTOR_EDGE_EVENT_TYPES,
        actions, ACTOR_EDGE_EVENT_TYPES,
        transitions, ACTOR_EDGE_EVENT_TYPES};
    if (cflow_machine_build(&fixture->machine, &definition) !=
        CFLOW_MACHINE_OK)
        return false;
    if (!cflow_executor_serial_init(&fixture->executor)) return false;
    if (!cflow_scheduler_worker_init(&fixture->scheduler, 1u)) return false;
    fixture->probe.actor = &fixture->actor;
    atomic_init(&fixture->probe.self_send_status,
                (int)CFLOW_ACTOR_SEND_INVALID_ARGUMENT);
    atomic_init(&fixture->probe.stop_status, (int)CFLOW_ACTOR_FAILED);
    config.machine = (cflow_machine_instance_config){
        &fixture->machine,
        &fixture->initial_state,
        &cmeta_type_int,
        fixture->guard_bindings,
        ACTOR_EDGE_EVENT_TYPES,
        fixture->action_bindings,
        ACTOR_EDGE_EVENT_TYPES,
        mailbox_capacity,
        &fixture->executor};
    config.scheduler = &fixture->scheduler;
    config.callbacks = (cflow_sink_callbacks){
        actor_edge_on_value,
        actor_edge_on_error,
        actor_edge_on_done,
        &fixture->probe};
    return cflow_actor_init(&fixture->actor, &config).status == CFLOW_ACTOR_OK;
}

static void actor_edge_fixture_destroy(actor_edge_fixture *fixture) {
    cflow_actor_destroy(&fixture->actor);
    if (cflow_scheduler_valid(&fixture->scheduler))
        cflow_scheduler_destroy(&fixture->scheduler);
    if (cflow_executor_valid(&fixture->executor))
        cflow_executor_destroy(&fixture->executor);
    cflow_machine_destroy(&fixture->machine);
}

static void actor_sender(void *user) {
    actor_sender_context *context = (actor_sender_context *)user;
    int index;
    if (context == NULL) return;
    for (size_t elapsed = 0u;
         context->go != NULL && !atomic_load(context->go) &&
         elapsed < ACTOR_TEST_TIMEOUT_MS;
         ++elapsed)
        turbo_thread_yield();
    if (context->go != NULL && !atomic_load(context->go)) {
        atomic_fetch_add(&context->unexpected, 1);
        return;
    }
    for (index = 0; index < context->count; ++index) {
        const int payload = context->first_payload + index;
        const cflow_event_view event = {
            context->event_id, &cmeta_type_int, &payload};
        const cflow_actor_send_status status =
            cflow_actor_ref_try_send(context->ref, &event);
        if (context->attempted != NULL)
            atomic_fetch_add(context->attempted, 1);
        switch (status) {
            case CFLOW_ACTOR_SEND_ACCEPTED:
                atomic_fetch_add(&context->accepted, 1);
                break;
            case CFLOW_ACTOR_SEND_FULL:
                atomic_fetch_add(&context->full, 1);
                break;
            case CFLOW_ACTOR_SEND_STOPPING:
                atomic_fetch_add(&context->stopping, 1);
                break;
            case CFLOW_ACTOR_SEND_STOPPED:
                atomic_fetch_add(&context->stopped, 1);
                break;
            case CFLOW_ACTOR_SEND_FAILED:
                atomic_fetch_add(&context->failed, 1);
                break;
            case CFLOW_ACTOR_SEND_STALE:
                atomic_fetch_add(&context->stale, 1);
                break;
            default:
                atomic_fetch_add(&context->unexpected, 1);
                break;
        }
    }
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
    cflow_machine_transition *transitions,
    cflow_machine_state_kind target_kind) {
    states[0] = (cflow_machine_state){
        10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE};
    states[1] = (cflow_machine_state){
        20u, &cmeta_type_long, target_kind};
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

static bool actor_fixture_init_with_target_kind(
    actor_fixture *fixture, cflow_machine_state_kind target_kind) {
    cflow_machine_state states[2];
    cflow_event_type events[1];
    cflow_machine_action actions[1];
    cflow_machine_transition transitions[1];
    const cflow_machine_definition definition = actor_definition(
        states, events, actions, transitions, target_kind);
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

static bool actor_fixture_init(actor_fixture *fixture) {
    return actor_fixture_init_with_target_kind(
        fixture, CFLOW_MACHINE_STATE_ACTIVE);
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

    it("fails when Run reports done while Actor is RUNNING") {
        actor_fixture fixture;
        cflow_actor_ref ref = {0};
        const bool payload = true;
        const cflow_event_view event = {
            100u, &cmeta_type_bool, &payload};
        const char *first_error;

        check_true(actor_fixture_init_with_target_kind(
            &fixture, CFLOW_MACHINE_STATE_DONE));
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(cflow_actor_wait(&fixture.actor), CFLOW_ACTOR_STATE_FAILED);
        first_error = cflow_actor_error(&fixture.actor);
        check_not_null(first_error);
        check_contains(first_error, "RUNNING");
        check_true(wait_until_at_least(&fixture.probe.errors, 1));
        check_equal(atomic_load(&fixture.probe.errors), 1);
        check_equal(atomic_load(&fixture.probe.dones), 0);
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_FAILED);
        check_true(cflow_actor_error(&fixture.actor) == first_error);

        cflow_actor_ref_release(&ref);
        actor_fixture_destroy(&fixture);
    }

    it("returns STALE before validating an Event after owner destruction") {
        actor_fixture fixture;
        cflow_actor_ref ref = {0};
        const cflow_event_view malformed = {0};

        check_true(actor_fixture_init(&fixture));
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        cflow_actor_destroy(&fixture.actor);
        check_equal(cflow_actor_ref_try_send(&ref, NULL),
                    CFLOW_ACTOR_SEND_STALE);
        check_equal(cflow_actor_ref_try_send(&ref, &malformed),
                    CFLOW_ACTOR_SEND_STALE);

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

    it("bounds saturation while one transition is blocked") {
        actor_edge_fixture fixture;
        actor_blocker blocker = {0};
        cflow_actor_ref ref = {0};
        const int payloads[] = {1, 2, 3};
        const cflow_event_view events[] = {
            {100u, &cmeta_type_int, &payloads[0]},
            {100u, &cmeta_type_int, &payloads[1]},
            {100u, &cmeta_type_int, &payloads[2]}
        };
        cflow_actor_stats stats = {0};

        check_true(actor_edge_fixture_init(&fixture, 1u));
        fixture.probe.blocker = &blocker;
        atomic_store(&fixture.probe.block_action, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &events[0]),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_until_true(&blocker.entered));
        check_equal(cflow_actor_ref_try_send(&ref, &events[1]),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(cflow_actor_ref_try_send(&ref, &events[2]),
                    CFLOW_ACTOR_SEND_FULL);

        atomic_store(&blocker.release, true);
        check_true(wait_until_at_least(&fixture.probe.values, 2));
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)2u);
        check_equal(stats.machine.completed, (uint64_t)2u);
        check_equal(stats.machine.cancelled_events, (uint64_t)0u);
        check_equal(stats.machine.pending, (size_t)0u);
        check_equal(stats.machine.in_flight, (size_t)0u);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("accounts for unique payload sequences from concurrent retained refs") {
        enum {
            PRODUCERS = 4,
            EVENTS_PER_PRODUCER = 8,
            TOTAL_EVENTS = PRODUCERS * EVENTS_PER_PRODUCER
        };
        actor_edge_fixture fixture;
        cflow_actor_ref refs[PRODUCERS] = {0};
        actor_sender_context contexts[PRODUCERS] = {0};
        turbo_thread_t threads[PRODUCERS] = {0};
        atomic_bool go = false;
        cflow_actor_stats stats = {0};
        int producer;
        int payload;

        check_true(actor_edge_fixture_init(&fixture, TOTAL_EVENTS));
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &refs[0]));
        for (producer = 1; producer < PRODUCERS; ++producer)
            check_true(cflow_actor_ref_retain(&refs[0], &refs[producer]));
        for (producer = 0; producer < PRODUCERS; ++producer) {
            contexts[producer].ref = &refs[producer];
            contexts[producer].event_id =
                (cflow_event_id)(100u + (unsigned)(producer % 2));
            contexts[producer].first_payload =
                producer * EVENTS_PER_PRODUCER;
            contexts[producer].count = EVENTS_PER_PRODUCER;
            contexts[producer].go = &go;
            check_equal(turbo_thread_create(
                            &threads[producer], actor_sender,
                            &contexts[producer]),
                        0);
        }
        atomic_store(&go, true);
        for (producer = 0; producer < PRODUCERS; ++producer)
            check_equal(turbo_thread_join(&threads[producer]), 0);
        check_true(wait_until_at_least(
            &fixture.probe.values, TOTAL_EVENTS));
        for (producer = 0; producer < PRODUCERS; ++producer) {
            check_equal(atomic_load(&contexts[producer].accepted),
                        EVENTS_PER_PRODUCER);
            check_equal(atomic_load(&contexts[producer].unexpected), 0);
        }
        for (payload = 0; payload < TOTAL_EVENTS; ++payload)
            check_equal(atomic_load(&fixture.probe.seen[payload]), 1);
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)TOTAL_EVENTS);
        check_equal(stats.machine.completed, (uint64_t)TOTAL_EVENTS);
        check_equal(stats.machine.cancelled_events, (uint64_t)0u);
        check_equal(atomic_load(&fixture.probe.action_calls), TOTAL_EVENTS);
        check_equal(atomic_load(&fixture.probe.dones), 1);

        for (producer = 0; producer < PRODUCERS; ++producer)
            cflow_actor_ref_release(&refs[producer]);
        actor_edge_fixture_destroy(&fixture);
    }

    it("admits a self-send from a Machine action in FIFO order") {
        actor_edge_fixture fixture;
        cflow_actor_ref ref = {0};
        const int payload = 1;
        const cflow_event_view event = {
            100u, &cmeta_type_int, &payload};
        cflow_actor_stats stats = {0};

        check_true(actor_edge_fixture_init(&fixture, 1u));
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        fixture.probe.self_ref = &ref;
        atomic_store(&fixture.probe.self_send, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_until_at_least(&fixture.probe.values, 2));
        check_equal(atomic_load(&fixture.probe.self_send_status),
                    (int)CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(fixture.probe.observations[0], 1);
        check_equal(fixture.probe.observations[1], 2);
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)2u);
        check_equal(stats.machine.completed, (uint64_t)2u);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("permits request_stop from an Actor sink callback") {
        actor_edge_fixture fixture;
        cflow_actor_ref ref = {0};
        const int payload = 9;
        const cflow_event_view event = {
            100u, &cmeta_type_int, &payload};
        cflow_actor_stats stats = {0};

        check_true(actor_edge_fixture_init(&fixture, 2u));
        atomic_store(&fixture.probe.stop_on_value, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(atomic_load(&fixture.probe.stop_requested));
        check_equal(atomic_load(&fixture.probe.stop_status),
                    (int)CFLOW_ACTOR_OK);
        check_equal(atomic_load(&fixture.probe.values), 1);
        check_equal(atomic_load(&fixture.probe.errors), 0);
        check_equal(atomic_load(&fixture.probe.dones), 1);
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)1u);
        check_equal(stats.machine.completed, (uint64_t)1u);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("stops with queued Events and commits exactly one in-flight transition") {
        actor_edge_fixture fixture;
        actor_blocker blocker = {0};
        cflow_actor_ref ref = {0};
        const int payloads[] = {11, 12, 13};
        const cflow_event_view events[] = {
            {100u, &cmeta_type_int, &payloads[0]},
            {100u, &cmeta_type_int, &payloads[1]},
            {100u, &cmeta_type_int, &payloads[2]}
        };
        cflow_actor_stats stats = {0};
        int index;

        check_true(actor_edge_fixture_init(&fixture, 3u));
        fixture.probe.blocker = &blocker;
        atomic_store(&fixture.probe.block_action, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &events[0]),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_until_true(&blocker.entered));
        for (index = 1; index < 3; ++index)
            check_equal(cflow_actor_ref_try_send(&ref, &events[index]),
                        CFLOW_ACTOR_SEND_ACCEPTED);
        check_equal(cflow_actor_request_stop(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.state, CFLOW_ACTOR_STATE_STOPPING);
        check_equal(stats.machine.accepted, (uint64_t)3u);
        check_equal(stats.machine.cancelled_events, (uint64_t)2u);
        check_equal(stats.machine.in_flight, (size_t)1u);
        check_equal(stats.machine.pending, (size_t)0u);

        atomic_store(&blocker.release, true);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)3u);
        check_equal(stats.machine.completed, (uint64_t)1u);
        check_equal(stats.machine.cancelled_events, (uint64_t)2u);
        check_equal(stats.machine.in_flight, (size_t)0u);
        check_equal(stats.machine.accepted,
                    stats.machine.completed + stats.machine.cancelled_events);
        check_equal(atomic_load(&fixture.probe.values), 1);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("fails an unhandled Event and preserves the first owned error") {
        actor_fixture fixture;
        cflow_actor_ref ref = {0};
        const bool payload = true;
        const cflow_event_view event = {
            100u, &cmeta_type_bool, &payload};
        cflow_actor_stats stats = {0};
        const char *first_error;

        check_true(actor_fixture_init(&fixture));
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_until_at_least(&fixture.probe.values, 1));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_FAILED));
        first_error = cflow_actor_error(&fixture.actor);
        check_not_null(first_error);
        check_contains(first_error, "no enabled transition");
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_FAILED);
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_FAILED);
        check_true(cflow_actor_error(&fixture.actor) == first_error);
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)2u);
        check_equal(stats.machine.completed, (uint64_t)1u);
        check_equal(stats.machine.failed, (uint64_t)1u);
        check_equal(stats.machine.cancelled_events, (uint64_t)0u);
        check_equal(stats.rejected_failed, (uint64_t)1u);
        check_equal(atomic_load(&fixture.probe.errors), 1);

        cflow_actor_ref_release(&ref);
        actor_fixture_destroy(&fixture);
    }

    it("fails on a guard error without committing the Event") {
        actor_edge_fixture fixture;
        cflow_actor_ref ref = {0};
        const int payload = 21;
        const cflow_event_view event = {
            100u, &cmeta_type_int, &payload};
        cflow_actor_stats stats = {0};

        check_true(actor_edge_fixture_init(&fixture, 2u));
        atomic_store(&fixture.probe.fail_guard, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_FAILED));
        check_contains(cflow_actor_error(&fixture.actor),
                       "actor edge guard failure");
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)1u);
        check_equal(stats.machine.completed, (uint64_t)0u);
        check_equal(stats.machine.failed, (uint64_t)1u);
        check_equal(stats.machine.cancelled_events, (uint64_t)0u);
        check_equal(atomic_load(&fixture.probe.action_calls), 0);
        check_equal(atomic_load(&fixture.probe.errors), 1);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("fails on an action error without committing the Event") {
        actor_edge_fixture fixture;
        cflow_actor_ref ref = {0};
        const int payload = 22;
        const cflow_event_view event = {
            100u, &cmeta_type_int, &payload};
        cflow_actor_stats stats = {0};

        check_true(actor_edge_fixture_init(&fixture, 2u));
        atomic_store(&fixture.probe.fail_action, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_FAILED));
        check_contains(cflow_actor_error(&fixture.actor),
                       "actor edge action failure");
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)1u);
        check_equal(stats.machine.completed, (uint64_t)0u);
        check_equal(stats.machine.failed, (uint64_t)1u);
        check_equal(stats.machine.cancelled_events, (uint64_t)0u);
        check_equal(atomic_load(&fixture.probe.action_calls), 1);
        check_equal(atomic_load(&fixture.probe.errors), 1);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("fails when the sink rejects a committed value") {
        actor_edge_fixture fixture;
        cflow_actor_ref ref = {0};
        const int payload = 23;
        const cflow_event_view event = {
            100u, &cmeta_type_int, &payload};
        cflow_actor_stats stats = {0};
        const char *first_error;

        check_true(actor_edge_fixture_init(&fixture, 2u));
        atomic_store(&fixture.probe.reject_value, true);
        check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&fixture.actor, &ref));
        check_equal(cflow_actor_ref_try_send(&ref, &event),
                    CFLOW_ACTOR_SEND_ACCEPTED);
        check_true(wait_actor_state(
            &fixture.actor, CFLOW_ACTOR_STATE_FAILED));
        first_error = cflow_actor_error(&fixture.actor);
        check_not_null(first_error);
        check_contains(first_error, "observer rejected value");
        check_true(cflow_actor_get_stats(&fixture.actor, &stats));
        check_equal(stats.machine.accepted, (uint64_t)1u);
        check_equal(stats.machine.completed, (uint64_t)1u);
        check_equal(stats.machine.failed, (uint64_t)0u);
        check_equal(stats.machine.cancelled_events, (uint64_t)0u);
        check_equal(atomic_load(&fixture.probe.values), 1);
        check_equal(atomic_load(&fixture.probe.errors), 1);
        check_equal(atomic_load(&fixture.probe.dones), 0);
        check_equal(cflow_actor_request_stop(&fixture.actor),
                    CFLOW_ACTOR_FAILED);
        check_true(cflow_actor_error(&fixture.actor) == first_error);

        cflow_actor_ref_release(&ref);
        actor_edge_fixture_destroy(&fixture);
    }

    it("keeps retained refs stale until each shell reference is released") {
        actor_edge_fixture fixture;
        cflow_actor_ref first = {0};
        cflow_actor_ref second = {0};
        cflow_actor_ref third = {0};
        const int payload = 31;
        const cflow_event_view event = {
            100u, &cmeta_type_int, &payload};

        check_true(actor_edge_fixture_init(&fixture, 2u));
        check_true(cflow_actor_ref_acquire(&fixture.actor, &first));
        check_true(cflow_actor_ref_retain(&first, &second));
        cflow_actor_destroy(&fixture.actor);
        check_null(fixture.actor.impl);
        check_true(cflow_actor_ref_retain(&second, &third));
        check_equal(cflow_actor_ref_try_send(&first, &event),
                    CFLOW_ACTOR_SEND_STALE);
        check_equal(cflow_actor_ref_try_send(&second, &event),
                    CFLOW_ACTOR_SEND_STALE);
        cflow_actor_ref_release(&first);
        check_null(first.impl);
        check_equal(cflow_actor_ref_try_send(&third, &event),
                    CFLOW_ACTOR_SEND_STALE);
        cflow_actor_ref_release(&second);
        check_null(second.impl);
        cflow_actor_ref_release(&third);
        check_null(third.impl);
        cflow_actor_ref_release(&third);

        actor_edge_fixture_destroy(&fixture);
    }

    it("replays the same Event sequence deterministically across two Actors") {
        enum { REPLAY_COUNT = 6 };
        const int sequence[REPLAY_COUNT] = {4, 1, 7, 3, 9, 2};
        actor_edge_fixture first;
        actor_edge_fixture second;
        cflow_actor_ref first_ref = {0};
        cflow_actor_ref second_ref = {0};
        cflow_actor_stats first_stats = {0};
        cflow_actor_stats second_stats = {0};
        int index;

        check_true(actor_edge_fixture_init(&first, REPLAY_COUNT));
        check_true(actor_edge_fixture_init(&second, REPLAY_COUNT));
        check_equal(cflow_actor_start(&first.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_start(&second.actor), CFLOW_ACTOR_OK);
        check_true(cflow_actor_ref_acquire(&first.actor, &first_ref));
        check_true(cflow_actor_ref_acquire(&second.actor, &second_ref));
        for (index = 0; index < REPLAY_COUNT; ++index) {
            const cflow_event_view first_event = {
                (cflow_event_id)(100u + (unsigned)(index % 2)),
                &cmeta_type_int,
                &sequence[index]};
            const cflow_event_view second_event = first_event;
            check_equal(cflow_actor_ref_try_send(&first_ref, &first_event),
                        CFLOW_ACTOR_SEND_ACCEPTED);
            check_equal(cflow_actor_ref_try_send(&second_ref, &second_event),
                        CFLOW_ACTOR_SEND_ACCEPTED);
        }
        check_true(wait_until_at_least(&first.probe.values, REPLAY_COUNT));
        check_true(wait_until_at_least(&second.probe.values, REPLAY_COUNT));
        for (index = 0; index < REPLAY_COUNT; ++index) {
            check_equal(first.probe.observations[index], sequence[index]);
            check_equal(second.probe.observations[index], sequence[index]);
        }
        check_equal(cflow_actor_request_stop(&first.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_request_stop(&second.actor), CFLOW_ACTOR_OK);
        check_true(wait_actor_state(&first.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(wait_actor_state(&second.actor, CFLOW_ACTOR_STATE_STOPPED));
        check_true(cflow_actor_get_stats(&first.actor, &first_stats));
        check_true(cflow_actor_get_stats(&second.actor, &second_stats));
        check_equal(first_stats.machine.accepted, (uint64_t)REPLAY_COUNT);
        check_equal(second_stats.machine.accepted, (uint64_t)REPLAY_COUNT);
        check_equal(first_stats.machine.completed, (uint64_t)REPLAY_COUNT);
        check_equal(second_stats.machine.completed, (uint64_t)REPLAY_COUNT);
        check_equal(first_stats.machine.current_state,
                    second_stats.machine.current_state);

        cflow_actor_ref_release(&first_ref);
        cflow_actor_ref_release(&second_ref);
        actor_edge_fixture_destroy(&first);
        actor_edge_fixture_destroy(&second);
    }

    it("classifies a bounded retained-ref send race against stop") {
        enum {
            STRESS_ITERATIONS = 20,
            STRESS_PRODUCERS = 4,
            STRESS_SENDS_PER_PRODUCER = 32,
            STRESS_SENDS = STRESS_PRODUCERS * STRESS_SENDS_PER_PRODUCER
        };
        int iteration;

        for (iteration = 0; iteration < STRESS_ITERATIONS; ++iteration) {
            actor_edge_fixture fixture;
            actor_blocker blocker = {0};
            cflow_actor_ref refs[STRESS_PRODUCERS] = {0};
            actor_sender_context contexts[STRESS_PRODUCERS] = {0};
            turbo_thread_t threads[STRESS_PRODUCERS] = {0};
            atomic_bool go = false;
            atomic_int attempted = 0;
            cflow_actor_stats stats = {0};
            const int seed_payload = 200;
            const cflow_event_view seed = {
                100u, &cmeta_type_int, &seed_payload};
            uint64_t classified_accepted = 1u;
            uint64_t classified_stopping = 0u;
            uint64_t classified_stopped = 0u;
            int producer;

            check_true(actor_edge_fixture_init(&fixture, 8u));
            fixture.probe.blocker = &blocker;
            atomic_store(&fixture.probe.block_action, true);
            check_equal(cflow_actor_start(&fixture.actor), CFLOW_ACTOR_OK);
            check_true(cflow_actor_ref_acquire(&fixture.actor, &refs[0]));
            for (producer = 1; producer < STRESS_PRODUCERS; ++producer)
                check_true(cflow_actor_ref_retain(
                    &refs[0], &refs[producer]));
            check_equal(cflow_actor_ref_try_send(&refs[0], &seed),
                        CFLOW_ACTOR_SEND_ACCEPTED);
            check_true(wait_until_true(&blocker.entered));
            for (producer = 0; producer < STRESS_PRODUCERS; ++producer) {
                contexts[producer].ref = &refs[producer];
                contexts[producer].event_id =
                    (cflow_event_id)(100u + (unsigned)(producer % 2));
                contexts[producer].first_payload =
                    producer * STRESS_SENDS_PER_PRODUCER;
                contexts[producer].count = STRESS_SENDS_PER_PRODUCER;
                contexts[producer].go = &go;
                contexts[producer].attempted = &attempted;
                check_equal(turbo_thread_create(
                                &threads[producer], actor_sender,
                                &contexts[producer]),
                            0);
            }
            atomic_store(&go, true);
            check_equal(cflow_actor_request_stop(&fixture.actor),
                        CFLOW_ACTOR_OK);
            atomic_store(&blocker.release, true);
            for (producer = 0; producer < STRESS_PRODUCERS; ++producer) {
                check_equal(turbo_thread_join(&threads[producer]), 0);
                classified_accepted += (uint64_t)atomic_load(
                    &contexts[producer].accepted);
                classified_stopping += (uint64_t)atomic_load(
                    &contexts[producer].stopping);
                classified_stopped += (uint64_t)atomic_load(
                    &contexts[producer].stopped);
                check_equal(atomic_load(&contexts[producer].failed), 0);
                check_equal(atomic_load(&contexts[producer].stale), 0);
                check_equal(atomic_load(&contexts[producer].unexpected), 0);
            }
            check_equal(atomic_load(&attempted), STRESS_SENDS);
            check_true(wait_actor_terminal(&fixture.actor));
            check_equal(cflow_actor_current_state(&fixture.actor),
                        CFLOW_ACTOR_STATE_STOPPED);
            check_true(cflow_actor_get_stats(&fixture.actor, &stats));
            check_equal(stats.machine.accepted, classified_accepted);
            check_equal(stats.machine.accepted,
                        stats.machine.completed +
                            stats.machine.cancelled_events);
            check_equal(stats.machine.failed, (uint64_t)0u);
            check_equal(stats.machine.in_flight, (size_t)0u);
            check_equal(stats.machine.pending, (size_t)0u);
            check_equal(stats.rejected_stopping, classified_stopping);
            check_equal(stats.rejected_stopped, classified_stopped);
            check_equal(stats.rejected_failed, (uint64_t)0u);
            check_equal(classified_accepted +
                            classified_stopping +
                            classified_stopped +
                            (uint64_t)(
                                atomic_load(&contexts[0].full) +
                                atomic_load(&contexts[1].full) +
                                atomic_load(&contexts[2].full) +
                                atomic_load(&contexts[3].full)),
                        (uint64_t)STRESS_SENDS + 1u);

            for (producer = 0; producer < STRESS_PRODUCERS; ++producer)
                cflow_actor_ref_release(&refs[producer]);
            actor_edge_fixture_destroy(&fixture);
        }
    }
}
