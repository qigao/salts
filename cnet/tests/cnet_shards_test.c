#include "cnet_module.h"
#include "cnet_shards.h"
#include "tinytest.h"

#include <turbo/clock.h>
#include <turbo/thread.h>

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_shards_test_socket;
  #define CNET_SHARDS_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int cnet_shards_test_socket;
  #define CNET_SHARDS_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_SHARDS_TEST_TIMEOUT_MS = 5000 };

static int cnet_shards_test_set_receive_timeout(cnet_shards_test_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = CNET_SHARDS_TEST_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                    (int)sizeof(timeout_ms)) == 0
             ? TURBO_OK
             : TURBO_EIO;
#else
  const struct timeval timeout = {CNET_SHARDS_TEST_TIMEOUT_MS / 1000,
                                  (CNET_SHARDS_TEST_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout)) ==
                 0
             ? TURBO_OK
             : TURBO_EIO;
#endif
}

static void cnet_shards_test_close_socket(cnet_shards_test_socket socket_value) {
  if (socket_value == CNET_SHARDS_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_shards_test_listener(cnet_shards_test_socket *out_listener,
                                     struct sockaddr_in *out_address) {
#if defined(_WIN32)
  int length = (int)sizeof(*out_address);
#else
  socklen_t length = (socklen_t)sizeof(*out_address);
#endif
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_SHARDS_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)out_address, &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cnet_shards_test_close_socket(*out_listener);
    *out_listener = CNET_SHARDS_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  return TURBO_OK;
}

static int cnet_shards_test_wait_state(cnet_shards *shards, cnet_shard_connection connection,
                                       cnet_session_state expected) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_SHARDS_TEST_TIMEOUT_MS;
  for (;;) {
    cnet_session_state state = CNET_SESSION_FREE;
    int status = cnet_shards_state(shards, connection, &state);
    if (status != TURBO_OK) return status;
    if (state == expected) return TURBO_OK;
    if (state == CNET_SESSION_TERMINAL && expected != CNET_SESSION_TERMINAL) return TURBO_EIO;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
    turbo_sleep_ms(1u);
  }
}

static int cnet_shards_test_wait_event(cnet_shards *shards, uint32_t shard,
                                       cnet_event_view *out_event) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_SHARDS_TEST_TIMEOUT_MS;
  for (;;) {
    const int status = cnet_shards_take_event(shards, shard, out_event);
    if (status != TURBO_ETIMEDOUT) return status;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
    turbo_sleep_ms(1u);
  }
}

spec("CNet long-lived owner shards") {
  it("assigns connections once and routes them to independent owner tasks") {
    cnet_shards shards = {0};
    const cnet_shards_config config = {.backend_kind =
#if defined(_WIN32)
                                           NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                           NATIVE_IO_BACKEND_EPOLL,
#else
                                           NATIVE_IO_BACKEND_KQUEUE,
#endif
                                       .shard_count = 2u,
                                       .connection_capacity_per_shard = 1u,
                                       .command_capacity_per_shard = 8u,
                                       .request_capacity_per_shard = 4u,
                                       .completion_batch_capacity = 4u,
                                       .event_capacity_per_shard = 8u,
                                       .receive_buffer_bytes = 64u,
                                       .max_command_payload_bytes =
                                           sizeof(cnet_owner_connect_payload)};
    cnet_shards_test_socket listener = CNET_SHARDS_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    uint32_t expected_shard;

    check_equal(cnet_shards_init(&shards, &config), TURBO_ESHUTDOWN);
    check_equal(cnet_module_init(), TURBO_OK);
    check_equal(cnet_shards_test_listener(&listener, &address), TURBO_OK);
    check_equal(cnet_shards_init(&shards, &config), TURBO_OK);

    for (expected_shard = 0u; expected_shard < 2u; ++expected_shard) {
      const unsigned char outbound = (unsigned char)(expected_shard + 1u);
      const unsigned char inbound = (unsigned char)(expected_shard + 11u);
      cnet_owner_connect_payload payload = {0};
      cnet_shard_connection connection = {0};
      cnet_shards_test_socket accepted = CNET_SHARDS_TEST_INVALID_SOCKET;
      cnet_event_view event = {0};
      cnet_session_terminal terminal = {0};
      unsigned char received = 0u;

      payload.scheme = CNET_URI_TCP;
      memcpy(payload.host, "127.0.0.1", sizeof("127.0.0.1"));
      payload.port = ntohs(address.sin_port);
      check_equal(cnet_shards_connect(&shards, &payload, &connection), TURBO_OK);
      check_equal(connection.shard, expected_shard);
      check_equal(cnet_shards_stop(&shards, 0u), TURBO_EBUSY);
      check_equal(cnet_shards_test_wait_state(&shards, connection, CNET_SESSION_OPEN), TURBO_OK);
      accepted = accept(listener, NULL, NULL);
      check_true(accepted != CNET_SHARDS_TEST_INVALID_SOCKET);
      check_equal(cnet_shards_test_set_receive_timeout(accepted), TURBO_OK);
      check_equal(cnet_shards_test_wait_event(&shards, connection.shard, &event), TURBO_OK);
      check_equal(event.state, CNET_EVENT_STATE_CONNECTED);
      check_equal(cnet_shards_release_event(&shards, connection.shard, &event), TURBO_OK);

      check_equal(cnet_shards_send(&shards, connection, &outbound, sizeof(outbound)), TURBO_OK);
      check_equal(recv(accepted, (char *)&received, (int)sizeof(received), 0),
                  (int)sizeof(received));
      check_equal(received, outbound);
      check_equal(cnet_shards_receive(&shards, connection, 1u), TURBO_OK);
      check_equal(send(accepted, (const char *)&inbound, (int)sizeof(inbound), 0),
                  (int)sizeof(inbound));
      check_equal(cnet_shards_test_wait_event(&shards, connection.shard, &event), TURBO_OK);
      check_equal(event.kind, CNET_EVENT_RECEIVE);
      check_equal(event.data, &inbound, sizeof(inbound));
      check_equal(cnet_shards_release_event(&shards, connection.shard, &event), TURBO_OK);

      check_equal(cnet_shards_close(&shards, connection), TURBO_OK);
      check_equal(cnet_shards_test_wait_state(&shards, connection, CNET_SESSION_TERMINAL),
                  TURBO_OK);
      check_equal(cnet_shards_test_wait_event(&shards, connection.shard, &event), TURBO_OK);
      check_equal(event.state, CNET_EVENT_STATE_CLOSING);
      check_equal(cnet_shards_release_event(&shards, connection.shard, &event), TURBO_OK);
      check_equal(cnet_shards_test_wait_event(&shards, connection.shard, &event), TURBO_OK);
      check_equal(event.state, CNET_EVENT_STATE_CLOSED);
      check_equal(cnet_shards_release_event(&shards, connection.shard, &event), TURBO_OK);
      check_equal(cnet_shards_recycle(&shards, connection, &terminal), TURBO_OK);
      check_equal(terminal.kind, CNET_SESSION_TERMINAL_CLOSED);
      cnet_shards_test_close_socket(accepted);
    }

    check_equal(cnet_shards_stop(&shards, CNET_SHARDS_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_shards_destroy(&shards), TURBO_OK);
    cnet_shards_test_close_socket(listener);
    check_equal(cnet_module_shutdown(), TURBO_OK);
  }
}
