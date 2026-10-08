#include <cflow/cnet_domain_route.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <tinytest.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET route_net_socket;
typedef int route_net_socklen;
#define ROUTE_NET_BAD_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int route_net_socket;
typedef socklen_t route_net_socklen;
#define ROUTE_NET_BAD_SOCKET (-1)
#endif

enum {
    ROUTE_TEST_OWNERS = 4,
    ROUTE_TEST_MESSAGES = 3,
    ROUTE_TEST_EVENT = 301,
    ROUTE_TEST_ACTION = 302,
    ROUTE_TEST_TIMEOUT_MS = 5000
};

typedef struct route_test_fixture {
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_actor_ref actor_ref;
    cflow_machine_action_binding action_binding;
    cflow_cnet_domain_route routes[ROUTE_TEST_OWNERS];
    cflow_cnet_domain_route_delivery last[ROUTE_TEST_OWNERS];
    const void *target_owner;
    int initial_state;
    size_t route_count;
    atomic_int actions;
    atomic_int values;
    atomic_int wrong_owner;
    atomic_int errors;
    atomic_int checksum;
    atomic_int duplicate_acks_rejected;
    atomic_int bad_tokens_rejected;
} route_test_fixture;

typedef struct route_source_fixture {
    cflow_cnet_domain_route *route;
    uint32_t owner_id;
    int messages;
    bool allow_full;
    atomic_int status;
    atomic_int full;
    atomic_int sent;
    atomic_int wrong_owner_rejected;
    atomic_bool started;
    atomic_bool done;
} route_source_fixture;

typedef struct route_reuse_source {
    cflow_cnet_domain_route *route;
    atomic_bool first_ready;
    atomic_bool release_second;
    atomic_bool done;
    atomic_int status;
    atomic_int before_ack_full;
} route_reuse_source;

static bool route_action(void *user, const void *state, const void *event,
                         void *target, void *observation,
                         const char **out_error) {
    route_test_fixture *f = (route_test_fixture *)user;
    const cflow_cnet_domain_route_delivery *delivery =
        (const cflow_cnet_domain_route_delivery *)event;
    cflow_cnet_domain_route_delivery forged;
    cflow_cnet_domain_route *route;
    cnet_receive_view view = {0};
    int byte_value;
    if (!f || !state || !event || !target || !observation || !out_error)
        return false;
    if (f->target_owner != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_owner, 1);
    if (delivery->source_owner == 0u ||
        delivery->source_owner > f->route_count) {
        *out_error = "unknown source owner";
        return false;
    }
    route = &f->routes[delivery->source_owner - 1u];
    forged = *delivery;
    forged.incarnation = 0u; /* Different/retired route incarnation. */
    if (cflow_cnet_domain_route_borrow(route, &forged, &view) ==
            SALTS_ENOENT)
        atomic_fetch_add(&f->bad_tokens_rejected, 1);
    else {
        *out_error = "stale route token was accepted";
        return false;
    }
    if (cflow_cnet_domain_route_borrow(route, delivery, &view) != SALTS_OK ||
        view.kind != CNET_MESSAGE_BYTES || view.size != 1u ||
        view.data == NULL) {
        *out_error = "owned payload was not retained across source callback";
        return false;
    }

    byte_value = ((const unsigned char *)view.data)[0];
    atomic_fetch_add(&f->checksum, byte_value);
    f->last[delivery->source_owner - 1u] = *delivery;
    /* No source CNet/NativeIO action occurs from this destination callback. */
    if (cflow_cnet_domain_route_acknowledge(route, delivery) != SALTS_OK) {
        *out_error = "ACK failed";
        return false;
    }
    if (cflow_cnet_domain_route_acknowledge(route, delivery) == SALTS_ENOENT)
        atomic_fetch_add(&f->duplicate_acks_rejected, 1);
    else {
        *out_error = "duplicate ACK was accepted";
        return false;
    }

    *(int *)target = *(const int *)state + 1;
    *(int *)observation = *(int *)target;
    atomic_fetch_add(&f->actions, 1);
    *out_error = NULL;
    return true;
}

static bool route_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    route_test_fixture *f = (route_test_fixture *)user;
    if (!f || !type || !value || !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    if (f->target_owner != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_owner, 1);
    atomic_fetch_add(&f->values, 1);
    return true;
}

static void route_sink_error(void *user, const char *message) {
    route_test_fixture *f = (route_test_fixture *)user;
    if (f && message) atomic_fetch_add(&f->errors, 1);
}

static void route_sink_done(void *user) {
    (void)user;
}

static bool route_test_init_mode(
    route_test_fixture *f, size_t route_count,
    size_t mailbox_capacity, size_t stage_capacity,
    bool defer_route_bindings) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {ROUTE_TEST_EVENT, &cflow_cnet_domain_route_delivery_type}
    };
    const cflow_machine_action actions[] = {{
        ROUTE_TEST_ACTION, &cmeta_type_int,
        ROUTE_TEST_EVENT, &cflow_cnet_domain_route_delivery_type,
        &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u
    }};
    const cflow_machine_transition transitions[] = {
        {10u, ROUTE_TEST_EVENT, 0u, ROUTE_TEST_ACTION, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u, events, 1u, NULL, 0u,
        actions, 1u, transitions, 1u
    };
    cflow_actor_config actor_config = {0};

    if (!f || route_count == 0u || route_count > ROUTE_TEST_OWNERS)
        return false;
    memset(f, 0, sizeof(*f));
    f->target_owner = cmeta_thread_current_token();
    f->route_count = route_count;
    f->initial_state = 0;
    f->action_binding = (cflow_machine_action_binding){
        ROUTE_TEST_ACTION, route_action, f
    };
    if (cflow_machine_build(&f->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_owner_init_with_capacity(
            &f->executor, 64u, NULL, NULL) ||
        !cflow_scheduler_owner_bind(&f->scheduler, &f->executor, 32u))
        return false;
    actor_config.machine = (cflow_machine_instance_config){
        &f->machine, &f->initial_state, &cmeta_type_int,
        NULL, 0u, &f->action_binding, 1u, mailbox_capacity,
        &f->executor
    };
    actor_config.scheduler = &f->scheduler;
    actor_config.callbacks = (cflow_subscriber_callbacks){
        route_sink_value, route_sink_error, route_sink_done, f
    };
    if (cflow_actor_init(&f->actor, &actor_config).status != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&f->actor, &f->actor_ref) ||
        cflow_actor_start(&f->actor) != CFLOW_ACTOR_OK)
        return false;

    if (defer_route_bindings) return true;
    for (size_t i = 0u; i < route_count; ++i) {
        const cflow_cnet_domain_route_config config = {
            .actor = &f->actor_ref,
            .event_id = ROUTE_TEST_EVENT,
            .source_owner = (uint32_t)(i + 1u),
            .connection = {(uint32_t)(i + 1u), 55u},
            .slot_capacity = stage_capacity,
            .max_receive_bytes = 16u
        };
        if (cflow_cnet_domain_route_init(&f->routes[i], &config) != SALTS_OK)
            return false;
    }
    return true;
}

static bool route_test_init(
    route_test_fixture *f, size_t route_count,
    size_t mailbox_capacity, size_t stage_capacity) {
    return route_test_init_mode(
        f, route_count, mailbox_capacity, stage_capacity, false);
}

static bool route_drive_until(route_test_fixture *f, int expected) {
    const uint64_t deadline = cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    while (atomic_load(&f->values) < expected &&
           cmeta_monotonic_ms() < deadline) {
        if (!cflow_executor_run_one(&f->executor))
            cmeta_sleep_ms(1u);
    }
    return atomic_load(&f->values) >= expected;
}

static void route_test_finish(route_test_fixture *f) {
    const uint64_t deadline = cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    if (f->actor.impl) {
        (void)cflow_actor_request_stop(&f->actor);
        while (cflow_actor_current_state(&f->actor) !=
                    CFLOW_ACTOR_STATE_STOPPED &&
               cflow_actor_current_state(&f->actor) !=
                    CFLOW_ACTOR_STATE_FAILED &&
               cmeta_monotonic_ms() < deadline) {
            if (!cflow_executor_run_one(&f->executor))
                cmeta_sleep_ms(1u);
        }
        cflow_actor_destroy(&f->actor);
    }
    for (size_t i = 0u; i < f->route_count; ++i) {
        cflow_cnet_domain_route_stats stats = {0};
        if (!f->routes[i].impl) continue;
        check_equal(cflow_cnet_domain_route_get_stats(
            &f->routes[i], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_false(stats.receive_credit_live);
        check_equal(
            cflow_cnet_domain_route_abort_after_quiescence(&f->routes[i]),
            SALTS_OK);
        /* Mailbox cancellation never calls a domain action/ACK. After
         * producer and Actor quiescence, the independent lease ledger must
         * nevertheless settle every retained byte and pending stage. */
        check_equal(cflow_cnet_domain_route_get_stats(
            &f->routes[i], &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.awaiting_ack, (size_t)0u);
        check_equal(stats.staged, (size_t)0u);
        check_equal(stats.retained_bytes, (size_t)0u);
        check_false(stats.receive_credit_live);
        check_equal(
            cflow_cnet_domain_route_destroy(&f->routes[i]), SALTS_OK);
    }
    cflow_actor_ref_release(&f->actor_ref);
    if (cflow_scheduler_valid(&f->scheduler))
        cflow_scheduler_destroy(&f->scheduler);
    if (cflow_executor_valid(&f->executor))
        cflow_executor_destroy(&f->executor);
    cflow_machine_destroy(&f->machine);
}

static void route_source_send(void *user) {
    route_source_fixture *source = (route_source_fixture *)user;
    int status;
    status = cflow_cnet_domain_route_bind_source(source->route);
    if (status != SALTS_OK) goto done;
    atomic_store(&source->started, true);

    for (int index = 0; index < source->messages; ++index) {
        cflow_cnet_domain_route_credit credit = {0};
        unsigned char byte = (unsigned char)(source->owner_id * 10u +
                                               (uint32_t)index);
        const cnet_receive_view view = {
            &byte, sizeof(byte), CNET_MESSAGE_BYTES
        };
        cflow_cnet_domain_route_delivery foreign_delivery = {0};
        cnet_receive_view borrowed = {0};

        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_OK) goto done;
        status = cflow_cnet_domain_route_receive(
            source->route, credit, &view);
        /* Mutate the original callback view: the target must observe the
         * preallocated copied payload, not a dangling CNet borrowed address. */
        byte = 0u;
        if (status == SALTS_ENOBUFS && source->allow_full)
            atomic_fetch_add(&source->full, 1);
        else if (status != SALTS_OK)
            goto done;
        foreign_delivery.incarnation = credit.incarnation;
        foreign_delivery.generation = credit.generation;
        foreign_delivery.slot = credit.slot;
        foreign_delivery.source_owner = credit.source_owner;
        foreign_delivery.connection = credit.connection;
        foreign_delivery.size = 1u;
        foreign_delivery.kind = CNET_MESSAGE_BYTES;
        if (cflow_cnet_domain_route_borrow(
                source->route, &foreign_delivery, &borrowed) == SALTS_EPERM)
            atomic_fetch_add(&source->wrong_owner_rejected, 1);
        else {
            status = SALTS_EPROTO;
            goto done;
        }
        atomic_fetch_add(&source->sent, 1);
    }
    status = cflow_cnet_domain_route_source_terminal(source->route);
done:
    atomic_store(&source->status, status);
    atomic_store(&source->done, true);
}

static void route_reuse_send(void *user) {
    route_reuse_source *source = (route_reuse_source *)user;
    int status = cflow_cnet_domain_route_bind_source(source->route);
    if (status != SALTS_OK) goto done;
    {
        cflow_cnet_domain_route_credit credit = {0};
        const unsigned char first = 41u;
        const cnet_receive_view view = {&first, 1u, CNET_MESSAGE_BYTES};
        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_OK) goto done;
        status = cflow_cnet_domain_route_receive(
            source->route, credit, &view);
        if (status != SALTS_OK) goto done;

        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_ENOBUFS) goto done;
        atomic_store(&source->before_ack_full, 1);
        atomic_store(&source->first_ready, true);

        const uint64_t deadline =
            cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
        while (!atomic_load(&source->release_second) &&
               cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        if (!atomic_load(&source->release_second)) {
            status = SALTS_ETIMEDOUT;
            goto done;
        }
        status = cflow_cnet_domain_route_reserve(source->route, &credit);
        if (status != SALTS_OK) goto done;
        {
            const unsigned char second = 42u;
            const cnet_receive_view next = {
                &second, 1u, CNET_MESSAGE_BYTES
            };
            status = cflow_cnet_domain_route_receive(
                source->route, credit, &next);
            if (status != SALTS_OK) goto done;
        }
    }
    status = cflow_cnet_domain_route_source_terminal(source->route);
done:
    atomic_store(&source->status, status);
    atomic_store(&source->done, true);
}

typedef struct route_terminal_source {
    cflow_cnet_domain_route *route;
    bool oversized;
    atomic_int status;
    atomic_bool done;
} route_terminal_source;

static void route_source_terminal_case(void *user) {
    route_terminal_source *source = (route_terminal_source *)user;
    cflow_cnet_domain_route_credit credit = {0};
    int status = cflow_cnet_domain_route_bind_source(source->route);
    if (status != SALTS_OK) goto done;
    status = cflow_cnet_domain_route_reserve(source->route, &credit);
    if (status != SALTS_OK) goto done;
    if (source->oversized) {
        const unsigned char data[17] = {0};
        const cnet_receive_view view = {
            data, sizeof(data), CNET_MESSAGE_BYTES
        };
        status = cflow_cnet_domain_route_receive(source->route, credit, &view);
        if (status != SALTS_EMSGSIZE) goto done;
    }
    status = cflow_cnet_domain_route_source_terminal(source->route);
    if (status != SALTS_OK) goto done;
    if (!source->oversized) {
        if (cflow_cnet_domain_route_cancel_credit(
                source->route, credit) != SALTS_ENOENT)
            status = SALTS_EPROTO;
    }
done:
    atomic_store(&source->status, status);
    atomic_store(&source->done, true);
}

/*
 * Real CNet qualification: each source CNet client/socket/poll loop lives
 * exclusively inside its own producer thread. The target owns the Machine,
 * shared SerialExecutor, Scheduler and every bounded route payload slot.
 * CNet terminal truth is delivered before the Actor processes/ACKs its event.
 */
typedef struct route_net_source {
    cflow_cnet_domain_route *route;
    cnet_client client;
    cnet_connection connection;
    route_net_socket listener;
    route_net_socket peer;
    cflow_cnet_domain_route_credit credit;
    const void *thread_owner;
    uint32_t owner_id;
    unsigned char sent_byte;
    atomic_bool connection_created;
    atomic_bool route_ready;
    atomic_bool completed;
    bool connected;
    bool terminal;
    bool wrong_owner;
    size_t received;
    int recv_status;
    int terminal_status;
    int result;
    bool allow_full;
} route_net_source;

static void route_net_close(route_net_socket socket_value) {
    if (socket_value == ROUTE_NET_BAD_SOCKET) return;
#if defined(_WIN32)
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
}

static bool route_net_would_block(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool route_net_nonblocking(route_net_socket fd) {
#if defined(_WIN32)
    u_long enabled = 1u;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 &&
           ((flags & O_NONBLOCK) != 0 ||
            fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
#endif
}

static native_io_backend_kind route_net_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return (native_io_backend_kind)0;
#endif
}

static int route_net_send_one(route_net_socket fd, unsigned char value) {
    const uint64_t deadline =
        cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    for (;;) {
#if defined(_WIN32)
        const int sent = send(fd, (const char *)&value, 1, 0);
        if (sent == SOCKET_ERROR) {
#elif defined(MSG_NOSIGNAL)
        const ssize_t sent = send(fd, &value, 1u, MSG_NOSIGNAL);
        if (sent < 0) {
#else
        const ssize_t sent = send(fd, &value, 1u, 0);
        if (sent < 0) {
#endif
            if (!route_net_would_block()) return SALTS_EIO;
            if (cmeta_monotonic_ms() >= deadline)
                return SALTS_ETIMEDOUT;
            cmeta_thread_yield();
            continue;
        }
        return sent == 1 ? SALTS_OK : SALTS_EIO;
    }
}

static void route_net_state(
    void *user, cnet_connection connection,
    cnet_connection_state state, const cnet_error *error) {
    route_net_source *s = (route_net_source *)user;
    (void)error;
    if (!s) return;
    if (s->thread_owner != cmeta_thread_current_token())
        s->wrong_owner = true;
    if (s->connection.slot != 0u &&
        (s->connection.slot != connection.slot ||
         s->connection.generation != connection.generation))
        s->wrong_owner = true;
    if (state == CNET_CONNECTION_CONNECTED) s->connected = true;
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) {
        s->terminal = true;
        /* The CNet owner, not the target Actor, settles native terminal
         * receive credit and seals just this connection's route. */
        if (s->route && s->route->impl)
            s->terminal_status =
                cflow_cnet_domain_route_source_terminal(s->route);
        if (state == CNET_CONNECTION_FAILED &&
            s->terminal_status == SALTS_OK)
            s->terminal_status =
                error != NULL && error->status != SALTS_OK
                    ? error->status : SALTS_EIO;
    }
}

static void route_net_receive(
    void *user, cnet_connection connection, const cnet_receive_view *view) {
    route_net_source *s = (route_net_source *)user;
    if (!s) return;
    if (s->thread_owner != cmeta_thread_current_token() ||
        s->connection.slot != connection.slot ||
        s->connection.generation != connection.generation)
        s->wrong_owner = true;
    if (!view || view->kind != CNET_MESSAGE_BYTES || view->size != 1u) {
        s->recv_status = SALTS_EPROTO;
        return;
    }
    s->recv_status = cflow_cnet_domain_route_receive(
        s->route, s->credit, view);
    ++s->received;
}

static void route_net_source_run(void *user) {
    route_net_source *s = (route_net_source *)user;
    struct sockaddr_in address = {0};
    route_net_socklen length = (route_net_socklen)sizeof(address);
    cnet_client_config config = {0};
    cnet_connect_options options = {0};
    uint16_t port;
    char uri[80];
    int result;
    const uint64_t deadline =
        cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;

    s->thread_owner = cmeta_thread_current_token();
    s->listener = ROUTE_NET_BAD_SOCKET;
    s->peer = ROUTE_NET_BAD_SOCKET;
    s->terminal_status = SALTS_OK;
#if defined(_WIN32)
    {
        WSADATA winsock;
        if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
            result = SALTS_EIO;
            goto done;
        }
    }
#endif
    s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->listener == ROUTE_NET_BAD_SOCKET) {
        result = SALTS_EIO;
        goto cleanup;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(0u);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s->listener, (const struct sockaddr *)&address,
             (route_net_socklen)sizeof(address)) != 0 ||
        getsockname(s->listener, (struct sockaddr *)&address,
                    &length) != 0 ||
        listen(s->listener, 2) != 0 ||
        !route_net_nonblocking(s->listener)) {
        result = SALTS_EIO;
        goto cleanup;
    }
    port = ntohs(address.sin_port);

    config.backend = route_net_backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 4u;
    config.event_capacity = 8u;
    config.max_send_bytes = 1024u;
    config.receive_buffer_bytes = 1024u;
    result = cnet_client_init(&s->client, &config);
    if (result != SALTS_OK) goto cleanup;

    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                   (unsigned)port);
    options.uri = uri;
    options.observer = (cnet_observer){
        .on_state = route_net_state,
        .on_receive = route_net_receive,
        .user = s
    };
    result = cnet_connect(&s->client, &options, &s->connection);
    if (result != SALTS_OK) goto cleanup;
    atomic_store_explicit(
        &s->connection_created, true, memory_order_release);

    while (!atomic_load_explicit(&s->route_ready, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline)
        cmeta_thread_yield();
    if (!atomic_load_explicit(&s->route_ready, memory_order_acquire)) {
        result = SALTS_ETIMEDOUT;
        goto cleanup;
    }
    result = cflow_cnet_domain_route_bind_source(s->route);
    if (result != SALTS_OK) goto cleanup;

    while ((!s->connected || s->peer == ROUTE_NET_BAD_SOCKET) &&
           cmeta_monotonic_ms() < deadline) {
        size_t events = 0u;
        if (s->peer == ROUTE_NET_BAD_SOCKET) {
            s->peer = accept(s->listener, NULL, NULL);
            if (s->peer != ROUTE_NET_BAD_SOCKET &&
                !route_net_nonblocking(s->peer)) {
                result = SALTS_EIO;
                goto cleanup;
            }
            if (s->peer == ROUTE_NET_BAD_SOCKET &&
                !route_net_would_block()) {
                result = SALTS_EIO;
                goto cleanup;
            }
        }
        result = cnet_client_poll(&s->client, 1u, &events);
        if (result != SALTS_OK) goto cleanup;
    }
    if (!s->connected || s->peer == ROUTE_NET_BAD_SOCKET) {
        result = SALTS_ETIMEDOUT;
        goto cleanup;
    }
    route_net_close(s->listener);
    s->listener = ROUTE_NET_BAD_SOCKET;

    /* Exactly one bounded CNet receive credit on this source owner. */
    result = cflow_cnet_domain_route_reserve(s->route, &s->credit);
    if (result != SALTS_OK) goto cleanup;
    result = cnet_receive(&s->client, s->connection, 1u);
    if (result != SALTS_OK) {
        (void)cflow_cnet_domain_route_cancel_credit(s->route, s->credit);
        goto cleanup;
    }
    result = route_net_send_one(s->peer, s->sent_byte);
    if (result != SALTS_OK) goto cleanup;
    while (s->received == 0u && !s->terminal &&
           cmeta_monotonic_ms() < deadline) {
        size_t events = 0u;
        result = cnet_client_poll(&s->client, 1u, &events);
        if (result != SALTS_OK) goto cleanup;
    }
    if (s->received != 1u) {
        result = SALTS_ETIMEDOUT;
        goto cleanup;
    }
    if (s->recv_status != SALTS_OK &&
        !(s->allow_full && s->recv_status == SALTS_ENOBUFS)) {
        result = s->recv_status;
        goto cleanup;
    }
    result = SALTS_OK;

cleanup:
    if (s->client.impl != NULL) {
        if (!s->terminal && s->connection.slot != 0u) {
            int close_status = cnet_close(&s->client, s->connection);
            if (result == SALTS_OK &&
                close_status != SALTS_OK &&
                close_status != SALTS_EALREADY &&
                close_status != SALTS_ENOENT)
                result = close_status;
        }
        while (!s->terminal && cmeta_monotonic_ms() < deadline) {
            size_t events = 0u;
            const int poll_status =
                cnet_client_poll(&s->client, 1u, &events);
            if (poll_status != SALTS_OK) {
                if (result == SALTS_OK) result = poll_status;
                break;
            }
        }
        if (!s->terminal && result == SALTS_OK)
            result = SALTS_ETIMEDOUT;
        {
            int stop_status =
                cnet_client_stop(&s->client, ROUTE_TEST_TIMEOUT_MS);
            if (stop_status == SALTS_ETIMEDOUT)
                stop_status = cnet_client_stop(
                    &s->client, ROUTE_TEST_TIMEOUT_MS);
            if (stop_status != SALTS_OK && result == SALTS_OK)
                result = stop_status;
            if (stop_status == SALTS_OK) {
                const int destroy_status =
                    cnet_client_destroy(&s->client);
                if (destroy_status != SALTS_OK && result == SALTS_OK)
                    result = destroy_status;
            }
        }
    }
    route_net_close(s->peer);
    route_net_close(s->listener);
    s->peer = ROUTE_NET_BAD_SOCKET;
    s->listener = ROUTE_NET_BAD_SOCKET;
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    if (s->wrong_owner && result == SALTS_OK)
        result = SALTS_EPERM;
    if (s->terminal_status != SALTS_OK && result == SALTS_OK)
        result = s->terminal_status;
done:
    s->result = result;
    atomic_store_explicit(&s->completed, true, memory_order_release);
}

static void route_net_multi_source_case(size_t owner_count,
                                        size_t mailbox_capacity) {
    route_test_fixture f;
    route_net_source sources[ROUTE_TEST_OWNERS] = {0};
    cmeta_thread_t threads[ROUTE_TEST_OWNERS] = {0};
    const uint64_t deadline =
        cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    size_t admitted = 0u;
    size_t staged = 0u;
    size_t full = 0u;
    int expected_checksum = 0;

    check_true(route_test_init_mode(
        &f, owner_count, mailbox_capacity, 2u, true));
    for (size_t i = 0u; i < owner_count; ++i) {
        sources[i].route = &f.routes[i];
        sources[i].owner_id = (uint32_t)(i + 1u);
        sources[i].sent_byte = (unsigned char)((i + 1u) * 10u);
        sources[i].allow_full = mailbox_capacity == 1u;
        expected_checksum += sources[i].sent_byte;
        check_equal(cmeta_thread_create(
            &threads[i], route_net_source_run, &sources[i]), 0);
    }

    for (size_t i = 0u; i < owner_count; ++i) {
        cflow_cnet_domain_route_config config = {0};
        while (!atomic_load_explicit(
                   &sources[i].connection_created,
                   memory_order_acquire) &&
               !atomic_load_explicit(
                   &sources[i].completed, memory_order_acquire) &&
               cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        check_true(atomic_load_explicit(
            &sources[i].connection_created, memory_order_acquire));
        config.actor = &f.actor_ref;
        config.event_id = ROUTE_TEST_EVENT;
        config.source_owner = (uint32_t)(i + 1u);
        config.connection = sources[i].connection;
        config.slot_capacity = 2u;
        config.max_receive_bytes = 16u;
        check_equal(cflow_cnet_domain_route_init(
            &f.routes[i], &config), SALTS_OK);
        atomic_store_explicit(
            &sources[i].route_ready, true, memory_order_release);
    }

    /* No target Actor quantum yet; real CNet callbacks race across four
     * independent NativeIO owner/client threads and fill only Actor's FIFO. */
    for (size_t i = 0u; i < owner_count; ++i) {
        check_equal(cmeta_thread_join(&threads[i]), 0);
        check_true(atomic_load(&sources[i].completed));
        check_equal(sources[i].result, SALTS_OK);
        check_true(sources[i].terminal);
        check_equal(sources[i].received, (size_t)1u);
        cflow_cnet_domain_route_stats stats = {0};
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[i], &stats), SALTS_OK);
        check_true(stats.source_terminal);
        check_false(stats.receive_credit_live);
        check_equal(stats.active_slots, (size_t)1u);
        admitted += stats.actor_accepted;
        full += stats.actor_full;
        staged += stats.staged;
    }
    if (mailbox_capacity == 1u) {
        check_equal(admitted, (size_t)1u);
        check_equal(staged, owner_count - 1u);
        check_equal(full, owner_count - 1u);
    } else {
        check_equal(admitted, owner_count);
        check_equal(staged, (size_t)0u);
        check_equal(full, (size_t)0u);
    }

    const uint64_t drive_deadline =
        cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;
    while (atomic_load(&f.values) < (int)owner_count &&
           cmeta_monotonic_ms() < drive_deadline) {
        /* The target advances only a finite Executor quantum before checking
         * retained route FULL. It never polls a foreign CNet client. */
        (void)cflow_executor_run_one(&f.executor);
        for (size_t i = 0u; i < owner_count; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            size_t worked = 0u;
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            if (stats.staged != 0u) {
                const int status = cflow_cnet_domain_route_retry_staged(
                    &f.routes[i], &worked);
                check_true(status == SALTS_OK || status == SALTS_ENOBUFS);
                check_true(worked <= (size_t)1u);
            }
        }
    }
    check_equal(atomic_load(&f.values), (int)owner_count);
    check_equal(atomic_load(&f.actions), (int)owner_count);
    check_equal(atomic_load(&f.checksum), expected_checksum);
    check_equal(atomic_load(&f.errors), 0);
    check_equal(atomic_load(&f.wrong_owner), 0);
    for (size_t i = 0u; i < owner_count; ++i) {
        cflow_cnet_domain_route_stats stats = {0};
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[i], &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.acknowledged, (uint64_t)1u);
        check_equal(stats.retained_bytes, (size_t)0u);
    }
    route_test_finish(&f);
}

suite("CNet cross-owner domain Actor retained lease routing") {
    it("moves real TCP receives from two independent CNet owners to one Actor owner") {
        route_net_multi_source_case(2u, 8u);
    }

    it("retains real TCP views from four CNet owners during Actor FULL and source close") {
        route_net_multi_source_case(4u, 1u);
    }

    it("routes four concurrent CNet source owners to one target Actor without a second FIFO") {
        route_test_fixture f;
        route_source_fixture sources[ROUTE_TEST_OWNERS] = {0};
        cmeta_thread_t threads[ROUTE_TEST_OWNERS] = {0};
        const int expected = ROUTE_TEST_OWNERS * ROUTE_TEST_MESSAGES;

        check_true(route_test_init(&f, ROUTE_TEST_OWNERS, 32u, 4u));
        for (size_t i = 0u; i < ROUTE_TEST_OWNERS; ++i) {
            sources[i].route = &f.routes[i];
            sources[i].owner_id = (uint32_t)(i + 1u);
            sources[i].messages = ROUTE_TEST_MESSAGES;
            check_equal(cmeta_thread_create(
                &threads[i], route_source_send, &sources[i]), 0);
        }
        for (size_t i = 0u; i < ROUTE_TEST_OWNERS; ++i) {
            check_equal(cmeta_thread_join(&threads[i]), 0);
            check_true(atomic_load(&sources[i].done));
            check_equal(atomic_load(&sources[i].status), SALTS_OK);
            check_equal(atomic_load(&sources[i].sent), ROUTE_TEST_MESSAGES);
            check_equal(atomic_load(&sources[i].full), 0);
            check_equal(atomic_load(&sources[i].wrong_owner_rejected),
                        ROUTE_TEST_MESSAGES);
            cflow_cnet_domain_route_stats stats = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            check_true(stats.source_bound);
            check_true(stats.sealed);
            check_true(stats.source_terminal);
            check_equal(stats.active_slots, (size_t)ROUTE_TEST_MESSAGES);
            check_equal(stats.awaiting_ack, (size_t)ROUTE_TEST_MESSAGES);
            check_equal(stats.actor_accepted, (uint64_t)ROUTE_TEST_MESSAGES);
            check_equal(stats.actor_full, (uint64_t)0u);
        }

        check_true(route_drive_until(&f, expected));
        check_equal(atomic_load(&f.actions), expected);
        check_equal(atomic_load(&f.values), expected);
        check_equal(atomic_load(&f.checksum), 312);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(atomic_load(&f.wrong_owner), 0);
        check_equal(atomic_load(&f.bad_tokens_rejected), expected);
        check_equal(atomic_load(&f.duplicate_acks_rejected), expected);

        for (size_t i = 0u; i < ROUTE_TEST_OWNERS; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            cnet_receive_view wrong = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            check_equal(stats.active_slots, (size_t)0u);
            check_equal(stats.retained_bytes, (size_t)0u);
            check_equal(stats.acknowledged, (uint64_t)ROUTE_TEST_MESSAGES);
            check_equal(cflow_cnet_domain_route_borrow(
                &f.routes[i], &f.last[i], &wrong), SALTS_ENOENT);
            check_equal(cflow_cnet_domain_route_acknowledge(
                &f.routes[i], &f.last[i]), SALTS_ENOENT);
        }
        route_test_finish(&f);
    }

    it("preserves two remote owners when the target Mailbox is full") {
        route_test_fixture f;
        route_source_fixture sources[2] = {0};
        cmeta_thread_t threads[2] = {0};
        size_t full = 0u;
        size_t accepted = 0u;

        check_true(route_test_init(&f, 2u, 1u, 2u));
        for (size_t i = 0u; i < 2u; ++i) {
            sources[i].route = &f.routes[i];
            sources[i].owner_id = (uint32_t)(i + 1u);
            sources[i].messages = 1;
            sources[i].allow_full = true;
            check_equal(cmeta_thread_create(
                &threads[i], route_source_send, &sources[i]), 0);
        }
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(cmeta_thread_join(&threads[i]), 0);
            check_equal(atomic_load(&sources[i].status), SALTS_OK);
            cflow_cnet_domain_route_stats stats = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            full += stats.actor_full;
            accepted += stats.actor_accepted;
            check_equal(stats.active_slots, (size_t)1u);
            check_true(stats.sealed);
        }
        check_equal(full, (size_t)1u);
        check_equal(accepted, (size_t)1u);
        check_equal(atomic_load(&f.actions), 0);

        /* A bounded retry while the mailbox is still full must preserve the
         * payload and return FULL without spinning or silently dropping it. */
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            size_t work = 55u;
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            if (stats.staged != 0u) {
                check_equal(cflow_cnet_domain_route_retry_staged(
                    &f.routes[i], &work), SALTS_ENOBUFS);
                check_equal(work, (size_t)0u);
            }
        }
        check_true(route_drive_until(&f, 1));
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            size_t work = 0u;
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            if (stats.staged != 0u) {
                check_equal(cflow_cnet_domain_route_retry_staged(
                    &f.routes[i], &work), SALTS_OK);
                check_equal(work, (size_t)1u);
            }
        }
        check_true(route_drive_until(&f, 2));
        check_equal(atomic_load(&f.actions), 2);
        check_equal(atomic_load(&f.checksum), 30);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(atomic_load(&f.wrong_owner), 0);
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_cnet_domain_route_stats stats = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.routes[i], &stats), SALTS_OK);
            check_equal(stats.active_slots, (size_t)0u);
            check_equal(stats.acknowledged, (uint64_t)1u);
        }
        route_test_finish(&f);
    }

    it("isolates an oversized receive on one owner without sealing neighbors") {
        route_test_fixture f;
        route_terminal_source oversized = {0};
        route_source_fixture healthy = {0};
        cmeta_thread_t threads[2] = {0};
        cflow_cnet_domain_route_stats bad_stats = {0};
        cflow_cnet_domain_route_stats good_stats = {0};

        check_true(route_test_init(&f, 2u, 4u, 2u));
        oversized.route = &f.routes[0];
        oversized.oversized = true;
        healthy.route = &f.routes[1];
        healthy.owner_id = 2u;
        healthy.messages = 1;
        check_equal(cmeta_thread_create(
            &threads[0], route_source_terminal_case, &oversized), 0);
        check_equal(cmeta_thread_create(
            &threads[1], route_source_send, &healthy), 0);
        check_equal(cmeta_thread_join(&threads[0]), 0);
        check_equal(cmeta_thread_join(&threads[1]), 0);
        check_equal(atomic_load(&oversized.status), SALTS_OK);
        check_equal(atomic_load(&healthy.status), SALTS_OK);
        check_true(atomic_load(&oversized.done));
        check_true(atomic_load(&healthy.done));

        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &bad_stats), SALTS_OK);
        check_equal(bad_stats.fatal_status, SALTS_EMSGSIZE);
        check_true(bad_stats.sealed);
        check_true(bad_stats.source_terminal);
        check_equal(bad_stats.actor_accepted, (uint64_t)0u);
        check_equal(bad_stats.active_slots, (size_t)1u);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[1], &good_stats), SALTS_OK);
        check_equal(good_stats.fatal_status, SALTS_OK);
        check_equal(good_stats.actor_accepted, (uint64_t)1u);
        check_true(route_drive_until(&f, 1));
        check_equal(atomic_load(&f.actions), 1);
        check_equal(atomic_load(&f.checksum), 20);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[1], &good_stats), SALTS_OK);
        check_equal(good_stats.acknowledged, (uint64_t)1u);
        check_equal(good_stats.active_slots, (size_t)0u);
        /* Target-owned abort only happens after Actor and source quiescence
         * in route_test_finish(), preserving the one failed source lease. */
        route_test_finish(&f);
    }

    it("retires a source CNet credit on terminal without fabricating an Actor ACK") {
        route_test_fixture f;
        route_terminal_source terminal = {0};
        cmeta_thread_t worker = NULL;
        cflow_cnet_domain_route_stats stats = {0};

        check_true(route_test_init(&f, 1u, 2u, 1u));
        terminal.route = &f.routes[0];
        check_equal(cmeta_thread_create(
            &worker, route_source_terminal_case, &terminal), 0);
        check_equal(cmeta_thread_join(&worker), 0);
        check_equal(atomic_load(&terminal.status), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_true(stats.source_terminal);
        check_false(stats.receive_credit_live);
        check_equal(stats.reserved_credits, (uint64_t)1u);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.actor_accepted, (uint64_t)0u);
        check_equal(stats.acknowledged, (uint64_t)0u);
        check_equal(atomic_load(&f.actions), 0);
        route_test_finish(&f);
    }

    it("keeps credit bounded across source-owner ACK reuse and terminal") {
        route_test_fixture f;
        route_reuse_source source = {0};
        cmeta_thread_t thread = NULL;
        cflow_cnet_domain_route_stats stats = {0};
        cflow_cnet_domain_route_credit wrong_owner_credit = {0};
        const uint64_t deadline = cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;

        check_true(route_test_init(&f, 1u, 2u, 1u));
        source.route = &f.routes[0];
        check_equal(cmeta_thread_create(
            &thread, route_reuse_send, &source), 0);
        while (!atomic_load(&source.first_ready) &&
               !atomic_load(&source.done) &&
               cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        check_true(atomic_load(&source.first_ready));
        check_equal(atomic_load(&source.before_ack_full), 1);
        check_equal(cflow_cnet_domain_route_reserve(
            &f.routes[0], &wrong_owner_credit), SALTS_EPERM);

        check_true(route_drive_until(&f, 1));
        atomic_store(&source.release_second, true);
        check_equal(cmeta_thread_join(&thread), 0);
        check_equal(atomic_load(&source.status), SALTS_OK);
        check_true(route_drive_until(&f, 2));
        check_equal(atomic_load(&f.checksum), 83);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_true(stats.source_terminal);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.reserved_credits, (uint64_t)2u);
        check_equal(stats.acknowledged, (uint64_t)2u);
        route_test_finish(&f);
    }

    it("stops before business execution and explicitly settles retained payload") {
        route_test_fixture f;
        route_source_fixture source = {0};
        cmeta_thread_t thread = NULL;
        cflow_cnet_domain_route_stats stats = {0};
        const uint64_t deadline =
            cmeta_monotonic_ms() + ROUTE_TEST_TIMEOUT_MS;

        check_true(route_test_init(&f, 1u, 2u, 1u));
        source.route = &f.routes[0];
        source.owner_id = 1u;
        source.messages = 1;
        check_equal(cmeta_thread_create(
            &thread, route_source_send, &source), SALTS_OK);
        check_equal(cmeta_thread_join(&thread), SALTS_OK);
        check_equal(atomic_load(&source.status), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_true(stats.sealed);
        check_true(stats.source_terminal);
        check_equal(stats.awaiting_ack, (size_t)1u);
        check_equal(stats.acknowledged, (uint64_t)0u);
        check_equal(atomic_load(&f.actions), 0);

        /* Request stop before any target executor quantum. This must not
         * manufacture a business ACK or free the independent lease ledger. */
        check_equal(cflow_actor_request_stop(&f.actor), CFLOW_ACTOR_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_equal(stats.awaiting_ack, (size_t)1u);
        check_equal(stats.acknowledged, (uint64_t)0u);

        while (cflow_actor_current_state(&f.actor) !=
                   CFLOW_ACTOR_STATE_STOPPED &&
               cflow_actor_current_state(&f.actor) !=
                   CFLOW_ACTOR_STATE_FAILED &&
               cmeta_monotonic_ms() < deadline) {
            if (!cflow_executor_run_one(&f.executor))
                cmeta_sleep_ms(1u);
        }
        check_equal(cflow_actor_current_state(&f.actor),
                    CFLOW_ACTOR_STATE_STOPPED);
        check_equal(atomic_load(&f.actions), 0);
        check_equal(atomic_load(&f.values), 0);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_equal(stats.acknowledged, (uint64_t)0u);
        check_equal(stats.awaiting_ack, (size_t)1u);

        /* Stop and source terminal are quiescent. Only this explicit
         * post-quiescence abort may release canceled mailbox payloads. */
        check_equal(cflow_cnet_domain_route_abort_after_quiescence(
            &f.routes[0]), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.routes[0], &stats), SALTS_OK);
        check_equal(stats.acknowledged, (uint64_t)0u);
        check_equal(stats.abandoned, (uint64_t)1u);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.awaiting_ack, (size_t)0u);
        check_equal(stats.retained_bytes, (size_t)0u);
        route_test_finish(&f);
    }

}
