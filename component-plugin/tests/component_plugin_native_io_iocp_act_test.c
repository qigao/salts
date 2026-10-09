#if !defined(_WIN32)
#error "The ACE IOCP/Plugin DSO fixture requires Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "component_plugin_publication_fixture.h"
#include "tinytest.h"

#include <salts/error_codes.h>
#include <salts/native_io.h>
#include <salts/native_io_ace_token.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * True IOCP × ACT × Plugin DSO acceptance.
 * NativeIO owns the kernel completion, ComponentPlugin owns module leases,
 * and this typed ACT owns neither. Every callback, buffer and Scope borrowed
 * by the request stays address-stable until the observed terminal.
 */
enum { ACE_IOCP_PIPE_CAPACITY = 4096u, ACE_IOCP_TIMEOUT_MS = 5000u };
NATIVE_IO_ACE_TOKEN_TYPE(ace_iocp_scope_token, salts_component_plugin_scope);

static void ace_iocp_close_pipe(HANDLE handle) {
    if (handle != NULL && handle != INVALID_HANDLE_VALUE)
        (void)CloseHandle(handle);
}

/* Mirror NativeIO's proven overlapped named-pipe fixture, no socket fallback
 * or synchronous read masquerading as IOCP completion. */
static int ace_iocp_make_named_pipe_pair(HANDLE pipes[2]) {
    static LONG sequence = 0;
    char name[128];
    OVERLAPPED connected = {0};
    HANDLE event = NULL;
    DWORD error = ERROR_SUCCESS;
    BOOL pending = FALSE;
    int length;

    pipes[0] = INVALID_HANDLE_VALUE;
    pipes[1] = INVALID_HANDLE_VALUE;
    length = snprintf(name, sizeof(name), "\\\\.\\pipe\\ace-iocp-act-%lu-%ld",
                      GetCurrentProcessId(), InterlockedIncrement(&sequence));
    if (length < 0 || (size_t)length >= sizeof(name)) return SALTS_ERANGE;

    pipes[0] = CreateNamedPipeA(name,
                               PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                               1u, ACE_IOCP_PIPE_CAPACITY, ACE_IOCP_PIPE_CAPACITY,
                               0u, NULL);
    if (pipes[0] == INVALID_HANDLE_VALUE) return -(int)GetLastError();

    event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (event == NULL) {
        error = GetLastError();
        goto failed;
    }
    connected.hEvent = event;
    if (!ConnectNamedPipe(pipes[0], &connected)) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING)
            pending = TRUE;
        else if (error != ERROR_PIPE_CONNECTED)
            goto failed;
    }
    pipes[1] = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
                           0u, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pipes[1] == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto failed;
    }
    if (pending) {
        DWORD transferred = 0u;
        if (!GetOverlappedResult(pipes[0], &connected, &transferred, TRUE)) {
            error = GetLastError();
            goto failed;
        }
    }
    (void)CloseHandle(event);
    return SALTS_OK;

failed:
    ace_iocp_close_pipe(pipes[1]);
    ace_iocp_close_pipe(pipes[0]);
    if (event != NULL) (void)CloseHandle(event);
    pipes[0] = INVALID_HANDLE_VALUE;
    pipes[1] = INVALID_HANDLE_VALUE;
    return -(int)error;
}

static void ace_iocp_scope_native_completion(bool cancel_request) {
    const cmeta_plugin_registry_config registry_config = {1};
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_ref plugin_ref;
    cmeta_plugin_lifecycle_info info;
    publication_generation_fixture generation = {0};
    salts_component_plugin_runtime runtime = {0};
    salts_component_plugin_generation *retired = NULL;
    salts_component_plugin_scope scope = {0};
    salts_component_plugin_scope *settled = NULL;
    ace_iocp_scope_token token = {0};

    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IOCP, 2u, 2u, 2u};
    HANDLE pipes[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    native_io_endpoint endpoints[2] = {0};
    native_io_request read_request = {0};
    native_io_request write_request = {0};
    native_io_operation read_op = {0};
    native_io_operation write_op = {0};
    native_io_completion completions[2] = {0};
    native_io_completion read_terminal = {0};
    unsigned char received = 0u;
    unsigned char payload = 0x5au;
    bool found_read = false;
    bool found_write = false;
    bool quiescent = false;

    check_equal(cmeta_plugin_registry_init(
        &registry, &registry_config), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_load(
        &registry, COMPONENT_PROVIDER_PLUGIN_PATH, &plugin_ref), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_start(
        &registry, plugin_ref), CMETA_PLUGIN_OK);
    check_equal(build_generation(
        &generation, UINT64_C(13), &registry, plugin_ref), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(salts_component_plugin_runtime_init(
        &runtime), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(salts_component_plugin_runtime_publish(
        &runtime, &generation.generation, &retired), SALTS_COMPONENT_PLUGIN_OK);
    check_null(retired);
    check_equal(salts_component_plugin_scope_acquire(
        &runtime, &scope), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);

    check_equal(ace_iocp_make_named_pipe_pair(pipes), SALTS_OK);
    check_equal(native_io_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_pipe(
        &backend, (uintptr_t)pipes[0], NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
        &endpoints[0]), SALTS_OK);
    check_equal(native_io_backend_attach_pipe(
        &backend, (uintptr_t)pipes[1], NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
        &endpoints[1]), SALTS_OK);

    read_op.kind = NATIVE_IO_OPERATION_PIPE_READ;
    read_op.endpoint = endpoints[0];
    read_op.buffer = &received;
    read_op.length = sizeof(received);
    read_op.user_data = (uintptr_t)0xacu;
    check_equal(native_io_backend_submit(&backend, &read_op, &read_request), SALTS_OK);
    check_equal(ace_iocp_scope_token_bind(
        &token, read_request, endpoints[0], read_op.user_data, &scope), SALTS_OK);

    /* Admission is closed, but an active request + borrowed module Scope
     * must keep both resource owners alive until terminal and explicit release. */
    check_equal(salts_component_plugin_runtime_close(
        &runtime, &retired), SALTS_COMPONENT_PLUGIN_OK);
    check_true(retired == &generation.generation);
    check_equal(salts_component_plugin_generation_drain(
        &runtime, retired), SALTS_COMPONENT_PLUGIN_BUSY);
    check_equal(native_io_backend_release_pipe(
        &backend, endpoints[0]), SALTS_EBUSY);
    check_equal(cmeta_plugin_registry_get_lifecycle(
        &registry, plugin_ref, &info), CMETA_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    if (cancel_request) {
        check_equal(native_io_backend_cancel(&backend, read_request), SALTS_OK);
    } else {
        write_op.kind = NATIVE_IO_OPERATION_PIPE_WRITE;
        write_op.endpoint = endpoints[1];
        write_op.buffer = &payload;
        write_op.length = sizeof(payload);
        write_op.user_data = (uintptr_t)0xadu;
        check_equal(native_io_backend_submit(
            &backend, &write_op, &write_request), SALTS_OK);
    }

    const size_t expected = cancel_request ? 1u : 2u;
    size_t observed = 0u;
    while (observed < expected) {
        size_t count = 0u;
        check_equal(native_io_backend_observe(
            &backend, completions + observed, expected - observed,
            ACE_IOCP_TIMEOUT_MS, &count), SALTS_OK);
        check_true(count != 0u);
        observed += count;
    }
    for (size_t i = 0u; i < observed; ++i) {
        const native_io_completion *event = &completions[i];
        if (event->request.slot == read_request.slot &&
            event->request.generation == read_request.generation) {
            check_false(found_read);
            found_read = true;
            read_terminal = *event;
        } else {
            check_false(cancel_request);
            check_false(found_write);
            found_write = true;
            check_equal(event->request.slot, write_request.slot);
            check_equal(event->request.generation, write_request.generation);
            check_equal(event->kind, NATIVE_IO_COMPLETION_OK);
            check_equal(event->bytes, sizeof(payload));
        }
    }
    check_true(found_read);
    check_equal(found_write, !cancel_request);
    check_equal(read_terminal.endpoint.slot, endpoints[0].slot);
    check_equal(read_terminal.endpoint.generation, endpoints[0].generation);
    check_equal(read_terminal.user_data, read_op.user_data);
    if (cancel_request) {
        check_equal(read_terminal.kind, NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(read_terminal.status, SALTS_ECANCELED);
    } else {
        check_equal(read_terminal.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(read_terminal.bytes, sizeof(payload));
        check_equal(received, payload);
    }
    /* A stale generation cannot consume an ACT, even after kernel completion. */
    native_io_completion stale = read_terminal;
    ++stale.request.generation;
    check_equal(ace_iocp_scope_token_settle(
        &token, &stale, &settled), SALTS_ENOENT);
    check_null(settled);
    check_true(token.active);
    check_equal(scope_value(&scope), COMPONENT_PROVIDER_VALUE);
    check_equal(ace_iocp_scope_token_settle(
        &token, &read_terminal, &settled), SALTS_OK);
    check_true(settled == &scope);
    check_equal(ace_iocp_scope_token_settle(
        &token, &read_terminal, &settled), SALTS_EALREADY);
    check_null(settled);
    check_equal(salts_component_plugin_generation_drain(
        &runtime, retired), SALTS_COMPONENT_PLUGIN_BUSY);

    check_equal(native_io_backend_close(&backend), SALTS_OK);
    ace_iocp_close_pipe(pipes[0]);
    ace_iocp_close_pipe(pipes[1]);
    check_equal(native_io_backend_release_pipe(
        &backend, endpoints[0]), SALTS_OK);
    check_equal(native_io_backend_release_pipe(
        &backend, endpoints[1]), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    check_equal(salts_component_plugin_scope_release(
        &scope), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(salts_component_plugin_generation_drain(
        &runtime, retired), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_get_lifecycle(
        &registry, plugin_ref, &info), CMETA_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)0u);
    check_equal(salts_component_plugin_runtime_destroy(
        &runtime), SALTS_COMPONENT_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_request_stop(
        &registry, plugin_ref), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_poll_quiescent(
        &registry, plugin_ref, &quiescent), CMETA_PLUGIN_OK);
    check_true(quiescent);
    check_equal(cmeta_plugin_registry_unload(
        &registry, plugin_ref), CMETA_PLUGIN_OK);
    check_equal(cmeta_plugin_registry_destroy(
        &registry), CMETA_PLUGIN_OK);
}

suite("CMeta ACE IOCP completion x Plugin Scope conformance") {
    it("observes a real overlapped IOCP read before releasing Plugin DSO") {
        ace_iocp_scope_native_completion(false);
    }
    it("drains a real IOCP cancel terminal before releasing Plugin DSO") {
        ace_iocp_scope_native_completion(true);
    }
}
