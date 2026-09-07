#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <cflow/cflow.h>

#include <salts/clock.h>
#include <salts/error_codes.h>

#include "tinytest.h"

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#if defined(interface)
#undef interface
#endif
#include <windows.h>
typedef HANDLE native_adapter_test_pipe;
#define NATIVE_ADAPTER_TEST_INVALID_PIPE INVALID_HANDLE_VALUE
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
typedef int native_adapter_test_pipe;
#define NATIVE_ADAPTER_TEST_INVALID_PIPE (-1)
#endif

enum {
    NATIVE_ADAPTER_TEST_CAPACITY = 2,
    NATIVE_ADAPTER_TEST_MAX_STEPS = 16,
    NATIVE_ADAPTER_TEST_POLL_TIMEOUT_MS = 1,
    NATIVE_ADAPTER_TEST_PIPE_BUFFER_CAPACITY = 4096
};

static const uint64_t NATIVE_ADAPTER_TEST_TIMEOUT_NS = UINT64_C(5000000000);

typedef struct native_adapter_test_operation {
    native_io_operation native;
    size_t *release_count;
} native_adapter_test_operation;

typedef struct native_adapter_test_completions {
    cflow_io_request_id ids[NATIVE_ADAPTER_TEST_CAPACITY];
    cflow_io_completion values[NATIVE_ADAPTER_TEST_CAPACITY];
    size_t count;
} native_adapter_test_completions;

static native_io_backend_kind native_adapter_test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static void native_adapter_test_close_pipe(native_adapter_test_pipe pipe_value) {
#if defined(_WIN32)
    if (pipe_value != NULL && pipe_value != INVALID_HANDLE_VALUE)
        (void)CloseHandle(pipe_value);
#else
    if (pipe_value >= 0)
        (void)close(pipe_value);
#endif
}

#if defined(_WIN32)
static int native_adapter_test_make_pipe_pair(native_adapter_test_pipe pipes[2]) {
    wchar_t name[128];
    OVERLAPPED connected = {0};
    HANDLE event = NULL;
    DWORD error = ERROR_SUCCESS;
    BOOL pending = FALSE;

    pipes[0] = INVALID_HANDLE_VALUE;
    pipes[1] = INVALID_HANDLE_VALUE;
    if (_snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE,
                     L"\\\\.\\pipe\\cflow-native-adapter-test-%lu-%llu",
                     GetCurrentProcessId(), (unsigned long long)salts_hrtime()) < 0)
        return SALTS_ERANGE;
    pipes[0] = CreateNamedPipeW(
        name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
        NATIVE_ADAPTER_TEST_PIPE_BUFFER_CAPACITY,
        NATIVE_ADAPTER_TEST_PIPE_BUFFER_CAPACITY, 0u, NULL);
    if (pipes[0] == INVALID_HANDLE_VALUE)
        return -(int)GetLastError();
    event = CreateEventW(NULL, TRUE, FALSE, NULL);
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
    pipes[1] = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0u, NULL,
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
    return SALTS_OK;

failed:
    native_adapter_test_close_pipe(pipes[1]);
    native_adapter_test_close_pipe(pipes[0]);
    if (event != NULL)
        (void)CloseHandle(event);
    pipes[0] = INVALID_HANDLE_VALUE;
    pipes[1] = INVALID_HANDLE_VALUE;
    return -(int)error;
}
#else
#if !defined(__linux__)
static int native_adapter_test_set_pipe_flags(int pipe_value) {
    int status_flags;
    int descriptor_flags;

    do {
        status_flags = fcntl(pipe_value, F_GETFL);
    } while (status_flags < 0 && errno == EINTR);
    if (status_flags < 0)
        return -errno;
    while (fcntl(pipe_value, F_SETFL, status_flags | O_NONBLOCK) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    do {
        descriptor_flags = fcntl(pipe_value, F_GETFD);
    } while (descriptor_flags < 0 && errno == EINTR);
    if (descriptor_flags < 0)
        return -errno;
    while (fcntl(pipe_value, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    return SALTS_OK;
}
#endif

static int native_adapter_test_make_pipe_pair(native_adapter_test_pipe pipes[2]) {
    pipes[0] = -1;
    pipes[1] = -1;
#if defined(__linux__)
    if (pipe2(pipes, O_NONBLOCK | O_CLOEXEC) == 0)
        return SALTS_OK;
    return -errno;
#else
    int status;
    if (pipe(pipes) != 0)
        return -errno;
    status = native_adapter_test_set_pipe_flags(pipes[0]);
    if (status == SALTS_OK)
        status = native_adapter_test_set_pipe_flags(pipes[1]);
    if (status != SALTS_OK) {
        native_adapter_test_close_pipe(pipes[0]);
        native_adapter_test_close_pipe(pipes[1]);
        pipes[0] = -1;
        pipes[1] = -1;
    }
    return status;
#endif
}
#endif

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
    if (probe->count >= NATIVE_ADAPTER_TEST_CAPACITY)
        return;
    probe->ids[probe->count] = request_id;
    probe->values[probe->count] = *completion;
    ++probe->count;
}

static int native_adapter_test_drive_until(
    cflow_io_native_adapter *adapter,
    cflow_io_actor *actor,
    cflow_executor *executor,
    native_adapter_test_completions *completions,
    size_t expected) {
    const uint64_t started = salts_hrtime();

    while (completions->count < expected) {
        cflow_io_run_result run_result =
            cflow_io_actor_run_ready(actor, NATIVE_ADAPTER_TEST_MAX_STEPS);
        size_t observed = 0u;
        int status;

        if (run_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT)
            return SALTS_EINVAL;
        if (run_result.status == CFLOW_IO_RUN_BUSY)
            return SALTS_EBUSY;
        status = cflow_io_native_adapter_observe(
            adapter, NATIVE_ADAPTER_TEST_POLL_TIMEOUT_MS, &observed);
        if (status != SALTS_OK && status != SALTS_ETIMEDOUT)
            return status;
        run_result = cflow_io_actor_run_ready(actor, NATIVE_ADAPTER_TEST_MAX_STEPS);
        if (run_result.status == CFLOW_IO_RUN_INVALID_ARGUMENT)
            return SALTS_EINVAL;
        if (run_result.status == CFLOW_IO_RUN_BUSY)
            return SALTS_EBUSY;
        (void)cflow_executor_run_ready(executor);
        if (salts_hrtime() - started >= NATIVE_ADAPTER_TEST_TIMEOUT_NS)
            return SALTS_ETIMEDOUT;
    }
    return SALTS_OK;
}

spec("CFlow NativeIO adapter pipe lifecycle") {
    it("rejects malformed capacity without publishing adapter state") {
        cflow_io_native_adapter adapter = {0};
        const cflow_io_native_adapter_config config = {
            {native_adapter_test_backend(), 1u, 1u, 0u}};

        check_equal(cflow_io_native_adapter_init(&adapter, &config), SALTS_EINVAL);
        check_null(adapter.impl);
    }

    it("round trips a pipe payload and releases operations after acknowledgement") {
        static const unsigned char payload = 0x41u;
        unsigned char received = 0u;
        native_adapter_test_pipe pipes[2];
        native_io_endpoint endpoints[2] = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        size_t release_count = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 2u, 2u, 2u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operations[2] = {
            {{.kind = NATIVE_IO_OPERATION_PIPE_READ,
              .buffer = &received,
              .length = sizeof(received)},
             &release_count},
            {{.kind = NATIVE_IO_OPERATION_PIPE_WRITE,
              .buffer = (void *)&payload,
              .length = sizeof(payload)},
             &release_count}};
        cflow_io_operation actor_operations[2] = {
            {&operations[0], native_adapter_test_release},
            {&operations[1], native_adapter_test_release}};
        cflow_io_submit_result submitted[2];

        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config), SALTS_OK);
        check_equal(native_adapter_test_make_pipe_pair(pipes), SALTS_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[0],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[0]),
                    SALTS_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[1],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[1]),
                    SALTS_OK);
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
        check_equal(cflow_io_actor_init(&actor, &actor_config), SALTS_OK);

        submitted[0] = cflow_io_actor_try_submit(&actor, 101u, &actor_operations[0]);
        submitted[1] = cflow_io_actor_try_submit(&actor, 102u, &actor_operations[1]);
        check_equal(submitted[0].status, CFLOW_IO_SUBMIT_ACCEPTED);
        check_equal(submitted[1].status, CFLOW_IO_SUBMIT_ACCEPTED);
        check_equal(native_adapter_test_drive_until(
                        &adapter, &actor, &executor, &completions, 2u),
                    SALTS_OK);
        check_equal(received, payload);
        check_equal(completions.count, 2u);
        check_equal(release_count, 0u);
        for (size_t index = 0u; index < completions.count; ++index) {
            check_equal(completions.values[index].kind, CFLOW_IO_COMPLETION_OK);
            check_equal(cflow_io_actor_acknowledge(&actor, completions.ids[index]),
                        CFLOW_IO_ACK_RELEASED);
        }
        check_equal(release_count, 2u);

        check_equal(cflow_io_actor_close(&actor), SALTS_OK);
        check_true(cflow_io_actor_is_quiescent(&actor));
        check_equal(cflow_io_actor_destroy(&actor), SALTS_OK);
        check_equal(cflow_io_native_adapter_close(&adapter), SALTS_OK);
        native_adapter_test_close_pipe(pipes[0]);
        native_adapter_test_close_pipe(pipes[1]);
        check_equal(cflow_io_native_adapter_release_pipe(&adapter, endpoints[0]), SALTS_OK);
        check_equal(cflow_io_native_adapter_release_pipe(&adapter, endpoints[1]), SALTS_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), SALTS_OK);
        check_true(cflow_executor_shutdown(&executor));
        cflow_executor_destroy(&executor);
    }

    it("keeps a cancelled pipe identity until close drain and acknowledgement") {
        unsigned char received = 0u;
        native_adapter_test_pipe pipes[2];
        native_io_endpoint endpoint = {0};
        cflow_io_native_adapter adapter = {0};
        cflow_io_native_adapter_stats stats = {0};
        cflow_executor executor = {0};
        cflow_io_actor actor = {0};
        native_adapter_test_completions completions = {0};
        size_t release_count = 0u;
        const cflow_io_native_adapter_config adapter_config = {
            {native_adapter_test_backend(), 1u, 1u, 1u}};
        cflow_io_actor_config actor_config = {0};
        native_adapter_test_operation operation = {
            {.kind = NATIVE_IO_OPERATION_PIPE_READ,
             .buffer = &received,
             .length = sizeof(received)},
            &release_count};
        cflow_io_operation actor_operation = {&operation, native_adapter_test_release};
        cflow_io_submit_result submitted;

        check_equal(cflow_io_native_adapter_init(&adapter, &adapter_config), SALTS_OK);
        check_equal(native_adapter_test_make_pipe_pair(pipes), SALTS_OK);
        check_equal(cflow_io_native_adapter_attach_pipe(
                        &adapter, (uintptr_t)pipes[0],
                        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                    SALTS_OK);
        operation.native.endpoint = endpoint;
        check_true(cflow_executor_manual_init_with_capacity(&executor, 1u));
        actor_config.request_capacity = 1u;
        actor_config.command_capacity = 2u;
        actor_config.executor = &executor;
        actor_config.backend = cflow_io_native_adapter_actor_ops();
        actor_config.backend_user = &adapter;
        actor_config.completion = native_adapter_test_complete;
        actor_config.completion_user = &completions;
        check_equal(cflow_io_actor_init(&actor, &actor_config), SALTS_OK);

        submitted = cflow_io_actor_try_submit(&actor, 201u, &actor_operation);
        check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
        (void)cflow_io_actor_run_ready(&actor, NATIVE_ADAPTER_TEST_MAX_STEPS);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.active_bridges, 1u);
        check_equal(stats.native.active_requests, 1u);
        check_equal(cflow_io_native_adapter_close(&adapter), SALTS_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), SALTS_EBUSY);

        check_equal(cflow_io_actor_try_cancel(&actor, submitted.request_id),
                    CFLOW_IO_CANCEL_ACCEPTED);
        check_equal(native_adapter_test_drive_until(
                        &adapter, &actor, &executor, &completions, 1u),
                    SALTS_OK);
        check_equal(completions.count, 1u);
        check_equal(completions.values[0].kind, CFLOW_IO_COMPLETION_CANCELLED);
        check_equal(release_count, 0u);
        check_equal(cflow_io_actor_acknowledge(&actor, completions.ids[0]),
                    CFLOW_IO_ACK_RELEASED);
        check_equal(release_count, 1u);
        check_true(cflow_io_native_adapter_get_stats(&adapter, &stats));
        check_equal(stats.active_bridges, 0u);
        check_equal(stats.native.active_requests, 0u);

        check_equal(cflow_io_actor_close(&actor), SALTS_OK);
        check_true(cflow_io_actor_is_quiescent(&actor));
        check_equal(cflow_io_actor_destroy(&actor), SALTS_OK);
        native_adapter_test_close_pipe(pipes[0]);
        native_adapter_test_close_pipe(pipes[1]);
        check_equal(cflow_io_native_adapter_release_pipe(&adapter, endpoint), SALTS_OK);
        check_equal(cflow_io_native_adapter_destroy(&adapter), SALTS_OK);
        check_true(cflow_executor_shutdown(&executor));
        cflow_executor_destroy(&executor);
    }
}
