#include "native_io_internal.h"

#include <turbo/error_codes.h>

#include <limits.h>

static turbo_io_impl *native_io_impl(turbo_io_backend *backend) {
  return backend != NULL ? (turbo_io_impl *)backend->impl : NULL;
}

static const turbo_io_impl *native_io_const_impl(const turbo_io_backend *backend) {
  return backend != NULL ? (const turbo_io_impl *)backend->impl : NULL;
}

turbo_io_model turbo_io_backend_model(turbo_io_backend_kind kind) {
  if (kind == TURBO_IO_BACKEND_IOCP || kind == TURBO_IO_BACKEND_IO_URING)
    return TURBO_IO_MODEL_COMPLETION;
  if (kind == TURBO_IO_BACKEND_EPOLL || kind == TURBO_IO_BACKEND_KQUEUE)
    return TURBO_IO_MODEL_READINESS;
  return TURBO_IO_MODEL_NONE;
}

bool turbo_io_backend_supported(turbo_io_backend_kind kind) {
  return turbo_io_backend_model(kind) != TURBO_IO_MODEL_NONE &&
         turbo_io_platform_backend_supported(kind);
}

bool turbo_io_endpoint_valid(turbo_io_endpoint endpoint) {
  return endpoint.slot != 0u && endpoint.generation != 0u;
}

bool turbo_io_request_valid(turbo_io_request request) {
  return request.slot != 0u && request.generation != 0u;
}

bool turbo_io_operation_valid(const turbo_io_operation *operation) {
  if (operation == NULL || !turbo_io_endpoint_valid(operation->endpoint) ||
      operation->buffer == NULL || operation->length == 0u ||
      operation->length > (size_t)UINT32_MAX)
    return false;
  if (operation->kind == TURBO_IO_TCP_RECV || operation->kind == TURBO_IO_TCP_SEND)
    return operation->address == NULL && operation->address_capacity == 0u &&
           operation->address_length == 0u;
  if (operation->kind == TURBO_IO_UDP_RECV_FROM)
    return operation->address != NULL && operation->address_capacity != 0u &&
           operation->address_capacity <= (size_t)INT_MAX && operation->address_length == 0u;
  if (operation->kind == TURBO_IO_UDP_SEND_TO)
    return operation->address != NULL && operation->address_length != 0u &&
           operation->address_length <= operation->address_capacity &&
           operation->address_length <= (size_t)INT_MAX;
  return false;
}

int turbo_io_backend_init(turbo_io_backend *backend, const turbo_io_backend_config *config) {
  if (backend == NULL) return TURBO_EINVAL;
  backend->impl = NULL;
  if (config == NULL || turbo_io_backend_model(config->kind) == TURBO_IO_MODEL_NONE ||
      config->endpoint_capacity == 0u || config->request_capacity == 0u ||
      config->completion_batch_capacity == 0u ||
      config->completion_batch_capacity > config->request_capacity)
    return TURBO_EINVAL;
  if (config->endpoint_capacity > UINT32_MAX || config->request_capacity > UINT32_MAX)
    return TURBO_ERANGE;
  if (!turbo_io_platform_backend_supported(config->kind)) return TURBO_ENOTSUP;
  return turbo_io_platform_backend_init(backend, config);
}

int turbo_io_backend_attach_socket(turbo_io_backend *backend, uintptr_t native_socket,
                                   turbo_io_endpoint *out_endpoint) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_endpoint != NULL) *out_endpoint = (turbo_io_endpoint){0};
  if (impl == NULL || impl->ops == NULL || impl->ops->attach_socket == NULL ||
      out_endpoint == NULL || native_socket == UINTPTR_MAX)
    return TURBO_EINVAL;
  return impl->ops->attach_socket(impl, native_socket, out_endpoint);
}

int turbo_io_backend_release_socket(turbo_io_backend *backend, turbo_io_endpoint endpoint) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->release_socket == NULL ||
      !turbo_io_endpoint_valid(endpoint))
    return TURBO_EINVAL;
  return impl->ops->release_socket(impl, endpoint);
}

int turbo_io_backend_submit(turbo_io_backend *backend, const turbo_io_operation *operation,
                            turbo_io_request *out_request) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_request != NULL) *out_request = (turbo_io_request){0};
  if (impl == NULL || impl->ops == NULL || impl->ops->submit == NULL ||
      !turbo_io_operation_valid(operation) || out_request == NULL)
    return TURBO_EINVAL;
  return impl->ops->submit(impl, operation, out_request);
}

int turbo_io_backend_cancel(turbo_io_backend *backend, turbo_io_request request) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->cancel == NULL ||
      !turbo_io_request_valid(request))
    return TURBO_EINVAL;
  return impl->ops->cancel(impl, request);
}

int turbo_io_backend_observe(turbo_io_backend *backend, turbo_io_completion *events,
                             size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (out_count != NULL) *out_count = 0u;
  if (impl == NULL || impl->ops == NULL || impl->ops->observe == NULL || events == NULL ||
      event_capacity == 0u || out_count == NULL)
    return TURBO_EINVAL;
  return impl->ops->observe(impl, events, event_capacity, timeout_ms, out_count);
}

int turbo_io_backend_close(turbo_io_backend *backend) {
  turbo_io_impl *impl = native_io_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->close == NULL) return TURBO_EINVAL;
  return impl->ops->close(impl);
}

int turbo_io_backend_destroy(turbo_io_backend *backend) {
  turbo_io_impl *impl = native_io_impl(backend);
  int status;
  if (impl == NULL || impl->ops == NULL || impl->ops->destroy == NULL) return TURBO_EINVAL;
  status = impl->ops->destroy(impl);
  if (status == TURBO_OK) backend->impl = NULL;
  return status;
}

bool turbo_io_backend_get_stats(const turbo_io_backend *backend,
                                turbo_io_backend_stats *out_stats) {
  const turbo_io_impl *impl = native_io_const_impl(backend);
  if (impl == NULL || impl->ops == NULL || impl->ops->get_stats == NULL || out_stats == NULL)
    return false;
  return impl->ops->get_stats(impl, out_stats);
}
