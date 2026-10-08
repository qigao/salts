#include <cflow/actor.h>
#include <cmeta/interface.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * A separately configured installed-SDK consumer, intentionally not including
 * any in-tree CFlow test headers or linking CMeta's private implementation.
 */
#define INSTALLED_PORT_METHODS(X, I) \
    X(I,FR1,int,try_send,stateful, \
      &cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (int,payload,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR))
#define INSTALLED_STRATEGY_METHODS(X, I) \
    X(I,FR1,int,transform,stateful, \
      &cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (int,payload,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR))

CMETA_INTERFACE(installed_actor_port, INSTALLED_PORT_METHODS);
CMETA_INTERFACE(installed_actor_strategy, INSTALLED_STRATEGY_METHODS);

typedef struct installed_probe {
    atomic_int action_calls;
    atomic_int strategy_calls;
    atomic_int values;
    atomic_int errors;
    atomic_int last_value;
} installed_probe;

typedef struct installed_action_context {
    installed_actor_strategy *strategy; /* borrowed through Actor destroy */
} installed_action_context;

typedef struct installed_sender {
    cflow_actor_ref *ref; /* borrowed, not a second owner */
} installed_sender;

static int installed_transform(void *self, int payload) {
    installed_probe *probe = (installed_probe *)self;
    atomic_fetch_add(&probe->strategy_calls, 1);
    return payload + 100;
}

static int installed_send(void *self, int payload) {
    const installed_sender *sender = (const installed_sender *)self;
    const cflow_event_view event = {100u, &cmeta_type_int, &payload};
    return (int)cflow_actor_ref_try_send(sender->ref, &event);
}

CMETA_IMPLEMENTS(installed_actor_port, installed_port_impl, 0u,
    .try_send = installed_send);
CMETA_IMPLEMENTS(installed_actor_strategy, installed_strategy_impl, 0u,
    .transform = installed_transform);

static bool installed_action(void *user, const void *state, const void *event,
    void *out_state, void *out_observation, const char **out_error) {
    installed_action_context *context = (installed_action_context *)user;
    if (state == NULL || event == NULL || out_state == NULL ||
        out_observation == NULL || out_error == NULL || context == NULL ||
        !installed_actor_strategy_valid(context->strategy))
        return false;
    *(int *)out_state = *(const int *)state + 1;
    *(int *)out_observation =
        installed_actor_strategy_transform(context->strategy,
                                           *(const int *)event);
    *out_error = NULL;
    return true;
}

static bool installed_on_value(void *user, const cmeta_type_desc *type,
    const void *value) {
    installed_probe *probe = (installed_probe *)user;
    if (probe == NULL || value == NULL ||
        !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    atomic_store(&probe->last_value, *(const int *)value);
    atomic_fetch_add(&probe->action_calls, 1);
    atomic_fetch_add(&probe->values, 1);
    return true;
}

static void installed_on_error(void *user, const char *message) {
    installed_probe *probe = (installed_probe *)user;
    if (probe != NULL && message != NULL)
        atomic_fetch_add(&probe->errors, 1);
}
static void installed_on_done(void *user) { (void)user; }

int main(void) {
    cflow_machine machine = {0};
    cflow_executor executor = {0};
    cflow_scheduler scheduler = {0};
    cflow_actor actor = {0};
    cflow_actor_ref producer = {0};
    installed_probe probe = {0};
    int initial_state = 0;
    int result = 0;
    const cflow_machine_state states[1] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[1] = {
        {100u, &cmeta_type_int}
    };
    const cflow_machine_action actions[1] = {
        {300u, &cmeta_type_int, 100u, &cmeta_type_int,
         &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
         CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
         CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u}
    };
    const cflow_machine_transition transitions[1] = {
        {10u, 100u, 0u, 300u, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u,
        events, 1u,
        NULL, 0u,
        actions, 1u,
        transitions, 1u
    };
    installed_actor_strategy strategy =
        installed_strategy_impl_as_installed_actor_strategy(&probe);
    installed_action_context action_context = {&strategy};
    cflow_machine_action_binding bindings[1] = {
        {300u, installed_action, &action_context}
    };
    installed_sender sender = {&producer};
    installed_actor_port port =
        installed_port_impl_as_installed_actor_port(&sender);
    cflow_actor_config config = {0};
    uint64_t deadline;

    if (!cmeta_interface_desc_valid(installed_actor_port_interface()) ||
        !cmeta_interface_desc_valid(installed_actor_strategy_interface()) ||
        installed_actor_port_interface()->methods[0].abi == NULL ||
        !installed_actor_port_valid(&port) ||
        !installed_actor_strategy_valid(&strategy)) {
        result = 1; goto finish;
    }
    if (cflow_machine_build(&machine, &definition) != CFLOW_MACHINE_OK) {
        result = 2; goto finish;
    }
    if (!cflow_executor_serial_init(&executor)) {
        result = 3; goto finish;
    }
    if (!cflow_scheduler_worker_init(&scheduler, 1u)) {
        result = 4; goto finish;
    }

    config.machine = (cflow_machine_instance_config) {
        &machine, &initial_state, &cmeta_type_int,
        NULL, 0u,
        bindings, 1u,
        2u, &executor
    };
    config.scheduler = &scheduler;
    config.callbacks = (cflow_subscriber_callbacks) {
        installed_on_value, installed_on_error, installed_on_done, &probe
    };
    if (cflow_actor_init(&actor, &config).status != CFLOW_ACTOR_OK) {
        result = 5; goto finish;
    }
    if (cflow_actor_start(&actor) != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&actor, &producer)) {
        result = 6; goto finish;
    }
    if (installed_actor_port_try_send(&port, 7) !=
        (int)CFLOW_ACTOR_SEND_ACCEPTED) {
        result = 7; goto finish;
    }

    deadline = cmeta_monotonic_ms() + UINT64_C(5000);
    while (atomic_load(&probe.values) == 0 &&
           cmeta_monotonic_ms() < deadline)
        cmeta_sleep_ms(1u);
    if (atomic_load(&probe.values) != 1 ||
        atomic_load(&probe.action_calls) != 1 ||
        atomic_load(&probe.strategy_calls) != 1 ||
        atomic_load(&probe.last_value) != 107 ||
        atomic_load(&probe.errors) != 0) {
        result = 8; goto finish;
    }

    if (cflow_actor_request_stop(&actor) != CFLOW_ACTOR_OK ||
        cflow_actor_wait(&actor) != CFLOW_ACTOR_STATE_STOPPED ||
        installed_actor_port_try_send(&port, 8) !=
            (int)CFLOW_ACTOR_SEND_STOPPED) {
        result = 9; goto finish;
    }

finish:
    cflow_actor_destroy(&actor);
    if (producer.impl != NULL) {
        if (result == 0 &&
            installed_actor_port_try_send(&port, 9) !=
                (int)CFLOW_ACTOR_SEND_STALE)
            result = 10;
        cflow_actor_ref_release(&producer);
    }
    if (cflow_scheduler_valid(&scheduler))
        cflow_scheduler_destroy(&scheduler);
    if (cflow_executor_valid(&executor))
        cflow_executor_destroy(&executor);
    cflow_machine_destroy(&machine);
    return result;
}
