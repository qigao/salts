#include <cflow/cflow.h>
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

#include "tinytest.h"

#include <stdatomic.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
typedef SOCKET native_adapter_test_socket;
typedef int native_adapter_test_socklen;
typedef HANDLE native_adapter_test_pipe;
  #define NATIVE_ADAPTER_TEST_INVALID_SOCKET INVALID_SOCKET
  #define NATIVE_ADAPTER_TEST_INVALID_PIPE INVALID_HANDLE_VALUE
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int native_adapter_test_socket;
typedef socklen_t native_adapter_test_socklen;
typedef int native_adapter_test_pipe;
  #define NATIVE_ADAPTER_TEST_INVALID_SOCKET (-1)
  #define NATIVE_ADAPTER_TEST_INVALID_PIPE (-1)
#endif

enum {
    NATIVE_ADAPTER_TEST_PIPE_BUFFER_CAPACITY = 4096,
    NATIVE_ADAPTER_TEST_TIMEOUT_MS = 5000,
    NATIVE_ADAPTER_TEST_THREAD_WAIT_SLICE_NS = 10000000,
    NATIVE_ADAPTER_TEST_THREAD_WAIT_LIMIT = 500,
    NATIVE_ADAPTER_TEST_PUBLISHER_QUEUE_CAPACITY = 16
};

enum {
    NATIVE_ADAPTER_TEST_THREAD_ROLE_NONE = 0,
    NATIVE_ADAPTER_TEST_THREAD_ROLE_PUBLISHER,
    NATIVE_ADAPTER_TEST_THREAD_ROLE_SUBSCRIBER
};

static TURBO_THREAD_LOCAL int native_adapter_test_thread_role;

typedef struct native_adapter_test_operation {
    native_io_operation native;
    atomic_size_t *release_count;
    turbo_mutex_t *release_gate;
    turbo_cond_t *release_changed;
} native_adapter_test_operation;

typedef struct native_adapter_test_completions {
    cflow_io_request_id ids[2];
    cflow_io_completion values[2];
    size_t count;
} native_adapter_test_completions;

typedef struct native_adapter_test_source_fixture {
    native_adapter_test_operation operations[2];
    size_t operation_count;
    atomic_size_t prepared;
    atomic_size_t encoded;
    atomic_size_t release_count;
    atomic_size_t drive_count;
} native_adapter_test_source_fixture;

typedef struct native_adapter_test_sink_probe {
    int values[2];
    size_t value_count;
    size_t error_count;
    size_t done_count;
    const char *error;
} native_adapter_test_sink_probe;

typedef struct native_adapter_test_threaded_sink_probe {
    turbo_mutex_t gate;
    turbo_cond_t changed;
    int values[2];
    size_t value_count;
    size_t error_count;
    size_t done_count;
    const char *error;
    atomic_int subscriber_callbacks;
    atomic_int role_collisions;
} native_adapter_test_threaded_sink_probe;

typedef struct native_adapter_test_threaded_driver {
    turbo_threadpool_t *pool;
    cflow_io_native_adapter *adapter;
    cflow_io_publisher_owner *owner;
    turbo_mutex_t gate;
    turbo_cond_t changed;
    bool drive_pending;
    bool stop_requested;
    atomic_int wake_status;
    atomic_int drive_status;
    atomic_size_t tasks;
    atomic_size_t observed;
    atomic_int publisher_callbacks;
    atomic_int role_collisions;
} native_adapter_test_threaded_driver;

typedef struct native_adapter_test_threaded_cleanup {
    cflow_io_native_adapter *adapter;
    cflow_io_publisher_owner *owner;
    native_adapter_test_pipe *pipes;
    native_io_endpoint *endpoints;
    int owner_status;
    int adapter_close_status;
    int release_status[2];
    int destroy_status;
} native_adapter_test_threaded_cleanup;

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

static void native_adapter_test_close_pipe(
    native_adapter_test_pipe pipe_handle) {
    if (pipe_handle == NATIVE_ADAPTER_TEST_INVALID_PIPE)
        return;
#if defined(_WIN32)
    (void)CloseHandle(pipe_handle);
#else
    (void)close(pipe_handle);
#endif
}

static int native_adapter_test_make_pipe_pair(
    native_adapter_test_pipe pipes[2]) {
#if defined(_WIN32)
    static LONG sequence = 0;
    char name[128];
    OVERLAPPED connected = {0};
    HANDLE event = NULL;
    DWORD error = ERROR_SUCCESS;
    BOOL pending = FALSE;
    int name_length;

    pipes[0] = INVALID_HANDLE_VALUE;
    pipes[1] = INVALID_HANDLE_VALUE;
    name_length = snprintf(
        name, sizeof(name), "\\\\.\\pipe\\cflow-native-adapter-%lu-%ld",
        GetCurrentProcessId(), InterlockedIncrement(&sequence));
    if (name_length < 0 || (size_t)name_length >= sizeof(name))
        return TURBO_ERANGE;
    pipes[0] = CreateNamedPipeA(
        name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
        NATIVE_ADAPTER_TEST_PIPE_BUFFER_CAPACITY,
        NATIVE_ADAPTER_TEST_PIPE_BUFFER_CAPACITY, 0u, NULL);
    if (pipes[0] == INVALID_HANDLE_VALUE)
        return -(int)GetLastError();
    event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (event == NULL) {
        error = GetLastError();
        goto failed;
    }
    connected.hEvent = event;
    if (!ConnectNamedPipe(pipes[0], &connected)) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING)
            pending = TRUE;
        else if (error != ERROR_PIPE_CONNECTED)
            goto failed;
    }
    pipes[1] = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0u, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pipes[1] == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto failed;
    }
    if (pending) {
        DWORD transferred = 0u;
        if (!GetOverlappedResult(pipes[0], &connected, &transferred, TRUE)) {
            error = GetLastError();
            goto failed;
        }
    }
    (void)CloseHandle(event);
    return TURBO_OK;

failed:
    native_adapter_test_close_pipe(pipes[1]);
    native_adapter_test_close_pipe(pipes[0]);
    if (event != NULL)
        (void)CloseHandle(event);
    pipes[0] = INVALID_HANDLE_VALUE;
    pipes[1] = INVALID_HANDLE_VALUE;
    return -(int)error;
#else
    int flags;
    if (pipe(pipes) != 0)
        return -errno;
    for (size_t index = 0u; index < 2u; ++index) {
        flags = fcntl(pipes[index], F_GETFL, 0);
        if (flags < 0 || fcntl(pipes[index], F_SETFL,
                               flags | O_NONBLOCK) != 0) {
            int status = -errno;
            native_adapter_test_close_pipe(pipes[0]);
            native_adapter_test_close_pipe(pipes[1]);
            pipes[0] = -1;
            pipes[1] = -1;
            return status;
        }
    }
    return TURBO_OK;
#endif
}

static void native_adapter_test_release(void *operation_user) {
    native_adapter_test_operation *operation =
        (native_adapter_test_operation *)operation_user;
    if (operation->release_gate != NULL)
        turbo_mutex_lock(operation->release_gate);
    atomic_fetch_add(operation->release_count, 1u);
    if (operation->release_changed != NULL)
        turbo_cond_broadcast(operation->release_changed);
    if (operation->release_gate != NULL)
        turbo_mutex_unlock(operation->release_gate);
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

static cflow_io_publisher_prepare_status native_adapter_test_source_prepare(
    void *user, cflow_io_operation *operation, const char **error) {
    native_adapter_test_source_fixture *fixture =
        (native_adapter_test_source_fixture *)user;
    const size_t prepared = atomic_load(&fixture->prepared);

    (void)error;
    if (prepared >= fixture->operation_count)
        return CFLOW_IO_PUBLISHER_PREPARE_DONE;
    operation->user = &fixture->operations[prepared];
    atomic_fetch_add(&fixture->prepared, 1u);
    operation->release = native_adapter_test_release;
    return CFLOW_IO_PUBLISHER_PREPARE_OPERATION;
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
    atomic_fetch_add(&fixture->encoded, 1u);
    return CFLOW_READ_VALUE;
}

static void native_adapter_test_source_drive(void *user) {
    native_adapter_test_source_fixture *fixture =
        (native_adapter_test_source_fixture *)user;
    atomic_fetch_add(&fixture->drive_count, 1u);
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

static void native_adapter_test_record_thread_role(
    int expected_role,
    atomic_int *callbacks,
    atomic_int *collisions) {
    if (native_adapter_test_thread_role != NATIVE_ADAPTER_TEST_THREAD_ROLE_NONE &&
        native_adapter_test_thread_role != expected_role)
        atomic_fetch_add(collisions, 1);
    native_adapter_test_thread_role = expected_role;
    atomic_fetch_add(callbacks, 1);
}

static bool native_adapter_test_threaded_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    native_adapter_test_threaded_sink_probe *probe =
        (native_adapter_test_threaded_sink_probe *)user;

    (void)type;
    native_adapter_test_record_thread_role(
        NATIVE_ADAPTER_TEST_THREAD_ROLE_SUBSCRIBER,
        &probe->subscriber_callbacks,
        &probe->role_collisions);
    turbo_mutex_lock(&probe->gate);
    if (probe->value_count < 2u)
        probe->values[probe->value_count] = *(const int *)value;
    ++probe->value_count;
    turbo_cond_broadcast(&probe->changed);
    turbo_mutex_unlock(&probe->gate);
    return true;
}

static void native_adapter_test_threaded_sink_error(
    void *user, const char *message) {
    native_adapter_test_threaded_sink_probe *probe =
        (native_adapter_test_threaded_sink_probe *)user;

    native_adapter_test_record_thread_role(
        NATIVE_ADAPTER_TEST_THREAD_ROLE_SUBSCRIBER,
        &probe->subscriber_callbacks,
        &probe->role_collisions);
    turbo_mutex_lock(&probe->gate);
    ++probe->error_count;
    probe->error = message;
    turbo_cond_broadcast(&probe->changed);
    turbo_mutex_unlock(&probe->gate);
}

static void native_adapter_test_threaded_sink_done(void *user) {
    native_adapter_test_threaded_sink_probe *probe =
        (native_adapter_test_threaded_sink_probe *)user;

    native_adapter_test_record_thread_role(
        NATIVE_ADAPTER_TEST_THREAD_ROLE_SUBSCRIBER,
        &probe->subscriber_callbacks,
        &probe->role_collisions);
    turbo_mutex_lock(&probe->gate);
    ++probe->done_count;
    turbo_cond_broadcast(&probe->changed);
    turbo_mutex_unlock(&probe->gate);
}

static bool native_adapter_test_threaded_sink_wait(
    native_adapter_test_threaded_sink_probe *probe,
    size_t values,
    size_t dones) {
    size_t waits = 0u;
    bool ready;

    turbo_mutex_lock(&probe->gate);
    while (probe->value_count < values && probe->error_count == 0u &&
           waits < NATIVE_ADAPTER_TEST_THREAD_WAIT_LIMIT) {
        (void)turbo_cond_timedwait(
            &probe->changed, &probe->gate,
            NATIVE_ADAPTER_TEST_THREAD_WAIT_SLICE_NS);
        ++waits;
    }
    while (probe->done_count < dones && probe->error_count == 0u &&
           waits < NATIVE_ADAPTER_TEST_THREAD_WAIT_LIMIT) {
        (void)turbo_cond_timedwait(
            &probe->changed, &probe->gate,
            NATIVE_ADAPTER_TEST_THREAD_WAIT_SLICE_NS);
        ++waits;
    }
    ready = probe->value_count >= values && probe->done_count >= dones &&
            probe->error_count == 0u;
    turbo_mutex_unlock(&probe->gate);
    return ready;
}

static bool native_adapter_test_threaded_release_wait(
    native_adapter_test_threaded_sink_probe *probe,
    const atomic_size_t *release_count,
    size_t expected) {
    size_t waits = 0u;
    bool ready;

    turbo_mutex_lock(&probe->gate);
    while (atomic_load(release_count) < expected &&
           waits < NATIVE_ADAPTER_TEST_THREAD_WAIT_LIMIT) {
        (void)turbo_cond_timedwait(
            &probe->changed, &probe->gate,
            NATIVE_ADAPTER_TEST_THREAD_WAIT_SLICE_NS);
        ++waits;
    }
    ready = atomic_load(release_count) >= expected;
    turbo_mutex_unlock(&probe->gate);
    return ready;
}

static void native_adapter_test_threaded_drive_task(void *user) {
    native_adapter_test_threaded_driver *driver =
        (native_adapter_test_threaded_driver *)user;
    native_adapter_test_record_thread_role(
        NATIVE_ADAPTER_TEST_THREAD_ROLE_PUBLISHER,
        &driver->publisher_callbacks,
        &driver->role_collisions);
    for (;;) {
        size_t observed = 0u;
        int status;

        turbo_mutex_lock(&driver->gate);
        while (!driver->drive_pending && !driver->stop_requested)
            turbo_cond_wait(&driver->changed, &driver->gate);
        if (driver->stop_requested) {
            turbo_mutex_unlock(&driver->gate);
            break;
        }
        driver->drive_pending = false;
        turbo_mutex_unlock(&driver->gate);

        status = cflow_io_native_adapter_drive_publisher(
            driver->adapter, driver->owner,
            UINT32_MAX, 64u, &observed);
        if (status != TURBO_OK) {
            int expected = TURBO_OK;
            (void)atomic_compare_exchange_strong(
                &driver->drive_status, &expected, status);
            break;
        }
        atomic_fetch_add(&driver->observed, observed);
        atomic_fetch_add(&driver->tasks, 1u);
    }
}

static void native_adapter_test_threaded_drive(void *user) {
    native_adapter_test_threaded_driver *driver =
        (native_adapter_test_threaded_driver *)user;
    int status;

    status = cflow_io_native_adapter_wake(driver->adapter);
    turbo_mutex_lock(&driver->gate);
    driver->drive_pending = true;
    if (status != TURBO_OK) {
        int expected = TURBO_OK;
        (void)atomic_compare_exchange_strong(
            &driver->wake_status, &expected, status);
    }
    turbo_cond_signal(&driver->changed);
    turbo_mutex_unlock(&driver->gate);
}

static void native_adapter_test_threaded_stop(
    native_adapter_test_threaded_driver *driver) {
    const int wake_status = cflow_io_native_adapter_wake(driver->adapter);
    turbo_mutex_lock(&driver->gate);
    driver->stop_requested = true;
    turbo_cond_broadcast(&driver->changed);
    turbo_mutex_unlock(&driver->gate);
    if (wake_status != TURBO_OK) {
        int expected = TURBO_OK;
        (void)atomic_compare_exchange_strong(
            &driver->wake_status, &expected, wake_status);
    }
}

static void native_adapter_test_threaded_cleanup_task(void *user) {
    native_adapter_test_threaded_cleanup *cleanup =
        (native_adapter_test_threaded_cleanup *)user;

    native_adapter_test_thread_role =
        NATIVE_ADAPTER_TEST_THREAD_ROLE_PUBLISHER;
    cleanup->owner_status = cflow_io_publisher_owner_close(cleanup->owner);
    if (cleanup->owner_status != TURBO_OK)
        return;
    cleanup->adapter_close_status =
        cflow_io_native_adapter_close(cleanup->adapter);
    if (cleanup->adapter_close_status != TURBO_OK)
        return;
    native_adapter_test_close_pipe(cleanup->pipes[0]);
    native_adapter_test_close_pipe(cleanup->pipes[1]);
    cleanup->pipes[0] = NATIVE_ADAPTER_TEST_INVALID_PIPE;
    cleanup->pipes[1] = NATIVE_ADAPTER_TEST_INVALID_PIPE;
    cleanup->release_status[0] = cflow_io_native_adapter_release_pipe(
        cleanup->adapter, cleanup->endpoints[0]);
    cleanup->release_status[1] = cflow_io_native_adapter_release_pipe(
        cleanup->adapter, cleanup->endpoints[1]);
    if (cleanup->release_status[0] != TURBO_OK ||
        cleanup->release_status[1] != TURBO_OK)
        return;
    cleanup->destroy_status =
        cflow_io_native_adapter_destroy(cleanup->adapter);
}

static native_io_backend_kind native_adapter_test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
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
        native_io_endpoint endpoint = {9u, 9u};
        native_adapter_test_pipe pipes[2] = {
            NATIVE_ADAPTER_TEST_INVALID_PIPE,
            NATIVE_ADAPTER_TEST_INVALID_PIPE};
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), TURBO_OK);
        check_equal(native_adapter_test_make_pipe_pair(pipes), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[0],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                        &endpoint),
                    TURBO_OK);
        native_adapter_test_close_pipe(pipes[0]);
        native_adapter_test_close_pipe(pipes[1]);
        check_equal(cflow_io_native_adapter_release_pipe(&adapter, endpoint),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), TURBO_OK);
    }

    it("round trips one TCP payload through Actor on the owner thread") {
        static const unsigned char payload[] = {0x41u, 0x42u, 0x43u, 0x44u};
        unsigned char received[sizeof(payload)] = {0};
        native_adapter_test_socket sockets[2];
        native_io_endpoint endpoints[2] = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        atomic_size_t release_count;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operations[2] = {
            {{NATIVE_IO_OPERATION_TCP_RECV, {0}, received, sizeof(received), 17u,
              NULL, 0u, 0u}, &release_count},
            {{NATIVE_IO_OPERATION_TCP_SEND, {0}, (void *)payload, sizeof(payload), 29u,
              NULL, 0u, 0u}, &release_count}};
        cflow_io_operation actor_operations[2] = {
            {&operations[0], native_adapter_test_release},
            {&operations[1], native_adapter_test_release}};
        cflow_io_submit_result submitted[2];

        atomic_init(&release_count, 0u);
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
        check_equal(atomic_load(&release_count), 2u);

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

    it("round trips one byte-pipe payload through Actor on the owner thread") {
        static const unsigned char payload[] = {0x70u, 0x69u, 0x70u, 0x65u};
        unsigned char received[sizeof(payload)] = {0};
        native_adapter_test_pipe pipes[2] = {
            NATIVE_ADAPTER_TEST_INVALID_PIPE,
            NATIVE_ADAPTER_TEST_INVALID_PIPE};
        native_io_endpoint endpoints[2] = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        atomic_size_t release_count;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operations[2] = {
            {{NATIVE_IO_OPERATION_PIPE_READ, {0}, received, sizeof(received), 37u,
              NULL, 0u, 0u}, &release_count},
            {{NATIVE_IO_OPERATION_PIPE_WRITE, {0}, (void *)payload, sizeof(payload), 39u,
              NULL, 0u, 0u}, &release_count}};
        cflow_io_operation actor_operations[2] = {
            {&operations[0], native_adapter_test_release},
            {&operations[1], native_adapter_test_release}};
        cflow_io_submit_result submitted[2];

        atomic_init(&release_count, 0u);
        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_pipe_pair(pipes), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[0],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[0]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[1],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[1]),
                    TURBO_OK);
        operations[0].native.endpoint = endpoints[0];
        operations[1].native.endpoint = endpoints[1];

        check_true(cflow_executor_manual_init_with_capacity(&executor, 2u));
        actor_config.request_capacity = 2u;
        actor_config.command_capacity = 2u;
        actor_config.executor = &executor;
        actor_config.backend = cflow_io_native_adapter_actor_ops();
        actor_config.backend_user = &adapter;
        actor_config.completion = native_adapter_test_complete;
        actor_config.completion_user = &completions;
        check_equal(cflow_io_actor_init(&actor, &actor_config), TURBO_OK);

        submitted[0] = cflow_io_actor_try_submit(&actor, 111u,
                                                  &actor_operations[0]);
        submitted[1] = cflow_io_actor_try_submit(&actor, 112u,
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
        check_equal(atomic_load(&release_count), 2u);

        check_equal(cflow_io_actor_close(&actor), TURBO_OK);
        check_true(cflow_io_actor_is_quiescent(&actor));
        check_equal(cflow_io_actor_destroy(&actor), TURBO_OK);
        check_equal(cflow_io_native_adapter_close(&adapter), TURBO_OK);
        native_adapter_test_close_pipe(pipes[0]);
        native_adapter_test_close_pipe(pipes[1]);
        check_equal(cflow_io_native_adapter_release_pipe(&adapter,
                                                          endpoints[0]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_release_pipe(&adapter,
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
        native_io_operation operation = {
            NATIVE_IO_OPERATION_TCP_RECV, {1u, 1u}, &byte, 1u, 0u,
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
        native_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        cflow_io_actor stale_actor = {0};
        cflow_io_backend_ops ops;
        native_io_operation operation = {
            NATIVE_IO_OPERATION_TCP_SEND, {0}, &byte, 1u, 0u, NULL, 0u, 0u};
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
        native_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        cflow_io_actor actor = {0};
        cflow_io_backend_ops ops;
        native_io_operation operations[2] = {
            {NATIVE_IO_OPERATION_TCP_RECV, {0}, &bytes[0], 1u, 0u, NULL, 0u, 0u},
            {NATIVE_IO_OPERATION_TCP_RECV, {0}, &bytes[1], 1u, 0u, NULL, 0u, 0u}};
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
        native_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        atomic_size_t release_count;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operation = {
            {NATIVE_IO_OPERATION_TCP_RECV, {0}, &received, 1u, 0u,
             NULL, 0u, 0u},
            &release_count};
        cflow_io_operation actor_operation = {
            &operation, native_adapter_test_release};
        cflow_io_submit_result submitted;

        atomic_init(&release_count, 0u);
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
        check_equal(atomic_load(&release_count), 1u);

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

    it("runs NativeIO Publisher and Subscriber on separate worker threads") {
        static const unsigned char payload[] = {0x51u, 0x52u, 0x53u, 0x54u};
        unsigned char received[sizeof(payload)] = {0};
        native_adapter_test_pipe pipes[2] = {
            NATIVE_ADAPTER_TEST_INVALID_PIPE,
            NATIVE_ADAPTER_TEST_INVALID_PIPE};
        native_io_endpoint endpoints[2] = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_publisher_owner owner = {0};
        cflow_publisher source = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_subscription run = {0};
        native_adapter_test_source_fixture fixture = {0};
        native_adapter_test_threaded_sink_probe sink_probe = {0};
        native_adapter_test_threaded_driver driver = {0};
        native_adapter_test_threaded_cleanup cleanup = {0};
        turbo_threadpool_t *publisher_pool;
        const turbo_threadpool_config_t pool_config = {
            1, NATIVE_ADAPTER_TEST_PUBLISHER_QUEUE_CAPACITY};
        cflow_subscriber_callbacks sink_callbacks = {
            native_adapter_test_threaded_sink_value,
            native_adapter_test_threaded_sink_error,
            native_adapter_test_threaded_sink_done,
            &sink_probe};
        cflow_subscriber sink = cflow_subscriber_from_callbacks(&sink_callbacks);
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};
        cflow_io_publisher_config source_config = {0};

        atomic_init(&fixture.prepared, 0u);
        atomic_init(&fixture.encoded, 0u);
        atomic_init(&fixture.release_count, 0u);
        atomic_init(&fixture.drive_count, 0u);
        turbo_mutex_init(&sink_probe.gate);
        turbo_cond_init(&sink_probe.changed);
        atomic_init(&sink_probe.subscriber_callbacks, 0);
        atomic_init(&sink_probe.role_collisions, 0);
        publisher_pool = turbo_threadpool_create_with_config(&pool_config);
        check_not_null(publisher_pool);
        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_pipe_pair(pipes), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[0],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[0]),
                    TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[1],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[1]),
                    TURBO_OK);

        fixture.operation_count = 2u;
        fixture.operations[0].native = (native_io_operation){
            NATIVE_IO_OPERATION_PIPE_READ, endpoints[0], received, sizeof(received), 0u,
            NULL, 0u, 0u};
        fixture.operations[0].release_count = &fixture.release_count;
        fixture.operations[0].release_gate = &sink_probe.gate;
        fixture.operations[0].release_changed = &sink_probe.changed;
        fixture.operations[1].native = (native_io_operation){
            NATIVE_IO_OPERATION_PIPE_WRITE, endpoints[1], (void *)payload, sizeof(payload),
            0u, NULL, 0u, 0u};
        fixture.operations[1].release_count = &fixture.release_count;
        fixture.operations[1].release_gate = &sink_probe.gate;
        fixture.operations[1].release_changed = &sink_probe.changed;
        source_config.name = "native-io-windowed-source";
        source_config.type = &cmeta_type_int;
        source_config.backend = cflow_io_native_adapter_actor_ops();
        source_config.backend_user = &adapter;
        source_config.prepare = native_adapter_test_source_prepare;
        source_config.encode = native_adapter_test_source_encode;
        source_config.user = &fixture;
        driver.pool = publisher_pool;
        driver.adapter = &adapter;
        driver.owner = &owner;
        turbo_mutex_init(&driver.gate);
        turbo_cond_init(&driver.changed);
        atomic_init(&driver.wake_status, TURBO_OK);
        atomic_init(&driver.drive_status, TURBO_OK);
        atomic_init(&driver.tasks, 0u);
        atomic_init(&driver.observed, 0u);
        atomic_init(&driver.publisher_callbacks, 0);
        atomic_init(&driver.role_collisions, 0);
        source_config.drive = native_adapter_test_threaded_drive;
        source_config.drive_user = &driver;

        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_scheduler_worker_init_with_capacity(
            &scheduler, 1u, 8u, 1u));
        check_equal(cflow_publisher_from_io_actor_windowed(
                        &source, &owner, &source_config, 2u),
                    TURBO_OK);
        check_true(cflow_subscribe(
            &run, &normalized, &source, &scheduler, &sink));
        check_equal(turbo_threadpool_submit(
                        publisher_pool,
                        native_adapter_test_threaded_drive_task,
                        &driver), TURBO_OK);
        check_true(cflow_subscription_request(&run, 2u));
        check_true(native_adapter_test_threaded_sink_wait(
            &sink_probe, 2u, 0u));
        check_true(cflow_scheduler_wait_idle(&scheduler));
        check_true(native_adapter_test_threaded_release_wait(
            &sink_probe, &fixture.release_count, 2u));
        check_equal(atomic_load(&fixture.prepared), 2u);

        check_equal(received, payload, sizeof(payload));
        check_equal(sink_probe.value_count, 2u);
        check_equal(sink_probe.error_count, 0u);
        check_equal(atomic_load(&fixture.encoded), 2u);
        check_equal(atomic_load(&fixture.release_count), 2u);
        check_equal(sink_probe.values[0], (int)sizeof(payload));
        check_equal(sink_probe.values[1], (int)sizeof(payload));
        check_true(atomic_load(&driver.publisher_callbacks) > 0);
        check_true(atomic_load(&sink_probe.subscriber_callbacks) > 0);
        check_equal(atomic_load(&driver.role_collisions), 0);
        check_equal(atomic_load(&sink_probe.role_collisions), 0);
        check_equal(atomic_load(&driver.wake_status), TURBO_OK);
        check_equal(atomic_load(&driver.drive_status), TURBO_OK);
        check_equal(atomic_load(&driver.observed), (size_t)2u);

        check_true(cflow_subscription_request(&run, 1u));
        check_true(native_adapter_test_threaded_sink_wait(
            &sink_probe, 2u, 1u));
        check_true(cflow_scheduler_wait_idle(&scheduler));
        check_equal(sink_probe.done_count, 1u);
        check_true(cflow_subscription_is_done(&run));
        cflow_subscription_close(&run);
        check_true(cflow_scheduler_wait_idle(&scheduler));
        native_adapter_test_threaded_stop(&driver);
        check_equal(turbo_threadpool_wait_status(publisher_pool), TURBO_OK);

        cleanup.adapter = &adapter;
        cleanup.owner = &owner;
        cleanup.pipes = pipes;
        cleanup.endpoints = endpoints;
        cleanup.owner_status = TURBO_EINVAL;
        cleanup.adapter_close_status = TURBO_EINVAL;
        cleanup.release_status[0] = TURBO_EINVAL;
        cleanup.release_status[1] = TURBO_EINVAL;
        cleanup.destroy_status = TURBO_EINVAL;
        check_equal(turbo_threadpool_submit(
                        publisher_pool,
                        native_adapter_test_threaded_cleanup_task,
                        &cleanup), TURBO_OK);
        check_equal(turbo_threadpool_wait_status(publisher_pool), TURBO_OK);
        check_equal(cleanup.owner_status, TURBO_OK);
        check_equal(cleanup.adapter_close_status, TURBO_OK);
        check_equal(cleanup.release_status[0], TURBO_OK);
        check_equal(cleanup.release_status[1], TURBO_OK);
        check_equal(cleanup.destroy_status, TURBO_OK);

        turbo_threadpool_shutdown(publisher_pool);
        turbo_threadpool_destroy(publisher_pool);
        cflow_scheduler_destroy(&scheduler);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
        turbo_cond_destroy(&sink_probe.changed);
        turbo_mutex_destroy(&sink_probe.gate);
        turbo_cond_destroy(&driver.changed);
        turbo_mutex_destroy(&driver.gate);
    }

    it("drains a pending NativeIO terminal after Run close") {
        unsigned char received = 0u;
        native_adapter_test_socket sockets[2];
        native_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_publisher_owner owner = {0};
        cflow_publisher source = {0};
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_scheduler scheduler = {0};
        cflow_subscription run = {0};
        native_adapter_test_source_fixture fixture = {0};
        native_adapter_test_sink_probe sink_probe = {0};
        cflow_subscriber_callbacks sink_callbacks = {
            native_adapter_test_sink_value,
            native_adapter_test_sink_error,
            native_adapter_test_sink_done,
            &sink_probe};
        cflow_subscriber sink = cflow_subscriber_from_callbacks(&sink_callbacks);
        cflow_io_native_adapter_stats adapter_stats = {0};
        size_t progressed = 0u;
        size_t observed = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};
        cflow_io_publisher_config source_config = {0};

        atomic_init(&fixture.prepared, 0u);
        atomic_init(&fixture.encoded, 0u);
        atomic_init(&fixture.release_count, 0u);
        atomic_init(&fixture.drive_count, 0u);
        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config),
                    TURBO_OK);
        check_equal(native_adapter_test_make_tcp_pair(sockets), TURBO_OK);
        check_equal(cflow_io_native_adapter_attach_socket(
                        &adapter, (uintptr_t)sockets[1], &endpoint),
                    TURBO_OK);
        fixture.operation_count = 1u;
        fixture.operations[0].native = (native_io_operation){
            NATIVE_IO_OPERATION_TCP_RECV, endpoint, &received, 1u, 0u,
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
        check_equal(cflow_publisher_from_io_actor_windowed(
                        &source, &owner, &source_config, 1u),
                    TURBO_OK);
        check_true(cflow_subscribe(
            &run, &normalized, &source, &scheduler, &sink));
        check_true(cflow_subscription_request(&run, 1u));
        (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
        check_equal(cflow_io_publisher_owner_run_ready(
                        &owner, 32u, &progressed), TURBO_OK);
        check_true(cflow_io_native_adapter_get_stats(&adapter,
                                                      &adapter_stats));
        check_equal(adapter_stats.active_bridges, 1u);

        cflow_subscription_close(&run);
        progressed = 0u;
        check_equal(cflow_io_publisher_owner_run_ready(
                        &owner, 32u, &progressed), TURBO_OK);
        check_true(progressed > 0u);
        check_equal(cflow_io_native_adapter_observe(
                        &adapter, NATIVE_ADAPTER_TEST_TIMEOUT_MS, &observed),
                    TURBO_OK);
        check_equal(observed, 1u);
        while (!cflow_io_publisher_owner_is_quiescent(&owner)) {
            progressed = 0u;
            check_equal(cflow_io_publisher_owner_run_ready(
                            &owner, 64u, &progressed), TURBO_OK);
            check_true(progressed > 0u);
        }
        check_equal(atomic_load(&fixture.encoded), 0u);
        check_equal(atomic_load(&fixture.release_count), 1u);
        check_equal(sink_probe.value_count, 0u);
        check_equal(sink_probe.error_count, 0u);
        check_equal(cflow_io_publisher_owner_close(&owner), TURBO_OK);
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
