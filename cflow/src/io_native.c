#include "io_native_internal.h"

#include <turbo/error_codes.h>

#include <limits.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

static cflow_io_native_impl *native_impl(cflow_io_native_backend *backend) {
    return backend != NULL ? (cflow_io_native_impl *)backend->impl : NULL;
}

static const cflow_io_native_impl *native_const_impl(
    const cflow_io_native_backend *backend) {
    return backend != NULL ? (const cflow_io_native_impl *)backend->impl : NULL;
}

bool cflow_io_native_operation_valid(const cflow_io_native_operation *operation) {
    if (operation == NULL || operation->kind > CFLOW_IO_NATIVE_TCP_CONNECT ||
        operation->socket == UINTPTR_MAX ||
        operation->length > UINT32_MAX ||
        (operation->length != 0u && operation->buffer == NULL))
        return false;

    switch (operation->kind) {
        case CFLOW_IO_NATIVE_TCP_RECV:
        case CFLOW_IO_NATIVE_TCP_SEND:
            return operation->address == NULL &&
                   operation->address_capacity == 0u &&
                   operation->address_length == 0u;
        case CFLOW_IO_NATIVE_UDP_RECV_FROM:
            return operation->address != NULL &&
                   operation->address_capacity != 0u &&
                   operation->address_capacity <= UINT32_MAX &&
                   operation->address_length == 0u;
        case CFLOW_IO_NATIVE_UDP_SEND_TO:
            return operation->address != NULL &&
                   operation->address_length != 0u &&
                   operation->address_length <= operation->address_capacity &&
                   operation->address_length <= UINT32_MAX;
        case CFLOW_IO_NATIVE_TCP_ACCEPT:
            return operation->buffer == NULL && operation->length == 0u &&
                   operation->result_socket ==
                       CFLOW_IO_NATIVE_INVALID_SOCKET &&
                   operation->address_length == 0u &&
                   ((operation->address == NULL &&
                     operation->address_capacity == 0u) ||
                    (operation->address != NULL &&
                     operation->address_capacity != 0u &&
                     operation->address_capacity <= UINT32_MAX));
        case CFLOW_IO_NATIVE_TCP_CONNECT:
            return operation->buffer == NULL && operation->length == 0u &&
                   operation->address != NULL &&
                   operation->address_length != 0u &&
                   operation->address_length <=
                       operation->address_capacity &&
                   operation->address_length <= UINT32_MAX;
    }
    return false;
}

bool cflow_io_native_vector_operation_valid(
    const cflow_io_native_vector_operation *operation) {
    size_t total = 0u;
    if (operation == NULL ||
        (operation->kind != CFLOW_IO_NATIVE_TCP_RECV_VECTOR &&
         operation->kind != CFLOW_IO_NATIVE_TCP_SEND_VECTOR) ||
        operation->socket == UINTPTR_MAX || operation->buffers == NULL ||
        operation->buffer_count == 0u ||
        operation->buffer_count > CFLOW_IO_NATIVE_VECTOR_MAX)
        return false;
    for (size_t index = 0u; index < operation->buffer_count; ++index) {
        const cflow_io_native_buffer_span *span = &operation->buffers[index];
        if (span->data == NULL || span->length == 0u ||
            span->length > (size_t)UINT32_MAX - total)
            return false;
        total += span->length;
    }
    return true;
}

bool cflow_io_native_pipe_operation_valid(
    const cflow_io_native_pipe_operation *operation) {
    const uint32_t known_flags = CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE;
    return operation != NULL &&
           operation->kind <= CFLOW_IO_NATIVE_PIPE_WRITE &&
           operation->handle != UINTPTR_MAX && operation->buffer != NULL &&
           operation->length != 0u && operation->length <= UINT32_MAX &&
           (operation->flags & ~known_flags) == 0u;
}

bool cflow_io_native_file_operation_valid(
    const cflow_io_native_file_operation *operation) {
    const uint32_t known_flags = CFLOW_IO_NATIVE_FILE_ASYNC_CAPABLE;
    if (operation == NULL || operation->handle == UINTPTR_MAX ||
        (operation->flags & ~known_flags) != 0u)
        return false;

    switch (operation->kind) {
        case CFLOW_IO_NATIVE_FILE_READ_AT:
        case CFLOW_IO_NATIVE_FILE_WRITE_AT:
            return operation->buffer != NULL && operation->length != 0u &&
                   operation->length <= UINT32_MAX &&
                   operation->offset <= (uint64_t)INT64_MAX &&
                   operation->length <=
                       (uint64_t)INT64_MAX - operation->offset;
        case CFLOW_IO_NATIVE_FILE_FLUSH:
            return operation->buffer == NULL && operation->length == 0u &&
                   operation->offset == 0u;
    }
    return false;
}

bool cflow_io_native_backend_supported(cflow_io_native_backend_kind kind) {
    switch (kind) {
#if defined(CFLOW_HAS_NATIVE_EPOLL)
        case CFLOW_IO_NATIVE_EPOLL:
            return true;
#endif
#if defined(CFLOW_HAS_NATIVE_KQUEUE)
        case CFLOW_IO_NATIVE_KQUEUE:
            return true;
#endif
#if defined(CFLOW_HAS_NATIVE_IOCP)
        case CFLOW_IO_NATIVE_IOCP:
            return true;
#endif
#if defined(CFLOW_HAS_NATIVE_IO_URING)
        case CFLOW_IO_NATIVE_IO_URING:
            return true;
#endif
#if defined(CFLOW_HAS_NATIVE_POLL)
        case CFLOW_IO_NATIVE_POLL:
            return true;
#endif
        default:
            return false;
    }
}

bool cflow_io_native_backend_communication_model(
    cflow_io_native_backend_kind kind,
    cflow_io_communication_model *out) {
    cflow_io_communication_model model;

    if (out == NULL)
        return false;
    switch (kind) {
        case CFLOW_IO_NATIVE_EPOLL:
        case CFLOW_IO_NATIVE_KQUEUE:
        case CFLOW_IO_NATIVE_POLL:
            model = CFLOW_IO_COMMUNICATION_READINESS;
            break;
        case CFLOW_IO_NATIVE_IOCP:
        case CFLOW_IO_NATIVE_IO_URING:
            model = CFLOW_IO_COMMUNICATION_COMPLETION;
            break;
        default:
            return false;
    }
    *out = model;
    return true;
}

bool cflow_io_native_backend_pipe_supported(
    cflow_io_native_backend_kind kind) {
    return cflow_io_native_backend_supported(kind);
}

bool cflow_io_native_backend_vector_operation_supported(
    cflow_io_native_backend_kind kind,
    cflow_io_native_vector_operation_kind operation_kind) {
    if (operation_kind != CFLOW_IO_NATIVE_TCP_RECV_VECTOR &&
        operation_kind != CFLOW_IO_NATIVE_TCP_SEND_VECTOR)
        return false;
    if (!cflow_io_native_backend_supported(kind))
        return false;
#if !defined(_WIN32)
    {
        const long host_limit = sysconf(_SC_IOV_MAX);
        return host_limit >= (long)CFLOW_IO_NATIVE_VECTOR_MAX;
    }
#else
    return true;
#endif
}

bool cflow_io_native_backend_file_operation_supported(
    cflow_io_native_backend_kind kind,
    cflow_io_native_file_operation_kind operation_kind) {
    switch (operation_kind) {
        case CFLOW_IO_NATIVE_FILE_READ_AT:
        case CFLOW_IO_NATIVE_FILE_WRITE_AT:
        case CFLOW_IO_NATIVE_FILE_FLUSH:
            break;
        default:
            return false;
    }
    switch (kind) {
#if defined(CFLOW_HAS_NATIVE_IOCP)
        case CFLOW_IO_NATIVE_IOCP:
            return operation_kind != CFLOW_IO_NATIVE_FILE_FLUSH;
#endif
#if defined(CFLOW_HAS_NATIVE_IO_URING)
        case CFLOW_IO_NATIVE_IO_URING:
            return true;
#endif
        default:
            return false;
    }
}

int cflow_io_native_backend_init(
    cflow_io_native_backend *backend,
    const cflow_io_native_backend_config *config) {
    if (backend == NULL)
        return TURBO_EINVAL;
    backend->impl = NULL;
    if (config == NULL || config->request_capacity == 0u ||
        config->completion_batch_capacity == 0u ||
        config->completion_batch_capacity > config->request_capacity ||
        config->request_capacity > UINT32_MAX)
        return TURBO_EINVAL;
    if (!cflow_io_native_backend_supported(config->kind))
        return TURBO_ENOTSUP;

    switch (config->kind) {
#if defined(CFLOW_HAS_NATIVE_EPOLL) || defined(CFLOW_HAS_NATIVE_KQUEUE) || \
    defined(CFLOW_HAS_NATIVE_POLL)
        case CFLOW_IO_NATIVE_EPOLL:
        case CFLOW_IO_NATIVE_KQUEUE:
        case CFLOW_IO_NATIVE_POLL:
            return cflow_io_native_readiness_init(backend, config);
#endif
#if defined(CFLOW_HAS_NATIVE_IOCP)
        case CFLOW_IO_NATIVE_IOCP:
            return cflow_io_native_iocp_init(backend, config);
#endif
#if defined(CFLOW_HAS_NATIVE_IO_URING)
        case CFLOW_IO_NATIVE_IO_URING:
            return cflow_io_native_io_uring_init(backend, config);
#endif
        default:
            return TURBO_ENOTSUP;
    }
}

static int native_actor_submit(void *backend_user,
                               cflow_io_actor *actor,
                               cflow_io_request_id request_id,
                               cflow_io_lease_id lease_id,
                               void *operation_user) {
    cflow_io_native_backend *backend = (cflow_io_native_backend *)backend_user;
    cflow_io_native_impl *impl = native_impl(backend);
    cflow_io_native_operation *operation =
        (cflow_io_native_operation *)operation_user;
    (void)lease_id;
    if (impl == NULL || impl->ops == NULL || impl->ops->submit == NULL ||
        actor == NULL || request_id == 0u ||
        !cflow_io_native_operation_valid(operation))
        return TURBO_EINVAL;
    return impl->ops->submit(impl, actor, request_id, operation);
}

static int native_actor_cancel(void *backend_user,
                               cflow_io_request_id request_id) {
    cflow_io_native_backend *backend = (cflow_io_native_backend *)backend_user;
    cflow_io_native_impl *impl = native_impl(backend);
    if (impl == NULL || impl->ops == NULL || impl->ops->cancel == NULL ||
        request_id == 0u)
        return TURBO_EINVAL;
    return impl->ops->cancel(impl, request_id);
}

static int native_vector_actor_submit(void *backend_user,
                                      cflow_io_actor *actor,
                                      cflow_io_request_id request_id,
                                      cflow_io_lease_id lease_id,
                                      void *operation_user) {
    cflow_io_native_backend *backend = (cflow_io_native_backend *)backend_user;
    cflow_io_native_impl *impl = native_impl(backend);
    cflow_io_native_vector_operation *operation =
        (cflow_io_native_vector_operation *)operation_user;
    (void)lease_id;
    if (impl == NULL || impl->ops == NULL || actor == NULL ||
        request_id == 0u ||
        !cflow_io_native_vector_operation_valid(operation))
        return TURBO_EINVAL;
    if (impl->ops->submit_vector == NULL ||
        !cflow_io_native_backend_vector_operation_supported(
            impl->kind, operation->kind))
        return TURBO_ENOTSUP;
    return impl->ops->submit_vector(impl, actor, request_id, operation);
}

static int native_pipe_actor_submit(void *backend_user,
                                    cflow_io_actor *actor,
                                    cflow_io_request_id request_id,
                                    cflow_io_lease_id lease_id,
                                    void *operation_user) {
    cflow_io_native_backend *backend = (cflow_io_native_backend *)backend_user;
    cflow_io_native_impl *impl = native_impl(backend);
    cflow_io_native_pipe_operation *operation =
        (cflow_io_native_pipe_operation *)operation_user;
    (void)lease_id;
    if (impl == NULL || impl->ops == NULL || actor == NULL ||
        request_id == 0u ||
        !cflow_io_native_pipe_operation_valid(operation))
        return TURBO_EINVAL;
    if (impl->ops->submit_pipe == NULL)
        return TURBO_ENOTSUP;
    return impl->ops->submit_pipe(impl, actor, request_id, operation);
}

static int native_file_actor_submit(void *backend_user,
                                    cflow_io_actor *actor,
                                    cflow_io_request_id request_id,
                                    cflow_io_lease_id lease_id,
                                    void *operation_user) {
    cflow_io_native_backend *backend = (cflow_io_native_backend *)backend_user;
    cflow_io_native_impl *impl = native_impl(backend);
    cflow_io_native_file_operation *operation =
        (cflow_io_native_file_operation *)operation_user;
    (void)lease_id;
    if (impl == NULL || impl->ops == NULL || actor == NULL ||
        request_id == 0u ||
        !cflow_io_native_file_operation_valid(operation))
        return TURBO_EINVAL;
    if (impl->ops->submit_file == NULL ||
        !cflow_io_native_backend_file_operation_supported(impl->kind,
                                                          operation->kind))
        return TURBO_ENOTSUP;
    return impl->ops->submit_file(impl, actor, request_id, operation);
}

cflow_io_backend_ops cflow_io_native_backend_actor_ops(void) {
    cflow_io_backend_ops ops = {native_actor_submit, native_actor_cancel};
    return ops;
}

cflow_io_backend_ops cflow_io_native_backend_vector_actor_ops(void) {
    cflow_io_backend_ops ops = {native_vector_actor_submit,
                                native_actor_cancel};
    return ops;
}

cflow_io_backend_ops cflow_io_native_backend_pipe_actor_ops(void) {
    cflow_io_backend_ops ops = {native_pipe_actor_submit,
                                native_actor_cancel};
    return ops;
}

cflow_io_backend_ops cflow_io_native_backend_file_actor_ops(void) {
    cflow_io_backend_ops ops = {native_file_actor_submit,
                                native_actor_cancel};
    return ops;
}

bool cflow_io_native_backend_get_stats(
    const cflow_io_native_backend *backend,
    cflow_io_native_backend_stats *out) {
    const cflow_io_native_impl *impl = native_const_impl(backend);
    return impl != NULL && impl->ops != NULL && impl->ops->get_stats != NULL &&
           out != NULL && impl->ops->get_stats(impl, out);
}

int cflow_io_native_backend_forget_socket(
    cflow_io_native_backend *backend, uintptr_t closed_socket) {
    cflow_io_native_impl *impl = native_impl(backend);
    if (impl == NULL || impl->ops == NULL ||
        impl->ops->forget_socket == NULL || closed_socket == UINTPTR_MAX)
        return TURBO_EINVAL;
    return impl->ops->forget_socket(impl, closed_socket);
}

int cflow_io_native_backend_forget_pipe(
    cflow_io_native_backend *backend, uintptr_t closed_handle) {
    cflow_io_native_impl *impl = native_impl(backend);
    if (impl == NULL || impl->ops == NULL || closed_handle == UINTPTR_MAX)
        return TURBO_EINVAL;
    if (impl->ops->forget_pipe == NULL)
        return TURBO_ENOTSUP;
    return impl->ops->forget_pipe(impl, closed_handle);
}

int cflow_io_native_backend_forget_file(
    cflow_io_native_backend *backend, uintptr_t closed_handle) {
    cflow_io_native_impl *impl = native_impl(backend);
    if (impl == NULL || impl->ops == NULL || closed_handle == UINTPTR_MAX)
        return TURBO_EINVAL;
    if (impl->ops->forget_file == NULL)
        return TURBO_ENOTSUP;
    return impl->ops->forget_file(impl, closed_handle);
}

int cflow_io_native_backend_shutdown(cflow_io_native_backend *backend) {
    cflow_io_native_impl *impl = native_impl(backend);
    if (impl == NULL || impl->ops == NULL || impl->ops->shutdown == NULL)
        return TURBO_EINVAL;
    return impl->ops->shutdown(impl);
}

int cflow_io_native_backend_destroy(cflow_io_native_backend *backend) {
    cflow_io_native_impl *impl = native_impl(backend);
    int status;
    if (impl == NULL || impl->ops == NULL || impl->ops->destroy == NULL)
        return TURBO_EINVAL;
    status = impl->ops->destroy(impl);
    if (status == TURBO_OK)
        backend->impl = NULL;
    return status;
}
