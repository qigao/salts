#include "tinytest.h"
#include <cnet/cnet.h>

#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_datagram_test_socket;
  #define CNET_DATAGRAM_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_datagram_test_socket;
  #define CNET_DATAGRAM_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_DATAGRAM_TEST_TIMEOUT_MS = 5000 };

typedef struct cnet_datagram_test_probe {
  cnet_datagram_peer peer;
  unsigned char received[32];
  size_t received_size;
  size_t send_size;
  uint64_t send_tag;
  int send_status;
  int receive_count;
  int send_count;
} cnet_datagram_test_probe;

static native_io_backend_kind cnet_datagram_test_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return (native_io_backend_kind)0;
#endif
}

static void cnet_datagram_test_close(cnet_datagram_test_socket socket_value) {
  if (socket_value == CNET_DATAGRAM_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static cnet_datagram_test_socket cnet_datagram_test_peer(struct sockaddr_in *address) {
  cnet_datagram_test_socket socket_value;
#if defined(_WIN32)
  int address_size = (int)sizeof(*address);
#else
  socklen_t address_size = (socklen_t)sizeof(*address);
#endif
  socket_value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_value == CNET_DATAGRAM_TEST_INVALID_SOCKET) return socket_value;
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(socket_value, (const struct sockaddr *)address, (int)sizeof(*address)) != 0 ||
      getsockname(socket_value, (struct sockaddr *)address, &address_size) != 0) {
    cnet_datagram_test_close(socket_value);
    return CNET_DATAGRAM_TEST_INVALID_SOCKET;
  }
  return socket_value;
}

static void cnet_datagram_test_receive(void *user, cnet_datagram *datagram,
                                       const cnet_datagram_peer *peer,
                                       const cnet_receive_view *view) {
  cnet_datagram_test_probe *probe = (cnet_datagram_test_probe *)user;
  (void)datagram;
  if (peer == NULL || view == NULL || view->kind != CNET_MESSAGE_DATAGRAM ||
      view->size > sizeof(probe->received))
    return;
  probe->peer = *peer;
  memcpy(probe->received, view->data, view->size);
  probe->received_size = view->size;
  ++probe->receive_count;
}

static void cnet_datagram_test_send(void *user, cnet_datagram *datagram,
                                    const cnet_datagram_peer *peer, size_t size, int status,
                                    uint64_t tag) {
  cnet_datagram_test_probe *probe = (cnet_datagram_test_probe *)user;
  (void)datagram;
  (void)peer;
  probe->send_size = size;
  probe->send_status = status;
  probe->send_tag = tag;
  ++probe->send_count;
}

static cnet_datagram_config cnet_datagram_test_config(cnet_datagram_test_probe *probe) {
  cnet_datagram_config config = CNET_DATAGRAM_CONFIG_INIT;
  config.backend = cnet_datagram_test_backend();
  config.host = "127.0.0.1";
  config.port = 0u;
  config.send_capacity = 1u;
  config.request_capacity = 2u;
  config.completion_batch_capacity = 2u;
  config.max_datagram_bytes = 256u;
  config.receive_buffer_bytes = 256u;
  config.observer.on_receive = cnet_datagram_test_receive;
  config.observer.on_send = cnet_datagram_test_send;
  config.observer.user = probe;
  return config;
}

spec("CNet bound UDP datagram") {
  it("rejects malformed hard bounds without publishing an object") {
    cnet_datagram datagram = {0};
    cnet_datagram_test_probe probe = {0};
    cnet_datagram_config config = cnet_datagram_test_config(&probe);

    config.send_capacity = 0u;
    check_equal(cnet_datagram_init(&datagram, &config), SALTS_EINVAL);
    check_null(datagram.impl);
    config = cnet_datagram_test_config(&probe);
    config.request_capacity = 1u;
    check_equal(cnet_datagram_init(&datagram, &config), SALTS_EINVAL);
    check_null(datagram.impl);
    config = cnet_datagram_test_config(&probe);
    config.receive_buffer_bytes = config.max_datagram_bytes - 1u;
    check_equal(cnet_datagram_init(&datagram, &config), SALTS_EINVAL);
    check_null(datagram.impl);
    config = cnet_datagram_test_config(&probe);
    config.reuse_port = 2;
    check_equal(cnet_datagram_init(&datagram, &config), SALTS_EINVAL);
    check_null(datagram.impl);
  }

  it("preserves peer identity and copied payloads in both directions") {
    static const unsigned char request[] = {1u, 3u, 5u, 7u};
    static const unsigned char expected_reply[] = {2u, 4u, 6u, 8u};
    cnet_datagram datagram = {0};
    cnet_datagram_test_probe probe = {0};
    cnet_datagram_config config = cnet_datagram_test_config(&probe);
    cnet_datagram_test_socket peer_socket = CNET_DATAGRAM_TEST_INVALID_SOCKET;
    struct sockaddr_in peer_address;
    struct sockaddr_in server_address;
    unsigned char reply[sizeof(expected_reply)] = {0};
    unsigned char send_copy[sizeof(expected_reply)];
    uint16_t port = 0u;
    size_t events = 0u;
#if defined(_WIN32)
    int source_size = (int)sizeof(server_address);
#else
    socklen_t source_size = (socklen_t)sizeof(server_address);
#endif

    check_equal(cnet_datagram_init(&datagram, &config), SALTS_OK);
    check_equal(cnet_datagram_port(&datagram, &port), SALTS_OK);
    check_true(port != 0u);
    peer_socket = cnet_datagram_test_peer(&peer_address);
    check_true(peer_socket != CNET_DATAGRAM_TEST_INVALID_SOCKET);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_address.sin_port = htons(port);

    check_equal(cnet_datagram_receive(&datagram, 1u), SALTS_OK);
    check_equal(sendto(peer_socket, (const char *)request, (int)sizeof(request), 0,
                       (const struct sockaddr *)&server_address, (int)sizeof(server_address)),
                (int)sizeof(request));
    check_equal(cnet_datagram_poll(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS, &events), SALTS_OK);
    check_equal(events, 1u);
    check_equal(probe.receive_count, 1);
    check_equal(probe.received_size, sizeof(request));
    check_equal(probe.received, request, sizeof(request));
    check_equal(probe.peer.family, CNET_DATAGRAM_ADDRESS_IPV4);
    check_equal(probe.peer.port, ntohs(peer_address.sin_port));

    memcpy(send_copy, expected_reply, sizeof(send_copy));
    check_equal(cnet_datagram_send(&datagram, &probe.peer, send_copy, sizeof(send_copy),
                                   UINT64_C(0x1020304050607080)), SALTS_OK);
    memset(send_copy, 0, sizeof(send_copy));
    check_equal(cnet_datagram_send(&datagram, &probe.peer, expected_reply, sizeof(expected_reply),
                                   UINT64_C(9)), SALTS_ENOBUFS);
    check_equal(cnet_datagram_poll(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS, &events), SALTS_OK);
    check_equal(probe.send_count, 1);
    check_equal(probe.send_status, SALTS_OK);
    check_equal(probe.send_size, sizeof(expected_reply));
    check_equal(probe.send_tag, UINT64_C(0x1020304050607080));
    check_equal(recvfrom(peer_socket, (char *)reply, (int)sizeof(reply), 0,
                         (struct sockaddr *)&server_address, &source_size),
                (int)sizeof(expected_reply));
    check_equal(reply, expected_reply, sizeof(expected_reply));

    cnet_datagram_test_close(peer_socket);
    check_equal(cnet_datagram_stop(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_datagram_destroy(&datagram), SALTS_OK);
  }

  it("cancels an active receive before destruction") {
    cnet_datagram datagram = {0};
    cnet_datagram_test_probe probe = {0};
    cnet_datagram_config config = cnet_datagram_test_config(&probe);

    check_equal(cnet_datagram_init(&datagram, &config), SALTS_OK);
    check_equal(cnet_datagram_receive(&datagram, 4u), SALTS_OK);
    check_equal(cnet_datagram_stop(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(probe.receive_count, 0);
    check_equal(cnet_datagram_destroy(&datagram), SALTS_OK);
    check_null(datagram.impl);
  }

  it("wakes a blocked owner poll without publishing a datagram event") {
    cnet_datagram datagram = {0};
    cnet_datagram_test_probe probe = {0};
    cnet_datagram_config config = cnet_datagram_test_config(&probe);
    size_t events = SIZE_MAX;

    check_equal(cnet_datagram_init(&datagram, &config), SALTS_OK);
    check_equal(cnet_datagram_wake(&datagram), SALTS_OK);
    check_equal(cnet_datagram_poll(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS, &events), SALTS_OK);
    check_equal(events, 0u);
    check_equal(cnet_datagram_stop(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_datagram_destroy(&datagram), SALTS_OK);
  }

  it("drains a receive that completed before stop without publishing it") {
    static const unsigned char payload[] = {9u, 8u, 7u};
    cnet_datagram datagram = {0};
    cnet_datagram_test_probe probe = {0};
    cnet_datagram_config config = cnet_datagram_test_config(&probe);
    cnet_datagram_test_socket peer_socket = CNET_DATAGRAM_TEST_INVALID_SOCKET;
    struct sockaddr_in peer_address;
    struct sockaddr_in server_address;
    uint16_t port = 0u;

    check_equal(cnet_datagram_init(&datagram, &config), SALTS_OK);
    check_equal(cnet_datagram_port(&datagram, &port), SALTS_OK);
    peer_socket = cnet_datagram_test_peer(&peer_address);
    check_true(peer_socket != CNET_DATAGRAM_TEST_INVALID_SOCKET);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_address.sin_port = htons(port);
    check_equal(cnet_datagram_receive(&datagram, 1u), SALTS_OK);
    check_equal(sendto(peer_socket, (const char *)payload, (int)sizeof(payload), 0,
                       (const struct sockaddr *)&server_address, (int)sizeof(server_address)),
                (int)sizeof(payload));
    check_equal(cnet_datagram_stop(&datagram, CNET_DATAGRAM_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(probe.receive_count, 0);
    cnet_datagram_test_close(peer_socket);
    check_equal(cnet_datagram_destroy(&datagram), SALTS_OK);
  }
}
