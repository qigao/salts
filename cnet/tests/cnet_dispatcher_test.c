#include "cnet_dispatcher.h"
#include "cnet_module.h"
#include "cnet_shards.h"
#include "tinytest.h"

#include <turbo/clock.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_dispatcher_test_socket;
  #define CNET_DISPATCHER_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_dispatcher_test_socket;
  #define CNET_DISPATCHER_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_DISPATCHER_TEST_TIMEOUT_MS = 5000 };

typedef struct cnet_dispatcher_test_probe {
  atomic_int connected;
  atomic_int received;
  atomic_int terminal;
  atomic_int order_error;
  atomic_int last_order;
  unsigned char value;
} cnet_dispatcher_test_probe;

static void cnet_dispatcher_test_close_socket(cnet_dispatcher_test_socket socket_value) {
  if (socket_value == CNET_DISPATCHER_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_dispatcher_test_listener(cnet_dispatcher_test_socket *out_listener,
                                         struct sockaddr_in *out_address) {
#if defined(_WIN32)
  int length = (int)sizeof(*out_address);
#else
  socklen_t length = (socklen_t)sizeof(*out_address);
#endif
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_DISPATCHER_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)out_address, &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cnet_dispatcher_test_close_socket(*out_listener);
    *out_listener = CNET_DISPATCHER_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  return TURBO_OK;
}

static void cnet_dispatcher_test_observe(void *context, const cnet_dispatch_view *view) {
  cnet_dispatcher_test_probe *probe = (cnet_dispatcher_test_probe *)context;
  int expected_order;
  int next_order;

  if (view->kind == CNET_EVENT_RECEIVE) {
    next_order = 2;
    probe->value = view->size == 1u ? *(const unsigned char *)view->data : 0u;
    atomic_fetch_add_explicit(&probe->received, 1, memory_order_release);
  } else if (view->state == CNET_EVENT_STATE_CONNECTED) {
    next_order = 1;
    atomic_fetch_add_explicit(&probe->connected, 1, memory_order_release);
  } else if (view->state == CNET_EVENT_STATE_CLOSING) {
    next_order = 3;
  } else {
    next_order = 4;
    atomic_fetch_add_explicit(&probe->terminal, 1, memory_order_release);
  }
  expected_order = atomic_load_explicit(&probe->last_order, memory_order_acquire);
  if (next_order == 3 && expected_order == 1) {
    if (!atomic_compare_exchange_strong_explicit(&probe->last_order, &expected_order, next_order,
                                                 memory_order_acq_rel, memory_order_acquire))
      atomic_store_explicit(&probe->order_error, 1, memory_order_release);
    return;
  }
  expected_order = next_order - 1;
  if (!atomic_compare_exchange_strong_explicit(&probe->last_order, &expected_order, next_order,
                                               memory_order_acq_rel, memory_order_acquire))
    atomic_store_explicit(&probe->order_error, 1, memory_order_release);
}

static int cnet_dispatcher_test_drive_until(cnet_shards *shards, cnet_dispatcher *dispatcher,
                                            atomic_int *value, int expected) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_DISPATCHER_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(value, memory_order_acquire) < expected) {
    int status = cnet_shards_poll(shards, 1u);
    if (status != TURBO_OK) return status;
    status = cnet_dispatcher_drive(dispatcher, 0u);
    if (status != TURBO_OK && status != TURBO_ETIMEDOUT && status != TURBO_ENOBUFS &&
        status != TURBO_EBUSY)
      return status;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
  }
  return TURBO_OK;
}

spec("CNet event dispatcher") {
  it("invokes callbacks inline and recycles after terminal completion") {
    cnet_shards shards = {0};
    cnet_dispatcher dispatcher = {0};
    const cnet_shards_config shards_config = {.backend_kind =
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
    cnet_dispatcher_test_socket listener = CNET_DISPATCHER_TEST_INVALID_SOCKET;
    cnet_dispatcher_test_socket accepted = CNET_DISPATCHER_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    cnet_owner_connect_payload payload = {0};
    cnet_shard_connection connection = {0};
    cnet_shard_connection replacement = {0};
    cnet_dispatcher_test_probe probe = {0};
    cnet_dispatcher_test_probe replacement_probe = {0};
    const unsigned char inbound = 23u;

    check_equal(cnet_module_init(), TURBO_OK);
    check_equal(cnet_dispatcher_test_listener(&listener, &address), TURBO_OK);
    check_equal(cnet_shards_init(&shards, &shards_config), TURBO_OK);
    check_equal(cnet_dispatcher_init(&dispatcher, &shards), TURBO_OK);
    check_equal(cnet_dispatcher_init(&dispatcher, &shards), TURBO_EALREADY);
    check_equal(cnet_dispatcher_destroy(&dispatcher), TURBO_EBUSY);

    payload.scheme = CNET_URI_TCP;
    memcpy(payload.host, "127.0.0.1", sizeof("127.0.0.1"));
    payload.port = ntohs(address.sin_port);
    check_equal(cnet_shards_connect(&shards, &payload, &connection), TURBO_OK);
    check_equal(
        cnet_dispatcher_register(&dispatcher, connection, cnet_dispatcher_test_observe, &probe),
        TURBO_OK);
    check_equal(
        cnet_dispatcher_register(&dispatcher, connection, cnet_dispatcher_test_observe, &probe),
        TURBO_EALREADY);
    check_equal(cnet_dispatcher_test_drive_until(&shards, &dispatcher, &probe.connected, 1),
                TURBO_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_DISPATCHER_TEST_INVALID_SOCKET);

    check_equal(cnet_shards_receive(&shards, connection, 1u), TURBO_OK);
    check_equal(send(accepted, (const char *)&inbound, (int)sizeof(inbound), 0),
                (int)sizeof(inbound));
    check_equal(cnet_dispatcher_test_drive_until(&shards, &dispatcher, &probe.received, 1),
                TURBO_OK);
    check_equal(probe.value, inbound);

    check_equal(cnet_shards_close(&shards, connection), TURBO_OK);
    check_equal(cnet_dispatcher_test_drive_until(&shards, &dispatcher, &probe.terminal, 1),
                TURBO_OK);
    check_equal(cnet_dispatcher_wait_idle(&dispatcher, CNET_DISPATCHER_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_shards_connect(&shards, &payload, &replacement), TURBO_OK);
    check_true(replacement.session.generation != connection.session.generation);
    check_equal(atomic_load_explicit(&probe.order_error, memory_order_acquire), 0);

    check_equal(cnet_dispatcher_register(&dispatcher, replacement, cnet_dispatcher_test_observe,
                                         &replacement_probe),
                TURBO_OK);
    check_equal(
        cnet_dispatcher_test_drive_until(&shards, &dispatcher, &replacement_probe.connected, 1),
        TURBO_OK);
    cnet_dispatcher_test_close_socket(accepted);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_DISPATCHER_TEST_INVALID_SOCKET);
    check_equal(cnet_dispatcher_drain(&dispatcher, CNET_DISPATCHER_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(atomic_load_explicit(&replacement_probe.order_error, memory_order_acquire), 0);
    check_equal(cnet_dispatcher_register(&dispatcher, replacement, cnet_dispatcher_test_observe,
                                         &replacement_probe),
                TURBO_ESHUTDOWN);
    check_equal(cnet_dispatcher_drain(&dispatcher, CNET_DISPATCHER_TEST_TIMEOUT_MS),
                TURBO_EALREADY);
    check_equal(cnet_dispatcher_destroy(&dispatcher), TURBO_OK);
    check_equal(cnet_shards_stop(&shards, CNET_DISPATCHER_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_shards_destroy(&shards), TURBO_OK);
    cnet_dispatcher_test_close_socket(accepted);
    cnet_dispatcher_test_close_socket(listener);
    check_equal(cnet_module_shutdown(), TURBO_OK);
  }
}
