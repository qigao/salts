#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include "native_io_internal.h"

#include <salts/clock.h>
#include <salts/error_codes.h>

#include <errno.h>
#include <limits.h>
#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

typedef enum salts_io_uring_phase {
  SALTS_IO_URING_FREE = 0,
  SALTS_IO_URING_PENDING,
  SALTS_IO_URING_TERMINAL
} salts_io_uring_phase;

typedef struct salts_io_uring_lane {
  /* Only the head enters the kernel: concurrent recv SQEs need not consume stream bytes FIFO. */
  uint32_t head;
  uint32_t tail;
} salts_io_uring_lane;

typedef struct salts_io_uring_endpoint {
  int fd;
  uint32_t generation;
  size_t active_requests;
  salts_io_uring_lane read_lane;
  salts_io_uring_lane write_lane;
  salts_io_resource_kind resource_kind;
  bool connected;
  bool connect_active;
  bool active;
} salts_io_uring_endpoint;

typedef struct salts_io_uring_request_record {
  salts_io_uring_phase phase;
  native_io_request request;
  native_io_endpoint endpoint;
  native_io_operation operation;
  struct iovec vector;
  struct msghdr message;
  native_io_completion completion;
  uint64_t native_token;
  uint32_t previous;
  uint32_t next;
  uint32_t staged_previous;
  uint32_t staged_next;
  bool staged;
  bool write_lane;
  bool in_flight;
  bool cancel_requested;
} salts_io_uring_request_record;

typedef struct salts_io_uring_impl {
  salts_io_impl base;
  salts_io_uring_endpoint *endpoints;
  salts_io_uring_request_record *requests;
  uint32_t *free_endpoints;
  uint32_t *free_requests;
  /* Terminal records stay borrowed until observe copies their completion. */
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
  uint32_t staged_head;
  uint32_t staged_tail;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t failed;
  uint64_t rejected_full;
  uint64_t native_submit_errors;
  uint64_t native_cancel_errors;
  int ring_fd;
  int wake_fd;
  void *sq_ring;
  void *cq_ring;
  struct io_uring_sqe *sqes;
  size_t sq_ring_size;
  size_t cq_ring_size;
  size_t sqes_size;
  unsigned *sq_head;
  unsigned *sq_tail;
  unsigned *sq_mask;
  unsigned *sq_entries;
  unsigned *sq_array;
  unsigned *cq_head;
  unsigned *cq_tail;
  unsigned *cq_mask;
  struct io_uring_cqe *cqes;
  bool single_mmap;
  bool ext_arg_wait;
  bool ring_wake;
  bool wake_armed;
  bool admission_open;
  atomic_bool wake_pending;
} salts_io_uring_impl;

enum {
  SALTS_IO_URING_CANCEL_TOKEN = 0u,
  SALTS_IO_URING_WAKE_TOKEN = 1u,
  SALTS_IO_URING_INDEX_NONE = UINT32_MAX
};

typedef struct salts_io_uring_sigpipe_guard {
  sigset_t blocked;
  sigset_t previous;
  bool active;
  bool had_pending;
} salts_io_uring_sigpipe_guard;

static void uring_counter_increment(uint64_t *counter) {
  if (*counter != UINT64_MAX) ++*counter;
}

static uint32_t uring_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static uint64_t uring_request_token(uint32_t index, uint32_t generation) {
  return ((uint64_t)generation << 32u) | (uint64_t)(index + 1u);
}

static int uring_sigpipe_guard_begin(salts_io_uring_sigpipe_guard *guard) {
  sigset_t pending;
  int status;
  memset(guard, 0, sizeof(*guard));
  sigemptyset(&guard->blocked);
  sigaddset(&guard->blocked, SIGPIPE);
  status = pthread_sigmask(SIG_BLOCK, &guard->blocked, &guard->previous);
  if (status != 0) return -status;
  guard->active = true;
  if (sigpending(&pending) == 0) guard->had_pending = sigismember(&pending, SIGPIPE) == 1;
  return SALTS_OK;
}

static void uring_sigpipe_guard_end(salts_io_uring_sigpipe_guard *guard) {
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

static salts_io_uring_endpoint *uring_endpoint(salts_io_uring_impl *impl,
                                               native_io_endpoint endpoint) {
  salts_io_uring_endpoint *record;
  if (!native_io_endpoint_valid(endpoint) || endpoint.slot > impl->endpoint_capacity) return NULL;
  record = &impl->endpoints[endpoint.slot - 1u];
  return record->active && record->generation == endpoint.generation ? record : NULL;
}

static salts_io_uring_request_record *uring_request(salts_io_uring_impl *impl,
                                                    native_io_request request) {
  salts_io_uring_request_record *record;
  if (!native_io_request_valid(request) || request.slot > impl->request_capacity) return NULL;
  record = &impl->requests[request.slot - 1u];
  return record->phase != SALTS_IO_URING_FREE && record->request.generation == request.generation
             ? record
             : NULL;
}

static salts_io_uring_request_record *uring_record_for_token(salts_io_uring_impl *impl,
                                                             uint64_t token) {
  const uint32_t slot = (uint32_t)token;
  const uint32_t generation = (uint32_t)(token >> 32u);
  salts_io_uring_request_record *record;
  if (slot == 0u || generation == 0u || slot > impl->request_capacity) return NULL;
  record = &impl->requests[slot - 1u];
  return record->phase == SALTS_IO_URING_PENDING && record->in_flight &&
                 record->native_token == token
             ? record
             : NULL;
}

static bool uring_is_write(native_io_operation_kind kind) {
  return kind == NATIVE_IO_OPERATION_STREAM_SEND || kind == NATIVE_IO_OPERATION_UDP_SEND_TO ||
         kind == NATIVE_IO_OPERATION_STREAM_CONNECT || kind == NATIVE_IO_OPERATION_PIPE_WRITE;
}

static salts_io_uring_lane *uring_lane(salts_io_uring_endpoint *endpoint, bool write_lane) {
  return write_lane ? &endpoint->write_lane : &endpoint->read_lane;
}

static void uring_lane_push(salts_io_uring_impl *impl, salts_io_uring_endpoint *endpoint,
                            uint32_t index) {
  salts_io_uring_request_record *request = &impl->requests[index];
  salts_io_uring_lane *lane = uring_lane(endpoint, request->write_lane);
  request->previous = lane->tail;
  request->next = SALTS_IO_URING_INDEX_NONE;
  if (lane->tail == SALTS_IO_URING_INDEX_NONE) lane->head = index;
  else impl->requests[lane->tail].next = index;
  lane->tail = index;
}

static void uring_lane_remove(salts_io_uring_impl *impl, salts_io_uring_endpoint *endpoint,
                              uint32_t index) {
  salts_io_uring_request_record *request = &impl->requests[index];
  salts_io_uring_lane *lane = uring_lane(endpoint, request->write_lane);
  if (request->previous == SALTS_IO_URING_INDEX_NONE) lane->head = request->next;
  else impl->requests[request->previous].next = request->next;
  if (request->next == SALTS_IO_URING_INDEX_NONE) lane->tail = request->previous;
  else impl->requests[request->next].previous = request->previous;
  request->previous = SALTS_IO_URING_INDEX_NONE;
  request->next = SALTS_IO_URING_INDEX_NONE;
}

static int uring_enter_arg_once(salts_io_uring_impl *impl, unsigned submit, unsigned minimum,
                                unsigned flags, void *argument, size_t argument_size) {
  const int status =
      (int)syscall(__NR_io_uring_enter, impl->ring_fd, submit, minimum, flags, argument,
                   argument_size);
  return status < 0 ? -errno : status;
}

static int uring_enter(salts_io_uring_impl *impl, unsigned submit, unsigned minimum,
                       unsigned flags) {
  int status;
  do {
    status = uring_enter_arg_once(impl, submit, minimum, flags, NULL, 0u);
  } while (status == -EINTR);
  return status;
}

/* Scheduling links live in the request record: no second payload/owner queue,
 * allocation, or scan over unused endpoint capacity. Admission/removal is O(1). */
static void uring_stage(salts_io_uring_impl *impl, uint32_t index) {
  salts_io_uring_request_record *request = &impl->requests[index];
  request->staged_previous = impl->staged_tail;
  request->staged_next = SALTS_IO_URING_INDEX_NONE;
  if (impl->staged_tail == SALTS_IO_URING_INDEX_NONE) impl->staged_head = index;
  else impl->requests[impl->staged_tail].staged_next = index;
  impl->staged_tail = index;
  request->staged = true;
}

static void uring_unstage(salts_io_uring_impl *impl, uint32_t index) {
  salts_io_uring_request_record *request = &impl->requests[index];
  if (!request->staged) return;
  if (request->staged_previous == SALTS_IO_URING_INDEX_NONE)
    impl->staged_head = request->staged_next;
  else impl->requests[request->staged_previous].staged_next = request->staged_next;
  if (request->staged_next == SALTS_IO_URING_INDEX_NONE)
    impl->staged_tail = request->staged_previous;
  else impl->requests[request->staged_next].staged_previous = request->staged_previous;
  request->staged = false;
}

static int uring_publish_sqe(salts_io_uring_impl *impl, const struct io_uring_sqe *prepared) {
  const unsigned head =
      atomic_load_explicit((_Atomic unsigned *)impl->sq_head, memory_order_acquire);
  const unsigned tail =
      atomic_load_explicit((_Atomic unsigned *)impl->sq_tail, memory_order_relaxed);
  unsigned index;
  int submitted;
  if (tail - head >= *impl->sq_entries) return SALTS_EBUSY;
  index = tail & *impl->sq_mask;
  impl->sqes[index] = *prepared;
  impl->sq_array[index] = index;
  atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, tail + 1u, memory_order_release);
  submitted = uring_enter(impl, 1u, 0u, 0u);
  if (submitted == 1) return SALTS_OK;
  atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, tail, memory_order_release);
  return submitted < 0 ? submitted : SALTS_EIO;
}

static bool uring_wake_can_fallback(int status) {
  return status == SALTS_EBUSY || status == -ECANCELED || status == -EINVAL ||
         status == -EOPNOTSUPP;
}

static int uring_arm_wake(salts_io_uring_impl *impl) {
  struct io_uring_sqe sqe;
  int status;
  if (!impl->ring_wake || impl->wake_armed) return SALTS_OK;
  memset(&sqe, 0, sizeof(sqe));
  sqe.opcode = IORING_OP_POLL_ADD;
  sqe.fd = impl->wake_fd;
  sqe.poll32_events = POLLIN;
  sqe.user_data = SALTS_IO_URING_WAKE_TOKEN;
  status = uring_publish_sqe(impl, &sqe);
  if (status == SALTS_OK) impl->wake_armed = true;
  return status;
}

static void uring_prepare_operation(salts_io_uring_request_record *record, struct io_uring_sqe *sqe,
                                    int fd) {
  memset(sqe, 0, sizeof(*sqe));
  sqe->fd = fd;
  sqe->user_data = record->native_token;
  if (record->operation.kind == NATIVE_IO_OPERATION_PIPE_READ ||
      record->operation.kind == NATIVE_IO_OPERATION_PIPE_WRITE) {
    sqe->opcode =
        record->operation.kind == NATIVE_IO_OPERATION_PIPE_READ ? IORING_OP_READ : IORING_OP_WRITE;
    sqe->addr = (uint64_t)(uintptr_t)record->operation.buffer;
    sqe->len = (uint32_t)record->operation.length;
    sqe->off = UINT64_MAX;
  } else if (record->operation.kind == NATIVE_IO_OPERATION_STREAM_RECV) {
    sqe->opcode = IORING_OP_RECV;
    sqe->addr = (uint64_t)(uintptr_t)record->operation.buffer;
    sqe->len = (uint32_t)record->operation.length;
  } else if (record->operation.kind == NATIVE_IO_OPERATION_STREAM_SEND) {
    sqe->opcode = IORING_OP_SEND;
    sqe->addr = (uint64_t)(uintptr_t)record->operation.buffer;
    sqe->len = (uint32_t)record->operation.length;
    sqe->msg_flags = MSG_NOSIGNAL;
  } else if (record->operation.kind == NATIVE_IO_OPERATION_STREAM_CONNECT) {
    sqe->opcode = IORING_OP_CONNECT;
    sqe->addr = (uint64_t)(uintptr_t)record->operation.address;
    sqe->off = (uint64_t)record->operation.address_length;
  } else {
    record->vector.iov_base = record->operation.buffer;
    record->vector.iov_len = record->operation.length;
    memset(&record->message, 0, sizeof(record->message));
    record->message.msg_name = record->operation.address;
    record->message.msg_namelen = (socklen_t)(record->operation.kind == NATIVE_IO_OPERATION_UDP_RECV_FROM
                                                  ? record->operation.address_capacity
                                                  : record->operation.address_length);
    record->message.msg_iov = &record->vector;
    record->message.msg_iovlen = 1u;
    sqe->opcode =
        record->operation.kind == NATIVE_IO_OPERATION_UDP_RECV_FROM ? IORING_OP_RECVMSG : IORING_OP_SENDMSG;
    sqe->addr = (uint64_t)(uintptr_t)&record->message;
    sqe->len = 1u;
    if (record->operation.kind == NATIVE_IO_OPERATION_UDP_SEND_TO) sqe->msg_flags = MSG_NOSIGNAL;
  }
}

static int uring_start_request(salts_io_uring_impl *impl, salts_io_uring_request_record *request,
                               int fd) {
  salts_io_uring_sigpipe_guard sigpipe_guard = {0};
  struct io_uring_sqe sqe;
  const bool guard_sigpipe = request->operation.kind == NATIVE_IO_OPERATION_PIPE_WRITE;
  int status;
  uring_prepare_operation(request, &sqe, fd);
  status = guard_sigpipe ? uring_sigpipe_guard_begin(&sigpipe_guard) : SALTS_OK;
  if (status == SALTS_OK) status = uring_publish_sqe(impl, &sqe);
  uring_sigpipe_guard_end(&sigpipe_guard);
  if (status == SALTS_OK) request->in_flight = true;
  return status;
}

static void uring_release_request(salts_io_uring_impl *impl, salts_io_uring_request_record *request,
                                  uint32_t index) {
  salts_io_uring_endpoint *endpoint = uring_endpoint(impl, request->endpoint);
  if (endpoint != NULL && endpoint->active_requests != 0u) --endpoint->active_requests;
  request->phase = SALTS_IO_URING_FREE;
  request->native_token = 0u;
  request->operation = (native_io_operation){0};
  request->completion = (native_io_completion){0};
  request->previous = SALTS_IO_URING_INDEX_NONE;
  request->next = SALTS_IO_URING_INDEX_NONE;
  request->write_lane = false;
  request->in_flight = false;
  request->cancel_requested = false;
  impl->free_requests[impl->free_request_count++] = index;
  --impl->active_requests;
}

static void uring_make_completion(salts_io_uring_impl *impl, salts_io_uring_request_record *request,
                                  int result, native_io_completion *completion) {
  const bool cancelled = request->cancel_requested && result == -ECANCELED;
  if (result >= 0 && request->operation.kind == NATIVE_IO_OPERATION_UDP_RECV_FROM &&
      (request->message.msg_flags & MSG_TRUNC) != 0)
    result = -EMSGSIZE;
  if (request->operation.kind == NATIVE_IO_OPERATION_STREAM_CONNECT) {
    salts_io_uring_endpoint *endpoint = uring_endpoint(impl, request->endpoint);
    if (endpoint != NULL) {
      endpoint->connect_active = false;
      endpoint->connected = result == 0;
    }
  }
  *completion = (native_io_completion){request->request,
                                      request->endpoint,
                                      NATIVE_IO_COMPLETION_OK,
                                      0u,
                                      SALTS_OK,
                                      0u,
                                      request->operation.user_data,
                                      0u};
  if (cancelled) {
    completion->kind = NATIVE_IO_COMPLETION_CANCELLED;
    completion->status = SALTS_ECANCELED;
    completion->native_status = ECANCELED;
    uring_counter_increment(&impl->cancelled);
  } else if (result < 0) {
    completion->kind = NATIVE_IO_COMPLETION_FAILED;
    completion->status = result;
    completion->native_status = (uint32_t)(-result);
    uring_counter_increment(&impl->failed);
  } else if ((request->operation.kind == NATIVE_IO_OPERATION_STREAM_RECV ||
              request->operation.kind == NATIVE_IO_OPERATION_PIPE_READ) &&
             result == 0) {
    completion->kind = NATIVE_IO_COMPLETION_EOF;
    completion->status = SALTS_EOF;
  } else {
    completion->bytes = (size_t)result;
    if (request->operation.kind == NATIVE_IO_OPERATION_UDP_RECV_FROM)
      completion->address_length = (size_t)request->message.msg_namelen;
  }
  uring_counter_increment(&impl->completed);
}

static void uring_queue_terminal(salts_io_uring_impl *impl, salts_io_uring_request_record *request,
                                 uint32_t index, int result) {
  const size_t tail = (impl->terminal_head + impl->terminal_count) % impl->request_capacity;
  request->in_flight = false;
  request->native_token = 0u;
  uring_make_completion(impl, request, result, &request->completion);
  request->phase = SALTS_IO_URING_TERMINAL;
  impl->terminal_requests[tail] = index;
  ++impl->terminal_count;
}

static void uring_start_lane(salts_io_uring_impl *impl, salts_io_uring_lane *lane) {
  if (lane->head != SALTS_IO_URING_INDEX_NONE) {
    salts_io_uring_request_record *request = &impl->requests[lane->head];
    if (!request->in_flight && !request->staged) uring_stage(impl, lane->head);
  }
}

static void uring_fail_staged(salts_io_uring_impl *impl, int status) {
  while (impl->staged_head != SALTS_IO_URING_INDEX_NONE) {
    const uint32_t index = impl->staged_head;
    salts_io_uring_request_record *request = &impl->requests[index];
    salts_io_uring_endpoint *endpoint = uring_endpoint(impl, request->endpoint);
    salts_io_uring_lane *lane = uring_lane(endpoint, request->write_lane);
    uring_unstage(impl, index);
    uring_lane_remove(impl, endpoint, index);
    uring_queue_terminal(impl, request, index, status);
    uring_counter_increment(&impl->native_submit_errors);
    uring_start_lane(impl, lane);
  }
}

/* O(eligible requests), including short enters: keep the prepared suffix in SQ
 * instead of rebuilding it on every retry. With no SQPOLL, an error can withdraw
 * only that unconsumed suffix without releasing any kernel-owned storage. */
static int uring_flush(salts_io_impl *base) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  while (impl->staged_head != SALTS_IO_URING_INDEX_NONE) {
    const unsigned head = atomic_load_explicit((_Atomic unsigned *)impl->sq_head, memory_order_acquire);
    const unsigned tail = atomic_load_explicit((_Atomic unsigned *)impl->sq_tail, memory_order_relaxed);
    const unsigned available = *impl->sq_entries - (tail - head);
    unsigned count = 0u;
    uint32_t index = impl->staged_head;
    bool pipe_write = false;
    salts_io_uring_sigpipe_guard guard = {0};
    int status;
    while (index != SALTS_IO_URING_INDEX_NONE && count < available) {
      salts_io_uring_request_record *request = &impl->requests[index];
      const unsigned slot = (tail + count) & *impl->sq_mask;
      uring_prepare_operation(request, &impl->sqes[slot],
                              impl->endpoints[request->endpoint.slot - 1u].fd);
      impl->sq_array[slot] = slot;
      pipe_write |= request->operation.kind == NATIVE_IO_OPERATION_PIPE_WRITE;
      index = request->staged_next;
      ++count;
    }
    status = count == 0u ? SALTS_EBUSY
                        : pipe_write ? uring_sigpipe_guard_begin(&guard) : SALTS_OK;
    if (status == SALTS_OK) {
      unsigned remaining = count;
      unsigned consumed_head = head;
      atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, tail + count, memory_order_release);
      do {
        status = uring_enter(impl, remaining, 0u, 0u);
        const unsigned next_head = atomic_load_explicit((_Atomic unsigned *)impl->sq_head,
                                                         memory_order_acquire);
        const unsigned consumed = next_head - consumed_head;
        if (consumed > remaining) {
          uring_sigpipe_guard_end(&guard);
          return SALTS_EPROTO;
        }
        for (unsigned i = 0u; i < consumed; ++i) {
          const uint32_t accepted = impl->staged_head;
          uring_unstage(impl, accepted);
          impl->requests[accepted].in_flight = true;
        }
        remaining -= consumed;
        consumed_head = next_head;
        if (status >= 0) status = consumed == 0u ? SALTS_EIO : SALTS_OK;
        if (status != SALTS_OK) {
          atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, next_head, memory_order_release);
          break;
        }
      } while (remaining != 0u);
    }
    uring_sigpipe_guard_end(&guard);
    if (status != SALTS_OK) {
      uring_fail_staged(impl, status);
      return status;
    }
  }
  return SALTS_OK;
}

static int uring_attach_endpoint(salts_io_uring_impl *impl, int fd,
                                 salts_io_resource_kind resource_kind, bool connected,
                                 native_io_endpoint *out_endpoint) {
  salts_io_uring_endpoint *endpoint;
  uint32_t index;
  size_t cursor;
  if (!impl->admission_open) return SALTS_ESHUTDOWN;
  for (cursor = 0u; cursor < impl->endpoint_capacity; ++cursor)
    if (impl->endpoints[cursor].active && impl->endpoints[cursor].fd == fd) return SALTS_EALREADY;
  if (impl->free_endpoint_count == 0u) return SALTS_ENOBUFS;
  index = impl->free_endpoints[--impl->free_endpoint_count];
  endpoint = &impl->endpoints[index];
  endpoint->fd = fd;
  endpoint->generation = uring_next_generation(endpoint->generation);
  endpoint->active_requests = 0u;
  endpoint->read_lane = (salts_io_uring_lane){SALTS_IO_URING_INDEX_NONE, SALTS_IO_URING_INDEX_NONE};
  endpoint->write_lane =
      (salts_io_uring_lane){SALTS_IO_URING_INDEX_NONE, SALTS_IO_URING_INDEX_NONE};
  endpoint->resource_kind = resource_kind;
  endpoint->connected = connected;
  endpoint->connect_active = false;
  endpoint->active = true;
  ++impl->endpoint_count;
  *out_endpoint = (native_io_endpoint){index + 1u, endpoint->generation};
  return SALTS_OK;
}

static int uring_attach_socket(salts_io_impl *base, uintptr_t native_socket,
                               native_io_endpoint *out_endpoint) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  salts_io_resource_kind resource_kind;
  struct sockaddr_storage peer_address;
  socklen_t peer_address_length = (socklen_t)sizeof(peer_address);
  bool connected = false;
  int socket_type = 0;
  socklen_t option_length = (socklen_t)sizeof(socket_type);
  if (!impl->admission_open) return SALTS_ESHUTDOWN;
  if (native_socket > (uintptr_t)INT_MAX) return SALTS_EINVAL;
  if (getsockopt((int)native_socket, SOL_SOCKET, SO_TYPE, &socket_type, &option_length) != 0)
    return -errno;
  if (socket_type == SOCK_STREAM)
    resource_kind = SALTS_IO_RESOURCE_STREAM_SOCKET;
  else if (socket_type == SOCK_DGRAM)
    resource_kind = SALTS_IO_RESOURCE_DATAGRAM_SOCKET;
  else
    return SALTS_ENOTSUP;
  if (resource_kind == SALTS_IO_RESOURCE_STREAM_SOCKET) {
    if (getpeername((int)native_socket, (struct sockaddr *)&peer_address, &peer_address_length) == 0)
      connected = true;
    else if (errno != ENOTCONN)
      return -errno;
  }
  return uring_attach_endpoint(impl, (int)native_socket, resource_kind, connected, out_endpoint);
}

static int uring_attach_pipe(salts_io_impl *base, uintptr_t native_handle, uint32_t flags,
                             native_io_endpoint *out_endpoint) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  struct stat descriptor_stat;
  int status;
  int fd;
  if (!impl->admission_open) return SALTS_ESHUTDOWN;
  if (flags != NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE) return SALTS_EINVAL;
  if (native_handle > (uintptr_t)INT_MAX) return SALTS_EINVAL;
  fd = (int)native_handle;
  do {
    status = fstat(fd, &descriptor_stat);
  } while (status < 0 && errno == EINTR);
  if (status < 0) return -errno;
  if (!S_ISFIFO(descriptor_stat.st_mode)) return SALTS_EINVAL;
  return uring_attach_endpoint(impl, fd, SALTS_IO_RESOURCE_BYTE_PIPE, false, out_endpoint);
}

static int uring_release_endpoint(salts_io_uring_impl *impl, native_io_endpoint endpoint_handle,
                                  bool socket_endpoint) {
  salts_io_uring_endpoint *endpoint = uring_endpoint(impl, endpoint_handle);
  uint32_t index;
  if (endpoint == NULL) return SALTS_ENOENT;
  if (socket_endpoint ? !native_io_resource_kind_is_socket(endpoint->resource_kind)
                      : endpoint->resource_kind != SALTS_IO_RESOURCE_BYTE_PIPE)
    return SALTS_EINVAL;
  if (endpoint->active_requests != 0u) return SALTS_EBUSY;
  index = endpoint_handle.slot - 1u;
  endpoint->active = false;
  endpoint->fd = -1;
  endpoint->resource_kind = (salts_io_resource_kind)0;
  endpoint->connected = false;
  endpoint->connect_active = false;
  impl->free_endpoints[impl->free_endpoint_count++] = index;
  --impl->endpoint_count;
  return SALTS_OK;
}

static int uring_release_socket(salts_io_impl *base, native_io_endpoint endpoint_handle) {
  return uring_release_endpoint((salts_io_uring_impl *)base, endpoint_handle, true);
}

static int uring_release_pipe(salts_io_impl *base, native_io_endpoint endpoint_handle) {
  return uring_release_endpoint((salts_io_uring_impl *)base, endpoint_handle, false);
}

static int uring_admit(salts_io_impl *base, const native_io_operation *operation,
                       native_io_request *out_request, bool prepared) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  salts_io_uring_endpoint *endpoint;
  salts_io_uring_request_record *request;
  salts_io_uring_lane *lane;
  uint32_t index;
  int status;
  if (!impl->admission_open) return SALTS_ESHUTDOWN;
  endpoint = uring_endpoint(impl, operation->endpoint);
  if (endpoint == NULL) return SALTS_ENOENT;
  if (native_io_operation_resource_kind(operation->kind) != endpoint->resource_kind)
    return SALTS_EINVAL;
  if (operation->kind == NATIVE_IO_OPERATION_STREAM_CONNECT) {
    if (endpoint->connected || endpoint->connect_active) return SALTS_EALREADY;
    if (endpoint->active_requests != 0u) return SALTS_EBUSY;
  } else if (operation->kind == NATIVE_IO_OPERATION_STREAM_RECV ||
             operation->kind == NATIVE_IO_OPERATION_STREAM_SEND) {
    if (endpoint->connect_active) return SALTS_EBUSY;
    if (!endpoint->connected) return SALTS_EINVAL;
  }
  if (impl->free_request_count == 0u) {
    uring_counter_increment(&impl->rejected_full);
    return SALTS_ENOBUFS;
  }
  index = impl->free_requests[--impl->free_request_count];
  request = &impl->requests[index];
  request->phase = SALTS_IO_URING_PENDING;
  request->request =
      (native_io_request){index + 1u, uring_next_generation(request->request.generation)};
  request->endpoint = operation->endpoint;
  request->operation = *operation;
  request->native_token = uring_request_token(index, request->request.generation);
  request->completion = (native_io_completion){0};
  request->previous = SALTS_IO_URING_INDEX_NONE;
  request->next = SALTS_IO_URING_INDEX_NONE;
  request->write_lane = uring_is_write(operation->kind);
  request->in_flight = false;
  request->cancel_requested = false;
  ++endpoint->active_requests;
  request->staged = false;
  ++impl->active_requests;
  if (operation->kind == NATIVE_IO_OPERATION_STREAM_CONNECT) endpoint->connect_active = true;
  lane = uring_lane(endpoint, request->write_lane);
  uring_lane_push(impl, endpoint, index);
  if (lane->head == index) {
    if (prepared) {
      uring_stage(impl, index);
      status = SALTS_OK;
    } else status = uring_start_request(impl, request, endpoint->fd);
    if (status != SALTS_OK) {
      uring_lane_remove(impl, endpoint, index);
      endpoint->connect_active = false;
      uring_release_request(impl, request, index);
      uring_counter_increment(&impl->native_submit_errors);
      return status;
    }
  }
  uring_counter_increment(&impl->submitted);
  *out_request = request->request;
  return SALTS_OK;
}

static int uring_submit(salts_io_impl *base, const native_io_operation *operation,
                        native_io_request *out_request) {
  return uring_admit(base, operation, out_request, false);
}

static int uring_prepare(salts_io_impl *base, const native_io_operation *operation,
                         native_io_request *out_request) {
  return uring_admit(base, operation, out_request, true);
}

static int uring_cancel(salts_io_impl *base, native_io_request request_handle) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  salts_io_uring_request_record *request = uring_request(impl, request_handle);
  salts_io_uring_endpoint *endpoint;
  struct io_uring_sqe sqe;
  uint32_t index;
  int status;
  if (request == NULL) return SALTS_ENOENT;
  if (request->phase == SALTS_IO_URING_TERMINAL) return SALTS_EALREADY;
  if (request->cancel_requested) return SALTS_EALREADY;
  request->cancel_requested = true;
  if (!request->in_flight) {
    endpoint = uring_endpoint(impl, request->endpoint);
    if (endpoint == NULL) return SALTS_ENOENT;
    index = request->request.slot - 1u;
    salts_io_uring_lane *lane = uring_lane(endpoint, request->write_lane);
    uring_unstage(impl, index);
    uring_lane_remove(impl, endpoint, index);
    uring_queue_terminal(impl, request, index, -ECANCELED);
    uring_start_lane(impl, lane);
    return SALTS_OK;
  }
  memset(&sqe, 0, sizeof(sqe));
  sqe.opcode = IORING_OP_ASYNC_CANCEL;
  sqe.fd = -1;
  sqe.addr = request->native_token;
  sqe.user_data = SALTS_IO_URING_CANCEL_TOKEN;
  status = uring_publish_sqe(impl, &sqe);
  if (status != SALTS_OK) {
    request->cancel_requested = false;
    uring_counter_increment(&impl->native_cancel_errors);
    return status;
  }
  return SALTS_OK;
}

static int uring_process_cq(salts_io_uring_impl *impl, bool *out_saw_wake) {
  unsigned head = atomic_load_explicit((_Atomic unsigned *)impl->cq_head, memory_order_relaxed);
  const unsigned tail =
      atomic_load_explicit((_Atomic unsigned *)impl->cq_tail, memory_order_acquire);
  while (head != tail) {
    const struct io_uring_cqe *cqe = &impl->cqes[head & *impl->cq_mask];
    const uint64_t token = cqe->user_data;
    const int result = cqe->res;
    ++head;
    if (token == SALTS_IO_URING_CANCEL_TOKEN) {
      if (result < 0 && result != -ENOENT && result != -EALREADY)
        uring_counter_increment(&impl->native_cancel_errors);
      continue;
    }
    if (token == SALTS_IO_URING_WAKE_TOKEN) {
      uint64_t wake_count;
      ssize_t read_status;
      int arm_status;
      impl->wake_armed = false;
      if (result < 0) {
        /* An internal poll can be cancelled when the task that re-armed it
         * exits. This is a control-path capability loss, not a user I/O
         * failure. Preserve wake semantics with the outer poll fallback. */
        if (uring_wake_can_fallback(result)) {
          impl->ring_wake = false;
          continue;
        }
        atomic_store_explicit((_Atomic unsigned *)impl->cq_head, head, memory_order_release);
        return result;
      }
      do {
        read_status = read(impl->wake_fd, &wake_count, sizeof(wake_count));
      } while (read_status < 0 && errno == EINTR);
      if (read_status < 0 && errno != EAGAIN) {
        atomic_store_explicit((_Atomic unsigned *)impl->cq_head, head, memory_order_release);
        return -errno;
      }
      atomic_store_explicit(&impl->wake_pending, false, memory_order_release);
      arm_status = uring_arm_wake(impl);
      if (arm_status != SALTS_OK) {
        if (uring_wake_can_fallback(arm_status)) {
          impl->ring_wake = false;
        } else {
          atomic_store_explicit((_Atomic unsigned *)impl->cq_head, head, memory_order_release);
          return arm_status;
        }
      }
      if (out_saw_wake != NULL) *out_saw_wake = true;
      continue;
    }
    {
      salts_io_uring_request_record *request = uring_record_for_token(impl, token);
      if (request != NULL) {
        const uint32_t index = request->request.slot - 1u;
        salts_io_uring_endpoint *endpoint = uring_endpoint(impl, request->endpoint);
        salts_io_uring_lane *lane;
        if (endpoint == NULL) continue;
        lane = uring_lane(endpoint, request->write_lane);
        request->in_flight = false;
        uring_lane_remove(impl, endpoint, index);
        uring_queue_terminal(impl, request, index, result);
        uring_start_lane(impl, lane);
      }
    }
  }
  atomic_store_explicit((_Atomic unsigned *)impl->cq_head, head, memory_order_release);
  return SALTS_OK;
}

static void uring_drain_terminals(salts_io_uring_impl *impl, native_io_completion *events,
                                  size_t limit, size_t *out_count) {
  while (impl->terminal_count != 0u && *out_count < limit) {
    const uint32_t index = impl->terminal_requests[impl->terminal_head];
    salts_io_uring_request_record *request = &impl->requests[index];
    events[*out_count] = request->completion;
    ++*out_count;
    impl->terminal_head = (impl->terminal_head + 1u) % impl->request_capacity;
    --impl->terminal_count;
    uring_release_request(impl, request, index);
  }
}

static int uring_progress(salts_io_uring_impl *impl, bool *out_saw_wake) {
  int status = uring_process_cq(impl, out_saw_wake);
  if (status != SALTS_OK) return status;
  while (impl->staged_head != SALTS_IO_URING_INDEX_NONE) {
    status = uring_flush(&impl->base);
    if (status != SALTS_OK) return status;
    /* enter can complete a send synchronously. Inspect its CQE before returning;
     * promoted lane heads still join the next bounded submission batch. */
    status = uring_process_cq(impl, out_saw_wake);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int uring_enter_wait(salts_io_uring_impl *impl, unsigned submit, uint32_t timeout_ms) {
  if (timeout_ms == UINT32_MAX)
    return uring_enter(impl, submit, 1u, IORING_ENTER_GETEVENTS);
#if defined(IORING_ENTER_EXT_ARG) && defined(IORING_FEAT_EXT_ARG)
  if (impl->ext_arg_wait) {
    struct __kernel_timespec timeout = {
        .tv_sec = (long long)(timeout_ms / 1000u),
        .tv_nsec = (long long)(timeout_ms % 1000u) * 1000000ll};
    struct io_uring_getevents_arg argument;
    memset(&argument, 0, sizeof(argument));
    argument.ts = (uint64_t)(uintptr_t)&timeout;
    return uring_enter_arg_once(impl, submit, 1u,
                                IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG,
                                &argument, sizeof(argument));
  }
#else
  (void)impl;
  (void)submit;
#endif
  return SALTS_ENOTSUP;
}

/* Common modern-kernel path: publish every eligible lane head and combine
 * submission with the completion wait in one io_uring_enter call. The ring is
 * sized above request_capacity, so all staged user heads normally fit at once.
 * A short/error enter commits only the kernel-consumed prefix and rolls the
 * userspace SQ tail back over the unconsumed suffix. */
static int uring_submit_staged_and_wait(salts_io_uring_impl *impl, uint32_t timeout_ms) {
  const unsigned head =
      atomic_load_explicit((_Atomic unsigned *)impl->sq_head, memory_order_acquire);
  const unsigned tail =
      atomic_load_explicit((_Atomic unsigned *)impl->sq_tail, memory_order_relaxed);
  const unsigned available = *impl->sq_entries - (tail - head);
  uint32_t index = impl->staged_head;
  unsigned count = 0u;
  bool pipe_write = false;
  salts_io_uring_sigpipe_guard guard = {0};
  int status;

  while (index != SALTS_IO_URING_INDEX_NONE) {
    salts_io_uring_request_record *request;
    unsigned slot;
    if (count == available) return SALTS_EBUSY;
    request = &impl->requests[index];
    slot = (tail + count) & *impl->sq_mask;
    uring_prepare_operation(request, &impl->sqes[slot],
                            impl->endpoints[request->endpoint.slot - 1u].fd);
    impl->sq_array[slot] = slot;
    pipe_write |= request->operation.kind == NATIVE_IO_OPERATION_PIPE_WRITE;
    index = request->staged_next;
    ++count;
  }

  status = pipe_write ? uring_sigpipe_guard_begin(&guard) : SALTS_OK;
  if (status != SALTS_OK) return status;
  if (count != 0u)
    atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, tail + count, memory_order_release);

  status = uring_enter_wait(impl, count, timeout_ms);
  {
    const unsigned next_head =
        atomic_load_explicit((_Atomic unsigned *)impl->sq_head, memory_order_acquire);
    const unsigned consumed = next_head - head;
    if (consumed > count) {
      uring_sigpipe_guard_end(&guard);
      return SALTS_EPROTO;
    }
    for (unsigned cursor = 0u; cursor < consumed; ++cursor) {
      const uint32_t accepted = impl->staged_head;
      uring_unstage(impl, accepted);
      impl->requests[accepted].in_flight = true;
    }
    if (consumed != count)
      atomic_store_explicit((_Atomic unsigned *)impl->sq_tail, next_head, memory_order_release);
  }
  uring_sigpipe_guard_end(&guard);

  if (status == -EINTR || status == -ETIME) return status;
  if (status < 0) {
    if (impl->staged_head != SALTS_IO_URING_INDEX_NONE) uring_fail_staged(impl, status);
    return status;
  }
  return SALTS_OK;
}

static int uring_poll_fallback(salts_io_uring_impl *impl, uint32_t timeout_ms,
                               bool *out_saw_wake) {
  struct pollfd descriptors[2] = {
      {impl->ring_fd, POLLIN, 0},
      {impl->wake_fd, POLLIN, 0}};
  const int native_timeout = timeout_ms == UINT32_MAX         ? -1
                             : timeout_ms > (uint32_t)INT_MAX ? INT_MAX
                                                              : (int)timeout_ms;
  const int status = poll(descriptors, 2u, native_timeout);
  if (status == 0) return SALTS_ETIMEDOUT;
  if (status < 0) return errno == EINTR ? -EINTR : -errno;
  if ((descriptors[0].revents & (POLLERR | POLLNVAL)) != 0 ||
      (descriptors[1].revents & (POLLERR | POLLNVAL)) != 0)
    return SALTS_EIO;
  if ((descriptors[1].revents & POLLIN) != 0) {
    uint64_t wake_count;
    ssize_t read_status;
    do {
      read_status = read(impl->wake_fd, &wake_count, sizeof(wake_count));
    } while (read_status < 0 && errno == EINTR);
    if (read_status < 0 && errno != EAGAIN) return -errno;
    atomic_store_explicit(&impl->wake_pending, false, memory_order_release);
    if (out_saw_wake != NULL) *out_saw_wake = true;
  }
  return SALTS_OK;
}

static int uring_observe(salts_io_impl *base, native_io_completion *events, size_t event_capacity,
                         uint32_t timeout_ms, size_t *out_count) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  const size_t limit = event_capacity < impl->completion_batch_capacity
                           ? event_capacity
                           : impl->completion_batch_capacity;
  const uint64_t started_ms = salts_monotonic_ms();
  bool saw_wake = false;
  int status = uring_process_cq(impl, &saw_wake);
  if (status != SALTS_OK) return status;

  /* Polling observe is still a progress boundary: prepared operations must be
   * submitted even when the caller does not want to block. */
  if (impl->staged_head != SALTS_IO_URING_INDEX_NONE) {
    status = uring_progress(impl, &saw_wake);
    if (status != SALTS_OK) return status;
  }
  uring_drain_terminals(impl, events, limit, out_count);
  if (*out_count != 0u || saw_wake) return SALTS_OK;
  if (timeout_ms == 0u) return SALTS_ETIMEDOUT;

  for (;;) {
    uint32_t wait_timeout = native_io_remaining_timeout(started_ms, timeout_ms);
    if (timeout_ms == UINT32_MAX) wait_timeout = UINT32_MAX;
    if (wait_timeout == 0u) return SALTS_ETIMEDOUT;
    saw_wake = false;

    if (!impl->ring_wake || (wait_timeout != UINT32_MAX && !impl->ext_arg_wait)) {
      status = uring_progress(impl, &saw_wake);
      if (status != SALTS_OK) return status;
      uring_drain_terminals(impl, events, limit, out_count);
      if (*out_count != 0u || saw_wake) return SALTS_OK;
      status = uring_poll_fallback(impl, wait_timeout, &saw_wake);
      if (status == SALTS_ETIMEDOUT) return SALTS_ETIMEDOUT;
      if (status == -EINTR) continue;
      if (status != SALTS_OK) return status;
    } else {
      status = uring_submit_staged_and_wait(impl, wait_timeout);
#if defined(IORING_ENTER_EXT_ARG) && defined(IORING_FEAT_EXT_ARG)
      if (status == -EINVAL && wait_timeout != UINT32_MAX && impl->ext_arg_wait) {
        impl->ext_arg_wait = false;
        continue;
      }
#endif
      if (status != SALTS_OK && status != -EINTR && status != -ETIME) return status;
    }

    {
      int cq_status = uring_process_cq(impl, &saw_wake);
      if (cq_status != SALTS_OK) return cq_status;
    }

    if (impl->terminal_count != 0u || saw_wake) {
      if (impl->staged_head != SALTS_IO_URING_INDEX_NONE) {
        const int progress_status = uring_progress(impl, &saw_wake);
        if (progress_status != SALTS_OK) return progress_status;
      }
      uring_drain_terminals(impl, events, limit, out_count);
      if (*out_count != 0u || saw_wake) return SALTS_OK;
    }

    if (status == -ETIME) return SALTS_ETIMEDOUT;
    if (status == -EINTR) continue;
    if (native_io_remaining_timeout(started_ms, timeout_ms) == 0u) {
      if (impl->staged_head != SALTS_IO_URING_INDEX_NONE) {
        const int flush_status = uring_flush(&impl->base);
        if (flush_status != SALTS_OK) return flush_status;
      }
      return SALTS_ETIMEDOUT;
    }
  }
}

static int uring_wake(salts_io_impl *base) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  const uint64_t signal = 1u;
  bool expected = false;
  ssize_t status;
  if (!impl->admission_open) return SALTS_ESHUTDOWN;
  if (!atomic_compare_exchange_strong_explicit(&impl->wake_pending, &expected, true,
                                               memory_order_acq_rel, memory_order_acquire))
    return SALTS_OK;
  do {
    status = write(impl->wake_fd, &signal, sizeof(signal));
  } while (status < 0 && errno == EINTR);
  if (status == (ssize_t)sizeof(signal) || (status < 0 && errno == EAGAIN)) return SALTS_OK;
  atomic_store_explicit(&impl->wake_pending, false, memory_order_release);
  return status < 0 ? -errno : SALTS_EIO;
}

static int uring_close(salts_io_impl *base) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  if (!impl->admission_open) return SALTS_EALREADY;
  impl->admission_open = false;
  return SALTS_OK;
}

static void uring_unmap(salts_io_uring_impl *impl) {
  if (impl->sqes != NULL && impl->sqes != MAP_FAILED) (void)munmap(impl->sqes, impl->sqes_size);
  if (!impl->single_mmap && impl->cq_ring != NULL && impl->cq_ring != MAP_FAILED)
    (void)munmap(impl->cq_ring, impl->cq_ring_size);
  if (impl->sq_ring != NULL && impl->sq_ring != MAP_FAILED)
    (void)munmap(impl->sq_ring, impl->sq_ring_size);
}

static int uring_destroy(salts_io_impl *base) {
  salts_io_uring_impl *impl = (salts_io_uring_impl *)base;
  if (impl->admission_open || impl->active_requests != 0u || impl->endpoint_count != 0u)
    return SALTS_EBUSY;
  uring_unmap(impl);
  if (impl->ring_fd >= 0) (void)close(impl->ring_fd);
  if (impl->wake_fd >= 0) (void)close(impl->wake_fd);
  free(impl->free_requests);
  free(impl->free_endpoints);
  free(impl->terminal_requests);
  free(impl->requests);
  free(impl->endpoints);
  free(impl);
  return SALTS_OK;
}

static bool uring_get_stats(const salts_io_impl *base, native_io_backend_stats *out_stats) {
  const salts_io_uring_impl *impl = (const salts_io_uring_impl *)base;
  *out_stats = (native_io_backend_stats){impl->endpoint_capacity,
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

static const salts_io_impl_ops uring_ops = {
    uring_attach_socket, uring_release_socket, uring_submit,      uring_cancel,
    uring_observe,       uring_wake,           uring_close,       uring_destroy,
    uring_get_stats,     uring_attach_pipe,    uring_release_pipe,
    uring_prepare,       uring_flush};

static bool uring_mapped_extent(size_t offset, size_t count, size_t element_size, size_t *out) {
  if (element_size == 0u || count > (SIZE_MAX - offset) / element_size) return false;
  *out = offset + count * element_size;
  return true;
}

static bool uring_field_fits(size_t offset, size_t field_size, size_t mapped_size) {
  return offset <= mapped_size && field_size <= mapped_size - offset;
}

static void *uring_field(void *mapping, unsigned offset) {
  return (void *)((unsigned char *)mapping + (size_t)offset);
}

static int uring_map(salts_io_uring_impl *impl, const struct io_uring_params *params) {
  size_t shared_size;
  if (params->sq_entries == 0u || params->cq_entries == 0u ||
      !uring_mapped_extent(params->sq_off.array, params->sq_entries, sizeof(unsigned),
                           &impl->sq_ring_size) ||
      !uring_mapped_extent(params->cq_off.cqes, params->cq_entries, sizeof(struct io_uring_cqe),
                           &impl->cq_ring_size) ||
      !uring_mapped_extent(0u, params->sq_entries, sizeof(struct io_uring_sqe), &impl->sqes_size) ||
      !uring_field_fits(params->sq_off.head, sizeof(unsigned), impl->sq_ring_size) ||
      !uring_field_fits(params->sq_off.tail, sizeof(unsigned), impl->sq_ring_size) ||
      !uring_field_fits(params->sq_off.ring_mask, sizeof(unsigned), impl->sq_ring_size) ||
      !uring_field_fits(params->sq_off.ring_entries, sizeof(unsigned), impl->sq_ring_size) ||
      !uring_field_fits(params->sq_off.array, sizeof(unsigned), impl->sq_ring_size) ||
      !uring_field_fits(params->cq_off.head, sizeof(unsigned), impl->cq_ring_size) ||
      !uring_field_fits(params->cq_off.tail, sizeof(unsigned), impl->cq_ring_size) ||
      !uring_field_fits(params->cq_off.ring_mask, sizeof(unsigned), impl->cq_ring_size) ||
      !uring_field_fits(params->cq_off.cqes, sizeof(struct io_uring_cqe), impl->cq_ring_size))
    return SALTS_ERANGE;
  impl->single_mmap = (params->features & IORING_FEAT_SINGLE_MMAP) != 0u;
  shared_size = impl->sq_ring_size > impl->cq_ring_size ? impl->sq_ring_size : impl->cq_ring_size;
  if (impl->single_mmap) impl->sq_ring_size = impl->cq_ring_size = shared_size;
  impl->sq_ring = mmap(NULL, impl->sq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                       impl->ring_fd, IORING_OFF_SQ_RING);
  if (impl->sq_ring == MAP_FAILED) return -errno;
  if (impl->single_mmap) {
    impl->cq_ring = impl->sq_ring;
  } else {
    impl->cq_ring = mmap(NULL, impl->cq_ring_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, impl->ring_fd, IORING_OFF_CQ_RING);
    if (impl->cq_ring == MAP_FAILED) return -errno;
  }
  impl->sqes = mmap(NULL, impl->sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                    impl->ring_fd, IORING_OFF_SQES);
  if (impl->sqes == MAP_FAILED) return -errno;
  impl->sq_head = uring_field(impl->sq_ring, params->sq_off.head);
  impl->sq_tail = uring_field(impl->sq_ring, params->sq_off.tail);
  impl->sq_mask = uring_field(impl->sq_ring, params->sq_off.ring_mask);
  impl->sq_entries = uring_field(impl->sq_ring, params->sq_off.ring_entries);
  impl->sq_array = uring_field(impl->sq_ring, params->sq_off.array);
  impl->cq_head = uring_field(impl->cq_ring, params->cq_off.head);
  impl->cq_tail = uring_field(impl->cq_ring, params->cq_off.tail);
  impl->cq_mask = uring_field(impl->cq_ring, params->cq_off.ring_mask);
  impl->cqes = uring_field(impl->cq_ring, params->cq_off.cqes);
  return SALTS_OK;
}

static void uring_free_partial(salts_io_uring_impl *impl) {
  uring_unmap(impl);
  if (impl->ring_fd >= 0) (void)close(impl->ring_fd);
  if (impl->wake_fd >= 0) (void)close(impl->wake_fd);
  free(impl->free_requests);
  free(impl->free_endpoints);
  free(impl->terminal_requests);
  free(impl->requests);
  free(impl->endpoints);
  free(impl);
}

int salts_io_uring_backend_init(native_io_backend *backend, const native_io_backend_config *config) {
  salts_io_uring_impl *impl;
  struct io_uring_params params;
  unsigned entries;
  int status;
  if (config->kind != NATIVE_IO_BACKEND_IO_URING) return SALTS_ENOTSUP;
  if (config->request_capacity > UINT32_MAX / 2u ||
      config->endpoint_capacity > SIZE_MAX / sizeof(salts_io_uring_endpoint) ||
      config->request_capacity > SIZE_MAX / sizeof(salts_io_uring_request_record) ||
      config->endpoint_capacity > SIZE_MAX / sizeof(uint32_t) ||
      config->request_capacity > SIZE_MAX / sizeof(uint32_t))
    return SALTS_ERANGE;
  entries = (unsigned)config->request_capacity * 2u;
  impl = (salts_io_uring_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->ring_fd = -1;
  impl->wake_fd = -1;
  impl->staged_head = impl->staged_tail = SALTS_IO_URING_INDEX_NONE;
  impl->sq_ring = MAP_FAILED;
  impl->cq_ring = MAP_FAILED;
  impl->sqes = MAP_FAILED;
  impl->endpoints =
      (salts_io_uring_endpoint *)calloc(config->endpoint_capacity, sizeof(*impl->endpoints));
  impl->requests =
      (salts_io_uring_request_record *)calloc(config->request_capacity, sizeof(*impl->requests));
  impl->free_endpoints = (uint32_t *)calloc(config->endpoint_capacity, sizeof(uint32_t));
  impl->free_requests = (uint32_t *)calloc(config->request_capacity, sizeof(uint32_t));
  impl->terminal_requests = (uint32_t *)calloc(config->request_capacity, sizeof(uint32_t));
  if (impl->endpoints == NULL || impl->requests == NULL || impl->free_endpoints == NULL ||
      impl->free_requests == NULL || impl->terminal_requests == NULL) {
    uring_free_partial(impl);
    return SALTS_ENOMEM;
  }
  impl->base.ops = &uring_ops;
  impl->base.kind = config->kind;
  impl->endpoint_capacity = config->endpoint_capacity;
  impl->request_capacity = config->request_capacity;
  impl->completion_batch_capacity = config->completion_batch_capacity;
  impl->ring_wake = true;
  impl->free_endpoint_count = config->endpoint_capacity;
  impl->free_request_count = config->request_capacity;
  impl->admission_open = true;
  atomic_init(&impl->wake_pending, false);
  for (size_t index = 0u; index < config->endpoint_capacity; ++index) {
    impl->free_endpoints[index] = (uint32_t)(config->endpoint_capacity - index - 1u);
    impl->endpoints[index].fd = -1;
  }
  for (size_t index = 0u; index < config->request_capacity; ++index)
    impl->free_requests[index] = (uint32_t)(config->request_capacity - index - 1u);
  impl->wake_fd = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
  if (impl->wake_fd < 0) {
    status = -errno;
    uring_free_partial(impl);
    return status;
  }
  memset(&params, 0, sizeof(params));
  impl->ring_fd = (int)syscall(__NR_io_uring_setup, entries, &params);
  if (impl->ring_fd < 0) {
    status = -errno;
    uring_free_partial(impl);
    return status;
  }
#if defined(IORING_FEAT_EXT_ARG)
  impl->ext_arg_wait = (params.features & IORING_FEAT_EXT_ARG) != 0u;
#else
  impl->ext_arg_wait = false;
#endif
  status = uring_map(impl, &params);
  if (status != SALTS_OK) {
    uring_free_partial(impl);
    return status;
  }
  status = uring_arm_wake(impl);
  if (status != SALTS_OK) {
    if (uring_wake_can_fallback(status)) {
      impl->ring_wake = false;
    } else {
      uring_free_partial(impl);
      return status;
    }
  }
  backend->impl = impl;
  return SALTS_OK;
}
