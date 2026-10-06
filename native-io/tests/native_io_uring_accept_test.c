#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#include <tinytest.h>
#include <stdint.h>
#include <string.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct accept_fixture {
    native_io_backend backend;
    int listener;
    native_io_endpoint endpoint;
    struct sockaddr_in address;
} accept_fixture;

static accept_fixture test_fixture;
static int test_peer = -1;
static uintptr_t test_accepted = UINTPTR_MAX;
static uintptr_t test_duplicate = UINTPTR_MAX;

static void fixture_reset(accept_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->listener = -1;
}

static int fixture_init(accept_fixture *fixture) {
    native_io_backend_config config = {
        NATIVE_IO_BACKEND_IO_URING, 4u, 4u, 4u
    };
    socklen_t address_length =
        (socklen_t)sizeof(fixture->address);
    int status;

    fixture_reset(fixture);
    status = native_io_backend_init(
        &fixture->backend, &config);
    if (status != SALTS_OK)
        return status;

    fixture->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(fixture->listener >= 0);

    memset(&fixture->address, 0, sizeof(fixture->address));
    fixture->address.sin_family = AF_INET;
    fixture->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    fixture->address.sin_port = 0u;
    check(bind(
               fixture->listener,
               (const struct sockaddr *)&fixture->address,
               sizeof(fixture->address)) == 0);
    check(listen(fixture->listener, 4) == 0);
    check(getsockname(
               fixture->listener,
               (struct sockaddr *)&fixture->address,
               &address_length) == 0);
    check(fixture->address.sin_port != 0u);

    check(native_io_backend_attach_socket(
               &fixture->backend,
               (uintptr_t)fixture->listener,
               &fixture->endpoint) == SALTS_OK);
    return SALTS_OK;
}

static int connect_peer(const struct sockaddr_in *address) {
    test_peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(test_peer >= 0);
    check(connect(
               test_peer, (const struct sockaddr *)address,
               sizeof(*address)) == 0);
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

static void destroy_fixture(
    accept_fixture *fixture, bool release_endpoint) {
    if (fixture->listener >= 0) {
        check(close(fixture->listener) == 0);
        fixture->listener = -1;
    }
    if (release_endpoint)
        check(native_io_backend_release_socket(
                   &fixture->backend,
                   fixture->endpoint) == SALTS_OK);
    check(native_io_backend_close(
               &fixture->backend) == SALTS_OK);
    check(native_io_backend_destroy(
               &fixture->backend) == SALTS_OK);
}

static void test_uring_accept_take_once(void) {
    native_io_request request = {0};
    native_io_completion completion;
    int child_flags;

    {
        const int status = fixture_init(&test_fixture);
        if (status == SALTS_ENOTSUP) {
            puts("io_uring is unavailable at runtime");
            exit(77);
        }
        check(status == SALTS_OK);
    }

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
    check(completion.bytes == 0u);

    check(native_io_backend_release_socket(
               &test_fixture.backend, test_fixture.endpoint) ==
           SALTS_EBUSY);

    check(native_io_internal_take_stream_accept(
               &test_fixture.backend, request,
               &test_accepted) == SALTS_OK);
    check(test_accepted != UINTPTR_MAX);
    child_flags = fcntl((int)test_accepted, F_GETFL, 0);
    check(child_flags >= 0);
    check((child_flags & O_NONBLOCK) != 0);
    check(native_io_internal_take_stream_accept(
               &test_fixture.backend, request,
               &test_duplicate) == SALTS_ENOENT);
    check(test_duplicate == UINTPTR_MAX);

    check(close((int)test_accepted) == 0);
    test_accepted = UINTPTR_MAX;
    check(close(test_peer) == 0);
    test_peer = -1;
    destroy_fixture(&test_fixture, true);
}

static void test_uring_accept_cancel_has_no_child(void) {
    native_io_request request = {0};
    native_io_completion completion;
    int cancel_status;

    {
        const int status = fixture_init(&test_fixture);
        if (status == SALTS_ENOTSUP) {
            puts("io_uring is unavailable at runtime");
            exit(77);
        }
        check(status == SALTS_OK);
    }

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

    destroy_fixture(&test_fixture, true);
}

static void test_uring_close_retires_unclaimed_child(void) {
    native_io_request request = {0};
    native_io_completion completion;

    {
        const int status = fixture_init(&test_fixture);
        if (status == SALTS_ENOTSUP) {
            puts("io_uring is unavailable at runtime");
            exit(77);
        }
        check(status == SALTS_OK);
    }

    check(native_io_internal_submit_stream_accept(
               &test_fixture.backend, test_fixture.endpoint,
               &request) == SALTS_OK);
    test_peer = connect_peer(&test_fixture.address);

    completion = observe_one(&test_fixture.backend);
    check(completion.kind == NATIVE_IO_COMPLETION_OK);

    check(native_io_backend_close(
               &test_fixture.backend) == SALTS_OK);
    check(close(test_peer) == 0);
    test_peer = -1;
    check(close(test_fixture.listener) == 0);
    test_fixture.listener = -1;

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

suite("NativeIO io_uring accept") {
    before_each() {
        check_null(test_fixture.backend.impl);
        fixture_reset(&test_fixture);
        test_peer = -1;
        test_accepted = test_duplicate = UINTPTR_MAX;
    }
    after_each() {
        enum { CLEANUP_ATTEMPTS = 4, CLEANUP_WAIT_MS = 500 };
        if (test_accepted != UINTPTR_MAX) {
            check_warn(close((int)test_accepted) == 0);
            test_accepted = UINTPTR_MAX;
        }
        if (test_duplicate != UINTPTR_MAX) {
            check_warn(close((int)test_duplicate) == 0);
            test_duplicate = UINTPTR_MAX;
        }
        if (test_peer != -1) {
            check_warn(close(test_peer) == 0);
            test_peer = -1;
        }
        if (test_fixture.listener != -1) {
            check_warn(close(test_fixture.listener) == 0);
            test_fixture.listener = -1;
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
        it("uring accept take once") { test_uring_accept_take_once(); }
        it("uring accept cancel has no child") { test_uring_accept_cancel_has_no_child(); }
        it("uring close retires unclaimed child") { test_uring_close_retires_unclaimed_child(); }
    }
}
#else
suite("NativeIO io_uring accept") {
    it_skip("backend is unavailable on this platform") {}
}
#endif
