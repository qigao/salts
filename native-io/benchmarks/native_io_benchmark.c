#if !defined(WIN32_LEAN_AND_MEAN)
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include "tinytest.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum native_bench_protocol { NATIVE_BENCH_TCP = 0, NATIVE_BENCH_UDP } native_bench_protocol;

typedef enum native_bench_driver {
  NATIVE_BENCH_RAW_IOCP = 0,
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

typedef struct native_bench_raw_request {
  OVERLAPPED overlapped;
  WSABUF buffer;
  DWORD flags;
  struct sockaddr_storage address;
  int address_length;
  bool receive;
} native_bench_raw_request;

typedef struct native_bench_fixture {
  native_bench_protocol protocol;
  native_bench_driver driver;
  SOCKET sockets[2];
  struct sockaddr_in addresses[2];
  HANDLE port;
  native_io_backend backend;
  native_io_endpoint endpoints[2];
  bool winsock_started;
} native_bench_fixture;

static int native_bench_error(int error) { return error == 0 ? TURBO_EIO : -error; }

static void native_bench_counter_add(uint64_t *counter, uint64_t value) {
  *counter = UINT64_MAX - *counter < value ? UINT64_MAX : *counter + value;
}

static int native_bench_bind_loopback(SOCKET socket_value, struct sockaddr_in *address) {
  int address_length = (int)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(socket_value, (const struct sockaddr *)address, (int)sizeof(*address)) != 0)
    return native_bench_error(WSAGetLastError());
  if (getsockname(socket_value, (struct sockaddr *)address, &address_length) != 0)
    return native_bench_error(WSAGetLastError());
  return TURBO_OK;
}

static int native_bench_make_tcp_pair(native_bench_fixture *fixture) {
  SOCKET listener = INVALID_SOCKET;
  struct sockaddr_in listener_address;
  int no_delay = 1;
  int status = TURBO_OK;

  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) return native_bench_error(WSAGetLastError());
  status = native_bench_bind_loopback(listener, &listener_address);
  if (status == TURBO_OK && listen(listener, 1) != 0)
    status = native_bench_error(WSAGetLastError());
  if (status == TURBO_OK) {
    fixture->sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->sockets[0] == INVALID_SOCKET) status = native_bench_error(WSAGetLastError());
  }
  if (status == TURBO_OK && connect(fixture->sockets[0], (const struct sockaddr *)&listener_address,
                                    (int)sizeof(listener_address)) != 0)
    status = native_bench_error(WSAGetLastError());
  if (status == TURBO_OK) {
    fixture->sockets[1] = accept(listener, NULL, NULL);
    if (fixture->sockets[1] == INVALID_SOCKET) status = native_bench_error(WSAGetLastError());
  }
  (void)closesocket(listener);
  if (status == TURBO_OK && (setsockopt(fixture->sockets[0], IPPROTO_TCP, TCP_NODELAY,
                                        (const char *)&no_delay, (int)sizeof(no_delay)) != 0 ||
                             setsockopt(fixture->sockets[1], IPPROTO_TCP, TCP_NODELAY,
                                        (const char *)&no_delay, (int)sizeof(no_delay)) != 0))
    status = native_bench_error(WSAGetLastError());
  return status;
}

static int native_bench_make_udp_pair(native_bench_fixture *fixture) {
  int status = TURBO_OK;
  fixture->sockets[0] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  fixture->sockets[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fixture->sockets[0] == INVALID_SOCKET || fixture->sockets[1] == INVALID_SOCKET)
    status = native_bench_error(WSAGetLastError());
  if (status == TURBO_OK)
    status = native_bench_bind_loopback(fixture->sockets[0], &fixture->addresses[0]);
  if (status == TURBO_OK)
    status = native_bench_bind_loopback(fixture->sockets[1], &fixture->addresses[1]);
  return status;
}

static int native_bench_fixture_init(native_bench_fixture *fixture, native_bench_protocol protocol,
                                     native_bench_driver driver) {
  const native_io_backend_config config = {NATIVE_IO_BACKEND_IOCP, NATIVE_BENCH_ENDPOINT_CAPACITY,
                                          NATIVE_BENCH_REQUEST_CAPACITY,
                                          NATIVE_BENCH_BATCH_CAPACITY};
  WSADATA winsock_data;
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  fixture->driver = driver;
  fixture->sockets[0] = INVALID_SOCKET;
  fixture->sockets[1] = INVALID_SOCKET;

  if (driver == NATIVE_BENCH_RAW_IOCP) {
    status = WSAStartup(MAKEWORD(2, 2), &winsock_data);
    if (status != 0) return native_bench_error(status);
    fixture->winsock_started = true;
  } else {
    status = native_io_backend_init(&fixture->backend, &config);
    if (status != TURBO_OK) return status;
  }

  status = protocol == NATIVE_BENCH_TCP ? native_bench_make_tcp_pair(fixture)
                                        : native_bench_make_udp_pair(fixture);
  if (status != TURBO_OK) return status;

  if (driver == NATIVE_BENCH_RAW_IOCP) {
    fixture->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0u, 1u);
    if (fixture->port == NULL) return native_bench_error((int)GetLastError());
    for (size_t index = 0u; index < 2u; ++index) {
      if (CreateIoCompletionPort((HANDLE)fixture->sockets[index], fixture->port, index + 1u, 0u) !=
          fixture->port)
        return native_bench_error((int)GetLastError());
    }
  } else {
    for (size_t index = 0u; index < 2u; ++index) {
      status = native_io_backend_attach_socket(&fixture->backend, (uintptr_t)fixture->sockets[index],
                                              &fixture->endpoints[index]);
      if (status != TURBO_OK) return status;
    }
  }
  return TURBO_OK;
}

static int native_bench_fixture_destroy(native_bench_fixture *fixture) {
  int status = TURBO_OK;
  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->sockets[index] != INVALID_SOCKET) {
      if (closesocket(fixture->sockets[index]) != 0 && status == TURBO_OK)
        status = native_bench_error(WSAGetLastError());
      fixture->sockets[index] = INVALID_SOCKET;
      if (fixture->driver == NATIVE_BENCH_NATIVE_IO &&
          native_io_endpoint_valid(fixture->endpoints[index])) {
        const int release_status =
            native_io_backend_release_socket(&fixture->backend, fixture->endpoints[index]);
        if (status == TURBO_OK && release_status != TURBO_OK) status = release_status;
        fixture->endpoints[index] = (native_io_endpoint){0};
      }
    }
  }
  if (fixture->driver == NATIVE_BENCH_RAW_IOCP) {
    if (fixture->port != NULL && !CloseHandle(fixture->port) && status == TURBO_OK)
      status = native_bench_error((int)GetLastError());
    fixture->port = NULL;
    if (fixture->winsock_started) {
      if (WSACleanup() != 0 && status == TURBO_OK) status = native_bench_error(WSAGetLastError());
      fixture->winsock_started = false;
    }
  } else if (fixture->backend.impl != NULL) {
    int close_status = native_io_backend_close(&fixture->backend);
    int destroy_status =
        close_status == TURBO_OK ? native_io_backend_destroy(&fixture->backend) : close_status;
    if (status == TURBO_OK && destroy_status != TURBO_OK) status = destroy_status;
  }
  return status;
}

static int native_bench_raw_post(native_bench_fixture *fixture, native_bench_raw_request *request,
                                 SOCKET socket_value, void *buffer, size_t length, bool receive,
                                 const struct sockaddr_in *destination,
                                 native_bench_stages *stages) {
  DWORD immediate_bytes = 0u;
  int call_status;
  int error;
  uint64_t started;
  memset(request, 0, sizeof(*request));
  request->buffer.buf = (CHAR *)buffer;
  request->buffer.len = (ULONG)length;
  request->receive = receive;
  request->address_length = (int)sizeof(request->address);

  started = turbo_hrtime();
  if (fixture->protocol == NATIVE_BENCH_TCP && receive) {
    call_status = WSARecv(socket_value, &request->buffer, 1u, &immediate_bytes, &request->flags,
                          &request->overlapped, NULL);
  } else if (fixture->protocol == NATIVE_BENCH_TCP) {
    call_status = WSASend(socket_value, &request->buffer, 1u, &immediate_bytes, 0u,
                          &request->overlapped, NULL);
  } else if (receive) {
    call_status = WSARecvFrom(socket_value, &request->buffer, 1u, &immediate_bytes, &request->flags,
                              (SOCKADDR *)&request->address, &request->address_length,
                              &request->overlapped, NULL);
  } else {
    call_status = WSASendTo(socket_value, &request->buffer, 1u, &immediate_bytes, 0u,
                            (const SOCKADDR *)destination, (int)sizeof(*destination),
                            &request->overlapped, NULL);
  }
  native_bench_counter_add(&stages->submit_ns, turbo_hrtime() - started);
  if (call_status != 0) {
    error = WSAGetLastError();
    if (error != WSA_IO_PENDING) return native_bench_error(error);
  }
  ++stages->operations;
  return TURBO_OK;
}

static int native_bench_raw_transfer(native_bench_fixture *fixture, size_t source_index,
                                     size_t destination_index, const unsigned char *sent,
                                     unsigned char *received, size_t length,
                                     native_bench_stages *stages) {
  native_bench_raw_request receive_request;
  native_bench_raw_request send_request;
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool send_pending = false;
  bool receive_pending = false;

  while (sent_offset < length || received_offset < length) {
    int status;
    DWORD bytes = 0u;
    ULONG_PTR completion_key = 0u;
    OVERLAPPED *overlapped = NULL;
    BOOL ok;
    DWORD native_error;
    uint64_t started;

    if (!receive_pending && received_offset < length) {
      status = native_bench_raw_post(fixture, &receive_request, fixture->sockets[destination_index],
                                     received + received_offset, length - received_offset, true,
                                     NULL, stages);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      status = native_bench_raw_post(fixture, &send_request, fixture->sockets[source_index],
                                     (void *)(sent + sent_offset), length - sent_offset, false,
                                     &fixture->addresses[destination_index], stages);
      if (status != TURBO_OK) return status;
      send_pending = true;
    }

    started = turbo_hrtime();
    ok = GetQueuedCompletionStatus(fixture->port, &bytes, &completion_key, &overlapped,
                                   NATIVE_BENCH_TIMEOUT_MS);
    native_error = ok ? ERROR_SUCCESS : GetLastError();
    native_bench_counter_add(&stages->observe_ns, turbo_hrtime() - started);
    (void)completion_key;
    if (overlapped == NULL)
      return native_error == WAIT_TIMEOUT ? TURBO_ETIMEDOUT : native_bench_error((int)native_error);
    if (!ok) return native_bench_error((int)native_error);
    if (overlapped == &receive_request.overlapped) {
      if (fixture->protocol == NATIVE_BENCH_TCP && bytes == 0u) return TURBO_EOF;
      received_offset += (size_t)bytes;
      receive_pending = false;
    } else if (overlapped == &send_request.overlapped) {
      sent_offset += (size_t)bytes;
      send_pending = false;
    } else {
      return TURBO_EPROTO;
    }
    if (fixture->protocol == NATIVE_BENCH_UDP &&
        ((received_offset != 0u && received_offset != length) ||
         (sent_offset != 0u && sent_offset != length)))
      return TURBO_EIO;
  }
  return TURBO_OK;
}

static int native_bench_submit_operation(native_io_backend *backend,
                                         const native_io_operation *operation,
                                         native_io_request *request, native_bench_stages *stages) {
  uint64_t started = turbo_hrtime();
  int status = native_io_backend_submit(backend, operation, request);
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
    native_io_completion events[2];
    size_t event_count = 0u;
    int status;
    uint64_t started;
    if (!receive_pending && received_offset < length) {
      native_io_operation operation = {.kind = fixture->protocol == NATIVE_BENCH_TCP
                                                  ? NATIVE_IO_OPERATION_TCP_RECV
                                                  : NATIVE_IO_OPERATION_UDP_RECV_FROM,
                                      .endpoint = fixture->endpoints[destination_index],
                                      .buffer = received + received_offset,
                                      .length = length - received_offset,
                                      .user_data = 1u};
      native_io_request request;
      if (fixture->protocol == NATIVE_BENCH_UDP) {
        operation.address = &peer_address;
        operation.address_capacity = sizeof(peer_address);
      }
      status = native_bench_submit_operation(&fixture->backend, &operation, &request, stages);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      native_io_operation operation = {
          .kind = fixture->protocol == NATIVE_BENCH_TCP ? NATIVE_IO_OPERATION_TCP_SEND : NATIVE_IO_OPERATION_UDP_SEND_TO,
          .endpoint = fixture->endpoints[source_index],
          .buffer = (void *)(sent + sent_offset),
          .length = length - sent_offset,
          .user_data = 2u};
      native_io_request request;
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
    status = native_io_backend_observe(&fixture->backend, events, 2u, NATIVE_BENCH_TIMEOUT_MS,
                                      &event_count);
    native_bench_counter_add(&stages->observe_ns, turbo_hrtime() - started);
    if (status != TURBO_OK) return status;
    for (size_t index = 0u; index < event_count; ++index) {
      if (events[index].kind != NATIVE_IO_COMPLETION_OK) return events[index].status;
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
  if (fixture->driver == NATIVE_BENCH_RAW_IOCP) {
    status = native_bench_raw_transfer(fixture, 0u, 1u, sent, server_received, length, stages);
    if (status == TURBO_OK)
      status = native_bench_raw_transfer(fixture, 1u, 0u, server_received, client_received, length,
                                         stages);
  } else {
    status = native_bench_native_transfer(fixture, 0u, 1u, sent, server_received, length, stages);
    if (status == TURBO_OK)
      status = native_bench_native_transfer(fixture, 1u, 0u, server_received, client_received,
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

static int native_bench_run(native_bench_protocol protocol, native_bench_driver driver,
                            size_t payload_size, native_bench_result *result) {
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

static void native_bench_print_tables(const char *protocol, const native_bench_result *raw,
                                      const native_bench_result *native, size_t count) {
  printf("\n%s loopback: raw IOCP vs NativeIO direct (%d x %d round trips)\n", protocol,
         NATIVE_BENCH_SAMPLES, NATIVE_BENCH_EXCHANGES_PER_SAMPLE);
  printf("\n%s latency\n", protocol);
  printf("| payload | raw p50 us | NativeIO p50 us | p50 delta | "
         "raw p95 us | NativeIO p95 us | p95 delta |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.3f | %.3f | %+.2f%% | %.3f | %.3f | "
           "%+.2f%% |\n",
           raw[index].payload_size / 1024u, (double)raw[index].p50_ns / 1000.0,
           (double)native[index].p50_ns / 1000.0,
           native_bench_delta((double)native[index].p50_ns, (double)raw[index].p50_ns),
           (double)raw[index].p95_ns / 1000.0, (double)native[index].p95_ns / 1000.0,
           native_bench_delta((double)native[index].p95_ns, (double)raw[index].p95_ns));
  }

  printf("\n%s throughput\n", protocol);
  printf("| payload | raw MiB/s | NativeIO MiB/s | delta |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const double raw_throughput = native_bench_mib_per_second(&raw[index]);
    const double native_throughput = native_bench_mib_per_second(&native[index]);
    printf("| %zu KiB | %.2f | %.2f | %+.2f%% |\n", raw[index].payload_size / 1024u, raw_throughput,
           native_throughput, native_bench_delta(native_throughput, raw_throughput));
  }

  printf("\n%s stage means\n", protocol);
  printf("| payload | raw submit ns/op | NativeIO submit ns/op | "
         "raw completion ns/op | NativeIO observe ns/op |\n");
  printf("| ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.2f | %.2f | %.2f | %.2f |\n", raw[index].payload_size / 1024u,
           (double)raw[index].stages.submit_ns / (double)raw[index].stages.operations,
           (double)native[index].stages.submit_ns / (double)native[index].stages.operations,
           (double)raw[index].stages.observe_ns / (double)raw[index].stages.operations,
           (double)native[index].stages.observe_ns / (double)native[index].stages.operations);
  }
}

spec("NativeIO benchmark") {
  it("compares direct completion overhead with raw IOCP") {
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
      check_equal(native_bench_run(NATIVE_BENCH_TCP, NATIVE_BENCH_RAW_IOCP,
                                   NATIVE_BENCH_TCP_PAYLOADS[index], &raw_tcp[index]),
                  TURBO_OK);
      check_equal(native_bench_run(NATIVE_BENCH_TCP, NATIVE_BENCH_NATIVE_IO,
                                   NATIVE_BENCH_TCP_PAYLOADS[index], &native_tcp[index]),
                  TURBO_OK);
    }
    for (size_t index = 0u; index < udp_count; ++index) {
      check_equal(native_bench_run(NATIVE_BENCH_UDP, NATIVE_BENCH_RAW_IOCP,
                                   NATIVE_BENCH_UDP_PAYLOADS[index], &raw_udp[index]),
                  TURBO_OK);
      check_equal(native_bench_run(NATIVE_BENCH_UDP, NATIVE_BENCH_NATIVE_IO,
                                   NATIVE_BENCH_UDP_PAYLOADS[index], &native_udp[index]),
                  TURBO_OK);
    }

    native_bench_print_tables("TCP", raw_tcp, native_tcp, tcp_count);
    native_bench_print_tables("UDP", raw_udp, native_udp, udp_count);
    printf("\nUDP 64 KiB is intentionally omitted: an IPv4 UDP datagram cannot "
           "carry 65536 payload bytes.\n");
  }
}
