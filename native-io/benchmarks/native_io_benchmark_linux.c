#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include "tinytest.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

typedef enum native_bench_protocol { NATIVE_BENCH_TCP = 0, NATIVE_BENCH_UDP } native_bench_protocol;

typedef enum native_bench_family {
  NATIVE_BENCH_EPOLL = 0,
  NATIVE_BENCH_IO_URING
} native_bench_family;

typedef enum native_bench_driver {
  NATIVE_BENCH_RAW = 0,
  NATIVE_BENCH_NATIVE_IO
} native_bench_driver;

enum {
  NATIVE_BENCH_SAMPLES = 20,
  NATIVE_BENCH_EXCHANGES_PER_SAMPLE = 256,
  NATIVE_BENCH_WARMUP_EXCHANGES = 64,
  NATIVE_BENCH_TIMEOUT_MS = 5000,
  NATIVE_BENCH_RING_ENTRIES = 8,
  NATIVE_BENCH_REQUEST_CAPACITY = 4,
  NATIVE_BENCH_ENDPOINT_CAPACITY = 2,
  NATIVE_BENCH_BATCH_CAPACITY = 4,
  NATIVE_BENCH_TOTAL_EXCHANGES = NATIVE_BENCH_SAMPLES * NATIVE_BENCH_EXCHANGES_PER_SAMPLE
};

static const size_t NATIVE_BENCH_TCP_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u, 65536u};
static const size_t NATIVE_BENCH_UDP_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u};

typedef struct native_bench_stages {
  uint64_t submit_ns;
  uint64_t observe_ns;
  uint64_t operations;
} native_bench_stages;

typedef struct native_bench_result {
  size_t payload_size;
  uint64_t wall_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  native_bench_stages stages;
} native_bench_result;

typedef struct native_bench_raw_ring {
  int fd;
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
} native_bench_raw_ring;

typedef struct native_bench_fixture {
  native_bench_protocol protocol;
  native_bench_family family;
  native_bench_driver driver;
  int sockets[2];
  struct sockaddr_in addresses[2];
  int epoll_fd;
  native_bench_raw_ring ring;
  turbo_io_backend backend;
  turbo_io_endpoint endpoints[2];
} native_bench_fixture;

typedef struct native_bench_raw_uring_request {
  bool receive;
  struct iovec vector;
  struct msghdr message;
  struct sockaddr_storage peer_address;
} native_bench_raw_uring_request;

static void native_bench_counter_add(uint64_t *counter, uint64_t value) {
  *counter = UINT64_MAX - *counter < value ? UINT64_MAX : *counter + value;
}

static int native_bench_error(void) { return errno == 0 ? TURBO_EIO : -errno; }

static int native_bench_bind_loopback(int fd, struct sockaddr_in *address) {
  socklen_t address_length = (socklen_t)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(fd, (const struct sockaddr *)address, sizeof(*address)) != 0)
    return native_bench_error();
  if (getsockname(fd, (struct sockaddr *)address, &address_length) != 0)
    return native_bench_error();
  return TURBO_OK;
}

static int native_bench_make_tcp_pair(native_bench_fixture *fixture) {
  int listener = -1;
  struct sockaddr_in address;
  int no_delay = 1;
  int status = TURBO_OK;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener < 0) return native_bench_error();
  status = native_bench_bind_loopback(listener, &address);
  if (status == TURBO_OK && listen(listener, 1) != 0) status = native_bench_error();
  if (status == TURBO_OK) {
    fixture->sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->sockets[0] < 0) status = native_bench_error();
  }
  if (status == TURBO_OK &&
      connect(fixture->sockets[0], (const struct sockaddr *)&address, sizeof(address)) != 0)
    status = native_bench_error();
  if (status == TURBO_OK) {
    fixture->sockets[1] = accept(listener, NULL, NULL);
    if (fixture->sockets[1] < 0) status = native_bench_error();
  }
  (void)close(listener);
  if (status == TURBO_OK &&
      (setsockopt(fixture->sockets[0], IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) !=
           0 ||
       setsockopt(fixture->sockets[1], IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) != 0))
    status = native_bench_error();
  return status;
}

static int native_bench_make_udp_pair(native_bench_fixture *fixture) {
  int status = TURBO_OK;
  fixture->sockets[0] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  fixture->sockets[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fixture->sockets[0] < 0 || fixture->sockets[1] < 0) status = native_bench_error();
  if (status == TURBO_OK)
    status = native_bench_bind_loopback(fixture->sockets[0], &fixture->addresses[0]);
  if (status == TURBO_OK)
    status = native_bench_bind_loopback(fixture->sockets[1], &fixture->addresses[1]);
  return status;
}

static bool native_bench_mapped_extent(size_t offset, size_t count, size_t element_size,
                                       size_t *out) {
  if (element_size == 0u || count > (SIZE_MAX - offset) / element_size) return false;
  *out = offset + count * element_size;
  return true;
}

static void *native_bench_ring_field(void *mapping, unsigned offset) {
  return (void *)((unsigned char *)mapping + (size_t)offset);
}

static void native_bench_raw_ring_destroy(native_bench_raw_ring *ring) {
  if (ring->sqes != NULL && ring->sqes != MAP_FAILED) (void)munmap(ring->sqes, ring->sqes_size);
  if (!ring->single_mmap && ring->cq_ring != NULL && ring->cq_ring != MAP_FAILED)
    (void)munmap(ring->cq_ring, ring->cq_ring_size);
  if (ring->sq_ring != NULL && ring->sq_ring != MAP_FAILED)
    (void)munmap(ring->sq_ring, ring->sq_ring_size);
  if (ring->fd >= 0) (void)close(ring->fd);
  memset(ring, 0, sizeof(*ring));
  ring->fd = -1;
}

static int native_bench_raw_ring_init(native_bench_raw_ring *ring) {
  struct io_uring_params params;
  size_t shared_size;
  memset(ring, 0, sizeof(*ring));
  ring->fd = -1;
  ring->sq_ring = MAP_FAILED;
  ring->cq_ring = MAP_FAILED;
  ring->sqes = MAP_FAILED;
  memset(&params, 0, sizeof(params));
  ring->fd = (int)syscall(__NR_io_uring_setup, NATIVE_BENCH_RING_ENTRIES, &params);
  if (ring->fd < 0) return native_bench_error();
  if (!native_bench_mapped_extent(params.sq_off.array, params.sq_entries, sizeof(unsigned),
                                  &ring->sq_ring_size) ||
      !native_bench_mapped_extent(params.cq_off.cqes, params.cq_entries,
                                  sizeof(struct io_uring_cqe), &ring->cq_ring_size) ||
      !native_bench_mapped_extent(0u, params.sq_entries, sizeof(struct io_uring_sqe),
                                  &ring->sqes_size)) {
    native_bench_raw_ring_destroy(ring);
    return TURBO_ERANGE;
  }
  ring->single_mmap = (params.features & IORING_FEAT_SINGLE_MMAP) != 0u;
  shared_size = ring->sq_ring_size > ring->cq_ring_size ? ring->sq_ring_size : ring->cq_ring_size;
  if (ring->single_mmap) ring->sq_ring_size = ring->cq_ring_size = shared_size;
  ring->sq_ring = mmap(NULL, ring->sq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                       ring->fd, IORING_OFF_SQ_RING);
  if (ring->sq_ring == MAP_FAILED) goto native_error;
  if (ring->single_mmap) {
    ring->cq_ring = ring->sq_ring;
  } else {
    ring->cq_ring = mmap(NULL, ring->cq_ring_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_CQ_RING);
    if (ring->cq_ring == MAP_FAILED) goto native_error;
  }
  ring->sqes = mmap(NULL, ring->sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                    ring->fd, IORING_OFF_SQES);
  if (ring->sqes == MAP_FAILED) goto native_error;
  ring->sq_head = native_bench_ring_field(ring->sq_ring, params.sq_off.head);
  ring->sq_tail = native_bench_ring_field(ring->sq_ring, params.sq_off.tail);
  ring->sq_mask = native_bench_ring_field(ring->sq_ring, params.sq_off.ring_mask);
  ring->sq_entries = native_bench_ring_field(ring->sq_ring, params.sq_off.ring_entries);
  ring->sq_array = native_bench_ring_field(ring->sq_ring, params.sq_off.array);
  ring->cq_head = native_bench_ring_field(ring->cq_ring, params.cq_off.head);
  ring->cq_tail = native_bench_ring_field(ring->cq_ring, params.cq_off.tail);
  ring->cq_mask = native_bench_ring_field(ring->cq_ring, params.cq_off.ring_mask);
  ring->cqes = native_bench_ring_field(ring->cq_ring, params.cq_off.cqes);
  return TURBO_OK;

native_error: {
  const int status = native_bench_error();
  native_bench_raw_ring_destroy(ring);
  return status;
}
}

static int native_bench_raw_ring_enter(native_bench_raw_ring *ring, unsigned submit) {
  int status;
  do {
    status = (int)syscall(__NR_io_uring_enter, ring->fd, submit, 0u, 0u, NULL, 0u);
  } while (status < 0 && errno == EINTR);
  return status < 0 ? native_bench_error() : status;
}

static int native_bench_raw_ring_submit(native_bench_raw_ring *ring,
                                        const struct io_uring_sqe *prepared) {
  const unsigned head =
      atomic_load_explicit((_Atomic unsigned *)ring->sq_head, memory_order_acquire);
  const unsigned tail =
      atomic_load_explicit((_Atomic unsigned *)ring->sq_tail, memory_order_relaxed);
  unsigned index;
  int status;
  if (tail - head >= *ring->sq_entries) return TURBO_EBUSY;
  index = tail & *ring->sq_mask;
  ring->sqes[index] = *prepared;
  ring->sq_array[index] = index;
  atomic_store_explicit((_Atomic unsigned *)ring->sq_tail, tail + 1u, memory_order_release);
  status = native_bench_raw_ring_enter(ring, 1u);
  if (status == 1) return TURBO_OK;
  atomic_store_explicit((_Atomic unsigned *)ring->sq_tail, tail, memory_order_release);
  return status < 0 ? status : TURBO_EIO;
}

static int native_bench_fixture_init(native_bench_fixture *fixture, native_bench_protocol protocol,
                                     native_bench_family family, native_bench_driver driver) {
  turbo_io_backend_config config;
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  fixture->family = family;
  fixture->driver = driver;
  fixture->sockets[0] = -1;
  fixture->sockets[1] = -1;
  fixture->epoll_fd = -1;
  fixture->ring.fd = -1;
  if (driver == NATIVE_BENCH_NATIVE_IO) {
    config = (turbo_io_backend_config){
        family == NATIVE_BENCH_EPOLL ? TURBO_IO_BACKEND_EPOLL : TURBO_IO_BACKEND_IO_URING,
        NATIVE_BENCH_ENDPOINT_CAPACITY, NATIVE_BENCH_REQUEST_CAPACITY, NATIVE_BENCH_BATCH_CAPACITY};
    status = native_io_init(&fixture->backend, &config);
    if (status != TURBO_OK) return status;
  } else if (family == NATIVE_BENCH_EPOLL) {
    fixture->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (fixture->epoll_fd < 0) return native_bench_error();
  } else {
    status = native_bench_raw_ring_init(&fixture->ring);
    if (status != TURBO_OK) return status;
  }
  status = protocol == NATIVE_BENCH_TCP ? native_bench_make_tcp_pair(fixture)
                                        : native_bench_make_udp_pair(fixture);
  if (status != TURBO_OK) return status;
  if (driver == NATIVE_BENCH_NATIVE_IO) {
    for (size_t index = 0u; index < 2u; ++index) {
      status = native_io_attach_socket(&fixture->backend, (uintptr_t)fixture->sockets[index],
                                              &fixture->endpoints[index]);
      if (status != TURBO_OK) return status;
    }
  }
  return TURBO_OK;
}

static int native_bench_fixture_destroy(native_bench_fixture *fixture) {
  int status = TURBO_OK;
  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->sockets[index] >= 0) {
      if (close(fixture->sockets[index]) != 0 && status == TURBO_OK) status = native_bench_error();
      fixture->sockets[index] = -1;
      if (fixture->driver == NATIVE_BENCH_NATIVE_IO &&
          turbo_io_endpoint_valid(fixture->endpoints[index])) {
        const int release_status =
            native_io_release_socket(&fixture->backend, fixture->endpoints[index]);
        if (status == TURBO_OK && release_status != TURBO_OK) status = release_status;
      }
    }
  }
  if (fixture->epoll_fd >= 0) {
    if (close(fixture->epoll_fd) != 0 && status == TURBO_OK) status = native_bench_error();
    fixture->epoll_fd = -1;
  }
  if (fixture->ring.fd >= 0) native_bench_raw_ring_destroy(&fixture->ring);
  if (fixture->backend.impl != NULL) {
    const int close_status = native_io_close(&fixture->backend);
    const int destroy_status =
        close_status == TURBO_OK ? native_io_destroy(&fixture->backend) : close_status;
    if (status == TURBO_OK && destroy_status != TURBO_OK) status = destroy_status;
  }
  return status;
}

static int native_bench_epoll_update(native_bench_fixture *fixture, int fd, uint64_t token,
                                     uint32_t old_events, uint32_t new_events) {
  struct epoll_event event = {0};
  int operation;
  int status;
  if (old_events == new_events) return TURBO_OK;
  operation = old_events == 0u ? EPOLL_CTL_ADD : new_events == 0u ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
  event.events = new_events | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
  event.data.u64 = token;
  do {
    status =
        epoll_ctl(fixture->epoll_fd, operation, fd, operation == EPOLL_CTL_DEL ? NULL : &event);
  } while (status < 0 && errno == EINTR);
  return status == 0 || (operation == EPOLL_CTL_DEL && errno == ENOENT) ? TURBO_OK
                                                                        : native_bench_error();
}

static ssize_t native_bench_try_receive(native_bench_fixture *fixture, size_t destination_index,
                                        unsigned char *buffer, size_t length) {
  if (fixture->protocol == NATIVE_BENCH_TCP)
    return recv(fixture->sockets[destination_index], buffer, length, MSG_DONTWAIT);
  {
    struct sockaddr_storage peer;
    socklen_t peer_length = (socklen_t)sizeof(peer);
    return recvfrom(fixture->sockets[destination_index], buffer, length, MSG_DONTWAIT,
                    (struct sockaddr *)&peer, &peer_length);
  }
}

static ssize_t native_bench_try_send(native_bench_fixture *fixture, size_t source_index,
                                     size_t destination_index, const unsigned char *buffer,
                                     size_t length) {
  if (fixture->protocol == NATIVE_BENCH_TCP)
    return send(fixture->sockets[source_index], buffer, length, MSG_DONTWAIT | MSG_NOSIGNAL);
  return sendto(fixture->sockets[source_index], buffer, length, MSG_DONTWAIT | MSG_NOSIGNAL,
                (const struct sockaddr *)&fixture->addresses[destination_index],
                sizeof(fixture->addresses[destination_index]));
}

static int native_bench_raw_epoll_transfer(native_bench_fixture *fixture, size_t source_index,
                                           size_t destination_index, const unsigned char *sent,
                                           unsigned char *received, size_t length,
                                           native_bench_stages *stages) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  uint32_t source_events = 0u;
  uint32_t destination_events = 0u;
  bool first_attempt = true;
  stages->operations += 2u;
  while (sent_offset < length || received_offset < length) {
    uint64_t started = turbo_hrtime();
    ssize_t result;
    int status;
    if (received_offset < length) {
      do {
        result = native_bench_try_receive(fixture, destination_index, received + received_offset,
                                          length - received_offset);
      } while (result < 0 && errno == EINTR);
      if (result > 0) received_offset += (size_t)result;
      else if (result == 0 && fixture->protocol == NATIVE_BENCH_TCP) return TURBO_EOF;
      else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return native_bench_error();
    }
    if (sent_offset < length) {
      do {
        result = native_bench_try_send(fixture, source_index, destination_index, sent + sent_offset,
                                       length - sent_offset);
      } while (result < 0 && errno == EINTR);
      if (result > 0) sent_offset += (size_t)result;
      else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return native_bench_error();
    }
    {
      const uint32_t next_destination = received_offset < length ? EPOLLIN : 0u;
      const uint32_t next_source = sent_offset < length ? EPOLLOUT : 0u;
      status = native_bench_epoll_update(fixture, fixture->sockets[destination_index], 2u,
                                         destination_events, next_destination);
      if (status != TURBO_OK) return status;
      destination_events = next_destination;
      status = native_bench_epoll_update(fixture, fixture->sockets[source_index], 1u, source_events,
                                         next_source);
      if (status != TURBO_OK) return status;
      source_events = next_source;
    }
    native_bench_counter_add(first_attempt ? &stages->submit_ns : &stages->observe_ns,
                             turbo_hrtime() - started);
    first_attempt = false;
    if (sent_offset == length && received_offset == length) break;
    {
      struct epoll_event events[2];
      int count;
      started = turbo_hrtime();
      do {
        count = epoll_wait(fixture->epoll_fd, events, 2, NATIVE_BENCH_TIMEOUT_MS);
      } while (count < 0 && errno == EINTR);
      native_bench_counter_add(&stages->observe_ns, turbo_hrtime() - started);
      if (count < 0) return native_bench_error();
      if (count == 0) return TURBO_ETIMEDOUT;
    }
  }
  return TURBO_OK;
}

static int native_bench_raw_uring_post(native_bench_fixture *fixture,
                                       native_bench_raw_uring_request *request, int fd,
                                       void *buffer, size_t length, bool receive,
                                       const struct sockaddr_in *destination,
                                       native_bench_stages *stages) {
  struct io_uring_sqe sqe;
  uint64_t started;
  int status;
  memset(request, 0, sizeof(*request));
  memset(&sqe, 0, sizeof(sqe));
  request->receive = receive;
  sqe.fd = fd;
  sqe.user_data = (uint64_t)(uintptr_t)request;
  if (fixture->protocol == NATIVE_BENCH_TCP) {
    sqe.opcode = receive ? IORING_OP_RECV : IORING_OP_SEND;
    sqe.addr = (uint64_t)(uintptr_t)buffer;
    sqe.len = (uint32_t)length;
    if (!receive) sqe.msg_flags = MSG_NOSIGNAL;
  } else {
    request->vector.iov_base = buffer;
    request->vector.iov_len = length;
    request->message.msg_iov = &request->vector;
    request->message.msg_iovlen = 1u;
    if (receive) {
      request->message.msg_name = &request->peer_address;
      request->message.msg_namelen = sizeof(request->peer_address);
      sqe.opcode = IORING_OP_RECVMSG;
    } else {
      request->message.msg_name = (void *)destination;
      request->message.msg_namelen = sizeof(*destination);
      sqe.opcode = IORING_OP_SENDMSG;
      sqe.msg_flags = MSG_NOSIGNAL;
    }
    sqe.addr = (uint64_t)(uintptr_t)&request->message;
    sqe.len = 1u;
  }
  started = turbo_hrtime();
  status = native_bench_raw_ring_submit(&fixture->ring, &sqe);
  native_bench_counter_add(&stages->submit_ns, turbo_hrtime() - started);
  if (status == TURBO_OK) ++stages->operations;
  return status;
}

static int native_bench_raw_ring_wait(native_bench_raw_ring *ring, struct io_uring_cqe *events,
                                      size_t event_capacity, size_t *out_count,
                                      native_bench_stages *stages) {
  unsigned head;
  unsigned tail;
  uint64_t started = turbo_hrtime();
  head = atomic_load_explicit((_Atomic unsigned *)ring->cq_head, memory_order_relaxed);
  tail = atomic_load_explicit((_Atomic unsigned *)ring->cq_tail, memory_order_acquire);
  if (head == tail) {
    struct pollfd descriptor = {ring->fd, POLLIN, 0};
    int status;
    do {
      status = poll(&descriptor, 1u, NATIVE_BENCH_TIMEOUT_MS);
    } while (status < 0 && errno == EINTR);
    if (status < 0) return native_bench_error();
    if (status == 0) return TURBO_ETIMEDOUT;
    tail = atomic_load_explicit((_Atomic unsigned *)ring->cq_tail, memory_order_acquire);
  }
  while (head != tail && *out_count < event_capacity) {
    events[*out_count] = ring->cqes[head & *ring->cq_mask];
    ++*out_count;
    ++head;
  }
  atomic_store_explicit((_Atomic unsigned *)ring->cq_head, head, memory_order_release);
  native_bench_counter_add(&stages->observe_ns, turbo_hrtime() - started);
  return *out_count == 0u ? TURBO_EIO : TURBO_OK;
}

static int native_bench_raw_uring_transfer(native_bench_fixture *fixture, size_t source_index,
                                           size_t destination_index, const unsigned char *sent,
                                           unsigned char *received, size_t length,
                                           native_bench_stages *stages) {
  native_bench_raw_uring_request receive_request;
  native_bench_raw_uring_request send_request;
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool send_pending = false;
  bool receive_pending = false;
  while (sent_offset < length || received_offset < length) {
    struct io_uring_cqe events[2];
    size_t event_count = 0u;
    int status;
    if (!receive_pending && received_offset < length) {
      status = native_bench_raw_uring_post(
          fixture, &receive_request, fixture->sockets[destination_index],
          received + received_offset, length - received_offset, true, NULL, stages);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      status = native_bench_raw_uring_post(fixture, &send_request, fixture->sockets[source_index],
                                           (void *)(sent + sent_offset), length - sent_offset,
                                           false, &fixture->addresses[destination_index], stages);
      if (status != TURBO_OK) return status;
      send_pending = true;
    }
    status = native_bench_raw_ring_wait(&fixture->ring, events, 2u, &event_count, stages);
    if (status != TURBO_OK) return status;
    for (size_t index = 0u; index < event_count; ++index) {
      native_bench_raw_uring_request *request =
          (native_bench_raw_uring_request *)(uintptr_t)events[index].user_data;
      if (events[index].res < 0) return events[index].res;
      if (request == &receive_request) {
        if (fixture->protocol == NATIVE_BENCH_TCP && events[index].res == 0) return TURBO_EOF;
        received_offset += (size_t)events[index].res;
        receive_pending = false;
      } else if (request == &send_request) {
        sent_offset += (size_t)events[index].res;
        send_pending = false;
      } else {
        return TURBO_EPROTO;
      }
    }
    if (fixture->protocol == NATIVE_BENCH_UDP &&
        ((received_offset != 0u && received_offset != length) ||
         (sent_offset != 0u && sent_offset != length)))
      return TURBO_EIO;
  }
  return TURBO_OK;
}

static int native_bench_submit_operation(turbo_io_backend *backend,
                                         const turbo_io_operation *operation,
                                         turbo_io_request *request, native_bench_stages *stages) {
  const uint64_t started = turbo_hrtime();
  const int status = native_io_submit(backend, operation, request);
  native_bench_counter_add(&stages->submit_ns, turbo_hrtime() - started);
  if (status == TURBO_OK) ++stages->operations;
  return status;
}

static int native_bench_native_transfer(native_bench_fixture *fixture, size_t source_index,
                                        size_t destination_index, const unsigned char *sent,
                                        unsigned char *received, size_t length,
                                        native_bench_stages *stages) {
  struct sockaddr_storage peer_address;
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool send_pending = false;
  bool receive_pending = false;
  while (sent_offset < length || received_offset < length) {
    turbo_io_completion events[2];
    size_t event_count = 0u;
    int status;
    uint64_t started;
    if (!receive_pending && received_offset < length) {
      turbo_io_operation operation = {.kind = fixture->protocol == NATIVE_BENCH_TCP
                                                  ? TURBO_IO_TCP_RECV
                                                  : TURBO_IO_UDP_RECV_FROM,
                                      .endpoint = fixture->endpoints[destination_index],
                                      .buffer = received + received_offset,
                                      .length = length - received_offset,
                                      .user_data = 1u};
      turbo_io_request request;
      if (fixture->protocol == NATIVE_BENCH_UDP) {
        operation.address = &peer_address;
        operation.address_capacity = sizeof(peer_address);
      }
      status = native_bench_submit_operation(&fixture->backend, &operation, &request, stages);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      turbo_io_operation operation = {
          .kind = fixture->protocol == NATIVE_BENCH_TCP ? TURBO_IO_TCP_SEND : TURBO_IO_UDP_SEND_TO,
          .endpoint = fixture->endpoints[source_index],
          .buffer = (void *)(sent + sent_offset),
          .length = length - sent_offset,
          .user_data = 2u};
      turbo_io_request request;
      if (fixture->protocol == NATIVE_BENCH_UDP) {
        operation.address = &fixture->addresses[destination_index];
        operation.address_capacity = sizeof(fixture->addresses[0]);
        operation.address_length = sizeof(fixture->addresses[0]);
      }
      status = native_bench_submit_operation(&fixture->backend, &operation, &request, stages);
      if (status != TURBO_OK) return status;
      send_pending = true;
    }
    started = turbo_hrtime();
    status = native_io_observe(&fixture->backend, events, 2u, NATIVE_BENCH_TIMEOUT_MS,
                                      &event_count);
    native_bench_counter_add(&stages->observe_ns, turbo_hrtime() - started);
    if (status != TURBO_OK) return status;
    for (size_t index = 0u; index < event_count; ++index) {
      if (events[index].kind != TURBO_IO_COMPLETION_OK) return events[index].status;
      if (events[index].user_data == 1u) {
        received_offset += events[index].bytes;
        receive_pending = false;
      } else if (events[index].user_data == 2u) {
        sent_offset += events[index].bytes;
        send_pending = false;
      } else {
        return TURBO_EPROTO;
      }
    }
    if (fixture->protocol == NATIVE_BENCH_UDP &&
        ((received_offset != 0u && received_offset != length) ||
         (sent_offset != 0u && sent_offset != length)))
      return TURBO_EIO;
  }
  return TURBO_OK;
}

static int native_bench_exchange(native_bench_fixture *fixture, const unsigned char *sent,
                                 unsigned char *server_received, unsigned char *client_received,
                                 size_t length, native_bench_stages *stages) {
  int status;
  if (fixture->driver == NATIVE_BENCH_NATIVE_IO) {
    status = native_bench_native_transfer(fixture, 0u, 1u, sent, server_received, length, stages);
    if (status == TURBO_OK)
      status = native_bench_native_transfer(fixture, 1u, 0u, server_received, client_received,
                                            length, stages);
  } else if (fixture->family == NATIVE_BENCH_EPOLL) {
    status =
        native_bench_raw_epoll_transfer(fixture, 0u, 1u, sent, server_received, length, stages);
    if (status == TURBO_OK)
      status = native_bench_raw_epoll_transfer(fixture, 1u, 0u, server_received, client_received,
                                               length, stages);
  } else {
    status =
        native_bench_raw_uring_transfer(fixture, 0u, 1u, sent, server_received, length, stages);
    if (status == TURBO_OK)
      status = native_bench_raw_uring_transfer(fixture, 1u, 0u, server_received, client_received,
                                               length, stages);
  }
  if (status == TURBO_OK && memcmp(sent, client_received, length) != 0) return TURBO_EIO;
  return status;
}

static int native_bench_u64_compare(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static int native_bench_run(native_bench_protocol protocol, native_bench_family family,
                            native_bench_driver driver, size_t payload_size,
                            native_bench_result *result) {
  native_bench_fixture fixture;
  unsigned char *sent = NULL;
  unsigned char *server_received = NULL;
  unsigned char *client_received = NULL;
  uint64_t latencies[NATIVE_BENCH_TOTAL_EXCHANGES];
  native_bench_stages stages = {0};
  uint64_t wall_started;
  size_t latency_count = 0u;
  int status = native_bench_fixture_init(&fixture, protocol, family, driver);
  if (status != TURBO_OK) {
    (void)native_bench_fixture_destroy(&fixture);
    return status;
  }
  sent = (unsigned char *)malloc(payload_size);
  server_received = (unsigned char *)malloc(payload_size);
  client_received = (unsigned char *)malloc(payload_size);
  if (sent == NULL || server_received == NULL || client_received == NULL) {
    status = TURBO_ENOMEM;
    goto cleanup;
  }
  memset(sent, 0x5a, payload_size);
  for (size_t index = 0u; index < NATIVE_BENCH_WARMUP_EXCHANGES; ++index) {
    status = native_bench_exchange(&fixture, sent, server_received, client_received, payload_size,
                                   &stages);
    if (status != TURBO_OK) goto cleanup;
  }
  stages = (native_bench_stages){0};
  wall_started = turbo_hrtime();
  for (size_t sample = 0u; sample < NATIVE_BENCH_SAMPLES; ++sample) {
    for (size_t exchange = 0u; exchange < NATIVE_BENCH_EXCHANGES_PER_SAMPLE; ++exchange) {
      const uint64_t started = turbo_hrtime();
      status = native_bench_exchange(&fixture, sent, server_received, client_received, payload_size,
                                     &stages);
      if (status != TURBO_OK) goto cleanup;
      latencies[latency_count++] = turbo_hrtime() - started;
    }
  }
  result->payload_size = payload_size;
  result->wall_ns = turbo_hrtime() - wall_started;
  result->stages = stages;
  qsort(latencies, latency_count, sizeof(latencies[0]), native_bench_u64_compare);
  result->p50_ns = latencies[(latency_count - 1u) * 50u / 100u];
  result->p95_ns = latencies[(latency_count - 1u) * 95u / 100u];

cleanup:
  free(client_received);
  free(server_received);
  free(sent);
  {
    const int cleanup_status = native_bench_fixture_destroy(&fixture);
    if (status == TURBO_OK) status = cleanup_status;
  }
  return status;
}

static double native_bench_mib_per_second(const native_bench_result *result) {
  const double bytes = (double)result->payload_size * 2.0 * (double)NATIVE_BENCH_TOTAL_EXCHANGES;
  return result->wall_ns == 0u ? 0.0
                               : bytes * 1000000000.0 / (double)result->wall_ns / (1024.0 * 1024.0);
}

static double native_bench_delta(double candidate, double baseline) {
  return baseline == 0.0 ? 0.0 : (candidate / baseline - 1.0) * 100.0;
}

static void native_bench_print_tables(const char *family, const char *protocol,
                                      const native_bench_result *raw,
                                      const native_bench_result *native, size_t count) {
  printf("\n%s %s loopback: raw vs NativeIO direct (%d x %d round trips)\n", family, protocol,
         NATIVE_BENCH_SAMPLES, NATIVE_BENCH_EXCHANGES_PER_SAMPLE);
  printf("\n%s %s latency\n", family, protocol);
  printf("| payload | raw p50 us | NativeIO p50 us | p50 delta | raw p95 us | "
         "NativeIO p95 us | p95 delta |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.3f | %.3f | %+.2f%% | %.3f | %.3f | %+.2f%% |\n",
           raw[index].payload_size / 1024u, (double)raw[index].p50_ns / 1000.0,
           (double)native[index].p50_ns / 1000.0,
           native_bench_delta((double)native[index].p50_ns, (double)raw[index].p50_ns),
           (double)raw[index].p95_ns / 1000.0, (double)native[index].p95_ns / 1000.0,
           native_bench_delta((double)native[index].p95_ns, (double)raw[index].p95_ns));
  }
  printf("\n%s %s throughput\n", family, protocol);
  printf("| payload | raw MiB/s | NativeIO MiB/s | delta |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const double raw_throughput = native_bench_mib_per_second(&raw[index]);
    const double native_throughput = native_bench_mib_per_second(&native[index]);
    printf("| %zu KiB | %.2f | %.2f | %+.2f%% |\n", raw[index].payload_size / 1024u, raw_throughput,
           native_throughput, native_bench_delta(native_throughput, raw_throughput));
  }
  printf("\n%s %s stage means\n", family, protocol);
  printf("| payload | raw submit ns/op | NativeIO submit ns/op | raw observe ns/op | "
         "NativeIO observe ns/op |\n");
  printf("| ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.2f | %.2f | %.2f | %.2f |\n", raw[index].payload_size / 1024u,
           (double)raw[index].stages.submit_ns / (double)raw[index].stages.operations,
           (double)native[index].stages.submit_ns / (double)native[index].stages.operations,
           (double)raw[index].stages.observe_ns / (double)raw[index].stages.operations,
           (double)native[index].stages.observe_ns / (double)native[index].stages.operations);
  }
}

static void native_bench_compare_family(native_bench_family family, const char *family_name) {
  native_bench_result
      raw_tcp[sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0])] = {0};
  native_bench_result
      native_tcp[sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0])] = {0};
  native_bench_result
      raw_udp[sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0])] = {0};
  native_bench_result
      native_udp[sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0])] = {0};
  const size_t tcp_count = sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0]);
  const size_t udp_count = sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0]);
  for (size_t index = 0u; index < tcp_count; ++index) {
    check_equal(native_bench_run(NATIVE_BENCH_TCP, family, NATIVE_BENCH_RAW,
                                 NATIVE_BENCH_TCP_PAYLOADS[index], &raw_tcp[index]),
                TURBO_OK);
    check_equal(native_bench_run(NATIVE_BENCH_TCP, family, NATIVE_BENCH_NATIVE_IO,
                                 NATIVE_BENCH_TCP_PAYLOADS[index], &native_tcp[index]),
                TURBO_OK);
  }
  for (size_t index = 0u; index < udp_count; ++index) {
    check_equal(native_bench_run(NATIVE_BENCH_UDP, family, NATIVE_BENCH_RAW,
                                 NATIVE_BENCH_UDP_PAYLOADS[index], &raw_udp[index]),
                TURBO_OK);
    check_equal(native_bench_run(NATIVE_BENCH_UDP, family, NATIVE_BENCH_NATIVE_IO,
                                 NATIVE_BENCH_UDP_PAYLOADS[index], &native_udp[index]),
                TURBO_OK);
  }
  native_bench_print_tables(family_name, "TCP", raw_tcp, native_tcp, tcp_count);
  native_bench_print_tables(family_name, "UDP", raw_udp, native_udp, udp_count);
}

spec("NativeIO Linux benchmark") {
  it("compares direct readiness overhead with raw epoll") {
    native_bench_compare_family(NATIVE_BENCH_EPOLL, "epoll");
  }

  it("compares direct completion overhead with raw io_uring") {
    native_bench_compare_family(NATIVE_BENCH_IO_URING, "io_uring");
  }
}
