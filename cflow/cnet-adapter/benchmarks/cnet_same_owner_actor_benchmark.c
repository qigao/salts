/* Phase 3j.3: opt-in real-TCP same-owner Domain Actor experiment.
 * Callback-to-business ACK, not wire latency. */
#include "cnet_benchmark_stats.h"
#include <cflow/actor.h>
#include <cflow/executor.h>
#include <cflow/scheduler.h>
#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET bench_socket;
typedef int bench_socklen;
#define BENCH_INVALID INVALID_SOCKET
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int bench_socket;
typedef socklen_t bench_socklen;
#define BENCH_INVALID (-1)
#endif
enum { WARMUP = 2000, SAMPLES = 20000, DEADLINE_MS = 10000,
       BENCH_EVENT = 701, BENCH_ACTION = 702 };
typedef struct actor_fixture {
    cnet_client client;
    cflow_machine machine;
    cflow_executor executor;
    cflow_scheduler scheduler;
    cflow_actor actor;
    cflow_actor_ref ref;
    cflow_machine_action_binding binding;
    int initial_state;
    uint64_t receive_started;
    bool actor_initialized;
    int actor_errors;
    const char *failure_stage;
    cnet_connection connection;
    uint64_t latencies[SAMPLES], scratch[SAMPLES];
    size_t received, settled;
    uint64_t checksum;
    int error;
    bool connected, terminal;
} actor_fixture;
static void close_socket(bench_socket fd) {
    if (fd == BENCH_INVALID) return;
#if defined(_WIN32)
    (void)closesocket(fd);
#else
    (void)close(fd);
#endif
}
static bool would_block(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
static bool nonblocking(bench_socket fd) {
#if defined(_WIN32)
    u_long enabled = 1;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
static native_io_backend_kind selected_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static void on_state(void *user, cnet_connection id,
                     cnet_connection_state state, const cnet_error *err) {
    actor_fixture *f = (actor_fixture *)user;
    if (id.slot != f->connection.slot || id.generation != f->connection.generation) {
        /* During synchronous connect the result may not yet be published. */
        if (f->connection.slot != 0u) f->error = SALTS_EPROTO;
        return;
    }
    if (state == CNET_CONNECTION_CONNECTED) f->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
        f->terminal = true;
    }
    if (state == CNET_CONNECTION_FAILED)
        f->error = err && err->status != SALTS_OK ? err->status : SALTS_EIO;
}
static bool domain_action(void *user, const void *state, const void *event,
                          void *target, void *observation, const char **error) {
    actor_fixture *f = (actor_fixture *)user;
    const int *received_byte = (const int *)event;
    if (!received_byte || *received_byte != (int)(f->settled % 251u)) {
        f->failure_stage = "domain_action.validate";
        *error = "invalid typed mailbox payload";
        f->error = SALTS_EPROTO;
        return false;
    }
    f->checksum += (unsigned char)*received_byte;
    /* Completing this Machine action is the business-settlement boundary;
     * do not conflate Mailbox ACCEPTED with completion. */
    if (f->settled >= WARMUP) {
        size_t index = f->settled - WARMUP;
        if (index >= SAMPLES || !f->receive_started) {
            *error = "invalid sample index";
            f->error = SALTS_ERANGE;
            return false;
        }
        uint64_t ns = cmeta_hrtime() - f->receive_started;
        f->latencies[index] = ns ? ns : 1u;
    }
    ++f->settled;
    *(int *)target = *(const int *)state + 1;
    *(int *)observation = *(int *)target;
    *error = NULL;
    return true;
}
static bool on_value(void *user, const cmeta_type_desc *type, const void *value) {
    actor_fixture *f = (actor_fixture *)user;
    if (!type || !value || !cmeta_type_equal(type, &cmeta_type_int)) {
        f->failure_stage = "actor.on_value";
        f->actor_errors++;
        return false;
    }
    return true;
}
static void on_error(void *user, const char *msg) {
    actor_fixture *f = (actor_fixture *)user;
    if (msg) {
        f->failure_stage = msg;
        f->actor_errors++;
    }
}
static void on_done(void *user) { (void)user; }
static int actor_init(actor_fixture *f) {
    const cflow_machine_state states[] = {
        {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
    };
    const cflow_event_type events[] = {
        {BENCH_EVENT, &cmeta_type_int}
    };
    const cflow_machine_action actions[] = {{
        BENCH_ACTION, &cmeta_type_int, BENCH_EVENT,
        &cmeta_type_int, &cmeta_type_int,
        CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
        CFLOW_MACHINE_ACTION_VALUE, &cmeta_type_int, 0u
    }};
    const cflow_machine_transition transitions[] = {
        {10u, BENCH_EVENT, 0u, BENCH_ACTION, 10u, 1u}
    };
    const cflow_machine_definition def = {
        states, 1u, 10u, events, 1u, NULL, 0u,
        actions, 1u, transitions, 1u
    };
    cflow_actor_config cfg = {0};
    f->binding = (cflow_machine_action_binding){BENCH_ACTION, domain_action, f};
    f->failure_stage = "actor_init.machine_or_scheduler";
    if (cflow_machine_build(&f->machine, &def) != CFLOW_MACHINE_OK ||
        !cflow_executor_owner_init_with_capacity(&f->executor, 16u, NULL, NULL) ||
        !cflow_scheduler_owner_bind(&f->scheduler, &f->executor, 8u))
        return SALTS_EPROTO;
    cfg.machine = (cflow_machine_instance_config){
        &f->machine, &f->initial_state, &cmeta_type_int,
        NULL, 0u, &f->binding, 1u, 2u, &f->executor
    };
    cfg.scheduler = &f->scheduler;
    cfg.callbacks = (cflow_subscriber_callbacks){on_value, on_error, on_done, f};
    f->failure_stage = "actor_init.actor";
    if (cflow_actor_init(&f->actor, &cfg).status != CFLOW_ACTOR_OK ||
        !cflow_actor_ref_acquire(&f->actor, &f->ref) ||
        cflow_actor_start(&f->actor) != CFLOW_ACTOR_OK)
        return SALTS_EPROTO;
    f->actor_initialized = true;
    return SALTS_OK;
}
static void on_receive(void *user, cnet_connection id,
                       const cnet_receive_view *view) {
    actor_fixture *f = (actor_fixture *)user;
    f->receive_started = cmeta_hrtime();
    if (id.slot != f->connection.slot || id.generation != f->connection.generation ||
        !view || view->kind != CNET_MESSAGE_BYTES || view->size != 1u ||
        ((const unsigned char *)view->data)[0] !=
            (unsigned char)(f->received % 251u)) {
        f->failure_stage = "on_receive.validation";
        f->error = SALTS_EPROTO;
        return;
    }
    int payload = (int)((const unsigned char *)view->data)[0];
    const cflow_event_view event = { BENCH_EVENT, &cmeta_type_int, &payload };
    if (cflow_actor_ref_try_send(&f->ref, &event) != CFLOW_ACTOR_SEND_ACCEPTED) {
        f->failure_stage = "on_receive.mailbox_send";
        f->error = SALTS_EPROTO;
    } else ++f->received;
}
static int send_byte(bench_socket peer, unsigned char byte) {
    uint64_t deadline = cmeta_monotonic_ms() + DEADLINE_MS;
    for (;;) {
#if defined(_WIN32)
        int sent = send(peer, (const char *)&byte, 1, 0);
        if (sent == SOCKET_ERROR) {
#elif defined(MSG_NOSIGNAL)
        ssize_t sent = send(peer, &byte, 1, MSG_NOSIGNAL);
        if (sent < 0) {
#else
        ssize_t sent = send(peer, &byte, 1, 0);
        if (sent < 0) {
#endif
            if (!would_block()) return SALTS_EIO;
            if (cmeta_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
            cmeta_thread_yield();
            continue;
        }
        return sent == 1 ? SALTS_OK : SALTS_EIO;
    }
}
int main(void) {
    actor_fixture *f = (actor_fixture *)calloc(1, sizeof(*f));
    bench_socket listener = BENCH_INVALID, peer = BENCH_INVALID;
    struct sockaddr_in address = {0};
    bench_socklen address_len = (bench_socklen)sizeof(address);
    cnet_client_config cfg = {
        .backend = selected_backend(), .connection_capacity = 2u,
        .command_capacity = 8u, .request_capacity = 8u,
        .completion_batch_capacity = 4u, .event_capacity = 8u,
        .max_send_bytes = 1024u, .receive_buffer_bytes = 1024u
    };
    cnet_connect_options options = {0};
    cflow_cnet_bench_summary summary = {0};
    char uri[80] = {0};
    int status = SALTS_OK;
    uint64_t began = 0, elapsed = 0;
    if (!f) return 2;
    if (cnet_client_init(&f->client, &cfg) != SALTS_OK) { status = SALTS_EIO; goto done; }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == BENCH_INVALID) { status = SALTS_EIO; goto done; }
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (const struct sockaddr *)&address, (int)sizeof(address)) ||
        getsockname(listener, (struct sockaddr *)&address, &address_len) ||
        listen(listener, 2) || !nonblocking(listener)) {
        status = SALTS_EIO; goto done;
    }
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned)ntohs(address.sin_port));
    options.uri = uri;
    options.observer = (cnet_observer){ .on_state = on_state, .on_receive = on_receive, .user = f };
    status = cnet_connect(&f->client, &options, &f->connection);
    if (status != SALTS_OK) goto done;
    {
        const uint64_t deadline = cmeta_monotonic_ms() + DEADLINE_MS;
        while ((!f->connected || peer == BENCH_INVALID) && cmeta_monotonic_ms() < deadline) {
            size_t events = 0;
            if (peer == BENCH_INVALID) {
                peer = accept(listener, NULL, NULL);
                if (peer == BENCH_INVALID && !would_block()) { status = SALTS_EIO; goto done; }
            }
            status = cnet_client_poll(&f->client, 1u, &events);
            if (status != SALTS_OK || f->error) goto done;
        }
        if (!f->connected || peer == BENCH_INVALID) { status = SALTS_ETIMEDOUT; goto done; }
    }
    f->failure_stage = "actor_init";
    status = actor_init(f);
    if (status != SALTS_OK) goto done;
    /* One bounded outstanding receive; never infer one callback per TCP send
     * without checking the completion count. Warmup excluded from samples. */
    for (size_t i = 0; i < WARMUP + SAMPLES; ++i) {
        uint64_t deadline = cmeta_monotonic_ms() + DEADLINE_MS;
        if (i == WARMUP) began = cmeta_hrtime();
        f->failure_stage = "cnet.receive";
        status = cnet_receive(&f->client, f->connection, 1u);
        if (status != SALTS_OK) goto done;
        f->failure_stage = "tcp.send";
        status = send_byte(peer, (unsigned char)(i % 251u));
        if (status != SALTS_OK) goto done;
        f->failure_stage = "cnet.poll";
        while (f->received == i && cmeta_monotonic_ms() < deadline) {
            size_t events = 0;
            status = cnet_client_poll(&f->client, 1u, &events);
            if (status != SALTS_OK || f->error) goto done;
        }
        if (f->received != i + 1u) { status = SALTS_ETIMEDOUT; goto done; }
        f->failure_stage = "actor.executor";
        while (f->settled == i && cmeta_monotonic_ms() < deadline) {
            if (!cflow_executor_run_one(&f->executor))
                cmeta_thread_yield();
            if (f->error || f->actor_errors) {
                status = SALTS_EPROTO;
                goto done;
            }
        }
        if (f->settled != i + 1u) { status = SALTS_ETIMEDOUT; goto done; }
    }
    elapsed = cmeta_hrtime() - began;
    status = cflow_cnet_bench_summarize(f->latencies, SAMPLES, f->scratch,
                                         SAMPLES, &summary);
    if (status != SALTS_OK) goto done;
    if (f->settled != WARMUP + SAMPLES || f->received != WARMUP + SAMPLES) {
        status = SALTS_EPROTO; goto done;
    }
    {
        uint64_t expected_checksum = 0u;
        for (size_t i = 0u; i < WARMUP + SAMPLES; ++i)
            expected_checksum += (unsigned char)(i % 251u);
        if (f->checksum != expected_checksum || elapsed == 0u) {
            status = SALTS_EPROTO; goto done;
        }
    }
    printf("mode,backend,warmup,samples,received,settled,checksum,elapsed_ns,p50_ns,p95_ns,p99_ns,max_ns\n");
    printf("actor-same-owner,%s,%u,%u,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu\n",
#if defined(_WIN32)
           "iocp",
#elif defined(__APPLE__)
           "kqueue",
#else
           "epoll",
#endif
           WARMUP, SAMPLES, f->received, f->settled,
           (unsigned long long)f->checksum, (unsigned long long)elapsed,
           (unsigned long long)summary.p50_ns, (unsigned long long)summary.p95_ns,
           (unsigned long long)summary.p99_ns, (unsigned long long)summary.max_ns);
done:
    if (status == SALTS_OK && f->error) status = f->error;
    if (f->actor_initialized) {
        (void)cflow_actor_request_stop(&f->actor);
        uint64_t deadline = cmeta_monotonic_ms() + DEADLINE_MS;
        while (cflow_actor_current_state(&f->actor) != CFLOW_ACTOR_STATE_STOPPED &&
               cflow_actor_current_state(&f->actor) != CFLOW_ACTOR_STATE_FAILED &&
               cmeta_monotonic_ms() < deadline)
            if (!cflow_executor_run_one(&f->executor)) cmeta_thread_yield();
        cflow_actor_destroy(&f->actor);
        cflow_actor_ref_release(&f->ref);
        cflow_scheduler_destroy(&f->scheduler);
        cflow_executor_destroy(&f->executor);
        cflow_machine_destroy(&f->machine);
    }
    if (f->client.impl) {
        if (f->connection.slot && !f->terminal)
            (void)cnet_close(&f->client, f->connection);
        (void)cnet_client_stop(&f->client, DEADLINE_MS);
        (void)cnet_client_destroy(&f->client);
    }
    close_socket(peer);
    close_socket(listener);
    if (status != SALTS_OK) fprintf(stderr,
        "Same-owner Actor benchmark failed: status=%d stage=%s received=%zu settled=%zu actor_errors=%d callback_error=%d checksum=%llu\n",
        status, f->failure_stage ? f->failure_stage : "unknown", f->received,
        f->settled, f->actor_errors, f->error, (unsigned long long)f->checksum);
    free(f);
    return status == SALTS_OK ? 0 : 1;
}
