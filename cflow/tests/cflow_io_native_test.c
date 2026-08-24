#include <cflow/io_native.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include "tinytest.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET native_test_socket;
#define NATIVE_TEST_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int native_test_socket;
#define NATIVE_TEST_INVALID_SOCKET (-1)
#endif

static const uint64_t NATIVE_TEST_TIMEOUT_NS = UINT64_C(5000000000);

enum {
    NATIVE_TEST_CAPACITY = 4,
    NATIVE_TEST_PAYLOAD_CAPACITY = 64,
    NATIVE_TEST_CANCEL_REUSE_ITERATIONS = 32,
    NATIVE_TEST_CANCEL_SETTLE_YIELDS = 64
};

typedef struct native_test_operation {
    cflow_io_native_operation native;
    int released;
} native_test_operation;

typedef struct native_completion_probe {
    cflow_io_request_id ids[NATIVE_TEST_CAPACITY];
    cflow_io_completion values[NATIVE_TEST_CAPACITY];
    size_t count;
} native_completion_probe;

typedef struct native_fixture {
    cflow_io_native_backend backend;
    cflow_executor executor;
    cflow_io_actor actor;
    native_completion_probe completions;
} native_fixture;

static void native_test_close_socket(native_test_socket socket_value) {
    if (socket_value == NATIVE_TEST_INVALID_SOCKET)
        return;
#if defined(_WIN32)
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
}

static int native_test_last_socket_error(void) {
#if defined(_WIN32)
    return -WSAGetLastError();
#else
    return -errno;
#endif
}

static int native_test_set_nonblocking(native_test_socket socket_value) {
#if defined(_WIN32)
    u_long enabled = 1u;
    return ioctlsocket(socket_value, FIONBIO, &enabled) == 0
               ? TURBO_OK : native_test_last_socket_error();
#else
    int flags;
    do {
        flags = fcntl(socket_value, F_GETFL);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0)
        return -errno;
    while (fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    return TURBO_OK;
#endif
}

#if !defined(_WIN32)
static int native_test_status_flags(native_test_socket socket_value,
                                    int *flags) {
    int value;
    do {
        value = fcntl(socket_value, F_GETFL);
    } while (value < 0 && errno == EINTR);
    if (value < 0)
        return -errno;
    *flags = value;
    return TURBO_OK;
}

static int native_test_set_blocking(native_test_socket socket_value) {
    int flags = 0;
    int status = native_test_status_flags(socket_value, &flags);
    if (status != TURBO_OK)
        return status;
    while (fcntl(socket_value, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    return TURBO_OK;
}
#endif

static int native_test_bind_loopback(native_test_socket socket_value,
                                     struct sockaddr_in *address) {
    int address_length = (int)sizeof(*address);
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address->sin_port = 0;
    if (bind(socket_value, (const struct sockaddr *)address,
             (int)sizeof(*address)) != 0)
        return native_test_last_socket_error();
    if (getsockname(socket_value, (struct sockaddr *)address,
#if defined(_WIN32)
                    &address_length
#else
                    (socklen_t *)&address_length
#endif
                    ) != 0)
        return native_test_last_socket_error();
    return TURBO_OK;
}

static int native_test_make_tcp_pair(native_test_socket sockets[2]) {
    native_test_socket listener = NATIVE_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    int status;
    sockets[0] = NATIVE_TEST_INVALID_SOCKET;
    sockets[1] = NATIVE_TEST_INVALID_SOCKET;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == NATIVE_TEST_INVALID_SOCKET)
        return native_test_last_socket_error();
    status = native_test_bind_loopback(listener, &address);
    if (status == TURBO_OK && listen(listener, 1) != 0)
        status = native_test_last_socket_error();
    if (status == TURBO_OK) {
        sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockets[0] == NATIVE_TEST_INVALID_SOCKET)
            status = native_test_last_socket_error();
    }
    if (status == TURBO_OK &&
        connect(sockets[0], (const struct sockaddr *)&address,
                (int)sizeof(address)) != 0)
        status = native_test_last_socket_error();
    if (status == TURBO_OK) {
        sockets[1] = accept(listener, NULL, NULL);
        if (sockets[1] == NATIVE_TEST_INVALID_SOCKET)
            status = native_test_last_socket_error();
    }
    native_test_close_socket(listener);
    if (status == TURBO_OK)
        status = native_test_set_nonblocking(sockets[0]);
    if (status == TURBO_OK)
        status = native_test_set_nonblocking(sockets[1]);
    if (status != TURBO_OK) {
        native_test_close_socket(sockets[0]);
        native_test_close_socket(sockets[1]);
        sockets[0] = NATIVE_TEST_INVALID_SOCKET;
        sockets[1] = NATIVE_TEST_INVALID_SOCKET;
    }
    return status;
}

static int native_test_make_udp_pair(native_test_socket sockets[2],
                                     struct sockaddr_in addresses[2]) {
    int status = TURBO_OK;
    sockets[0] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockets[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockets[0] == NATIVE_TEST_INVALID_SOCKET ||
        sockets[1] == NATIVE_TEST_INVALID_SOCKET)
        status = native_test_last_socket_error();
    if (status == TURBO_OK)
        status = native_test_bind_loopback(sockets[0], &addresses[0]);
    if (status == TURBO_OK)
        status = native_test_bind_loopback(sockets[1], &addresses[1]);
    if (status == TURBO_OK)
        status = native_test_set_nonblocking(sockets[0]);
    if (status == TURBO_OK)
        status = native_test_set_nonblocking(sockets[1]);
    if (status != TURBO_OK) {
        native_test_close_socket(sockets[0]);
        native_test_close_socket(sockets[1]);
        sockets[0] = NATIVE_TEST_INVALID_SOCKET;
        sockets[1] = NATIVE_TEST_INVALID_SOCKET;
    }
    return status;
}

static void native_operation_release(void *user) {
    native_test_operation *operation = (native_test_operation *)user;
    ++operation->released;
}

static void native_completion_record(
    void *user, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user,
    const cflow_io_completion *completion) {
    native_completion_probe *probe = (native_completion_probe *)user;
    (void)lease_id;
    (void)operation_user;
    if (probe->count < NATIVE_TEST_CAPACITY) {
        probe->ids[probe->count] = request_id;
        probe->values[probe->count] = *completion;
        ++probe->count;
    }
}

static int native_fixture_init(native_fixture *fixture,
                               cflow_io_native_backend_kind kind,
                               size_t capacity) {
    cflow_io_native_backend_config backend_config = {
        kind, capacity, capacity};
    cflow_io_actor_config actor_config;
    int status;
    memset(fixture, 0, sizeof(*fixture));
    status = cflow_io_native_backend_init(&fixture->backend, &backend_config);
    if (status != TURBO_OK)
        return status;
    if (!cflow_executor_manual_init_with_capacity(&fixture->executor,
                                                   capacity)) {
        (void)cflow_io_native_backend_shutdown(&fixture->backend);
        (void)cflow_io_native_backend_destroy(&fixture->backend);
        return TURBO_ENOMEM;
    }
    memset(&actor_config, 0, sizeof(actor_config));
    actor_config.request_capacity = capacity;
    actor_config.command_capacity = capacity;
    actor_config.executor = &fixture->executor;
    actor_config.backend = cflow_io_native_backend_actor_ops();
    actor_config.backend_user = &fixture->backend;
    actor_config.completion = native_completion_record;
    actor_config.completion_user = &fixture->completions;
    status = cflow_io_actor_init(&fixture->actor, &actor_config);
    if (status != TURBO_OK) {
        cflow_executor_destroy(&fixture->executor);
        (void)cflow_io_native_backend_shutdown(&fixture->backend);
        (void)cflow_io_native_backend_destroy(&fixture->backend);
    }
    return status;
}

static int native_fixture_wait(native_fixture *fixture, size_t count) {
    const uint64_t started = turbo_hrtime();
    while (fixture->completions.count < count) {
        (void)cflow_io_actor_run_ready(&fixture->actor, 64u);
        (void)cflow_executor_run_ready(&fixture->executor);
        if (turbo_hrtime() - started >= NATIVE_TEST_TIMEOUT_NS)
            return TURBO_ETIMEDOUT;
        turbo_thread_yield();
    }
    return TURBO_OK;
}

static void native_fixture_destroy(native_fixture *fixture) {
    const int close_status = cflow_io_actor_close(&fixture->actor);
    check_true(close_status == TURBO_OK || close_status == TURBO_EALREADY);
    (void)cflow_io_actor_run_ready(&fixture->actor, 64u);
    check_true(cflow_io_actor_is_quiescent(&fixture->actor));
    check_equal(cflow_io_actor_destroy(&fixture->actor), TURBO_OK);
    check_equal(cflow_io_native_backend_shutdown(&fixture->backend), TURBO_OK);
    check_equal(cflow_io_native_backend_destroy(&fixture->backend), TURBO_OK);
    check_true(cflow_executor_shutdown(&fixture->executor));
    cflow_executor_destroy(&fixture->executor);
}

static cflow_io_submit_result native_submit(
    native_fixture *fixture, cflow_io_lease_id lease,
    native_test_operation *operation) {
    cflow_io_operation actor_operation = {
        operation, native_operation_release};
    return cflow_io_actor_try_submit(
        &fixture->actor, lease, &actor_operation);
}

static void native_check_tcp(cflow_io_native_backend_kind kind) {
    static const unsigned char payload[] = {0x31u};
    native_fixture fixture;
    native_test_socket sockets[2];
    native_test_operation receive = {0};
    native_test_operation send_operation = {0};
    cflow_io_submit_result receive_result;
    cflow_io_submit_result send_result;
    unsigned char received[NATIVE_TEST_PAYLOAD_CAPACITY] = {0};

    check_equal(native_fixture_init(&fixture, kind, 2u), TURBO_OK);
    check_equal(native_test_make_tcp_pair(sockets), TURBO_OK);
    receive.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)sockets[1], received,
        sizeof(payload), NULL, 0u, 0u};
    send_operation.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_SEND, (uintptr_t)sockets[0],
        (void *)payload, sizeof(payload), NULL, 0u, 0u};

    receive_result = native_submit(&fixture, 11u, &receive);
    send_result = native_submit(&fixture, 12u, &send_operation);
    check_equal(receive_result.status, CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(send_result.status, CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(native_fixture_wait(&fixture, 2u), TURBO_OK);
    check_equal(received, payload, sizeof(payload));
    for (size_t i = 0u; i < fixture.completions.count; ++i) {
        check_equal(fixture.completions.values[i].kind,
                    CFLOW_IO_COMPLETION_OK);
        check_equal(fixture.completions.values[i].bytes, sizeof(payload));
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, fixture.completions.ids[i]),
                    CFLOW_IO_ACK_RELEASED);
    }
    check_equal(receive.released, 1);
    check_equal(send_operation.released, 1);
    native_test_close_socket(sockets[0]);
    native_test_close_socket(sockets[1]);
    native_fixture_destroy(&fixture);
}

static void native_check_udp(cflow_io_native_backend_kind kind) {
    static const unsigned char payload[] = "native udp payload";
    native_fixture fixture;
    native_test_socket sockets[2] = {
        NATIVE_TEST_INVALID_SOCKET, NATIVE_TEST_INVALID_SOCKET};
    struct sockaddr_in addresses[2];
    struct sockaddr_storage source_address;
    native_test_operation receive = {0};
    native_test_operation send_operation = {0};
    unsigned char received[NATIVE_TEST_PAYLOAD_CAPACITY] = {0};

    check_equal(native_fixture_init(&fixture, kind, 2u), TURBO_OK);
    check_equal(native_test_make_udp_pair(sockets, addresses), TURBO_OK);
    receive.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_UDP_RECV_FROM, (uintptr_t)sockets[1], received,
        sizeof(payload), &source_address, sizeof(source_address), 0u};
    send_operation.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_UDP_SEND_TO, (uintptr_t)sockets[0],
        (void *)payload, sizeof(payload), &addresses[1],
        sizeof(addresses[1]), sizeof(addresses[1])};
    check_equal(native_submit(&fixture, 21u, &receive).status,
                CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(native_submit(&fixture, 22u, &send_operation).status,
                CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(native_fixture_wait(&fixture, 2u), TURBO_OK);
    check_equal(received, payload, sizeof(payload));
    check_true(receive.native.address_length > 0u);
    for (size_t i = 0u; i < fixture.completions.count; ++i) {
        check_equal(fixture.completions.values[i].kind,
                    CFLOW_IO_COMPLETION_OK);
        check_equal(fixture.completions.values[i].bytes, sizeof(payload));
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, fixture.completions.ids[i]),
                    CFLOW_IO_ACK_RELEASED);
    }
    native_test_close_socket(sockets[0]);
    native_test_close_socket(sockets[1]);
    native_fixture_destroy(&fixture);
}

static void native_check_same_tcp_socket_bidirectional(
    cflow_io_native_backend_kind kind) {
    static const unsigned char incoming[] = {0x41u};
    static const unsigned char outgoing[] = {0x42u};
    native_fixture fixture;
    native_test_socket sockets[2];
    native_test_operation receive = {0};
    native_test_operation send_operation = {0};
    unsigned char received[NATIVE_TEST_PAYLOAD_CAPACITY] = {0};

    check_equal(native_fixture_init(&fixture, kind, 2u), TURBO_OK);
    check_equal(native_test_make_tcp_pair(sockets), TURBO_OK);
    check_equal(send(sockets[1], (const char *)incoming,
                     (int)sizeof(incoming), 0), (int)sizeof(incoming));
    receive.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)sockets[0], received,
        sizeof(incoming), NULL, 0u, 0u};
    send_operation.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_SEND, (uintptr_t)sockets[0],
        (void *)outgoing, sizeof(outgoing), NULL, 0u, 0u};
    check_equal(native_submit(&fixture, 41u, &receive).status,
                CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(native_submit(&fixture, 42u, &send_operation).status,
                CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(native_fixture_wait(&fixture, 2u), TURBO_OK);
    check_equal(received, incoming, sizeof(incoming));
    for (size_t index = 0u; index < fixture.completions.count; ++index) {
        check_equal(fixture.completions.values[index].kind,
                    CFLOW_IO_COMPLETION_OK);
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, fixture.completions.ids[index]),
                    CFLOW_IO_ACK_RELEASED);
    }
    native_test_close_socket(sockets[0]);
    native_test_close_socket(sockets[1]);
    native_fixture_destroy(&fixture);
}

static void native_check_cancel(cflow_io_native_backend_kind kind) {
    native_fixture fixture;
    native_test_socket sockets[2];
    native_test_operation receive = {0};
    unsigned char received[NATIVE_TEST_PAYLOAD_CAPACITY] = {0};
    cflow_io_submit_result submitted;

    check_equal(native_fixture_init(&fixture, kind, 1u), TURBO_OK);
    check_equal(native_test_make_tcp_pair(sockets), TURBO_OK);
    receive.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)sockets[1], received,
        sizeof(received), NULL, 0u, 0u};
    submitted = native_submit(&fixture, 31u, &receive);
    check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
    (void)cflow_io_actor_run_ready(&fixture.actor, 8u);
    check_equal(cflow_io_actor_try_cancel(
                    &fixture.actor, submitted.request_id),
                CFLOW_IO_CANCEL_ACCEPTED);
    check_equal(native_fixture_wait(&fixture, 1u), TURBO_OK);
    check_equal(fixture.completions.values[0].kind,
                CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(cflow_io_actor_acknowledge(
                    &fixture.actor, submitted.request_id),
                CFLOW_IO_ACK_RELEASED);
    native_test_close_socket(sockets[0]);
    native_test_close_socket(sockets[1]);
    check_equal(cflow_io_native_backend_forget_socket(
                    &fixture.backend, (uintptr_t)sockets[1]),
                TURBO_OK);
    native_fixture_destroy(&fixture);
}

#if !defined(_WIN32)
static void native_check_rejects_truncated_socket(
    cflow_io_native_backend_kind kind) {
    native_fixture fixture;
    native_test_operation receive = {0};
    unsigned char received = 0u;
    cflow_io_submit_result submitted;

    check_equal(native_fixture_init(&fixture, kind, 1u), TURBO_OK);
    receive.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)INT_MAX + 1u, &received,
        sizeof(received), NULL, 0u, 0u};
    submitted = native_submit(&fixture, 61u, &receive);
    check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
    check_equal(native_fixture_wait(&fixture, 1u), TURBO_OK);
    check_equal(fixture.completions.values[0].kind,
                CFLOW_IO_COMPLETION_FAILED);
    check_equal(fixture.completions.values[0].error, TURBO_EINVAL);
    check_equal(cflow_io_actor_acknowledge(
                    &fixture.actor, submitted.request_id),
                CFLOW_IO_ACK_RELEASED);
    native_fixture_destroy(&fixture);
}

static void native_check_preserves_caller_socket_flags(
    cflow_io_native_backend_kind kind) {
    native_fixture fixture;
    native_test_socket sockets[2];
    native_test_operation receive = {0};
    unsigned char received = 0u;
    cflow_io_submit_result submitted;
    cflow_io_native_backend_stats stats;
    int flags_before = 0;
    int flags_after = 0;

    check_equal(native_fixture_init(&fixture, kind, 1u), TURBO_OK);
    check_equal(native_test_make_tcp_pair(sockets), TURBO_OK);
    check_equal(native_test_set_blocking(sockets[1]), TURBO_OK);
    check_equal(native_test_status_flags(sockets[1], &flags_before), TURBO_OK);
    receive.native = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)sockets[1], &received,
        sizeof(received), NULL, 0u, 0u};
    submitted = native_submit(&fixture, 62u, &receive);
    check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
    (void)cflow_io_actor_run_ready(&fixture.actor, 8u);
    check_true(cflow_io_native_backend_get_stats(&fixture.backend, &stats));
    check_equal(stats.submitted, 1u);
    check_equal(stats.active_requests, 1u);
    check_equal(native_test_status_flags(sockets[1], &flags_after), TURBO_OK);
    check_equal(flags_after, flags_before);
    check_equal(cflow_io_actor_try_cancel(
                    &fixture.actor, submitted.request_id),
                CFLOW_IO_CANCEL_ACCEPTED);
    check_equal(native_fixture_wait(&fixture, 1u), TURBO_OK);
    check_equal(fixture.completions.values[0].kind,
                CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(cflow_io_actor_acknowledge(
                    &fixture.actor, submitted.request_id),
                CFLOW_IO_ACK_RELEASED);
    native_test_close_socket(sockets[0]);
    native_test_close_socket(sockets[1]);
    check_equal(cflow_io_native_backend_forget_socket(
                    &fixture.backend, (uintptr_t)sockets[1]),
                TURBO_OK);
    native_fixture_destroy(&fixture);
}
#endif

#if defined(__linux__)
static void native_check_cancelled_slot_reuse(
    cflow_io_native_backend_kind kind) {
    static const unsigned char payload[] = {0x5au};
    native_fixture fixture;
    native_test_socket sockets[2];
    unsigned char received[NATIVE_TEST_PAYLOAD_CAPACITY];

    check_equal(native_fixture_init(&fixture, kind, 1u), TURBO_OK);
    check_equal(native_test_make_tcp_pair(sockets), TURBO_OK);
    for (size_t iteration = 0u;
         iteration < NATIVE_TEST_CANCEL_REUSE_ITERATIONS; ++iteration) {
        native_test_operation cancelled = {0};
        native_test_operation replacement = {0};
        cflow_io_submit_result submitted;

        memset(received, 0, sizeof(received));
        cancelled.native = (cflow_io_native_operation){
            CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)sockets[1], received,
            sizeof(received), NULL, 0u, 0u};
        submitted = native_submit(&fixture, 51u, &cancelled);
        check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&fixture.actor, 8u);
        check_equal(cflow_io_actor_try_cancel(
                        &fixture.actor, submitted.request_id),
                    CFLOW_IO_CANCEL_ACCEPTED);
        check_equal(native_fixture_wait(&fixture, 1u), TURBO_OK);
        check_equal(fixture.completions.values[0].kind,
                    CFLOW_IO_COMPLETION_CANCELLED);
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, submitted.request_id),
                    CFLOW_IO_ACK_RELEASED);
        fixture.completions.count = 0u;

        replacement.native = (cflow_io_native_operation){
            CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)sockets[1], received,
            sizeof(payload), NULL, 0u, 0u};
        submitted = native_submit(&fixture, 52u, &replacement);
        check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&fixture.actor, 8u);
        for (size_t attempt = 0u;
             attempt < NATIVE_TEST_CANCEL_SETTLE_YIELDS; ++attempt) {
            (void)cflow_io_actor_run_ready(&fixture.actor, 8u);
            (void)cflow_executor_run_ready(&fixture.executor);
            turbo_thread_yield();
        }
        check_equal(fixture.completions.count, 0u);
        check_equal(send(sockets[0], (const char *)payload,
                         (int)sizeof(payload), 0), (int)sizeof(payload));
        check_equal(native_fixture_wait(&fixture, 1u), TURBO_OK);
        check_equal(fixture.completions.values[0].kind,
                    CFLOW_IO_COMPLETION_OK);
        check_equal(fixture.completions.values[0].bytes, sizeof(payload));
        check_equal(received, payload, sizeof(payload));
        check_equal(cflow_io_actor_acknowledge(
                        &fixture.actor, submitted.request_id),
                    CFLOW_IO_ACK_RELEASED);
        fixture.completions.count = 0u;
    }
    native_test_close_socket(sockets[0]);
    native_test_close_socket(sockets[1]);
    check_equal(cflow_io_native_backend_forget_socket(
                    &fixture.backend, (uintptr_t)sockets[1]),
                TURBO_OK);
    native_fixture_destroy(&fixture);
}
#endif

static void native_check_backend(cflow_io_native_backend_kind kind) {
    native_check_tcp(kind);
    native_check_udp(kind);
    native_check_same_tcp_socket_bidirectional(kind);
    native_check_cancel(kind);
#if !defined(_WIN32)
    native_check_rejects_truncated_socket(kind);
    native_check_preserves_caller_socket_flags(kind);
#endif
}

spec("CFlow native IO backend") {
    it("rejects invalid bounded configuration without mutating output") {
        cflow_io_native_backend backend = {(void *)(uintptr_t)1u};
        cflow_io_native_backend_config config = {
            CFLOW_IO_NATIVE_IOCP, 0u, 1u};
        check_equal(cflow_io_native_backend_init(&backend, &config),
                    TURBO_EINVAL);
        check_null(backend.impl);

        backend.impl = (void *)(uintptr_t)1u;
        config.request_capacity = 1u;
        config.completion_batch_capacity = 0u;
        check_equal(cflow_io_native_backend_init(&backend, &config),
                    TURBO_EINVAL);
        check_null(backend.impl);
    }

    it("exposes complete Actor strategy callbacks") {
        cflow_io_backend_ops ops = cflow_io_native_backend_actor_ops();
        check_not_null(ops.submit);
        check_not_null(ops.cancel);
    }

#if defined(_WIN32)
    it("runs TCP UDP and cancellation through IOCP") {
        check_true(cflow_io_native_backend_supported(CFLOW_IO_NATIVE_IOCP));
        native_check_backend(CFLOW_IO_NATIVE_IOCP);
    }
#elif defined(__APPLE__)
    it("runs TCP UDP and cancellation through kqueue") {
        check_true(cflow_io_native_backend_supported(CFLOW_IO_NATIVE_KQUEUE));
        native_check_backend(CFLOW_IO_NATIVE_KQUEUE);
    }
#elif defined(__linux__)
#if defined(CFLOW_TEST_NATIVE_EPOLL)
    it("runs TCP UDP and cancellation through epoll") {
        check_true(cflow_io_native_backend_supported(CFLOW_IO_NATIVE_EPOLL));
        native_check_backend(CFLOW_IO_NATIVE_EPOLL);
    }
#endif
    it("runs TCP UDP and cancellation through io_uring when available") {
        if (cflow_io_native_backend_supported(CFLOW_IO_NATIVE_IO_URING)) {
            cflow_io_native_backend probe = {0};
            cflow_io_native_backend_config config = {
                CFLOW_IO_NATIVE_IO_URING, 1u, 1u};
            const int status = cflow_io_native_backend_init(&probe, &config);
            if (status == TURBO_OK) {
                check_equal(cflow_io_native_backend_shutdown(&probe), TURBO_OK);
                check_equal(cflow_io_native_backend_destroy(&probe), TURBO_OK);
                native_check_backend(CFLOW_IO_NATIVE_IO_URING);
                native_check_cancelled_slot_reuse(
                    CFLOW_IO_NATIVE_IO_URING);
            } else {
                check_true(status < 0);
                check_null(probe.impl);
            }
        }
    }
#endif
}
