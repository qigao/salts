#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>

typedef SOCKET native_io_test_socket;
typedef int native_io_test_socklen;
  #define NATIVE_IO_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <unistd.h>

typedef int native_io_test_socket;
typedef socklen_t native_io_test_socklen;
  #define NATIVE_IO_TEST_INVALID_SOCKET (-1)
#endif

enum {
  NATIVE_IO_TEST_ENDPOINT_CAPACITY = 2,
  NATIVE_IO_TEST_REQUEST_CAPACITY = 3,
  NATIVE_IO_TEST_BATCH_CAPACITY = 3,
  NATIVE_IO_TEST_PIPE_BUFFER_CAPACITY = 4096,
  NATIVE_IO_TEST_TIMEOUT_MS = 5000,
  NATIVE_IO_TEST_MAX_BACKENDS = 2
};

static int native_io_test_last_error(void) {
#if defined(_WIN32)
  return -(int)WSAGetLastError();
#else
  return -errno;
#endif
}

static void native_io_test_close_socket(native_io_test_socket socket_value) {
  if (socket_value == NATIVE_IO_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static size_t native_io_test_backends(turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS]) {
#if defined(_WIN32)
  backends[0] = TURBO_IO_BACKEND_IOCP;
  return 1u;
#elif defined(__linux__)
  backends[0] = TURBO_IO_BACKEND_EPOLL;
  backends[1] = TURBO_IO_BACKEND_IO_URING;
  return 2u;
#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
       defined(__DragonFly__)) && \
    UINTPTR_MAX > UINT32_MAX
  backends[0] = TURBO_IO_BACKEND_KQUEUE;
  return 1u;
#else
  (void)backends;
  return 0u;
#endif
}

#if !defined(_WIN32)
static size_t
native_io_test_readiness_backends(turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS]) {
#if defined(__linux__)
  backends[0] = TURBO_IO_BACKEND_EPOLL;
  return 1u;
#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
       defined(__DragonFly__)) && \
    UINTPTR_MAX > UINT32_MAX
  backends[0] = TURBO_IO_BACKEND_KQUEUE;
  return 1u;
#else
  (void)backends;
  return 0u;
#endif
}

static int native_io_test_observe_all(turbo_io_backend *backend,
                                      turbo_io_completion *events, size_t expected);

static int native_io_test_make_pipe(int descriptors[2], bool nonblocking) {
  int flags;
  if (pipe(descriptors) != 0) return -errno;
  if (!nonblocking) return TURBO_OK;
  flags = fcntl(descriptors[0], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) goto failed;
  flags = fcntl(descriptors[1], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK) != 0) goto failed;
  return TURBO_OK;

failed:
  flags = errno;
  (void)close(descriptors[0]);
  (void)close(descriptors[1]);
  descriptors[0] = -1;
  descriptors[1] = -1;
  return -flags;
}

static void native_io_test_readiness_pipe_round_trip(turbo_io_backend_kind kind) {
  static const unsigned char payload[] = {0x41u, 0x42u, 0x43u, 0x44u};
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 2u, 2u, 2u};
  int descriptors[2] = {-1, -1};
  turbo_io_endpoint endpoints[2] = {0};
  turbo_io_request requests[2] = {0};
  turbo_io_completion events[2] = {0};
  unsigned char received[sizeof(payload)] = {0};
  const uint32_t flags = TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE;
  turbo_io_operation operations[2];

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_pipe(descriptors, true), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[0], flags,
                                           &endpoints[0]),
              TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[1], flags,
                                           &endpoints[1]),
              TURBO_OK);
  operations[0] = (turbo_io_operation){.kind = TURBO_IO_PIPE_READ,
                                       .endpoint = endpoints[0],
                                       .buffer = received,
                                       .length = sizeof(received),
                                       .user_data = 41u};
  operations[1] = (turbo_io_operation){.kind = TURBO_IO_PIPE_WRITE,
                                       .endpoint = endpoints[1],
                                       .buffer = (void *)payload,
                                       .length = sizeof(payload),
                                       .user_data = 42u};
  check_equal(native_io_submit(&backend, &operations[0], &requests[0]), TURBO_OK);
  check_equal(native_io_release_pipe(&backend, endpoints[0]), TURBO_EBUSY);
  check_equal(native_io_submit(&backend, &operations[1], &requests[1]), TURBO_OK);
  check_equal(native_io_test_observe_all(&backend, events, 2u), TURBO_OK);
  check_equal(events[0].kind, TURBO_IO_COMPLETION_OK);
  check_equal(events[1].kind, TURBO_IO_COMPLETION_OK);
  check_equal(memcmp(received, payload, sizeof(payload)), 0);

  (void)close(descriptors[0]);
  (void)close(descriptors[1]);
  check_equal(native_io_release_pipe(&backend, endpoints[0]), TURBO_OK);
  check_equal(native_io_release_pipe(&backend, endpoints[1]), TURBO_OK);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_readiness_pipe_eof_and_reuse(turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 1u, 1u, 1u};
  int first[2] = {-1, -1};
  int second[2] = {-1, -1};
  turbo_io_endpoint old_endpoint = {0};
  turbo_io_endpoint new_endpoint = {0};
  turbo_io_request request = {0};
  turbo_io_completion event = {0};
  unsigned char byte = 0u;
  size_t count = 0u;
  turbo_io_operation operation;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_pipe(first, true), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)first[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &old_endpoint),
              TURBO_OK);
  (void)close(first[1]);
  first[1] = -1;
  operation = (turbo_io_operation){.kind = TURBO_IO_PIPE_READ,
                                   .endpoint = old_endpoint,
                                   .buffer = &byte,
                                   .length = sizeof(byte),
                                   .user_data = 51u};
  check_equal(native_io_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_observe(&backend, &event, 1u, NATIVE_IO_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(event.kind, TURBO_IO_COMPLETION_EOF);
  check_equal(event.status, TURBO_EOF);
  (void)close(first[0]);
  first[0] = -1;
  check_equal(native_io_release_pipe(&backend, old_endpoint), TURBO_OK);
  check_equal(native_io_release_pipe(&backend, old_endpoint), TURBO_ENOENT);

  check_equal(native_io_test_make_pipe(second, true), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)second[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &new_endpoint),
              TURBO_OK);
  check_equal(new_endpoint.slot, old_endpoint.slot);
  check_not_equal(new_endpoint.generation, old_endpoint.generation);
  operation.endpoint = old_endpoint;
  check_equal(native_io_submit(&backend, &operation, &request), TURBO_ENOENT);
  check_false(turbo_io_request_valid(request));
  (void)close(second[0]);
  (void)close(second[1]);
  check_equal(native_io_release_pipe(&backend, new_endpoint), TURBO_OK);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_readiness_pipe_fifo_and_cancel(turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 2u, 4u, 4u};
  int descriptors[2] = {-1, -1};
  turbo_io_endpoint endpoints[2] = {0};
  turbo_io_request reads[3] = {0};
  turbo_io_request write_request = {0};
  turbo_io_completion events[4] = {0};
  unsigned char received[3] = {0};
  unsigned char payload[2] = {0x61u, 0x62u};
  turbo_io_operation read_operation;
  turbo_io_operation write_operation;
  size_t read_completion_index = 0u;
  uintptr_t read_order[2] = {0u, 0u};

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_pipe(descriptors, true), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[0]),
              TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[1],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[1]),
              TURBO_OK);
  read_operation = (turbo_io_operation){.kind = TURBO_IO_PIPE_READ,
                                        .endpoint = endpoints[0],
                                        .buffer = &received[0],
                                        .length = 1u,
                                        .user_data = 61u};
  check_equal(native_io_submit(&backend, &read_operation, &reads[0]), TURBO_OK);
  read_operation.buffer = &received[1];
  read_operation.user_data = 62u;
  check_equal(native_io_submit(&backend, &read_operation, &reads[1]), TURBO_OK);
  read_operation.buffer = &received[2];
  read_operation.user_data = 63u;
  check_equal(native_io_submit(&backend, &read_operation, &reads[2]), TURBO_OK);
  check_equal(native_io_cancel(&backend, reads[1]), TURBO_OK);
  write_operation = (turbo_io_operation){.kind = TURBO_IO_PIPE_WRITE,
                                         .endpoint = endpoints[1],
                                         .buffer = payload,
                                         .length = sizeof(payload),
                                         .user_data = 64u};
  check_equal(native_io_submit(&backend, &write_operation, &write_request), TURBO_OK);
  check_equal(native_io_test_observe_all(&backend, events, 4u), TURBO_OK);
  for (size_t index = 0u; index < 4u; ++index) {
    if (events[index].user_data == 62u)
      check_equal(events[index].kind, TURBO_IO_COMPLETION_CANCELLED);
    if (events[index].user_data == 61u || events[index].user_data == 63u)
      read_order[read_completion_index++] = events[index].user_data;
  }
  check_equal(read_completion_index, 2u);
  check_equal(read_order[0], (uintptr_t)61u);
  check_equal(read_order[1], (uintptr_t)63u);
  check_equal(received[0], payload[0]);
  check_equal(received[2], payload[1]);

  (void)close(descriptors[0]);
  (void)close(descriptors[1]);
  check_equal(native_io_release_pipe(&backend, endpoints[0]), TURBO_OK);
  check_equal(native_io_release_pipe(&backend, endpoints[1]), TURBO_OK);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_readiness_pipe_capacity_and_broken_peer(
    turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 1u, 1u, 1u};
  int descriptors[2] = {-1, -1};
  turbo_io_endpoint endpoint = {0};
  turbo_io_endpoint rejected = {1u, 1u};
  turbo_io_request request = {0};
  unsigned char byte = 0x71u;
  turbo_io_operation operation;
  sigset_t pending;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_pipe(descriptors, true), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[1],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
              TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &rejected),
              TURBO_ENOBUFS);
  check_false(turbo_io_endpoint_valid(rejected));
  (void)close(descriptors[0]);
  descriptors[0] = -1;
  operation = (turbo_io_operation){.kind = TURBO_IO_PIPE_WRITE,
                                   .endpoint = endpoint,
                                   .buffer = &byte,
                                   .length = sizeof(byte)};
  check_equal(native_io_submit(&backend, &operation, &request), -EPIPE);
  check_false(turbo_io_request_valid(request));
  check_equal(sigpending(&pending), 0);
  check_equal(sigismember(&pending, SIGPIPE), 0);
  (void)close(descriptors[1]);
  check_equal(native_io_release_pipe(&backend, endpoint), TURBO_OK);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}
#endif

static int native_io_test_bind_loopback(native_io_test_socket socket_value,
                                        struct sockaddr_in *address) {
  native_io_test_socklen address_length = (native_io_test_socklen)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(socket_value, (const struct sockaddr *)address,
           (native_io_test_socklen)sizeof(*address)) != 0)
    return native_io_test_last_error();
  if (getsockname(socket_value, (struct sockaddr *)address, &address_length) != 0)
    return native_io_test_last_error();
  return TURBO_OK;
}

static int native_io_test_make_tcp_pair(native_io_test_socket sockets[2]) {
  native_io_test_socket listener = NATIVE_IO_TEST_INVALID_SOCKET;
  struct sockaddr_in address;
  int status;

  sockets[0] = NATIVE_IO_TEST_INVALID_SOCKET;
  sockets[1] = NATIVE_IO_TEST_INVALID_SOCKET;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == NATIVE_IO_TEST_INVALID_SOCKET) return native_io_test_last_error();
  status = native_io_test_bind_loopback(listener, &address);
  if (status == TURBO_OK && listen(listener, 1) != 0) status = native_io_test_last_error();
  if (status == TURBO_OK) {
    sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets[0] == NATIVE_IO_TEST_INVALID_SOCKET) status = native_io_test_last_error();
  }
  if (status == TURBO_OK && connect(sockets[0], (const struct sockaddr *)&address,
                                    (native_io_test_socklen)sizeof(address)) != 0)
    status = native_io_test_last_error();
  if (status == TURBO_OK) {
    sockets[1] = accept(listener, NULL, NULL);
    if (sockets[1] == NATIVE_IO_TEST_INVALID_SOCKET) status = native_io_test_last_error();
  }
  native_io_test_close_socket(listener);
  if (status != TURBO_OK) {
    native_io_test_close_socket(sockets[0]);
    native_io_test_close_socket(sockets[1]);
    sockets[0] = NATIVE_IO_TEST_INVALID_SOCKET;
    sockets[1] = NATIVE_IO_TEST_INVALID_SOCKET;
  }
  return status;
}

static int native_io_test_make_udp_pair(native_io_test_socket sockets[2],
                                        struct sockaddr_in addresses[2]) {
  int status = TURBO_OK;
  sockets[0] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockets[0] == NATIVE_IO_TEST_INVALID_SOCKET) return native_io_test_last_error();
  sockets[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockets[1] == NATIVE_IO_TEST_INVALID_SOCKET) status = native_io_test_last_error();
  if (status == TURBO_OK) status = native_io_test_bind_loopback(sockets[0], &addresses[0]);
  if (status == TURBO_OK) status = native_io_test_bind_loopback(sockets[1], &addresses[1]);
  if (status != TURBO_OK) {
    native_io_test_close_socket(sockets[0]);
    native_io_test_close_socket(sockets[1]);
    sockets[0] = NATIVE_IO_TEST_INVALID_SOCKET;
    sockets[1] = NATIVE_IO_TEST_INVALID_SOCKET;
  }
  return status;
}

static int native_io_test_observe_all(turbo_io_backend *backend, turbo_io_completion *events,
                                      size_t expected) {
  size_t total = 0u;
  while (total < expected) {
    size_t count = 0u;
    int status = native_io_observe(backend, events + total, expected - total,
                                          NATIVE_IO_TEST_TIMEOUT_MS, &count);
    if (status != TURBO_OK) return status;
    total += count;
  }
  return TURBO_OK;
}

static void native_io_test_close_endpoint(turbo_io_backend *backend, turbo_io_endpoint endpoint,
                                          native_io_test_socket socket_value) {
  native_io_test_close_socket(socket_value);
  check_equal(native_io_release_socket(backend, endpoint), TURBO_OK);
}

static void native_io_test_round_trip_tcp(turbo_io_backend_kind kind) {
  static const unsigned char payload[] = {0x31u, 0x32u, 0x33u, 0x34u};
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, NATIVE_IO_TEST_ENDPOINT_CAPACITY,
                                          NATIVE_IO_TEST_REQUEST_CAPACITY,
                                          NATIVE_IO_TEST_BATCH_CAPACITY};
  native_io_test_socket sockets[2];
  turbo_io_endpoint endpoints[2] = {0};
  turbo_io_request requests[2] = {0};
  turbo_io_completion events[2] = {0};
  unsigned char received[sizeof(payload)] = {0};
  turbo_io_operation operations[2];
  bool saw_send = false;
  bool saw_receive = false;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_tcp_pair(sockets), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[0], &endpoints[0]),
              TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoints[1]),
              TURBO_OK);
  operations[0] = (turbo_io_operation){.kind = TURBO_IO_TCP_RECV,
                                       .endpoint = endpoints[1],
                                       .buffer = received,
                                       .length = sizeof(received),
                                       .user_data = 11u};
  operations[1] = (turbo_io_operation){.kind = TURBO_IO_TCP_SEND,
                                       .endpoint = endpoints[0],
                                       .buffer = (void *)payload,
                                       .length = sizeof(payload),
                                       .user_data = 12u};
  check_equal(native_io_submit(&backend, &operations[0], &requests[0]), TURBO_OK);
  check_equal(native_io_submit(&backend, &operations[1], &requests[1]), TURBO_OK);
  check_true(turbo_io_request_valid(requests[0]));
  check_true(turbo_io_request_valid(requests[1]));
  check_equal(native_io_test_observe_all(&backend, events, 2u), TURBO_OK);

  for (size_t index = 0u; index < 2u; ++index) {
    check_equal(events[index].kind, TURBO_IO_COMPLETION_OK);
    check_equal(events[index].status, TURBO_OK);
    check_equal(events[index].bytes, sizeof(payload));
    if (events[index].user_data == 11u) saw_receive = true;
    else if (events[index].user_data == 12u) saw_send = true;
  }
  check_true(saw_receive);
  check_true(saw_send);
  check_equal(memcmp(received, payload, sizeof(payload)), 0);

  native_io_test_close_endpoint(&backend, endpoints[0], sockets[0]);
  native_io_test_close_endpoint(&backend, endpoints[1], sockets[1]);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
  check_null(backend.impl);
}

static void native_io_test_fifo_tcp_receives(turbo_io_backend_kind kind) {
  static const unsigned char payload[] = {0x51u, 0x52u};
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 2u, 3u, 3u};
  native_io_test_socket sockets[2];
  turbo_io_endpoint endpoints[2] = {0};
  turbo_io_request requests[3] = {0};
  turbo_io_completion events[3] = {0};
  unsigned char received[2] = {0};
  turbo_io_operation operations[3];

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_tcp_pair(sockets), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[0], &endpoints[0]),
              TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoints[1]),
              TURBO_OK);
  operations[0] = (turbo_io_operation){.kind = TURBO_IO_TCP_RECV,
                                       .endpoint = endpoints[1],
                                       .buffer = &received[0],
                                       .length = 1u,
                                       .user_data = 51u};
  operations[1] = (turbo_io_operation){.kind = TURBO_IO_TCP_RECV,
                                       .endpoint = endpoints[1],
                                       .buffer = &received[1],
                                       .length = 1u,
                                       .user_data = 52u};
  operations[2] = (turbo_io_operation){.kind = TURBO_IO_TCP_SEND,
                                       .endpoint = endpoints[0],
                                       .buffer = (void *)payload,
                                       .length = sizeof(payload),
                                       .user_data = 53u};
  check_equal(native_io_submit(&backend, &operations[0], &requests[0]), TURBO_OK);
  check_equal(native_io_submit(&backend, &operations[1], &requests[1]), TURBO_OK);
  check_equal(native_io_submit(&backend, &operations[2], &requests[2]), TURBO_OK);
  check_equal(native_io_test_observe_all(&backend, events, 3u), TURBO_OK);
  check_equal(received[0], payload[0]);
  check_equal(received[1], payload[1]);

  native_io_test_close_endpoint(&backend, endpoints[0], sockets[0]);
  native_io_test_close_endpoint(&backend, endpoints[1], sockets[1]);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_cancel_pending(turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 1u, 1u, 1u};
  native_io_test_socket sockets[2];
  turbo_io_endpoint endpoint = {0};
  turbo_io_request request = {0};
  turbo_io_completion event = {0};
  unsigned char byte = 0u;
  turbo_io_operation operation;
  size_t count = 0u;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_tcp_pair(sockets), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoint), TURBO_OK);
  operation = (turbo_io_operation){.kind = TURBO_IO_TCP_RECV,
                                   .endpoint = endpoint,
                                   .buffer = &byte,
                                   .length = sizeof(byte),
                                   .user_data = 21u};
  check_equal(native_io_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_cancel(&backend, request), TURBO_OK);
  check_equal(native_io_release_socket(&backend, endpoint), TURBO_EBUSY);
  check_equal(native_io_observe(&backend, &event, 1u, NATIVE_IO_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(event.kind, TURBO_IO_COMPLETION_CANCELLED);
  check_equal(event.status, TURBO_ECANCELED);
  check_equal(event.user_data, 21u);

  native_io_test_close_socket(sockets[0]);
  native_io_test_close_endpoint(&backend, endpoint, sockets[1]);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_cancel_same_lane(turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 1u, 2u, 2u};
  native_io_test_socket sockets[2];
  turbo_io_endpoint endpoint = {0};
  turbo_io_request requests[2] = {0};
  turbo_io_completion events[2] = {0};
  unsigned char bytes[2] = {0};
  turbo_io_operation operations[2];
  int cancel_status[2];
  bool saw_first = false;
  bool saw_second = false;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_tcp_pair(sockets), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoint), TURBO_OK);
  for (size_t index = 0u; index < 2u; ++index) {
    operations[index] = (turbo_io_operation){.kind = TURBO_IO_TCP_RECV,
                                             .endpoint = endpoint,
                                             .buffer = &bytes[index],
                                             .length = 1u,
                                             .user_data = 61u + index};
    check_equal(native_io_submit(&backend, &operations[index], &requests[index]), TURBO_OK);
  }
  cancel_status[1] = native_io_cancel(&backend, requests[1]);
  cancel_status[0] = native_io_cancel(&backend, requests[0]);
  check_true(cancel_status[1] == TURBO_OK || cancel_status[1] == TURBO_EALREADY);
  check_true(cancel_status[0] == TURBO_OK || cancel_status[0] == TURBO_EALREADY);
  check_equal(native_io_test_observe_all(&backend, events, 2u), TURBO_OK);
  for (size_t index = 0u; index < 2u; ++index) {
    check_equal(events[index].kind, TURBO_IO_COMPLETION_CANCELLED);
    check_equal(events[index].status, TURBO_ECANCELED);
    if (events[index].user_data == 61u) saw_first = true;
    else if (events[index].user_data == 62u) saw_second = true;
  }
  check_true(saw_first);
  check_true(saw_second);

  native_io_test_close_socket(sockets[0]);
  native_io_test_close_endpoint(&backend, endpoint, sockets[1]);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_round_trip_udp(turbo_io_backend_kind kind) {
  static const unsigned char payload[] = {0x41u, 0x42u, 0x43u};
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 2u, 2u, 2u};
  native_io_test_socket sockets[2];
  struct sockaddr_in addresses[2];
  struct sockaddr_storage peer_address;
  turbo_io_endpoint endpoints[2] = {0};
  turbo_io_request requests[2] = {0};
  turbo_io_completion events[2] = {0};
  unsigned char received[sizeof(payload)] = {0};
  turbo_io_operation receive_operation;
  turbo_io_operation send_operation;
  bool saw_receive = false;
  bool saw_send = false;

  memset(&peer_address, 0, sizeof(peer_address));
  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_udp_pair(sockets, addresses), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[0], &endpoints[0]),
              TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoints[1]),
              TURBO_OK);
  receive_operation = (turbo_io_operation){.kind = TURBO_IO_UDP_RECV_FROM,
                                           .endpoint = endpoints[1],
                                           .buffer = received,
                                           .length = sizeof(received),
                                           .user_data = 41u,
                                           .address = &peer_address,
                                           .address_capacity = sizeof(peer_address)};
  send_operation = (turbo_io_operation){.kind = TURBO_IO_UDP_SEND_TO,
                                        .endpoint = endpoints[0],
                                        .buffer = (void *)payload,
                                        .length = sizeof(payload),
                                        .user_data = 42u,
                                        .address = &addresses[1],
                                        .address_capacity = sizeof(addresses[1]),
                                        .address_length = sizeof(addresses[1])};
  check_equal(native_io_submit(&backend, &receive_operation, &requests[0]), TURBO_OK);
  check_equal(native_io_submit(&backend, &send_operation, &requests[1]), TURBO_OK);
  check_equal(native_io_test_observe_all(&backend, events, 2u), TURBO_OK);

  for (size_t index = 0u; index < 2u; ++index) {
    check_equal(events[index].kind, TURBO_IO_COMPLETION_OK);
    check_equal(events[index].bytes, sizeof(payload));
    if (events[index].user_data == 41u) {
      const struct sockaddr_in *peer = (const struct sockaddr_in *)&peer_address;
      saw_receive = true;
      check_equal(events[index].address_length, sizeof(*peer));
      check_equal(peer->sin_family, AF_INET);
      check_equal(peer->sin_port, addresses[0].sin_port);
    } else if (events[index].user_data == 42u) {
      saw_send = true;
      check_equal(events[index].address_length, 0u);
    }
  }
  check_true(saw_receive);
  check_true(saw_send);
  check_equal(memcmp(received, payload, sizeof(payload)), 0);

  native_io_test_close_endpoint(&backend, endpoints[0], sockets[0]);
  native_io_test_close_endpoint(&backend, endpoints[1], sockets[1]);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_capacity_and_close(turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 1u, 1u, 1u};
  native_io_test_socket sockets[2];
  turbo_io_endpoint endpoint = {0};
  turbo_io_endpoint rejected_endpoint = {0};
  turbo_io_request request = {0};
  turbo_io_request rejected_request = {0};
  turbo_io_completion event = {0};
  turbo_io_backend_stats stats = {0};
  unsigned char byte = 0u;
  turbo_io_operation operation;
  size_t count = 99u;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_tcp_pair(sockets), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoint), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &rejected_endpoint),
              TURBO_EALREADY);
  check_false(turbo_io_endpoint_valid(rejected_endpoint));
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[0], &rejected_endpoint),
              TURBO_ENOBUFS);
  check_equal(native_io_observe(&backend, &event, 1u, 0u, &count), TURBO_ETIMEDOUT);
  check_equal(count, 0u);

  operation = (turbo_io_operation){.kind = TURBO_IO_TCP_RECV,
                                   .endpoint = endpoint,
                                   .buffer = &byte,
                                   .length = sizeof(byte),
                                   .user_data = 31u};
  check_equal(native_io_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_submit(&backend, &operation, &rejected_request), TURBO_ENOBUFS);
  check_false(turbo_io_request_valid(rejected_request));
  check_true(native_io_get_stats(&backend, &stats));
  check_equal(stats.endpoint_count, 1u);
  check_equal(stats.active_requests, 1u);
  check_equal(stats.rejected_full, 1u);

  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_submit(&backend, &operation, &rejected_request), TURBO_ESHUTDOWN);
  check_equal(native_io_destroy(&backend), TURBO_EBUSY);
  check_equal(native_io_cancel(&backend, request), TURBO_OK);
  check_equal(native_io_observe(&backend, &event, 1u, NATIVE_IO_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(event.kind, TURBO_IO_COMPLETION_CANCELLED);
  check_equal(native_io_cancel(&backend, request), TURBO_ENOENT);

  native_io_test_close_socket(sockets[0]);
  native_io_test_close_endpoint(&backend, endpoint, sockets[1]);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_reject_pipe_operation_on_socket(turbo_io_backend_kind kind) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {kind, 1u, 1u, 1u};
  native_io_test_socket sockets[2];
  turbo_io_endpoint endpoint = {0};
  turbo_io_request request = {0};
  unsigned char byte = 0u;
  const turbo_io_operation operation = {.kind = TURBO_IO_PIPE_READ,
                                        .endpoint = endpoint,
                                        .buffer = &byte,
                                        .length = sizeof(byte)};
  turbo_io_operation submitted = operation;

  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_test_make_tcp_pair(sockets), TURBO_OK);
  check_equal(native_io_attach_socket(&backend, (uintptr_t)sockets[1], &endpoint),
              TURBO_OK);
  submitted.endpoint = endpoint;
  check_equal(native_io_submit(&backend, &submitted, &request), TURBO_EINVAL);
  check_false(turbo_io_request_valid(request));

  native_io_test_close_socket(sockets[0]);
  native_io_test_close_endpoint(&backend, endpoint, sockets[1]);
  check_equal(native_io_close(&backend), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

#if defined(_WIN32)
static void native_io_test_close_pipe(HANDLE pipe_handle) {
  if (pipe_handle != NULL && pipe_handle != INVALID_HANDLE_VALUE)
    (void)CloseHandle(pipe_handle);
}

static int native_io_test_make_named_pipe_pair(HANDLE pipes[2], bool outbound_only) {
  static LONG sequence = 0;
  char name[128];
  OVERLAPPED connected = {0};
  HANDLE event = NULL;
  DWORD error = ERROR_SUCCESS;
  BOOL pending = FALSE;
  int name_length;

  pipes[0] = INVALID_HANDLE_VALUE;
  pipes[1] = INVALID_HANDLE_VALUE;
  name_length = snprintf(name, sizeof(name), "\\\\.\\pipe\\native-io-test-%lu-%ld",
                         GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (name_length < 0 || (size_t)name_length >= sizeof(name)) return TURBO_ERANGE;

  pipes[0] = CreateNamedPipeA(name,
                              (outbound_only ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_DUPLEX) |
                                  FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                              NATIVE_IO_TEST_PIPE_BUFFER_CAPACITY,
                              NATIVE_IO_TEST_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (pipes[0] == INVALID_HANDLE_VALUE) return -(int)GetLastError();
  event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    error = GetLastError();
    goto failed;
  }
  connected.hEvent = event;
  if (!ConnectNamedPipe(pipes[0], &connected)) {
    error = GetLastError();
    if (error == ERROR_IO_PENDING)
      pending = TRUE;
    else if (error != ERROR_PIPE_CONNECTED)
      goto failed;
  }
  pipes[1] = CreateFileA(name, outbound_only ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                         0u, NULL, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, NULL);
  if (pipes[1] == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    goto failed;
  }
  if (pending) {
    DWORD transferred = 0u;
    if (!GetOverlappedResult(pipes[0], &connected, &transferred, TRUE)) {
      error = GetLastError();
      goto failed;
    }
  }
  (void)CloseHandle(event);
  return TURBO_OK;

failed:
  native_io_test_close_pipe(pipes[1]);
  native_io_test_close_pipe(pipes[0]);
  if (event != NULL) (void)CloseHandle(event);
  pipes[0] = INVALID_HANDLE_VALUE;
  pipes[1] = INVALID_HANDLE_VALUE;
  return -(int)error;
}

static void native_io_test_iocp_pipe_round_trip(void) {
  static const unsigned char payload[] = {0x70u, 0x69u, 0x70u, 0x65u};
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {TURBO_IO_BACKEND_IOCP, 2u, 2u, 2u};
  HANDLE pipes[2];
  turbo_io_endpoint endpoints[2] = {0};
  turbo_io_endpoint duplicate = {9u, 9u};
  turbo_io_request requests[2] = {0};
  turbo_io_completion events[2] = {0};
  unsigned char received[sizeof(payload)] = {0};
  turbo_io_operation operations[2];
  size_t count = 0u;

  check_equal(native_io_test_make_named_pipe_pair(pipes, false), TURBO_OK);
  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipes[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                           &endpoints[0]),
              TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipes[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &duplicate),
              TURBO_EALREADY);
  check_false(turbo_io_endpoint_valid(duplicate));
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipes[1],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                           &endpoints[1]),
              TURBO_OK);
  operations[0] = (turbo_io_operation){TURBO_IO_PIPE_READ, endpoints[0], received,
                                       sizeof(received), 71u, NULL, 0u, 0u};
  operations[1] = (turbo_io_operation){TURBO_IO_PIPE_WRITE, endpoints[1], (void *)payload,
                                       sizeof(payload), 72u, NULL, 0u, 0u};
  check_equal(native_io_submit(&backend, &operations[0], &requests[0]), TURBO_OK);
  check_equal(native_io_submit(&backend, &operations[1], &requests[1]), TURBO_OK);
  check_equal(native_io_test_observe_all(&backend, events, 2u), TURBO_OK);
  for (count = 0u; count < 2u; ++count) {
    check_equal(events[count].kind, TURBO_IO_COMPLETION_OK);
    check_equal(events[count].bytes, sizeof(payload));
  }
  check_equal(received, payload, sizeof(payload));

  check_equal(native_io_close(&backend), TURBO_OK);
  native_io_test_close_pipe(pipes[0]);
  native_io_test_close_pipe(pipes[1]);
  check_equal(native_io_release_pipe(&backend, endpoints[0]), TURBO_OK);
  check_equal(native_io_release_pipe(&backend, endpoints[1]), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}

static void native_io_test_iocp_pipe_cancel_and_eof(void) {
  turbo_io_backend backend = {0};
  const turbo_io_backend_config config = {TURBO_IO_BACKEND_IOCP, 1u, 1u, 1u};
  HANDLE pipes[2];
  turbo_io_endpoint endpoint = {0};
  turbo_io_request request = {0};
  turbo_io_completion event = {0};
  unsigned char byte = 0u;
  turbo_io_operation operation;
  size_t count = 0u;

  check_equal(native_io_test_make_named_pipe_pair(pipes, false), TURBO_OK);
  check_equal(native_io_init(&backend, &config), TURBO_OK);
  check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipes[0],
                                           TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
              TURBO_OK);
  operation = (turbo_io_operation){TURBO_IO_PIPE_READ, endpoint, &byte, sizeof(byte),
                                   73u, NULL, 0u, 0u};
  check_equal(native_io_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_cancel(&backend, request), TURBO_OK);
  check_equal(native_io_observe(&backend, &event, 1u, NATIVE_IO_TEST_TIMEOUT_MS,
                                       &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(event.kind, TURBO_IO_COMPLETION_CANCELLED);
  check_equal(event.status, TURBO_ECANCELED);

  request = (turbo_io_request){0};
  event = (turbo_io_completion){0};
  count = 0u;
  check_equal(native_io_submit(&backend, &operation, &request), TURBO_OK);
  native_io_test_close_pipe(pipes[1]);
  pipes[1] = INVALID_HANDLE_VALUE;
  check_equal(native_io_observe(&backend, &event, 1u, NATIVE_IO_TEST_TIMEOUT_MS,
                                       &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(event.kind, TURBO_IO_COMPLETION_EOF);
  check_equal(event.status, TURBO_EOF);

  check_equal(native_io_close(&backend), TURBO_OK);
  native_io_test_close_pipe(pipes[0]);
  check_equal(native_io_release_pipe(&backend, endpoint), TURBO_OK);
  check_equal(native_io_destroy(&backend), TURBO_OK);
}
#endif

spec("NativeIO direct backend") {
  it("describes every explicit backend model without fallback") {
    check_equal(native_io_get_model(TURBO_IO_BACKEND_IOCP), TURBO_IO_MODEL_COMPLETION);
    check_equal(native_io_get_model(TURBO_IO_BACKEND_EPOLL), TURBO_IO_MODEL_READINESS);
    check_equal(native_io_get_model(TURBO_IO_BACKEND_IO_URING), TURBO_IO_MODEL_COMPLETION);
    check_equal(native_io_get_model(TURBO_IO_BACKEND_KQUEUE), TURBO_IO_MODEL_READINESS);
#if defined(_WIN32)
    check_true(native_io_pipe_supported(TURBO_IO_BACKEND_IOCP));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IO_URING));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_KQUEUE));
    check_true(native_io_supported(TURBO_IO_BACKEND_IOCP));
    check_false(native_io_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_supported(TURBO_IO_BACKEND_IO_URING));
    check_false(native_io_supported(TURBO_IO_BACKEND_KQUEUE));
#elif defined(__linux__)
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IOCP));
    check_true(native_io_pipe_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IO_URING));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_KQUEUE));
    check_false(native_io_supported(TURBO_IO_BACKEND_IOCP));
    check_true(native_io_supported(TURBO_IO_BACKEND_EPOLL));
    check_true(native_io_supported(TURBO_IO_BACKEND_IO_URING));
    check_false(native_io_supported(TURBO_IO_BACKEND_KQUEUE));
#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
       defined(__DragonFly__)) && \
    UINTPTR_MAX > UINT32_MAX
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IOCP));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IO_URING));
    check_true(native_io_pipe_supported(TURBO_IO_BACKEND_KQUEUE));
    check_false(native_io_supported(TURBO_IO_BACKEND_IOCP));
    check_false(native_io_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_supported(TURBO_IO_BACKEND_IO_URING));
    check_true(native_io_supported(TURBO_IO_BACKEND_KQUEUE));
#else
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IOCP));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_IO_URING));
    check_false(native_io_pipe_supported(TURBO_IO_BACKEND_KQUEUE));
    check_false(native_io_supported(TURBO_IO_BACKEND_IOCP));
    check_false(native_io_supported(TURBO_IO_BACKEND_EPOLL));
    check_false(native_io_supported(TURBO_IO_BACKEND_IO_URING));
    check_false(native_io_supported(TURBO_IO_BACKEND_KQUEUE));
#endif
  }

  it("rejects malformed bounded configuration and clears output") {
    turbo_io_backend backend = {(void *)(uintptr_t)1u};
    const turbo_io_backend_config config = {TURBO_IO_BACKEND_IOCP, 0u, 1u, 1u};
    check_equal(native_io_init(&backend, &config), TURBO_EINVAL);
    check_null(backend.impl);
  }

  it("validates TCP and UDP address ownership shapes") {
    unsigned char payload = 0u;
    unsigned char address[32] = {0};
    turbo_io_operation operation = {.kind = TURBO_IO_UDP_RECV_FROM,
                                    .endpoint = {1u, 1u},
                                    .buffer = &payload,
                                    .length = sizeof(payload)};

    check_false(turbo_io_operation_valid(&operation));
    operation.address = address;
    operation.address_capacity = sizeof(address);
    check_true(turbo_io_operation_valid(&operation));
    operation.kind = TURBO_IO_UDP_SEND_TO;
    check_false(turbo_io_operation_valid(&operation));
    operation.address_length = sizeof(address);
    check_true(turbo_io_operation_valid(&operation));
    operation.kind = TURBO_IO_TCP_SEND;
    check_false(turbo_io_operation_valid(&operation));
    operation.address = NULL;
    operation.address_capacity = 0u;
    operation.address_length = 0u;
    check_true(turbo_io_operation_valid(&operation));
  }

  it("preserves socket operation values and validates byte-pipe shapes") {
    unsigned char payload = 0u;
    turbo_io_operation operation = {.kind = TURBO_IO_PIPE_READ,
                                    .endpoint = {1u, 1u},
                                    .buffer = &payload,
                                    .length = sizeof(payload)};

    check_equal(TURBO_IO_TCP_RECV, 1);
    check_equal(TURBO_IO_TCP_SEND, 2);
    check_equal(TURBO_IO_UDP_RECV_FROM, 3);
    check_equal(TURBO_IO_UDP_SEND_TO, 4);
    check_equal(TURBO_IO_PIPE_READ, 5);
    check_equal(TURBO_IO_PIPE_WRITE, 6);
    check_true(turbo_io_operation_valid(&operation));

    operation.address = &payload;
    check_false(turbo_io_operation_valid(&operation));
    operation.address = NULL;
    operation.address_capacity = sizeof(payload);
    check_false(turbo_io_operation_valid(&operation));
    operation.address_capacity = 0u;
    operation.address_length = sizeof(payload);
    check_false(turbo_io_operation_valid(&operation));

    operation.address_length = 0u;
    operation.kind = TURBO_IO_PIPE_WRITE;
    check_true(turbo_io_operation_valid(&operation));
  }

  it("clears a pipe endpoint when attach arguments are invalid") {
    turbo_io_endpoint endpoint = {1u, 1u};

    check_equal(native_io_attach_pipe(NULL, 0u,
                                             TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                             &endpoint),
                TURBO_EINVAL);
    check_false(turbo_io_endpoint_valid(endpoint));
  }

  it("rejects a byte-pipe operation on a socket endpoint before native admission") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_reject_pipe_operation_on_socket(backends[index]);
  }

#if defined(_WIN32)
  it("rejects synchronous anonymous pipes instead of selecting a worker fallback") {
    turbo_io_backend backend = {0};
    const turbo_io_backend_config config = {TURBO_IO_BACKEND_IOCP, 1u, 1u, 1u};
    HANDLE descriptors[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    turbo_io_endpoint endpoint = {0};

    check_true(CreatePipe(&descriptors[0], &descriptors[1], NULL, 0u));
    check_equal(native_io_init(&backend, &config), TURBO_OK);
    check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                             TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                TURBO_ENOTSUP);
    check_false(turbo_io_endpoint_valid(endpoint));
    check_equal(native_io_close(&backend), TURBO_OK);
    check_equal(native_io_destroy(&backend), TURBO_OK);
    native_io_test_close_pipe(descriptors[0]);
    native_io_test_close_pipe(descriptors[1]);
  }

  it("rejects message-mode named pipes before IOCP association") {
    static LONG sequence = 0;
    char name[128];
    turbo_io_backend backend = {0};
    const turbo_io_backend_config config = {TURBO_IO_BACKEND_IOCP, 1u, 1u, 1u};
    turbo_io_endpoint endpoint = {0};
    HANDLE pipe_handle;
    int name_length = snprintf(name, sizeof(name),
                               "\\\\.\\pipe\\native-io-message-test-%lu-%ld",
                               GetCurrentProcessId(), InterlockedIncrement(&sequence));

    check_true(name_length >= 0 && (size_t)name_length < sizeof(name));
    pipe_handle = CreateNamedPipeA(
        name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1u,
        NATIVE_IO_TEST_PIPE_BUFFER_CAPACITY, NATIVE_IO_TEST_PIPE_BUFFER_CAPACITY,
        0u, NULL);
    check_true(pipe_handle != INVALID_HANDLE_VALUE);
    check_equal(native_io_init(&backend, &config), TURBO_OK);
    check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipe_handle,
                                             TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                TURBO_EINVAL);
    check_false(turbo_io_endpoint_valid(endpoint));
    check_equal(native_io_close(&backend), TURBO_OK);
    check_equal(native_io_destroy(&backend), TURBO_OK);
    native_io_test_close_pipe(pipe_handle);
  }

  it("accepts an overlapped outbound server with least-privilege access") {
    static const unsigned char payload[] = {0x6cu, 0x65u, 0x61u, 0x73u, 0x74u};
    turbo_io_backend backend = {0};
    const turbo_io_backend_config config = {TURBO_IO_BACKEND_IOCP, 2u, 2u, 2u};
    HANDLE pipes[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    turbo_io_endpoint endpoints[2] = {0};
    turbo_io_request requests[2] = {0};
    turbo_io_completion events[2] = {0};
    unsigned char received[sizeof(payload)] = {0};
    turbo_io_operation operations[2];

    check_equal(native_io_test_make_named_pipe_pair(pipes, true), TURBO_OK);
    check_equal(native_io_init(&backend, &config), TURBO_OK);
    check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipes[0],
                                             TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[0]),
                TURBO_OK);
    check_equal(native_io_attach_pipe(&backend, (uintptr_t)pipes[1],
                                             TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoints[1]),
                TURBO_OK);
    operations[0] = (turbo_io_operation){TURBO_IO_PIPE_WRITE, endpoints[0], (void *)payload,
                                         sizeof(payload), 81u, NULL, 0u, 0u};
    operations[1] = (turbo_io_operation){TURBO_IO_PIPE_READ, endpoints[1], received,
                                         sizeof(received), 82u, NULL, 0u, 0u};
    check_equal(native_io_submit(&backend, &operations[0], &requests[0]), TURBO_OK);
    check_equal(native_io_submit(&backend, &operations[1], &requests[1]), TURBO_OK);
    check_equal(native_io_test_observe_all(&backend, events, 2u), TURBO_OK);
    check_equal(received, payload, sizeof(payload));
    check_equal(native_io_close(&backend), TURBO_OK);
    native_io_test_close_pipe(pipes[0]);
    native_io_test_close_pipe(pipes[1]);
    check_equal(native_io_release_pipe(&backend, endpoints[0]), TURBO_OK);
    check_equal(native_io_release_pipe(&backend, endpoints[1]), TURBO_OK);
    check_equal(native_io_destroy(&backend), TURBO_OK);
  }

  it("round trips byte-pipe payloads through IOCP completion") {
    native_io_test_iocp_pipe_round_trip();
  }

  it("publishes IOCP pipe cancellation and peer EOF as terminal completions") {
    native_io_test_iocp_pipe_cancel_and_eof();
  }
#endif

#if !defined(_WIN32)
  it("rejects blocking byte-pipe descriptors without changing their flags") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_readiness_backends(backends);
    for (size_t index = 0u; index < count; ++index) {
      turbo_io_backend backend = {0};
      const turbo_io_backend_config config = {backends[index], 1u, 1u, 1u};
      int descriptors[2] = {-1, -1};
      turbo_io_endpoint endpoint = {1u, 1u};

      check_equal(native_io_init(&backend, &config), TURBO_OK);
      check_equal(native_io_test_make_pipe(descriptors, false), TURBO_OK);
      check_equal(native_io_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                               TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                  TURBO_EINVAL);
      check_false(turbo_io_endpoint_valid(endpoint));
      check_equal(fcntl(descriptors[0], F_GETFL, 0) & O_NONBLOCK, 0);
      (void)close(descriptors[0]);
      (void)close(descriptors[1]);
      check_equal(native_io_close(&backend), TURBO_OK);
      check_equal(native_io_destroy(&backend), TURBO_OK);
    }
  }

  it("round trips bytes through readiness pipe endpoints") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_readiness_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_readiness_pipe_round_trip(backends[index]);
  }

  it("publishes pipe EOF and rejects stale endpoints after descriptor reuse") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_readiness_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_readiness_pipe_eof_and_reuse(backends[index]);
  }

  it("preserves pipe read FIFO order while cancelling a queued entry") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_readiness_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_readiness_pipe_fifo_and_cancel(backends[index]);
  }

  it("enforces pipe capacity and contains broken-peer SIGPIPE") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_readiness_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_readiness_pipe_capacity_and_broken_peer(backends[index]);
  }
#endif

  it("round trips TCP through every platform backend") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_round_trip_tcp(backends[index]);
  }

  it("preserves FIFO receive lanes on one endpoint") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_fifo_tcp_receives(backends[index]);
  }

  it("keeps cancellation borrowed until terminal observation") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_cancel_pending(backends[index]);
  }

  it("cancels both active and queued operations in one lane") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_cancel_same_lane(backends[index]);
  }

  it("round trips UDP and publishes its peer address") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_round_trip_udp(backends[index]);
  }

  it("enforces capacity stale-handle and close boundaries") {
    turbo_io_backend_kind backends[NATIVE_IO_TEST_MAX_BACKENDS];
    const size_t count = native_io_test_backends(backends);
    for (size_t index = 0u; index < count; ++index)
      native_io_test_capacity_and_close(backends[index]);
  }
}
