#include <cflow/io_cnet_adapter.h>

#include <salts/error_codes.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum cflow_io_cnet_bridge_phase {
    CFLOW_IO_CNET_BRIDGE_FREE = 0,
    CFLOW_IO_CNET_BRIDGE_CREDIT_PENDING
} cflow_io_cnet_bridge_phase;

typedef struct cflow_io_cnet_bridge {
    cflow_io_actor *actor;
    cflow_io_request_id actor_request;
    cflow_io_cnet_receive_operation *operation;
    uint32_t next_free;
    cflow_io_cnet_bridge_phase phase;
    bool cancelled;
} cflow_io_cnet_bridge;

typedef struct cflow_io_cnet_session_adapter_impl {
    cnet_client *client;
    cflow_io_cnet_bridge *bridges;
    uint32_t *credit_order;
    cflow_io_actor *bound_actor;
    cnet_connection connection;
    cnet_state_fn on_state;
    cnet_send_fn on_send;
    void *observer_user;
    size_t bridge_capacity;
    size_t active_bridges;
    size_t credit_head;
    size_t credit_tail;
    size_t pending_credits;
    size_t cancelled_tombstones;
    uint32_t free_head;
    uint64_t admitted_credits;
    uint64_t received_values;
    uint64_t copied_bytes;
    uint64_t cancelled_requests;
    uint64_t discarded_cancelled_values;
    uint64_t terminal_drains;
    uint64_t stale_callbacks;
    cnet_connection_state state;
    bool bound;
    bool state_known;
    bool terminal;
    bool closed;
} cflow_io_cnet_session_adapter_impl;

enum { CFLOW_IO_CNET_NO_SLOT = UINT32_MAX };

static cflow_io_cnet_session_adapter_impl *
cflow_io_cnet_impl(cflow_io_cnet_session_adapter *adapter) {
    return adapter != NULL
        ? (cflow_io_cnet_session_adapter_impl *)adapter->impl
        : NULL;
}

static const cflow_io_cnet_session_adapter_impl *
cflow_io_cnet_const_impl(const cflow_io_cnet_session_adapter *adapter) {
    return adapter != NULL
        ? (const cflow_io_cnet_session_adapter_impl *)adapter->impl
        : NULL;
}

static bool cflow_io_cnet_connection_valid(cnet_connection connection) {
    return connection.slot != 0u && connection.generation != 0u;
}

static bool cflow_io_cnet_same_connection(
    cnet_connection left, cnet_connection right) {
    return left.slot == right.slot &&
           left.generation == right.generation;
}

static cflow_io_cnet_bridge *
cflow_io_cnet_reserve(cflow_io_cnet_session_adapter_impl *impl,
                      size_t *out_index) {
    cflow_io_cnet_bridge *bridge;
    const uint32_t index = impl->free_head;

    if (index == CFLOW_IO_CNET_NO_SLOT)
        return NULL;
    bridge = &impl->bridges[index];
    impl->free_head = bridge->next_free;
    bridge->next_free = CFLOW_IO_CNET_NO_SLOT;
    bridge->phase = CFLOW_IO_CNET_BRIDGE_CREDIT_PENDING;
    bridge->cancelled = false;
    ++impl->active_bridges;
    *out_index = (size_t)index;
    return bridge;
}

static void cflow_io_cnet_release(
    cflow_io_cnet_session_adapter_impl *impl,
    size_t index) {
    cflow_io_cnet_bridge *bridge = &impl->bridges[index];

    if (bridge->cancelled && impl->cancelled_tombstones != 0u)
        --impl->cancelled_tombstones;
    bridge->actor = NULL;
    bridge->actor_request = 0u;
    bridge->operation = NULL;
    bridge->cancelled = false;
    bridge->phase = CFLOW_IO_CNET_BRIDGE_FREE;
    bridge->next_free = impl->free_head;
    impl->free_head = (uint32_t)index;
    if (impl->active_bridges != 0u)
        --impl->active_bridges;
}

static void cflow_io_cnet_queue_push(
    cflow_io_cnet_session_adapter_impl *impl,
    size_t index) {
    impl->credit_order[impl->credit_tail] = (uint32_t)index;
    impl->credit_tail =
        (impl->credit_tail + 1u) % impl->bridge_capacity;
    ++impl->pending_credits;
}

static bool cflow_io_cnet_queue_pop(
    cflow_io_cnet_session_adapter_impl *impl,
    size_t *out_index) {
    if (impl->pending_credits == 0u || out_index == NULL)
        return false;
    *out_index = (size_t)impl->credit_order[impl->credit_head];
    impl->credit_head =
        (impl->credit_head + 1u) % impl->bridge_capacity;
    --impl->pending_credits;
    return true;
}

static cflow_io_cnet_bridge *
cflow_io_cnet_find_request(
    cflow_io_cnet_session_adapter_impl *impl,
    cflow_io_request_id request_id) {
    size_t index;

    for (index = 0u; index < impl->bridge_capacity; ++index) {
        cflow_io_cnet_bridge *bridge = &impl->bridges[index];
        if (bridge->phase == CFLOW_IO_CNET_BRIDGE_CREDIT_PENDING &&
            bridge->actor_request == request_id)
            return bridge;
    }
    return NULL;
}

static cflow_io_complete_status cflow_io_cnet_complete(
    cflow_io_cnet_session_adapter_impl *impl,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    cflow_io_completion completion) {
    const cflow_io_complete_status status =
        cflow_io_actor_complete(actor, request_id, &completion);
    if (status != CFLOW_IO_COMPLETE_ACCEPTED)
        ++impl->stale_callbacks;
    return status;
}

static void cflow_io_cnet_receive_callback(
    void *user,
    cnet_connection connection,
    const cnet_receive_view *view) {
    cflow_io_cnet_session_adapter *adapter =
        (cflow_io_cnet_session_adapter *)user;
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);
    cflow_io_cnet_bridge *bridge;
    cflow_io_completion completion;
    size_t index;

    if (impl == NULL || view == NULL ||
        !impl->bound ||
        !cflow_io_cnet_same_connection(impl->connection, connection) ||
        !cflow_io_cnet_queue_pop(impl, &index)) {
        if (impl != NULL)
            ++impl->stale_callbacks;
        return;
    }

    bridge = &impl->bridges[index];
    ++impl->received_values;
    if (bridge->phase != CFLOW_IO_CNET_BRIDGE_CREDIT_PENDING) {
        ++impl->stale_callbacks;
        return;
    }

    if (bridge->cancelled) {
        ++impl->discarded_cancelled_values;
        cflow_io_cnet_release(impl, index);
        return;
    }

    completion = (cflow_io_completion){
        CFLOW_IO_COMPLETION_OK, view->size, SALTS_OK};
    if (bridge->operation == NULL ||
        bridge->operation->buffer == NULL ||
        bridge->operation->capacity == 0u) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, SALTS_EPROTO};
    } else if (view->size > bridge->operation->capacity) {
        bridge->operation->size = 0u;
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, SALTS_EMSGSIZE};
    } else {
        if (view->size != 0u)
            memcpy(bridge->operation->buffer, view->data, view->size);
        bridge->operation->size = view->size;
        bridge->operation->kind = view->kind;
        impl->copied_bytes += view->size;
    }

    (void)cflow_io_cnet_complete(
        impl, bridge->actor, bridge->actor_request, completion);
    cflow_io_cnet_release(impl, index);
}

static void cflow_io_cnet_drain_terminal(
    cflow_io_cnet_session_adapter_impl *impl,
    cnet_connection_state state,
    const cnet_error *error) {
    size_t index;

    while (cflow_io_cnet_queue_pop(impl, &index)) {
        cflow_io_cnet_bridge *bridge = &impl->bridges[index];
        ++impl->terminal_drains;
        if (bridge->phase != CFLOW_IO_CNET_BRIDGE_CREDIT_PENDING) {
            ++impl->stale_callbacks;
            continue;
        }
        if (!bridge->cancelled) {
            const cflow_io_completion completion =
                state == CNET_CONNECTION_FAILED
                    ? (cflow_io_completion){
                          CFLOW_IO_COMPLETION_FAILED,
                          0u,
                          error != NULL ? error->status : SALTS_EIO}
                    : (cflow_io_completion){
                          CFLOW_IO_COMPLETION_EOF, 0u, SALTS_OK};
            (void)cflow_io_cnet_complete(
                impl, bridge->actor, bridge->actor_request, completion);
        }
        cflow_io_cnet_release(impl, index);
    }
}

static void cflow_io_cnet_state_callback(
    void *user,
    cnet_connection connection,
    cnet_connection_state state,
    const cnet_error *error) {
    cflow_io_cnet_session_adapter *adapter =
        (cflow_io_cnet_session_adapter *)user;
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);

    if (impl == NULL)
        return;
    if (!impl->bound ||
        !cflow_io_cnet_same_connection(impl->connection, connection)) {
        ++impl->stale_callbacks;
        return;
    }

    impl->state = state;
    impl->state_known = true;
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) {
        impl->terminal = true;
        cflow_io_cnet_drain_terminal(impl, state, error);
    }

    if (impl->on_state != NULL)
        impl->on_state(
            impl->observer_user, connection, state, error);
}

static void cflow_io_cnet_send_callback(
    void *user,
    cnet_connection connection,
    size_t size) {
    cflow_io_cnet_session_adapter *adapter =
        (cflow_io_cnet_session_adapter *)user;
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);

    if (impl == NULL)
        return;
    if (!impl->bound ||
        !cflow_io_cnet_same_connection(impl->connection, connection)) {
        ++impl->stale_callbacks;
        return;
    }
    if (impl->on_send != NULL)
        impl->on_send(impl->observer_user, connection, size);
}

static int cflow_io_cnet_actor_submit(
    void *backend_user,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user) {
    cflow_io_cnet_session_adapter *adapter =
        (cflow_io_cnet_session_adapter *)backend_user;
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);
    cflow_io_cnet_receive_operation *operation =
        (cflow_io_cnet_receive_operation *)operation_user;
    cflow_io_cnet_bridge *bridge;
    size_t index = 0u;
    int status;

    (void)lease_id;
    if (impl == NULL || actor == NULL || request_id == 0u ||
        operation == NULL || operation->buffer == NULL ||
        operation->capacity == 0u)
        return SALTS_EINVAL;
    if (impl->closed)
        return SALTS_ESHUTDOWN;
    if (!impl->bound || impl->terminal)
        return SALTS_ENOTCONN;
    if (impl->bound_actor != NULL && impl->bound_actor != actor)
        return SALTS_EINVAL;
    if (impl->bound_actor == NULL)
        impl->bound_actor = actor;

    if (impl->pending_credits >= impl->bridge_capacity)
        return SALTS_ENOBUFS;
    bridge = cflow_io_cnet_reserve(impl, &index);
    if (bridge == NULL)
        return SALTS_ENOBUFS;
    bridge->actor = actor;
    bridge->actor_request = request_id;
    bridge->operation = operation;
    operation->size = 0u;

    status = cnet_receive(impl->client, impl->connection, 1u);
    if (status != SALTS_OK) {
        cflow_io_cnet_release(impl, index);
        return status;
    }
    cflow_io_cnet_queue_push(impl, index);
    ++impl->admitted_credits;
    return SALTS_OK;
}

static int cflow_io_cnet_actor_cancel(
    void *backend_user,
    cflow_io_request_id request_id) {
    cflow_io_cnet_session_adapter *adapter =
        (cflow_io_cnet_session_adapter *)backend_user;
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);
    cflow_io_cnet_bridge *bridge;
    cflow_io_complete_status completed;

    if (impl == NULL || request_id == 0u)
        return SALTS_EINVAL;
    bridge = cflow_io_cnet_find_request(impl, request_id);
    if (bridge == NULL)
        return SALTS_ENOENT;
    if (bridge->cancelled)
        return SALTS_EALREADY;

    bridge->cancelled = true;
    bridge->operation = NULL;
    ++impl->cancelled_tombstones;
    ++impl->cancelled_requests;

    completed = cflow_io_cnet_complete(
        impl, bridge->actor, bridge->actor_request,
        (cflow_io_completion){
            CFLOW_IO_COMPLETION_CANCELLED, 0u, SALTS_OK});
    return completed == CFLOW_IO_COMPLETE_ACCEPTED
        ? SALTS_OK
        : SALTS_EPROTO;
}

int cflow_io_cnet_session_adapter_init(
    cflow_io_cnet_session_adapter *adapter,
    const cflow_io_cnet_session_adapter_config *config) {
    cflow_io_cnet_session_adapter_impl *impl;
    size_t index;

    if (adapter == NULL || config == NULL ||
        adapter->impl != NULL || config->client == NULL ||
        config->bridge_capacity == 0u ||
        config->bridge_capacity > UINT32_MAX ||
        config->bridge_capacity >
            SIZE_MAX / sizeof(cflow_io_cnet_bridge) ||
        config->bridge_capacity >
            SIZE_MAX / sizeof(uint32_t))
        return SALTS_EINVAL;

    impl = (cflow_io_cnet_session_adapter_impl *)calloc(
        1u, sizeof(*impl));
    if (impl == NULL)
        return SALTS_ENOMEM;
    impl->bridges = (cflow_io_cnet_bridge *)calloc(
        config->bridge_capacity, sizeof(*impl->bridges));
    impl->credit_order = (uint32_t *)calloc(
        config->bridge_capacity, sizeof(*impl->credit_order));
    if (impl->bridges == NULL || impl->credit_order == NULL) {
        free(impl->credit_order);
        free(impl->bridges);
        free(impl);
        return SALTS_ENOMEM;
    }

    impl->client = config->client;
    impl->bridge_capacity = config->bridge_capacity;
    impl->on_state = config->on_state;
    impl->on_send = config->on_send;
    impl->observer_user = config->observer_user;
    impl->free_head = 0u;
    for (index = 0u; index < impl->bridge_capacity; ++index) {
        impl->bridges[index].phase = CFLOW_IO_CNET_BRIDGE_FREE;
        impl->bridges[index].next_free =
            index + 1u < impl->bridge_capacity
                ? (uint32_t)(index + 1u)
                : CFLOW_IO_CNET_NO_SLOT;
    }

    adapter->impl = impl;
    return SALTS_OK;
}

cnet_observer cflow_io_cnet_session_adapter_observer(
    cflow_io_cnet_session_adapter *adapter) {
    const cnet_observer observer = {
        cflow_io_cnet_state_callback,
        cflow_io_cnet_receive_callback,
        adapter,
        cflow_io_cnet_send_callback};
    return cflow_io_cnet_impl(adapter) != NULL
        ? observer
        : (cnet_observer){0};
}

int cflow_io_cnet_session_adapter_bind(
    cflow_io_cnet_session_adapter *adapter,
    cnet_connection connection) {
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);

    if (impl == NULL || !cflow_io_cnet_connection_valid(connection))
        return SALTS_EINVAL;
    if (impl->bound)
        return SALTS_EALREADY;
    if (impl->closed)
        return SALTS_ESHUTDOWN;
    impl->connection = connection;
    impl->bound = true;
    return SALTS_OK;
}

cflow_io_backend_ops cflow_io_cnet_session_adapter_actor_ops(void) {
    const cflow_io_backend_ops ops = {
        cflow_io_cnet_actor_submit,
        cflow_io_cnet_actor_cancel};
    return ops;
}

int cflow_io_cnet_session_adapter_close(
    cflow_io_cnet_session_adapter *adapter) {
    cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_impl(adapter);

    if (impl == NULL)
        return SALTS_EINVAL;
    if (impl->closed)
        return SALTS_EALREADY;
    impl->closed = true;
    return SALTS_OK;
}

bool cflow_io_cnet_session_adapter_get_stats(
    const cflow_io_cnet_session_adapter *adapter,
    cflow_io_cnet_session_adapter_stats *out_stats) {
    const cflow_io_cnet_session_adapter_impl *impl =
        cflow_io_cnet_const_impl(adapter);

    if (impl == NULL || out_stats == NULL)
        return false;
    *out_stats = (cflow_io_cnet_session_adapter_stats){
        .bridge_capacity = impl->bridge_capacity,
        .active_bridges = impl->active_bridges,
        .pending_credits = impl->pending_credits,
        .cancelled_tombstones = impl->cancelled_tombstones,
        .admitted_credits = impl->admitted_credits,
        .received_values = impl->received_values,
        .copied_bytes = impl->copied_bytes,
        .cancelled_requests = impl->cancelled_requests,
        .discarded_cancelled_values =
            impl->discarded_cancelled_values,
        .terminal_drains = impl->terminal_drains,
        .stale_callbacks = impl->stale_callbacks,
        .connection = impl->connection,
        .state = impl->state,
        .bound = impl->bound,
        .state_known = impl->state_known,
        .terminal = impl->terminal,
        .closed = impl->closed};
    return true;
}

int cflow_io_cnet_session_adapter_destroy(
    cflow_io_cnet_session_adapter *adapter) {
    cflow_io_cnet_session_adapter_impl *impl;

    if (adapter == NULL || adapter->impl == NULL)
        return SALTS_EINVAL;
    impl = (cflow_io_cnet_session_adapter_impl *)adapter->impl;
    if (!impl->closed || impl->active_bridges != 0u ||
        impl->pending_credits != 0u)
        return SALTS_EBUSY;

    adapter->impl = NULL;
    free(impl->credit_order);
    free(impl->bridges);
    free(impl);
    return SALTS_OK;
}
