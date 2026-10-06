#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#include <tinytest.h>
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

static accept_fixture test_fixture;
static SOCKET test_peer = INVALID_SOCKET;
static uintptr_t test_accepted = UINTPTR_MAX;
static uintptr_t test_duplicate = UINTPTR_MAX;

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
    check(native_io_backend_init(
               &fixture->backend, &config) == SALTS_OK);

    fixture->listener = WSASocketW(
        AF_INET, SOCK_STREAM, IPPROTO_TCP,
        NULL, 0u, WSA_FLAG_OVERLAPPED);
    check(fixture->listener != INVALID_SOCKET);

    memset(&fixture->address, 0, sizeof(fixture->address));
    fixture->address.sin_family = AF_INET;
    fixture->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    fixture->address.sin_port = 0u;
    check(bind(
               fixture->listener,
               (const SOCKADDR *)&fixture->address,
               (int)sizeof(fixture->address)) == 0);
    check(listen(fixture->listener, 4) == 0);
    check(getsockname(
               fixture->listener,
               (SOCKADDR *)&fixture->address,
               &address_length) == 0);
    check(fixture->address.sin_port != 0u);

    check(native_io_backend_attach_socket(
               &fixture->backend,
               (uintptr_t)fixture->listener,
               &fixture->endpoint) == SALTS_OK);
}

static SOCKET connect_peer(const SOCKADDR_IN *address) {
    test_peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(test_peer != INVALID_SOCKET);
    check(connect(
               test_peer, (const SOCKADDR *)address,
               (int)sizeof(*address)) == 0);
    return test_peer;
}

static native_io_completion observe_one(
    native_io_backend *backend) {
    native_io_completion completion = {0};
    size_t count = 0u;
    check(native_io_backend_observe(
               backend, &completion, 1u, 2000u,
               &count) == SALTS_OK);
    check(count == 1u);
    return completion;
}

static void test_iocp_accept_take_once(void) {
    native_io_request request = {0};
    native_io_completion completion;

    fixture_init(&test_fixture);
    check(native_io_internal_submit_stream_accept(
               &test_fixture.backend, test_fixture.endpoint,
               &request) == SALTS_OK);

    test_peer = connect_peer(&test_fixture.address);
    completion = observe_one(&test_fixture.backend);
    check(completion.request.slot == request.slot);
    check(completion.request.generation == request.generation);
    check(completion.endpoint.slot == test_fixture.endpoint.slot);
    check(completion.endpoint.generation ==
           test_fixture.endpoint.generation);
    check(completion.kind == NATIVE_IO_COMPLETION_OK);
    check(completion.status == SALTS_OK);

    /*
     * The observed request slot is retired, but the accepted child remains
     * escrow-owned and therefore keeps the listener generation retained.
     */
    check(native_io_backend_release_socket(
               &test_fixture.backend, test_fixture.endpoint) ==
           SALTS_EBUSY);

    check(native_io_internal_take_stream_accept(
               &test_fixture.backend, request,
               &test_accepted) == SALTS_OK);
    check(test_accepted != UINTPTR_MAX);
    check(native_io_internal_take_stream_accept(
               &test_fixture.backend, request,
               &test_duplicate) == SALTS_ENOENT);
    check(test_duplicate == UINTPTR_MAX);

    check(closesocket((SOCKET)test_accepted) == 0);
    test_accepted = UINTPTR_MAX;
    check(closesocket(test_peer) == 0);
    test_peer = INVALID_SOCKET;
    check(closesocket(test_fixture.listener) == 0);
    test_fixture.listener = INVALID_SOCKET;
    check(native_io_backend_release_socket(
               &test_fixture.backend, test_fixture.endpoint) ==
           SALTS_OK);

    check(native_io_backend_close(
               &test_fixture.backend) == SALTS_OK);
    check(native_io_backend_destroy(
               &test_fixture.backend) == SALTS_OK);
}

static void test_iocp_accept_cancel_has_no_child(void) {
    native_io_request request = {0};
    native_io_completion completion;
    int cancel_status;

    fixture_init(&test_fixture);
    check(native_io_internal_submit_stream_accept(
               &test_fixture.backend, test_fixture.endpoint,
               &request) == SALTS_OK);

    cancel_status = native_io_backend_cancel(
        &test_fixture.backend, request);
    check(cancel_status == SALTS_OK ||
           cancel_status == SALTS_EALREADY);

    completion = observe_one(&test_fixture.backend);
    check(completion.request.slot == request.slot);
    check(completion.request.generation == request.generation);
    check(completion.kind == NATIVE_IO_COMPLETION_CANCELLED);
    check(completion.status == SALTS_ECANCELED);
    check(native_io_internal_take_stream_accept(
               &test_fixture.backend, request,
               &test_accepted) == SALTS_ENOENT);
    check(test_accepted == UINTPTR_MAX);

    check(closesocket(test_fixture.listener) == 0);
    test_fixture.listener = INVALID_SOCKET;
    check(native_io_backend_release_socket(
               &test_fixture.backend, test_fixture.endpoint) ==
           SALTS_OK);
    check(native_io_backend_close(
               &test_fixture.backend) == SALTS_OK);
    check(native_io_backend_destroy(
               &test_fixture.backend) == SALTS_OK);
}

static void test_iocp_close_retires_unclaimed_child(void) {
    native_io_request request = {0};
    native_io_completion completion;

    fixture_init(&test_fixture);
    check(native_io_internal_submit_stream_accept(
               &test_fixture.backend, test_fixture.endpoint,
               &request) == SALTS_OK);
    test_peer = connect_peer(&test_fixture.address);
    completion = observe_one(&test_fixture.backend);
    check(completion.kind == NATIVE_IO_COMPLETION_OK);

    check(native_io_backend_close(
               &test_fixture.backend) == SALTS_OK);
    check(closesocket(test_peer) == 0);
    test_peer = INVALID_SOCKET;
    check(closesocket(test_fixture.listener) == 0);
    test_fixture.listener = INVALID_SOCKET;

    /*
     * During backend shutdown, releasing the listener retires any terminal
     * accepted child still held by the private escrow. The child is not
     * claimable afterwards and is closed exactly once by the backend.
     */
    check(native_io_backend_release_socket(
               &test_fixture.backend, test_fixture.endpoint) ==
           SALTS_OK);
    check(native_io_internal_take_stream_accept(
               &test_fixture.backend, request,
               &test_accepted) == SALTS_ENOENT);
    check(test_accepted == UINTPTR_MAX);
    check(native_io_backend_destroy(
               &test_fixture.backend) == SALTS_OK);
}

suite("NativeIO IOCP accept") {
    before_each() {
        check_null(test_fixture.backend.impl);
        fixture_reset(&test_fixture);
        test_peer = INVALID_SOCKET;
        test_accepted = test_duplicate = UINTPTR_MAX;
    }
    after_each() {
        enum { CLEANUP_ATTEMPTS = 4, CLEANUP_WAIT_MS = 500 };
        if (test_accepted != UINTPTR_MAX) {
            check_warn(closesocket((SOCKET)test_accepted) == 0);
            test_accepted = UINTPTR_MAX;
        }
        if (test_duplicate != UINTPTR_MAX) {
            check_warn(closesocket((SOCKET)test_duplicate) == 0);
            test_duplicate = UINTPTR_MAX;
        }
        if (test_peer != INVALID_SOCKET) {
            check_warn(closesocket(test_peer) == 0);
            test_peer = INVALID_SOCKET;
        }
        if (test_fixture.listener != INVALID_SOCKET) {
            check_warn(closesocket(test_fixture.listener) == 0);
            test_fixture.listener = INVALID_SOCKET;
        }
        if (test_fixture.backend.impl != NULL) {
            int status = native_io_backend_close(&test_fixture.backend);
            check_warn(status == SALTS_OK || status == SALTS_EALREADY);
            if (native_io_endpoint_valid(test_fixture.endpoint)) {
                int release_status = SALTS_EBUSY;
                for (unsigned attempt = 0; attempt < CLEANUP_ATTEMPTS; ++attempt) {
                    release_status = native_io_backend_release_socket(
                        &test_fixture.backend, test_fixture.endpoint);
                    if (release_status != SALTS_EBUSY) break;
                    native_io_completion completion;
                    size_t count = 0u;
                    status = native_io_backend_observe(
                        &test_fixture.backend, &completion, 1u, CLEANUP_WAIT_MS, &count);
                    check_warn(status == SALTS_OK || status == SALTS_ETIMEDOUT);
                }
                check_warn(release_status == SALTS_OK || release_status == SALTS_ENOENT);
            }
            check_warn(native_io_backend_destroy(&test_fixture.backend) == SALTS_OK);
        }
    }
    group("accept lifecycle") {
        it("iocp accept take once") { test_iocp_accept_take_once(); }
        it("iocp accept cancel has no child") { test_iocp_accept_cancel_has_no_child(); }
        it("iocp close retires unclaimed child") { test_iocp_close_retires_unclaimed_child(); }
    }
}
#else
suite("NativeIO IOCP accept") {
    it_skip("backend is unavailable on this platform") {}
}
#endif
