#include <cflow/machine_runtime.h>

#include <turbo/thread.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_machine_instance_impl {
    const cflow_machine *machine;
    cflow_executor *executor;
    cflow_mailbox mailbox;
    bool mailbox_initialized;
    cflow_machine_guard_binding *guards;
    size_t guard_count;
    cflow_machine_action_binding *actions;
    size_t action_count;
    const cmeta_type_desc *output_type;
    const cflow_machine_state *state;
    unsigned char *state_value;
    size_t state_capacity;
    unsigned char *target_value;
    unsigned char *event_value;
    unsigned char *observation_value;
    size_t event_capacity;
    size_t observation_capacity;
    turbo_mutex_t lock;
    char *error;
    uint64_t completed;
    uint64_t failed;
    uint64_t cancelled_in_flight;
    uint64_t emitted_values;
    uint64_t emitted_events;
    size_t in_flight;
    bool closed;
    bool cancelled;
    bool done;
} cflow_machine_instance_impl;

static bool runtime_alignment_valid(size_t alignment) {
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u &&
           alignment <= _Alignof(cmeta_capture_storage);
}

static bool runtime_type_supported(const cmeta_type_desc *type) {
    const cmeta_trait_flags required =
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
    return cmeta_type_desc_valid(type) && type->size != 0u &&
           runtime_alignment_valid(type->align) &&
           cmeta_type_require_traits(type, required) == CMETA_OK;
}

static const cflow_machine_state *find_state(
    const cflow_machine *machine, cflow_machine_state_id id) {
    size_t left = 0u;
    size_t right = cflow_machine_state_count(machine);
    while (left < right) {
        const size_t middle = left + (right - left) / 2u;
        const cflow_machine_state *state =
            cflow_machine_state_at(machine, middle);
        if (state->id == id) return state;
        if (state->id < id)
            left = middle + 1u;
        else
            right = middle;
    }
    return NULL;
}

static int compare_guard_binding(const void *left, const void *right) {
    const cflow_machine_guard_binding *a =
        (const cflow_machine_guard_binding *)left;
    const cflow_machine_guard_binding *b =
        (const cflow_machine_guard_binding *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static int compare_action_binding(const void *left, const void *right) {
    const cflow_machine_action_binding *a =
        (const cflow_machine_action_binding *)left;
    const cflow_machine_action_binding *b =
        (const cflow_machine_action_binding *)right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static void instance_impl_free(cflow_machine_instance_impl *impl) {
    if (impl == NULL) return;
    if (impl->mailbox_initialized) cflow_mailbox_destroy(&impl->mailbox);
    if (impl->lock != NULL) turbo_mutex_destroy(&impl->lock);
    free(impl->error);
    free(impl->observation_value);
    free(impl->event_value);
    free(impl->target_value);
    free(impl->state_value);
    free(impl->actions);
    free(impl->guards);
    free(impl);
}

static cflow_machine_runtime_status copy_and_validate_bindings(
    cflow_machine_instance_impl *impl,
    const cflow_machine_instance_config *config) {
    const size_t guard_count = cflow_machine_guard_count(config->machine);
    const size_t action_count = cflow_machine_action_count(config->machine);
    size_t index;

    if (config->guard_count != guard_count ||
        config->action_count != action_count ||
        (guard_count != 0u && config->guards == NULL) ||
        (action_count != 0u && config->actions == NULL))
        return CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH;

    if (guard_count != 0u) {
        if (guard_count > SIZE_MAX / sizeof(*impl->guards))
            return CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT;
        impl->guards = (cflow_machine_guard_binding *)malloc(
            guard_count * sizeof(*impl->guards));
        if (impl->guards == NULL)
            return CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED;
        memcpy(impl->guards, config->guards,
               guard_count * sizeof(*impl->guards));
        qsort(impl->guards, guard_count, sizeof(*impl->guards),
              compare_guard_binding);
        for (index = 0u; index < guard_count; ++index) {
            const cflow_machine_guard *declaration =
                cflow_machine_guard_at(config->machine, index);
            if (impl->guards[index].id != declaration->id ||
                impl->guards[index].fn == NULL)
                return CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH;
        }
    }

    if (action_count != 0u) {
        if (action_count > SIZE_MAX / sizeof(*impl->actions))
            return CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT;
        impl->actions = (cflow_machine_action_binding *)malloc(
            action_count * sizeof(*impl->actions));
        if (impl->actions == NULL)
            return CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED;
        memcpy(impl->actions, config->actions,
               action_count * sizeof(*impl->actions));
        qsort(impl->actions, action_count, sizeof(*impl->actions),
              compare_action_binding);
        for (index = 0u; index < action_count; ++index) {
            const cflow_machine_action *declaration =
                cflow_machine_action_at(config->machine, index);
            if (impl->actions[index].id != declaration->id ||
                impl->actions[index].fn == NULL)
                return CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH;
        }
    }

    impl->guard_count = guard_count;
    impl->action_count = action_count;
    return CFLOW_MACHINE_RUNTIME_OK;
}

static cflow_machine_runtime_status measure_supported_types(
    cflow_machine_instance_impl *impl,
    const cflow_machine_instance_config *config) {
    size_t index;
    size_t state_capacity = 0u;
    size_t observation_capacity = config->output_type->size;

    if (!runtime_type_supported(config->output_type))
        return CFLOW_MACHINE_RUNTIME_UNSUPPORTED_TYPE;

    for (index = 0u; index < cflow_machine_state_count(config->machine);
         ++index) {
        const cflow_machine_state *state =
            cflow_machine_state_at(config->machine, index);
        if (!runtime_type_supported(state->value_type))
            return CFLOW_MACHINE_RUNTIME_UNSUPPORTED_TYPE;
        if (state->value_type->size > state_capacity)
            state_capacity = state->value_type->size;
    }

    for (index = 0u; index < cflow_machine_action_count(config->machine);
         ++index) {
        const cflow_machine_action *action =
            cflow_machine_action_at(config->machine, index);
        if (action->observation == CFLOW_MACHINE_ACTION_VALUE) {
            if (!cmeta_type_equal(action->output_type, config->output_type))
                return CFLOW_MACHINE_RUNTIME_TYPE_MISMATCH;
        }
        if (action->observation != CFLOW_MACHINE_ACTION_NONE) {
            if (!runtime_type_supported(action->output_type))
                return CFLOW_MACHINE_RUNTIME_UNSUPPORTED_TYPE;
            if (action->output_type->size > observation_capacity)
                observation_capacity = action->output_type->size;
        }
    }

    impl->state_capacity = state_capacity;
    impl->observation_capacity = observation_capacity;
    return CFLOW_MACHINE_RUNTIME_OK;
}

static cflow_machine_runtime_status initialize_mailbox(
    cflow_machine_instance_impl *impl,
    const cflow_machine_instance_config *config) {
    const size_t event_count = cflow_machine_event_count(config->machine);
    cflow_event_type *schema;
    size_t index;
    cflow_mailbox_status status;

    if (event_count == 0u) return CFLOW_MACHINE_RUNTIME_OK;
    if (event_count > SIZE_MAX / sizeof(*schema))
        return CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT;
    schema = (cflow_event_type *)malloc(event_count * sizeof(*schema));
    if (schema == NULL) return CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED;
    for (index = 0u; index < event_count; ++index)
        schema[index] = *cflow_machine_event_at(config->machine, index);
    status = cflow_mailbox_init(&impl->mailbox, schema, event_count,
                                config->mailbox_capacity);
    free(schema);
    if (status == CFLOW_MAILBOX_ALLOCATION_FAILED)
        return CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED;
    if (status != CFLOW_MAILBOX_OK)
        return CFLOW_MACHINE_RUNTIME_UNSUPPORTED_TYPE;
    impl->mailbox_initialized = true;
    impl->event_capacity = cflow_mailbox_payload_capacity(&impl->mailbox);
    return CFLOW_MACHINE_RUNTIME_OK;
}

cflow_machine_runtime_status cflow_machine_instance_init(
    cflow_machine_instance *instance,
    const cflow_machine_instance_config *config) {
    cflow_machine_instance_impl *impl;
    cflow_machine_runtime_status status;
    const cflow_machine_state *initial;

    if (instance == NULL || config == NULL || instance->impl != NULL ||
        config->machine == NULL || config->machine->impl == NULL ||
        config->initial_state == NULL || config->output_type == NULL ||
        config->executor == NULL || config->mailbox_capacity == 0u)
        return CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT;
    if (!cflow_executor_valid(config->executor) ||
        !cflow_executor_has(config->executor, CMETA_EXEC_CAP_SERIAL) ||
        cflow_executor_has(config->executor, CMETA_EXEC_CAP_MANUAL))
        return CFLOW_MACHINE_RUNTIME_INVALID_EXECUTOR;

    initial = find_state(config->machine,
                         cflow_machine_initial_state(config->machine));
    if (initial == NULL) return CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT;

    impl = (cflow_machine_instance_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED;
    impl->machine = config->machine;
    impl->executor = config->executor;
    impl->output_type = config->output_type;
    impl->state = initial;

    status = copy_and_validate_bindings(impl, config);
    if (status != CFLOW_MACHINE_RUNTIME_OK) {
        instance_impl_free(impl);
        return status;
    }
    status = measure_supported_types(impl, config);
    if (status != CFLOW_MACHINE_RUNTIME_OK) {
        instance_impl_free(impl);
        return status;
    }
    status = initialize_mailbox(impl, config);
    if (status != CFLOW_MACHINE_RUNTIME_OK) {
        instance_impl_free(impl);
        return status;
    }

    impl->state_value = (unsigned char *)malloc(impl->state_capacity);
    impl->target_value = (unsigned char *)malloc(impl->state_capacity);
    impl->event_value = (unsigned char *)malloc(
        impl->event_capacity != 0u ? impl->event_capacity : 1u);
    impl->observation_value = (unsigned char *)malloc(
        impl->observation_capacity);
    turbo_mutex_init(&impl->lock);
    if (impl->state_value == NULL || impl->target_value == NULL ||
        impl->event_value == NULL || impl->observation_value == NULL ||
        impl->lock == NULL) {
        instance_impl_free(impl);
        return CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED;
    }
    memcpy(impl->state_value, config->initial_state,
           initial->value_type->size);
    impl->done = initial->kind != CFLOW_MACHINE_STATE_ACTIVE;
    instance->impl = impl;
    return CFLOW_MACHINE_RUNTIME_OK;
}

bool cflow_machine_instance_copy_state(
    const cflow_machine_instance *instance,
    const cmeta_type_desc **out_type,
    void *out_value,
    size_t out_value_capacity) {
    cflow_machine_instance_impl *impl = instance != NULL
        ? (cflow_machine_instance_impl *)instance->impl : NULL;
    if (out_type != NULL) *out_type = NULL;
    if (impl == NULL || out_type == NULL || out_value == NULL)
        return false;
    turbo_mutex_lock(&impl->lock);
    if (out_value_capacity < impl->state->value_type->size) {
        turbo_mutex_unlock(&impl->lock);
        return false;
    }
    memcpy(out_value, impl->state_value, impl->state->value_type->size);
    *out_type = impl->state->value_type;
    turbo_mutex_unlock(&impl->lock);
    return true;
}

cflow_machine_state_id cflow_machine_instance_current_state(
    const cflow_machine_instance *instance) {
    cflow_machine_instance_impl *impl = instance != NULL
        ? (cflow_machine_instance_impl *)instance->impl : NULL;
    cflow_machine_state_id result = 0u;
    if (impl == NULL) return 0u;
    turbo_mutex_lock(&impl->lock);
    result = impl->state->id;
    turbo_mutex_unlock(&impl->lock);
    return result;
}

bool cflow_machine_instance_get_stats(
    const cflow_machine_instance *instance,
    cflow_machine_instance_stats *out) {
    cflow_machine_instance_impl *impl = instance != NULL
        ? (cflow_machine_instance_impl *)instance->impl : NULL;
    cflow_machine_instance_stats snapshot = {0};
    cflow_mailbox_stats mailbox_stats = {0};
    if (impl == NULL || out == NULL) return false;
    if (impl->mailbox_initialized &&
        !cflow_mailbox_get_stats(&impl->mailbox, &mailbox_stats))
        return false;
    turbo_mutex_lock(&impl->lock);
    snapshot.accepted = mailbox_stats.accepted;
    snapshot.completed = impl->completed;
    snapshot.failed = impl->failed;
    snapshot.cancelled_events = mailbox_stats.cancelled +
                                impl->cancelled_in_flight;
    snapshot.emitted_values = impl->emitted_values;
    snapshot.emitted_events = impl->emitted_events;
    snapshot.pending = mailbox_stats.pending;
    snapshot.in_flight = impl->in_flight;
    snapshot.current_state = impl->state->id;
    snapshot.closed = impl->closed;
    snapshot.cancelled = impl->cancelled;
    snapshot.done = impl->done;
    snapshot.errored = impl->error != NULL;
    turbo_mutex_unlock(&impl->lock);
    *out = snapshot;
    return true;
}

const char *cflow_machine_instance_error(
    const cflow_machine_instance *instance) {
    cflow_machine_instance_impl *impl = instance != NULL
        ? (cflow_machine_instance_impl *)instance->impl : NULL;
    const char *error = NULL;
    if (impl == NULL) return NULL;
    turbo_mutex_lock(&impl->lock);
    error = impl->error;
    turbo_mutex_unlock(&impl->lock);
    return error;
}

void cflow_machine_instance_destroy(cflow_machine_instance *instance) {
    cflow_machine_instance_impl *impl;
    if (instance == NULL) return;
    impl = (cflow_machine_instance_impl *)instance->impl;
    instance->impl = NULL;
    instance_impl_free(impl);
}
