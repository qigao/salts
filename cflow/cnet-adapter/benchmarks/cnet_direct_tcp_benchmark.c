/* Phase 3j.2: opt-in, real-TCP Direct CNet baseline.
 * This is not an Actor comparison and does not measure wire latency. */
#include "cnet_benchmark_stats.h"
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
enum { WARMUP = 2000, SAMPLES = 20000, DEADLINE_MS = 10000 };
typedef struct direct_fixture {
    cnet_client client;
    cnet_connection connection;
    uint64_t latencies[SAMPLES], scratch[SAMPLES];
    size_t received, settled;
    uint64_t checksum;
    int error;
    bool connected, terminal;
} direct_fixture;
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
    direct_fixture *f = (direct_fixture *)user;
    if (id.slot != f->connection.slot || id.generation != f->connection.generation) {
        /* During synchronous connect the result may not yet be published. */
        if (f->connection.slot != 0u) f->error = SALTS_EPROTO;
        return;
    }
    if (state == CNET_CONNECTION_CONNECTED) f->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED)
        f->terminal = true;
    if (state == CNET_CONNECTION_FAILED)
        f->error = err && err->status != SALTS_OK ? err->status : SALTS_EIO;
}
static void on_receive(void *user, cnet_connection id,
                       const cnet_receive_view *view) {
    direct_fixture *f = (direct_fixture *)user;
    uint64_t entered = cmeta_hrtime();
    if (id.slot != f->connection.slot || id.generation != f->connection.generation ||
        !view || view->kind != CNET_MESSAGE_BYTES || view->size != 1u ||
        ((const unsigned char *)view->data)[0] != (unsigned char)(f->received % 251u)) {
        f->error = SALTS_EPROTO;
        return;
    }
    /* Same deterministic business operation will be used in Actor mode. */
    f->checksum += ((const unsigned char *)view->data)[0];
    if (f->received >= WARMUP) {
        size_t index = f->received - WARMUP;
        if (index >= SAMPLES) { f->error = SALTS_ERANGE; return; }
        uint64_t ns = cmeta_hrtime() - entered;
        f->latencies[index] = ns ? ns : 1u;
    }
    ++f->received;
    ++f->settled;
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
    direct_fixture *f = (direct_fixture *)calloc(1, sizeof(*f));
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
    /* One bounded outstanding receive; never infer one callback per TCP send
     * without checking the completion count. Warmup excluded from samples. */
    for (size_t i = 0; i < WARMUP + SAMPLES; ++i) {
        uint64_t deadline = cmeta_monotonic_ms() + DEADLINE_MS;
        if (i == WARMUP) began = cmeta_hrtime();
        status = cnet_receive(&f->client, f->connection, 1u);
        if (status != SALTS_OK) goto done;
        status = send_byte(peer, (unsigned char)(i % 251u));
        if (status != SALTS_OK) goto done;
        while (f->received == i && cmeta_monotonic_ms() < deadline) {
            size_t events = 0;
            status = cnet_client_poll(&f->client, 1u, &events);
            if (status != SALTS_OK || f->error) goto done;
        }
        if (f->received != i + 1u) { status = SALTS_ETIMEDOUT; goto done; }
    }
    elapsed = cmeta_hrtime() - began;
    status = cflow_cnet_bench_summarize(f->latencies, SAMPLES, f->scratch,
                                         SAMPLES, &summary);
    if (status != SALTS_OK) goto done;
    if (f->settled != WARMUP + SAMPLES) { status = SALTS_EPROTO; goto done; }
    printf("mode,backend,warmup,samples,received,settled,checksum,elapsed_ns,p50_ns,p95_ns,p99_ns,max_ns\n");
    printf("direct-cnet,%s,%u,%u,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu\n",
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
    if (f->client.impl) {
        if (f->connection.slot && !f->terminal)
            (void)cnet_close(&f->client, f->connection);
        (void)cnet_client_stop(&f->client, DEADLINE_MS);
        (void)cnet_client_destroy(&f->client);
    }
    close_socket(peer);
    close_socket(listener);
    free(f);
    if (status != SALTS_OK) fprintf(stderr, "Direct CNet baseline failed: %d\n", status);
    return status == SALTS_OK ? 0 : 1;
}
