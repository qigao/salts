#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
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
    assert(fixture->listener >= 0);

    memset(&fixture->address, 0, sizeof(fixture->address));
    fixture->address.sin_family = AF_INET;
    fixture->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    fixture->address.sin_port = 0u;
    assert(bind(
               fixture->listener,
               (const struct sockaddr *)&fixture->address,
               sizeof(fixture->address)) == 0);
    assert(listen(fixture->listener, 4) == 0);
    assert(getsockname(
               fixture->listener,
               (struct sockaddr *)&fixture->address,
               &address_length) == 0);
    assert(fixture->address.sin_port != 0u);

    assert(native_io_backend_attach_socket(
               &fixture->backend,
               (uintptr_t)fixture->listener,
               &fixture->endpoint) == SALTS_OK);
    return SALTS_OK;
}

static int connect_peer(const struct sockaddr_in *address) {
    int peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(peer >= 0);
    assert(connect(
               peer, (const struct sockaddr *)address,
               sizeof(*address)) == 0);
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

static void destroy_fixture(
    accept_fixture *fixture, bool release_endpoint) {
    if (fixture->listener >= 0) {
        assert(close(fixture->listener) == 0);
        fixture->listener = -1;
    }
    if (release_endpoint)
        assert(native_io_backend_release_socket(
                   &fixture->backend,
                   fixture->endpoint) == SALTS_OK);
    assert(native_io_backend_close(
               &fixture->backend) == SALTS_OK);
    assert(native_io_backend_destroy(
               &fixture->backend) == SALTS_OK);
}

static void test_uring_accept_take_once(void) {
    accept_fixture fixture;
    native_io_request request = {0};
    native_io_completion completion;
    uintptr_t accepted = UINTPTR_MAX;
    uintptr_t duplicate = UINTPTR_MAX;
    int peer;
    int child_flags;

    if (fixture_init(&fixture) == SALTS_ENOTSUP)
        return;

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
    assert(completion.bytes == 0u);

    assert(native_io_backend_release_socket(
               &fixture.backend, fixture.endpoint) ==
           SALTS_EBUSY);

    assert(native_io_internal_take_stream_accept(
               &fixture.backend, request,
               &accepted) == SALTS_OK);
    assert(accepted != UINTPTR_MAX);
    child_flags = fcntl((int)accepted, F_GETFL, 0);
    assert(child_flags >= 0);
    assert((child_flags & O_NONBLOCK) != 0);
    assert(native_io_internal_take_stream_accept(
               &fixture.backend, request,
               &duplicate) == SALTS_ENOENT);
    assert(duplicate == UINTPTR_MAX);

    assert(close((int)accepted) == 0);
    assert(close(peer) == 0);
    destroy_fixture(&fixture, true);
}

static void test_uring_accept_cancel_has_no_child(void) {
    accept_fixture fixture;
    native_io_request request = {0};
    native_io_completion completion;
    uintptr_t accepted = UINTPTR_MAX;
    int cancel_status;

    if (fixture_init(&fixture) == SALTS_ENOTSUP)
        return;

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

    destroy_fixture(&fixture, true);
}

static void test_uring_close_retires_unclaimed_child(void) {
    accept_fixture fixture;
    native_io_request request = {0};
    native_io_completion completion;
    uintptr_t accepted = UINTPTR_MAX;
    int peer;

    if (fixture_init(&fixture) == SALTS_ENOTSUP)
        return;

    assert(native_io_internal_submit_stream_accept(
               &fixture.backend, fixture.endpoint,
               &request) == SALTS_OK);
    peer = connect_peer(&fixture.address);

    completion = observe_one(&fixture.backend);
    assert(completion.kind == NATIVE_IO_COMPLETION_OK);

    assert(native_io_backend_close(
               &fixture.backend) == SALTS_OK);
    assert(close(peer) == 0);
    assert(close(fixture.listener) == 0);
    fixture.listener = -1;

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
    test_uring_accept_take_once();
    test_uring_accept_cancel_has_no_child();
    test_uring_close_retires_unclaimed_child();
    return 0;
}
#else
int main(void) {
    return 0;
}
#endif
