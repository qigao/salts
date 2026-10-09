#include <cnet/cnet.h>
#include <cflow/cflow.h>
#include <salts/clock.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Out-of-tree exact Salts SDK test of the public CNet -> CFlow owner boundary.
 * No private headers, no duplicate queue/actor, no silent fallback.
 * The sibling installed SDK program qualifies the concrete Actor lifecycle;
 * the in-tree Half-Sync test covers the complete CNet -> Channel -> Actor path.
 */
typedef struct installed_handoff_state {
    cflow_channel *channel;
    bool net_live;
    unsigned client_connected;
    unsigned server_connected;
    unsigned client_terminal;
    unsigned server_terminal;
    unsigned borrowed_after_close;
    unsigned channel_accepted;
    unsigned channel_full;
    unsigned unexpected;
    unsigned seen;
    unsigned values;
    int last_value;
    unsigned errors;
} installed_handoff_state;

typedef struct installed_net_binding {
    installed_handoff_state *state;
    bool server;
} installed_net_binding;

static native_io_backend_kind installed_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static cnet_client_config installed_net_config(void) {
    cnet_client_config config = {0};
    config.backend = installed_backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.event_capacity = 8u;
    config.max_send_bytes = 1024u;
    config.receive_buffer_bytes = 1024u;
    config.connect_timeout_ms = 5000u;
    return config;
}

static void installed_on_state(void *user, cnet_connection connection,
    cnet_connection_state state, const cnet_error *error) {
    const installed_net_binding *binding = (const installed_net_binding *)user;
    installed_handoff_state *data = binding->state;
    (void)connection;
    if (!data->net_live) ++data->borrowed_after_close;
    if (state == CNET_CONNECTION_CONNECTED) {
        if (binding->server) ++data->server_connected;
        else ++data->client_connected;
    }
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
        if (binding->server) ++data->server_terminal;
        else ++data->client_terminal;
    }
    if (error != NULL || state == CNET_CONNECTION_FAILED)
        ++data->errors;
}

static void installed_on_receive(void *user, cnet_connection connection,
    const cnet_receive_view *view) {
    const installed_net_binding *binding = (const installed_net_binding *)user;
    installed_handoff_state *data = binding->state;
    size_t i;
    (void)connection;
    if (!data->net_live) ++data->borrowed_after_close;
    if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
        view->data == NULL || view->size == 0u) {
        ++data->unexpected;
        return;
    }
    for (i = 0u; i < view->size; ++i) {
        const int payload = (int)((const unsigned char *)view->data)[i];
        const cflow_channel_status status =
            cflow_channel_try_push(data->channel, &payload);
        ++data->seen;
        if (status == CFLOW_CHANNEL_OK) ++data->channel_accepted;
        else if (status == CFLOW_CHANNEL_FULL) ++data->channel_full;
        else ++data->unexpected;
    }
}

static bool installed_sink_value(void *user, const cmeta_type_desc *type,
    const void *value) {
    installed_handoff_state *state = (installed_handoff_state *)user;
    if (!cmeta_type_equal(type, &cmeta_type_int) || value == NULL)
        return false;
    state->last_value = *(const int *)value;
    ++state->values;
    return true;
}

static void installed_sink_error(void *user, const char *message) {
    installed_handoff_state *state = (installed_handoff_state *)user;
    if (message != NULL) ++state->errors;
}

static void installed_sink_done(void *user) { (void)user; }

int main(void) {
    const cnet_client_config cfg = installed_net_config();
    const cnet_listener_config lcfg = {
        installed_backend(), "127.0.0.1", 0u, 8u
    };
    static const unsigned char bytes[] = {4u, 5u};
    cnet_client connector = {0}, acceptor = {0};
    cnet_listener listener = {0};
    cnet_connection outgoing = {0}, incoming = {0};
    cnet_connect_options options = {0};
    cnet_observer outbound_observer = {0}, inbound_observer = {0};
    cflow_channel channel = {0};
    cflow_graph surface = {0}, graph = {0};
    cflow_publisher publisher = {0};
    cflow_scheduler scheduler = {0};
    cflow_subscription subscription = {0};
    cflow_subscriber_callbacks sink_callbacks = {0};
    cflow_subscriber sink;
    cflow_channel_stats stats = {0};
    installed_handoff_state state = {0};
    installed_net_binding client_binding = {&state, false};
    installed_net_binding server_binding = {&state, true};
    mem_buffer_t *buffer = NULL;
    char uri[64];
    uint16_t port = 0u;
    uint64_t deadline;
    size_t progress = 0u;
    int ready = 0;
    int result = 0;

    state.channel = &channel;
    state.net_live = true;
    graph.root = CMETA_INVALID_ID;
    sink_callbacks = (cflow_subscriber_callbacks){
        installed_sink_value, installed_sink_error, installed_sink_done, &state
    };
    sink = cflow_subscriber_from_callbacks(&sink_callbacks);
    if (!cmeta_interface_desc_valid(cflow_subscriber_interface()) ||
        !cflow_subscriber_valid(&sink) ||
        !cflow_channel_init(&channel, &cmeta_type_int, 1u)) {
        result = 1; goto finish;
    }
    cflow_graph_init(&surface, &cmeta_type_int);
    if (!cflow_graph_normalize(&graph, &surface) ||
        !cflow_scheduler_test_init(&scheduler) ||
        !cflow_publisher_from_channel(&publisher, &channel) ||
        !cflow_subscribe(&subscription, &graph, &publisher, &scheduler, &sink)) {
        result = 2; goto finish;
    }

    outbound_observer.on_state = installed_on_state;
    outbound_observer.user = &client_binding;
    inbound_observer.on_state = installed_on_state;
    inbound_observer.on_receive = installed_on_receive;
    inbound_observer.user = &server_binding;

    if (cnet_client_init(&connector, &cfg) != SALTS_OK ||
        cnet_client_init(&acceptor, &cfg) != SALTS_OK ||
        cnet_listener_init(&listener, &lcfg) != SALTS_OK ||
        cnet_listener_port(&listener, &port) != SALTS_OK ||
        port == 0u) { result = 3; goto finish; }
    if (snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                 (unsigned)port) <= 0) { result = 4; goto finish; }
    options.uri = uri;
    options.observer = outbound_observer;
    if (cnet_connect(&connector, &options, &outgoing) != SALTS_OK) {
        result = 5; goto finish;
    }
    deadline = cmeta_monotonic_ms() + UINT64_C(5000);
    while (state.client_connected == 0u &&
           cmeta_monotonic_ms() < deadline) {
        if (cnet_client_poll(&connector, 1u, &progress) != SALTS_OK) {
            result = 6; goto finish;
        }
    }
    if (state.client_connected != 1u ||
        cnet_listener_wait(&listener, 5000u, &ready) != SALTS_OK ||
        ready != 1 ||
        cnet_listener_accept(&listener, &acceptor, &inbound_observer,
                             &incoming) != SALTS_OK) {
        result = 7; goto finish;
    }
    deadline = cmeta_monotonic_ms() + UINT64_C(5000);
    while (state.server_connected == 0u &&
           cmeta_monotonic_ms() < deadline) {
        if (cnet_client_poll(&acceptor, 1u, &progress) != SALTS_OK) {
            result = 8; goto finish;
        }
    }
    if (state.server_connected != 1u ||
        cnet_receive(&acceptor, incoming, 2u) != SALTS_OK) {
        result = 9; goto finish;
    }

    buffer = mem_get_buffer(mem_global(), sizeof(bytes));
    if (buffer == NULL) { result = 10; goto finish; }
    memcpy(mem_buffer_data(buffer), bytes, sizeof(bytes));
    mem_set_used(buffer, sizeof(bytes));
    if (cnet_send_buffer(&connector, outgoing, buffer) != SALTS_OK) {
        result = 11; goto finish;
    }
    mem_buffer_release(buffer);
    buffer = NULL;

    deadline = cmeta_monotonic_ms() + UINT64_C(5000);
    while (state.seen < 2u && cmeta_monotonic_ms() < deadline) {
        if (cnet_client_poll(&connector, 1u, &progress) != SALTS_OK ||
            cnet_client_poll(&acceptor, 1u, &progress) != SALTS_OK) {
            result = 12; goto finish;
        }
    }
    if (state.seen != 2u || state.channel_accepted != 1u ||
        state.channel_full != 1u || state.unexpected != 0u ||
        state.errors != 0u ||
        !cflow_channel_get_stats(&channel, &stats) ||
        stats.capacity != 1u || stats.pending != 1u ||
        stats.rejected_full != 1u || stats.peak_pending != 1u) {
        result = 13; goto finish;
    }
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    if (state.values != 0u ||
        !cflow_subscription_request(&subscription, 1u)) {
        result = 14; goto finish;
    }
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    if (state.values != 1u || state.last_value != 4 ||
        cflow_subscription_outstanding_demand(&subscription) != 0u) {
        result = 15; goto finish;
    }

finish:
    if (buffer != NULL) mem_buffer_release(buffer);
    if (connector.impl != NULL && outgoing.slot != 0u)
        (void)cnet_close(&connector, outgoing);
    if (acceptor.impl != NULL && incoming.slot != 0u)
        (void)cnet_close(&acceptor, incoming);
    if (connector.impl != NULL) {
        if (cnet_client_stop(&connector, 5000u) != SALTS_OK && result == 0)
            result = 16;
        if (cnet_client_destroy(&connector) != SALTS_OK && result == 0)
            result = 17;
    }
    if (acceptor.impl != NULL) {
        if (cnet_client_stop(&acceptor, 5000u) != SALTS_OK && result == 0)
            result = 18;
        if (cnet_client_destroy(&acceptor) != SALTS_OK && result == 0)
            result = 19;
    }
    if (listener.impl != NULL) {
        if (cnet_listener_close(&listener) != SALTS_OK && result == 0)
            result = 20;
        if (cnet_listener_destroy(&listener) != SALTS_OK && result == 0)
            result = 21;
    }
    state.net_live = false;
    cflow_subscription_cancel(&subscription);
    cflow_subscription_close(&subscription);
    if (publisher.self != NULL) cflow_publisher_destroy(&publisher);
    if (channel.impl != NULL) {
        cflow_channel_close(&channel);
        cflow_channel_destroy(&channel);
    }
    if (cflow_scheduler_valid(&scheduler))
        cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    cflow_graph_destroy(&surface);
    if (state.borrowed_after_close != 0u && result == 0) result = 22;
    if (result != 0)
        fprintf(stderr, "ACE installed Half-Sync stage failed: %d\n", result);
    return result;
}
