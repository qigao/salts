#include <cflow/cflow.h>

#include "tinytest.h"

#include <turbo/clock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
typedef SOCKET adapter_bench_socket;
typedef int adapter_bench_socklen;
  #define ADAPTER_BENCH_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <netinet/in.h>
  #include <sys/resource.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int adapter_bench_socket;
typedef socklen_t adapter_bench_socklen;
  #define ADAPTER_BENCH_INVALID_SOCKET (-1)
#endif

enum {
    ADAPTER_BENCH_SAMPLES = 20,
    ADAPTER_BENCH_TRANSFERS_PER_SAMPLE = 256,
    ADAPTER_BENCH_WARMUP_TRANSFERS = 8,
    ADAPTER_BENCH_STAGE_TRANSFERS = 256,
    ADAPTER_BENCH_TIMEOUT_MS = 5000,
    ADAPTER_BENCH_MAX_COMPLETIONS = 2,
    ADAPTER_BENCH_TOTAL_TRANSFERS =
        ADAPTER_BENCH_SAMPLES * ADAPTER_BENCH_TRANSFERS_PER_SAMPLE
};

static const size_t ADAPTER_BENCH_PAYLOADS[] = {
    1024u, 4u * 1024u, 8u * 1024u, 16u * 1024u,
    32u * 1024u, 64u * 1024u};

typedef enum adapter_bench_mode {
    ADAPTER_BENCH_DIRECT = 0,
    ADAPTER_BENCH_ACTOR,
    ADAPTER_BENCH_SOURCE,
    ADAPTER_BENCH_MODE_COUNT
} adapter_bench_mode;

typedef enum adapter_bench_role {
    ADAPTER_BENCH_SEND = 0,
    ADAPTER_BENCH_RECV
} adapter_bench_role;

typedef struct adapter_bench_stages {
    uint64_t admission_ns;
    uint64_t native_submit_ns;
    uint64_t observe_ns;
    uint64_t actor_transition_ns;
    uint64_t executor_delivery_ns;
    uint64_t acknowledge_ns;
    uint64_t source_runtime_ns;
    uint64_t source_owner_ns;
    uint64_t native_submit_calls;
    uint64_t observe_calls;
    uint64_t actor_operations;
} adapter_bench_stages;

typedef struct adapter_bench_result {
    size_t payload_size;
    uint64_t wall_ns;
    uint64_t cpu_ns;
    uint64_t latencies[ADAPTER_BENCH_TOTAL_TRANSFERS];
    size_t latency_count;
    uint64_t p50_ns;
    uint64_t p95_ns;
    uint64_t p99_ns;
    adapter_bench_stages stages;
} adapter_bench_result;

typedef struct adapter_bench_actor_operation {
    turbo_io_operation native;
    adapter_bench_role role;
    size_t *release_count;
} adapter_bench_actor_operation;

typedef struct adapter_bench_delivery {
    cflow_io_request_id request_id;
    adapter_bench_actor_operation *operation;
    cflow_io_completion completion;
} adapter_bench_delivery;

typedef struct adapter_bench_fixture {
    adapter_bench_mode mode;
    adapter_bench_socket sockets[2];
    turbo_io_endpoint endpoints[2];
    turbo_io_backend direct;
    cflow_io_native_adapter adapter;
    cflow_executor executor;
    cflow_io_actor actor;
    cflow_graph surface;
    cflow_graph normalized;
    cflow_scheduler scheduler;
    cflow_source source;
    cflow_io_source_owner source_owner;
    cflow_run run;
    cflow_sink_callbacks sink_callbacks;
    cflow_sink sink;
    cflow_io_backend_ops adapter_ops;
    adapter_bench_stages *stages;
    adapter_bench_delivery deliveries[ADAPTER_BENCH_MAX_COMPLETIONS];
    size_t delivery_count;
    size_t release_count;
    cflow_io_lease_id next_lease;
    adapter_bench_actor_operation source_operations[2];
    size_t source_offsets[2];
    bool source_pending[2];
    size_t source_sink_values;
    uint64_t operation_count;
    const char *source_error;
    unsigned char *sent;
    unsigned char *received;
    size_t payload_size;
    bool backend_initialized;
    bool sockets_created;
    bool actor_initialized;
    bool executor_initialized;
    bool surface_initialized;
    bool normalized_initialized;
    bool scheduler_initialized;
    bool source_owner_initialized;
    bool run_initialized;
} adapter_bench_fixture;

static const char *adapter_bench_mode_name(adapter_bench_mode mode) {
    if (mode == ADAPTER_BENCH_DIRECT)
        return "NativeIO direct";
    return mode == ADAPTER_BENCH_ACTOR
        ? "Actor/NativeIO" : "Source(window=2)/NativeIO";
}

static turbo_io_backend_kind adapter_bench_backend(void) {
#if defined(_WIN32)
    return TURBO_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return TURBO_IO_BACKEND_EPOLL;
#else
    return TURBO_IO_BACKEND_KQUEUE;
#endif
}

static int adapter_bench_last_socket_error(void) {
#if defined(_WIN32)
    return -(int)WSAGetLastError();
#else
    return -errno;
#endif
}

static uint64_t adapter_bench_process_cpu_ns(void) {
#if defined(_WIN32)
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    ULARGE_INTEGER kernel_ticks;
    ULARGE_INTEGER user_ticks;

    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited,
                         &kernel, &user))
        return 0u;
    kernel_ticks.LowPart = kernel.dwLowDateTime;
    kernel_ticks.HighPart = kernel.dwHighDateTime;
    user_ticks.LowPart = user.dwLowDateTime;
    user_ticks.HighPart = user.dwHighDateTime;
    return (kernel_ticks.QuadPart + user_ticks.QuadPart) * UINT64_C(100);
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0u;
    return ((uint64_t)usage.ru_utime.tv_sec +
            (uint64_t)usage.ru_stime.tv_sec) * UINT64_C(1000000000) +
        ((uint64_t)usage.ru_utime.tv_usec +
         (uint64_t)usage.ru_stime.tv_usec) * UINT64_C(1000);
#endif
}

static void adapter_bench_close_socket(adapter_bench_socket socket_value) {
    if (socket_value == ADAPTER_BENCH_INVALID_SOCKET)
        return;
#if defined(_WIN32)
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
}

static int adapter_bench_make_tcp_pair(adapter_bench_socket sockets[2]) {
    adapter_bench_socket listener = ADAPTER_BENCH_INVALID_SOCKET;
    struct sockaddr_in address;
    adapter_bench_socklen address_length =
        (adapter_bench_socklen)sizeof(address);
    int status = TURBO_OK;

    sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
    sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == ADAPTER_BENCH_INVALID_SOCKET)
        return adapter_bench_last_socket_error();
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (const struct sockaddr *)&address,
             (adapter_bench_socklen)sizeof(address)) != 0 ||
        getsockname(listener, (struct sockaddr *)&address,
                    &address_length) != 0 ||
        listen(listener, 1) != 0)
        status = adapter_bench_last_socket_error();
    if (status == TURBO_OK) {
        sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockets[0] == ADAPTER_BENCH_INVALID_SOCKET)
            status = adapter_bench_last_socket_error();
    }
    if (status == TURBO_OK &&
        connect(sockets[0], (const struct sockaddr *)&address,
                (adapter_bench_socklen)sizeof(address)) != 0)
        status = adapter_bench_last_socket_error();
    if (status == TURBO_OK) {
        sockets[1] = accept(listener, NULL, NULL);
        if (sockets[1] == ADAPTER_BENCH_INVALID_SOCKET)
            status = adapter_bench_last_socket_error();
    }
    adapter_bench_close_socket(listener);
    if (status != TURBO_OK) {
        adapter_bench_close_socket(sockets[0]);
        adapter_bench_close_socket(sockets[1]);
        sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
        sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
    }
    return status;
}

static void adapter_bench_counter_add(uint64_t *counter, uint64_t value) {
    *counter = UINT64_MAX - *counter < value ? UINT64_MAX : *counter + value;
}

static int adapter_bench_compare_u64(const void *left, const void *right) {
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static void adapter_bench_release(void *operation_user) {
    adapter_bench_actor_operation *operation =
        (adapter_bench_actor_operation *)operation_user;
    ++*operation->release_count;
}

static void adapter_bench_complete(
    void *completion_user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion) {
    adapter_bench_fixture *fixture =
        (adapter_bench_fixture *)completion_user;
    adapter_bench_delivery *delivery;

    (void)lease_id;
    if (fixture->delivery_count >= ADAPTER_BENCH_MAX_COMPLETIONS)
        return;
    delivery = &fixture->deliveries[fixture->delivery_count++];
    delivery->request_id = request_id;
    delivery->operation = (adapter_bench_actor_operation *)operation_user;
    delivery->completion = *completion;
}

static cflow_io_source_prepare_status adapter_bench_source_prepare(
    void *user, cflow_io_operation *operation, const char **error) {
    adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;

    (void)error;
    for (size_t role = 0u; role < 2u; ++role) {
        adapter_bench_actor_operation *prepared;
        if (fixture->source_pending[role] ||
            fixture->source_offsets[role] >= fixture->payload_size)
            continue;
        prepared = &fixture->source_operations[role];
        prepared->native = (turbo_io_operation){
            role == ADAPTER_BENCH_SEND
                ? TURBO_IO_TCP_SEND : TURBO_IO_TCP_RECV,
            role == ADAPTER_BENCH_SEND
                ? fixture->endpoints[0] : fixture->endpoints[1],
            (role == ADAPTER_BENCH_SEND
                 ? fixture->sent : fixture->received) +
                fixture->source_offsets[role],
            fixture->payload_size - fixture->source_offsets[role],
            (uintptr_t)(role + 1u), NULL, 0u, 0u};
        prepared->role = (adapter_bench_role)role;
        prepared->release_count = &fixture->release_count;
        fixture->source_pending[role] = true;
        operation->user = prepared;
        operation->release = adapter_bench_release;
        ++fixture->operation_count;
        if (fixture->stages != NULL)
            ++fixture->stages->actor_operations;
        return CFLOW_IO_SOURCE_PREPARE_OPERATION;
    }
    return CFLOW_IO_SOURCE_PREPARE_DONE;
}

static cflow_read_status adapter_bench_source_encode(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion,
    void *out_value,
    const char **error) {
    static const char completion_error[] =
        "Source benchmark received a non-success completion";
    adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
    adapter_bench_actor_operation *operation =
        (adapter_bench_actor_operation *)operation_user;
    const size_t role = (size_t)operation->role;

    (void)request_id;
    (void)lease_id;
    if (role >= 2u || !fixture->source_pending[role] ||
        completion->kind != CFLOW_IO_COMPLETION_OK ||
        completion->bytes == 0u) {
        *error = completion_error;
        return CFLOW_READ_ERROR;
    }
    fixture->source_offsets[role] += completion->bytes;
    fixture->source_pending[role] = false;
    *(int *)out_value = (int)completion->bytes;
    return CFLOW_READ_VALUE;
}

static void adapter_bench_source_drive(void *user) {
    (void)user;
}

static bool adapter_bench_source_sink_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
    if (!cmeta_type_equal(type, &cmeta_type_int) ||
        value == NULL || *(const int *)value <= 0)
        return false;
    ++fixture->source_sink_values;
    return true;
}

static void adapter_bench_source_sink_error(
    void *user, const char *message) {
    adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
    fixture->source_error = message;
}

static void adapter_bench_source_sink_done(void *user) {
    adapter_bench_fixture *fixture = (adapter_bench_fixture *)user;
    if (fixture->source_error == NULL)
        fixture->source_error = "Source benchmark terminated unexpectedly";
}

static int adapter_bench_timed_submit(
    void *backend_user,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user) {
    adapter_bench_fixture *fixture =
        (adapter_bench_fixture *)backend_user;
    uint64_t started = 0u;
    int status;

    if (fixture->stages == NULL)
        return fixture->adapter_ops.submit(
            &fixture->adapter, actor, request_id, lease_id,
            operation_user);
    started = turbo_hrtime();
    status = fixture->adapter_ops.submit(
        &fixture->adapter, actor, request_id, lease_id, operation_user);
    adapter_bench_counter_add(&fixture->stages->native_submit_ns,
                              turbo_hrtime() - started);
    ++fixture->stages->native_submit_calls;
    return status;
}

static int adapter_bench_timed_cancel(
    void *backend_user, cflow_io_request_id request_id) {
    adapter_bench_fixture *fixture =
        (adapter_bench_fixture *)backend_user;
    return fixture->adapter_ops.cancel(&fixture->adapter, request_id);
}

static int adapter_bench_fixture_init(
    adapter_bench_fixture *fixture,
    adapter_bench_mode mode,
    size_t payload_size,
    adapter_bench_stages *stages) {
    const turbo_io_backend_config backend_config = {
        adapter_bench_backend(), 2u, 2u, 2u};
    int status;

    if (fixture == NULL || stages == NULL || payload_size == 0u)
        return TURBO_EINVAL;
    memset(fixture, 0, sizeof(*fixture));
    fixture->mode = mode;
    fixture->sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
    fixture->sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
    fixture->payload_size = payload_size;
    fixture->stages = stages;
    fixture->next_lease = 1u;
    fixture->sent = (unsigned char *)malloc(payload_size);
    fixture->received = (unsigned char *)malloc(payload_size);
    if (fixture->sent == NULL || fixture->received == NULL)
        return TURBO_ENOMEM;
    memset(fixture->sent, 0x5au, payload_size);

    if (mode == ADAPTER_BENCH_DIRECT) {
        status = turbo_io_backend_init(&fixture->direct, &backend_config);
    } else {
        const cflow_io_native_adapter_config adapter_config = {backend_config};
        status = cflow_io_native_adapter_init(&fixture->adapter,
                                              &adapter_config);
    }
    if (status != TURBO_OK)
        return status;
    fixture->backend_initialized = true;
    status = adapter_bench_make_tcp_pair(fixture->sockets);
    if (status != TURBO_OK)
        return status;
    fixture->sockets_created = true;
    for (size_t index = 0u; index < 2u; ++index) {
        status = mode == ADAPTER_BENCH_DIRECT
            ? turbo_io_backend_attach_socket(
                  &fixture->direct, (uintptr_t)fixture->sockets[index],
                  &fixture->endpoints[index])
            : cflow_io_native_adapter_attach_socket(
                  &fixture->adapter, (uintptr_t)fixture->sockets[index],
                  &fixture->endpoints[index]);
        if (status != TURBO_OK)
            return status;
    }

    if (mode == ADAPTER_BENCH_ACTOR) {
        cflow_io_actor_config actor_config = {0};
        fixture->adapter_ops = cflow_io_native_adapter_actor_ops();
        if (!cflow_executor_manual_init_with_capacity(&fixture->executor, 2u))
            return TURBO_ENOMEM;
        fixture->executor_initialized = true;
        actor_config.request_capacity = 2u;
        actor_config.command_capacity = 2u;
        actor_config.executor = &fixture->executor;
        actor_config.backend.submit = adapter_bench_timed_submit;
        actor_config.backend.cancel = adapter_bench_timed_cancel;
        actor_config.backend_user = fixture;
        actor_config.completion = adapter_bench_complete;
        actor_config.completion_user = fixture;
        status = cflow_io_actor_init(&fixture->actor, &actor_config);
        if (status != TURBO_OK)
            return status;
        fixture->actor_initialized = true;
    } else if (mode == ADAPTER_BENCH_SOURCE) {
        cflow_io_source_config source_config = {0};
        fixture->adapter_ops = cflow_io_native_adapter_actor_ops();
        fixture->normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&fixture->surface, &cmeta_type_int);
        fixture->surface_initialized = true;
        if (!cflow_graph_normalize(&fixture->normalized, &fixture->surface))
            return TURBO_ENOMEM;
        fixture->normalized_initialized = true;
        if (!cflow_scheduler_test_init(&fixture->scheduler))
            return TURBO_ENOMEM;
        fixture->scheduler_initialized = true;
        source_config.name = "native-io-adapter-benchmark-source";
        source_config.type = &cmeta_type_int;
        source_config.backend.submit = adapter_bench_timed_submit;
        source_config.backend.cancel = adapter_bench_timed_cancel;
        source_config.backend_user = fixture;
        source_config.prepare = adapter_bench_source_prepare;
        source_config.encode = adapter_bench_source_encode;
        source_config.user = fixture;
        source_config.drive = adapter_bench_source_drive;
        source_config.drive_user = fixture;
        status = cflow_source_from_io_actor_windowed(
            &fixture->source, &fixture->source_owner,
            &source_config, 2u);
        if (status != TURBO_OK)
            return status;
        fixture->source_owner_initialized = true;
        fixture->sink_callbacks = (cflow_sink_callbacks){
            adapter_bench_source_sink_value,
            adapter_bench_source_sink_error,
            adapter_bench_source_sink_done,
            fixture};
        fixture->sink = cflow_sink_from_callbacks(&fixture->sink_callbacks);
        if (!cflow_run_open(
                &fixture->run, &fixture->normalized, &fixture->source,
                &fixture->scheduler, &fixture->sink))
            return TURBO_EIO;
        fixture->run_initialized = true;
    }
    return TURBO_OK;
}

static int adapter_bench_fixture_destroy(adapter_bench_fixture *fixture) {
    int status = TURBO_OK;

    if (fixture == NULL)
        return TURBO_EINVAL;
    if (fixture->run_initialized) {
        cflow_run_close(&fixture->run);
        fixture->run_initialized = false;
    } else if (cflow_source_valid(&fixture->source)) {
        cflow_source_destroy(&fixture->source);
    }
    if (fixture->source_owner_initialized) {
        int current = cflow_io_source_owner_close(&fixture->source_owner);
        if (current != TURBO_OK && status == TURBO_OK)
            status = current;
        if (current == TURBO_OK)
            fixture->source_owner_initialized = false;
    }
    if (fixture->scheduler_initialized) {
        cflow_scheduler_destroy(&fixture->scheduler);
        fixture->scheduler_initialized = false;
    }
    if (fixture->normalized_initialized) {
        cflow_graph_destroy(&fixture->normalized);
        fixture->normalized_initialized = false;
    }
    if (fixture->surface_initialized) {
        cflow_graph_destroy(&fixture->surface);
        fixture->surface_initialized = false;
    }
    if (fixture->actor_initialized) {
        int current = cflow_io_actor_close(&fixture->actor);
        if (current != TURBO_OK && status == TURBO_OK)
            status = current;
        current = cflow_io_actor_destroy(&fixture->actor);
        if (current != TURBO_OK && status == TURBO_OK)
            status = current;
        if (current == TURBO_OK)
            fixture->actor_initialized = false;
    }
    if (fixture->backend_initialized) {
        int current = fixture->mode == ADAPTER_BENCH_DIRECT
            ? turbo_io_backend_close(&fixture->direct)
            : cflow_io_native_adapter_close(&fixture->adapter);
        if (current != TURBO_OK && status == TURBO_OK)
            status = current;
    }
    if (fixture->sockets_created) {
        adapter_bench_close_socket(fixture->sockets[0]);
        adapter_bench_close_socket(fixture->sockets[1]);
        fixture->sockets[0] = ADAPTER_BENCH_INVALID_SOCKET;
        fixture->sockets[1] = ADAPTER_BENCH_INVALID_SOCKET;
        fixture->sockets_created = false;
    }
    if (fixture->backend_initialized) {
        for (size_t index = 0u; index < 2u; ++index) {
            int current = fixture->mode == ADAPTER_BENCH_DIRECT
                ? turbo_io_backend_release_socket(
                      &fixture->direct, fixture->endpoints[index])
                : cflow_io_native_adapter_release_socket(
                      &fixture->adapter, fixture->endpoints[index]);
            if (current != TURBO_OK && status == TURBO_OK)
                status = current;
        }
        {
            int current = fixture->mode == ADAPTER_BENCH_DIRECT
                ? turbo_io_backend_destroy(&fixture->direct)
                : cflow_io_native_adapter_destroy(&fixture->adapter);
            if (current != TURBO_OK && status == TURBO_OK)
                status = current;
            if (current == TURBO_OK)
                fixture->backend_initialized = false;
        }
    }
    if (fixture->executor_initialized) {
        if (!cflow_executor_shutdown(&fixture->executor) && status == TURBO_OK)
            status = TURBO_EBUSY;
        cflow_executor_destroy(&fixture->executor);
        fixture->executor_initialized = false;
    }
    free(fixture->received);
    free(fixture->sent);
    fixture->received = NULL;
    fixture->sent = NULL;
    return status;
}

static int adapter_bench_direct_exchange(adapter_bench_fixture *fixture) {
    size_t sent = 0u;
    size_t received = 0u;
    bool send_pending = false;
    bool recv_pending = false;

    memset(fixture->received, 0, fixture->payload_size);
    while (sent < fixture->payload_size || received < fixture->payload_size) {
        turbo_io_request requests[2] = {{0}};
        turbo_io_completion completions[2];
        size_t completion_count = 0u;
        uint64_t started = 0u;
        int status;

        if (!recv_pending && received < fixture->payload_size) {
            turbo_io_operation operation = {
                TURBO_IO_TCP_RECV, fixture->endpoints[1],
                fixture->received + received,
                fixture->payload_size - received, 2u, NULL, 0u, 0u};
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            status = turbo_io_backend_submit(&fixture->direct, &operation,
                                             &requests[ADAPTER_BENCH_RECV]);
            if (fixture->stages != NULL) {
                adapter_bench_counter_add(
                    &fixture->stages->native_submit_ns,
                    turbo_hrtime() - started);
                ++fixture->stages->native_submit_calls;
            }
            if (status != TURBO_OK)
                return status;
            recv_pending = true;
        }
        if (!send_pending && sent < fixture->payload_size) {
            turbo_io_operation operation = {
                TURBO_IO_TCP_SEND, fixture->endpoints[0],
                fixture->sent + sent, fixture->payload_size - sent,
                1u, NULL, 0u, 0u};
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            status = turbo_io_backend_submit(&fixture->direct, &operation,
                                             &requests[ADAPTER_BENCH_SEND]);
            if (fixture->stages != NULL) {
                adapter_bench_counter_add(
                    &fixture->stages->native_submit_ns,
                    turbo_hrtime() - started);
                ++fixture->stages->native_submit_calls;
            }
            if (status != TURBO_OK)
                return status;
            send_pending = true;
        }

        if (fixture->stages != NULL)
            started = turbo_hrtime();
        status = turbo_io_backend_observe(
            &fixture->direct, completions, 2u,
            ADAPTER_BENCH_TIMEOUT_MS, &completion_count);
        if (fixture->stages != NULL) {
            adapter_bench_counter_add(&fixture->stages->observe_ns,
                                      turbo_hrtime() - started);
            ++fixture->stages->observe_calls;
        }
        if (status != TURBO_OK)
            return status;
        for (size_t index = 0u; index < completion_count; ++index) {
            const turbo_io_completion *completion = &completions[index];
            if (completion->kind != TURBO_IO_COMPLETION_OK ||
                completion->bytes == 0u)
                return completion->status != TURBO_OK
                    ? completion->status : TURBO_EPROTO;
            if (completion->user_data == 1u) {
                sent += completion->bytes;
                send_pending = false;
            } else if (completion->user_data == 2u) {
                received += completion->bytes;
                recv_pending = false;
            } else {
                return TURBO_EPROTO;
            }
        }
    }
    return memcmp(fixture->sent, fixture->received,
                  fixture->payload_size) == 0
        ? TURBO_OK : TURBO_EPROTO;
}

static int adapter_bench_actor_run_transition(
    adapter_bench_fixture *fixture, size_t max_steps) {
    cflow_io_run_result result;
    uint64_t nested_before;
    uint64_t started = 0u;
    uint64_t elapsed;
    uint64_t nested;

    if (fixture->stages == NULL) {
        result = cflow_io_actor_run_ready(&fixture->actor, max_steps);
        return result.status == CFLOW_IO_RUN_INVALID_ARGUMENT ||
                       result.status == CFLOW_IO_RUN_BUSY
            ? TURBO_EPROTO : TURBO_OK;
    }
    nested_before = fixture->stages->native_submit_ns;
    started = turbo_hrtime();
    result = cflow_io_actor_run_ready(&fixture->actor, max_steps);
    elapsed = turbo_hrtime() - started;
    nested = fixture->stages->native_submit_ns - nested_before;

    adapter_bench_counter_add(&fixture->stages->actor_transition_ns,
                              elapsed > nested ? elapsed - nested : 0u);
    return result.status == CFLOW_IO_RUN_INVALID_ARGUMENT ||
                   result.status == CFLOW_IO_RUN_BUSY
        ? TURBO_EPROTO : TURBO_OK;
}

static int adapter_bench_actor_exchange(adapter_bench_fixture *fixture) {
    adapter_bench_actor_operation operations[2] = {0};
    bool pending[2] = {false, false};
    size_t offsets[2] = {0u, 0u};
    const size_t release_before = fixture->release_count;
    const uint64_t operation_before = fixture->operation_count;

    memset(fixture->received, 0, fixture->payload_size);
    while (offsets[ADAPTER_BENCH_SEND] < fixture->payload_size ||
           offsets[ADAPTER_BENCH_RECV] < fixture->payload_size) {
        for (size_t role = 0u; role < 2u; ++role) {
            cflow_io_operation actor_operation;
            cflow_io_submit_result submitted;
            uint64_t started = 0u;

            if (pending[role] || offsets[role] >= fixture->payload_size)
                continue;
            operations[role].native = (turbo_io_operation){
                role == ADAPTER_BENCH_SEND
                    ? TURBO_IO_TCP_SEND : TURBO_IO_TCP_RECV,
                role == ADAPTER_BENCH_SEND
                    ? fixture->endpoints[0] : fixture->endpoints[1],
                (role == ADAPTER_BENCH_SEND
                     ? fixture->sent : fixture->received) + offsets[role],
                fixture->payload_size - offsets[role],
                (uintptr_t)(role + 1u), NULL, 0u, 0u};
            operations[role].role = (adapter_bench_role)role;
            operations[role].release_count = &fixture->release_count;
            actor_operation = (cflow_io_operation){
                &operations[role], adapter_bench_release};
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            submitted = cflow_io_actor_try_submit(
                &fixture->actor, fixture->next_lease++, &actor_operation);
            if (fixture->stages != NULL)
                adapter_bench_counter_add(
                    &fixture->stages->admission_ns,
                    turbo_hrtime() - started);
            if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
                cflow_io_actor_stats stats = {0};
                (void)cflow_io_actor_get_stats(&fixture->actor, &stats);
                fprintf(stderr,
                        "Actor admission failed: status=%d active=%zu "
                        "queued=%zu ready=%zu pending=%zu delivered=%zu\n",
                        (int)submitted.status, stats.active_requests,
                        stats.queued_commands, stats.ready,
                        stats.backend_pending,
                        stats.delivered_unacknowledged);
                switch (submitted.status) {
                case CFLOW_IO_SUBMIT_INVALID_ARGUMENT:
                    return TURBO_EINVAL;
                case CFLOW_IO_SUBMIT_FULL:
                    return TURBO_ENOBUFS;
                case CFLOW_IO_SUBMIT_CLOSED:
                    return TURBO_ESHUTDOWN;
                case CFLOW_IO_SUBMIT_LEASE_IN_USE:
                    return TURBO_EALREADY;
                case CFLOW_IO_SUBMIT_ID_EXHAUSTED:
                    return TURBO_ERANGE;
                case CFLOW_IO_SUBMIT_ACCEPTED:
                    break;
                }
                return TURBO_EPROTO;
            }
            pending[role] = true;
            ++fixture->operation_count;
            if (fixture->stages != NULL)
                ++fixture->stages->actor_operations;
        }
        {
            int status = adapter_bench_actor_run_transition(fixture, 8u);
            if (status != TURBO_OK)
                return status;
        }
        while (fixture->delivery_count == 0u) {
            size_t observed = 0u;
            uint64_t started = 0u;
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            int status = cflow_io_native_adapter_observe(
                &fixture->adapter, ADAPTER_BENCH_TIMEOUT_MS, &observed);
            if (fixture->stages != NULL) {
                adapter_bench_counter_add(
                    &fixture->stages->observe_ns,
                    turbo_hrtime() - started);
                ++fixture->stages->observe_calls;
            }
            if (status != TURBO_OK)
                return status;
            status = adapter_bench_actor_run_transition(fixture, 8u);
            if (status != TURBO_OK)
                return status;
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            (void)cflow_executor_run_ready(&fixture->executor);
            if (fixture->stages != NULL)
                adapter_bench_counter_add(
                    &fixture->stages->executor_delivery_ns,
                    turbo_hrtime() - started);
        }
        for (size_t index = 0u; index < fixture->delivery_count; ++index) {
            adapter_bench_delivery *delivery = &fixture->deliveries[index];
            const size_t role = (size_t)delivery->operation->role;
            uint64_t started = 0u;

            if (role >= 2u || !pending[role] ||
                delivery->completion.kind != CFLOW_IO_COMPLETION_OK ||
                delivery->completion.bytes == 0u)
                return delivery->completion.error != TURBO_OK
                    ? delivery->completion.error : TURBO_EPROTO;
            offsets[role] += delivery->completion.bytes;
            pending[role] = false;
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            if (cflow_io_actor_acknowledge(
                    &fixture->actor, delivery->request_id) !=
                CFLOW_IO_ACK_RELEASED)
                return TURBO_EPROTO;
            if (fixture->stages != NULL)
                adapter_bench_counter_add(
                    &fixture->stages->acknowledge_ns,
                    turbo_hrtime() - started);
        }
        fixture->delivery_count = 0u;
    }
    if (fixture->release_count - release_before !=
        fixture->operation_count - operation_before) {
        return TURBO_EPROTO;
    }
    return memcmp(fixture->sent, fixture->received,
                  fixture->payload_size) == 0
        ? TURBO_OK : TURBO_EPROTO;
}

static int adapter_bench_source_owner_run(
    adapter_bench_fixture *fixture, size_t max_steps) {
    size_t progressed = 0u;
    uint64_t nested_before;
    uint64_t started = 0u;
    uint64_t elapsed;
    uint64_t nested;
    int status;

    if (fixture->stages == NULL)
        return cflow_io_source_owner_run_ready(
            &fixture->source_owner, max_steps, &progressed);
    nested_before = fixture->stages->native_submit_ns;
    started = turbo_hrtime();
    status = cflow_io_source_owner_run_ready(
        &fixture->source_owner, max_steps, &progressed);
    elapsed = turbo_hrtime() - started;
    nested = fixture->stages->native_submit_ns - nested_before;
    adapter_bench_counter_add(&fixture->stages->source_owner_ns,
                              elapsed > nested ? elapsed - nested : 0u);
    return status;
}

static int adapter_bench_source_exchange(adapter_bench_fixture *fixture) {
    const size_t release_before = fixture->release_count;
    const size_t sink_before = fixture->source_sink_values;
    const uint64_t operation_before = fixture->operation_count;

    memset(fixture->received, 0, fixture->payload_size);
    memset(fixture->source_offsets, 0, sizeof(fixture->source_offsets));
    memset(fixture->source_pending, 0, sizeof(fixture->source_pending));
    while (fixture->source_offsets[ADAPTER_BENCH_SEND] <
               fixture->payload_size ||
           fixture->source_offsets[ADAPTER_BENCH_RECV] <
               fixture->payload_size) {
        size_t requested = 0u;
        size_t observed = 0u;
        uint64_t started = 0u;
        int status;

        for (size_t role = 0u; role < 2u; ++role) {
            if (fixture->source_offsets[role] < fixture->payload_size)
                ++requested;
        }
        if (fixture->stages != NULL)
            started = turbo_hrtime();
        if (!cflow_run_request(&fixture->run, requested))
            return TURBO_EPROTO;
        (void)cflow_scheduler_run_until_idle(&fixture->scheduler, 0u);
        if (fixture->stages != NULL)
            adapter_bench_counter_add(
                &fixture->stages->source_runtime_ns,
                turbo_hrtime() - started);
        status = adapter_bench_source_owner_run(fixture, 64u);
        if (status != TURBO_OK)
            return status;
        while (observed < requested) {
            size_t batch = 0u;
            if (fixture->stages != NULL)
                started = turbo_hrtime();
            status = cflow_io_native_adapter_observe(
                &fixture->adapter, ADAPTER_BENCH_TIMEOUT_MS, &batch);
            if (fixture->stages != NULL) {
                adapter_bench_counter_add(
                    &fixture->stages->observe_ns,
                    turbo_hrtime() - started);
                ++fixture->stages->observe_calls;
            }
            if (status != TURBO_OK)
                return status;
            observed += batch;
        }
        status = adapter_bench_source_owner_run(fixture, 64u);
        if (status != TURBO_OK)
            return status;
        if (fixture->stages != NULL)
            started = turbo_hrtime();
        (void)cflow_scheduler_run_until_idle(&fixture->scheduler, 0u);
        if (fixture->stages != NULL)
            adapter_bench_counter_add(
                &fixture->stages->source_runtime_ns,
                turbo_hrtime() - started);
        if (fixture->source_error != NULL)
            return TURBO_EIO;
    }
    if (fixture->release_count - release_before !=
            fixture->operation_count - operation_before ||
        fixture->source_sink_values - sink_before !=
            fixture->operation_count - operation_before)
        return TURBO_EPROTO;
    return memcmp(fixture->sent, fixture->received,
                  fixture->payload_size) == 0
        ? TURBO_OK : TURBO_EPROTO;
}

static int adapter_bench_exchange(adapter_bench_fixture *fixture) {
    if (fixture->mode == ADAPTER_BENCH_DIRECT)
        return adapter_bench_direct_exchange(fixture);
    return fixture->mode == ADAPTER_BENCH_ACTOR
        ? adapter_bench_actor_exchange(fixture)
        : adapter_bench_source_exchange(fixture);
}

static bool adapter_bench_validate(
    adapter_bench_fixture *fixture,
    const adapter_bench_result *result) {
    turbo_io_backend_stats direct_stats = {0};
    cflow_io_native_adapter_stats adapter_stats = {0};
    cflow_io_actor_stats actor_stats = {0};
    cflow_io_source_stats source_stats = {0};
    cflow_io_source_window_stats window_stats = {0};

    if (result->latency_count != ADAPTER_BENCH_TOTAL_TRANSFERS ||
        memcmp(fixture->sent, fixture->received,
               fixture->payload_size) != 0)
        return false;
    if (fixture->mode == ADAPTER_BENCH_DIRECT) {
        return turbo_io_backend_get_stats(&fixture->direct, &direct_stats) &&
            direct_stats.active_requests == 0u &&
            direct_stats.submitted == direct_stats.completed &&
            direct_stats.cancelled == 0u &&
            direct_stats.rejected_full == 0u &&
            direct_stats.failed == 0u &&
            direct_stats.native_submit_errors == 0u &&
            direct_stats.native_cancel_errors == 0u;
    }
    if (!cflow_io_native_adapter_get_stats(
            &fixture->adapter, &adapter_stats) ||
        adapter_stats.active_bridges != 0u ||
        adapter_stats.stale_actor_completions != 0u ||
        adapter_stats.native.active_requests != 0u ||
        adapter_stats.native.submitted != adapter_stats.native.completed ||
        adapter_stats.native.cancelled != 0u ||
        adapter_stats.native.failed != 0u ||
        adapter_stats.native.rejected_full != 0u ||
        adapter_stats.native.native_submit_errors != 0u ||
        adapter_stats.native.native_cancel_errors != 0u)
        return false;
    if (fixture->mode == ADAPTER_BENCH_SOURCE) {
        return cflow_io_source_owner_get_stats(
                   &fixture->source_owner, &source_stats) &&
            cflow_io_source_owner_get_window_stats(
                &fixture->source_owner, &window_stats) &&
            source_stats.actor.active_requests == 0u &&
            source_stats.actor.rejected_request_full == 0u &&
            source_stats.actor.rejected_command_full == 0u &&
            source_stats.actor.stale_completions == 0u &&
            source_stats.actor.backend_submit_errors == 0u &&
            source_stats.actor.executor_rejected_full == 0u &&
            source_stats.actor.acknowledged ==
                source_stats.actor.accepted &&
            fixture->source_sink_values ==
                source_stats.actor.accepted &&
            window_stats.occupied == 0u &&
            window_stats.demand_reserved == 0u &&
            window_stats.results_ready == 0u &&
            fixture->release_count == source_stats.actor.accepted;
    }
    return cflow_io_actor_get_stats(&fixture->actor, &actor_stats) &&
        actor_stats.active_requests == 0u &&
        actor_stats.rejected_request_full == 0u &&
        actor_stats.rejected_command_full == 0u &&
        actor_stats.stale_completions == 0u &&
        actor_stats.backend_submit_errors == 0u &&
        actor_stats.executor_rejected_full == 0u &&
        actor_stats.acknowledged == actor_stats.accepted &&
        fixture->release_count == actor_stats.accepted;
}

static double adapter_bench_throughput(const adapter_bench_result *result) {
    const double bytes = (double)result->payload_size *
        (double)ADAPTER_BENCH_TOTAL_TRANSFERS;
    return result->wall_ns == 0u ? 0.0
        : bytes * 1000000000.0 / (double)result->wall_ns /
              (1024.0 * 1024.0);
}

static double adapter_bench_cpu_throughput(
    const adapter_bench_result *result) {
    const double bytes = (double)result->payload_size *
        (double)ADAPTER_BENCH_TOTAL_TRANSFERS;
    return result->cpu_ns == 0u ? 0.0
        : bytes * 1000000000.0 / (double)result->cpu_ns /
              (1024.0 * 1024.0);
}

static double adapter_bench_delta(double value, double baseline) {
    return baseline == 0.0 ? 0.0 : (value / baseline - 1.0) * 100.0;
}

static double adapter_bench_mean(uint64_t total, uint64_t count) {
    return count == 0u ? 0.0 : (double)total / (double)count;
}

static void adapter_bench_finalize(adapter_bench_result *result) {
    qsort(result->latencies, result->latency_count,
          sizeof(result->latencies[0]), adapter_bench_compare_u64);
    result->p50_ns = result->latencies[
        (result->latency_count - 1u) * 50u / 100u];
    result->p95_ns = result->latencies[
        (result->latency_count - 1u) * 95u / 100u];
    result->p99_ns = result->latencies[
        (result->latency_count - 1u) * 99u / 100u];
}

static int adapter_bench_run_sample(
    adapter_bench_fixture *fixture,
    adapter_bench_result *result) {
    const uint64_t wall_started = turbo_hrtime();
    int status = TURBO_OK;

    for (size_t transfer = 0u;
         transfer < ADAPTER_BENCH_TRANSFERS_PER_SAMPLE; ++transfer) {
        const uint64_t started = turbo_hrtime();
        status = adapter_bench_exchange(fixture);
        if (status != TURBO_OK)
            break;
        result->latencies[result->latency_count++] =
            turbo_hrtime() - started;
    }
    adapter_bench_counter_add(
        &result->wall_ns, turbo_hrtime() - wall_started);
    return status;
}

static int adapter_bench_measure_cpu(
    adapter_bench_fixture *fixture,
    adapter_bench_result *result) {
    const uint64_t started = adapter_bench_process_cpu_ns();
    int status = TURBO_OK;

    for (size_t transfer = 0u;
         transfer < ADAPTER_BENCH_TOTAL_TRANSFERS; ++transfer) {
        status = adapter_bench_exchange(fixture);
        if (status != TURBO_OK)
            break;
    }
    result->cpu_ns = adapter_bench_process_cpu_ns() - started;
    return status;
}

static int adapter_bench_measure_stages(
    adapter_bench_fixture *fixture,
    adapter_bench_result *result) {
    int status = TURBO_OK;

    fixture->stages = &result->stages;
    for (size_t transfer = 0u;
         transfer < ADAPTER_BENCH_STAGE_TRANSFERS; ++transfer) {
        status = adapter_bench_exchange(fixture);
        if (status != TURBO_OK)
            break;
    }
    fixture->stages = NULL;
    return status;
}

static void adapter_bench_print_tables(
    adapter_bench_result results[][ADAPTER_BENCH_MODE_COUNT],
    size_t payload_count) {
    printf("\nProtocol: loopback TCP, %u samples x %u transfers, "
           "window=2, rotating mode order per sample; CPU and stage "
           "passes are measured separately.\n",
           (unsigned)ADAPTER_BENCH_SAMPLES,
           (unsigned)ADAPTER_BENCH_TRANSFERS_PER_SAMPLE);
    printf("\nCFlow NativeIO TCP latency (direct is denominator)\n");
    printf("| payload | mode | p50 us | p95 us | p99 us | p50 vs direct |\n");
    printf("| ---: | :--- | ---: | ---: | ---: | ---: |\n");
    for (size_t payload = 0u; payload < payload_count; ++payload) {
        const adapter_bench_result *direct =
            &results[payload][ADAPTER_BENCH_DIRECT];
        for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
            const adapter_bench_result *current = &results[payload][mode];
            printf("| %zu KiB | %s | %.3f | %.3f | %.3f | %+.2f%% |\n",
                   current->payload_size / 1024u,
                   adapter_bench_mode_name((adapter_bench_mode)mode),
                   (double)current->p50_ns / 1000.0,
                   (double)current->p95_ns / 1000.0,
                   (double)current->p99_ns / 1000.0,
                   adapter_bench_delta((double)current->p50_ns,
                                       (double)direct->p50_ns));
        }
    }

    printf("\nCFlow NativeIO TCP throughput (application payload counted once)\n");
    printf("| payload | direct MiB/s | Actor MiB/s | Actor vs direct | "
           "Source MiB/s | Source vs direct |\n");
    printf("| ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for (size_t payload = 0u; payload < payload_count; ++payload) {
        const double direct = adapter_bench_throughput(
            &results[payload][ADAPTER_BENCH_DIRECT]);
        const double actor = adapter_bench_throughput(
            &results[payload][ADAPTER_BENCH_ACTOR]);
        const double source = adapter_bench_throughput(
            &results[payload][ADAPTER_BENCH_SOURCE]);
        printf("| %zu KiB | %.2f | %.2f | %+.2f%% | %.2f | %+.2f%% |\n",
               results[payload][0].payload_size / 1024u,
               direct, actor, adapter_bench_delta(actor, direct),
               source, adapter_bench_delta(source, direct));
    }

    printf("\nCFlow NativeIO TCP process CPU\n");
    printf("| payload | mode | CPU us/transfer | MiB/CPU-s |\n");
    printf("| ---: | :--- | ---: | ---: |\n");
    for (size_t payload = 0u; payload < payload_count; ++payload) {
        for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
            const adapter_bench_result *current = &results[payload][mode];
            printf("| %zu KiB | %s | %.3f | %.2f |\n",
                   current->payload_size / 1024u,
                   adapter_bench_mode_name((adapter_bench_mode)mode),
                   (double)current->cpu_ns / 1000.0 /
                       (double)ADAPTER_BENCH_TOTAL_TRANSFERS,
                   adapter_bench_cpu_throughput(current));
        }
    }
#if defined(_WIN32)
    printf("Windows process CPU values use GetProcessTimes and may be "
           "quantized at the host accounting interval.\n");
#endif

    printf("\nCFlow NativeIO mean stage cost per native request\n");
    printf("| payload | mode | admission ns | native submit ns | observe ns | "
           "Actor transition ns | executor ns | ack ns | Source runtime ns | "
           "Source owner ns |\n");
    printf("| ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | "
           "---: | ---: |\n");
    for (size_t payload = 0u; payload < payload_count; ++payload) {
        for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
            const adapter_bench_result *current = &results[payload][mode];
            const adapter_bench_stages *stages = &current->stages;
            printf("| %zu KiB | %s | %.2f | %.2f | %.2f | %.2f | %.2f | "
                   "%.2f | %.2f | %.2f |\n",
                   current->payload_size / 1024u,
                   adapter_bench_mode_name((adapter_bench_mode)mode),
                   adapter_bench_mean(stages->admission_ns,
                                      stages->actor_operations),
                   adapter_bench_mean(stages->native_submit_ns,
                                      stages->native_submit_calls),
                   adapter_bench_mean(stages->observe_ns,
                                      stages->observe_calls),
                   adapter_bench_mean(stages->actor_transition_ns,
                                      stages->actor_operations),
                   adapter_bench_mean(stages->executor_delivery_ns,
                                      stages->actor_operations),
                   adapter_bench_mean(stages->acknowledge_ns,
                                      stages->actor_operations),
                   adapter_bench_mean(stages->source_runtime_ns,
                                      stages->actor_operations),
                   adapter_bench_mean(stages->source_owner_ns,
                                      stages->actor_operations));
        }
    }
}

spec("CFlow NativeIO adapter benchmark") {
    it("compares Actor and Source overhead against NativeIO direct") {
        static adapter_bench_result
            results[sizeof(ADAPTER_BENCH_PAYLOADS) /
                        sizeof(ADAPTER_BENCH_PAYLOADS[0])]
                   [ADAPTER_BENCH_MODE_COUNT];
        const size_t payload_count =
            sizeof(ADAPTER_BENCH_PAYLOADS) /
            sizeof(ADAPTER_BENCH_PAYLOADS[0]);

        memset(results, 0, sizeof(results));
        for (size_t payload = 0u; payload < payload_count; ++payload) {
            adapter_bench_fixture fixtures[ADAPTER_BENCH_MODE_COUNT];
            size_t initialized = 0u;
            int status = TURBO_OK;

            memset(fixtures, 0, sizeof(fixtures));
            for (size_t mode = 0u; mode < ADAPTER_BENCH_MODE_COUNT; ++mode) {
                adapter_bench_result *result = &results[payload][mode];
                adapter_bench_stages warmup_stages = {0};

                result->payload_size = ADAPTER_BENCH_PAYLOADS[payload];
                status = adapter_bench_fixture_init(
                    &fixtures[mode], (adapter_bench_mode)mode,
                    result->payload_size, &warmup_stages);
                initialized = mode + 1u;
                check_equal(status, TURBO_OK);
                if (status != TURBO_OK)
                    break;
                for (size_t warmup = 0u;
                     warmup < ADAPTER_BENCH_WARMUP_TRANSFERS; ++warmup) {
                    status = adapter_bench_exchange(&fixtures[mode]);
                    if (status != TURBO_OK)
                        break;
                }
                check_equal(status, TURBO_OK);
                fixtures[mode].stages = NULL;
                if (status != TURBO_OK)
                    break;
            }

            if (status == TURBO_OK) {
                for (size_t sample = 0u;
                     sample < ADAPTER_BENCH_SAMPLES && status == TURBO_OK;
                     ++sample) {
                    for (size_t offset = 0u;
                         offset < ADAPTER_BENCH_MODE_COUNT; ++offset) {
                        const size_t mode =
                            (payload + sample + offset) %
                            ADAPTER_BENCH_MODE_COUNT;
                        status = adapter_bench_run_sample(
                            &fixtures[mode], &results[payload][mode]);
                        if (status != TURBO_OK)
                            break;
                    }
                }
            }
            if (status == TURBO_OK) {
                for (size_t offset = 0u;
                     offset < ADAPTER_BENCH_MODE_COUNT; ++offset) {
                    const size_t mode =
                        (payload + offset) % ADAPTER_BENCH_MODE_COUNT;
                    status = adapter_bench_measure_cpu(
                        &fixtures[mode], &results[payload][mode]);
                    if (status != TURBO_OK)
                        break;
                }
            }
            if (status == TURBO_OK) {
                for (size_t offset = 0u;
                     offset < ADAPTER_BENCH_MODE_COUNT; ++offset) {
                    const size_t mode =
                        (payload + offset + 1u) %
                        ADAPTER_BENCH_MODE_COUNT;
                    status = adapter_bench_measure_stages(
                        &fixtures[mode], &results[payload][mode]);
                    if (status != TURBO_OK)
                        break;
                }
            }
            check_equal(status, TURBO_OK);
            for (size_t mode = 0u; mode < initialized; ++mode) {
                adapter_bench_result *result = &results[payload][mode];
                if (status == TURBO_OK) {
                    check_true(result->cpu_ns > 0u);
                    check_true(adapter_bench_validate(
                        &fixtures[mode], result));
                    adapter_bench_finalize(result);
                }
                check_equal(
                    adapter_bench_fixture_destroy(&fixtures[mode]),
                    TURBO_OK);
            }
            if (status != TURBO_OK)
                return;
        }
        adapter_bench_print_tables(results, payload_count);
    }
}
