#include "cnet_test_named_pipe.h"
#include "cnet_test_pipe.h"
#include "cnet_transport.h"
#include "tinytest.h"
#include <turbo/native_io.h>

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_test_socket;
  #define CNET_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_test_socket;
  #define CNET_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_TEST_TIMEOUT_MS = 5000, CNET_TEST_MAX_BACKENDS = 2 };

static size_t cnet_test_backends(native_io_backend_kind backends[CNET_TEST_MAX_BACKENDS]) {
#if defined(_WIN32)
  backends[0] = NATIVE_IO_BACKEND_IOCP;
  return 1u;
#elif defined(__linux__)
  backends[0] = NATIVE_IO_BACKEND_EPOLL;
  backends[1] = NATIVE_IO_BACKEND_IO_URING;
  return 2u;
#elif defined(__APPLE__) && UINTPTR_MAX > UINT32_MAX
  backends[0] = NATIVE_IO_BACKEND_KQUEUE;
  return 1u;
#else
  (void)backends;
  return 0u;
#endif
}

static void cnet_test_close_socket(cnet_test_socket socket_value) {
  if (socket_value == CNET_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_test_listener(cnet_test_socket *out_listener, struct sockaddr_in *out_address) {
#if defined(_WIN32)
  int length = (int)sizeof(*out_address);
#else
  socklen_t length = (socklen_t)sizeof(*out_address);
#endif
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)out_address, &length) != 0 ||
      listen(*out_listener, 1) != 0) {
    cnet_test_close_socket(*out_listener);
    *out_listener = CNET_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  return TURBO_OK;
}

static int cnet_test_udp_peer(cnet_test_socket *out_socket, struct sockaddr_in *out_address) {
#if defined(_WIN32)
  int length = (int)sizeof(*out_address);
#else
  socklen_t length = (socklen_t)sizeof(*out_address);
#endif
  *out_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (*out_socket == CNET_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_socket, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_socket, (struct sockaddr *)out_address, &length) != 0) {
    cnet_test_close_socket(*out_socket);
    *out_socket = CNET_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  return TURBO_OK;
}

static void cnet_test_tcp_transport(native_io_backend_kind kind) {
  native_io_backend backend = {0};
  const native_io_backend_config config = {kind, 1u, 1u, 1u};
  cnet_transport transport = {0};
  cnet_test_socket listener = CNET_TEST_INVALID_SOCKET;
  cnet_test_socket accepted = CNET_TEST_INVALID_SOCKET;
  struct sockaddr_in address;
  native_io_request request = {0};
  native_io_completion completion = {0};
  size_t count = 0u;

  check_equal(native_io_backend_init(&backend, &config), TURBO_OK);
  check_equal(cnet_test_listener(&listener, &address), TURBO_OK);
  check_equal(cnet_transport_tcp_connect(&transport, &backend, kind, &address, sizeof(address), 91u,
                                         &request),
              TURBO_OK);
  check_true(native_io_request_valid(request));
  check_equal(native_io_backend_observe(&backend, &completion, 1u, CNET_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
  check_equal(completion.user_data, 91u);
  accepted = accept(listener, NULL, NULL);
  check_true(accepted != CNET_TEST_INVALID_SOCKET);

  cnet_test_close_socket(accepted);
  cnet_test_close_socket(listener);
  check_equal(cnet_transport_close(&transport, &backend), TURBO_OK);
  check_equal(native_io_backend_close(&backend), TURBO_OK);
  check_equal(native_io_backend_destroy(&backend), TURBO_OK);
}

static void cnet_test_udp_transport(native_io_backend_kind kind) {
  static const unsigned char payload[] = {7u, 8u, 9u};
  native_io_backend backend = {0};
  const native_io_backend_config config = {kind, 1u, 1u, 1u};
  cnet_transport transport = {0};
  cnet_test_socket peer = CNET_TEST_INVALID_SOCKET;
  struct sockaddr_in address;
  native_io_request request = {0};
  native_io_completion completion = {0};
  native_io_operation operation;
  unsigned char received[sizeof(payload)] = {0};
  size_t count = 0u;

  check_equal(native_io_backend_init(&backend, &config), TURBO_OK);
  check_equal(cnet_test_udp_peer(&peer, &address), TURBO_OK);
  check_equal(cnet_transport_udp_connect(&transport, &backend, kind, &address, sizeof(address)),
              TURBO_OK);
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_UDP_SEND_TO,
                                    .endpoint = transport.endpoint,
                                    .buffer = (void *)payload,
                                    .length = sizeof(payload)};
  check_equal(native_io_backend_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_backend_observe(&backend, &completion, 1u, CNET_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
  check_equal(recv(peer, (char *)received, (int)sizeof(received), 0), (int)sizeof(received));
  check_equal(received, payload, sizeof(payload));

  cnet_test_close_socket(peer);
  check_equal(cnet_transport_close(&transport, &backend), TURBO_OK);
  check_equal(native_io_backend_close(&backend), TURBO_OK);
  check_equal(native_io_backend_destroy(&backend), TURBO_OK);
}

static void cnet_test_pipe_transport(native_io_backend_kind kind) {
  static const unsigned char inbound[] = {1u, 2u, 3u, 4u};
  static const unsigned char outbound[] = {9u, 8u, 7u, 6u};
  native_io_backend backend = {0};
  const native_io_backend_config config = {kind, 2u, 1u, 1u};
  cnet_transport transport = {0};
  cnet_shared_test_pipe_pair pair;
  native_io_request request = {0};
  native_io_completion completion = {0};
  native_io_operation operation;
  unsigned char received[sizeof(inbound)] = {0};
  size_t count = 0u;

  check_equal(cnet_shared_test_make_pipe_pair(&pair), TURBO_OK);
  check_equal(native_io_backend_init(&backend, &config), TURBO_OK);
  check_equal(cnet_transport_adopt_pipe(&transport, &backend, pair.cnet_read, pair.cnet_write),
              TURBO_OK);
  pair.cnet_read = pair.cnet_write = UINTPTR_MAX;

  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                    .endpoint = cnet_transport_read_endpoint(&transport),
                                    .buffer = received,
                                    .length = sizeof(received)};
  check_equal(native_io_backend_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(cnet_shared_test_pipe_peer_write(pair.peer_write, inbound, sizeof(inbound)),
              TURBO_OK);
  check_equal(native_io_backend_observe(&backend, &completion, 1u, CNET_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
  check_equal(received, inbound, sizeof(inbound));

  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_WRITE,
                                    .endpoint = cnet_transport_write_endpoint(&transport),
                                    .buffer = (void *)outbound,
                                    .length = sizeof(outbound)};
  check_equal(native_io_backend_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_backend_observe(&backend, &completion, 1u, CNET_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
  memset(received, 0, sizeof(received));
  check_equal(cnet_shared_test_pipe_peer_read(pair.peer_read, received, sizeof(received)),
              TURBO_OK);
  check_equal(received, outbound, sizeof(outbound));

  check_equal(cnet_transport_close(&transport, &backend), TURBO_OK);
  cnet_shared_test_close_pipe_pair(&pair);
  check_equal(native_io_backend_close(&backend), TURBO_OK);
  check_equal(native_io_backend_destroy(&backend), TURBO_OK);
}

static void cnet_test_named_pipe_transport(native_io_backend_kind kind) {
  static const unsigned char inbound[] = {3u, 1u, 4u, 1u};
  static const unsigned char outbound[] = {2u, 7u, 1u, 8u};
  native_io_backend backend = {0};
  const native_io_backend_config config = {kind, 2u, 1u, 1u};
  cnet_transport transport = {0};
  cnet_shared_test_named_pipe pipe;
  native_io_request request = {0};
  native_io_completion completion = {0};
  native_io_operation operation;
  unsigned char received[sizeof(inbound)] = {0};
  size_t count = 0u;

  check_equal(cnet_shared_test_named_pipe_start(&pipe), TURBO_OK);
  check_equal(native_io_backend_init(&backend, &config), TURBO_OK);
  check_equal(cnet_transport_pipe_connect(&transport, &backend, kind, pipe.name), TURBO_OK);
  check_equal(cnet_shared_test_named_pipe_finish(&pipe), TURBO_OK);

  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                    .endpoint = cnet_transport_read_endpoint(&transport),
                                    .buffer = received,
                                    .length = sizeof(received)};
  check_equal(native_io_backend_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(cnet_shared_test_named_pipe_peer_write(&pipe, inbound, sizeof(inbound)), TURBO_OK);
  check_equal(native_io_backend_observe(&backend, &completion, 1u, CNET_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
  check_equal(received, inbound, sizeof(inbound));

  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_WRITE,
                                    .endpoint = cnet_transport_write_endpoint(&transport),
                                    .buffer = (void *)outbound,
                                    .length = sizeof(outbound)};
  check_equal(native_io_backend_submit(&backend, &operation, &request), TURBO_OK);
  check_equal(native_io_backend_observe(&backend, &completion, 1u, CNET_TEST_TIMEOUT_MS, &count),
              TURBO_OK);
  check_equal(count, 1u);
  check_equal(completion.kind, NATIVE_IO_COMPLETION_OK);
  memset(received, 0, sizeof(received));
  check_equal(cnet_shared_test_named_pipe_peer_read(&pipe, received, sizeof(received)), TURBO_OK);
  check_equal(received, outbound, sizeof(outbound));

  check_equal(cnet_transport_close(&transport, &backend), TURBO_OK);
  cnet_shared_test_named_pipe_close(&pipe);
  check_equal(native_io_backend_close(&backend), TURBO_OK);
  check_equal(native_io_backend_destroy(&backend), TURBO_OK);
}

spec("CNet NativeIO transport ownership") {
  it("converts numeric IPv4 and IPv6 endpoints without resolver state") {
    struct sockaddr_storage address;
    size_t address_length = SIZE_MAX;
    const uint16_t port = UINT16_C(43123);

    memset(&address, 0xa5, sizeof(address));
    check_equal(cnet_transport_parse_numeric_address("127.0.0.1", port, &address, sizeof(address),
                                                     &address_length),
                TURBO_OK);
    check_equal(address_length, sizeof(struct sockaddr_in));
    check_equal(((const struct sockaddr_in *)&address)->sin_family, AF_INET);
    check_equal(((const struct sockaddr_in *)&address)->sin_port, htons(port));
    check_equal(((const struct sockaddr_in *)&address)->sin_addr.s_addr, htonl(INADDR_LOOPBACK));

    memset(&address, 0xa5, sizeof(address));
    address_length = SIZE_MAX;
    check_equal(cnet_transport_parse_numeric_address("::1", port, &address, sizeof(address),
                                                     &address_length),
                TURBO_OK);
    check_equal(address_length, sizeof(struct sockaddr_in6));
    check_equal(((const struct sockaddr_in6 *)&address)->sin6_family, AF_INET6);
    check_equal(((const struct sockaddr_in6 *)&address)->sin6_port, htons(port));
    {
      static const struct in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
      check_equal(&((const struct sockaddr_in6 *)&address)->sin6_addr, &loopback, sizeof(loopback));
    }
  }

  it("leaves hostnames for the resolver and clears the output length") {
    struct sockaddr_storage address;
    size_t address_length = SIZE_MAX;
    memset(&address, 0, sizeof(address));
    check_equal(cnet_transport_parse_numeric_address("localhost", UINT16_C(443), &address,
                                                     sizeof(address), &address_length),
                TURBO_ENOENT);
    check_equal(address_length, 0u);
  }

  it("creates owns connects and releases one TCP socket") {
    native_io_backend_kind backends[CNET_TEST_MAX_BACKENDS];
    const size_t count = cnet_test_backends(backends);
    size_t index;
    for (index = 0u; index < count; ++index)
      cnet_test_tcp_transport(backends[index]);
  }

  it("creates owns connects and releases one UDP socket") {
    native_io_backend_kind backends[CNET_TEST_MAX_BACKENDS];
    const size_t count = cnet_test_backends(backends);
    size_t index;
    for (index = 0u; index < count; ++index)
      cnet_test_udp_transport(backends[index]);
  }

  it("adopts owns and releases one platform byte-pipe connection") {
    native_io_backend_kind backends[CNET_TEST_MAX_BACKENDS];
    const size_t count = cnet_test_backends(backends);
    size_t index;
    for (index = 0u; index < count; ++index)
      if (native_io_backend_kind_supports_pipe(backends[index]))
        cnet_test_pipe_transport(backends[index]);
  }

  it("opens owns and releases one named platform byte-pipe connection") {
    native_io_backend_kind backends[CNET_TEST_MAX_BACKENDS];
    const size_t count = cnet_test_backends(backends);
    size_t index;
    for (index = 0u; index < count; ++index)
      if (native_io_backend_kind_supports_pipe(backends[index]))
        cnet_test_named_pipe_transport(backends[index]);
  }

  it("clears transport output when a platform pipe name cannot be opened") {
    native_io_backend_kind backends[CNET_TEST_MAX_BACKENDS];
    const size_t count = cnet_test_backends(backends);
    size_t index;
    for (index = 0u; index < count; ++index) {
      native_io_backend backend = {0};
      const native_io_backend_config config = {backends[index], 2u, 1u, 1u};
      cnet_transport transport = {.native_handle = 1u,
                                  .write_native_handle = 2u,
                                  .resource_kind = CNET_TRANSPORT_RESOURCE_PIPE,
                                  .native_open = true,
                                  .attached = true,
                                  .write_native_open = true,
                                  .write_attached = true};
      int status;
      if (!native_io_backend_kind_supports_pipe(backends[index])) continue;
      check_equal(native_io_backend_init(&backend, &config), TURBO_OK);
      status = cnet_transport_pipe_connect(&transport, &backend, backends[index],
                                           "cnet-missing-platform-pipe");
      check(status != TURBO_OK);
      check_false(cnet_transport_active(&transport));
      check_equal(native_io_backend_close(&backend), TURBO_OK);
      check_equal(native_io_backend_destroy(&backend), TURBO_OK);
    }
  }

  it("clears transport output on invalid admission") {
    cnet_transport transport = {.native_handle = (uintptr_t)1u,
                                .endpoint = {1u, 1u},
                                .resource_kind = CNET_TRANSPORT_RESOURCE_SOCKET,
                                .native_open = true,
                                .attached = true};
    native_io_request request = {1u, 1u};
    check_equal(cnet_transport_tcp_connect(&transport, NULL, NATIVE_IO_BACKEND_IOCP, NULL, 0u, 0u,
                                           &request),
                TURBO_EINVAL);
    check_equal(transport.native_handle, UINTPTR_MAX);
    check_false(transport.attached);
    check_false(native_io_request_valid(request));
  }
}
