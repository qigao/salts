#include <cflow/cflow.h>

#include "tinytest.h"

#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET native_adapter_test_socket;
typedef int native_adapter_test_socklen;
  #define NATIVE_ADAPTER_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int native_adapter_test_socket;
typedef socklen_t native_adapter_test_socklen;
  #define NATIVE_ADAPTER_TEST_INVALID_SOCKET (-1)
#endif

enum { NATIVE_ADAPTER_TEST_TIMEOUT_MS = 5000 };

typedef struct native_adapter_test_operation {
    turbo_io_operation native;
    size_t *release_count;
} native_adapter_test_operation;

typedef struct native_adapter_test_completions {
    cflow_io_request_id ids[2];
    cflow_io_completion values[2];
    size_t count;
} native_adapter_test_completions;

typedef struct native_adapter_test_source_fixture {
    native_adapter_test_operation operations[2];
    size_t operation_count;
    size_t prepared;
    size_t encoded;
    size_t release_count;
    size_t drive_count;
} native_adapter_test_source_fixture;

typedef struct native_adapter_test_sink_probe {
    int values[2];
    size_t value_count;
    size_t error_count;
    size_t done_count;
    const char *error;
} native_adapter_test_sink_probe;

static int native_adapter_test_last_error(void) {
#if defined(_WIN32)
    return -(int)WSAGetLastError();
#else
    return -errno;
#endif
}

static void native_adapter_test_close_socket(
    native_adapter_test_socket socket_value) {
    if (socket_value == NATIVE_ADAPTER_TEST_INVALID_SOCKET)
        return;
#if defined(_WIN32)
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
}

static int native_adapter_test_bind_loopback(
    native_adapter_test_socket socket_value,
    struct sockaddr_in *address) {
    native_adapter_test_socklen address_length =
        (native_adapter_test_socklen)sizeof(*address);

    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address->sin_port = 0;
    if (bind(socket_value, (const struct sockaddr *)address,
             (native_adapter_test_socklen)sizeof(*address)) != 0)
        return native_adapter_test_last_error();
    if (getsockname(socket_value, (struct sockaddr *)address,
                    &address_length) != 0)
        return native_adapter_test_last_error();
    return TURBO_OK;
}

static int native_adapter_test_make_tcp_pair(
    native_adapter_test_socket sockets[2]) {
    native_adapter_test_socket listener = NATIVE_ADAPTER_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    int status;

    sockets[0] = NATIVE_ADAPTER_TEST_INVALID_SOCKET;
    sockets[1] = NATIVE_ADAPTER_TEST_INVALID_SOCKET;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == NATIVE_ADAPTER_TEST_INVALID_SOCKET)
        return native_adapter_test_last_error();
    status = native_adapter_test_bind_loopback(listener, &address);
    if (status == TURBO_OK && listen(listener, 1) != 0)
        status = native_adapter_test_last_error();
    if (status == TURBO_OK) {
        sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockets[0] == NATIVE_ADAPTER_TEST_INVALID_SOCKET)
            status = native_adapter_test_last_error();
    }
    if (status == TURBO_OK &&
        connect(sockets[0], (const struct sockaddr *)&address,
                (native_adapter_test_socklen)sizeof(address)) != 0)
        status = native_adapter_test_last_error();
    if (status == TURBO_OK) {
        sockets[1] = accept(listener, NULL, NULL);
        if (sockets[1] == NATIVE_ADAPTER_TEST_INVALID_SOCKET)
            status = native_adapter_test_last_error();
    }
    native_adapter_test_close_socket(listener);
    if (status != TURBO_OK) {
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        sockets[0] = NATIVE_ADAPTER_TEST_INVALID_SOCKET;
        sockets[1] = NATIVE_ADAPTER_TEST_INVALID_SOCKET;
    }
    return status;
}

static void native_adapter_test_release(void *operation_user) {
    native_adapter_test_operation *operation =
        (native_adapter_test_operation *)operation_user;
    ++*operation->release_count;
}

static void native_adapter_test_complete(
    void *completion_user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion) {
    native_adapter_test_completions *probe =
        (native_adapter_test_completions *)completion_user;
    (void)lease_id;
    (void)operation_user;
    if (probe->count >= 2u)
        return;
    probe->ids[probe->count] = request_id;
    probe->values[probe->count] = *completion;
    ++probe->count;
}

static cflow_io_source_prepare_status native_adapter_test_source_prepare(
    void *user, cflow_io_operation *operation, const char **error) {
    native_adapter_test_source_fixture *fixture =
        (native_adapter_test_source_fixture *)user;

    (void)error;
    if (fixture->prepared >= fixture->operation_count)
        return CFLOW_IO_SOURCE_PREPARE_DONE;
    operation->user = &fixture->operations[fixture->prepared++];
    operation->release = native_adapter_test_release;
    return CFLOW_IO_SOURCE_PREPARE_OPERATION;
}

static cflow_read_status native_adapter_test_source_encode(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion,
    void *out_value,
    const char **error) {
    static const char completion_error[] =
        "NativeIO source received a non-success completion";
    native_adapter_test_source_fixture *fixture =
        (native_adapter_test_source_fixture *)user;

    (void)request_id;
    (void)lease_id;
    (void)operation_user;
    if (completion->kind != CFLOW_IO_COMPLETION_OK) {
        *error = completion_error;
        return CFLOW_READ_ERROR;
    }
    *(int *)out_value = (int)completion->bytes;
    ++fixture->encoded;
    return CFLOW_READ_VALUE;
}

static void native_adapter_test_source_drive(void *user) {
    native_adapter_test_source_fixture *fixture =
        (native_adapter_test_source_fixture *)user;
    ++fixture->drive_count;
}

static bool native_adapter_test_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    native_adapter_test_sink_probe *probe =
        (native_adapter_test_sink_probe *)user;

    (void)type;
    if (probe->value_count < 2u)
        probe->values[probe->value_count] = *(const int *)value;
    ++probe->value_count;
    return true;
}

static void native_adapter_test_sink_error(void *user, const char *message) {
    native_adapter_test_sink_probe *probe =
        (native_adapter_test_sink_probe *)user;
    ++probe->error_count;
    probe->error = message;
}

static void native_adapter_test_sink_done(void *user) {
    native_adapter_test_sink_probe *probe =
        (native_adapter_test_sink_probe *)user;
    ++probe->done_count;
}

static turbo_io_backend_kind native_adapter_test_backend(void) {
#if defined(_WIN32)
    return TURBO_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return TURBO_IO_BACKEND_EPOLL;
#else
    return TURBO_IO_BACKEND_KQUEUE;
#endif
}

spec("CFlow NativeIO Actor adapter") {
    it("preserves NativeIO malformed-capacity errors") {
        cflow_io_native_adapter adapter = {0};
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 0u, 1u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config),
                    TURBO_EINVAL);
        check_null(adapter.impl);
    }

    it("owns one fixed-capacity NativeIO backend") {
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), TURBO_OK);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.native.endpoint_capacity, 2u);
        check_equal(stats.native.request_capacity, 2u);
        check_equal(stats.active_bridges, 0u);
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("forwards explicit pipe capability without fallback") {
        cflow_io_native_adapter adapter = {0};
        turbo_io_endpoint endpoint = {9u, 9u};
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), TURBO_OK);
#if defined(_WIN32)
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, 0u, TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                        &endpoint),
                    TURBO_ENOTSUP);
        check_false(turbo_io_endpoint_valid(endpoint));
        check_equal(cflow_io_native_adapter_release_pipe(&adapter, endpoint),
                    TURBO_EINVAL);
#else
        {
            int descriptors[2] = {-1, -1};
            int flags;
            check_equal(pipe(descriptors), 0);
            flags = fcntl(descriptors[0], F_GETFL, 0);
            check_true(flags >= 0);
            check_equal(fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK), 0);
            check_equal(cflow_io_native_adapter_attach_pipe(
                            &adapter, (uintptr_t)descriptors[0],
                            TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                        TURBO_OK);
            check_equal(close(descriptors[0]), 0);
            check_equal(close(descriptors[1]), 0);
            check_equal(cflow_io_native_adapter_release_pipe(&adapter, endpoint),
                        TURBO_OK);
        }
#endif
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("round trips one TCP payload through Actor on the owner thread") {
        static const unsigned char payload[] = {0x41u, 0x42u, 0x43u, 0x44u};
        unsigned char received[sizeof(payload)] = {0};
        native_adapter_test_socket sockets[2];
        turbo_io_endpoint endpoints[2] = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        size_t release_count = 0u;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operations[2] = {
            {{TURBO_IO_TCP_RECV, {0}, received, sizeof(received), 17u,
              NULL, 0u, 0u}, &release_count},
            {{TURBO_IO_TCP_SEND, {0}, (void *)payload, sizeof(payload), 29u,
              NULL, 0u, 0u}, &release_count}};
        cflow_io_operation actor_operations[2] = {
            {&operations[0], native_adapter_test_release},
            {&operations[1], native_adapter_test_release}};
        cflow_io_submit_result submitted[2];

        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[0], &endpoints[0]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[1], &endpoints[1]),
                    TURBO_OK);
        operations[0].native.endpoint = endpoints[1];
        operations[1].native.endpoint = endpoints[0];

        check_true(cflow_executor_manual_init_with_capacity(&executor, 2u));
        actor_config.request_capacity = 2u;
        actor_config.command_capacity = 2u;
        actor_config.executor = &executor;
        actor_config.backend = cflow_io_native_adapter_actor_ops();
        actor_config.backend_user = &adapter;
        actor_config.completion = native_adapter_test_complete;
        actor_config.completion_user = &completions;
        check_equal(cflow_io_actor_init(&actor, &actor_config), TURBO_OK);

        submitted[0] = cflow_io_actor_try_submit(&actor, 101u,
                                                  &actor_operations[0]);
        submitted[1] = cflow_io_actor_try_submit(&actor, 102u,
                                                  &actor_operations[1]);
        check_equal(submitted[0].status, CFLOW_IO_SUBMIT_ACCEPTED);
        check_equal(submitted[1].status, CFLOW_IO_SUBMIT_ACCEPTED);
        check_equal(cflow_io_actor_run_ready(&actor, 8u).status,
                    CFLOW_IO_RUN_PROGRESSED);

        while (observed < 2u) {
            size_t batch = 0u;
            check_equal(cflow_io_native_adapter_observe(
                            &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &batch),
                        TURBO_OK);
            observed += batch;
        }
        (void)cflow_io_actor_run_ready(&actor, 8u);
        (void)cflow_executor_run_ready(&executor);
        check_equal(completions.count, 2u);
        check_equal(received, payload, sizeof(payload));
        for (size_t index = 0u; index < completions.count; ++index) {
            check_equal(completions.values[index].kind,
                        CFLOW_IO_COMPLETION_OK);
            check_equal(cflow_io_actor_acknowledge(&actor,
                                                    completions.ids[index]),
                        CFLOW_IO_ACK_RELEASED);
        }
        check_equal(release_count, 2u);

        check_equal(cflow_io_actor_close(&actor), TURBO_OK);
        check_true(cflow_io_actor_is_quiescent(&actor));
        check_equal(cflow_io_actor_destroy(&actor), TURBO_OK);
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        check_equal(cflow_io_native_adapter_release_socket(&adapter,
                                                            endpoints[0]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_release_socket(&adapter,
                                                            endpoints[1]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
        check_true(cflow_executor_shutdown(&executor));
        cflow_executor_destroy(&executor);
    }

    it("rolls back rejected submit and keeps one Actor binding") {
        unsigned char byte = 0u;
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        cflow_io_actor first_actor = {(void *)(uintptr_t)1u};
        cflow_io_actor second_actor = {(void *)(uintptr_t)2u};
        cflow_io_backend_ops ops;
        turbo_io_operation operation = {
            TURBO_IO_TCP_RECV, {1u, 1u}, &byte, 1u, 0u,
            NULL, 0u, 0u};
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), TURBO_OK);
        ops = cflow_io_native_adapter_actor_ops();
        check_equal(ops.submit(&adapter, &first_actor, 1u, 0u, &operation),
                    TURBO_ENOENT);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.active_bridges, 0u);
        check_equal(ops.submit(&adapter, &second_actor, 1u, 0u, &operation),
                    TURBO_EINVAL);
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("reclaims a bridge when its Actor no longer accepts completion") {
        unsigned char byte = 0x61u;
        native_adapter_test_socket sockets[2];
        turbo_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        cflow_io_actor stale_actor = {0};
        cflow_io_backend_ops ops;
        turbo_io_operation operation = {
            TURBO_IO_TCP_SEND, {0}, &byte, 1u, 0u, NULL, 0u, 0u};
        size_t observed = 0u;
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[0], &endpoint),
                    TURBO_OK);
        operation.endpoint = endpoint;
        ops = cflow_io_native_adapter_actor_ops();
        check_equal(ops.submit(&adapter, &stale_actor, 301u, 0u, &operation),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_observe(
                        &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &observed),
                    TURBO_OK);
        check_equal(observed, 1u);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.active_bridges, 0u);
        check_equal(stats.actor_completions, (uint64_t)0u);
        check_equal(stats.stale_actor_completions, (uint64_t)1u);

        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        check_equal(cflow_io_native_adapter_release_socket(&adapter, endpoint),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("applies fixed-capacity backpressure without a fallback queue") {
        unsigned char bytes[2] = {0};
        native_adapter_test_socket sockets[2];
        turbo_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        cflow_io_actor actor = {0};
        cflow_io_backend_ops ops;
        turbo_io_operation operations[2] = {
            {TURBO_IO_TCP_RECV, {0}, &bytes[0], 1u, 0u, NULL, 0u, 0u},
            {TURBO_IO_TCP_RECV, {0}, &bytes[1], 1u, 0u, NULL, 0u, 0u}};
        size_t observed = 0u;
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[1], &endpoint),
                    TURBO_OK);
        operations[0].endpoint = endpoint;
        operations[1].endpoint = endpoint;
        ops = cflow_io_native_adapter_actor_ops();
        check_equal(ops.submit(&adapter, &actor, 401u, 0u, &operations[0]),
                    TURBO_OK);
        check_equal(ops.submit(&adapter, &actor, 402u, 0u, &operations[1]),
                    TURBO_ENOBUFS);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.active_bridges, 1u);
        check_equal(stats.native.active_requests, 1u);
        check_equal(ops.cancel(&adapter, 401u), TURBO_OK);
        check_equal(ops.cancel(&adapter, 401u), TURBO_OK);
        check_equal(cflow_io_native_adapter_observe(
                        &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &observed),
                    TURBO_OK);
        check_equal(observed, 1u);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.active_bridges, 0u);
        check_equal(stats.native.active_requests, 0u);

        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        check_equal(cflow_io_native_adapter_release_socket(&adapter, endpoint),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("retains a cancelled receive until NativeIO publishes its terminal") {
        unsigned char received = 0u;
        native_adapter_test_socket sockets[2];
        turbo_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        size_t release_count = 0u;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operation = {
            {TURBO_IO_TCP_RECV, {0}, &received, 1u, 0u,
             NULL, 0u, 0u},
            &release_count};
        cflow_io_operation actor_operation = {
            &operation, native_adapter_test_release};
        cflow_io_submit_result submitted;

        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[1], &endpoint),
                    TURBO_OK);
        operation.native.endpoint = endpoint;
        check_true(cflow_executor_manual_init_with_capacity(&executor, 1u));
        actor_config.request_capacity = 1u;
        actor_config.command_capacity = 2u;
        actor_config.executor = &executor;
        actor_config.backend = cflow_io_native_adapter_actor_ops();
        actor_config.backend_user = &adapter;
        actor_config.completion = native_adapter_test_complete;
        actor_config.completion_user = &completions;
        check_equal(cflow_io_actor_init(&actor, &actor_config), TURBO_OK);

        submitted = cflow_io_actor_try_submit(&actor, 201u,
                                               &actor_operation);
        check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&actor, 4u);
        check_equal(cflow_io_actor_try_cancel(&actor, submitted.request_id),
                    CFLOW_IO_CANCEL_ACCEPTED);
        (void)cflow_io_actor_run_ready(&actor, 4u);
        check_equal(cflow_io_native_adapter_observe(
                        &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &observed),
                    TURBO_OK);
        check_equal(observed, 1u);
        (void)cflow_io_actor_run_ready(&actor, 4u);
        (void)cflow_executor_run_ready(&executor);
        check_equal(completions.count, 1u);
        check_equal(completions.values[0].kind,
                    CFLOW_IO_COMPLETION_CANCELLED);
        check_equal(cflow_io_actor_acknowledge(&actor, completions.ids[0]),
                    CFLOW_IO_ACK_RELEASED);
        check_equal(release_count, 1u);

        check_equal(cflow_io_actor_close(&actor), TURBO_OK);
        check_true(cflow_io_actor_is_quiescent(&actor));
        check_equal(cflow_io_actor_destroy(&actor), TURBO_OK);
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        check_equal(cflow_io_native_adapter_release_socket(&adapter, endpoint),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
        check_true(cflow_executor_shutdown(&executor));
        cflow_executor_destroy(&executor);
    }

    it("feeds a windowed Source from one owner-driven NativeIO backend") {
        static const unsigned char payload[] = {0x51u, 0x52u, 0x53u, 0x54u};
        unsigned char received[sizeof(payload)] = {0};
        native_adapter_test_socket sockets[2];
        turbo_io_endpoint endpoints[2] = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_source_owner owner = {0};
        cflow_source source = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        native_adapter_test_source_fixture fixture = {0};
        native_adapter_test_sink_probe sink_probe = {0};
        cflow_sink_callbacks sink_callbacks = {
            native_adapter_test_sink_value,
            native_adapter_test_sink_error,
            native_adapter_test_sink_done,
            &sink_probe};
        cflow_sink sink = cflow_sink_from_callbacks(&sink_callbacks);
        size_t observed = 0u;
        size_t progressed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};
        cflow_io_source_config source_config = {0};

        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[0], &endpoints[0]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[1], &endpoints[1]),
                    TURBO_OK);

        fixture.operation_count = 2u;
        fixture.operations[0].native = (turbo_io_operation){
            TURBO_IO_TCP_RECV, endpoints[1], received, sizeof(received), 0u,
            NULL, 0u, 0u};
        fixture.operations[0].release_count = &fixture.release_count;
        fixture.operations[1].native = (turbo_io_operation){
            TURBO_IO_TCP_SEND, endpoints[0], (void *)payload, sizeof(payload),
            0u, NULL, 0u, 0u};
        fixture.operations[1].release_count = &fixture.release_count;
        source_config.name = "native-io-windowed-source";
        source_config.type = &cmeta_type_int;
        source_config.backend = cflow_io_native_adapter_actor_ops();
        source_config.backend_user = &adapter;
        source_config.prepare = native_adapter_test_source_prepare;
        source_config.encode = native_adapter_test_source_encode;
        source_config.user = &fixture;
        source_config.drive = native_adapter_test_source_drive;
        source_config.drive_user = &fixture;

        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_equal(cflow_source_from_io_actor_windowed(
                        &source, &owner, &source_config, 2u),
                    TURBO_OK);
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 2u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(fixture.prepared, 2u);
        check_equal(cflow_io_source_owner_run_ready(
                        &owner, 32u, &progressed), TURBO_OK);
        check_true(progressed > 0u);

        while (observed < 2u) {
            size_t batch = 0u;
            check_equal(cflow_io_native_adapter_observe(
                            &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &batch),
                        TURBO_OK);
            observed += batch;
        }
        progressed = 0u;
        check_equal(cflow_io_source_owner_run_ready(
                        &owner, 64u, &progressed), TURBO_OK);
        check_true(progressed > 0u);
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);

        check_equal(received, payload, sizeof(payload));
        check_equal(sink_probe.value_count, 2u);
        check_equal(sink_probe.error_count, 0u);
        check_equal(fixture.encoded, 2u);
        check_equal(fixture.release_count, 2u);
        check_equal(sink_probe.values[0], (int)sizeof(payload));
        check_equal(sink_probe.values[1], (int)sizeof(payload));

        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(sink_probe.done_count, 1u);
        check_true(cflow_run_is_done(&run));
        cflow_run_close(&run);
        check_equal(cflow_io_source_owner_close(&owner), TURBO_OK);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);

        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        check_equal(cflow_io_native_adapter_release_socket(
                        &adapter, endpoints[0]), TURBO_OK);
        check_equal(cflow_io_native_adapter_release_socket(
                        &adapter, endpoints[1]), TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("drains a pending NativeIO terminal after Run close") {
        unsigned char received = 0u;
        native_adapter_test_socket sockets[2];
        turbo_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_source_owner owner = {0};
        cflow_source source = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_run run = {0};
        native_adapter_test_source_fixture fixture = {0};
        native_adapter_test_sink_probe sink_probe = {0};
        cflow_sink_callbacks sink_callbacks = {
            native_adapter_test_sink_value,
            native_adapter_test_sink_error,
            native_adapter_test_sink_done,
            &sink_probe};
        cflow_sink sink = cflow_sink_from_callbacks(&sink_callbacks);
        cflow_io_native_adapter_stats adapter_stats = {0};
        size_t progressed = 0u;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};
        cflow_io_source_config source_config = {0};

        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[1], &endpoint),
                    TURBO_OK);
        fixture.operation_count = 1u;
        fixture.operations[0].native = (turbo_io_operation){
            TURBO_IO_TCP_RECV, endpoint, &received, 1u, 0u,
            NULL, 0u, 0u};
        fixture.operations[0].release_count = &fixture.release_count;
        source_config.name = "native-io-cancelled-source";
        source_config.type = &cmeta_type_int;
        source_config.backend = cflow_io_native_adapter_actor_ops();
        source_config.backend_user = &adapter;
        source_config.prepare = native_adapter_test_source_prepare;
        source_config.encode = native_adapter_test_source_encode;
        source_config.user = &fixture;
        source_config.drive = native_adapter_test_source_drive;
        source_config.drive_user = &fixture;

        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_test_init(&scheduler));
        check_equal(cflow_source_from_io_actor_windowed(
                        &source, &owner, &source_config, 1u),
                    TURBO_OK);
        check_true(cflow_run_open(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_run_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(cflow_io_source_owner_run_ready(
                        &owner, 32u, &progressed), TURBO_OK);
        check_true(cflow_io_native_adapter_get_stats(&adapter,
                                                      &adapter_stats));
        check_equal(adapter_stats.active_bridges, 1u);

        cflow_run_close(&run);
        progressed = 0u;
        check_equal(cflow_io_source_owner_run_ready(
                        &owner, 32u, &progressed), TURBO_OK);
        check_true(progressed > 0u);
        check_equal(cflow_io_native_adapter_observe(
                        &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &observed),
                    TURBO_OK);
        check_equal(observed, 1u);
        while (!cflow_io_source_owner_is_quiescent(&owner)) {
            progressed = 0u;
            check_equal(cflow_io_source_owner_run_ready(
                            &owner, 64u, &progressed), TURBO_OK);
            check_true(progressed > 0u);
        }
        check_equal(fixture.encoded, 0u);
        check_equal(fixture.release_count, 1u);
        check_equal(sink_probe.value_count, 0u);
        check_equal(sink_probe.error_count, 0u);
        check_equal(cflow_io_source_owner_close(&owner), TURBO_OK);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);

        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_socket(sockets[0]);
        native_adapter_test_close_socket(sockets[1]);
        check_equal(cflow_io_native_adapter_release_socket(&adapter, endpoint),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }
}
