#include <cnet/cnet.h>

#include <salts/clock.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <tinytest.h>

enum { TEST_TIMEOUT_MS = 5000 };

typedef struct endpoint_probe {
  atomic_int connected;
  atomic_int terminal;
  atomic_int failed;
  atomic_int sent;
  atomic_int received;
} endpoint_probe;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config test_client_config(void) {
  cnet_client_config config = {0};
  config.backend = test_backend();
  config.connection_capacity = 2u;
  config.command_capacity = 8u;
  config.request_capacity = 8u;
  config.completion_batch_capacity = 8u;
  config.event_capacity = 8u;
  config.max_send_bytes = 1024u;
  config.receive_buffer_bytes = 1024u;
  return config;
}

static void on_state(void *user, cnet_connection connection, cnet_connection_state state,
                     const cnet_error *error) {
  endpoint_probe *probe = (endpoint_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
}

static void on_owned_receive(void *user, cnet_connection connection, mem_slice_t slice,
                             cnet_message_kind kind) {
  endpoint_probe *probe = (endpoint_probe *)user;
  (void)connection;
  (void)kind;
  if (probe != NULL) atomic_fetch_add_explicit(&probe->received, 1, memory_order_acq_rel);
  mem_slice_release(&slice);
}

static void on_send(void *user, cnet_connection connection, size_t size) {
  endpoint_probe *probe = (endpoint_probe *)user;
  (void)connection;
  check_warn(size != 0u);
  if (probe != NULL) atomic_fetch_add_explicit(&probe->sent, 1, memory_order_acq_rel);
}

static int send_one_byte(cnet_client *client, cnet_connection connection) {
  mem_buffer_t *buffer = mem_get_buffer(mem_global(), 1u);
  int status;
  if (buffer == NULL) return SALTS_ENOMEM;
  mem_buffer_data(buffer)[0] = 'x';
  mem_set_used(buffer, 1u);
  status = cnet_send_buffer(client, connection, buffer);
  mem_buffer_release(buffer);
  return status;
}

static int peer_is_loopback_v4(const cnet_stream_peer *peer) {
  return peer != NULL && peer->family == CNET_DATAGRAM_ADDRESS_IPV4 && peer->address[0] == 127u &&
         peer->address[1] == 0u && peer->address[2] == 0u && peer->address[3] == 1u;
}

static int peer_equal(const cnet_stream_peer *left, const cnet_stream_peer *right) {
  if (left == NULL || right == NULL || left->family != right->family || left->port != right->port ||
      left->scope_id != right->scope_id)
    return 0;
  return memcmp(left->address, right->address,
                left->family == CNET_DATAGRAM_ADDRESS_IPV4 ? 4u : 16u) == 0;
}

static cnet_listener listener;
static cnet_listener outbound;
static cnet_client client;
static cnet_client accepted_client;
static endpoint_probe client_probe;
static endpoint_probe accepted_probe;

static void test_accept_nodelay(uint64_t nodelay) {
  cnet_client_config config = test_client_config();
  cnet_listener_config listen_config = {
      .backend = test_backend(), .host = "127.0.0.1", .port = 0u, .backlog = 1u};
  cnet_stream_socket_options future = CNET_STREAM_SOCKET_OPTIONS_INIT;
  cnet_observer observer = {.on_state = on_state, .user = &accepted_probe};
  cnet_connect_options options = {0};
  cnet_connection connection = {0}, accepted = {0};
  uint64_t actual = 0u;
  uint16_t port = 0u;
  char uri[64];
  int ready = 0;
  uint64_t deadline;
  check_equal(cnet_client_init(&client, &config), SALTS_OK);
  check_equal(cnet_client_init(&accepted_client, &config), SALTS_OK);
  future.nodelay = (int)(1u - nodelay);
  check_equal(cnet_client_set_stream_socket_options(&accepted_client, &future), SALTS_OK);
  check_equal(cnet_listener_init(&listener, &listen_config), SALTS_OK);
  check_equal(cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_NODELAY, 1u - nodelay), SALTS_OK);
  check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
  check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
  options.uri = uri;
  options.observer = (cnet_observer){.on_state = on_state, .user = &client_probe};
  check_equal(cnet_connect(&client, &options, &connection), SALTS_OK);
  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!atomic_load_explicit(&client_probe.connected, memory_order_acquire)) {
    size_t events = 0u;
    check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
    check_less(cmeta_monotonic_ms(), deadline);
  }
  check_equal(cnet_listener_wait(&listener, TEST_TIMEOUT_MS, &ready), SALTS_OK);
  check_equal(ready, 1);
  /* Update after the handshake so inherited socket state cannot replace replay. */
  check_equal(cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_NODELAY, nodelay), SALTS_OK);
  check_equal(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_NODELAY, &actual), SALTS_OK);
  check_equal(actual, nodelay);
  check_equal(cnet_listener_accept(&listener, &accepted_client, &observer, &accepted), SALTS_OK);
  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!atomic_load_explicit(&accepted_probe.connected, memory_order_acquire)) {
    size_t events = 0u;
    check_equal(cnet_client_poll(&accepted_client, 1u, &events), SALTS_OK);
    check_less(cmeta_monotonic_ms(), deadline);
  }
  check_equal(cnet_connection_tcp_option_get(&accepted_client, accepted, CNET_TCP_SOCKET_NODELAY, &actual), SALTS_OK);
  check_equal(actual, nodelay);
}

static void test_tcp_endpoints(void) {
  cnet_stream_endpoint listener_bind = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint listener_endpoint = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint client_local_endpoint = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint client_remote_endpoint = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint server_local_endpoint = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint server_remote_endpoint = CNET_STREAM_ENDPOINT_INIT;
  cnet_client_config client_config = test_client_config();
  cnet_stream_socket_options accepted_future_options = CNET_STREAM_SOCKET_OPTIONS_INIT;
  cnet_connection connection = {0};
  cnet_connection accepted = {0};
  cnet_observer client_observer = {on_state, NULL, &client_probe, on_send};
  cnet_observer accepted_observer = {on_state, NULL, &accepted_probe, on_send};
  cnet_stream_peer listener_local = {0};
  cnet_stream_peer accepted_remote = {0};
  cnet_stream_peer client_local = {0};
  cnet_stream_peer client_remote = {0};
  cnet_stream_peer server_local = {0};
  cnet_stream_peer server_remote = {0};
  uint16_t port = 0u;
  uint64_t listener_keepalive = 0u;
  uint64_t listener_hop_limit = 0u;
  uint64_t listener_receive_buffer = 0u;
  uint64_t listener_send_buffer = 0u;
  uint64_t listener_keepalive_idle = 0u;
  uint64_t listener_keepalive_interval = 0u;
  uint64_t listener_keepalive_count = 0u;
  int listener_keepalive_idle_status = SALTS_ENOTSUP;
  int listener_keepalive_interval_status = SALTS_ENOTSUP;
  int listener_keepalive_count_status = SALTS_ENOTSUP;
  uint64_t deadline;
  int ready = 0;
  int accepted_done = 0;

  listener_bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
  listener_bind.address[0] = 127u;
  listener_bind.address[3] = 1u;

  check(cnet_listener_open(&listener, test_backend(), CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  check(cnet_listener_local(&listener, &listener_local) == SALTS_EBUSY);
  check(cnet_listener_port(&listener, &port) == SALTS_EBUSY);
  check(cnet_listener_listen(&listener, 16u) == SALTS_EBUSY);
  check(cnet_listener_set_backlog(&listener, 16u) == SALTS_OK);

  check(cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_KEEPALIVE_ENABLED, 1u) == SALTS_OK);
  check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
                                     &listener_keepalive) == SALTS_OK);
  check(listener_keepalive == 1u);

  check(cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_HOP_LIMIT, 51u) == SALTS_OK);
  check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_HOP_LIMIT, &listener_hop_limit) ==
        SALTS_OK);
  check(listener_hop_limit == 51u);

  check(cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES, 16384u) ==
        SALTS_OK);
  check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
                                     &listener_receive_buffer) == SALTS_OK);
  check(listener_receive_buffer != 0u);

  check(cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_SEND_BUFFER_BYTES, 16384u) ==
        SALTS_OK);
  check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
                                     &listener_send_buffer) == SALTS_OK);
  check(listener_send_buffer != 0u);

  listener_keepalive_idle_status =
      cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS, 2000u);
  check(listener_keepalive_idle_status == SALTS_OK ||
        listener_keepalive_idle_status == SALTS_ENOTSUP);
  if (listener_keepalive_idle_status == SALTS_OK)
    check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS,
                                       &listener_keepalive_idle) == SALTS_OK);

  listener_keepalive_interval_status =
      cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS, 2000u);
  check(listener_keepalive_interval_status == SALTS_OK ||
        listener_keepalive_interval_status == SALTS_ENOTSUP);
  if (listener_keepalive_interval_status == SALTS_OK)
    check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS,
                                       &listener_keepalive_interval) == SALTS_OK);

  listener_keepalive_count_status =
      cnet_listener_tcp_option_set(&listener, CNET_TCP_SOCKET_KEEPALIVE_COUNT, 3u);
  check(listener_keepalive_count_status == SALTS_OK ||
        listener_keepalive_count_status == SALTS_ENOTSUP);
  if (listener_keepalive_count_status == SALTS_OK)
    check(cnet_listener_tcp_option_get(&listener, CNET_TCP_SOCKET_KEEPALIVE_COUNT,
                                       &listener_keepalive_count) == SALTS_OK);

  check(cnet_listener_bind_open_endpoint(&listener, &listener_bind) == SALTS_OK);
  check(cnet_listener_bind_open_endpoint(&listener, &listener_bind) == SALTS_EALREADY);
  check(cnet_listener_port(&listener, &port) == SALTS_OK);
  check(port != 0u);
  check(cnet_listener_local(&listener, &listener_local) == SALTS_OK);
  check(cnet_listener_local_endpoint(&listener, &listener_endpoint) == SALTS_OK);
  check(listener_local.port == port);
  check(listener_endpoint.port == port);
  check(listener_endpoint.family == CNET_DATAGRAM_ADDRESS_IPV4);
  check(listener_endpoint.flow_info == 0u);
  check(listener_endpoint.scope_id == 0u);
  check(peer_is_loopback_v4(&listener_local));
  check(cnet_listener_wait(&listener, 0u, &ready) == SALTS_EBUSY);

  check(cnet_listener_listen(&listener, 16u) == SALTS_OK);
  check(cnet_listener_listen(&listener, 16u) == SALTS_EALREADY);
  check(cnet_listener_set_backlog(&listener, 32u) == SALTS_OK);

  check(cnet_client_init(&client, &client_config) == SALTS_OK);
  check(cnet_client_init(&accepted_client, &client_config) == SALTS_OK);
  accepted_future_options.receive_buffer_bytes = 4096u;
  accepted_future_options.send_buffer_bytes = 4096u;
  check(cnet_client_set_stream_socket_options(&accepted_client, &accepted_future_options) ==
        SALTS_OK);

  check(cnet_listener_open(&outbound, test_backend(), CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  check(cnet_listener_tcp_option_set(&outbound, CNET_TCP_SOCKET_KEEPALIVE_ENABLED, 1u) == SALTS_OK);
  check(cnet_listener_tcp_option_set(&outbound, CNET_TCP_SOCKET_HOP_LIMIT, 54u) == SALTS_OK);
  check(cnet_listener_local(&outbound, &client_local) == SALTS_EBUSY);

  check(cnet_listener_connect_endpoint(&outbound, &client, &listener_endpoint, &client_observer,
                                       &connection) == SALTS_OK);
  check(outbound.impl == NULL);

  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!accepted_done ||
         atomic_load_explicit(&client_probe.connected, memory_order_acquire) == 0 ||
         atomic_load_explicit(&accepted_probe.connected, memory_order_acquire) == 0) {
    size_t events = 0u;
    int status;

    status = cnet_client_poll(&client, 1u, &events);
    check(status == SALTS_OK);
    status = cnet_client_poll(&accepted_client, 0u, &events);
    check(status == SALTS_OK);

    if (!accepted_done) {
      status = cnet_listener_accept_peer(&listener, &accepted_client, &accepted_observer, &accepted,
                                         &accepted_remote);
      if (status == SALTS_OK) accepted_done = 1;
      else check(status == SALTS_ETIMEDOUT);
    }

    check(cmeta_monotonic_ms() < deadline);
  }

  check(atomic_load_explicit(&client_probe.failed, memory_order_acquire) == 0);
  check(atomic_load_explicit(&accepted_probe.failed, memory_order_acquire) == 0);

  check(cnet_connection_local_peer(&client, connection, &client_local) == SALTS_OK);
  check(cnet_connection_remote_peer(&client, connection, &client_remote) == SALTS_OK);
  check(cnet_connection_local_peer(&accepted_client, accepted, &server_local) == SALTS_OK);
  check(cnet_connection_remote_peer(&accepted_client, accepted, &server_remote) == SALTS_OK);
  check(cnet_connection_local_endpoint(&client, connection, &client_local_endpoint) == SALTS_OK);
  check(cnet_connection_remote_endpoint(&client, connection, &client_remote_endpoint) == SALTS_OK);
  check(cnet_connection_local_endpoint(&accepted_client, accepted, &server_local_endpoint) ==
        SALTS_OK);
  check(cnet_connection_remote_endpoint(&accepted_client, accepted, &server_remote_endpoint) ==
        SALTS_OK);

  check(client_local_endpoint.family == CNET_DATAGRAM_ADDRESS_IPV4);
  check(client_local_endpoint.port == client_local.port);
  check(client_remote_endpoint.port == listener_endpoint.port);
  check(server_local_endpoint.port == listener_endpoint.port);
  check(server_remote_endpoint.port == client_local_endpoint.port);
  check(client_local_endpoint.flow_info == 0u);
  check(client_remote_endpoint.flow_info == 0u);
  check(server_local_endpoint.flow_info == 0u);
  check(server_remote_endpoint.flow_info == 0u);

  check(peer_is_loopback_v4(&client_local));
  check(client_local.port != 0u);
  check(peer_equal(&client_remote, &listener_local));
  check(peer_equal(&server_remote, &client_local));
  check(server_local.port == listener_local.port);
  check(peer_is_loopback_v4(&server_local));
  check(peer_equal(&accepted_remote, &client_local));

  {
    uint64_t inherited = 0u;

    check(cnet_connection_tcp_option_get(&accepted_client, accepted,
                                         CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
                                         &inherited) == SALTS_OK);
    check(inherited == listener_keepalive);

    check(cnet_connection_tcp_option_get(&accepted_client, accepted, CNET_TCP_SOCKET_HOP_LIMIT,
                                         &inherited) == SALTS_OK);
    check(inherited == listener_hop_limit);

    check(cnet_connection_tcp_option_get(&accepted_client, accepted,
                                         CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
                                         &inherited) == SALTS_OK);
    check_equal(inherited, listener_receive_buffer);

    check(cnet_connection_tcp_option_get(&accepted_client, accepted,
                                         CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
                                         &inherited) == SALTS_OK);
    check_equal(inherited, listener_send_buffer);

    if (listener_keepalive_idle_status == SALTS_OK) {
      check(cnet_connection_tcp_option_get(&accepted_client, accepted,
                                           CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS,
                                           &inherited) == SALTS_OK);
      check(inherited == listener_keepalive_idle);
    }
    if (listener_keepalive_interval_status == SALTS_OK) {
      check(cnet_connection_tcp_option_get(&accepted_client, accepted,
                                           CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS,
                                           &inherited) == SALTS_OK);
      check(inherited == listener_keepalive_interval);
    }
    if (listener_keepalive_count_status == SALTS_OK) {
      check(cnet_connection_tcp_option_get(&accepted_client, accepted,
                                           CNET_TCP_SOCKET_KEEPALIVE_COUNT,
                                           &inherited) == SALTS_OK);
      check(inherited == listener_keepalive_count);
    }
  }

  {
    uint64_t value = 0u;
    int status;

    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
                                         &value) == SALTS_OK);
    check(value == 1u);
    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_HOP_LIMIT, &value) ==
          SALTS_OK);
    check(value == 54u);

    check(cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
                                         1u) == SALTS_OK);
    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
                                         &value) == SALTS_OK);
    check(value == 1u);

    check(cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_HOP_LIMIT, 55u) ==
          SALTS_OK);
    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_HOP_LIMIT, &value) ==
          SALTS_OK);
    check(value == 55u);

    check(cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
                                         8192u) == SALTS_OK);
    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
                                         &value) == SALTS_OK);
    check(value != 0u);

    check(cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
                                         8192u) == SALTS_OK);
    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
                                         &value) == SALTS_OK);
    check(value != 0u);

    status = cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS,
                                            2000u);
    check(status == SALTS_OK || status == SALTS_ENOTSUP);
    if (status == SALTS_OK) {
      check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS,
                                           &value) == SALTS_OK);
      check(value != 0u);
    }

    status = cnet_connection_tcp_option_set(&client, connection,
                                            CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS, 2000u);
    check(status == SALTS_OK || status == SALTS_ENOTSUP);
    if (status == SALTS_OK) {
      check(cnet_connection_tcp_option_get(
                &client, connection, CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS, &value) == SALTS_OK);
      check(value != 0u);
    }

    status =
        cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_COUNT, 3u);
    check(status == SALTS_OK || status == SALTS_ENOTSUP);
    if (status == SALTS_OK) {
      check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_KEEPALIVE_COUNT,
                                           &value) == SALTS_OK);
      check(value != 0u);
    }

    check(cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_HOP_LIMIT, 0u) !=
          SALTS_OK);
  }

  check(cnet_set_receive_slice_handler(&accepted_client, accepted, on_owned_receive,
                                       &accepted_probe) == SALTS_OK);
  check(cnet_receive(&accepted_client, accepted, 1u) == SALTS_OK);
  check(cnet_connection_shutdown(&accepted_client, accepted, CNET_TCP_SHUTDOWN_RECEIVE) ==
        SALTS_OK);
  check(cnet_receive(&accepted_client, accepted, 1u) == SALTS_ESHUTDOWN);

  check(cnet_set_receive_slice_handler(&client, connection, on_owned_receive, &client_probe) ==
        SALTS_OK);
  check(cnet_connection_shutdown(&client, connection, CNET_TCP_SHUTDOWN_RECEIVE) == SALTS_OK);
  check(cnet_connection_shutdown(&client, connection, CNET_TCP_SHUTDOWN_RECEIVE) == SALTS_OK);
  check(cnet_receive(&client, connection, 1u) == SALTS_ESHUTDOWN);

  check(send_one_byte(&client, connection) == SALTS_OK);
  check(cnet_connection_shutdown(&client, connection, CNET_TCP_SHUTDOWN_SEND) == SALTS_OK);
  check(send_one_byte(&client, connection) == SALTS_ESHUTDOWN);

  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (atomic_load_explicit(&client_probe.sent, memory_order_acquire) == 0) {
    size_t events = 0u;
    check(cnet_client_poll(&client, 1u, &events) == SALTS_OK);
    check(cnet_client_poll(&accepted_client, 0u, &events) == SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
  }
  /*
   * Receive shutdown is a local contract: no further receive event may be
   * published on that direction. The sending peer may still observe a
   * platform-specific terminal/reset indication after its final write.
   */
  check(atomic_load_explicit(&accepted_probe.received, memory_order_acquire) == 0);

  check(cnet_connection_shutdown(&client, connection, CNET_TCP_SHUTDOWN_SEND) == SALTS_OK);
  check(cnet_connection_shutdown(&client, connection, CNET_TCP_SHUTDOWN_BOTH) == SALTS_OK);

  check(cnet_close(&client, connection) == SALTS_OK);
  check(cnet_close(&accepted_client, accepted) == SALTS_OK);

  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (atomic_load_explicit(&client_probe.terminal, memory_order_acquire) == 0 ||
         atomic_load_explicit(&accepted_probe.terminal, memory_order_acquire) == 0) {
    size_t events = 0u;
    check(cnet_client_poll(&client, 1u, &events) == SALTS_OK);
    check(cnet_client_poll(&accepted_client, 1u, &events) == SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
  }

  check(cnet_connection_local_peer(&client, connection, &client_local) == SALTS_ENOENT);
  check(cnet_connection_remote_peer(&client, connection, &client_remote) == SALTS_ENOENT);
  {
    uint64_t stale_value = 0u;
    check(cnet_connection_tcp_option_get(&client, connection, CNET_TCP_SOCKET_HOP_LIMIT,
                                         &stale_value) == SALTS_ENOENT);
    check(cnet_connection_tcp_option_set(&client, connection, CNET_TCP_SOCKET_HOP_LIMIT, 32u) ==
          SALTS_ENOENT);
    check(cnet_connection_shutdown(&client, connection, CNET_TCP_SHUTDOWN_BOTH) == SALTS_ENOENT);
  }

  check(cnet_client_stop(&client, TEST_TIMEOUT_MS) == SALTS_OK);
  check(cnet_client_destroy(&client) == SALTS_OK);
  check(cnet_client_stop(&accepted_client, TEST_TIMEOUT_MS) == SALTS_OK);
  check(cnet_client_destroy(&accepted_client) == SALTS_OK);

  check(cnet_listener_close(&listener) == SALTS_OK);
  check(cnet_listener_destroy(&listener) == SALTS_OK);
}

suite("CNet TCP endpoints") {
  group("address, socket policy and half-close contracts") {
    before_each() {
      endpoint_probe *const probes[] = {&client_probe, &accepted_probe};
      for (size_t i = 0u; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        atomic_init(&probes[i]->connected, 0);
        atomic_init(&probes[i]->terminal, 0);
        atomic_init(&probes[i]->failed, 0);
        atomic_init(&probes[i]->sent, 0);
        atomic_init(&probes[i]->received, 0);
      }
    }
    after_each() {
      if (client.impl != NULL) {
        check_warn(cnet_client_stop(&client, TEST_TIMEOUT_MS) == SALTS_OK);
        check_warn(cnet_client_destroy(&client) == SALTS_OK);
      }
      if (accepted_client.impl != NULL) {
        check_warn(cnet_client_stop(&accepted_client, TEST_TIMEOUT_MS) == SALTS_OK);
        check_warn(cnet_client_destroy(&accepted_client) == SALTS_OK);
      }
      if (outbound.impl != NULL) {
        check_warn(cnet_listener_close(&outbound) == SALTS_OK);
        check_warn(cnet_listener_destroy(&outbound) == SALTS_OK);
      }
      if (listener.impl != NULL) {
        const int status = cnet_listener_close(&listener);
        check_warn(status == SALTS_OK || status == SALTS_EALREADY);
        check_warn(cnet_listener_destroy(&listener) == SALTS_OK);
      }
    }
    it("preserves endpoint identities, options and shutdown semantics") { test_tcp_endpoints(); }
    it("replays enabled NODELAY onto queued accepts without client policy override") { test_accept_nodelay(1u); }
    it("replays disabled NODELAY onto queued accepts without client policy override") { test_accept_nodelay(0u); }
  }
}
