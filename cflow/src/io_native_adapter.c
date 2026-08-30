#include <cflow/io_native_adapter.h>

#include "io_publisher_internal.h"
#include "scheduler_internal.h"

#include <stdint.h>
#include <stdlib.h>

typedef enum cflow_io_native_bridge_phase {
    CFLOW_IO_NATIVE_BRIDGE_FREE = 0,
    CFLOW_IO_NATIVE_BRIDGE_RESERVED,
    CFLOW_IO_NATIVE_BRIDGE_PENDING
} cflow_io_native_bridge_phase;

typedef struct cflow_io_native_bridge {
    cflow_io_actor *actor;
    cflow_io_request_id actor_request;
    turbo_io_request native_request;
    turbo_io_operation *operation;
    uint32_t generation;
    uint32_t next_free;
    cflow_io_native_bridge_phase phase;
} cflow_io_native_bridge;

typedef struct cflow_io_native_adapter_impl {
    turbo_io_backend backend;
    cflow_io_native_bridge *bridges;
    turbo_io_completion *completions;
    cflow_io_actor *bound_actor;
    size_t bridge_capacity;
    size_t completion_capacity;
    size_t active_bridges;
    uint32_t free_head;
    uint64_t actor_completions;
    uint64_t stale_actor_completions;
} cflow_io_native_adapter_impl;

enum { CFLOW_IO_NATIVE_NO_SLOT = UINT32_MAX };

static cflow_io_native_adapter_impl *native_adapter_impl(
    cflow_io_native_adapter *adapter) {
    return adapter != NULL
        ? (cflow_io_native_adapter_impl *)adapter->impl
        : NULL;
}

static cflow_io_native_bridge *native_adapter_reserve_bridge(
    cflow_io_native_adapter_impl *impl,
    size_t *out_index) {
    cflow_io_native_bridge *bridge;
    const uint32_t index = impl->free_head;

    if (index == CFLOW_IO_NATIVE_NO_SLOT)
        return NULL;
    bridge = &impl->bridges[index];
    impl->free_head = bridge->next_free;
    bridge->next_free = CFLOW_IO_NATIVE_NO_SLOT;
    bridge->phase = CFLOW_IO_NATIVE_BRIDGE_RESERVED;
    ++impl->active_bridges;
    *out_index = index;
    return bridge;
}

static void native_adapter_release_bridge(
    cflow_io_native_adapter_impl *impl,
    size_t index) {
    cflow_io_native_bridge *bridge = &impl->bridges[index];

    bridge->actor = NULL;
    bridge->actor_request = 0u;
    bridge->native_request = (turbo_io_request){0};
    bridge->operation = NULL;
    ++bridge->generation;
    if (bridge->generation == 0u)
        ++bridge->generation;
    bridge->phase = CFLOW_IO_NATIVE_BRIDGE_FREE;
    bridge->next_free = impl->free_head;
    impl->free_head = (uint32_t)index;
    --impl->active_bridges;
}

static cflow_io_native_bridge *native_adapter_find_actor_request(
    cflow_io_native_adapter_impl *impl,
    cflow_io_request_id request_id) {
    size_t index;

    for (index = 0u; index < impl->bridge_capacity; ++index) {
        cflow_io_native_bridge *bridge = &impl->bridges[index];
        if (bridge->phase == CFLOW_IO_NATIVE_BRIDGE_PENDING &&
            bridge->actor_request == request_id)
            return bridge;
    }
    return NULL;
}

static bool native_adapter_same_request(
    turbo_io_request left,
    turbo_io_request right) {
    return left.slot == right.slot && left.generation == right.generation;
}

static cflow_io_completion native_adapter_completion(
    const turbo_io_completion *native) {
    cflow_io_completion completion = {CFLOW_IO_COMPLETION_FAILED,
                                      native->bytes,
                                      native->status};

    switch (native->kind) {
    case TURBO_IO_COMPLETION_OK:
        completion.kind = CFLOW_IO_COMPLETION_OK;
        completion.error = TURBO_OK;
        break;
    case TURBO_IO_COMPLETION_EOF:
        completion.kind = CFLOW_IO_COMPLETION_EOF;
        completion.error = TURBO_OK;
        break;
    case TURBO_IO_COMPLETION_CANCELLED:
        completion.kind = CFLOW_IO_COMPLETION_CANCELLED;
        completion.error = TURBO_OK;
        break;
    case TURBO_IO_COMPLETION_FAILED:
        break;
    default:
        completion.kind = CFLOW_IO_COMPLETION_FAILED;
        completion.error = TURBO_EPROTO;
        break;
    }
    return completion;
}

static int native_adapter_actor_submit(
    void *backend_user,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user) {
    cflow_io_native_adapter *adapter =
        (cflow_io_native_adapter *)backend_user;
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    turbo_io_operation *operation = (turbo_io_operation *)operation_user;
    cflow_io_native_bridge *bridge;
    turbo_io_operation submitted;
    size_t index = 0u;
    int status;

    (void)lease_id;
    if (impl == NULL || actor == NULL || request_id == 0u ||
        operation == NULL)
        return TURBO_EINVAL;
    if (impl->bound_actor != NULL && impl->bound_actor != actor)
        return TURBO_EINVAL;
    if (impl->bound_actor == NULL)
        impl->bound_actor = actor;
    bridge = native_adapter_reserve_bridge(impl, &index);
    if (bridge == NULL)
        return TURBO_ENOBUFS;
    bridge->actor = actor;
    bridge->actor_request = request_id;
    bridge->operation = operation;

    submitted = *operation;
    submitted.user_data = (uintptr_t)(index + 1u);
    status = turbo_io_backend_submit(&impl->backend, &submitted,
                                     &bridge->native_request);
    if (status != TURBO_OK) {
        native_adapter_release_bridge(impl, index);
        return status;
    }
    bridge->phase = CFLOW_IO_NATIVE_BRIDGE_PENDING;
    return TURBO_OK;
}

static int native_adapter_actor_cancel(
    void *backend_user,
    cflow_io_request_id request_id) {
    cflow_io_native_adapter *adapter =
        (cflow_io_native_adapter *)backend_user;
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    cflow_io_native_bridge *bridge;
    int status;

    if (impl == NULL || request_id == 0u)
        return TURBO_EINVAL;
    bridge = native_adapter_find_actor_request(impl, request_id);
    if (bridge == NULL)
        return TURBO_ENOENT;
    status = turbo_io_backend_cancel(&impl->backend, bridge->native_request);
    /* NativeIO EALREADY still guarantees a terminal packet to observe. From
     * the Actor's perspective cancellation dispatch therefore succeeded. */
    return status == TURBO_EALREADY ? TURBO_OK : status;
}

int cflow_io_native_adapter_init(
    cflow_io_native_adapter *adapter,
    const cflow_io_native_adapter_config *config) {
    cflow_io_native_adapter_impl *impl;
    size_t index;
    int status;

    if (adapter == NULL || config == NULL || adapter->impl != NULL)
        return TURBO_EINVAL;
    if (config->backend.endpoint_capacity == 0u ||
        config->backend.request_capacity == 0u ||
        config->backend.completion_batch_capacity == 0u ||
        config->backend.completion_batch_capacity >
            config->backend.request_capacity)
        return TURBO_EINVAL;
    if (config->backend.endpoint_capacity > UINT32_MAX ||
        config->backend.request_capacity > UINT32_MAX ||
        config->backend.request_capacity >
            SIZE_MAX / sizeof(cflow_io_native_bridge) ||
        config->backend.completion_batch_capacity >
            SIZE_MAX / sizeof(turbo_io_completion))
        return TURBO_ERANGE;

    impl = (cflow_io_native_adapter_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;

    impl->bridges = (cflow_io_native_bridge *)calloc(
        config->backend.request_capacity, sizeof(*impl->bridges));
    impl->completions = (turbo_io_completion *)calloc(
        config->backend.completion_batch_capacity, sizeof(*impl->completions));
    if (impl->bridges == NULL || impl->completions == NULL) {
        free(impl->completions);
        free(impl->bridges);
        free(impl);
        return TURBO_ENOMEM;
    }

    status = turbo_io_backend_init(&impl->backend, &config->backend);
    if (status != TURBO_OK) {
        free(impl->completions);
        free(impl->bridges);
        free(impl);
        return status;
    }

    impl->bridge_capacity = config->backend.request_capacity;
    impl->completion_capacity = config->backend.completion_batch_capacity;
    impl->free_head = 0u;
    for (index = 0u; index < impl->bridge_capacity; ++index) {
        impl->bridges[index].generation = 1u;
        impl->bridges[index].next_free =
            index + 1u < impl->bridge_capacity
                ? (uint32_t)(index + 1u)
                : CFLOW_IO_NATIVE_NO_SLOT;
    }

    adapter->impl = impl;
    return TURBO_OK;
}

cflow_io_backend_ops cflow_io_native_adapter_actor_ops(void) {
    cflow_io_backend_ops ops = {
        native_adapter_actor_submit, native_adapter_actor_cancel};
    return ops;
}

int cflow_io_native_adapter_attach_socket(
    cflow_io_native_adapter *adapter,
    uintptr_t native_socket,
    turbo_io_endpoint *out_endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return TURBO_EINVAL;
    return turbo_io_backend_attach_socket(&impl->backend, native_socket,
                                          out_endpoint);
}

int cflow_io_native_adapter_release_socket(
    cflow_io_native_adapter *adapter,
    turbo_io_endpoint endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return TURBO_EINVAL;
    return turbo_io_backend_release_socket(&impl->backend, endpoint);
}

int cflow_io_native_adapter_attach_pipe(
    cflow_io_native_adapter *adapter,
    uintptr_t native_handle,
    uint32_t flags,
    turbo_io_endpoint *out_endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return TURBO_EINVAL;
    return turbo_io_backend_attach_pipe(&impl->backend, native_handle, flags,
                                        out_endpoint);
}

int cflow_io_native_adapter_release_pipe(
    cflow_io_native_adapter *adapter,
    turbo_io_endpoint endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return TURBO_EINVAL;
    return turbo_io_backend_release_pipe(&impl->backend, endpoint);
}

int cflow_io_native_adapter_observe(
    cflow_io_native_adapter *adapter,
    uint32_t timeout_ms,
    size_t *out_completed) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    size_t count = 0u;
    size_t index;
    int status;
    int delivery_status = TURBO_OK;

    if (impl == NULL || out_completed == NULL)
        return TURBO_EINVAL;
    *out_completed = 0u;
    status = turbo_io_backend_observe(&impl->backend, impl->completions,
                                      impl->completion_capacity, timeout_ms,
                                      &count);
    if (status != TURBO_OK)
        return status;

    for (index = 0u; index < count; ++index) {
        const turbo_io_completion *native = &impl->completions[index];
        const uintptr_t token = native->user_data;
        size_t bridge_index;
        cflow_io_native_bridge *bridge;
        cflow_io_completion completion;
        cflow_io_complete_status completed;

        if (token == 0u || token > impl->bridge_capacity) {
            if (delivery_status == TURBO_OK)
                delivery_status = TURBO_EPROTO;
            continue;
        }
        bridge_index = (size_t)(token - 1u);
        bridge = &impl->bridges[bridge_index];
        if (bridge->phase != CFLOW_IO_NATIVE_BRIDGE_PENDING ||
            !native_adapter_same_request(bridge->native_request,
                                         native->request)) {
            ++impl->stale_actor_completions;
            if (delivery_status == TURBO_OK)
                delivery_status = TURBO_EPROTO;
            continue;
        }

        if (bridge->operation->kind == TURBO_IO_UDP_RECV_FROM)
            bridge->operation->address_length = native->address_length;
        completion = native_adapter_completion(native);
        completed = cflow_io_actor_complete(
            bridge->actor, bridge->actor_request, &completion);
        if (completed == CFLOW_IO_COMPLETE_ACCEPTED)
            ++impl->actor_completions;
        else
            ++impl->stale_actor_completions;
        native_adapter_release_bridge(impl, bridge_index);
    }

    *out_completed = count;
    return delivery_status;
}

static int native_adapter_run_reactive_owner(
    cflow_io_publisher_owner *owner,
    size_t max_phase_steps,
    bool manual_scheduler,
    size_t *progressed) {
    int status;

    if (!manual_scheduler)
        return cflow_io_publisher_owner_run_ready(
            owner, max_phase_steps, progressed);
    status = cflow_io_publisher_owner_run_serial_batch_phase_internal(
        owner, max_phase_steps, progressed);
    if (status != TURBO_ENOTSUP)
        return status;
    return cflow_io_publisher_owner_run_ready(
        owner, max_phase_steps, progressed);
}

int cflow_io_native_adapter_drive_reactive(
    cflow_io_native_adapter *adapter,
    cflow_io_publisher_owner *owner,
    cflow_scheduler *scheduler,
    uint32_t timeout_ms,
    size_t max_phase_steps,
    size_t *out_completed) {
    size_t progressed = 0u;
    bool manual_scheduler;
    int observe_status;
    int owner_status;

    if (out_completed != NULL)
        *out_completed = 0u;
    if (native_adapter_impl(adapter) == NULL || owner == NULL ||
        owner->impl == NULL || max_phase_steps == 0u ||
        !cflow_scheduler_valid(scheduler) || out_completed == NULL)
        return TURBO_EINVAL;
    {
        const unsigned capabilities =
            cflow_scheduler_capabilities(scheduler);
        if ((capabilities & CMETA_SCHED_CAP_CALLER_DRIVEN_ZERO_DELAY) == 0u ||
            (capabilities & CMETA_SCHED_CAP_CONCURRENT) != 0u)
            return TURBO_EINVAL;
    }
    manual_scheduler = cflow_scheduler_is_manual_internal(scheduler);

    (void)cflow_scheduler_run_until_idle(scheduler, max_phase_steps);
    owner_status = native_adapter_run_reactive_owner(
        owner, max_phase_steps, manual_scheduler, &progressed);
    if (owner_status != TURBO_OK)
        return owner_status;

    observe_status = cflow_io_native_adapter_observe(
        adapter, timeout_ms, out_completed);
    progressed = 0u;
    owner_status = native_adapter_run_reactive_owner(
        owner, max_phase_steps, manual_scheduler, &progressed);
    (void)cflow_scheduler_run_until_idle(scheduler, max_phase_steps);
    return observe_status != TURBO_OK ? observe_status : owner_status;
}

int cflow_io_native_adapter_close(cflow_io_native_adapter *adapter) {
    cflow_io_native_adapter_impl *impl;

    if (adapter == NULL || adapter->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_io_native_adapter_impl *)adapter->impl;
    return turbo_io_backend_close(&impl->backend);
}

int cflow_io_native_adapter_destroy(cflow_io_native_adapter *adapter) {
    cflow_io_native_adapter_impl *impl;
    int status;

    if (adapter == NULL || adapter->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_io_native_adapter_impl *)adapter->impl;
    status = turbo_io_backend_destroy(&impl->backend);
    if (status != TURBO_OK)
        return status;

    free(impl->completions);
    free(impl->bridges);
    free(impl);
    adapter->impl = NULL;
    return TURBO_OK;
}

bool cflow_io_native_adapter_get_stats(
    const cflow_io_native_adapter *adapter,
    cflow_io_native_adapter_stats *out_stats) {
    const cflow_io_native_adapter_impl *impl;

    if (adapter == NULL || adapter->impl == NULL || out_stats == NULL)
        return false;
    impl = (const cflow_io_native_adapter_impl *)adapter->impl;
    *out_stats = (cflow_io_native_adapter_stats){0};
    if (!turbo_io_backend_get_stats(&impl->backend, &out_stats->native))
        return false;
    out_stats->active_bridges = impl->active_bridges;
    out_stats->actor_completions = impl->actor_completions;
    out_stats->stale_actor_completions = impl->stale_actor_completions;
    return true;
}
