#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include "tinytest.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef enum native_bench_protocol { NATIVE_BENCH_TCP = 0, NATIVE_BENCH_UDP } native_bench_protocol;

typedef enum native_bench_driver {
  NATIVE_BENCH_RAW_KQUEUE = 0,
  NATIVE_BENCH_NATIVE_IO
} native_bench_driver;

enum {
  NATIVE_BENCH_SAMPLES = 20,
  NATIVE_BENCH_EXCHANGES_PER_SAMPLE = 256,
  NATIVE_BENCH_WARMUP_EXCHANGES = 64,
  NATIVE_BENCH_TIMEOUT_MS = 5000,
  NATIVE_BENCH_REQUEST_CAPACITY = 4,
  NATIVE_BENCH_ENDPOINT_CAPACITY = 2,
  NATIVE_BENCH_BATCH_CAPACITY = 4,
  NATIVE_BENCH_TOTAL_EXCHANGES = NATIVE_BENCH_SAMPLES * NATIVE_BENCH_EXCHANGES_PER_SAMPLE
};

static const size_t NATIVE_BENCH_TCP_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u, 65536u};
static const size_t NATIVE_BENCH_UDP_PAYLOADS[] = {1024u, 4096u, 8192u};

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

typedef struct native_bench_fixture {
  native_bench_protocol protocol;
  native_bench_driver driver;
  int sockets[2];
  struct sockaddr_in addresses[2];
  int kqueue_fd;
  turbo_io_backend backend;
  turbo_io_endpoint endpoints[2];
} native_bench_fixture;

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
  if (bind(fd, (const struct sockaddr *)address, (socklen_t)sizeof(*address)) != 0)
    return native_bench_error();
  if (getsockname(fd, (struct sockaddr *)address, &address_length) != 0)
    return native_bench_error();
  return TURBO_OK;
}

static int native_bench_disable_sigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, (socklen_t)sizeof(enabled)) != 0)
    return native_bench_error();
#else
  (void)fd;
#endif
  return TURBO_OK;
}

static int native_bench_make_tcp_pair(native_bench_fixture *fixture) {
  int listener = -1;
  struct sockaddr_in address;
  const int no_delay = 1;
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
      connect(fixture->sockets[0], (const struct sockaddr *)&address,
              (socklen_t)sizeof(address)) != 0)
    status = native_bench_error();
  if (status == TURBO_OK) {
    fixture->sockets[1] = accept(listener, NULL, NULL);
    if (fixture->sockets[1] < 0) status = native_bench_error();
  }
  (void)close(listener);
  if (status == TURBO_OK &&
      (setsockopt(fixture->sockets[0], IPPROTO_TCP, TCP_NODELAY, &no_delay,
                  (socklen_t)sizeof(no_delay)) != 0 ||
       setsockopt(fixture->sockets[1], IPPROTO_TCP, TCP_NODELAY, &no_delay,
                  (socklen_t)sizeof(no_delay)) != 0))
    status = native_bench_error();
  if (status == TURBO_OK) status = native_bench_disable_sigpipe(fixture->sockets[0]);
  if (status == TURBO_OK) status = native_bench_disable_sigpipe(fixture->sockets[1]);
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

static int native_bench_fixture_init(native_bench_fixture *fixture,
                                     native_bench_protocol protocol,
                                     native_bench_driver driver) {
  const turbo_io_backend_config config = {TURBO_IO_BACKEND_KQUEUE,
                                          NATIVE_BENCH_ENDPOINT_CAPACITY,
                                          NATIVE_BENCH_REQUEST_CAPACITY,
                                          NATIVE_BENCH_BATCH_CAPACITY};
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  fixture->driver = driver;
  fixture->sockets[0] = -1;
  fixture->sockets[1] = -1;
  fixture->kqueue_fd = -1;
  if (driver == NATIVE_BENCH_RAW_KQUEUE) {
    fixture->kqueue_fd = kqueue();
    if (fixture->kqueue_fd < 0) return native_bench_error();
  } else {
    status = native_io_init(&fixture->backend, &config);
    if (status != TURBO_OK) return status;
  }
  status = protocol == NATIVE_BENCH_TCP ? native_bench_make_tcp_pair(fixture)
                                        : native_bench_make_udp_pair(fixture);
  if (status != TURBO_OK) return status;
  if (driver == NATIVE_BENCH_NATIVE_IO) {
    for (size_t index = 0u; index < 2u; ++index) {
      status = native_io_attach_socket(&fixture->backend,
                                              (uintptr_t)fixture->sockets[index],
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
      if (close(fixture->sockets[index]) != 0 && status == TURBO_OK)
        status = native_bench_error();
      fixture->sockets[index] = -1;
      if (fixture->driver == NATIVE_BENCH_NATIVE_IO &&
          turbo_io_endpoint_valid(fixture->endpoints[index])) {
        const int release_status =
            native_io_release_socket(&fixture->backend, fixture->endpoints[index]);
        if (status == TURBO_OK && release_status != TURBO_OK) status = release_status;
      }
    }
  }
  if (fixture->kqueue_fd >= 0) {
    if (close(fixture->kqueue_fd) != 0 && status == TURBO_OK) status = native_bench_error();
    fixture->kqueue_fd = -1;
  }
  if (fixture->backend.impl != NULL) {
    const int close_status = native_io_close(&fixture->backend);
    const int destroy_status =
        close_status == TURBO_OK ? native_io_destroy(&fixture->backend) : close_status;
    if (status == TURBO_OK && destroy_status != TURBO_OK) status = destroy_status;
  }
  return status;
}

static int native_bench_kqueue_update(native_bench_fixture *fixture, int fd, int16_t filter,
                                      uintptr_t token, bool old_enabled, bool new_enabled) {
  struct kevent change;
  int status;
  if (old_enabled == new_enabled) return TURBO_OK;
  EV_SET(&change, (uintptr_t)fd, filter,
         new_enabled ? (EV_ADD | EV_ENABLE) : EV_DELETE, 0u, 0,
         new_enabled ? (void *)token : NULL);
  do {
    status = kevent(fixture->kqueue_fd, &change, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  if (status == 0 || (!new_enabled && errno == ENOENT)) return TURBO_OK;
  return native_bench_error();
}

static ssize_t native_bench_try_receive(native_bench_fixture *fixture,
                                        size_t destination_index, unsigned char *buffer,
                                        size_t length) {
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
    return send(fixture->sockets[source_index], buffer, length, MSG_DONTWAIT);
  return sendto(fixture->sockets[source_index], buffer, length, MSG_DONTWAIT,
                (const struct sockaddr *)&fixture->addresses[destination_index],
                (socklen_t)sizeof(fixture->addresses[destination_index]));
}

static int native_bench_raw_kqueue_transfer(native_bench_fixture *fixture,
                                            size_t source_index,
                                            size_t destination_index,
                                            const unsigned char *sent,
                                            unsigned char *received, size_t length,
                                            native_bench_stages *stages) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool source_enabled = false;
  bool destination_enabled = false;
  bool first_attempt = true;
  stages->operations += 2u;
  while (sent_offset < length || received_offset < length) {
    uint64_t started = turbo_hrtime();
    ssize_t result;
    int status;
    if (received_offset < length) {
      do {
        result = native_bench_try_receive(fixture, destination_index,
                                          received + received_offset,
                                          length - received_offset);
      } while (result < 0 && errno == EINTR);
      if (result > 0) received_offset += (size_t)result;
      else if (result == 0 && fixture->protocol == NATIVE_BENCH_TCP) return TURBO_EOF;
      else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return native_bench_error();
    }
    if (sent_offset < length) {
      do {
        result = native_bench_try_send(fixture, source_index, destination_index,
                                       sent + sent_offset, length - sent_offset);
      } while (result < 0 && errno == EINTR);
      if (result > 0) sent_offset += (size_t)result;
      else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return native_bench_error();
    }
    {
      const bool next_destination = received_offset < length;
      const bool next_source = sent_offset < length;
      status = native_bench_kqueue_update(
          fixture, fixture->sockets[destination_index], EVFILT_READ, 2u,
          destination_enabled, next_destination);
      if (status != TURBO_OK) return status;
      destination_enabled = next_destination;
      status = native_bench_kqueue_update(fixture, fixture->sockets[source_index],
                                          EVFILT_WRITE, 1u, source_enabled,
                                          next_source);
      if (status != TURBO_OK) return status;
      source_enabled = next_source;
    }
    native_bench_counter_add(first_attempt ? &stages->submit_ns : &stages->observe_ns,
                             turbo_hrtime() - started);
    first_attempt = false;
    if (sent_offset == length && received_offset == length) break;
    {
      struct kevent events[2];
      const struct timespec timeout = {NATIVE_BENCH_TIMEOUT_MS / 1000,
                                       (NATIVE_BENCH_TIMEOUT_MS % 1000) * 1000000L};
      int count;
      started = turbo_hrtime();
      do {
        count = kevent(fixture->kqueue_fd, NULL, 0, events, 2, &timeout);
      } while (count < 0 && errno == EINTR);
      native_bench_counter_add(&stages->observe_ns, turbo_hrtime() - started);
      if (count < 0) return native_bench_error();
      if (count == 0) return TURBO_ETIMEDOUT;
      for (int index = 0; index < count; ++index)
        if ((events[index].flags & EV_ERROR) != 0u && events[index].data != 0)
          return -(int)events[index].data;
    }
  }
  return TURBO_OK;
}

static int native_bench_submit_operation(turbo_io_backend *backend,
                                         const turbo_io_operation *operation,
                                         turbo_io_request *request,
                                         native_bench_stages *stages) {
  const uint64_t started = turbo_hrtime();
  const int status = native_io_submit(backend, operation, request);
  native_bench_counter_add(&stages->submit_ns, turbo_hrtime() - started);
  if (status == TURBO_OK) ++stages->operations;
  return status;
}

static int native_bench_native_transfer(native_bench_fixture *fixture,
                                        size_t source_index,
                                        size_t destination_index,
                                        const unsigned char *sent,
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
      turbo_io_operation operation = {
          .kind = fixture->protocol == NATIVE_BENCH_TCP ? TURBO_IO_TCP_RECV
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
      status = native_bench_submit_operation(&fixture->backend, &operation, &request,
                                             stages);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      turbo_io_operation operation = {
          .kind = fixture->protocol == NATIVE_BENCH_TCP ? TURBO_IO_TCP_SEND
                                                        : TURBO_IO_UDP_SEND_TO,
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
      status = native_bench_submit_operation(&fixture->backend, &operation, &request,
                                             stages);
      if (status != TURBO_OK) return status;
      send_pending = true;
    }
    started = turbo_hrtime();
    status = native_io_observe(&fixture->backend, events, 2u,
                                      NATIVE_BENCH_TIMEOUT_MS, &event_count);
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

static int native_bench_exchange(native_bench_fixture *fixture,
                                 const unsigned char *sent,
                                 unsigned char *server_received,
                                 unsigned char *client_received, size_t length,
                                 native_bench_stages *stages) {
  int status;
  if (fixture->driver == NATIVE_BENCH_NATIVE_IO) {
    status = native_bench_native_transfer(fixture, 0u, 1u, sent, server_received,
                                          length, stages);
    if (status == TURBO_OK)
      status = native_bench_native_transfer(fixture, 1u, 0u, server_received,
                                            client_received, length, stages);
  } else {
    status = native_bench_raw_kqueue_transfer(fixture, 0u, 1u, sent,
                                              server_received, length, stages);
    if (status == TURBO_OK)
      status = native_bench_raw_kqueue_transfer(fixture, 1u, 0u, server_received,
                                                client_received, length, stages);
  }
  if (status == TURBO_OK && memcmp(sent, client_received, length) != 0)
    return TURBO_EIO;
  return status;
}

static int native_bench_u64_compare(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static int native_bench_run(native_bench_protocol protocol,
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
  int status = native_bench_fixture_init(&fixture, protocol, driver);
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
    status = native_bench_exchange(&fixture, sent, server_received, client_received,
                                   payload_size, &stages);
    if (status != TURBO_OK) goto cleanup;
  }
  stages = (native_bench_stages){0};
  wall_started = turbo_hrtime();
  for (size_t sample = 0u; sample < NATIVE_BENCH_SAMPLES; ++sample) {
    for (size_t exchange = 0u; exchange < NATIVE_BENCH_EXCHANGES_PER_SAMPLE;
         ++exchange) {
      const uint64_t started = turbo_hrtime();
      status = native_bench_exchange(&fixture, sent, server_received,
                                     client_received, payload_size, &stages);
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
  const double bytes =
      (double)result->payload_size * 2.0 * (double)NATIVE_BENCH_TOTAL_EXCHANGES;
  return result->wall_ns == 0u
             ? 0.0
             : bytes * 1000000000.0 / (double)result->wall_ns /
                   (1024.0 * 1024.0);
}

static double native_bench_delta(double candidate, double baseline) {
  return baseline == 0.0 ? 0.0 : (candidate / baseline - 1.0) * 100.0;
}

static void native_bench_print_tables(const char *protocol,
                                      const native_bench_result *raw,
                                      const native_bench_result *native, size_t count) {
  printf("\nkqueue %s loopback: raw vs NativeIO direct (%d x %d round trips)\n",
         protocol, NATIVE_BENCH_SAMPLES, NATIVE_BENCH_EXCHANGES_PER_SAMPLE);
  printf("\nkqueue %s latency\n", protocol);
  printf("| payload | raw p50 us | NativeIO p50 us | p50 delta | raw p95 us | "
         "NativeIO p95 us | p95 delta |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.3f | %.3f | %+.2f%% | %.3f | %.3f | %+.2f%% |\n",
           raw[index].payload_size / 1024u, (double)raw[index].p50_ns / 1000.0,
           (double)native[index].p50_ns / 1000.0,
           native_bench_delta((double)native[index].p50_ns,
                              (double)raw[index].p50_ns),
           (double)raw[index].p95_ns / 1000.0,
           (double)native[index].p95_ns / 1000.0,
           native_bench_delta((double)native[index].p95_ns,
                              (double)raw[index].p95_ns));
  }
  printf("\nkqueue %s throughput\n", protocol);
  printf("| payload | raw MiB/s | NativeIO MiB/s | delta |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const double raw_throughput = native_bench_mib_per_second(&raw[index]);
    const double native_throughput = native_bench_mib_per_second(&native[index]);
    printf("| %zu KiB | %.2f | %.2f | %+.2f%% |\n",
           raw[index].payload_size / 1024u, raw_throughput, native_throughput,
           native_bench_delta(native_throughput, raw_throughput));
  }
  printf("\nkqueue %s stage means\n", protocol);
  printf("| payload | raw submit ns/op | NativeIO submit ns/op | raw observe ns/op | "
         "NativeIO observe ns/op |\n");
  printf("| ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.2f | %.2f | %.2f | %.2f |\n",
           raw[index].payload_size / 1024u,
           (double)raw[index].stages.submit_ns /
               (double)raw[index].stages.operations,
           (double)native[index].stages.submit_ns /
               (double)native[index].stages.operations,
           (double)raw[index].stages.observe_ns /
               (double)raw[index].stages.operations,
           (double)native[index].stages.observe_ns /
               (double)native[index].stages.operations);
  }
}

static void native_bench_compare_kqueue(void) {
  native_bench_result
      raw_tcp[sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0])] = {0};
  native_bench_result
      native_tcp[sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0])] = {0};
  native_bench_result
      raw_udp[sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0])] = {0};
  native_bench_result
      native_udp[sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0])] = {0};
  const size_t tcp_count =
      sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0]);
  const size_t udp_count =
      sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0]);
  for (size_t index = 0u; index < tcp_count; ++index) {
    check_equal(native_bench_run(NATIVE_BENCH_TCP, NATIVE_BENCH_RAW_KQUEUE,
                                 NATIVE_BENCH_TCP_PAYLOADS[index], &raw_tcp[index]),
                TURBO_OK);
    check_equal(native_bench_run(NATIVE_BENCH_TCP, NATIVE_BENCH_NATIVE_IO,
                                 NATIVE_BENCH_TCP_PAYLOADS[index], &native_tcp[index]),
                TURBO_OK);
  }
  for (size_t index = 0u; index < udp_count; ++index) {
    check_equal(native_bench_run(NATIVE_BENCH_UDP, NATIVE_BENCH_RAW_KQUEUE,
                                 NATIVE_BENCH_UDP_PAYLOADS[index], &raw_udp[index]),
                TURBO_OK);
    check_equal(native_bench_run(NATIVE_BENCH_UDP, NATIVE_BENCH_NATIVE_IO,
                                 NATIVE_BENCH_UDP_PAYLOADS[index], &native_udp[index]),
                TURBO_OK);
  }
  native_bench_print_tables("TCP", raw_tcp, native_tcp, tcp_count);
  native_bench_print_tables("UDP", raw_udp, native_udp, udp_count);
}

spec("NativeIO kqueue benchmark") {
  it("compares direct readiness overhead with raw kqueue") {
    native_bench_compare_kqueue();
  }
}
