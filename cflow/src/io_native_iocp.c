#include "io_native_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#if defined(interface)
#undef interface
#endif
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum cflow_iocp_record_phase {
    CFLOW_IOCP_RECORD_FREE = 0,
    CFLOW_IOCP_RECORD_PENDING
} cflow_iocp_record_phase;

typedef enum cflow_iocp_resource_kind {
    CFLOW_IOCP_RESOURCE_SOCKET = 0,
    CFLOW_IOCP_RESOURCE_PIPE,
    CFLOW_IOCP_RESOURCE_FILE
} cflow_iocp_resource_kind;

typedef struct cflow_iocp_record {
    OVERLAPPED overlapped;
    WSABUF buffer;
    WSABUF vector_buffers[CFLOW_IO_NATIVE_VECTOR_MAX];
    cflow_iocp_record_phase phase;
    cflow_io_request_id request_id;
    cflow_io_actor *actor;
    cflow_io_native_operation *operation;
    cflow_io_native_pipe_operation *pipe_operation;
    cflow_io_native_file_operation *file_operation;
    cflow_io_native_vector_operation_kind vector_kind;
    DWORD vector_buffer_count;
    HANDLE native_handle;
    SOCKET socket_value;
    DWORD flags;
    int address_length;
    SOCKET accepted_socket;
    unsigned char accept_addresses[
        2u * (sizeof(SOCKADDR_STORAGE) + 16u)];
    bool cancel_requested;
    cflow_iocp_resource_kind resource_kind;
} cflow_iocp_record;

_Static_assert(offsetof(cflow_iocp_record, overlapped) == 0u,
               "OVERLAPPED must remain the stable record prefix");

typedef struct cflow_iocp_resource_record {
    HANDLE native_handle;
    bool active;
    bool associating;
} cflow_iocp_resource_record;

typedef struct cflow_iocp_impl {
    cflow_io_native_impl base;
    turbo_mutex_t gate;
    turbo_cond_t changed;
    turbo_thread_t worker;
    HANDLE port;
    cflow_iocp_record *records;
    cflow_iocp_resource_record *resources;
    size_t request_capacity;
    size_t completion_batch_capacity;
    size_t active_requests;
    uint64_t submitted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t stale_native_completions;
    uint64_t native_submit_errors;
    uint64_t native_cancel_errors;
    bool admission_open;
    bool worker_running;
    bool shutdown_complete;
    bool winsock_started;
} cflow_iocp_impl;

static const ULONG_PTR CFLOW_IOCP_STOP_KEY = (ULONG_PTR)1u;

static void iocp_counter_increment(uint64_t *counter) {
    if (*counter != UINT64_MAX)
        ++*counter;
}

static int iocp_error(DWORD error) {
    return error == ERROR_SUCCESS ? TURBO_EIO : -(int)error;
}

static int iocp_accept_extension(SOCKET socket_value,
                                 LPFN_ACCEPTEX *out) {
    GUID id = WSAID_ACCEPTEX;
    DWORD bytes = 0u;
    if (WSAIoctl(socket_value, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &id, sizeof(id), out, sizeof(*out), &bytes,
                 NULL, NULL) == 0)
        return TURBO_OK;
    return -(int)WSAGetLastError();
}

static int iocp_connect_extension(SOCKET socket_value,
                                  LPFN_CONNECTEX *out) {
    GUID id = WSAID_CONNECTEX;
    DWORD bytes = 0u;
    if (WSAIoctl(socket_value, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &id, sizeof(id), out, sizeof(*out), &bytes,
                 NULL, NULL) == 0)
        return TURBO_OK;
    return -(int)WSAGetLastError();
}

static int iocp_create_accept_socket(SOCKET listener, SOCKET *out) {
    WSAPROTOCOL_INFO protocol;
    int protocol_length = (int)sizeof(protocol);
    SOCKET accepted;
    if (getsockopt(listener, SOL_SOCKET, SO_PROTOCOL_INFO,
                   (char *)&protocol, &protocol_length) != 0)
        return -(int)WSAGetLastError();
    accepted = WSASocket(protocol.iAddressFamily, protocol.iSocketType,
                         protocol.iProtocol, NULL, 0u,
                         WSA_FLAG_OVERLAPPED);
    if (accepted == INVALID_SOCKET)
        return -(int)WSAGetLastError();
    *out = accepted;
    return TURBO_OK;
}

static int iocp_bind_connect_socket(SOCKET socket_value) {
    WSAPROTOCOL_INFO protocol;
    SOCKADDR_STORAGE local;
    int protocol_length = (int)sizeof(protocol);
    int local_length = (int)sizeof(local);
    int bind_length;
    int error;
    memset(&local, 0, sizeof(local));
    if (getsockname(socket_value, (struct sockaddr *)&local,
                    &local_length) == 0)
        return TURBO_OK;
    error = WSAGetLastError();
    if (error != WSAEINVAL)
        return -(int)error;
    if (getsockopt(socket_value, SOL_SOCKET, SO_PROTOCOL_INFO,
                   (char *)&protocol, &protocol_length) != 0)
        return -(int)WSAGetLastError();
    if (protocol.iAddressFamily == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)&local;
        ipv4->sin_family = AF_INET;
        bind_length = (int)sizeof(*ipv4);
    } else if (protocol.iAddressFamily == AF_INET6) {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&local;
        ipv6->sin6_family = AF_INET6;
        bind_length = (int)sizeof(*ipv6);
    } else {
        return TURBO_ENOTSUP;
    }
    if (bind(socket_value, (const struct sockaddr *)&local,
             bind_length) == 0)
        return TURBO_OK;
    error = WSAGetLastError();
    /* Winsock may report WSAEINVAL from getsockname for a socket already
       bound to ADDR_ANY. In that case ConnectEx remains the authoritative
       state check; every other bind error is terminal here. */
    return error == WSAEINVAL ? TURBO_OK : -(int)error;
}

static cflow_iocp_record *iocp_find_free_locked(cflow_iocp_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase == CFLOW_IOCP_RECORD_FREE)
            return &impl->records[index];
    }
    return NULL;
}

static cflow_iocp_record *iocp_find_request_locked(
    cflow_iocp_impl *impl, cflow_io_request_id request_id) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase == CFLOW_IOCP_RECORD_PENDING &&
            impl->records[index].request_id == request_id)
            return &impl->records[index];
    }
    return NULL;
}

static int iocp_begin_operation(cflow_iocp_record *record) {
    cflow_io_native_operation *operation = record->operation;
    DWORD bytes = 0u;
    int status;

    record->buffer.buf = (CHAR *)operation->buffer;
    record->buffer.len = (ULONG)operation->length;
    record->flags = 0u;
    switch (operation->kind) {
        case CFLOW_IO_NATIVE_TCP_RECV:
            status = WSARecv(record->socket_value, &record->buffer, 1u,
                             &bytes, &record->flags, &record->overlapped,
                             NULL);
            break;
        case CFLOW_IO_NATIVE_TCP_SEND:
            status = WSASend(record->socket_value, &record->buffer, 1u,
                             &bytes, 0u, &record->overlapped, NULL);
            break;
        case CFLOW_IO_NATIVE_UDP_RECV_FROM:
            record->address_length = (int)operation->address_capacity;
            status = WSARecvFrom(record->socket_value, &record->buffer, 1u,
                                 &bytes, &record->flags,
                                 (struct sockaddr *)operation->address,
                                 &record->address_length,
                                 &record->overlapped, NULL);
            break;
        case CFLOW_IO_NATIVE_UDP_SEND_TO:
            status = WSASendTo(record->socket_value, &record->buffer, 1u,
                               &bytes, 0u,
                               (const struct sockaddr *)operation->address,
                               (int)operation->address_length,
                               &record->overlapped, NULL);
            break;
        case CFLOW_IO_NATIVE_TCP_ACCEPT: {
            LPFN_ACCEPTEX accept_ex = NULL;
            const DWORD address_bytes =
                (DWORD)(sizeof(SOCKADDR_STORAGE) + 16u);
            status = iocp_accept_extension(record->socket_value,
                                           &accept_ex);
            if (status != TURBO_OK)
                return status;
            status = iocp_create_accept_socket(
                record->socket_value, &record->accepted_socket);
            if (status != TURBO_OK)
                return status;
            status = accept_ex(
                         record->socket_value, record->accepted_socket,
                         record->accept_addresses, 0u, address_bytes,
                         address_bytes, &bytes, &record->overlapped)
                         ? 0
                         : SOCKET_ERROR;
            break;
        }
        case CFLOW_IO_NATIVE_TCP_CONNECT: {
            LPFN_CONNECTEX connect_ex = NULL;
            status = iocp_connect_extension(record->socket_value,
                                            &connect_ex);
            if (status != TURBO_OK)
                return status;
            status = iocp_bind_connect_socket(record->socket_value);
            if (status != TURBO_OK)
                return status;
            status = connect_ex(
                         record->socket_value,
                         (const struct sockaddr *)operation->address,
                         (int)operation->address_length, NULL, 0u, &bytes,
                         &record->overlapped)
                         ? 0
                         : SOCKET_ERROR;
            break;
        }
        default:
            return TURBO_EINVAL;
    }
    if (status == 0)
        return TURBO_OK;
    status = WSAGetLastError();
    return status == WSA_IO_PENDING ? TURBO_OK : -(int)status;
}

static int iocp_begin_vector_operation(cflow_iocp_record *record) {
    DWORD bytes = 0u;
    int status;
    record->flags = 0u;
    if (record->vector_kind == CFLOW_IO_NATIVE_TCP_RECV_VECTOR) {
        status = WSARecv(record->socket_value, record->vector_buffers,
                         record->vector_buffer_count, &bytes, &record->flags,
                         &record->overlapped, NULL);
    } else {
        status = WSASend(record->socket_value, record->vector_buffers,
                         record->vector_buffer_count, &bytes, 0u,
                         &record->overlapped, NULL);
    }
    if (status == 0)
        return TURBO_OK;
    status = WSAGetLastError();
    return status == WSA_IO_PENDING ? TURBO_OK : -(int)status;
}

static int iocp_begin_pipe_operation(cflow_iocp_record *record) {
    cflow_io_native_pipe_operation *operation = record->pipe_operation;
    BOOL started;
    DWORD error;

    if (operation->kind == CFLOW_IO_NATIVE_PIPE_READ) {
        started = ReadFile(record->native_handle, operation->buffer,
                           (DWORD)operation->length, NULL,
                           &record->overlapped);
    } else {
        started = WriteFile(record->native_handle, operation->buffer,
                            (DWORD)operation->length, NULL,
                            &record->overlapped);
    }
    if (started)
        return TURBO_OK;
    error = GetLastError();
    return error == ERROR_IO_PENDING ? TURBO_OK : iocp_error(error);
}

static int iocp_begin_file_operation(cflow_iocp_impl *impl,
                                     cflow_iocp_record *record) {
    cflow_io_native_file_operation *operation = record->file_operation;
    BOOL started;
    DWORD error;

    record->overlapped.Offset =
        (DWORD)(operation->offset & (uint64_t)UINT32_MAX);
    record->overlapped.OffsetHigh = (DWORD)(operation->offset >> 32u);
    if (operation->kind == CFLOW_IO_NATIVE_FILE_READ_AT) {
        started = ReadFile(record->native_handle, operation->buffer,
                           (DWORD)operation->length, NULL,
                           &record->overlapped);
    } else if (operation->kind == CFLOW_IO_NATIVE_FILE_WRITE_AT) {
        started = WriteFile(record->native_handle, operation->buffer,
                            (DWORD)operation->length, NULL,
                            &record->overlapped);
    } else {
        return TURBO_ENOTSUP;
    }
    if (started)
        return TURBO_OK;
    error = GetLastError();
    if (error == ERROR_IO_PENDING)
        return TURBO_OK;
    if (operation->kind == CFLOW_IO_NATIVE_FILE_READ_AT &&
        error == ERROR_HANDLE_EOF) {
        return PostQueuedCompletionStatus(
                   impl->port, 0u, 0u, &record->overlapped)
                   ? TURBO_OK : iocp_error(GetLastError());
    }
    return iocp_error(error);
}

static int iocp_finish_accept(SOCKET listener,
                              cflow_io_native_operation *operation,
                              SOCKET accepted_socket) {
    SOCKADDR_STORAGE peer;
    int peer_length = (int)sizeof(peer);
    u_long nonblocking = 1u;
    if (setsockopt(accepted_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (const char *)&listener,
                   (int)sizeof(listener)) != 0)
        return -(int)WSAGetLastError();
    if (ioctlsocket(accepted_socket, FIONBIO, &nonblocking) != 0)
        return -(int)WSAGetLastError();
    if (operation->address == NULL)
        return TURBO_OK;
    memset(&peer, 0, sizeof(peer));
    if (getpeername(accepted_socket, (struct sockaddr *)&peer,
                    &peer_length) != 0)
        return -(int)WSAGetLastError();
    if ((size_t)peer_length > operation->address_capacity)
        return TURBO_ERANGE;
    memcpy(operation->address, &peer, (size_t)peer_length);
    operation->address_length = (size_t)peer_length;
    return TURBO_OK;
}

static int iocp_finish_connect(SOCKET socket_value) {
    if (setsockopt(socket_value, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
                   NULL, 0) == 0)
        return TURBO_OK;
    return -(int)WSAGetLastError();
}

static void iocp_finish_record(cflow_iocp_impl *impl,
                               cflow_iocp_record *record,
                               DWORD bytes,
                               DWORD native_error) {
    cflow_io_actor *actor;
    cflow_io_request_id request_id;
    cflow_io_native_operation *operation;
    cflow_io_native_pipe_operation *pipe_operation;
    cflow_io_native_file_operation *file_operation;
    cflow_io_completion completion;
    cflow_io_complete_status delivery_status;
    SOCKET socket_value;
    SOCKET accepted_socket;
    cflow_iocp_resource_kind resource_kind;
    cflow_io_native_vector_operation_kind vector_kind;
    DWORD vector_buffer_count;
    bool cancelled;

    turbo_mutex_lock(&impl->gate);
    if (record->phase != CFLOW_IOCP_RECORD_PENDING) {
        iocp_counter_increment(&impl->stale_native_completions);
        turbo_mutex_unlock(&impl->gate);
        return;
    }
    actor = record->actor;
    request_id = record->request_id;
    operation = record->operation;
    pipe_operation = record->pipe_operation;
    file_operation = record->file_operation;
    resource_kind = record->resource_kind;
    vector_kind = record->vector_kind;
    vector_buffer_count = record->vector_buffer_count;
    socket_value = record->socket_value;
    accepted_socket = record->accepted_socket;
    cancelled = record->cancel_requested &&
                (native_error == ERROR_OPERATION_ABORTED ||
                 native_error == WSA_OPERATION_ABORTED);
    if (resource_kind == CFLOW_IOCP_RESOURCE_SOCKET &&
        record->vector_buffer_count == 0u &&
        native_error == ERROR_SUCCESS &&
        operation->kind == CFLOW_IO_NATIVE_UDP_RECV_FROM)
        operation->address_length = (size_t)record->address_length;

    record->phase = CFLOW_IOCP_RECORD_FREE;
    record->request_id = 0u;
    record->actor = NULL;
    record->operation = NULL;
    record->pipe_operation = NULL;
    record->file_operation = NULL;
    record->vector_buffer_count = 0u;
    record->native_handle = INVALID_HANDLE_VALUE;
    record->socket_value = INVALID_SOCKET;
    record->accepted_socket = INVALID_SOCKET;
    record->cancel_requested = false;
    --impl->active_requests;
    iocp_counter_increment(&impl->completed);
    if (cancelled)
        iocp_counter_increment(&impl->cancelled);
    turbo_mutex_unlock(&impl->gate);

    if (cancelled) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
    } else if (resource_kind == CFLOW_IOCP_RESOURCE_PIPE &&
               pipe_operation->kind == CFLOW_IO_NATIVE_PIPE_READ &&
               (native_error == ERROR_BROKEN_PIPE ||
                native_error == ERROR_HANDLE_EOF)) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK};
    } else if (resource_kind == CFLOW_IOCP_RESOURCE_FILE &&
               file_operation->kind == CFLOW_IO_NATIVE_FILE_READ_AT &&
               native_error == ERROR_HANDLE_EOF) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK};
    } else if (native_error != ERROR_SUCCESS) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, iocp_error(native_error)};
    } else if (resource_kind == CFLOW_IOCP_RESOURCE_PIPE) {
        completion = pipe_operation->kind == CFLOW_IO_NATIVE_PIPE_READ &&
                             bytes == 0u
                         ? (cflow_io_completion){
                               CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK}
                         : (cflow_io_completion){
                               CFLOW_IO_COMPLETION_OK, (size_t)bytes,
                               TURBO_OK};
    } else if (resource_kind == CFLOW_IOCP_RESOURCE_FILE) {
        completion = file_operation->kind == CFLOW_IO_NATIVE_FILE_READ_AT &&
                             bytes == 0u
                         ? (cflow_io_completion){
                               CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK}
                         : (cflow_io_completion){
                               CFLOW_IO_COMPLETION_OK, (size_t)bytes,
                               TURBO_OK};
    } else if (vector_buffer_count != 0u) {
        completion = vector_kind == CFLOW_IO_NATIVE_TCP_RECV_VECTOR &&
                             bytes == 0u
                         ? (cflow_io_completion){
                               CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK}
                         : (cflow_io_completion){
                               CFLOW_IO_COMPLETION_OK, (size_t)bytes,
                               TURBO_OK};
    } else if (operation->kind == CFLOW_IO_NATIVE_TCP_ACCEPT) {
        const int status = iocp_finish_accept(
            socket_value, operation, accepted_socket);
        if (status == TURBO_OK) {
            operation->result_socket = (uintptr_t)accepted_socket;
            completion = (cflow_io_completion){
                CFLOW_IO_COMPLETION_OK, 0u, TURBO_OK};
        } else {
            completion = (cflow_io_completion){
                CFLOW_IO_COMPLETION_FAILED, 0u, status};
        }
    } else if (operation->kind == CFLOW_IO_NATIVE_TCP_CONNECT) {
        const int status = iocp_finish_connect(socket_value);
        completion = status == TURBO_OK
                         ? (cflow_io_completion){
                               CFLOW_IO_COMPLETION_OK, 0u, TURBO_OK}
                         : (cflow_io_completion){
                               CFLOW_IO_COMPLETION_FAILED, 0u, status};
    } else if (operation->kind == CFLOW_IO_NATIVE_TCP_RECV && bytes == 0u) {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK};
    } else {
        completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_OK, (size_t)bytes, TURBO_OK};
    }
    delivery_status = cflow_io_actor_complete(
        actor, request_id, &completion);
    if (resource_kind == CFLOW_IOCP_RESOURCE_SOCKET &&
        vector_buffer_count == 0u &&
        operation->kind == CFLOW_IO_NATIVE_TCP_ACCEPT &&
        (completion.kind != CFLOW_IO_COMPLETION_OK ||
         delivery_status != CFLOW_IO_COMPLETE_ACCEPTED)) {
        if (accepted_socket != INVALID_SOCKET)
            (void)closesocket(accepted_socket);
        operation->result_socket = CFLOW_IO_NATIVE_INVALID_SOCKET;
        operation->address_length = 0u;
    }
}

static cflow_iocp_resource_record *iocp_find_resource_locked(
    cflow_iocp_impl *impl, HANDLE native_handle) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->resources[index].active &&
            impl->resources[index].native_handle == native_handle)
            return &impl->resources[index];
    }
    return NULL;
}

static cflow_iocp_resource_record *iocp_find_free_resource_locked(
    cflow_iocp_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (!impl->resources[index].active)
            return &impl->resources[index];
    }
    return NULL;
}

static int iocp_associate_resource(cflow_iocp_impl *impl,
                                   HANDLE native_handle) {
    cflow_iocp_resource_record *resource_record;
    HANDLE associated;
    DWORD association_error = ERROR_SUCCESS;

    turbo_mutex_lock(&impl->gate);
    for (;;) {
        resource_record = iocp_find_resource_locked(impl, native_handle);
        if (resource_record == NULL || !resource_record->associating)
            break;
        turbo_cond_wait(&impl->changed, &impl->gate);
    }
    if (resource_record != NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_OK;
    }
    resource_record = iocp_find_free_resource_locked(impl);
    if (resource_record == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    resource_record->native_handle = native_handle;
    resource_record->active = true;
    resource_record->associating = true;
    turbo_mutex_unlock(&impl->gate);

    associated = CreateIoCompletionPort(native_handle, impl->port, 0u, 0u);
    if (associated != impl->port)
        association_error = GetLastError();
    turbo_mutex_lock(&impl->gate);
    if (associated != impl->port) {
        resource_record->native_handle = INVALID_HANDLE_VALUE;
        resource_record->active = false;
        resource_record->associating = false;
        turbo_cond_broadcast(&impl->changed);
        turbo_mutex_unlock(&impl->gate);
        return iocp_error(association_error);
    }
    resource_record->associating = false;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static void iocp_worker(void *user) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)user;
    for (;;) {
        size_t batch_index;
        for (batch_index = 0u;
             batch_index < impl->completion_batch_capacity;
             ++batch_index) {
            DWORD bytes = 0u;
            ULONG_PTR completion_key = 0u;
            OVERLAPPED *overlapped = NULL;
            const DWORD timeout = batch_index == 0u ? INFINITE : 0u;
            const BOOL ok = GetQueuedCompletionStatus(
                impl->port, &bytes, &completion_key, &overlapped, timeout);
            const DWORD native_error = ok ? ERROR_SUCCESS : GetLastError();

            if (overlapped == NULL) {
                if (ok && completion_key == CFLOW_IOCP_STOP_KEY)
                    goto stopped;
                if (native_error == WAIT_TIMEOUT)
                    break;
                continue;
            }
            iocp_finish_record(impl, (cflow_iocp_record *)overlapped,
                               bytes, native_error);
        }
    }

stopped:
    turbo_mutex_lock(&impl->gate);
    impl->worker_running = false;
    turbo_mutex_unlock(&impl->gate);
}

static int iocp_submit_record(
    cflow_iocp_impl *impl, cflow_io_actor *actor,
    cflow_io_request_id request_id, cflow_iocp_resource_kind resource_kind,
    cflow_io_native_operation *operation,
    cflow_io_native_vector_operation *vector_operation,
    cflow_io_native_pipe_operation *pipe_operation,
    cflow_io_native_file_operation *file_operation,
    HANDLE native_handle, SOCKET socket_value) {
    cflow_iocp_record *record;
    int status;

    turbo_mutex_lock(&impl->gate);
    if (!impl->admission_open) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ESHUTDOWN;
    }
    record = iocp_find_free_locked(impl);
    if (record == NULL) {
        iocp_counter_increment(&impl->rejected_full);
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    memset(&record->overlapped, 0, sizeof(record->overlapped));
    record->phase = CFLOW_IOCP_RECORD_PENDING;
    record->request_id = request_id;
    record->actor = actor;
    record->operation = operation;
    record->vector_buffer_count = 0u;
    if (vector_operation != NULL) {
        record->vector_kind = vector_operation->kind;
        record->vector_buffer_count = (DWORD)vector_operation->buffer_count;
        for (DWORD index = 0u; index < record->vector_buffer_count; ++index) {
            record->vector_buffers[index].buf =
                (CHAR *)vector_operation->buffers[index].data;
            record->vector_buffers[index].len =
                (ULONG)vector_operation->buffers[index].length;
        }
    }
    record->pipe_operation = pipe_operation;
    record->file_operation = file_operation;
    record->native_handle = native_handle;
    record->socket_value = socket_value;
    record->accepted_socket = INVALID_SOCKET;
    record->cancel_requested = false;
    record->resource_kind = resource_kind;
    ++impl->active_requests;
    turbo_mutex_unlock(&impl->gate);

    status = iocp_associate_resource(impl, record->native_handle);
    if (status == TURBO_OK) {
        if (resource_kind == CFLOW_IOCP_RESOURCE_SOCKET)
            status = record->vector_buffer_count != 0u
                         ? iocp_begin_vector_operation(record)
                         : iocp_begin_operation(record);
        else if (resource_kind == CFLOW_IOCP_RESOURCE_PIPE)
            status = iocp_begin_pipe_operation(record);
        else
            status = iocp_begin_file_operation(impl, record);
    }
    if (status == TURBO_OK) {
        turbo_mutex_lock(&impl->gate);
        iocp_counter_increment(&impl->submitted);
        turbo_mutex_unlock(&impl->gate);
        return TURBO_OK;
    }

    turbo_mutex_lock(&impl->gate);
    record->phase = CFLOW_IOCP_RECORD_FREE;
    record->request_id = 0u;
    record->actor = NULL;
    record->operation = NULL;
    record->pipe_operation = NULL;
    record->file_operation = NULL;
    record->vector_buffer_count = 0u;
    record->native_handle = INVALID_HANDLE_VALUE;
    record->socket_value = INVALID_SOCKET;
    if (record->accepted_socket != INVALID_SOCKET)
        (void)closesocket(record->accepted_socket);
    record->accepted_socket = INVALID_SOCKET;
    --impl->active_requests;
    iocp_counter_increment(&impl->native_submit_errors);
    turbo_mutex_unlock(&impl->gate);
    return status;
}

static int iocp_submit(cflow_io_native_impl *base,
                       cflow_io_actor *actor,
                       cflow_io_request_id request_id,
                       cflow_io_native_operation *operation) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    return iocp_submit_record(
        impl, actor, request_id, CFLOW_IOCP_RESOURCE_SOCKET, operation, NULL,
        NULL, NULL, (HANDLE)(uintptr_t)operation->socket,
        (SOCKET)operation->socket);
}

static int iocp_submit_vector(
    cflow_io_native_impl *base, cflow_io_actor *actor,
    cflow_io_request_id request_id,
    cflow_io_native_vector_operation *operation) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    return iocp_submit_record(
        impl, actor, request_id, CFLOW_IOCP_RESOURCE_SOCKET, NULL, operation,
        NULL, NULL, (HANDLE)(uintptr_t)operation->socket,
        (SOCKET)operation->socket);
}

static int iocp_submit_pipe(cflow_io_native_impl *base,
                            cflow_io_actor *actor,
                            cflow_io_request_id request_id,
                            cflow_io_native_pipe_operation *operation) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    HANDLE handle = (HANDLE)operation->handle;
    DWORD pipe_flags = 0u;
    DWORD file_type;
    DWORD error;

    if ((operation->flags & CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE) == 0u)
        return TURBO_ENOTSUP;
    if (handle == NULL || handle == INVALID_HANDLE_VALUE)
        return TURBO_EINVAL;
    SetLastError(ERROR_SUCCESS);
    file_type = GetFileType(handle);
    error = GetLastError();
    if (file_type == FILE_TYPE_UNKNOWN && error != ERROR_SUCCESS)
        return iocp_error(error);
    if (file_type != FILE_TYPE_PIPE)
        return TURBO_ENOTSUP;
    if (!GetNamedPipeInfo(handle, &pipe_flags, NULL, NULL, NULL))
        return iocp_error(GetLastError());
    if ((pipe_flags & PIPE_TYPE_MESSAGE) != 0u)
        return TURBO_ENOTSUP;
    return iocp_submit_record(
        impl, actor, request_id, CFLOW_IOCP_RESOURCE_PIPE, NULL, NULL,
        operation, NULL, handle, INVALID_SOCKET);
}

static int iocp_submit_file(cflow_io_native_impl *base,
                            cflow_io_actor *actor,
                            cflow_io_request_id request_id,
                            cflow_io_native_file_operation *operation) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    HANDLE handle = (HANDLE)operation->handle;
    DWORD file_type;
    DWORD error;

    if (operation->kind == CFLOW_IO_NATIVE_FILE_FLUSH)
        return TURBO_ENOTSUP;
    if ((operation->flags & CFLOW_IO_NATIVE_FILE_ASYNC_CAPABLE) == 0u)
        return TURBO_ENOTSUP;
    if (handle == NULL || handle == INVALID_HANDLE_VALUE)
        return TURBO_EINVAL;
    SetLastError(ERROR_SUCCESS);
    file_type = GetFileType(handle);
    error = GetLastError();
    if (file_type == FILE_TYPE_UNKNOWN && error != ERROR_SUCCESS)
        return TURBO_EINVAL;
    if (file_type != FILE_TYPE_DISK)
        return TURBO_EINVAL;
    return iocp_submit_record(
        impl, actor, request_id, CFLOW_IOCP_RESOURCE_FILE, NULL, NULL,
        NULL, operation, handle, INVALID_SOCKET);
}

static int iocp_cancel(cflow_io_native_impl *base,
                       cflow_io_request_id request_id) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    cflow_iocp_record *record;
    HANDLE native_handle;
    OVERLAPPED *overlapped;
    DWORD error;

    turbo_mutex_lock(&impl->gate);
    record = iocp_find_request_locked(impl, request_id);
    if (record == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOENT;
    }
    record->cancel_requested = true;
    native_handle = record->native_handle;
    overlapped = &record->overlapped;
    turbo_mutex_unlock(&impl->gate);

    if (CancelIoEx(native_handle, overlapped))
        return TURBO_OK;
    error = GetLastError();
    if (error == ERROR_NOT_FOUND)
        return TURBO_OK;
    turbo_mutex_lock(&impl->gate);
    iocp_counter_increment(&impl->native_cancel_errors);
    turbo_mutex_unlock(&impl->gate);
    return iocp_error(error);
}

static bool iocp_get_stats(const cflow_io_native_impl *base,
                           cflow_io_native_backend_stats *out) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    turbo_mutex_lock(&impl->gate);
    *out = (cflow_io_native_backend_stats){
        impl->request_capacity,
        impl->active_requests,
        impl->submitted,
        impl->completed,
        impl->cancelled,
        impl->rejected_full,
        impl->stale_native_completions,
        impl->native_submit_errors,
        impl->native_cancel_errors,
        impl->admission_open,
        impl->worker_running,
        impl->shutdown_complete};
    turbo_mutex_unlock(&impl->gate);
    return true;
}

static int iocp_forget_resource(cflow_io_native_impl *base,
                                uintptr_t closed_identity) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    cflow_iocp_resource_record *resource_record;
    HANDLE native_handle = (HANDLE)closed_identity;
    size_t index;
    turbo_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase == CFLOW_IOCP_RECORD_PENDING &&
            impl->records[index].native_handle == native_handle) {
            turbo_mutex_unlock(&impl->gate);
            return TURBO_EBUSY;
        }
    }
    resource_record = iocp_find_resource_locked(impl, native_handle);
    if (resource_record == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOENT;
    }
    if (resource_record->associating) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    resource_record->native_handle = INVALID_HANDLE_VALUE;
    resource_record->active = false;
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static int iocp_forget_socket(cflow_io_native_impl *base,
                              uintptr_t closed_socket) {
    return iocp_forget_resource(base, closed_socket);
}

static int iocp_forget_pipe(cflow_io_native_impl *base,
                            uintptr_t closed_handle) {
    return iocp_forget_resource(base, closed_handle);
}

static int iocp_forget_file(cflow_io_native_impl *base,
                            uintptr_t closed_handle) {
    return iocp_forget_resource(base, closed_handle);
}

static int iocp_shutdown(cflow_io_native_impl *base) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    int join_status;
    turbo_mutex_lock(&impl->gate);
    if (impl->shutdown_complete) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EALREADY;
    }
    impl->admission_open = false;
    if (impl->active_requests != 0u) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->gate);

    if (!PostQueuedCompletionStatus(impl->port, 0u,
                                    CFLOW_IOCP_STOP_KEY, NULL))
        return iocp_error(GetLastError());
    join_status = turbo_thread_join(&impl->worker);
    if (join_status != TURBO_OK)
        return join_status;
    turbo_thread_destroy(&impl->worker);

    turbo_mutex_lock(&impl->gate);
    impl->shutdown_complete = true;
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static int iocp_destroy(cflow_io_native_impl *base) {
    cflow_iocp_impl *impl = (cflow_iocp_impl *)base;
    turbo_mutex_lock(&impl->gate);
    if (!impl->shutdown_complete) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->gate);
    (void)CloseHandle(impl->port);
    if (impl->winsock_started)
        (void)WSACleanup();
    turbo_cond_destroy(&impl->changed);
    turbo_mutex_destroy(&impl->gate);
    free(impl->resources);
    free(impl->records);
    free(impl);
    return TURBO_OK;
}

static const cflow_io_native_impl_ops iocp_ops = {
    .submit = iocp_submit,
    .submit_vector = iocp_submit_vector,
    .submit_pipe = iocp_submit_pipe,
    .submit_file = iocp_submit_file,
    .cancel = iocp_cancel,
    .get_stats = iocp_get_stats,
    .forget_socket = iocp_forget_socket,
    .forget_pipe = iocp_forget_pipe,
    .forget_file = iocp_forget_file,
    .shutdown = iocp_shutdown,
    .destroy = iocp_destroy};

int cflow_io_native_iocp_init(cflow_io_native_backend *backend,
                              const cflow_io_native_backend_config *config) {
    cflow_iocp_impl *impl;
    WSADATA winsock_data;
    int status;

    if (config->request_capacity > SIZE_MAX / sizeof(cflow_iocp_record) ||
        config->request_capacity >
            SIZE_MAX / sizeof(cflow_iocp_resource_record))
        return TURBO_ERANGE;
    impl = (cflow_iocp_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->records = (cflow_iocp_record *)calloc(
        config->request_capacity, sizeof(*impl->records));
    impl->resources = (cflow_iocp_resource_record *)calloc(
        config->request_capacity, sizeof(*impl->resources));
    if (impl->records == NULL || impl->resources == NULL) {
        free(impl->resources);
        free(impl->records);
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->base.ops = &iocp_ops;
    impl->base.kind = config->kind;
    impl->request_capacity = config->request_capacity;
    impl->completion_batch_capacity = config->completion_batch_capacity;
    impl->admission_open = true;
    turbo_mutex_init(&impl->gate);
    turbo_cond_init(&impl->changed);
    if (impl->gate == NULL || impl->changed == NULL) {
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->resources);
        free(impl->records);
        free(impl);
        return TURBO_ENOMEM;
    }

    status = WSAStartup(MAKEWORD(2, 2), &winsock_data);
    if (status != 0) {
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->resources);
        free(impl->records);
        free(impl);
        return -status;
    }
    impl->winsock_started = true;
    impl->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0u, 0u);
    if (impl->port == NULL) {
        status = iocp_error(GetLastError());
        (void)WSACleanup();
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->resources);
        free(impl->records);
        free(impl);
        return status;
    }
    status = turbo_thread_create(&impl->worker, iocp_worker, impl);
    if (status != TURBO_OK) {
        (void)CloseHandle(impl->port);
        (void)WSACleanup();
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->resources);
        free(impl->records);
        free(impl);
        return status;
    }
    impl->worker_running = true;
    backend->impl = impl;
    return TURBO_OK;
}
