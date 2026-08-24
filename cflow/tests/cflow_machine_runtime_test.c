#include <cflow/machine_runtime.h>

#include "tinytest.h"

static bool guard_enabled(void *user,
                          const void *state,
                          const void *event,
                          bool *out_enabled,
                          const char **out_error) {
    (void)user;
    (void)state;
    (void)event;
    if (out_enabled == NULL || out_error == NULL) return false;
    *out_enabled = true;
    *out_error = NULL;
    return true;
}

static bool action_to_long(void *user,
                           const void *state,
                           const void *event,
                           void *out_target_state,
                           void *out_observation,
                           const char **out_error) {
    (void)user;
    (void)event;
    if (state == NULL || out_target_state == NULL ||
        out_observation == NULL || out_error == NULL)
        return false;
    *(long *)out_target_state = (long)*(const int *)state + 1L;
    *(long *)out_observation = 70L;
    *out_error = NULL;
    return true;
}

static cflow_machine_definition runtime_definition(
    cflow_machine_state *states,
    cflow_event_type *events,
    cflow_machine_guard *guards,
    cflow_machine_action *actions,
    cflow_machine_transition *transitions) {
    states[0] = (cflow_machine_state){
        10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE};
    states[1] = (cflow_machine_state){
        20u, &cmeta_type_long, CFLOW_MACHINE_STATE_DONE};
    events[0] = (cflow_event_type){100u, &cmeta_type_bool};
    guards[0] = (cflow_machine_guard){
        200u, &cmeta_type_int, 100u, &cmeta_type_bool,
        CMETA_EFFECT_PURE,
        CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
    actions[0] = (cflow_machine_action){
        300u, &cmeta_type_int, 100u, &cmeta_type_bool,
        &cmeta_type_long, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_long, 0u};
    transitions[0] = (cflow_machine_transition){
        10u, 100u, 200u, 300u, 20u, 7u};
    return (cflow_machine_definition){
        states, 2u, 10u,
        events, 1u,
        guards, 1u,
        actions, 1u,
        transitions, 1u};
}

static bool owned_value_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    *(int *)destination = *(const int *)source;
    return true;
}

static void owned_value_destroy(void *value) {
    (void)value;
}

static const cmeta_type_traits owned_value_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY,
    NULL,
    NULL,
    NULL,
    owned_value_copy,
    NULL,
    owned_value_destroy
};

static const cmeta_type_desc owned_value_type = {
    "machine_owned_value",
    sizeof(int),
    CMETA_ALIGNOF(int),
    CMETA_T_OBJECT,
    NULL,
    &owned_value_traits,
    NULL
};

suite("CFlow Machine Resumable runtime") {
    it("transactionally initializes exact bindings on SerialExecutor") {
        cflow_machine_state states[2];
        cflow_event_type events[1];
        cflow_machine_guard guards[1];
        cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = runtime_definition(
            states, events, guards, actions, transitions);
        cflow_machine machine = {0};
        cflow_executor executor = {0};
        cflow_machine_instance instance = {0};
        const cflow_machine_guard_binding guard_bindings[] = {
            {200u, guard_enabled, NULL}
        };
        const cflow_machine_action_binding action_bindings[] = {
            {300u, action_to_long, NULL}
        };
        int initial = 7;
        const cflow_machine_instance_config config = {
            &machine, &initial, &cmeta_type_long,
            guard_bindings, 1u, action_bindings, 1u, 4u, &executor
        };
        cflow_machine_instance_stats stats = {0};
        const cmeta_type_desc *state_type = NULL;
        int state_value = 0;

        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_OK);
        initial = 99;
        check_equal(cflow_machine_instance_current_state(&instance),
                    (cflow_machine_state_id)10u);
        check_true(cflow_machine_instance_copy_state(
            &instance, &state_type, &state_value, sizeof(state_value)));
        check_true(cmeta_type_equal(state_type, &cmeta_type_int));
        check_equal(state_value, 7);
        check_true(cflow_machine_instance_get_stats(&instance, &stats));
        check_equal(stats.accepted, (uint64_t)0u);
        check_equal(stats.completed, (uint64_t)0u);
        check_equal(stats.pending, (size_t)0u);
        check_false(stats.closed);
        check_false(stats.cancelled);

        cflow_machine_instance_destroy(&instance);
        check_null(instance.impl);
        cflow_executor_destroy(&executor);
        cflow_machine_destroy(&machine);
    }

    it("rejects binding drift and unsupported executors without publication") {
        cflow_machine_state states[2];
        cflow_event_type events[1];
        cflow_machine_guard guards[1];
        cflow_machine_action actions[1];
        cflow_machine_transition transitions[1];
        cflow_machine_definition definition = runtime_definition(
            states, events, guards, actions, transitions);
        cflow_machine machine = {0};
        cflow_executor serial = {0};
        cflow_executor manual = {0};
        cflow_machine_instance instance = {0};
        cflow_machine_guard_binding guard_bindings[2] = {
            {200u, guard_enabled, NULL},
            {200u, guard_enabled, NULL}
        };
        cflow_machine_action_binding action_binding = {
            300u, action_to_long, NULL
        };
        int initial = 7;
        cflow_machine_instance_config config = {
            &machine, &initial, &cmeta_type_long,
            guard_bindings, 1u, &action_binding, 1u, 4u, &serial
        };

        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_OK);
        check_true(cflow_executor_serial_init(&serial));
        check_true(cflow_executor_manual_init(&manual));

        config.guard_count = 0u;
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH);
        check_null(instance.impl);
        config.guard_count = 2u;
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH);
        check_null(instance.impl);
        config.guard_count = 1u;
        guard_bindings[0].id = 201u;
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH);
        check_null(instance.impl);
        guard_bindings[0].id = 200u;
        guard_bindings[0].fn = NULL;
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH);
        check_null(instance.impl);
        guard_bindings[0].fn = guard_enabled;
        config.executor = &manual;
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_INVALID_EXECUTOR);
        check_null(instance.impl);
        config.executor = &serial;
        config.output_type = &cmeta_type_int;
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_TYPE_MISMATCH);
        check_null(instance.impl);

        cflow_machine_instance_destroy(&instance);
        cflow_executor_destroy(&manual);
        cflow_executor_destroy(&serial);
        cflow_machine_destroy(&machine);
    }

    it("rejects nontrivial state storage in the supported fragment") {
        const cflow_machine_state states[] = {
            {1u, &owned_value_type, CFLOW_MACHINE_STATE_DONE}
        };
        const cflow_machine_definition definition = {
            states, 1u, 1u, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u
        };
        cflow_machine machine = {0};
        cflow_executor executor = {0};
        cflow_machine_instance instance = {0};
        const int initial = 4;
        const cflow_machine_instance_config config = {
            &machine, &initial, &cmeta_type_int,
            NULL, 0u, NULL, 0u, 1u, &executor
        };

        check_equal(cflow_machine_build(&machine, &definition),
                    CFLOW_MACHINE_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(cflow_machine_instance_init(&instance, &config),
                    CFLOW_MACHINE_RUNTIME_UNSUPPORTED_TYPE);
        check_null(instance.impl);

        cflow_machine_instance_destroy(&instance);
        cflow_executor_destroy(&executor);
        cflow_machine_destroy(&machine);
    }
}
