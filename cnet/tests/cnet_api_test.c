#include "tinytest.h"
#include <cnet/cnet.h>

#include <turbo/clock.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_api_test_socket;
  #define CNET_API_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_api_test_socket;
  #define CNET_API_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_API_TEST_TIMEOUT_MS = 5000 };

typedef struct cnet_api_test_probe {
  cnet_client *client;
  atomic_int connected;
  atomic_int received;
  atomic_int terminal;
  atomic_int callback_stop_status;
  atomic_int callback_operation_status;
  atomic_int failed;
  atomic_int block_terminal;
  atomic_int release_terminal;
  cnet_message_kind expected_kind;
  unsigned char received_value;
} cnet_api_test_probe;

static void cnet_api_test_close_socket(cnet_api_test_socket socket_value) {
  if (socket_value == CNET_API_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_api_test_listener(cnet_api_test_socket *out_listener, uint16_t *out_port) {
  struct sockaddr_in address;
#if defined(_WIN32)
  int length = (int)sizeof(address);
#else
  socklen_t length = (socklen_t)sizeof(address);
#endif
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_API_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)&address, (int)sizeof(address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)&address, &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cnet_api_test_close_socket(*out_listener);
    *out_listener = CNET_API_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  *out_port = ntohs(address.sin_port);
  return TURBO_OK;
}

static int cnet_api_test_wait(atomic_int *value, int expected) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_API_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(value, memory_order_acquire) < expected) {
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
    turbo_thread_yield();
  }
  return TURBO_OK;
}

static void cnet_api_test_state(void *user, cnet_connection connection, cnet_connection_state state,
                                const cnet_error *error) {
  cnet_api_test_probe *probe = (cnet_api_test_probe *)user;
  if (state == CNET_CONNECTION_CONNECTED) {
    atomic_store_explicit(&probe->callback_stop_status,
                          cnet_client_stop(probe->client, CNET_API_TEST_TIMEOUT_MS),
                          memory_order_release);
    atomic_store_explicit(&probe->callback_operation_status,
                          cnet_receive(probe->client, connection, 1u), memory_order_release);
    atomic_fetch_add_explicit(&probe->connected, 1, memory_order_release);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_fetch_add_explicit(&probe->terminal, 1, memory_order_release);
    while (atomic_load_explicit(&probe->block_terminal, memory_order_acquire) != 0 &&
           atomic_load_explicit(&probe->release_terminal, memory_order_acquire) == 0)
      turbo_thread_yield();
  }
}

static void cnet_api_test_receive(void *user, cnet_connection connection,
                                  const cnet_receive_view *view) {
  cnet_api_test_probe *probe = (cnet_api_test_probe *)user;
  if (view->kind != probe->expected_kind || view->size != 1u)
    atomic_store_explicit(&probe->failed, 1, memory_order_release);
  else probe->received_value = *(const unsigned char *)view->data;
  atomic_store_explicit(&probe->callback_operation_status, cnet_close(probe->client, connection),
                        memory_order_release);
  atomic_fetch_add_explicit(&probe->received, 1, memory_order_release);
}

static void cnet_api_test_ignore_state(void *user, cnet_connection connection,
                                       cnet_connection_state state, const cnet_error *error) {
  (void)user;
  (void)connection;
  (void)state;
  (void)error;
}

static cnet_client_config cnet_api_test_config(void) {
  const cnet_client_config config = {.backend =
#if defined(_WIN32)
                                         NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                         NATIVE_IO_BACKEND_EPOLL,
#else
                                         NATIVE_IO_BACKEND_KQUEUE,
#endif
                                     .io_shards = 1u,
                                     .callback_workers = 1u,
                                     .connection_capacity = 2u,
                                     .command_capacity_per_shard = 8u,
                                     .request_capacity_per_shard = 4u,
                                     .completion_batch_capacity = 4u,
                                     .event_capacity_per_shard = 8u,
                                     .max_send_bytes = 256u,
                                     .receive_buffer_bytes = 256u};
  return config;
}

spec("CNet public client API") {
  it("rejects invalid configuration without publishing a client") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();

    check_equal(cnet_client_init(NULL, &config), TURBO_EINVAL);
    check_equal(cnet_client_init(&client, NULL), TURBO_EINVAL);
    config.io_shards = 0u;
    check_equal(cnet_client_init(&client, &config), TURBO_EINVAL);
    check_null(client.impl);
  }

  it("clears immediate connect failures and stops a quiescent client") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_connection connection = {17u, 19u};
    cnet_connect_options options = {.uri = "unknown://endpoint",
                                    .observer = {.on_state = cnet_api_test_ignore_state}};

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_not_null(client.impl);
    check_equal(cnet_connect(&client, &options, &connection), TURBO_ENOTSUP);
    check_equal(connection.slot, 0u);
    check_equal(connection.generation, 0u);
    check_equal(cnet_client_destroy(&client), TURBO_EBUSY);
    check_equal(cnet_client_stop(&client, 5000u), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    check_null(client.impl);
  }

  it("copies options and supports reentrant TCP operations on callback workers") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_BYTES};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket accepted = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    char uri[64];
    uint16_t port = 0u;
    unsigned char outbound = 0u;
    unsigned char send_value = 41u;
    const unsigned char expected_outbound = 41u;
    const unsigned char expected_inbound = 73u;
    cnet_connect_options options;
    int stale_status = TURBO_OK;
    uint64_t stale_deadline;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    memset(uri, 'x', strlen(uri));
    options.observer = (cnet_observer){0};

    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_api_test_wait(&probe.connected, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.callback_stop_status, memory_order_acquire),
                TURBO_EBUSY);
    check_equal(atomic_load_explicit(&probe.callback_operation_status, memory_order_acquire),
                TURBO_OK);

    check_equal(cnet_send(&client, connection, &send_value, sizeof(send_value)), TURBO_OK);
    send_value = 99u;
    check_equal(recv(accepted, (char *)&outbound, (int)sizeof(outbound), 0), (int)sizeof(outbound));
    check_equal(outbound, expected_outbound);
    check_equal(send(accepted, (const char *)&expected_inbound, (int)sizeof(expected_inbound), 0),
                (int)sizeof(expected_inbound));
    check_equal(cnet_api_test_wait(&probe.received, 1), TURBO_OK);
    check_equal(probe.received_value, expected_inbound);
    check_equal(cnet_api_test_wait(&probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);

    stale_deadline = turbo_monotonic_ms() + CNET_API_TEST_TIMEOUT_MS;
    do {
      stale_status = cnet_send(&client, connection, &expected_outbound, sizeof(expected_outbound));
      if (stale_status == TURBO_ENOENT) break;
      turbo_thread_yield();
    } while (turbo_monotonic_ms() < stale_deadline);
    check_equal(stale_status, TURBO_ENOENT);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(accepted);
    cnet_api_test_close_socket(listener);
  }

  it("drains one live TCP connection before stopping") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_BYTES};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket accepted = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    uint16_t port = 0u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_api_test_wait(&probe.connected, 1), TURBO_OK);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.terminal, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(accepted);
    cnet_api_test_close_socket(listener);
  }

  it("retries a timed out stop without duplicating terminal delivery") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_BYTES};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket accepted = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    uint16_t port = 0u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 1);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_api_test_wait(&probe.connected, 1), TURBO_OK);
    check_equal(cnet_client_stop(&client, 20u), TURBO_ETIMEDOUT);
    check_equal(cnet_api_test_wait(&probe.terminal, 1), TURBO_OK);
    atomic_store_explicit(&probe.release_terminal, 1, memory_order_release);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.terminal, memory_order_acquire), 1);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(accepted);
    cnet_api_test_close_socket(listener);
  }

  it("preserves one connected UDP datagram as one public receive value") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_DATAGRAM};
    cnet_api_test_socket server = CNET_API_TEST_INVALID_SOCKET;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
#if defined(_WIN32)
    int server_length = (int)sizeof(server_address);
    int client_length = (int)sizeof(client_address);
#else
    socklen_t server_length = (socklen_t)sizeof(server_address);
    socklen_t client_length = (socklen_t)sizeof(client_address);
#endif
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    unsigned char outbound = 0u;
    const unsigned char expected_outbound = 29u;
    const unsigned char expected_inbound = 97u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    check_true(server != CNET_API_TEST_INVALID_SOCKET);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check_equal(bind(server, (const struct sockaddr *)&server_address, (int)sizeof(server_address)),
                0);
    check_equal(getsockname(server, (struct sockaddr *)&server_address, &server_length), 0);
    (void)snprintf(uri, sizeof(uri), "udp://127.0.0.1:%u",
                   (unsigned int)ntohs(server_address.sin_port));
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    check_equal(cnet_api_test_wait(&probe.connected, 1), TURBO_OK);
    check_equal(cnet_send(&client, connection, &expected_outbound, sizeof(expected_outbound)),
                TURBO_OK);
    check_equal(recvfrom(server, (char *)&outbound, (int)sizeof(outbound), 0,
                         (struct sockaddr *)&client_address, &client_length),
                (int)sizeof(outbound));
    check_equal(outbound, expected_outbound);
    check_equal(sendto(server, (const char *)&expected_inbound, (int)sizeof(expected_inbound), 0,
                       (const struct sockaddr *)&client_address, client_length),
                (int)sizeof(expected_inbound));
    check_equal(cnet_api_test_wait(&probe.received, 1), TURBO_OK);
    check_equal(probe.received_value, expected_inbound);
    check_equal(cnet_api_test_wait(&probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(server);
  }
}
