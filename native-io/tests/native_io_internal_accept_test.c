#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct accept_probe {
    native_io_endpoint listener;
    native_io_request request;
    uintptr_t transport;
    uint32_t submit_calls;
    uint32_t take_calls;
} accept_probe;

typedef struct fake_impl {
    salts_io_impl base;
    accept_probe *probe;
} fake_impl;

static int fake_submit_accept(
    salts_io_impl *base,
    native_io_endpoint listener,
    native_io_request *out_request) {
    fake_impl *impl = (fake_impl *)base;

    assert(impl != NULL);
    assert(impl->probe != NULL);
    assert(out_request != NULL);
    ++impl->probe->submit_calls;
    impl->probe->listener = listener;
    *out_request = impl->probe->request;
    return SALTS_OK;
}

static int fake_take_accept(
    salts_io_impl *base,
    native_io_request request,
    uintptr_t *out_transport) {
    fake_impl *impl = (fake_impl *)base;

    assert(impl != NULL);
    assert(impl->probe != NULL);
    assert(out_transport != NULL);
    ++impl->probe->take_calls;
    assert(request.slot == impl->probe->request.slot);
    assert(request.generation ==
           impl->probe->request.generation);
    *out_transport = impl->probe->transport;
    return SALTS_OK;
}

static void test_private_accept_seam_dispatches(void) {
    static const salts_io_impl_ops ops = {
        .submit_stream_accept = fake_submit_accept,
        .take_stream_accept = fake_take_accept
    };
    accept_probe probe = {
        .request = {7u, 11u},
        .transport = (uintptr_t)0x1234u
    };
    fake_impl impl = {0};
    native_io_backend backend = {0};
    native_io_endpoint listener = {3u, 5u};
    native_io_request request = {0};
    uintptr_t transport = UINTPTR_MAX;

    impl.base.ops = &ops;
    impl.probe = &probe;
    backend.impl = &impl;

    assert(native_io_internal_submit_stream_accept(
               &backend, listener, &request) == SALTS_OK);
    assert(probe.submit_calls == 1u);
    assert(probe.listener.slot == listener.slot);
    assert(probe.listener.generation == listener.generation);
    assert(request.slot == probe.request.slot);
    assert(request.generation == probe.request.generation);

    assert(native_io_internal_take_stream_accept(
               &backend, request, &transport) == SALTS_OK);
    assert(probe.take_calls == 1u);
    assert(transport == probe.transport);
}

static void test_private_accept_seam_fails_closed(void) {
    static const salts_io_impl_ops unsupported_ops = {0};
    salts_io_impl impl = {0};
    native_io_backend backend = {0};
    native_io_request request = {9u, 2u};
    native_io_request out_request = {8u, 8u};
    native_io_endpoint listener = {4u, 6u};
    uintptr_t transport = (uintptr_t)17u;

    impl.ops = &unsupported_ops;
    backend.impl = &impl;

    assert(native_io_internal_submit_stream_accept(
               &backend, listener, &out_request) ==
           SALTS_ENOTSUP);
    assert(out_request.slot == 0u);
    assert(out_request.generation == 0u);

    assert(native_io_internal_take_stream_accept(
               &backend, request, &transport) ==
           SALTS_ENOTSUP);
    assert(transport == UINTPTR_MAX);

    out_request = (native_io_request){8u, 8u};
    assert(native_io_internal_submit_stream_accept(
               NULL, listener, &out_request) ==
           SALTS_EINVAL);
    assert(out_request.slot == 0u);
    assert(out_request.generation == 0u);

    transport = (uintptr_t)17u;
    assert(native_io_internal_take_stream_accept(
               &backend, (native_io_request){0}, &transport) ==
           SALTS_EINVAL);
    assert(transport == UINTPTR_MAX);

    transport = (uintptr_t)17u;
    assert(native_io_internal_take_stream_accept(
               &backend, request, NULL) == SALTS_EINVAL);
    assert(transport == (uintptr_t)17u);
}

int main(void) {
    test_private_accept_seam_dispatches();
    test_private_accept_seam_fails_closed();
    return 0;
}
