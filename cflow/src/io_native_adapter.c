#include <cflow/io_native_adapter.h>

#include "io_publisher_internal.h"

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
    native_io_request native_request;
    native_io_operation *operation;
    uint32_t generation;
    uint32_t next_free;
    cflow_io_native_bridge_phase phase;
} cflow_io_native_bridge;

typedef struct cflow_io_native_adapter_impl {
    native_io_backend backend;
    cflow_io_native_bridge *bridges;
    native_io_completion *completions;
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
    bridge->native_request = (native_io_request){0};
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
    native_io_request left,
    native_io_request right) {
    return left.slot == right.slot && left.generation == right.generation;
}

static cflow_io_completion native_adapter_completion(
    const native_io_completion *native) {
    cflow_io_completion completion = {CFLOW_IO_COMPLETION_FAILED,
                                      native->bytes,
                                      native->status};

    switch (native->kind) {
    case NATIVE_IO_COMPLETION_OK:
        completion.kind = CFLOW_IO_COMPLETION_OK;
        completion.error = SALTS_OK;
        break;
    case NATIVE_IO_COMPLETION_EOF:
        completion.kind = CFLOW_IO_COMPLETION_EOF;
        completion.error = SALTS_OK;
        break;
    case NATIVE_IO_COMPLETION_CANCELLED:
        completion.kind = CFLOW_IO_COMPLETION_CANCELLED;
        completion.error = SALTS_OK;
        break;
    case NATIVE_IO_COMPLETION_FAILED:
        break;
    default:
        completion.kind = CFLOW_IO_COMPLETION_FAILED;
        completion.error = SALTS_EPROTO;
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
    native_io_operation *operation = (native_io_operation *)operation_user;
    cflow_io_native_bridge *bridge;
    native_io_operation submitted;
    size_t index = 0u;
    int status;

    (void)lease_id;
    if (impl == NULL || actor == NULL || request_id == 0u ||
        operation == NULL)
        return SALTS_EINVAL;
    if (impl->bound_actor != NULL && impl->bound_actor != actor)
        return SALTS_EINVAL;
    if (impl->bound_actor == NULL)
        impl->bound_actor = actor;
    bridge = native_adapter_reserve_bridge(impl, &index);
    if (bridge == NULL)
        return SALTS_ENOBUFS;
    bridge->actor = actor;
    bridge->actor_request = request_id;
    bridge->operation = operation;

    submitted = *operation;
    submitted.user_data = (uintptr_t)(index + 1u);
    status = native_io_backend_submit(&impl->backend, &submitted,
                                     &bridge->native_request);
    if (status != SALTS_OK) {
        native_adapter_release_bridge(impl, index);
        return status;
    }
    bridge->phase = CFLOW_IO_NATIVE_BRIDGE_PENDING;
    return SALTS_OK;
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
        return SALTS_EINVAL;
    bridge = native_adapter_find_actor_request(impl, request_id);
    if (bridge == NULL)
        return SALTS_ENOENT;
    status = native_io_backend_cancel(&impl->backend, bridge->native_request);
    /* NativeIO EALREADY still guarantees a terminal packet to observe. From
     * the Actor's perspective cancellation dispatch therefore succeeded. */
    return status == SALTS_EALREADY ? SALTS_OK : status;
}

int cflow_io_native_adapter_init(
    cflow_io_native_adapter *adapter,
    const cflow_io_native_adapter_config *config) {
    cflow_io_native_adapter_impl *impl;
    size_t index;
    int status;

    if (adapter == NULL || config == NULL || adapter->impl != NULL)
        return SALTS_EINVAL;
    if (config->backend.endpoint_capacity == 0u ||
        config->backend.request_capacity == 0u ||
        config->backend.completion_batch_capacity == 0u ||
        config->backend.completion_batch_capacity >
            config->backend.request_capacity)
        return SALTS_EINVAL;
    if (config->backend.endpoint_capacity > UINT32_MAX ||
        config->backend.request_capacity > UINT32_MAX ||
        config->backend.request_capacity >
            SIZE_MAX / sizeof(cflow_io_native_bridge) ||
        config->backend.completion_batch_capacity >
            SIZE_MAX / sizeof(native_io_completion))
        return SALTS_ERANGE;

    impl = (cflow_io_native_adapter_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return SALTS_ENOMEM;

    impl->bridges = (cflow_io_native_bridge *)calloc(
        config->backend.request_capacity, sizeof(*impl->bridges));
    impl->completions = (native_io_completion *)calloc(
        config->backend.completion_batch_capacity, sizeof(*impl->completions));
    if (impl->bridges == NULL || impl->completions == NULL) {
        free(impl->completions);
        free(impl->bridges);
        free(impl);
        return SALTS_ENOMEM;
    }

    status = native_io_backend_init(&impl->backend, &config->backend);
    if (status != SALTS_OK) {
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
    return SALTS_OK;
}

cflow_io_backend_ops cflow_io_native_adapter_actor_ops(void) {
    cflow_io_backend_ops ops = {
        native_adapter_actor_submit, native_adapter_actor_cancel};
    return ops;
}

int cflow_io_native_adapter_attach_socket(
    cflow_io_native_adapter *adapter,
    uintptr_t native_socket,
    native_io_endpoint *out_endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return SALTS_EINVAL;
    return native_io_backend_attach_socket(&impl->backend, native_socket,
                                          out_endpoint);
}

int cflow_io_native_adapter_release_socket(
    cflow_io_native_adapter *adapter,
    native_io_endpoint endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return SALTS_EINVAL;
    return native_io_backend_release_socket(&impl->backend, endpoint);
}

int cflow_io_native_adapter_attach_pipe(
    cflow_io_native_adapter *adapter,
    uintptr_t native_handle,
    uint32_t flags,
    native_io_endpoint *out_endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return SALTS_EINVAL;
    return native_io_backend_attach_pipe(&impl->backend, native_handle, flags,
                                        out_endpoint);
}

int cflow_io_native_adapter_release_pipe(
    cflow_io_native_adapter *adapter,
    native_io_endpoint endpoint) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return SALTS_EINVAL;
    return native_io_backend_release_pipe(&impl->backend, endpoint);
}

int cflow_io_native_adapter_observe(
    cflow_io_native_adapter *adapter,
    uint32_t timeout_ms,
    size_t *out_completed) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    size_t count = 0u;
    size_t index;
    int status;
    int delivery_status = SALTS_OK;

    if (impl == NULL || out_completed == NULL)
        return SALTS_EINVAL;
    *out_completed = 0u;
    status = native_io_backend_observe(&impl->backend, impl->completions,
                                      impl->completion_capacity, timeout_ms,
                                      &count);
    if (status != SALTS_OK)
        return status;

    for (index = 0u; index < count; ++index) {
        const native_io_completion *native = &impl->completions[index];
        const uintptr_t token = native->user_data;
        size_t bridge_index;
        cflow_io_native_bridge *bridge;
        cflow_io_completion completion;
        cflow_io_complete_status completed;

        if (token == 0u || token > impl->bridge_capacity) {
            if (delivery_status == SALTS_OK)
                delivery_status = SALTS_EPROTO;
            continue;
        }
        bridge_index = (size_t)(token - 1u);
        bridge = &impl->bridges[bridge_index];
        if (bridge->phase != CFLOW_IO_NATIVE_BRIDGE_PENDING ||
            !native_adapter_same_request(bridge->native_request,
                                         native->request)) {
            ++impl->stale_actor_completions;
            if (delivery_status == SALTS_OK)
                delivery_status = SALTS_EPROTO;
            continue;
        }

        if (bridge->operation->kind == NATIVE_IO_OPERATION_UDP_RECV_FROM)
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

int cflow_io_native_adapter_wake(cflow_io_native_adapter *adapter) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    if (impl == NULL)
        return SALTS_EINVAL;
    return native_io_backend_wake(&impl->backend);
}

int cflow_io_native_adapter_drive_publisher(
    cflow_io_native_adapter *adapter,
    cflow_io_publisher_owner *owner,
    uint32_t timeout_ms,
    size_t max_phase_steps,
    size_t *out_completed) {
    cflow_io_native_adapter_impl *impl = native_adapter_impl(adapter);
    size_t progressed = 0u;
    size_t polled = 0u;
    int poll_status;
    int observe_status = SALTS_OK;
    int owner_status;

    if (out_completed != NULL)
        *out_completed = 0u;
    if (impl == NULL || owner == NULL || owner->impl == NULL ||
        max_phase_steps == 0u || out_completed == NULL)
        return SALTS_EINVAL;

    /* A wake can be posted before the owner reaches observe. Consume that
     * control edge before admitting new operations so it cannot masquerade as
     * progress for the newly submitted batch. Ready completions are delivered
     * at the same boundary and cause this tick to return after owner work. */
    poll_status = cflow_io_native_adapter_observe(adapter, 0u, &polled);
    if (poll_status != SALTS_OK && poll_status != SALTS_ETIMEDOUT)
        return poll_status;
    *out_completed = polled;

    owner_status = cflow_io_publisher_owner_run_ready(
        owner, max_phase_steps, &progressed);
    if (owner_status != SALTS_OK)
        return owner_status;
    if (polled != 0u)
        return SALTS_OK;
    if (impl->active_bridges == 0u)
        return SALTS_OK;

    observe_status = cflow_io_native_adapter_observe(
        adapter, timeout_ms, out_completed);
    progressed = 0u;
    owner_status = cflow_io_publisher_owner_run_ready(
        owner, max_phase_steps, &progressed);
    return observe_status != SALTS_OK ? observe_status : owner_status;
}

int cflow_io_native_adapter_close(cflow_io_native_adapter *adapter) {
    cflow_io_native_adapter_impl *impl;

    if (adapter == NULL || adapter->impl == NULL)
        return SALTS_EINVAL;
    impl = (cflow_io_native_adapter_impl *)adapter->impl;
    return native_io_backend_close(&impl->backend);
}

int cflow_io_native_adapter_destroy(cflow_io_native_adapter *adapter) {
    cflow_io_native_adapter_impl *impl;
    int status;

    if (adapter == NULL || adapter->impl == NULL)
        return SALTS_EINVAL;
    impl = (cflow_io_native_adapter_impl *)adapter->impl;
    status = native_io_backend_destroy(&impl->backend);
    if (status != SALTS_OK)
        return status;

    free(impl->completions);
    free(impl->bridges);
    free(impl);
    adapter->impl = NULL;
    return SALTS_OK;
}

bool cflow_io_native_adapter_get_stats(
    const cflow_io_native_adapter *adapter,
    cflow_io_native_adapter_stats *out_stats) {
    const cflow_io_native_adapter_impl *impl;

    if (adapter == NULL || adapter->impl == NULL || out_stats == NULL)
        return false;
    impl = (const cflow_io_native_adapter_impl *)adapter->impl;
    *out_stats = (cflow_io_native_adapter_stats){0};
    if (!native_io_backend_get_stats(&impl->backend, &out_stats->native))
        return false;
    out_stats->active_bridges = impl->active_bridges;
    out_stats->actor_completions = impl->actor_completions;
    out_stats->stale_actor_completions = impl->stale_actor_completions;
    return true;
}
