#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "component_plugin_publication_fixture.h"
#include "tinytest.h"

#include <salts/native_io.h>
#include <salts/native_io_ace_token.h>
#include <salts/error_codes.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

/* NativeIO owns actual completion and buffer borrowing. ComponentPlugin owns
 * DSO/module retention. This typed ACT is only the caller's exact request-to-
 * borrowed-Scope association and must not create a second completion registry.
 *
 * POSIX-only executable fixture: epoll on Linux, kqueue on Darwin. Windows
 * native IOCP ACT and Plugin Scope contracts have independent regression
 * suites; never pretend a POSIX pipe qualifies an IOCP runtime.
 */
#if defined(__linux__)
#define ACE_NATIVE_BACKEND NATIVE_IO_BACKEND_EPOLL
#elif defined(__APPLE__)
#define ACE_NATIVE_BACKEND NATIVE_IO_BACKEND_KQUEUE
#else
#error "This real NativeIO pipe conformance suite needs epoll or kqueue"
#endif

enum { ACE_NATIVE_TIMEOUT_MS = 5000u };
NATIVE_IO_ACE_TOKEN_TYPE(ace_native_scope_token, salts_component_plugin_scope);

static void run_native_io_scope_case(bool cancel_request) {
    const cmeta_plugin_registry_config registry_config = {1};
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_ref plugin_ref;
    cmeta_plugin_lifecycle_info lifecycle;
    publication_generation_fixture generation = {0};
    salts_component_plugin_runtime runtime = {0};
    salts_component_plugin_scope scope = {0};
    salts_component_plugin_scope *settled_scope = NULL;
    salts_component_plugin_generation *retired = NULL;
    ace_native_scope_token token = {0};
    native_io_backend backend = {0};
    const native_io_backend_config config = {
        ACE_NATIVE_BACKEND, 1u, 1u, 1u
    };
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion completion = {0};
    native_io_operation operation = {0};
    int descriptors[2] = {-1, -1};
    unsigned char received = 0u;
    const unsigned char expected_byte = 0x5Au;
    size_t count = 0u;
    bool quiet = false;

    check_equal(cmeta_plugin_registry_init(
        &registry, &registry_config), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_load(
        &registry, COMPONENT_PROVIDER_PLUGIN_PATH, &plugin_ref), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_start(
        &registry, plugin_ref), CMETA_PLUGIN_OK);
    check_equal(build_generation(
        &generation, UINT64_C(9), &registry, plugin_ref), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(salts_component_plugin_runtime_init(
        &runtime), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(salts_component_plugin_runtime_publish(
        &runtime, &generation.generation, &retired), SALTS_COMPONENT_PLUGIN_OK);
    check_null(retired);
    check_equal(salts_component_plugin_scope_acquire(
        &runtime, &scope), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);

    check_equal(pipe(descriptors), 0);
    const int old_flags = fcntl(descriptors[0], F_GETFL, 0);
    check_true(old_flags >= 0);
    check_equal(fcntl(descriptors[0], F_SETFL, old_flags | O_NONBLOCK), 0);
    check_equal(native_io_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_pipe(
        &backend, (uintptr_t)descriptors[0],
        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint), SALTS_OK);
    operation.kind = NATIVE_IO_OPERATION_PIPE_READ;
    operation.endpoint = endpoint;
    operation.buffer = &received;
    operation.length = sizeof(received);
    operation.user_data = (uintptr_t)0xACu;
    check_equal(native_io_backend_submit(&backend, &operation, &request), SALTS_OK);
    check_equal(ace_native_scope_token_bind(
        &token, request, endpoint, operation.user_data, &scope), SALTS_OK);

    /* Closing Component publication does NOT close the borrowed request,
     * reclaim provider code, or turn a cancellation request into a terminal. */
    check_equal(salts_component_plugin_runtime_close(
        &runtime, &retired), SALTS_COMPONENT_PLUGIN_OK);
    check_true(retired == &generation.generation);
    check_equal(salts_component_plugin_generation_drain(
        &runtime, retired), SALTS_COMPONENT_PLUGIN_BUSY);
    check_equal(native_io_backend_release_pipe(
        &backend, endpoint), SALTS_EBUSY);
    check_equal(cmeta_plugin_registry_get_lifecycle(
        &registry, plugin_ref, &lifecycle), CMETA_PLUGIN_OK);
    check_equal(lifecycle.active_leases, (size_t)1u);
    check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);

    if (cancel_request) {
        check_equal(native_io_backend_cancel(&backend, request), SALTS_OK);
    } else {
        check_equal(write(descriptors[1], &expected_byte, 1u), 1);
    }
    /* Only a real NativeIO observation can complete the request. */
    check_equal(native_io_backend_observe(
        &backend, &completion, 1u, ACE_NATIVE_TIMEOUT_MS, &count), SALTS_OK);
    check_equal(count, 1u);
    check_equal(completion.request.slot, request.slot);
    check_equal(completion.request.generation, request.generation);
    check_equal(completion.user_data, operation.user_data);
    if (cancel_request) {
        check_equal(completion.kind, NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(completion.status, SALTS_ECANCELED);
    } else {
        check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(completion.bytes, (size_t)1u);
        check_equal(received, expected_byte);
    }
    check_equal(ace_native_scope_token_settle(
        &token, &completion, &settled_scope), SALTS_OK);
    check_true(settled_scope == &scope);
    check_equal(ace_native_scope_token_settle(
        &token, &completion, &settled_scope), SALTS_EALREADY);
    check_null(settled_scope);
    /* ACT settlement does not release the provider scope or DSO lease. */
    check_equal(salts_component_plugin_generation_drain(
        &runtime, retired), SALTS_COMPONENT_PLUGIN_BUSY);
    check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);

    check_equal(close(descriptors[0]), 0);
    check_equal(close(descriptors[1]), 0);
    check_equal(native_io_backend_release_pipe(
        &backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    check_equal(salts_component_plugin_scope_release(
        &scope), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(salts_component_plugin_generation_drain(
        &runtime, retired), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_get_lifecycle(
        &registry, plugin_ref, &lifecycle), CMETA_PLUGIN_OK);
    check_equal(lifecycle.active_leases, (size_t)0u);
    check_equal(salts_component_plugin_runtime_destroy(
        &runtime), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_request_stop(
        &registry, plugin_ref), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_poll_quiescent(
        &registry, plugin_ref, &quiet), CMETA_PLUGIN_OK);
    check_true(quiet);
    check_equal(cmeta_plugin_registry_unload(
        &registry, plugin_ref), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_destroy(
        &registry), CMETA_PLUGIN_OK);
}

suite("CMeta ACE real NativeIO ACT and Plugin lease conformance") {
    it("pins borrowed Plugin Scope until observed native read completion") {
        run_native_io_scope_case(false);
    }
    it("pins borrowed Plugin Scope until observed native cancel completion") {
        run_native_io_scope_case(true);
    }
}
