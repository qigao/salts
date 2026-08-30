#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include "native_io_readiness.h"

#include <turbo/clock.h>
#include <turbo/error_codes.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

enum { TURBO_IO_INDEX_NONE = UINT32_MAX };

typedef enum turbo_io_readiness_phase {
  TURBO_IO_READINESS_FREE = 0,
  TURBO_IO_READINESS_PENDING,
  TURBO_IO_READINESS_TERMINAL
} turbo_io_readiness_phase;

typedef struct turbo_io_readiness_lane {
  uint32_t head;
  uint32_t tail;
} turbo_io_readiness_lane;

typedef struct turbo_io_readiness_endpoint {
  int fd;
  uint32_t generation;
  size_t active_requests;
  uint32_t interests;
  turbo_io_readiness_lane read_lane;
  turbo_io_readiness_lane write_lane;
  turbo_io_resource_kind resource_kind;
  bool active;
} turbo_io_readiness_endpoint;

typedef struct turbo_io_readiness_request {
  turbo_io_readiness_phase phase;
  turbo_io_request request;
  turbo_io_endpoint endpoint;
  turbo_io_operation operation;
  turbo_io_completion completion;
  uint32_t previous;
  uint32_t next;
  bool write_lane;
} turbo_io_readiness_request;

typedef struct turbo_io_readiness_impl {
  turbo_io_impl base;
  const turbo_io_readiness_driver_ops *driver_ops;
  void *driver_state;
  turbo_io_readiness_endpoint *endpoints;
  turbo_io_readiness_request *requests;
  turbo_io_ready_event *ready_events;
  uint32_t *free_endpoints;
  uint32_t *free_requests;
  uint32_t *terminal_requests;
  size_t endpoint_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t free_endpoint_count;
  size_t free_request_count;
  size_t terminal_head;
  size_t terminal_count;
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
} turbo_io_readiness_impl;

typedef struct turbo_io_sigpipe_guard {
  sigset_t blocked;
  sigset_t previous;
  bool active;
  bool had_pending;
} turbo_io_sigpipe_guard;

static void readiness_counter_increment(uint64_t *counter) {
  if (*counter != UINT64_MAX) ++*counter;
}

static uint32_t readiness_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static uint64_t readiness_endpoint_token(uint32_t index, uint32_t generation) {
  return ((uint64_t)generation << 32u) | (uint64_t)(index + 1u);
}

static turbo_io_readiness_endpoint *readiness_endpoint(turbo_io_readiness_impl *impl,
                                                       turbo_io_endpoint endpoint) {
  turbo_io_readiness_endpoint *record;
  if (!turbo_io_endpoint_valid(endpoint) || endpoint.slot > impl->endpoint_capacity) return NULL;
  record = &impl->endpoints[endpoint.slot - 1u];
  return record->active && record->generation == endpoint.generation ? record : NULL;
}

static turbo_io_readiness_request *readiness_request(turbo_io_readiness_impl *impl,
                                                     turbo_io_request request) {
  turbo_io_readiness_request *record;
  if (!turbo_io_request_valid(request) || request.slot > impl->request_capacity) return NULL;
  record = &impl->requests[request.slot - 1u];
  return record->phase != TURBO_IO_READINESS_FREE &&
                 record->request.generation == request.generation
             ? record
             : NULL;
}

static int readiness_sigpipe_begin(turbo_io_sigpipe_guard *guard) {
  sigset_t pending;
  int status;
  memset(guard, 0, sizeof(*guard));
  sigemptyset(&guard->blocked);
  sigaddset(&guard->blocked, SIGPIPE);
  status = pthread_sigmask(SIG_BLOCK, &guard->blocked, &guard->previous);
  if (status != 0) return -status;
  guard->active = true;
  if (sigpending(&pending) == 0) guard->had_pending = sigismember(&pending, SIGPIPE) == 1;
  return TURBO_OK;
}

static void readiness_sigpipe_end(turbo_io_sigpipe_guard *guard) {
  sigset_t pending;
  if (guard == NULL || !guard->active) return;
  if (!guard->had_pending && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
    int signal_number;
    int status;
    do {
      status = sigwait(&guard->blocked, &signal_number);
    } while (status == EINTR);
  }
  (void)pthread_sigmask(SIG_SETMASK, &guard->previous, NULL);
  guard->active = false;
}

static bool readiness_is_write(turbo_io_operation_kind kind) {
  return kind == TURBO_IO_TCP_SEND || kind == TURBO_IO_UDP_SEND_TO ||
         kind == TURBO_IO_PIPE_WRITE;
}

static turbo_io_readiness_lane *readiness_lane(turbo_io_readiness_endpoint *endpoint,
                                               bool write_lane) {
  return write_lane ? &endpoint->write_lane : &endpoint->read_lane;
}

static uint32_t readiness_derived_interests(const turbo_io_readiness_endpoint *endpoint) {
  uint32_t interests = 0u;
  if (endpoint->read_lane.head != TURBO_IO_INDEX_NONE) interests |= TURBO_IO_READY_READ;
  if (endpoint->write_lane.head != TURBO_IO_INDEX_NONE) interests |= TURBO_IO_READY_WRITE;
  return interests;
}

static int readiness_update_interests(turbo_io_readiness_impl *impl,
                                      turbo_io_endpoint endpoint_handle,
                                      turbo_io_readiness_endpoint *endpoint) {
  const uint32_t next = readiness_derived_interests(endpoint);
  int status;
  if (next == endpoint->interests) return TURBO_OK;
  status = impl->driver_ops->update(
      impl->driver_state, endpoint->fd,
      readiness_endpoint_token(endpoint_handle.slot - 1u, endpoint->generation),
      endpoint->interests, next);
  if (status == TURBO_OK) endpoint->interests = next;
  return status;
}

static void readiness_lane_push(turbo_io_readiness_impl *impl,
                                turbo_io_readiness_endpoint *endpoint, uint32_t index) {
  turbo_io_readiness_request *request = &impl->requests[index];
  turbo_io_readiness_lane *lane = readiness_lane(endpoint, request->write_lane);
  request->previous = lane->tail;
  request->next = TURBO_IO_INDEX_NONE;
  if (lane->tail == TURBO_IO_INDEX_NONE) lane->head = index;
  else impl->requests[lane->tail].next = index;
  lane->tail = index;
}

static void readiness_lane_remove(turbo_io_readiness_impl *impl,
                                  turbo_io_readiness_endpoint *endpoint, uint32_t index) {
  turbo_io_readiness_request *request = &impl->requests[index];
  turbo_io_readiness_lane *lane = readiness_lane(endpoint, request->write_lane);
  if (request->previous == TURBO_IO_INDEX_NONE) lane->head = request->next;
  else impl->requests[request->previous].next = request->next;
  if (request->next == TURBO_IO_INDEX_NONE) lane->tail = request->previous;
  else impl->requests[request->next].previous = request->previous;
  request->previous = TURBO_IO_INDEX_NONE;
  request->next = TURBO_IO_INDEX_NONE;
}

static void readiness_release_request(turbo_io_readiness_impl *impl,
                                      turbo_io_readiness_request *request, uint32_t index) {
  turbo_io_readiness_endpoint *endpoint = readiness_endpoint(impl, request->endpoint);
  if (endpoint != NULL && endpoint->active_requests != 0u) --endpoint->active_requests;
  request->phase = TURBO_IO_READINESS_FREE;
  request->operation = (turbo_io_operation){0};
  request->completion = (turbo_io_completion){0};
  impl->free_requests[impl->free_request_count++] = index;
  --impl->active_requests;
}

static void readiness_publish_terminal(turbo_io_readiness_impl *impl,
                                       turbo_io_readiness_request *request, uint32_t index,
                                       turbo_io_completion_kind kind, size_t bytes, int status,
                                       uint32_t native_status, size_t address_length) {
  const size_t tail = (impl->terminal_head + impl->terminal_count) % impl->request_capacity;
  request->phase = TURBO_IO_READINESS_TERMINAL;
  request->completion = (turbo_io_completion){request->request,
                                              request->endpoint,
                                              kind,
                                              bytes,
                                              status,
                                              native_status,
                                              request->operation.user_data,
                                              address_length};
  impl->terminal_requests[tail] = index;
  ++impl->terminal_count;
  readiness_counter_increment(&impl->completed);
  if (kind == TURBO_IO_COMPLETION_CANCELLED) readiness_counter_increment(&impl->cancelled);
  if (kind == TURBO_IO_COMPLETION_FAILED) readiness_counter_increment(&impl->failed);
}

static int readiness_try_socket(turbo_io_readiness_endpoint *endpoint,
                                turbo_io_readiness_request *request, size_t *out_bytes,
                                size_t *out_address_length) {
  turbo_io_sigpipe_guard guard;
  ssize_t result;
  int saved_error = 0;
  int flags = MSG_DONTWAIT;
  int guard_status = TURBO_OK;
  socklen_t address_length = (socklen_t)request->operation.address_capacity;
#if defined(MSG_NOSIGNAL)
  if (request->write_lane) flags |= MSG_NOSIGNAL;
#endif
#if !defined(MSG_NOSIGNAL)
  if (request->write_lane) {
    guard_status = readiness_sigpipe_begin(&guard);
    if (guard_status != TURBO_OK) return guard_status;
  }
#else
  (void)guard;
  (void)guard_status;
#endif
  do {
    if (request->operation.kind == TURBO_IO_TCP_RECV)
      result = recv(endpoint->fd, request->operation.buffer, request->operation.length, flags);
    else if (request->operation.kind == TURBO_IO_TCP_SEND)
      result = send(endpoint->fd, request->operation.buffer, request->operation.length, flags);
    else if (request->operation.kind == TURBO_IO_UDP_RECV_FROM)
      result = recvfrom(endpoint->fd, request->operation.buffer, request->operation.length, flags,
                        (struct sockaddr *)request->operation.address, &address_length);
    else
      result = sendto(endpoint->fd, request->operation.buffer, request->operation.length, flags,
                      (const struct sockaddr *)request->operation.address,
                      (socklen_t)request->operation.address_length);
  } while (result < 0 && errno == EINTR);
  if (result < 0) saved_error = errno;
#if !defined(MSG_NOSIGNAL)
  if (request->write_lane) readiness_sigpipe_end(&guard);
#endif
  if (result < 0) return -saved_error;
  *out_bytes = (size_t)result;
  *out_address_length =
      request->operation.kind == TURBO_IO_UDP_RECV_FROM ? (size_t)address_length : 0u;
  return TURBO_OK;
}

static int readiness_try_pipe(turbo_io_readiness_endpoint *endpoint,
                              turbo_io_readiness_request *request, size_t *out_bytes) {
  turbo_io_sigpipe_guard guard;
  ssize_t result;
  int saved_error = 0;
  int guard_status = TURBO_OK;
  if (request->write_lane) {
    guard_status = readiness_sigpipe_begin(&guard);
    if (guard_status != TURBO_OK) return guard_status;
  }
  do {
    result = request->write_lane
                 ? write(endpoint->fd, request->operation.buffer, request->operation.length)
                 : read(endpoint->fd, request->operation.buffer, request->operation.length);
  } while (result < 0 && errno == EINTR);
  if (result < 0) saved_error = errno;
  if (request->write_lane) readiness_sigpipe_end(&guard);
  if (result < 0) return -saved_error;
  *out_bytes = (size_t)result;
  return TURBO_OK;
}

static int readiness_try_operation(turbo_io_readiness_endpoint *endpoint,
                                   turbo_io_readiness_request *request, size_t *out_bytes,
                                   size_t *out_address_length) {
  if (endpoint->resource_kind == TURBO_IO_RESOURCE_SOCKET)
    return readiness_try_socket(endpoint, request, out_bytes, out_address_length);
  if (endpoint->resource_kind == TURBO_IO_RESOURCE_BYTE_PIPE) {
    *out_address_length = 0u;
    return readiness_try_pipe(endpoint, request, out_bytes);
  }
  return TURBO_EINVAL;
}

static bool readiness_would_block(int status) {
  return status == -EAGAIN || status == -EWOULDBLOCK;
}

static void readiness_finish_attempt(turbo_io_readiness_impl *impl,
                                     turbo_io_readiness_request *request, uint32_t index,
                                     int status, size_t bytes, size_t address_length) {
  if (status < 0) {
    readiness_publish_terminal(impl, request, index, TURBO_IO_COMPLETION_FAILED, 0u, status,
                               (uint32_t)(-status), 0u);
  } else if ((request->operation.kind == TURBO_IO_TCP_RECV ||
              request->operation.kind == TURBO_IO_PIPE_READ) &&
             bytes == 0u) {
    readiness_publish_terminal(impl, request, index, TURBO_IO_COMPLETION_EOF, 0u, TURBO_EOF, 0u,
                               0u);
  } else {
    readiness_publish_terminal(impl, request, index, TURBO_IO_COMPLETION_OK, bytes, TURBO_OK, 0u,
                               address_length);
  }
}

static int readiness_attach_endpoint(turbo_io_readiness_impl *impl, int fd,
                                     turbo_io_resource_kind resource_kind,
                                     turbo_io_endpoint *out_endpoint) {
  turbo_io_readiness_endpoint *endpoint;
  uint32_t index;
  size_t cursor;
  if (!impl->admission_open) return TURBO_ESHUTDOWN;
  for (cursor = 0u; cursor < impl->endpoint_capacity; ++cursor)
    if (impl->endpoints[cursor].active && impl->endpoints[cursor].fd == fd)
      return TURBO_EALREADY;
  if (impl->free_endpoint_count == 0u) return TURBO_ENOBUFS;
  index = impl->free_endpoints[--impl->free_endpoint_count];
  endpoint = &impl->endpoints[index];
  endpoint->fd = fd;
  endpoint->generation = readiness_next_generation(endpoint->generation);
  endpoint->active_requests = 0u;
  endpoint->interests = 0u;
  endpoint->read_lane = (turbo_io_readiness_lane){TURBO_IO_INDEX_NONE, TURBO_IO_INDEX_NONE};
  endpoint->write_lane = (turbo_io_readiness_lane){TURBO_IO_INDEX_NONE, TURBO_IO_INDEX_NONE};
  endpoint->resource_kind = resource_kind;
  endpoint->active = true;
  ++impl->endpoint_count;
  *out_endpoint = (turbo_io_endpoint){index + 1u, endpoint->generation};
  return TURBO_OK;
}

static int readiness_attach_socket(turbo_io_impl *base, uintptr_t native_socket,
                                   turbo_io_endpoint *out_endpoint) {
  if (native_socket > (uintptr_t)INT_MAX) return TURBO_EINVAL;
  return readiness_attach_endpoint((turbo_io_readiness_impl *)base, (int)native_socket,
                                   TURBO_IO_RESOURCE_SOCKET, out_endpoint);
}

static int readiness_attach_pipe(turbo_io_impl *base, uintptr_t native_handle, uint32_t flags,
                                 turbo_io_endpoint *out_endpoint) {
  struct stat descriptor_stat;
  int descriptor_flags;
  int fd;
  (void)flags;
  if (native_handle > (uintptr_t)INT_MAX) return TURBO_EINVAL;
  fd = (int)native_handle;
  descriptor_flags = fcntl(fd, F_GETFL, 0);
  if (descriptor_flags < 0) return -errno;
  if ((descriptor_flags & O_NONBLOCK) == 0) return TURBO_EINVAL;
  if (fstat(fd, &descriptor_stat) != 0) return -errno;
  if (!S_ISFIFO(descriptor_stat.st_mode)) return TURBO_EINVAL;
  return readiness_attach_endpoint((turbo_io_readiness_impl *)base, fd,
                                   TURBO_IO_RESOURCE_BYTE_PIPE, out_endpoint);
}

static int readiness_release_endpoint(turbo_io_readiness_impl *impl,
                                      turbo_io_endpoint endpoint_handle,
                                      turbo_io_resource_kind resource_kind) {
  turbo_io_readiness_endpoint *endpoint = readiness_endpoint(impl, endpoint_handle);
  uint32_t index;
  if (endpoint == NULL) return TURBO_ENOENT;
  if (endpoint->resource_kind != resource_kind) return TURBO_EINVAL;
  if (endpoint->active_requests != 0u || endpoint->interests != 0u) return TURBO_EBUSY;
  index = endpoint_handle.slot - 1u;
  endpoint->active = false;
  endpoint->fd = -1;
  endpoint->resource_kind = (turbo_io_resource_kind)0;
  impl->free_endpoints[impl->free_endpoint_count++] = index;
  --impl->endpoint_count;
  return TURBO_OK;
}

static int readiness_release_socket(turbo_io_impl *base, turbo_io_endpoint endpoint_handle) {
  return readiness_release_endpoint((turbo_io_readiness_impl *)base, endpoint_handle,
                                    TURBO_IO_RESOURCE_SOCKET);
}

static int readiness_release_pipe(turbo_io_impl *base, turbo_io_endpoint endpoint_handle) {
  return readiness_release_endpoint((turbo_io_readiness_impl *)base, endpoint_handle,
                                    TURBO_IO_RESOURCE_BYTE_PIPE);
}

static int readiness_submit(turbo_io_impl *base, const turbo_io_operation *operation,
                            turbo_io_request *out_request) {
  turbo_io_readiness_impl *impl = (turbo_io_readiness_impl *)base;
  turbo_io_readiness_endpoint *endpoint;
  turbo_io_readiness_request *request;
  uint32_t index;
  size_t bytes = 0u;
  size_t address_length = 0u;
  int status;
  if (!impl->admission_open) return TURBO_ESHUTDOWN;
  endpoint = readiness_endpoint(impl, operation->endpoint);
  if (endpoint == NULL) return TURBO_ENOENT;
  if (turbo_io_operation_resource_kind(operation->kind) != endpoint->resource_kind)
    return TURBO_EINVAL;
  if (impl->free_request_count == 0u) {
    readiness_counter_increment(&impl->rejected_full);
    return TURBO_ENOBUFS;
  }
  index = impl->free_requests[--impl->free_request_count];
  request = &impl->requests[index];
  request->phase = TURBO_IO_READINESS_PENDING;
  request->request =
      (turbo_io_request){index + 1u, readiness_next_generation(request->request.generation)};
  request->endpoint = operation->endpoint;
  request->operation = *operation;
  request->previous = TURBO_IO_INDEX_NONE;
  request->next = TURBO_IO_INDEX_NONE;
  request->write_lane = readiness_is_write(operation->kind);
  ++endpoint->active_requests;
  ++impl->active_requests;
  status = readiness_try_operation(endpoint, request, &bytes, &address_length);
  if (readiness_would_block(status)) {
    readiness_lane_push(impl, endpoint, index);
    status = readiness_update_interests(impl, operation->endpoint, endpoint);
    if (status != TURBO_OK) {
      readiness_lane_remove(impl, endpoint, index);
      readiness_release_request(impl, request, index);
      readiness_counter_increment(&impl->native_submit_errors);
      return status;
    }
  } else if (status < 0) {
    readiness_release_request(impl, request, index);
    readiness_counter_increment(&impl->native_submit_errors);
    return status;
  } else {
    readiness_finish_attempt(impl, request, index, TURBO_OK, bytes, address_length);
  }
  readiness_counter_increment(&impl->submitted);
  *out_request = request->request;
  return TURBO_OK;
}

static int readiness_cancel(turbo_io_impl *base, turbo_io_request request_handle) {
  turbo_io_readiness_impl *impl = (turbo_io_readiness_impl *)base;
  turbo_io_readiness_request *request = readiness_request(impl, request_handle);
  turbo_io_readiness_endpoint *endpoint;
  uint32_t index;
  int status;
  if (request == NULL) return TURBO_ENOENT;
  if (request->phase == TURBO_IO_READINESS_TERMINAL) return TURBO_EALREADY;
  endpoint = readiness_endpoint(impl, request->endpoint);
  if (endpoint == NULL) return TURBO_ENOENT;
  index = request_handle.slot - 1u;
  readiness_lane_remove(impl, endpoint, index);
  status = readiness_update_interests(impl, request->endpoint, endpoint);
  if (status != TURBO_OK) {
    readiness_lane_push(impl, endpoint, index);
    readiness_counter_increment(&impl->native_cancel_errors);
    return status;
  }
  readiness_publish_terminal(impl, request, index, TURBO_IO_COMPLETION_CANCELLED, 0u,
                             TURBO_ECANCELED, (uint32_t)ECANCELED, 0u);
  return TURBO_OK;
}

static int readiness_drive_lane(turbo_io_readiness_impl *impl, turbo_io_endpoint endpoint_handle,
                                turbo_io_readiness_endpoint *endpoint, bool write_lane) {
  turbo_io_readiness_lane *lane = readiness_lane(endpoint, write_lane);
  while (lane->head != TURBO_IO_INDEX_NONE) {
    const uint32_t index = lane->head;
    turbo_io_readiness_request *request = &impl->requests[index];
    size_t bytes = 0u;
    size_t address_length = 0u;
    const int status = readiness_try_operation(endpoint, request, &bytes, &address_length);
    if (readiness_would_block(status)) break;
    readiness_lane_remove(impl, endpoint, index);
    readiness_finish_attempt(impl, request, index, status, bytes, address_length);
  }
  return readiness_update_interests(impl, endpoint_handle, endpoint);
}

static void readiness_drain_terminals(turbo_io_readiness_impl *impl, turbo_io_completion *events,
                                      size_t limit, size_t *out_count) {
  while (*out_count < limit && impl->terminal_count != 0u) {
    const uint32_t index = impl->terminal_requests[impl->terminal_head];
    turbo_io_readiness_request *request = &impl->requests[index];
    impl->terminal_head = (impl->terminal_head + 1u) % impl->request_capacity;
    --impl->terminal_count;
    events[(*out_count)++] = request->completion;
    readiness_release_request(impl, request, index);
  }
}

static uint32_t readiness_remaining_timeout(uint64_t started_ms, uint32_t timeout_ms) {
  uint64_t elapsed;
  if (timeout_ms == UINT32_MAX) return UINT32_MAX;
  elapsed = turbo_monotonic_ms() - started_ms;
  return elapsed >= timeout_ms ? 0u : timeout_ms - (uint32_t)elapsed;
}

static int readiness_observe(turbo_io_impl *base, turbo_io_completion *events,
                             size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  turbo_io_readiness_impl *impl = (turbo_io_readiness_impl *)base;
  const size_t limit = event_capacity < impl->completion_batch_capacity
                           ? event_capacity
                           : impl->completion_batch_capacity;
  const uint64_t started_ms = turbo_monotonic_ms();
  uint32_t wait_timeout = timeout_ms;
  readiness_drain_terminals(impl, events, limit, out_count);
  if (*out_count != 0u) return TURBO_OK;
  for (;;) {
    size_t ready_count = 0u;
    int status =
        impl->driver_ops->wait(impl->driver_state, impl->ready_events,
                               impl->completion_batch_capacity, wait_timeout, &ready_count);
    if (status != TURBO_OK) return status;
    for (size_t cursor = 0u; cursor < ready_count; ++cursor) {
      const turbo_io_ready_event *ready = &impl->ready_events[cursor];
      const uint32_t slot = (uint32_t)ready->token;
      const uint32_t generation = (uint32_t)(ready->token >> 32u);
      turbo_io_endpoint handle = {slot, generation};
      turbo_io_readiness_endpoint *endpoint = readiness_endpoint(impl, handle);
      if (endpoint == NULL) continue;
      if ((ready->interests & (TURBO_IO_READY_READ | TURBO_IO_READY_ERROR)) != 0u) {
        status = readiness_drive_lane(impl, handle, endpoint, false);
        if (status != TURBO_OK) return status;
      }
      if ((ready->interests & (TURBO_IO_READY_WRITE | TURBO_IO_READY_ERROR)) != 0u) {
        status = readiness_drive_lane(impl, handle, endpoint, true);
        if (status != TURBO_OK) return status;
      }
    }
    readiness_drain_terminals(impl, events, limit, out_count);
    if (*out_count != 0u) return TURBO_OK;
    wait_timeout = readiness_remaining_timeout(started_ms, timeout_ms);
    if (wait_timeout == 0u) return TURBO_ETIMEDOUT;
  }
}

static int readiness_close(turbo_io_impl *base) {
  turbo_io_readiness_impl *impl = (turbo_io_readiness_impl *)base;
  if (!impl->admission_open) return TURBO_EALREADY;
  impl->admission_open = false;
  return TURBO_OK;
}

static int readiness_destroy(turbo_io_impl *base) {
  turbo_io_readiness_impl *impl = (turbo_io_readiness_impl *)base;
  if (impl->admission_open || impl->active_requests != 0u || impl->endpoint_count != 0u)
    return TURBO_EBUSY;
  impl->driver_ops->destroy(impl->driver_state);
  free(impl->terminal_requests);
  free(impl->free_requests);
  free(impl->free_endpoints);
  free(impl->ready_events);
  free(impl->requests);
  free(impl->endpoints);
  free(impl->driver_state);
  free(impl);
  return TURBO_OK;
}

static bool readiness_get_stats(const turbo_io_impl *base, turbo_io_backend_stats *out_stats) {
  const turbo_io_readiness_impl *impl = (const turbo_io_readiness_impl *)base;
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

static const turbo_io_impl_ops readiness_ops = {
    readiness_attach_socket, readiness_release_socket, readiness_submit,  readiness_cancel,
    readiness_observe,       readiness_close,          readiness_destroy, readiness_get_stats,
    readiness_attach_pipe,   readiness_release_pipe};

static bool readiness_array_fits(size_t count, size_t element_size) {
  return element_size != 0u && count <= SIZE_MAX / element_size;
}

int turbo_io_readiness_backend_init(turbo_io_backend *backend,
                                    const turbo_io_backend_config *config,
                                    const turbo_io_readiness_driver_ops *driver_ops,
                                    size_t driver_state_size) {
  turbo_io_readiness_impl *impl;
  int status;
  if (driver_ops == NULL || driver_ops->init == NULL || driver_ops->update == NULL ||
      driver_ops->wait == NULL || driver_ops->destroy == NULL || driver_state_size == 0u ||
      !readiness_array_fits(config->endpoint_capacity, sizeof(turbo_io_readiness_endpoint)) ||
      !readiness_array_fits(config->request_capacity, sizeof(turbo_io_readiness_request)) ||
      !readiness_array_fits(config->completion_batch_capacity, sizeof(turbo_io_ready_event)) ||
      !readiness_array_fits(config->endpoint_capacity, sizeof(uint32_t)) ||
      !readiness_array_fits(config->request_capacity, sizeof(uint32_t)))
    return TURBO_ERANGE;
  impl = (turbo_io_readiness_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->driver_state = calloc(1u, driver_state_size);
  impl->endpoints =
      (turbo_io_readiness_endpoint *)calloc(config->endpoint_capacity, sizeof(*impl->endpoints));
  impl->requests =
      (turbo_io_readiness_request *)calloc(config->request_capacity, sizeof(*impl->requests));
  impl->ready_events = (turbo_io_ready_event *)calloc(config->completion_batch_capacity,
                                                      sizeof(*impl->ready_events));
  impl->free_endpoints = (uint32_t *)calloc(config->endpoint_capacity, sizeof(uint32_t));
  impl->free_requests = (uint32_t *)calloc(config->request_capacity, sizeof(uint32_t));
  impl->terminal_requests = (uint32_t *)calloc(config->request_capacity, sizeof(uint32_t));
  if (impl->driver_state == NULL || impl->endpoints == NULL || impl->requests == NULL ||
      impl->ready_events == NULL || impl->free_endpoints == NULL || impl->free_requests == NULL ||
      impl->terminal_requests == NULL) {
    status = TURBO_ENOMEM;
    goto failed;
  }
  impl->base.ops = &readiness_ops;
  impl->base.kind = config->kind;
  impl->driver_ops = driver_ops;
  impl->endpoint_capacity = config->endpoint_capacity;
  impl->request_capacity = config->request_capacity;
  impl->completion_batch_capacity = config->completion_batch_capacity;
  impl->free_endpoint_count = config->endpoint_capacity;
  impl->free_request_count = config->request_capacity;
  impl->admission_open = true;
  for (size_t index = 0u; index < config->endpoint_capacity; ++index) {
    impl->free_endpoints[index] = (uint32_t)(config->endpoint_capacity - index - 1u);
    impl->endpoints[index].fd = -1;
  }
  for (size_t index = 0u; index < config->request_capacity; ++index)
    impl->free_requests[index] = (uint32_t)(config->request_capacity - index - 1u);
  status = driver_ops->init(impl->driver_state, config->completion_batch_capacity);
  if (status != TURBO_OK) goto failed;
  backend->impl = impl;
  return TURBO_OK;

failed:
  free(impl->terminal_requests);
  free(impl->free_requests);
  free(impl->free_endpoints);
  free(impl->ready_events);
  free(impl->requests);
  free(impl->endpoints);
  free(impl->driver_state);
  free(impl);
  return status;
}
