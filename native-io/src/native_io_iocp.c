#include "native_io_internal.h"

#include <turbo/error_codes.h>

#if defined(interface)
  #undef interface
#endif
#include <windows.h>
#include <winsock2.h>

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum turbo_iocp_record_phase {
  TURBO_IOCP_RECORD_FREE = 0,
  TURBO_IOCP_RECORD_PENDING
} turbo_iocp_record_phase;

typedef struct turbo_iocp_endpoint_record {
  SOCKET socket_value;
  uint32_t generation;
  size_t active_requests;
  bool active;
} turbo_iocp_endpoint_record;

typedef struct turbo_iocp_request_record {
  OVERLAPPED overlapped;
  WSABUF buffer;
  turbo_iocp_record_phase phase;
  turbo_io_request request;
  turbo_io_endpoint endpoint;
  turbo_io_operation_kind operation_kind;
  SOCKET socket_value;
  DWORD flags;
  void *address;
  int address_length;
  uintptr_t user_data;
} turbo_iocp_request_record;

_Static_assert(offsetof(turbo_iocp_request_record, overlapped) == 0u,
               "OVERLAPPED must remain the stable request prefix");

typedef struct turbo_iocp_impl {
  turbo_io_impl base;
  HANDLE port;
  turbo_iocp_endpoint_record *endpoints;
  turbo_iocp_request_record *requests;
  uint32_t *free_endpoints;
  uint32_t *free_requests;
  size_t endpoint_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t free_endpoint_count;
  size_t free_request_count;
  size_t endpoint_count;
  size_t active_requests;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t failed;
  uint64_t rejected_full;
  uint64_t native_submit_errors;
  uint64_t native_cancel_errors;
  bool admission_open;
  bool winsock_started;
} turbo_iocp_impl;

static void iocp_counter_increment(uint64_t *counter) {
  if (*counter != UINT64_MAX) ++*counter;
}

static uint32_t iocp_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static int iocp_native_error(DWORD error) {
  if (error == ERROR_SUCCESS || error > (DWORD)INT_MAX) return TURBO_EIO;
  return -(int)error;
}

static turbo_iocp_endpoint_record *iocp_endpoint(turbo_iocp_impl *impl,
                                                 turbo_io_endpoint endpoint) {
  turbo_iocp_endpoint_record *record;
  if (!turbo_io_endpoint_valid(endpoint) || endpoint.slot > impl->endpoint_capacity) return NULL;
  record = &impl->endpoints[endpoint.slot - 1u];
  return record->active && record->generation == endpoint.generation ? record : NULL;
}

static turbo_iocp_request_record *iocp_request(turbo_iocp_impl *impl, turbo_io_request request) {
  turbo_iocp_request_record *record;
  if (!turbo_io_request_valid(request) || request.slot > impl->request_capacity) return NULL;
  record = &impl->requests[request.slot - 1u];
  return record->phase == TURBO_IOCP_RECORD_PENDING &&
                 record->request.generation == request.generation
             ? record
             : NULL;
}

static int iocp_attach_socket(turbo_io_impl *base, uintptr_t native_socket,
                              turbo_io_endpoint *out_endpoint) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  uint32_t index;
  turbo_iocp_endpoint_record *endpoint;
  HANDLE associated;
  size_t cursor;

  if (!impl->admission_open) return TURBO_ESHUTDOWN;
  for (cursor = 0u; cursor < impl->endpoint_capacity; ++cursor) {
    if (impl->endpoints[cursor].active &&
        impl->endpoints[cursor].socket_value == (SOCKET)native_socket)
      return TURBO_EALREADY;
  }
  if (impl->free_endpoint_count == 0u) return TURBO_ENOBUFS;

  index = impl->free_endpoints[impl->free_endpoint_count - 1u];
  endpoint = &impl->endpoints[index];
  associated =
      CreateIoCompletionPort((HANDLE)native_socket, impl->port, (ULONG_PTR)(index + 1u), 0u);
  if (associated != impl->port) return iocp_native_error(GetLastError());

  --impl->free_endpoint_count;
  endpoint->generation = iocp_next_generation(endpoint->generation);
  endpoint->socket_value = (SOCKET)native_socket;
  endpoint->active_requests = 0u;
  endpoint->active = true;
  ++impl->endpoint_count;
  *out_endpoint = (turbo_io_endpoint){index + 1u, endpoint->generation};
  return TURBO_OK;
}

static int iocp_release_socket(turbo_io_impl *base, turbo_io_endpoint endpoint_handle) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  turbo_iocp_endpoint_record *endpoint = iocp_endpoint(impl, endpoint_handle);
  uint32_t index;
  if (endpoint == NULL) return TURBO_ENOENT;
  if (endpoint->active_requests != 0u) return TURBO_EBUSY;

  index = endpoint_handle.slot - 1u;
  endpoint->socket_value = INVALID_SOCKET;
  endpoint->active = false;
  impl->free_endpoints[impl->free_endpoint_count] = index;
  ++impl->free_endpoint_count;
  --impl->endpoint_count;
  return TURBO_OK;
}

static void iocp_release_request(turbo_iocp_impl *impl, turbo_iocp_request_record *request,
                                 uint32_t index) {
  turbo_iocp_endpoint_record *endpoint = iocp_endpoint(impl, request->endpoint);
  if (endpoint != NULL && endpoint->active_requests != 0u) --endpoint->active_requests;
  request->phase = TURBO_IOCP_RECORD_FREE;
  request->socket_value = INVALID_SOCKET;
  request->flags = 0u;
  request->address = NULL;
  request->address_length = 0;
  request->user_data = 0u;
  impl->free_requests[impl->free_request_count] = index;
  ++impl->free_request_count;
  --impl->active_requests;
}

static int iocp_submit(turbo_io_impl *base, const turbo_io_operation *operation,
                       turbo_io_request *out_request) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  turbo_iocp_endpoint_record *endpoint;
  turbo_iocp_request_record *request;
  uint32_t index;
  uint32_t generation;
  DWORD immediate_bytes = 0u;
  int native_status;
  DWORD native_error;

  if (!impl->admission_open) return TURBO_ESHUTDOWN;
  endpoint = iocp_endpoint(impl, operation->endpoint);
  if (endpoint == NULL) return TURBO_ENOENT;
  if (impl->free_request_count == 0u) {
    iocp_counter_increment(&impl->rejected_full);
    return TURBO_ENOBUFS;
  }

  index = impl->free_requests[impl->free_request_count - 1u];
  request = &impl->requests[index];
  generation = iocp_next_generation(request->request.generation);
  memset(&request->overlapped, 0, sizeof(request->overlapped));
  request->buffer.buf = (CHAR *)operation->buffer;
  request->buffer.len = (ULONG)operation->length;
  request->phase = TURBO_IOCP_RECORD_PENDING;
  request->request = (turbo_io_request){index + 1u, generation};
  request->endpoint = operation->endpoint;
  request->operation_kind = operation->kind;
  request->socket_value = endpoint->socket_value;
  request->flags = 0u;
  request->address = operation->address;
  request->address_length = operation->kind == TURBO_IO_UDP_RECV_FROM
                                ? (int)operation->address_capacity
                                : (int)operation->address_length;
  request->user_data = operation->user_data;
  --impl->free_request_count;
  ++endpoint->active_requests;
  ++impl->active_requests;

  if (operation->kind == TURBO_IO_TCP_RECV) {
    native_status = WSARecv(request->socket_value, &request->buffer, 1u, &immediate_bytes,
                            &request->flags, &request->overlapped, NULL);
  } else if (operation->kind == TURBO_IO_TCP_SEND) {
    native_status = WSASend(request->socket_value, &request->buffer, 1u, &immediate_bytes, 0u,
                            &request->overlapped, NULL);
  } else if (operation->kind == TURBO_IO_UDP_RECV_FROM) {
    native_status = WSARecvFrom(request->socket_value, &request->buffer, 1u, &immediate_bytes,
                                &request->flags, (SOCKADDR *)request->address,
                                &request->address_length, &request->overlapped, NULL);
  } else {
    native_status = WSASendTo(request->socket_value, &request->buffer, 1u, &immediate_bytes, 0u,
                              (const SOCKADDR *)request->address, request->address_length,
                              &request->overlapped, NULL);
  }
  if (native_status == 0) {
    iocp_counter_increment(&impl->submitted);
    *out_request = request->request;
    return TURBO_OK;
  }
  native_error = (DWORD)WSAGetLastError();
  if (native_error == WSA_IO_PENDING) {
    iocp_counter_increment(&impl->submitted);
    *out_request = request->request;
    return TURBO_OK;
  }
  iocp_release_request(impl, request, index);
  iocp_counter_increment(&impl->native_submit_errors);
  return iocp_native_error(native_error);
}

static int iocp_cancel(turbo_io_impl *base, turbo_io_request request_handle) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  turbo_iocp_request_record *request = iocp_request(impl, request_handle);
  DWORD error;
  if (request == NULL) return TURBO_ENOENT;
  if (CancelIoEx((HANDLE)request->socket_value, &request->overlapped)) return TURBO_OK;
  error = GetLastError();
  if (error == ERROR_NOT_FOUND) return TURBO_EALREADY;
  iocp_counter_increment(&impl->native_cancel_errors);
  return iocp_native_error(error);
}

static turbo_iocp_request_record *
iocp_completed_request(turbo_iocp_impl *impl, OVERLAPPED *overlapped, uint32_t *out_index) {
  const uintptr_t base = (uintptr_t)impl->requests;
  const uintptr_t value = (uintptr_t)overlapped;
  const size_t bytes = impl->request_capacity * sizeof(*impl->requests);
  uintptr_t offset;
  if (value < base || value - base >= bytes) return NULL;
  offset = value - base;
  if (offset % sizeof(*impl->requests) != 0u) return NULL;
  *out_index = (uint32_t)(offset / sizeof(*impl->requests));
  if (impl->requests[*out_index].phase != TURBO_IOCP_RECORD_PENDING) return NULL;
  return &impl->requests[*out_index];
}

static void iocp_make_completion(turbo_iocp_impl *impl, turbo_iocp_request_record *request,
                                 uint32_t request_index, DWORD bytes, DWORD native_error,
                                 turbo_io_completion *event) {
  *event = (turbo_io_completion){
      request->request, request->endpoint,      TURBO_IO_COMPLETION_OK, (size_t)bytes,
      TURBO_OK,         (uint32_t)native_error, request->user_data,     0u};

  if (native_error == ERROR_OPERATION_ABORTED) {
    event->kind = TURBO_IO_COMPLETION_CANCELLED;
    event->bytes = 0u;
    event->status = TURBO_ECANCELED;
    iocp_counter_increment(&impl->cancelled);
  } else if (native_error != ERROR_SUCCESS) {
    event->kind = TURBO_IO_COMPLETION_FAILED;
    event->bytes = 0u;
    event->status = iocp_native_error(native_error);
    iocp_counter_increment(&impl->failed);
  } else if (request->operation_kind == TURBO_IO_TCP_RECV && bytes == 0u) {
    event->kind = TURBO_IO_COMPLETION_EOF;
    event->status = TURBO_EOF;
  } else if (request->operation_kind == TURBO_IO_UDP_RECV_FROM) {
    event->address_length = (size_t)request->address_length;
  }

  iocp_counter_increment(&impl->completed);
  iocp_release_request(impl, request, request_index);
}

static int iocp_observe(turbo_io_impl *base, turbo_io_completion *events, size_t event_capacity,
                        uint32_t timeout_ms, size_t *out_count) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  const size_t limit = event_capacity < impl->completion_batch_capacity
                           ? event_capacity
                           : impl->completion_batch_capacity;
  size_t count;

  for (count = 0u; count < limit; ++count) {
    DWORD bytes = 0u;
    ULONG_PTR completion_key = 0u;
    OVERLAPPED *overlapped = NULL;
    const DWORD wait_ms = count == 0u ? timeout_ms : 0u;
    const BOOL ok =
        GetQueuedCompletionStatus(impl->port, &bytes, &completion_key, &overlapped, wait_ms);
    const DWORD native_error = ok ? ERROR_SUCCESS : GetLastError();
    turbo_iocp_request_record *request;
    uint32_t request_index = 0u;
    (void)completion_key;

    if (overlapped == NULL) {
      *out_count = count;
      if (native_error == WAIT_TIMEOUT) return count == 0u ? TURBO_ETIMEDOUT : TURBO_OK;
      return iocp_native_error(native_error);
    }
    request = iocp_completed_request(impl, overlapped, &request_index);
    if (request == NULL) {
      *out_count = count;
      return TURBO_EPROTO;
    }
    iocp_make_completion(impl, request, request_index, bytes, native_error, &events[count]);
  }
  *out_count = count;
  return TURBO_OK;
}

static int iocp_close(turbo_io_impl *base) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  if (!impl->admission_open) return TURBO_EALREADY;
  impl->admission_open = false;
  return TURBO_OK;
}

static int iocp_destroy(turbo_io_impl *base) {
  turbo_iocp_impl *impl = (turbo_iocp_impl *)base;
  int cleanup_status;
  if (impl->admission_open || impl->active_requests != 0u || impl->endpoint_count != 0u)
    return TURBO_EBUSY;
  if (impl->port != NULL) {
    if (!CloseHandle(impl->port)) return iocp_native_error(GetLastError());
    impl->port = NULL;
  }
  if (impl->winsock_started) {
    cleanup_status = WSACleanup();
    if (cleanup_status != 0) return iocp_native_error((DWORD)WSAGetLastError());
    impl->winsock_started = false;
  }
  free(impl->free_requests);
  free(impl->free_endpoints);
  free(impl->requests);
  free(impl->endpoints);
  free(impl);
  return TURBO_OK;
}

static bool iocp_get_stats(const turbo_io_impl *base, turbo_io_backend_stats *out_stats) {
  const turbo_iocp_impl *impl = (const turbo_iocp_impl *)base;
  *out_stats = (turbo_io_backend_stats){impl->endpoint_capacity,
                                        impl->endpoint_count,
                                        impl->request_capacity,
                                        impl->active_requests,
                                        impl->submitted,
                                        impl->completed,
                                        impl->cancelled,
                                        impl->failed,
                                        impl->rejected_full,
                                        impl->native_submit_errors,
                                        impl->native_cancel_errors,
                                        impl->admission_open};
  return true;
}

static const turbo_io_impl_ops iocp_ops = {iocp_attach_socket, iocp_release_socket, iocp_submit,
                                           iocp_cancel,        iocp_observe,        iocp_close,
                                           iocp_destroy,       iocp_get_stats};

bool turbo_io_platform_backend_supported(turbo_io_backend_kind kind) {
  return kind == TURBO_IO_BACKEND_IOCP;
}

int turbo_io_platform_backend_init(turbo_io_backend *backend,
                                   const turbo_io_backend_config *config) {
  turbo_iocp_impl *impl;
  WSADATA winsock_data;
  size_t index;
  int status;

  if (config->endpoint_capacity > SIZE_MAX / sizeof(turbo_iocp_endpoint_record) ||
      config->request_capacity > SIZE_MAX / sizeof(turbo_iocp_request_record) ||
      config->endpoint_capacity > SIZE_MAX / sizeof(uint32_t) ||
      config->request_capacity > SIZE_MAX / sizeof(uint32_t))
    return TURBO_ERANGE;

  impl = (turbo_iocp_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->endpoints =
      (turbo_iocp_endpoint_record *)calloc(config->endpoint_capacity, sizeof(*impl->endpoints));
  impl->requests =
      (turbo_iocp_request_record *)calloc(config->request_capacity, sizeof(*impl->requests));
  impl->free_endpoints =
      (uint32_t *)calloc(config->endpoint_capacity, sizeof(*impl->free_endpoints));
  impl->free_requests = (uint32_t *)calloc(config->request_capacity, sizeof(*impl->free_requests));
  if (impl->endpoints == NULL || impl->requests == NULL || impl->free_endpoints == NULL ||
      impl->free_requests == NULL) {
    free(impl->free_requests);
    free(impl->free_endpoints);
    free(impl->requests);
    free(impl->endpoints);
    free(impl);
    return TURBO_ENOMEM;
  }

  status = WSAStartup(MAKEWORD(2, 2), &winsock_data);
  if (status != 0) {
    free(impl->free_requests);
    free(impl->free_endpoints);
    free(impl->requests);
    free(impl->endpoints);
    free(impl);
    return -(int)status;
  }
  impl->winsock_started = true;
  impl->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0u, 1u);
  if (impl->port == NULL) {
    status = iocp_native_error(GetLastError());
    (void)WSACleanup();
    free(impl->free_requests);
    free(impl->free_endpoints);
    free(impl->requests);
    free(impl->endpoints);
    free(impl);
    return status;
  }

  impl->base.ops = &iocp_ops;
  impl->base.kind = config->kind;
  impl->endpoint_capacity = config->endpoint_capacity;
  impl->request_capacity = config->request_capacity;
  impl->completion_batch_capacity = config->completion_batch_capacity;
  impl->free_endpoint_count = config->endpoint_capacity;
  impl->free_request_count = config->request_capacity;
  impl->admission_open = true;
  for (index = 0u; index < config->endpoint_capacity; ++index) {
    impl->free_endpoints[index] = (uint32_t)(config->endpoint_capacity - index - 1u);
    impl->endpoints[index].socket_value = INVALID_SOCKET;
  }
  for (index = 0u; index < config->request_capacity; ++index) {
    impl->free_requests[index] = (uint32_t)(config->request_capacity - index - 1u);
    impl->requests[index].socket_value = INVALID_SOCKET;
  }
  backend->impl = impl;
  return TURBO_OK;
}
