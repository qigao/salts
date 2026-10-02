#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct accept_fixture {
    native_io_backend backend;
    SOCKET listener;
    native_io_endpoint endpoint;
    SOCKADDR_IN address;
} accept_fixture;

static void fixture_reset(accept_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->listener = INVALID_SOCKET;
}

static void fixture_init(accept_fixture *fixture) {
    native_io_backend_config config = {
        NATIVE_IO_BACKEND_IOCP, 4u, 4u, 4u
    };
    int address_length = (int)sizeof(fixture->address);

    fixture_reset(fixture);
    assert(native_io_backend_init(
               &fixture->backend, &config) == SALTS_OK);

    fixture->listener = WSASocketW(
        AF_INET, SOCK_STREAM, IPPROTO_TCP,
        NULL, 0u, WSA_FLAG_OVERLAPPED);
    assert(fixture->listener != INVALID_SOCKET);

    memset(&fixture->address, 0, sizeof(fixture->address));
    fixture->address.sin_family = AF_INET;
    fixture->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    fixture->address.sin_port = 0u;
    assert(bind(
               fixture->listener,
               (const SOCKADDR *)&fixture->address,
               (int)sizeof(fixture->address)) == 0);
    assert(listen(fixture->listener, 4) == 0);
    assert(getsockname(
               fixture->listener,
               (SOCKADDR *)&fixture->address,
               &address_length) == 0);
    assert(fixture->address.sin_port != 0u);

    assert(native_io_backend_attach_socket(
               &fixture->backend,
               (uintptr_t)fixture->listener,
               &fixture->endpoint) == SALTS_OK);
}

static SOCKET connect_peer(const SOCKADDR_IN *address) {
    SOCKET peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(peer != INVALID_SOCKET);
    assert(connect(
               peer, (const SOCKADDR *)address,
               (int)sizeof(*address)) == 0);
    return peer;
}

static native_io_completion observe_one(
    native_io_backend *backend) {
    native_io_completion completion = {0};
    size_t count = 0u;
    assert(native_io_backend_observe(
               backend, &completion, 1u, 2000u,
               &count) == SALTS_OK);
    assert(count == 1u);
    return completion;
}

static void test_iocp_accept_take_once(void) {
    accept_fixture fixture;
    native_io_request request = {0};
    native_io_completion completion;
    uintptr_t accepted = UINTPTR_MAX;
    uintptr_t duplicate = UINTPTR_MAX;
    SOCKET peer;

    fixture_init(&fixture);
    assert(native_io_internal_submit_stream_accept(
               &fixture.backend, fixture.endpoint,
               &request) == SALTS_OK);

    peer = connect_peer(&fixture.address);
    completion = observe_one(&fixture.backend);
    assert(completion.request.slot == request.slot);
    assert(completion.request.generation == request.generation);
    assert(completion.endpoint.slot == fixture.endpoint.slot);
    assert(completion.endpoint.generation ==
           fixture.endpoint.generation);
    assert(completion.kind == NATIVE_IO_COMPLETION_OK);
    assert(completion.status == SALTS_OK);

    /*
     * The observed request slot is retired, but the accepted child remains
     * escrow-owned and therefore keeps the listener generation retained.
     */
    assert(native_io_backend_release_socket(
               &fixture.backend, fixture.endpoint) ==
           SALTS_EBUSY);

    assert(native_io_internal_take_stream_accept(
               &fixture.backend, request,
               &accepted) == SALTS_OK);
    assert(accepted != UINTPTR_MAX);
    assert(native_io_internal_take_stream_accept(
               &fixture.backend, request,
               &duplicate) == SALTS_ENOENT);
    assert(duplicate == UINTPTR_MAX);

    assert(closesocket((SOCKET)accepted) == 0);
    assert(closesocket(peer) == 0);
    assert(closesocket(fixture.listener) == 0);
    fixture.listener = INVALID_SOCKET;
    assert(native_io_backend_release_socket(
               &fixture.backend, fixture.endpoint) ==
           SALTS_OK);

    assert(native_io_backend_close(
               &fixture.backend) == SALTS_OK);
    assert(native_io_backend_destroy(
               &fixture.backend) == SALTS_OK);
}

static void test_iocp_accept_cancel_has_no_child(void) {
    accept_fixture fixture;
    native_io_request request = {0};
    native_io_completion completion;
    uintptr_t accepted = UINTPTR_MAX;
    int cancel_status;

    fixture_init(&fixture);
    assert(native_io_internal_submit_stream_accept(
               &fixture.backend, fixture.endpoint,
               &request) == SALTS_OK);

    cancel_status = native_io_backend_cancel(
        &fixture.backend, request);
    assert(cancel_status == SALTS_OK ||
           cancel_status == SALTS_EALREADY);

    completion = observe_one(&fixture.backend);
    assert(completion.request.slot == request.slot);
    assert(completion.request.generation == request.generation);
    assert(completion.kind == NATIVE_IO_COMPLETION_CANCELLED);
    assert(completion.status == SALTS_ECANCELED);
    assert(native_io_internal_take_stream_accept(
               &fixture.backend, request,
               &accepted) == SALTS_ENOENT);
    assert(accepted == UINTPTR_MAX);

    assert(closesocket(fixture.listener) == 0);
    fixture.listener = INVALID_SOCKET;
    assert(native_io_backend_release_socket(
               &fixture.backend, fixture.endpoint) ==
           SALTS_OK);
    assert(native_io_backend_close(
               &fixture.backend) == SALTS_OK);
    assert(native_io_backend_destroy(
               &fixture.backend) == SALTS_OK);
}

static void test_iocp_close_retires_unclaimed_child(void) {
    accept_fixture fixture;
    native_io_request request = {0};
    native_io_completion completion;
    uintptr_t accepted = UINTPTR_MAX;
    SOCKET peer;

    fixture_init(&fixture);
    assert(native_io_internal_submit_stream_accept(
               &fixture.backend, fixture.endpoint,
               &request) == SALTS_OK);
    peer = connect_peer(&fixture.address);
    completion = observe_one(&fixture.backend);
    assert(completion.kind == NATIVE_IO_COMPLETION_OK);

    assert(native_io_backend_close(
               &fixture.backend) == SALTS_OK);
    assert(closesocket(peer) == 0);
    assert(closesocket(fixture.listener) == 0);
    fixture.listener = INVALID_SOCKET;

    /*
     * During backend shutdown, releasing the listener retires any terminal
     * accepted child still held by the private escrow. The child is not
     * claimable afterwards and is closed exactly once by the backend.
     */
    assert(native_io_backend_release_socket(
               &fixture.backend, fixture.endpoint) ==
           SALTS_OK);
    assert(native_io_internal_take_stream_accept(
               &fixture.backend, request,
               &accepted) == SALTS_ENOENT);
    assert(accepted == UINTPTR_MAX);
    assert(native_io_backend_destroy(
               &fixture.backend) == SALTS_OK);
}

int main(void) {
    test_iocp_accept_take_once();
    test_iocp_accept_cancel_has_no_child();
    test_iocp_close_retires_unclaimed_child();
    return 0;
}
#else
int main(void) {
    return 0;
}
#endif
