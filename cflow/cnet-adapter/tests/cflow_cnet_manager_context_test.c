#include <cflow/cnet_manager_context.h>
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
typedef SOCKET manager_socket;
#define MANAGER_BAD_SOCKET INVALID_SOCKET
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int manager_socket;
#define MANAGER_BAD_SOCKET (-1)
#endif

static void manager_socket_close(manager_socket fd) {
    if (fd == MANAGER_BAD_SOCKET) return;
#if defined(_WIN32)
    (void)closesocket(fd);
#else
    (void)close(fd);
#endif
}
static bool manager_would_block(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
static bool manager_nonblocking(manager_socket fd) {
#if defined(_WIN32)
    u_long enabled = 1u;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
static int manager_send_byte(manager_socket fd, unsigned char byte) {
    const uint64_t deadline = cmeta_monotonic_ms() + 5000u;
    for (;;) {
#if defined(_WIN32)
        int n = send(fd, (const char *)&byte, 1, 0);
        if (n == SOCKET_ERROR) {
#elif defined(MSG_NOSIGNAL)
        ssize_t n = send(fd, &byte, 1u, MSG_NOSIGNAL);
        if (n < 0) {
#else
        ssize_t n = send(fd, &byte, 1u, 0);
        if (n < 0) {
#endif
            if (!manager_would_block()) return SALTS_EIO;
            if (cmeta_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
            cmeta_thread_yield();
            continue;
        }
        return n == 1 ? SALTS_OK : SALTS_EIO;
    }
}

enum {
    MANAGER_LEASE_TIMEOUT_MS = 5000u,
    MANAGER_LEASE_EVENT = 501u,
    MANAGER_LEASE_ACTION = 502u
};

typedef struct manager_lease_fixture {
    /* Native connection + CNetManager always belong to source/main Owner. */
    cnet_client client;
    cnet_listener listener;
    cnet_manager manager;
    cnet_managed_connection managed;
    cnet_connection connection;
    cflow_cnet_manager_context guard;
    cflow_cnet_domain_route *associated[2];
    cmeta_thread_t target_thread;

    /* Actor and route belong to a DIFFERENT target OS thread. */
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_actor_ref ref;
    cflow_machine_action_binding binding;
    cflow_cnet_domain_route route;
    cflow_cnet_domain_route route2;
    int initial_state;

    atomic_bool connected;
    atomic_bool terminal;
    atomic_bool target_ready;
    atomic_bool target_run;
    atomic_bool first_ack;
    atomic_bool target_stop;
    atomic_bool target_done;
    atomic_int target_status;
    atomic_int source_terminal_status;
    atomic_int action_count;
    atomic_int values;
    atomic_int sink_errors;
    atomic_int checksum;
    atomic_int wrong_owner_releases;
    atomic_int wrong_route_binding;
    atomic_int wakes;
    atomic_int recycled;
    atomic_int receives;
    atomic_int receive_status;
    cflow_cnet_domain_route_credit receive_credit;
} manager_lease_fixture;

static native_io_backend_kind manager_lease_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static void manager_lease_state(
    void *user, cnet_connection connection,
    cnet_connection_state state, const cnet_error *error) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    (void)error;
    if (!f || f->connection.slot != connection.slot ||
        f->connection.generation != connection.generation)
        return;
    if (state == CNET_CONNECTION_CONNECTED)
        atomic_store(&f->connected, true);
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) {
        if (f->route.impl) {
            int status = cflow_cnet_domain_route_source_terminal(&f->route);
            if (status == SALTS_OK && f->route2.impl)
                status = cflow_cnet_domain_route_source_terminal(&f->route2);
            atomic_store(&f->source_terminal_status, status);
        }
        atomic_store(&f->terminal, true);
    }
}

static void manager_lease_receive(
    void *user, cnet_connection connection, const cnet_receive_view *view) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    if (!f || connection.slot != f->connection.slot ||
        connection.generation != f->connection.generation ||
        !view || view->kind != CNET_MESSAGE_BYTES || view->size != 1u) {
        if (f) atomic_store(&f->receive_status, SALTS_EPROTO);
        return;
    }
    int index = atomic_load(&f->receives);
    cflow_cnet_domain_route *route = index == 0 ? &f->route : &f->route2;
    atomic_store(&f->receive_status,
        cflow_cnet_domain_route_receive(route, f->receive_credit, view));
    atomic_fetch_add(&f->receives, 1);
}

static void manager_lease_recycle(void *user) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    if (f) atomic_fetch_add(&f->recycled, 1);
}

static void manager_lease_wake(void *user) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    /* Signal-only; safe from target action and does not enter manager. */
    if (f) atomic_fetch_add(&f->wakes, 1);
}

static bool manager_lease_action(
    void *user, const void *state, const void *event,
    void *target, void *observation, const char **error) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    const cflow_cnet_domain_route_delivery *delivery =
        (const cflow_cnet_domain_route_delivery *)event;
    cnet_receive_view retained = {0};
    bool released = false;
    if (!f || !state || !event || !target || !observation || !error)
        return false;
    cflow_cnet_domain_route *route = &f->route;
    if (cflow_cnet_domain_route_borrow(route, delivery, &retained) != SALTS_OK) {
        route = &f->route2;
        if (cflow_cnet_domain_route_borrow(route, delivery, &retained) != SALTS_OK) {
            *error = "unknown route delivery";
            return false;
        }
    }
    if (retained.size != 1u || retained.kind != CNET_MESSAGE_BYTES) {
        *error = "cross-owner business payload missing";
        return false;
    }
    atomic_fetch_add(&f->checksum,
                     ((const unsigned char *)retained.data)[0]);
    if (cflow_cnet_domain_route_acknowledge(
            route, delivery) != SALTS_OK) {
        *error = "business ACK rejected";
        return false;
    }
    if (cflow_cnet_manager_context_poll_release(
            &f->guard, &released) == SALTS_EPERM && !released)
        atomic_fetch_add(&f->wrong_owner_releases, 1);
    else {
        *error = "target illegally released source CNetManager context";
        return false;
    }
    if (cflow_cnet_manager_context_notify_settled(&f->guard) != SALTS_OK ||
        cflow_cnet_manager_context_notify_settled(&f->guard) != SALTS_OK) {
        *error = "target ACK completion notification rejected";
        return false;
    }
    *(int *)target = *(const int *)state + 1;
    *(int *)observation = *(int *)target;
    if (atomic_fetch_add(&f->action_count, 1) == 0) {
        /* First ACK must not implicitly settle another route. */
        atomic_store(&f->target_run, false);
        atomic_store(&f->first_ack, true);
    }
    *error = NULL;
    return true;
}

static bool manager_lease_on_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    if (!f || !type || !value ||
        !cmeta_type_equal(type, &cmeta_type_int)) return false;
    atomic_fetch_add(&f->values, 1);
    return true;
}

static void manager_lease_on_error(void *user, const char *error) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    if (f && error) atomic_fetch_add(&f->sink_errors, 1);
}

static void manager_lease_on_done(void *user) {
    (void)user;
}

static int manager_lease_target_init(manager_lease_fixture *f) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {MANAGER_LEASE_EVENT, &cflow_cnet_domain_route_delivery_type}
    };
    const cflow_machine_action actions[] = {{
        MANAGER_LEASE_ACTION, &cmeta_type_int, MANAGER_LEASE_EVENT,
        &cflow_cnet_domain_route_delivery_type, &cmeta_type_int,
        CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u
    }};
    const cflow_machine_transition transitions[] = {
        {10u, MANAGER_LEASE_EVENT, 0u, MANAGER_LEASE_ACTION, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u, events, 1u, NULL, 0u,
        actions, 1u, transitions, 1u
    };
    cflow_actor_config actor_cfg = {0};
    cflow_cnet_domain_route_config route_cfg = {0};

    f->initial_state = 0;
    f->binding = (cflow_machine_action_binding){
        MANAGER_LEASE_ACTION, manager_lease_action, f
    };
    if (cflow_machine_build(&f->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_owner_init_with_capacity(
            &f->executor, 16u, NULL, NULL) ||
        !cflow_scheduler_owner_bind(&f->scheduler, &f->executor, 8u))
        return SALTS_EPROTO;
    actor_cfg.machine = (cflow_machine_instance_config){
        &f->machine, &f->initial_state, &cmeta_type_int,
        NULL, 0u, &f->binding, 1u, 2u, &f->executor
    };
    actor_cfg.scheduler = &f->scheduler;
    actor_cfg.callbacks = (cflow_subscriber_callbacks){
        manager_lease_on_value, manager_lease_on_error,
        manager_lease_on_done, f
    };
    if (cflow_actor_init(&f->actor, &actor_cfg).status != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&f->actor, &f->ref) ||
        cflow_actor_start(&f->actor) != CFLOW_ACTOR_OK)
        return SALTS_EPROTO;
    route_cfg = (cflow_cnet_domain_route_config){
        .actor = &f->ref,
        .event_id = MANAGER_LEASE_EVENT,
        .source_owner = 1u,
        .connection = f->connection,
        .slot_capacity = 1u,
        .max_receive_bytes = 16u
    };
    if (cflow_cnet_domain_route_init(&f->route, &route_cfg) != SALTS_OK ||
        cflow_cnet_domain_route_init(&f->route2, &route_cfg) != SALTS_OK)
        return SALTS_EPROTO;
    /* Wrong side cannot read or impersonate the bound CNet source owner. */
    {
        cnet_connection connection = {0};
        uint32_t source = 0u;
        if (cflow_cnet_domain_route_get_source_binding(
                &f->route, &connection, &source) == SALTS_EPERM)
            atomic_fetch_add(&f->wrong_route_binding, 1);
    }
    return SALTS_OK;
}

static void manager_lease_target_finish(manager_lease_fixture *f) {
    const uint64_t deadline =
        cmeta_monotonic_ms() + MANAGER_LEASE_TIMEOUT_MS;
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
    if (f->route.impl &&
        cflow_cnet_domain_route_destroy(&f->route) != SALTS_OK)
        atomic_store(&f->target_status, SALTS_EBUSY);
    if (f->route2.impl &&
        cflow_cnet_domain_route_destroy(&f->route2) != SALTS_OK)
        atomic_store(&f->target_status, SALTS_EBUSY);
    cflow_actor_ref_release(&f->ref);
    if (cflow_scheduler_valid(&f->scheduler))
        cflow_scheduler_destroy(&f->scheduler);
    if (cflow_executor_valid(&f->executor))
        cflow_executor_destroy(&f->executor);
    cflow_machine_destroy(&f->machine);
}

static void manager_lease_target_thread(void *user) {
    manager_lease_fixture *f = (manager_lease_fixture *)user;
    int status = manager_lease_target_init(f);
    atomic_store(&f->target_status, status);
    atomic_store_explicit(&f->target_ready, true, memory_order_release);
    if (status != SALTS_OK) return;
    while (!atomic_load_explicit(&f->target_stop, memory_order_acquire)) {
        if (atomic_load_explicit(&f->target_run, memory_order_acquire)) {
            if (!cflow_executor_run_one(&f->executor))
                cmeta_sleep_ms(1u);
        } else {
            cmeta_sleep_ms(1u);
        }
    }
    manager_lease_target_finish(f);
    atomic_store(&f->target_done, true);
}

static bool manager_lease_wait_bool(atomic_bool *flag) {
    const uint64_t deadline =
        cmeta_monotonic_ms() + MANAGER_LEASE_TIMEOUT_MS;
    while (!atomic_load_explicit(flag, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline)
        cmeta_sleep_ms(1u);
    return atomic_load_explicit(flag, memory_order_acquire);
}

spec("CNetManager held context across cross-owner Actor ACK") {
    it("keeps one managed context until both independent route ACKs") {
        manager_lease_fixture f = {0};
        cnet_client_config config = {
            .backend = manager_lease_backend(),
            .connection_capacity = 2u,
            .command_capacity = 8u,
            .request_capacity = 8u,
            .completion_batch_capacity = 4u,
            .event_capacity = 8u,
            .max_send_bytes = 1024u,
            .receive_buffer_bytes = 1024u
        };
        manager_socket listen_fd = MANAGER_BAD_SOCKET;
        manager_socket peer_fd = MANAGER_BAD_SOCKET;
        struct sockaddr_in address = {0};
#if defined(_WIN32)
        int address_length = (int)sizeof(address);
#else
        socklen_t address_length = (socklen_t)sizeof(address);
#endif
        cnet_manager_config mconfig = {0};
        cnet_manager_attachment attachment = {0};
        cflow_cnet_manager_context_config context_config = {0};
        cflow_cnet_manager_context_stats guard_stats = {0};
        cnet_manager_snapshot manager_stats = {0};
        cflow_cnet_domain_route_credit credit = {0};
        cflow_cnet_domain_route_stats route_stats = {0};
        cnet_connect_options options = {0};
        cnet_managed_connection stale_managed = {0};
        cnet_connection source_connection = {0};
        uint32_t source_id = 0u;
        uint16_t port = 0u;
        char uri[80] = {0};
        size_t work = 0u;
        bool released = true;
        const uint64_t deadline =
            cmeta_monotonic_ms() + MANAGER_LEASE_TIMEOUT_MS;

        check_equal(cnet_client_init(&f.client, &config), SALTS_OK);
        listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        check_true(listen_fd != MANAGER_BAD_SOCKET);
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(0u);
        check_equal(bind(listen_fd, (const struct sockaddr *)&address,
                         (int)sizeof(address)), 0);
        check_equal(getsockname(listen_fd, (struct sockaddr *)&address,
                                &address_length), 0);
        check_equal(listen(listen_fd, 2), 0);
        check_true(manager_nonblocking(listen_fd));
        port = ntohs(address.sin_port);
        check_true(port != 0u);
        (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                       (unsigned)port);
        mconfig = (cnet_manager_config){
            sizeof(mconfig), CNET_MANAGER_VERSION, &f.client, 2u, 1u
        };
        check_equal(cnet_manager_init(&f.manager, &mconfig), SALTS_OK);
        attachment.observer = (cnet_observer){
            .on_state = manager_lease_state,
            .on_receive = manager_lease_receive,
            .user = &f
        };
        attachment.on_recycle = manager_lease_recycle;
        attachment.hold_context = true;
        check_equal(cnet_manager_reserve(
            &f.manager, &attachment, &f.managed), SALTS_OK);
        options.uri = uri;
        check_equal(cnet_manager_connect(
            &f.manager, f.managed, &options, &f.connection), SALTS_OK);
        while ((!atomic_load(&f.connected) || peer_fd == MANAGER_BAD_SOCKET) &&
               cmeta_monotonic_ms() < deadline) {
            size_t events = 0u;
            if (peer_fd == MANAGER_BAD_SOCKET) {
                peer_fd = accept(listen_fd, NULL, NULL);
                if (peer_fd == MANAGER_BAD_SOCKET)
                    check_true(manager_would_block());
            }
            check_equal(cnet_client_poll(&f.client, 1u, &events), SALTS_OK);
        }
        check_true(peer_fd != MANAGER_BAD_SOCKET);
        check_true(atomic_load(&f.connected));

        check_equal(cmeta_thread_create(
            &f.target_thread, manager_lease_target_thread, &f), SALTS_OK);
        check_true(manager_lease_wait_bool(&f.target_ready));
        check_equal(atomic_load(&f.target_status), SALTS_OK);
        check_equal(atomic_load(&f.wrong_route_binding), 1);
        check_equal(cflow_cnet_domain_route_bind_source(&f.route), SALTS_OK);
        check_equal(cflow_cnet_domain_route_bind_source(&f.route2), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_source_binding(
            &f.route, &source_connection, &source_id), SALTS_OK);
        check_equal(source_id, (uint32_t)1u);
        check_equal(source_connection.slot, f.connection.slot);
        check_equal(source_connection.generation, f.connection.generation);

        f.associated[0] = &f.route;
        f.associated[1] = &f.route2;
        context_config = (cflow_cnet_manager_context_config){
            .manager = &f.manager,
            .managed = f.managed,
            .routes = f.associated,
            .route_count = 2u,
            .wake = manager_lease_wake,
            .wake_user = &f
        };
        stale_managed = f.managed;
        ++stale_managed.generation;
        context_config.managed = stale_managed;
        check_equal(cflow_cnet_manager_context_init(
            &f.guard, &context_config), SALTS_ENOENT);
        check_null(f.guard.impl);
        context_config.managed = f.managed;
        check_equal(cflow_cnet_manager_context_init(
            &f.guard, &context_config), SALTS_OK);
        check_equal(cflow_cnet_manager_context_destroy(
            &f.guard), SALTS_EBUSY);

        /* Real CNet receive callback consumes the reserved route credit.
         * The Actor stays paused until transport terminal and manager RETIRED. */
        check_equal(cflow_cnet_domain_route_reserve(
            &f.route, &credit), SALTS_OK);
        f.receive_credit = credit;
        check_equal(cnet_receive(&f.client, f.connection, 1u), SALTS_OK);
        check_equal(manager_send_byte(peer_fd, 67u), SALTS_OK);
        while (atomic_load(&f.receives) == 0 &&
               cmeta_monotonic_ms() < deadline) {
            size_t events = 0u;
            check_equal(cnet_client_poll(&f.client, 1u, &events), SALTS_OK);
        }
        check_equal(atomic_load(&f.receives), 1);
        check_equal(atomic_load(&f.receive_status), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.route, &route_stats), SALTS_OK);
        check_equal(route_stats.awaiting_ack, (size_t)1u);
        /* A full strict-key route does not borrow credit from another route,
         * even when both use the same CNetManager attachment. */
        check_equal(cflow_cnet_domain_route_reserve(
            &f.route, &credit), SALTS_ENOBUFS);
        /* The same physical CNet connection carries a second independently
         * retained semantic route, not another managed connection. */
        check_equal(cflow_cnet_domain_route_reserve(
            &f.route2, &credit), SALTS_OK);
        f.receive_credit = credit;
        check_equal(cnet_receive(&f.client, f.connection, 1u), SALTS_OK);
        check_equal(manager_send_byte(peer_fd, 83u), SALTS_OK);
        while (atomic_load(&f.receives) < 2 &&
               cmeta_monotonic_ms() < deadline) {
            size_t events = 0u;
            check_equal(cnet_client_poll(&f.client, 1u, &events), SALTS_OK);
        }
        check_equal(atomic_load(&f.receives), 2);
        check_equal(atomic_load(&f.receive_status), SALTS_OK);
        check_equal(cflow_cnet_domain_route_get_stats(
            &f.route2, &route_stats), SALTS_OK);
        check_equal(route_stats.awaiting_ack, (size_t)1u);
        check_equal(cflow_cnet_domain_route_reserve(
            &f.route2, &credit), SALTS_ENOBUFS);
        /* FULL is per-route, never a permission to reroute onto another
         * semantic key or to over-issue receive demand. */
        check_equal(atomic_load(&f.receives), 2);

        check_equal(cnet_close(&f.client, f.connection), SALTS_OK);
        while (!atomic_load(&f.terminal) &&
               cmeta_monotonic_ms() < deadline) {
            size_t events = 0u;
            check_equal(cnet_client_poll(&f.client, 1u, &events), SALTS_OK);
        }
        check_true(atomic_load(&f.terminal));
        check_equal(atomic_load(&f.source_terminal_status), SALTS_OK);
        check_equal(cnet_manager_get_snapshot(
            &f.manager, &manager_stats), SALTS_OK);
        check_equal(manager_stats.retired, (size_t)1u);
        check_equal(manager_stats.context_holds, (size_t)1u);
        check_false(manager_stats.drained);
        check_equal(cnet_manager_advance(&f.manager, 2u, &work), SALTS_OK);
        check_equal(atomic_load(&f.recycled), 0);
        check_equal(cnet_manager_destroy(&f.manager), SALTS_EBUSY);
        released = true;
        check_equal(cflow_cnet_manager_context_poll_release(
            &f.guard, &released), SALTS_EBUSY);
        check_false(released);

        /* The target Actor was intentionally paused while the real CNet
         * connection reached terminal. Its owned bytes remain usable. */
        check_equal(atomic_load(&f.action_count), 0);
        atomic_store_explicit(&f.target_run, true, memory_order_release);
        {
            const uint64_t target_deadline =
                cmeta_monotonic_ms() + MANAGER_LEASE_TIMEOUT_MS;
            while (atomic_load(&f.values) == 0 &&
                   cmeta_monotonic_ms() < target_deadline)
                cmeta_sleep_ms(1u);
        }
        check_true(manager_lease_wait_bool(&f.first_ack));
        check_equal(atomic_load(&f.action_count), 1);
        check_equal(atomic_load(&f.sink_errors), 0);
        /* The manager guard clears a pending ACK wake before scanning: the
         * remaining route is still held, so no premature recycle occurs. */
        {
            cflow_cnet_domain_route_stats left = {0};
            cflow_cnet_domain_route_stats right = {0};
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.route, &left), SALTS_OK);
            check_equal(cflow_cnet_domain_route_get_stats(
                &f.route2, &right), SALTS_OK);
            check_equal(left.awaiting_ack + right.awaiting_ack, (size_t)1u);
            check_equal(left.active_slots + right.active_slots, (size_t)1u);
        }
        released = true;
        check_equal(cflow_cnet_manager_context_poll_release(
            &f.guard, &released), SALTS_EBUSY);
        check_false(released);
        check_equal(cnet_manager_get_snapshot(
            &f.manager, &manager_stats), SALTS_OK);
        check_equal(manager_stats.context_holds, (size_t)1u);
        check_equal(atomic_load(&f.recycled), 0);
        /* Exercise repeated source-owner scans while the second route is
         * unacknowledged. No false progress or release is permitted, and
         * every scan clears only the coalesced hint, not real route state. */
        for (unsigned attempt = 0u; attempt < 128u; ++attempt) {
            released = true;
            check_equal(cflow_cnet_manager_context_poll_release(
                &f.guard, &released), SALTS_EBUSY);
            check_false(released);
        }
        check_equal(cnet_manager_get_snapshot(
            &f.manager, &manager_stats), SALTS_OK);
        check_equal(manager_stats.context_holds, (size_t)1u);
        check_equal(atomic_load(&f.recycled), 0);
        /* Target ACK #2 now executes on its real other OS thread while the
         * source actively polls. One scan may observe outstanding work and
         * return EBUSY; a later scan must observe the settled route and
         * release once. The source never fabricates a target ACK. */
        atomic_store_explicit(&f.target_run, true, memory_order_release);
        {
            const uint64_t ack_deadline =
                cmeta_monotonic_ms() + MANAGER_LEASE_TIMEOUT_MS;
            int poll_status = SALTS_EBUSY;
            while (cmeta_monotonic_ms() < ack_deadline) {
                released = false;
                poll_status = cflow_cnet_manager_context_poll_release(
                    &f.guard, &released);
                if (poll_status == SALTS_OK) {
                    check_true(released);
                    break;
                }
                check_equal(poll_status, SALTS_EBUSY);
                check_false(released);
                cmeta_thread_yield();
            }
            check_equal(poll_status, SALTS_OK);
        }
        {
            const uint64_t done_deadline =
                cmeta_monotonic_ms() + MANAGER_LEASE_TIMEOUT_MS;
            while (atomic_load(&f.values) < 2 &&
                   cmeta_monotonic_ms() < done_deadline)
                cmeta_thread_yield();
        }
        check_equal(atomic_load(&f.values), 2);
        check_equal(atomic_load(&f.action_count), 2);
        check_equal(atomic_load(&f.checksum), 150);
        check_equal(atomic_load(&f.wrong_owner_releases), 2);
        check_equal(atomic_load(&f.sink_errors), 0);

        check_equal(cflow_cnet_manager_context_get_stats(
            &f.guard, &guard_stats), SALTS_OK);
        check_equal(guard_stats.route_count, (size_t)2u);
        check_equal(guard_stats.notifications, (uint64_t)4u);
        /* ACK #2 can race a source poll between its two notifications.
         * That permits an additional rearmed hint, not another release. */
        check_true(guard_stats.coalesced_wakes >= (uint64_t)2u);
        check_true(guard_stats.coalesced_wakes <= (uint64_t)3u);
        check_equal(atomic_load(&f.wakes),
                    (int)guard_stats.coalesced_wakes);
        /* Source may observe ACK #2 and release before its target Owner
         * publishes notify_settled(). A late wake is only a hint, never a
         * second release request. Either pending state is valid here. */
        /* Concurrent polling already committed the one valid release. */
        check_equal(cflow_cnet_manager_context_poll_release(
            &f.guard, &released), SALTS_EALREADY);
        check_false(released);
        check_equal(cnet_manager_get_snapshot(
            &f.manager, &manager_stats), SALTS_OK);
        check_equal(manager_stats.context_holds, (size_t)0u);
        check_equal(cnet_manager_advance(&f.manager, 2u, &work), SALTS_OK);
        check_equal(atomic_load(&f.recycled), 1);
        check_equal(cnet_manager_get_snapshot(
            &f.manager, &manager_stats), SALTS_OK);
        check_true(manager_stats.drained);

        /* The host establishes target callback/notification quiescence
         * BEFORE destroying the source-owned guard and manager. */
        atomic_store_explicit(&f.target_stop, true, memory_order_release);
        check_equal(cmeta_thread_join(&f.target_thread), SALTS_OK);
        check_true(atomic_load(&f.target_done));
        check_equal(atomic_load(&f.target_status), SALTS_OK);
        check_equal(cflow_cnet_manager_context_destroy(&f.guard), SALTS_OK);
        check_equal(cnet_manager_destroy(&f.manager), SALTS_OK);
        check_equal(cnet_client_stop(
            &f.client, MANAGER_LEASE_TIMEOUT_MS), SALTS_OK);
        check_equal(cnet_client_destroy(&f.client), SALTS_OK);
        manager_socket_close(peer_fd);
        manager_socket_close(listen_fd);
    }
}
