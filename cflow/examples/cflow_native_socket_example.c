#include "cflow_native_example_runtime.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#if defined(interface)
#undef interface
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET cflow_example_socket;
#define CFLOW_EXAMPLE_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int cflow_example_socket;
#define CFLOW_EXAMPLE_INVALID_SOCKET (-1)
#endif

enum { CFLOW_SOCKET_EXAMPLE_SKIP = 77 };

typedef struct cflow_socket_example_operation {
    cflow_io_native_operation native;
    size_t released;
} cflow_socket_example_operation;

static cflow_io_native_backend_kind cflow_socket_example_backend(void) {
#if defined(_WIN32)
    return CFLOW_IO_NATIVE_IOCP;
#else
    return CFLOW_IO_NATIVE_POLL;
#endif
}

static int cflow_socket_example_last_error(void) {
#if defined(_WIN32)
    return -WSAGetLastError();
#else
    return -errno;
#endif
}

static void cflow_socket_example_close(cflow_example_socket socket_value) {
    if (socket_value == CFLOW_EXAMPLE_INVALID_SOCKET)
        return;
#if defined(_WIN32)
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
}

static int cflow_socket_example_set_nonblocking(
    cflow_example_socket socket_value) {
#if defined(_WIN32)
    u_long enabled = 1u;
    return ioctlsocket(socket_value, FIONBIO, &enabled) == 0
        ? TURBO_OK : cflow_socket_example_last_error();
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

static int cflow_socket_example_pair(cflow_example_socket sockets[2]) {
    cflow_example_socket listener = CFLOW_EXAMPLE_INVALID_SOCKET;
    struct sockaddr_in address;
    int address_length = (int)sizeof(address);
    int status = TURBO_OK;

    sockets[0] = CFLOW_EXAMPLE_INVALID_SOCKET;
    sockets[1] = CFLOW_EXAMPLE_INVALID_SOCKET;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == CFLOW_EXAMPLE_INVALID_SOCKET)
        return cflow_socket_example_last_error();
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0u;
    if (bind(listener, (const struct sockaddr *)&address,
             (int)sizeof(address)) != 0)
        status = cflow_socket_example_last_error();
    if (status == TURBO_OK &&
        getsockname(listener, (struct sockaddr *)&address,
#if defined(_WIN32)
                    &address_length
#else
                    (socklen_t *)&address_length
#endif
                    ) != 0)
        status = cflow_socket_example_last_error();
    if (status == TURBO_OK && listen(listener, 1) != 0)
        status = cflow_socket_example_last_error();
    if (status == TURBO_OK) {
        sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockets[0] == CFLOW_EXAMPLE_INVALID_SOCKET)
            status = cflow_socket_example_last_error();
    }
    if (status == TURBO_OK &&
        connect(sockets[0], (const struct sockaddr *)&address,
                (int)sizeof(address)) != 0)
        status = cflow_socket_example_last_error();
    if (status == TURBO_OK) {
        sockets[1] = accept(listener, NULL, NULL);
        if (sockets[1] == CFLOW_EXAMPLE_INVALID_SOCKET)
            status = cflow_socket_example_last_error();
    }
    cflow_socket_example_close(listener);
    if (status == TURBO_OK)
        status = cflow_socket_example_set_nonblocking(sockets[0]);
    if (status == TURBO_OK)
        status = cflow_socket_example_set_nonblocking(sockets[1]);
    if (status != TURBO_OK) {
        cflow_socket_example_close(sockets[0]);
        cflow_socket_example_close(sockets[1]);
        sockets[0] = CFLOW_EXAMPLE_INVALID_SOCKET;
        sockets[1] = CFLOW_EXAMPLE_INVALID_SOCKET;
    }
    return status;
}

static void cflow_socket_example_release(void *user) {
    cflow_socket_example_operation *operation =
        (cflow_socket_example_operation *)user;
    ++operation->released;
}

int main(void) {
    static const unsigned char payload = 0x53u;
    cflow_native_example_runtime runtime;
    cflow_example_socket sockets[2] = {
        CFLOW_EXAMPLE_INVALID_SOCKET, CFLOW_EXAMPLE_INVALID_SOCKET};
    cflow_socket_example_operation receive_operation = {0};
    cflow_socket_example_operation send_operation = {0};
    cflow_io_operation receive_token;
    cflow_io_operation send_token;
    cflow_io_submit_result receive_result;
    cflow_io_submit_result send_result;
    cflow_io_actor_stats actor_stats = {0};
    unsigned char received = 0u;
    int status;
    int result = EXIT_FAILURE;
    bool success = false;
    bool require_retained_identity;

    status = cflow_native_example_runtime_init(
        &runtime, cflow_socket_example_backend(),
        cflow_io_native_backend_actor_ops());
    if (status == TURBO_ENOTSUP) {
        fprintf(stderr,
                "native socket example: selected backend is unsupported; "
                "no fallback was attempted\n");
        return CFLOW_SOCKET_EXAMPLE_SKIP;
    }
    if (status != TURBO_OK) {
        fprintf(stderr, "native socket example: backend init failed: %d\n",
                status);
        (void)cflow_native_example_destroy_runtime(&runtime);
        return result;
    }
    status = cflow_socket_example_pair(sockets);
    if (status != TURBO_OK) {
        fprintf(stderr, "native socket example: loopback setup failed: %d\n",
                status);
        goto cleanup;
    }

    receive_operation.native = (cflow_io_native_operation){
        .kind = CFLOW_IO_NATIVE_TCP_RECV,
        .socket = (uintptr_t)sockets[1],
        .buffer = &received,
        .length = sizeof(received),
        .result_socket = CFLOW_IO_NATIVE_INVALID_SOCKET};
    send_operation.native = (cflow_io_native_operation){
        .kind = CFLOW_IO_NATIVE_TCP_SEND,
        .socket = (uintptr_t)sockets[0],
        .buffer = (void *)&payload,
        .length = sizeof(payload),
        .result_socket = CFLOW_IO_NATIVE_INVALID_SOCKET};
    receive_token = (cflow_io_operation){
        &receive_operation, cflow_socket_example_release};
    send_token = (cflow_io_operation){
        &send_operation, cflow_socket_example_release};
    receive_result = cflow_io_actor_try_submit(
        &runtime.actor, 1u, &receive_token);
    send_result = cflow_io_actor_try_submit(
        &runtime.actor, 2u, &send_token);
    if (receive_result.status != CFLOW_IO_SUBMIT_ACCEPTED ||
        send_result.status != CFLOW_IO_SUBMIT_ACCEPTED) {
        fprintf(stderr,
                "native socket example: bounded submit rejected: recv=%d "
                "send=%d\n",
                (int)receive_result.status, (int)send_result.status);
        goto cleanup;
    }
    status = cflow_native_example_drive_until(
        &runtime, CFLOW_NATIVE_EXAMPLE_CAPACITY);
    if (status != TURBO_OK) {
        fprintf(stderr, "native socket example: completion drain failed: %d\n",
                status);
        goto cleanup;
    }
    for (size_t index = 0u; index < runtime.log.count; ++index) {
        if (runtime.log.completions[index].kind != CFLOW_IO_COMPLETION_OK ||
            runtime.log.completions[index].bytes != sizeof(payload)) {
            fprintf(stderr,
                    "native socket example: unexpected completion kind=%d "
                    "bytes=%zu\n",
                    (int)runtime.log.completions[index].kind,
                    runtime.log.completions[index].bytes);
            goto cleanup;
        }
    }
    if (received != payload || receive_operation.released != 1u ||
        send_operation.released != 1u ||
        !cflow_io_actor_get_stats(&runtime.actor, &actor_stats) ||
        actor_stats.active_requests != 0u ||
        actor_stats.accepted != actor_stats.acknowledged) {
        fprintf(stderr,
                "native socket example: ownership invariant failed: "
                "received=%u releases=%zu/%zu active=%zu accepted=%llu "
                "acknowledged=%llu\n",
                (unsigned)received, receive_operation.released,
                send_operation.released, actor_stats.active_requests,
                (unsigned long long)actor_stats.accepted,
                (unsigned long long)actor_stats.acknowledged);
        goto cleanup;
    }
    success = true;

cleanup:
    require_retained_identity = success;
    if (!success) {
        status = cflow_native_example_close_actor(&runtime);
        if (status != TURBO_OK)
            fprintf(stderr,
                    "native socket example: Actor cleanup failed: %d\n",
                    status);
    }
    for (size_t index = 0u; index < 2u; ++index) {
        if (sockets[index] != CFLOW_EXAMPLE_INVALID_SOCKET) {
            const uintptr_t identity = (uintptr_t)sockets[index];
            cflow_socket_example_close(sockets[index]);
            sockets[index] = CFLOW_EXAMPLE_INVALID_SOCKET;
            status = cflow_io_native_backend_forget_socket(
                &runtime.backend, identity);
            if (status != TURBO_OK &&
                (require_retained_identity || status != TURBO_ENOENT)) {
                fprintf(stderr,
                        "native socket example: socket forget failed: %d\n",
                        status);
                success = false;
            }
        }
    }
    status = cflow_native_example_close_actor(&runtime);
    if (status != TURBO_OK) {
        fprintf(stderr, "native socket example: Actor close failed: %d\n",
                status);
        success = false;
    }
    status = cflow_native_example_destroy_runtime(&runtime);
    if (status != TURBO_OK) {
        fprintf(stderr, "native socket example: runtime destroy failed: %d\n",
                status);
        success = false;
    }
    if (success) {
        printf("native socket: completions=2 acknowledged=2 releases=2 "
               "active=0\n");
        result = EXIT_SUCCESS;
    }
    return result;
}
