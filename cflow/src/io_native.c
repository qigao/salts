#include "io_native_internal.h"

#include <turbo/error_codes.h>

#include <limits.h>

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
#if defined(CFLOW_HAS_NATIVE_EPOLL) || defined(CFLOW_HAS_NATIVE_KQUEUE)
        case CFLOW_IO_NATIVE_EPOLL:
        case CFLOW_IO_NATIVE_KQUEUE:
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

cflow_io_backend_ops cflow_io_native_backend_actor_ops(void) {
    cflow_io_backend_ops ops = {native_actor_submit, native_actor_cancel};
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
