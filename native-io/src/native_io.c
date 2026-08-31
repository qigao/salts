#include "native_io_internal.h"

#include <turbo/error_codes.h>

#include <limits.h>

static turbo_io_impl *native_io_impl(native_io_backend *backend) {
  return backend != NULL ? (turbo_io_impl *)backend->impl : NULL;
}

static const turbo_io_impl *native_io_const_impl(const native_io_backend *backend) {
  return backend != NULL ? (const turbo_io_impl *)backend->impl : NULL;
}

native_io_model native_io_backend_kind_model(native_io_backend_kind kind) {
  if (kind == NATIVE_IO_BACKEND_IOCP || kind == NATIVE_IO_BACKEND_IO_URING)
    return NATIVE_IO_MODEL_COMPLETION;
  if (kind == NATIVE_IO_BACKEND_EPOLL || kind == NATIVE_IO_BACKEND_KQUEUE)
    return NATIVE_IO_MODEL_READINESS;
  return NATIVE_IO_MODEL_NONE;
}

bool native_io_backend_kind_supported(native_io_backend_kind kind) {
  return native_io_backend_kind_model(kind) != NATIVE_IO_MODEL_NONE &&
         native_io_platform_backend_supported(kind);
}

bool native_io_backend_kind_supports_pipe(native_io_backend_kind kind) {
  return native_io_backend_kind_supported(kind) && native_io_platform_pipe_supported(kind);
}

bool native_io_endpoint_valid(native_io_endpoint endpoint) {
  return endpoint.slot != 0u && endpoint.generation != 0u;
}

bool native_io_request_valid(native_io_request request) {
  return request.slot != 0u && request.generation != 0u;
}

bool native_io_operation_valid(const native_io_operation *operation) {
  if (operation == NULL || !native_io_endpoint_valid(operation->endpoint) ||
      operation->buffer == NULL || operation->length == 0u ||
      operation->length > (size_t)UINT32_MAX)
    return false;
  if (operation->kind == NATIVE_IO_OPERATION_TCP_RECV || operation->kind == NATIVE_IO_OPERATION_TCP_SEND ||
      operation->kind == NATIVE_IO_OPERATION_PIPE_READ || operation->kind == NATIVE_IO_OPERATION_PIPE_WRITE)
    return operation->address == NULL && operation->address_capacity == 0u &&
           operation->address_length == 0u;
  if (operation->kind == NATIVE_IO_OPERATION_UDP_RECV_FROM)
    return operation->address != NULL && operation->address_capacity != 0u &&
           operation->address_capacity <= (size_t)INT_MAX && operation->address_length == 0u;
  if (operation->kind == NATIVE_IO_OPERATION_UDP_SEND_TO)
    return operation->address != NULL && operation->address_length != 0u &&
           operation->address_length <= operation->address_capacity &&
           operation->address_length <= (size_t)INT_MAX;
  return false;
}

int native_io_backend_init(native_io_backend *backend, const native_io_backend_config *config) {
  if (backend == NULL) return TURBO_EINVAL;
  backend->impl = NULL;
  if (config == NULL || native_io_backend_kind_model(config->kind) == NATIVE_IO_MODEL_NONE ||
      config->endpoint_capacity == 0u || config->request_capacity == 0u ||
      config->completion_batch_capacity == 0u ||
      config->completion_batch_capacity > config->request_capacity)
    return TURBO_EINVAL;
  if (config->endpoint_capacity > UINT32_MAX || config->request_capacity > UINT32_MAX)
    return TURBO_ERANGE;
  if (!native_io_platform_backend_supported(config->kind)) return TURBO_ENOTSUP;
  return native_io_platform_backend_init(backend, config);
}

int native_io_backend_attach_socket(native_io_backend *backend, uintptr_t native_socket,
                                   native_io_endpoint *out_endpoint) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_endpoint != NULL) *out_endpoint = (native_io_endpoint){0};
  if (impl == NULL || impl->ops == NULL || impl->ops->attach_socket == NULL ||
      out_endpoint == NULL || native_socket == UINTPTR_MAX)
    return TURBO_EINVAL;
  return impl->ops->attach_socket(impl, native_socket, out_endpoint);
}

int native_io_backend_release_socket(native_io_backend *backend, native_io_endpoint endpoint) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->release_socket == NULL ||
      !native_io_endpoint_valid(endpoint))
    return TURBO_EINVAL;
  return impl->ops->release_socket(impl, endpoint);
}

int native_io_backend_attach_pipe(native_io_backend *backend, uintptr_t native_handle,
                                 uint32_t flags, native_io_endpoint *out_endpoint) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_endpoint != NULL) *out_endpoint = (native_io_endpoint){0};
  if (out_endpoint == NULL || native_handle == UINTPTR_MAX ||
      flags != NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE)
    return TURBO_EINVAL;
  if (impl == NULL || impl->ops == NULL) return TURBO_EINVAL;
  if (impl->ops->attach_pipe == NULL) return TURBO_ENOTSUP;
  return impl->ops->attach_pipe(impl, native_handle, flags, out_endpoint);
}

int native_io_backend_release_pipe(native_io_backend *backend, native_io_endpoint endpoint) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || !native_io_endpoint_valid(endpoint))
    return TURBO_EINVAL;
  if (impl->ops->release_pipe == NULL) return TURBO_ENOTSUP;
  return impl->ops->release_pipe(impl, endpoint);
}

int native_io_backend_submit(native_io_backend *backend, const native_io_operation *operation,
                            native_io_request *out_request) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_request != NULL) *out_request = (native_io_request){0};
  if (impl == NULL || impl->ops == NULL || impl->ops->submit == NULL ||
      !native_io_operation_valid(operation) || out_request == NULL)
    return TURBO_EINVAL;
  return impl->ops->submit(impl, operation, out_request);
}

int native_io_backend_cancel(native_io_backend *backend, native_io_request request) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->cancel == NULL ||
      !native_io_request_valid(request))
    return TURBO_EINVAL;
  return impl->ops->cancel(impl, request);
}

int native_io_backend_observe(native_io_backend *backend, native_io_completion *events,
                             size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_count != NULL) *out_count = 0u;
  if (impl == NULL || impl->ops == NULL || impl->ops->observe == NULL || events == NULL ||
      event_capacity == 0u || out_count == NULL)
    return TURBO_EINVAL;
  return impl->ops->observe(impl, events, event_capacity, timeout_ms, out_count);
}

int native_io_backend_wake(native_io_backend *backend) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->wake == NULL) return TURBO_EINVAL;
  return impl->ops->wake(impl);
}

int native_io_backend_close(native_io_backend *backend) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->close == NULL) return TURBO_EINVAL;
  return impl->ops->close(impl);
}

int native_io_backend_destroy(native_io_backend *backend) {
  turbo_io_impl *impl = native_io_impl(backend);
  int status;
  if (impl == NULL || impl->ops == NULL || impl->ops->destroy == NULL) return TURBO_EINVAL;
  status = impl->ops->destroy(impl);
  if (status == TURBO_OK) backend->impl = NULL;
  return status;
}

bool native_io_backend_get_stats(const native_io_backend *backend,
                                native_io_backend_stats *out_stats) {
  const turbo_io_impl *impl = native_io_const_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->get_stats == NULL || out_stats == NULL)
    return false;
  return impl->ops->get_stats(impl, out_stats);
}
