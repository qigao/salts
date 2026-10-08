#include <cflow/cnet_domain_actor.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include "tinytest.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET domain_socket;
typedef int domain_socklen;
#define DOMAIN_BAD_SOCKET INVALID_SOCKET
#elif defined(__unix__) || defined(__APPLE__)
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int domain_socket;
typedef socklen_t domain_socklen;
#define DOMAIN_BAD_SOCKET (-1)
#endif

enum {
    DOMAIN_TEST_EVENT = 150,
    DOMAIN_TEST_ACTION = 250,
    DOMAIN_TEST_TIMEOUT_MS = 5000
};

typedef struct domain_test_fixture {
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_actor_ref ref;
    cflow_cnet_domain_bridge bridge;
    cflow_machine_action_binding action;
    const void *owner_thread;
    int initial_state;
    atomic_int actions;
    atomic_int values;
    atomic_int errors;
    atomic_int dones;
    atomic_int wrong_thread;
    atomic_int first_bytes_sum;
    cflow_cnet_domain_delivery last_delivery;
} domain_test_fixture;

static bool domain_action(void *user, const void *state, const void *event,
                          void *target, void *observation,
                          const char **out_error) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    const cflow_cnet_domain_delivery *delivery =
        (const cflow_cnet_domain_delivery *)event;
    cnet_receive_view payload = {0};
    if (!f || !state || !event || !target || !observation || !out_error)
        return false;
    if (f->owner_thread != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_thread, 1);
    if (cflow_cnet_domain_borrow(&f->bridge, delivery, &payload) != SALTS_OK) {
        *out_error = "Domain Actor could not borrow retained payload";
        return false;
    }
    if (payload.kind != CNET_MESSAGE_BYTES || payload.size == 0u) {
        *out_error = "unexpected received byte slice";
        return false;
    }
    atomic_fetch_add(&f->first_bytes_sum,
                     ((const unsigned char *)payload.data)[0]);
    f->last_delivery = *delivery;
    /* This fixture's business processing is the byte copy above. Production
     * consumers ACK only at their actual semantic commit boundary. */
    if (cflow_cnet_domain_acknowledge(&f->bridge, delivery) != SALTS_OK) {
        *out_error = "Domain Actor payload ACK failed";
        return false;
    }
    *(int *)target = *(const int *)state + 1;
    *(int *)observation = *(int *)target;
    *out_error = NULL;
    atomic_fetch_add(&f->actions, 1);
    return true;
}

static bool domain_on_value(void *user, const cmeta_type_desc *type,
                            const void *value) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    if (!f || !type || !cmeta_type_equal(type, &cmeta_type_int) || !value)
        return false;
    if (f->owner_thread != cmeta_thread_current_token())
        atomic_fetch_add(&f->wrong_thread, 1);
    atomic_fetch_add(&f->values, 1);
    return true;
}

static void domain_on_error(void *user, const char *error) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    if (f && error) atomic_fetch_add(&f->errors, 1);
}

static void domain_on_done(void *user) {
    domain_test_fixture *f = (domain_test_fixture *)user;
    if (f) atomic_fetch_add(&f->dones, 1);
}

static bool domain_fixture_init_connection(
    domain_test_fixture *f, size_t mailbox_capacity, size_t stage_capacity,
    cnet_connection connection) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {DOMAIN_TEST_EVENT, &cflow_cnet_domain_delivery_type}
    };
    const cflow_machine_action actions[] = {{
        DOMAIN_TEST_ACTION, &cmeta_type_int,
        DOMAIN_TEST_EVENT, &cflow_cnet_domain_delivery_type,
        &cmeta_type_int, CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u
    }};
    const cflow_machine_transition transitions[] = {
        {10u, DOMAIN_TEST_EVENT, 0u, DOMAIN_TEST_ACTION, 10u, 1u}
    };
    const cflow_machine_definition definition = {
        states, 1u, 10u, events, 1u, NULL, 0u,
        actions, 1u, transitions, 1u
    };
    cflow_actor_config actor_config = {0};
    cflow_cnet_domain_config stage_config = {0};
    memset(f, 0, sizeof(*f));
    f->owner_thread = cmeta_thread_current_token();
    f->initial_state = 0;
    f->action = (cflow_machine_action_binding){
        DOMAIN_TEST_ACTION, domain_action, f
    };
    if (cflow_machine_build(&f->machine, &definition) != CFLOW_MACHINE_OK ||
        !cflow_executor_owner_init_with_capacity(
            &f->executor, 32u, NULL, NULL) ||
        !cflow_scheduler_owner_bind(&f->scheduler, &f->executor, 16u))
        return false;
    actor_config.machine = (cflow_machine_instance_config){
        &f->machine, &f->initial_state, &cmeta_type_int,
        NULL, 0u, &f->action, 1u, mailbox_capacity, &f->executor
    };
    actor_config.scheduler = &f->scheduler;
    actor_config.callbacks = (cflow_subscriber_callbacks){
        domain_on_value, domain_on_error, domain_on_done, f
    };
    if (cflow_actor_init(&f->actor, &actor_config).status != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&f->actor, &f->ref) ||
        cflow_actor_start(&f->actor) != CFLOW_ACTOR_OK)
        return false;
    stage_config = (cflow_cnet_domain_config){
        .actor = &f->ref,
        .event_id = DOMAIN_TEST_EVENT,
        .connection = connection,
        .slot_capacity = stage_capacity,
        .max_receive_bytes = 32u
    };
    return cflow_cnet_domain_bridge_init(&f->bridge, &stage_config) == SALTS_OK;
}

static bool domain_fixture_init(
    domain_test_fixture *f, size_t mailbox_capacity, size_t stage_capacity) {
    return domain_fixture_init_connection(
        f, mailbox_capacity, stage_capacity, (cnet_connection){5u, 13u});
}

static bool domain_drive_until(domain_test_fixture *f, int expected) {
    const uint64_t started = cmeta_monotonic_ms();
    while (atomic_load(&f->values) < expected &&
           cmeta_monotonic_ms() - started < DOMAIN_TEST_TIMEOUT_MS) {
        if (!cflow_executor_run_one(&f->executor))
            cmeta_sleep_ms(1u);
    }
    return atomic_load(&f->values) >= expected;
}

static void domain_fixture_finish(domain_test_fixture *f) {
    const uint64_t started = cmeta_monotonic_ms();
    if (f->actor.impl) {
        (void)cflow_actor_request_stop(&f->actor);
        while (cflow_actor_current_state(&f->actor) !=
                   CFLOW_ACTOR_STATE_STOPPED &&
               cflow_actor_current_state(&f->actor) !=
                   CFLOW_ACTOR_STATE_FAILED &&
               cmeta_monotonic_ms() - started < DOMAIN_TEST_TIMEOUT_MS) {
            if (!cflow_executor_run_one(&f->executor))
                cmeta_sleep_ms(1u);
        }
        cflow_actor_destroy(&f->actor);
    }
    if (f->bridge.impl) {
        cflow_cnet_domain_stats stats = {0};
        check_equal(cflow_cnet_domain_seal(&f->bridge), SALTS_OK);
        check_equal(cflow_cnet_domain_transport_terminal(&f->bridge), SALTS_OK);
        check_equal(cflow_cnet_domain_abort_after_quiescence(&f->bridge),
                    SALTS_OK);
        check_equal(cflow_cnet_domain_get_stats(&f->bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(cflow_cnet_domain_bridge_destroy(&f->bridge), SALTS_OK);
    }
    cflow_actor_ref_release(&f->ref);
    if (cflow_scheduler_valid(&f->scheduler))
        cflow_scheduler_destroy(&f->scheduler);
    if (cflow_executor_valid(&f->executor))
        cflow_executor_destroy(&f->executor);
    cflow_machine_destroy(&f->machine);
}

typedef struct domain_foreign_probe {
    cflow_cnet_domain_bridge *bridge;
    atomic_int status;
} domain_foreign_probe;

static void domain_foreign_stats(void *user) {
    domain_foreign_probe *p = (domain_foreign_probe *)user;
    cflow_cnet_domain_stats snapshot = {0};
    atomic_store(&p->status,
        cflow_cnet_domain_get_stats(p->bridge, &snapshot));
}

/*
 * Production CNet callback qualification uses loopback TCP but keeps the
 * listener and accepted peer socket HOST-owned. The CNet client, domain Actor
 * and shared Scheduler are all driven by this same calling owner.
 */
typedef struct domain_loopback {
    cnet_client client;
    native_io_backend backend; /* Borrowed by CNet in external mode. */
    domain_test_fixture domain;
    cnet_connection connection;
    domain_socket listener;
    domain_socket peer;
    cnet_connection_state state;
    int on_receive_status;
    size_t received_callbacks;
    size_t observed_completions;
    size_t routed_completions;
    size_t unmatched_completions;
    bool external;
    bool connected;
    bool terminal;
} domain_loopback;

static void domain_socket_close(domain_socket fd) {
    if (fd == DOMAIN_BAD_SOCKET) return;
#if defined(_WIN32)
    (void)closesocket(fd);
#else
    (void)close(fd);
#endif
}

static bool domain_socket_would_block(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static bool domain_socket_nonblock(domain_socket fd) {
#if defined(_WIN32)
    u_long yes = 1u;
    return ioctlsocket(fd, FIONBIO, &yes) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 &&
           ((flags & O_NONBLOCK) != 0 ||
            fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
#endif
}

static int domain_socket_send_all(domain_socket fd, const void *bytes,
                                  size_t count) {
    const unsigned char *data = (const unsigned char *)bytes;
    const uint64_t deadline = cmeta_monotonic_ms() + DOMAIN_TEST_TIMEOUT_MS;
    size_t offset = 0u;
    while (offset < count) {
#if defined(_WIN32)
        const int wrote = send(fd, (const char *)data + offset,
                               (int)(count - offset), 0);
        if (wrote == SOCKET_ERROR) {
#elif defined(MSG_NOSIGNAL)
        const ssize_t wrote = send(fd, data + offset, count - offset,
                                   MSG_NOSIGNAL);
        if (wrote < 0) {
#else
        const ssize_t wrote = send(fd, data + offset, count - offset, 0);
        if (wrote < 0) {
#endif
            if (!domain_socket_would_block())
                return SALTS_EIO;
            if (cmeta_monotonic_ms() >= deadline)
                return SALTS_ETIMEDOUT;
            cmeta_thread_yield();
            continue;
        }
        if (wrote == 0) return SALTS_EIO;
        offset += (size_t)wrote;
    }
    return SALTS_OK;
}

static native_io_backend_kind domain_loopback_backend(void) {
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

static void domain_network_state(
    void *user, cnet_connection connection, cnet_connection_state state,
    const cnet_error *error) {
    domain_loopback *s = (domain_loopback *)user;
    (void)error;
    if (!s || s->connection.slot != connection.slot ||
        s->connection.generation != connection.generation)
        return;
    s->state = state;
    if (state == CNET_CONNECTION_CONNECTED)
        s->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
        s->terminal = true;
        if (s->domain.bridge.impl)
            (void)cflow_cnet_domain_transport_terminal(&s->domain.bridge);
    }
}

static void domain_network_receive(
    void *user, cnet_connection connection, const cnet_receive_view *view) {
    domain_loopback *s = (domain_loopback *)user;
    if (!s) return;
    s->on_receive_status = cflow_cnet_domain_receive(
        &s->domain.bridge, connection, view);
    ++s->received_callbacks;
}

/* The embedding host is the ONLY NativeIO completion observer in external
 * mode; CNet never polls the borrowed backend. Domain Actor execution remains
 * separately owned and bounded by the caller's run_one() fairness quantum. */
static int domain_loopback_progress(domain_loopback *s, uint32_t wait_ms) {
    native_io_completion batch[8] = {{0}};
    size_t count = 0u;
    size_t events = 0u;
    int status;
    if (!s->external)
        return cnet_client_poll(&s->client, wait_ms, &events);
    status = cnet_client_advance_external(&s->client, &events);
    if (status != SALTS_OK) return status;
    status = native_io_backend_observe(&s->backend, batch, 8u,
                                       wait_ms, &count);
    if (status != SALTS_OK && status != SALTS_ETIMEDOUT) return status;
    if (status == SALTS_OK) {
        s->observed_completions += count;
        for (size_t i = 0u; i < count; ++i) {
            bool consumed = false;
            size_t routed_events = 0u;
            status = cnet_client_route_external_completion(
                &s->client, &batch[i], &consumed, &routed_events);
            if (status != SALTS_OK) return status;
            if (!consumed) {
                ++s->unmatched_completions;
                return SALTS_EPROTO; /* Never fabricate a terminal owner. */
            }
            ++s->routed_completions;
        }
    }
    return cnet_client_advance_external(&s->client, &events);
}

static int domain_loopback_open_mode(domain_loopback *s, bool external) {
    struct sockaddr_in address = {0};
    domain_socklen length = (domain_socklen)sizeof(address);
    uint16_t port;
    char uri[80];
    cnet_client_config config = {
        .backend = domain_loopback_backend(),
        .connection_capacity = 2u,
        .command_capacity = 8u,
        .request_capacity = 8u,
        .completion_batch_capacity = 4u,
        .event_capacity = 8u,
        .max_send_bytes = 1024u,
        .receive_buffer_bytes = 1024u
    };
    cnet_connect_options connect_options = {0};
    const uint64_t deadline = cmeta_monotonic_ms() + DOMAIN_TEST_TIMEOUT_MS;
    int status;

    memset(s, 0, sizeof(*s));
    s->external = external;
    s->listener = DOMAIN_BAD_SOCKET;
    s->peer = DOMAIN_BAD_SOCKET;
#if defined(_WIN32)
    {
        WSADATA winsock;
        if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0)
            return SALTS_EIO;
    }
#endif
    s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->listener == DOMAIN_BAD_SOCKET) return SALTS_EIO;
    address.sin_family = AF_INET;
    address.sin_port = htons(0u);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s->listener, (const struct sockaddr *)&address,
             (domain_socklen)sizeof(address)) != 0 ||
        getsockname(s->listener, (struct sockaddr *)&address,
                    &length) != 0 ||
        listen(s->listener, 2) != 0 ||
        !domain_socket_nonblock(s->listener))
        return SALTS_EIO;
    port = ntohs(address.sin_port);

    if (external) {
        const native_io_backend_config backend_config = {
            domain_loopback_backend(), 4u, 16u, 8u
        };
        status = native_io_backend_init(&s->backend, &backend_config);
        if (status != SALTS_OK) return status;
        status = cnet_client_init_external(&s->client, &config, &s->backend);
    } else {
        status = cnet_client_init(&s->client, &config);
    }
    if (status != SALTS_OK) return status;
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                   (unsigned)port);
    connect_options.uri = uri;
    connect_options.observer = (cnet_observer){
        .on_state = domain_network_state,
        .on_receive = domain_network_receive,
        .user = s
    };
    status = cnet_connect(&s->client, &connect_options, &s->connection);
    if (status != SALTS_OK) return status;
    if (!domain_fixture_init_connection(
            &s->domain, 1u, 2u, s->connection))
        return SALTS_EPROTO;

    while (!s->connected || s->peer == DOMAIN_BAD_SOCKET) {
        size_t events = 0u;
        if (s->peer == DOMAIN_BAD_SOCKET) {
            s->peer = accept(s->listener, NULL, NULL);
            if (s->peer != DOMAIN_BAD_SOCKET &&
                !domain_socket_nonblock(s->peer))
                return SALTS_EIO;
            if (s->peer == DOMAIN_BAD_SOCKET &&
                !domain_socket_would_block())
                return SALTS_EIO;
        }
        (void)events;
        status = domain_loopback_progress(s, 1u);
        if (status != SALTS_OK) return status;
        if (cmeta_monotonic_ms() >= deadline)
            return SALTS_ETIMEDOUT;
    }
    domain_socket_close(s->listener);
    s->listener = DOMAIN_BAD_SOCKET;
    return SALTS_OK;
}

static int domain_loopback_open(domain_loopback *s) {
    return domain_loopback_open_mode(s, false);
}

static int domain_loopback_reserve_and_arm(domain_loopback *s) {
    cflow_cnet_domain_credit credit = {0};
    int status = cflow_cnet_domain_reserve_credit(
        &s->domain.bridge, &credit);
    if (status != SALTS_OK) return status;
    status = cnet_receive(&s->client, s->connection, 1u);
    if (status != SALTS_OK) {
        /* Failure has not transferred a CNet receive credit. */
        if (cflow_cnet_domain_cancel_credit(
                &s->domain.bridge, credit) != SALTS_OK)
            return SALTS_EPROTO;
    }
    return status;
}

static int domain_loopback_poll_callbacks(domain_loopback *s,
                                          size_t expected) {
    const uint64_t deadline = cmeta_monotonic_ms() + DOMAIN_TEST_TIMEOUT_MS;
    while (s->received_callbacks < expected) {
        size_t events = 0u;
        (void)events;
        const int status = domain_loopback_progress(s, 1u);
        if (status != SALTS_OK) return status;
        if (s->terminal || cmeta_monotonic_ms() >= deadline)
            return SALTS_ETIMEDOUT;
    }
    return SALTS_OK;
}

static void domain_loopback_finish(domain_loopback *s) {
    if (s->client.impl != NULL) {
        if (!s->terminal && s->connection.slot != 0u) {
            int status = cnet_close(&s->client, s->connection);
            check_true(status == SALTS_OK ||
                       status == SALTS_EALREADY ||
                       status == SALTS_ENOENT);
        }
        for (size_t i = 0u; !s->terminal &&
                i < (size_t)DOMAIN_TEST_TIMEOUT_MS; ++i) {
            size_t events = 0u;
            int status = cnet_client_poll(&s->client, 1u, &events);
            if (status != SALTS_OK) break;
        }
        {
            int status = cnet_client_stop(
                &s->client, DOMAIN_TEST_TIMEOUT_MS);
            if (status == SALTS_ETIMEDOUT)
                status = cnet_client_stop(
                    &s->client, DOMAIN_TEST_TIMEOUT_MS);
            check_equal(status, SALTS_OK);
        }
        check_equal(cnet_client_destroy(&s->client), SALTS_OK);
    }
    domain_socket_close(s->peer);
    domain_socket_close(s->listener);
    s->peer = DOMAIN_BAD_SOCKET;
    s->listener = DOMAIN_BAD_SOCKET;
    domain_fixture_finish(&s->domain);
#if defined(_WIN32)
    (void)WSACleanup();
#endif
}

suite("CNet to CFlow Domain Actor bounded credit bridge") {
    it("drives real CNet TCP receive credit through Actor FULL and ACK") {
        domain_loopback s;
        cflow_cnet_domain_stats stats = {0};
        const unsigned char first[] = {13u, 14u, 15u};
        const unsigned char second[] = {47u, 48u};
        const unsigned char third[] = {8u};
        size_t retried = 0u;

        check_equal(domain_loopback_open(&s), SALTS_OK);
        check_equal(domain_loopback_reserve_and_arm(&s), SALTS_OK);
        check_equal(domain_socket_send_all(
            s.peer, first, sizeof(first)), SALTS_OK);
        check_equal(domain_loopback_poll_callbacks(&s, 1u), SALTS_OK);
        check_equal(s.on_receive_status, SALTS_OK);
        check_equal(atomic_load(&s.domain.actions), 0);

        check_equal(domain_loopback_reserve_and_arm(&s), SALTS_OK);
        check_equal(domain_socket_send_all(
            s.peer, second, sizeof(second)), SALTS_OK);
        check_equal(domain_loopback_poll_callbacks(&s, 2u), SALTS_OK);
        check_equal(s.on_receive_status, SALTS_ENOBUFS);
        check_equal(cflow_cnet_domain_reserve_credit(
            &s.domain.bridge, &(cflow_cnet_domain_credit){0}), SALTS_EBUSY);
        check_equal(cflow_cnet_domain_get_stats(
            &s.domain.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)2u);
        check_equal(stats.awaiting_ack, (size_t)1u);
        check_equal(stats.pending_actor_admission, (size_t)1u);
        check_equal(stats.retained_bytes, sizeof(first) + sizeof(second));

        check_true(domain_drive_until(&s.domain, 1));
        check_equal(cflow_cnet_domain_retry_actor(
            &s.domain.bridge, &retried), SALTS_OK);
        check_equal(retried, (size_t)1u);
        check_true(domain_drive_until(&s.domain, 2));
        check_equal(atomic_load(&s.domain.first_bytes_sum), 60);
        check_equal(atomic_load(&s.domain.actions), 2);
        check_equal(atomic_load(&s.domain.wrong_thread), 0);
        check_equal(atomic_load(&s.domain.errors), 0);

        /* An ACKed slot becomes available for future CNet receive demand. */
        check_equal(domain_loopback_reserve_and_arm(&s), SALTS_OK);
        check_equal(domain_socket_send_all(
            s.peer, third, sizeof(third)), SALTS_OK);
        check_equal(domain_loopback_poll_callbacks(&s, 3u), SALTS_OK);
        check_equal(s.on_receive_status, SALTS_OK);
        check_true(domain_drive_until(&s.domain, 3));
        check_equal(atomic_load(&s.domain.first_bytes_sum), 68);

        check_equal(cflow_cnet_domain_get_stats(
            &s.domain.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.retained_bytes, (size_t)0u);
        check_equal(stats.mailbox_accepted, (uint64_t)3u);
        check_equal(stats.acknowledged, (uint64_t)3u);
        check_true(stats.mailbox_full >= (uint64_t)1u);
        check_equal(stats.reserved_credits, (uint64_t)3u);
        domain_loopback_finish(&s);
    }

    it("retains real CNet callback bytes across peer EOF until Actor ACK") {
        domain_loopback s;
        cflow_cnet_domain_stats stats = {0};
        const unsigned char body[] = {91u};
        const uint64_t deadline = cmeta_monotonic_ms() + DOMAIN_TEST_TIMEOUT_MS;

        check_equal(domain_loopback_open(&s), SALTS_OK);
        check_equal(domain_loopback_reserve_and_arm(&s), SALTS_OK);
        check_equal(domain_socket_send_all(s.peer, body, sizeof(body)),
                    SALTS_OK);
        check_equal(domain_loopback_poll_callbacks(&s, 1u), SALTS_OK);
        check_equal(s.on_receive_status, SALTS_OK);
        /* Keep one CNet receive credit outstanding to observe the peer EOF.
         * CNet's terminal event must settle THAT credit, not the already
         * accepted Domain Actor payload lease. */
        check_equal(domain_loopback_reserve_and_arm(&s), SALTS_OK);
        domain_socket_close(s.peer);
        s.peer = DOMAIN_BAD_SOCKET;

        while (!s.terminal && cmeta_monotonic_ms() < deadline) {
            size_t events = 0u;
            check_equal(cnet_client_poll(&s.client, 1u, &events), SALTS_OK);
        }
        check_true(s.terminal);
        check_equal(cflow_cnet_domain_get_stats(
            &s.domain.bridge, &stats), SALTS_OK);
        check_true(stats.transport_terminal);
        check_equal(stats.active_slots, (size_t)1u);
        check_equal(stats.awaiting_ack, (size_t)1u);
        check_equal(stats.mailbox_accepted, (uint64_t)1u);
        check_true(domain_drive_until(&s.domain, 1));
        check_equal(atomic_load(&s.domain.first_bytes_sum), 91);
        check_equal(cflow_cnet_domain_get_stats(
            &s.domain.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.acknowledged, (uint64_t)1u);
        domain_loopback_finish(&s);
    }

    it("reserves before CNet receive, rolls back rejection and rejects foreign owner") {
        domain_test_fixture f;
        cflow_cnet_domain_credit first = {0};
        cflow_cnet_domain_credit second = {0};
        cflow_cnet_domain_stats stats = {0};
        domain_foreign_probe foreign = {0};
        cmeta_thread_t thread = NULL;
        const cnet_connection wrong = {6u, 13u};
        const unsigned char data[] = {11u};
        const cnet_receive_view view = {data, sizeof(data), CNET_MESSAGE_BYTES};

        check_true(domain_fixture_init(&f, 1u, 2u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &first), SALTS_OK);
        check_not_equal(first.generation, (uint64_t)0u);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &second),
                    SALTS_EBUSY);
        check_equal(cflow_cnet_domain_receive(&f.bridge, wrong, &view),
                    SALTS_ENOENT);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, first), SALTS_OK);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, first), SALTS_ENOENT);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &second),
                    SALTS_OK);
        check_not_equal(first.generation, second.generation);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, first), SALTS_ENOENT);

        foreign.bridge = &f.bridge;
        check_equal(cmeta_thread_create(&thread, domain_foreign_stats, &foreign),
                    0);
        check_equal(cmeta_thread_join(&thread), 0);
        check_equal(atomic_load(&foreign.status), SALTS_EPERM);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, second), SALTS_OK);

        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.reserved_credits, (uint64_t)2u);
        check_equal(stats.rolled_back_credits, (uint64_t)2u);
        domain_fixture_finish(&f);
    }

    it("retains borrowed bytes through FULL and delivers only after owner progress") {
        domain_test_fixture f;
        cflow_cnet_domain_credit credit = {0};
        cflow_cnet_domain_stats stats = {0};
        const cnet_connection conn = {5u, 13u};
        unsigned char first[] = {21u, 22u, 23u};
        unsigned char second[] = {41u, 42u};
        cnet_receive_view view1 = {first, sizeof(first), CNET_MESSAGE_BYTES};
        cnet_receive_view view2 = {second, sizeof(second), CNET_MESSAGE_BYTES};
        size_t work = 555u;

        check_true(domain_fixture_init(&f, 1u, 2u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(&f.bridge, conn, &view1), SALTS_OK);
        memset(first, 0u, sizeof(first)); /* CNet's callback borrow has ended. */

        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(&f.bridge, conn, &view2),
                    SALTS_ENOBUFS);
        memset(second, 0u, sizeof(second));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit),
                    SALTS_EBUSY);
        check_equal(cflow_cnet_domain_retry_actor(&f.bridge, &work),
                    SALTS_ENOBUFS);
        check_equal(work, (size_t)0u);

        check_true(domain_drive_until(&f, 1));
        check_equal(cflow_cnet_domain_retry_actor(&f.bridge, &work), SALTS_OK);
        check_equal(work, (size_t)1u);
        check_true(domain_drive_until(&f, 2));
        check_equal(atomic_load(&f.first_bytes_sum), 62);
        check_equal(atomic_load(&f.actions), 2);
        check_equal(atomic_load(&f.wrong_thread), 0);
        check_equal(atomic_load(&f.errors), 0);
        check_equal(cflow_cnet_domain_acknowledge(
            &f.bridge, &f.last_delivery), SALTS_ENOENT);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.awaiting_ack, (size_t)0u);
        check_equal(stats.retained_bytes, (size_t)0u);
        check_equal(stats.received_views, (uint64_t)2u);
        check_equal(stats.mailbox_accepted, (uint64_t)2u);
        check_equal(stats.mailbox_full, (uint64_t)2u);
        check_equal(stats.acknowledged, (uint64_t)2u);
        check_equal(stats.peak_retained_bytes, (size_t)5u);
        domain_fixture_finish(&f);
    }

    it("rejects an unsolicited receive instead of silently losing bytes") {
        domain_test_fixture f;
        cflow_cnet_domain_stats stats = {0};
        unsigned char byte = 9u;
        const cnet_receive_view view = {&byte, 1u, CNET_MESSAGE_BYTES};

        check_true(domain_fixture_init(&f, 1u, 1u));
        check_equal(cflow_cnet_domain_receive(
            &f.bridge, (cnet_connection){5u, 13u}, &view), SALTS_EPROTO);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.fatal_status, SALTS_EPROTO);
        check_true(stats.sealed);
        check_equal(stats.mailbox_accepted, (uint64_t)0u);
        check_equal(stats.active_slots, (size_t)0u);
        domain_fixture_finish(&f);
    }

    it("reports oversize as fatal, never fabricates Actor acceptance") {
        domain_test_fixture f;
        cflow_cnet_domain_credit credit = {0};
        cflow_cnet_domain_stats stats = {0};
        unsigned char oversized[33] = {0};
        const cnet_receive_view view = {
            oversized, sizeof(oversized), CNET_MESSAGE_BYTES};
        check_true(domain_fixture_init(&f, 1u, 1u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(
            &f.bridge, (cnet_connection){5u, 13u}, &view), SALTS_EMSGSIZE);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.fatal_status, SALTS_EMSGSIZE);
        check_equal(stats.mailbox_accepted, (uint64_t)0u);
        check_equal(stats.active_slots, (size_t)1u);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &credit),
                    SALTS_ESHUTDOWN);
        check_equal(cflow_cnet_domain_bridge_destroy(&f.bridge), SALTS_EBUSY);
        domain_fixture_finish(&f);
    }

    it("preserves Actor-accepted leases across CNet transport terminal") {
        domain_test_fixture f;
        cflow_cnet_domain_credit first = {0};
        cflow_cnet_domain_credit pending = {0};
        cflow_cnet_domain_stats stats = {0};
        unsigned char payload[] = {73u};
        cnet_receive_view view = {payload, sizeof(payload), CNET_MESSAGE_BYTES};
        check_true(domain_fixture_init(&f, 1u, 2u));
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &first), SALTS_OK);
        check_equal(cflow_cnet_domain_receive(
            &f.bridge, (cnet_connection){5u, 13u}, &view), SALTS_OK);
        check_equal(cflow_cnet_domain_reserve_credit(&f.bridge, &pending),
                    SALTS_OK);
        check_equal(cflow_cnet_domain_transport_terminal(&f.bridge), SALTS_OK);
        check_equal(cflow_cnet_domain_cancel_credit(&f.bridge, pending),
                    SALTS_ENOENT);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_true(stats.transport_terminal);
        check_equal(stats.active_slots, (size_t)1u);
        check_equal(stats.awaiting_ack, (size_t)1u);
        check_true(domain_drive_until(&f, 1));
        check_equal(atomic_load(&f.first_bytes_sum), 73);
        check_equal(cflow_cnet_domain_get_stats(&f.bridge, &stats), SALTS_OK);
        check_equal(stats.active_slots, (size_t)0u);
        check_equal(stats.acknowledged, (uint64_t)1u);
        domain_fixture_finish(&f);
    }
}
