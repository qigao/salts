#include "cnet_module.h"
#include "cnet_shards.h"
#include "tinytest.h"

#include <salts/clock.h>

#include <stdatomic.h>
#include <stdbool.h>
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
  #include <unistd.h>
typedef int cnet_shards_test_socket;
  #define CNET_SHARDS_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_SHARDS_TEST_TIMEOUT_MS = 5000 };

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
  if (*out_listener == CNET_SHARDS_TEST_INVALID_SOCKET) return SALTS_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)out_address, &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cnet_shards_test_close_socket(*out_listener);
    *out_listener = CNET_SHARDS_TEST_INVALID_SOCKET;
    return SALTS_EIO;
  }
  return SALTS_OK;
}

static int cnet_shards_test_wait_state(cnet_shards *shards, cnet_shard_connection connection,
                                       cnet_session_state expected) {
  const uint64_t deadline = salts_monotonic_ms() + CNET_SHARDS_TEST_TIMEOUT_MS;
  for (;;) {
    cnet_session_state state = CNET_SESSION_FREE;
    int status = cnet_shards_state(shards, connection, &state);
    if (status != SALTS_OK) return status;
    if (state == expected) return SALTS_OK;
    if (state == CNET_SESSION_TERMINAL && expected != CNET_SESSION_TERMINAL) return SALTS_EIO;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
    status = cnet_shards_poll(shards, 1u);
    if (status != SALTS_OK) return status;
  }
}

static int cnet_shards_test_wait_atomic(cnet_shards *shards, const atomic_int *value,
                                        int expected) {
  const uint64_t deadline = salts_monotonic_ms() + CNET_SHARDS_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(value, memory_order_acquire) != expected) {
    const int status = cnet_shards_poll(shards, 1u);
    if (status != SALTS_OK) return status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

typedef struct cnet_shards_test_sink_probe {
  cnet_shards *shards;
  atomic_int fail_once;
  atomic_int connected;
  atomic_int terminal;
  atomic_int status;
} cnet_shards_test_sink_probe;

static int cnet_shards_test_sink(void *context, uint32_t shard, const cnet_event *event) {
  cnet_shards_test_sink_probe *probe = (cnet_shards_test_sink_probe *)context;
  bool terminal_event;
  cnet_shard_connection connection;
  int status = SALTS_OK;

  if (event == NULL) return SALTS_EINVAL;
  terminal_event = event->kind == CNET_EVENT_STATE && (event->state == CNET_EVENT_STATE_CLOSED ||
                                                       event->state == CNET_EVENT_STATE_FAILED);
  connection = (cnet_shard_connection){shard, event->session};
  if (event->kind == CNET_EVENT_STATE && event->state == CNET_EVENT_STATE_CONNECTED &&
      atomic_exchange_explicit(&probe->fail_once, 0, memory_order_acq_rel) != 0)
    return SALTS_EIO;
  if (event->kind == CNET_EVENT_STATE && event->state == CNET_EVENT_STATE_CONNECTED)
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  if (terminal_event) {
    cnet_session_terminal terminal = {0};
    status = cnet_shards_recycle(probe->shards, connection, &terminal);
    atomic_store_explicit(&probe->status, status, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
  return status;
}

spec("CNet long-lived owner shards") {
  it("lets an owner drive a bound event sink without a dispatcher thread") {
    cnet_shards shards = {0};
    const cnet_shards_config config = {.backend_kind =
#if defined(_WIN32)
                                           NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                           NATIVE_IO_BACKEND_EPOLL,
#else
                                           NATIVE_IO_BACKEND_KQUEUE,
#endif
                                       .shard_count = 1u,
                                       .connection_capacity_per_shard = 1u,
                                       .command_capacity_per_shard = 8u,
                                       .request_capacity_per_shard = 4u,
                                       .completion_batch_capacity = 4u,
                                       .event_capacity_per_shard = 8u,
                                       .receive_buffer_bytes = 64u,
                                       .max_command_payload_bytes =
                                           sizeof(cnet_owner_connect_payload)};
    cnet_shards_test_sink_probe probe = {.shards = &shards};
    cnet_shards_test_socket listener = CNET_SHARDS_TEST_INVALID_SOCKET;
    cnet_shards_test_socket accepted = CNET_SHARDS_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    cnet_owner_connect_payload payload = {0};
    cnet_shard_connection connection = {0};

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.fail_once, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.status, SALTS_OK);
    check_equal(cnet_module_init(), SALTS_OK);
    check_equal(cnet_shards_test_listener(&listener, &address), SALTS_OK);
    check_equal(cnet_shards_init(&shards, &config), SALTS_OK);
    {
      cnet_shards_layout layout = {0};
      check_true(cnet_shards_get_layout(&shards, &layout));
      check_equal(layout.max_event_payload_bytes, config.receive_buffer_bytes);
    }
    check_equal(cnet_shards_bind_event_sink(&shards, cnet_shards_test_sink, &probe), SALTS_OK);

    payload.scheme = CNET_URI_TCP;
    memcpy(payload.host, "127.0.0.1", sizeof("127.0.0.1"));
    payload.port = ntohs(address.sin_port);
    check_equal(cnet_shards_connect(&shards, &payload, &connection), SALTS_OK);
    check_equal(cnet_shards_test_wait_state(&shards, connection, CNET_SESSION_OPEN), SALTS_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_SHARDS_TEST_INVALID_SOCKET);
    check_equal(cnet_shards_test_wait_atomic(&shards, &probe.connected, 1), SALTS_OK);
    check_equal(atomic_load_explicit(&probe.connected, memory_order_acquire), 1);
    {
      cnet_event_view queued = {0};
      check_equal(cnet_shards_take_event(&shards, 0u, &queued), SALTS_ETIMEDOUT);
    }

    check_equal(cnet_shards_close(&shards, connection), SALTS_OK);
    check_equal(cnet_shards_test_wait_atomic(&shards, &probe.terminal, 1), SALTS_OK);
    check_equal(atomic_load_explicit(&probe.status, memory_order_acquire), SALTS_OK);
    check_equal(cnet_shards_stop(&shards, CNET_SHARDS_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_shards_destroy(&shards), SALTS_OK);
    cnet_shards_test_close_socket(accepted);
    cnet_shards_test_close_socket(listener);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("preserves a fatal progress error while still reaching destroyable quiescence") {
    cnet_shards shards = {0};
    const cnet_shards_config config = {.backend_kind =
#if defined(_WIN32)
                                           NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                           NATIVE_IO_BACKEND_EPOLL,
#else
                                           NATIVE_IO_BACKEND_KQUEUE,
#endif
                                       .shard_count = 1u,
                                       .connection_capacity_per_shard = 1u,
                                       .command_capacity_per_shard = 8u,
                                       .request_capacity_per_shard = 4u,
                                       .completion_batch_capacity = 4u,
                                       .event_capacity_per_shard = 8u,
                                       .receive_buffer_bytes = 64u,
                                       .max_command_payload_bytes =
                                           sizeof(cnet_owner_connect_payload)};
    cnet_shards_test_sink_probe probe = {.shards = &shards};
    cnet_shards_test_socket listener = CNET_SHARDS_TEST_INVALID_SOCKET;
    cnet_shards_test_socket accepted = CNET_SHARDS_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    cnet_owner_connect_payload payload = {0};
    cnet_shard_connection connection = {0};
    uint64_t deadline;

    atomic_init(&probe.fail_once, 1);
    atomic_init(&probe.connected, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.status, SALTS_OK);
    check_equal(cnet_module_init(), SALTS_OK);
    check_equal(cnet_shards_test_listener(&listener, &address), SALTS_OK);
    check_equal(cnet_shards_init(&shards, &config), SALTS_OK);
    check_equal(cnet_shards_bind_event_sink(&shards, cnet_shards_test_sink, &probe), SALTS_OK);

    payload.scheme = CNET_URI_TCP;
    memcpy(payload.host, "127.0.0.1", sizeof("127.0.0.1"));
    payload.port = ntohs(address.sin_port);
    check_equal(cnet_shards_connect(&shards, &payload, &connection), SALTS_OK);
    deadline = salts_monotonic_ms() + CNET_SHARDS_TEST_TIMEOUT_MS;
    while (cnet_shards_poll(&shards, 1u) != SALTS_EIO && salts_monotonic_ms() < deadline)
      ;
    check_true(salts_monotonic_ms() < deadline);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_SHARDS_TEST_INVALID_SOCKET);
    check_equal(cnet_shards_close(&shards, connection), SALTS_OK);
    deadline = salts_monotonic_ms() + CNET_SHARDS_TEST_TIMEOUT_MS;
    while (atomic_load_explicit(&probe.terminal, memory_order_acquire) == 0 &&
           salts_monotonic_ms() < deadline)
      (void)cnet_shards_poll(&shards, 1u);
    check_equal(atomic_load_explicit(&probe.terminal, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.status, memory_order_acquire), SALTS_OK);
    check_equal(cnet_shards_stop(&shards, CNET_SHARDS_TEST_TIMEOUT_MS), SALTS_EIO);
    check_true(cnet_shards_stopped(&shards));
    check_equal(cnet_shards_destroy(&shards), SALTS_OK);
    cnet_shards_test_close_socket(accepted);
    cnet_shards_test_close_socket(listener);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("rejects multiple progress owners") {
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
    check_equal(cnet_module_init(), SALTS_OK);
    check_equal(cnet_shards_init(&shards, &config), SALTS_EINVAL);
    check_null(shards.impl);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }
}
