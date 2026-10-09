#include <cnet/cnet.h>
#include <cmeta/interface.h>
#include <salts/clock.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { ACE_CONNECT_TIMEOUT_MS = 7000u };

/*
 * ACE Acceptor-Connector + Strategy conformance: CNet is the sole I/O owner.
 * The CMeta Interface is a borrowed exact typed event handler, not a new
 * reactor, a global registry or a new service lifetime authority.
 */
#define ACE_STATE_METHODS(X, I) \
    X(I, FV1, void, observe, stateful, \
      &cmeta_type_void, CMETA_ABI_VOID, \
      (int, state, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR))

CMETA_INTERFACE(ace_state_handler, ACE_STATE_METHODS);

typedef struct ace_state_probe {
    unsigned connected;
    unsigned terminal;
    unsigned failed;
    unsigned callbacks;
    unsigned callbacks_after_owner_expiry;
    bool owner_live;
} ace_state_probe;

static void ace_state_observe_impl(void *self, int state) {
    ace_state_probe *probe = (ace_state_probe *)self;
    if (!probe->owner_live)
        ++probe->callbacks_after_owner_expiry;
    ++probe->callbacks;
    if (state == CNET_CONNECTION_CONNECTED)
        ++probe->connected;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED)
        ++probe->terminal;
    if (state == CNET_CONNECTION_FAILED)
        ++probe->failed;
}

CMETA_IMPLEMENTS(ace_state_handler, ace_state_counter_impl, 0u,
    .observe = ace_state_observe_impl);

typedef struct ace_observer_binding {
    ace_state_handler handler; /* borrowed; not a CNet-owned object */
    unsigned *callback_errors;
} ace_observer_binding;

static void ace_cnet_state_callback(void *user,
    cnet_connection connection, cnet_connection_state state,
    const cnet_error *error) {
    ace_observer_binding *binding = (ace_observer_binding *)user;
    (void)connection;
    if (error != NULL && binding->callback_errors != NULL)
        ++*binding->callback_errors;
    ace_state_handler_observe(&binding->handler, (int)state);
}

static native_io_backend_kind ace_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static cnet_client_config ace_client_config(void) {
    cnet_client_config config = {0};
    config.backend = ace_backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.event_capacity = 8u;
    config.max_send_bytes = 1024u;
    config.receive_buffer_bytes = 1024u;
    config.connect_timeout_ms = ACE_CONNECT_TIMEOUT_MS;
    return config;
}

static cnet_client connector_owner = {0};
static cnet_client acceptor_owner = {0};
static cnet_listener listener = {0};
static ace_state_probe outgoing = {0};
static ace_state_probe incoming = {0};
static ace_observer_binding outgoing_binding = {0};
static ace_observer_binding incoming_binding = {0};
static unsigned callback_errors = 0u;

static void ace_poll_one(cnet_client *owner) {
    size_t events = 0u;
    check_equal(cnet_client_poll(owner, 1u, &events), SALTS_OK);
}

suite("ACE Acceptor-Connector with CMeta typed Strategy") {
    before_each() {
        connector_owner = (cnet_client){0};
        acceptor_owner = (cnet_client){0};
        listener = (cnet_listener){0};
        outgoing = (ace_state_probe){0};
        incoming = (ace_state_probe){0};
        callback_errors = 0u;
        outgoing.owner_live = true;
        incoming.owner_live = true;
        outgoing_binding.handler =
            ace_state_counter_impl_as_ace_state_handler(&outgoing);
        incoming_binding.handler =
            ace_state_counter_impl_as_ace_state_handler(&incoming);
        outgoing_binding.callback_errors = &callback_errors;
        incoming_binding.callback_errors = &callback_errors;
    }

    after_each() {
        /* Callback handlers remain alive until both I/O owners finish stop. */
        if (connector_owner.impl != NULL) {
            check_warn(cnet_client_stop(
                &connector_owner, ACE_CONNECT_TIMEOUT_MS) == SALTS_OK);
            check_warn(cnet_client_destroy(&connector_owner) == SALTS_OK);
        }
        if (acceptor_owner.impl != NULL) {
            check_warn(cnet_client_stop(
                &acceptor_owner, ACE_CONNECT_TIMEOUT_MS) == SALTS_OK);
            check_warn(cnet_client_destroy(&acceptor_owner) == SALTS_OK);
        }
        if (listener.impl != NULL) {
            check_warn(cnet_listener_close(&listener) == SALTS_OK);
            check_warn(cnet_listener_destroy(&listener) == SALTS_OK);
        }
        outgoing.owner_live = false;
        incoming.owner_live = false;
        check_equal(outgoing.callbacks_after_owner_expiry, 0u);
        check_equal(incoming.callbacks_after_owner_expiry, 0u);
    }

    it("borrows typed callback Strategies across connect, accept and close") {
        const cnet_client_config config = ace_client_config();
        const cnet_listener_config listen_config = {
            ace_backend(), "127.0.0.1", 0u, 8u
        };
        const cnet_observer client_observer = {
            ace_cnet_state_callback, NULL, &outgoing_binding, NULL
        };
        const cnet_observer server_observer = {
            ace_cnet_state_callback, NULL, &incoming_binding, NULL
        };
        cnet_connect_options connect = {0};
        cnet_connect_options invalid = {0};
        cnet_connection outbound = {0};
        cnet_connection accepted = {0};
        cnet_connection rejected = {0};
        uint16_t port = 0u;
        uint64_t deadline;
        char uri[64];
        int ready = 0;
        bool accepted_done = false;

        check_true(cmeta_interface_desc_valid(ace_state_handler_interface()));
        check_true(ace_state_handler_valid(&outgoing_binding.handler));
        check_true(ace_state_handler_valid(&incoming_binding.handler));
        check_not_null(ace_state_handler_interface()->methods[0].abi);
        check_equal(ace_state_handler_interface()->methods[0].dispatch_arity,
                    1u);

        check_equal(cnet_client_init(&connector_owner, &config), SALTS_OK);
        check_equal(cnet_client_init(&acceptor_owner, &config), SALTS_OK);
        check_equal(cnet_listener_init(&listener, &listen_config), SALTS_OK);
        check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
        check_true(port != 0u);
        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned)port) > 0);

        /* Invalid admission must fail before emitting any callback or handle. */
        invalid.uri = uri;
        check_equal(cnet_connect(&connector_owner, &invalid, &rejected),
                    SALTS_EINVAL);
        check_equal(rejected.slot, 0u);
        check_equal(outgoing.callbacks, 0u);

        connect.uri = uri;
        connect.observer = client_observer;
        check_equal(cnet_connect(&connector_owner, &connect, &outbound), SALTS_OK);
        check_true(outbound.slot != 0u);

        deadline = cmeta_monotonic_ms() + ACE_CONNECT_TIMEOUT_MS;
        while (outgoing.connected == 0u) {
            ace_poll_one(&connector_owner);
            check_true(cmeta_monotonic_ms() < deadline);
        }
        check_equal(cnet_listener_wait(&listener, ACE_CONNECT_TIMEOUT_MS, &ready),
                    SALTS_OK);
        check_equal(ready, 1);

        deadline = cmeta_monotonic_ms() + ACE_CONNECT_TIMEOUT_MS;
        while (!accepted_done || incoming.connected == 0u) {
            ace_poll_one(&connector_owner);
            ace_poll_one(&acceptor_owner);
            if (!accepted_done) {
                const int status = cnet_listener_accept(
                    &listener, &acceptor_owner, &server_observer, &accepted);
                if (status == SALTS_OK)
                    accepted_done = true;
                else
                    check_equal(status, SALTS_ETIMEDOUT);
            }
            check_true(cmeta_monotonic_ms() < deadline);
        }

        check_equal(outgoing.connected, 1u);
        check_equal(incoming.connected, 1u);
        check_equal(outgoing.failed, 0u);
        check_equal(incoming.failed, 0u);
        check_equal(callback_errors, 0u);

        /* Closing Acceptor admission does not revoke admitted connections. */
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
        check_equal(outgoing.terminal, 0u);
        check_equal(incoming.terminal, 0u);

        check_equal(cnet_close(&connector_owner, outbound), SALTS_OK);
        check_equal(cnet_close(&acceptor_owner, accepted), SALTS_OK);
        deadline = cmeta_monotonic_ms() + ACE_CONNECT_TIMEOUT_MS;
        while (outgoing.terminal == 0u || incoming.terminal == 0u) {
            ace_poll_one(&connector_owner);
            ace_poll_one(&acceptor_owner);
            check_true(cmeta_monotonic_ms() < deadline);
        }
        check_equal(outgoing.callbacks_after_owner_expiry, 0u);
        check_equal(incoming.callbacks_after_owner_expiry, 0u);
    }
}
