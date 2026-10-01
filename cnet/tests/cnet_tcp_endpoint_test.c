#include <cnet/cnet.h>

#include <salts/clock.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdatomic.h>
#include <string.h>

enum { TEST_TIMEOUT_MS = 5000 };

typedef struct endpoint_probe {
  atomic_int connected;
  atomic_int terminal;
  atomic_int failed;
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

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state, const cnet_error *error) {
  endpoint_probe *probe = (endpoint_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  } else if (state == CNET_CONNECTION_CLOSED ||
             state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
}

static void on_owned_receive(void *user, cnet_connection connection,
                             mem_slice_t slice, cnet_message_kind kind) {
  (void)user;
  (void)connection;
  (void)slice;
  (void)kind;
}

static int peer_is_loopback_v4(const cnet_stream_peer *peer) {
  return peer != NULL &&
         peer->family == CNET_DATAGRAM_ADDRESS_IPV4 &&
         peer->address[0] == 127u &&
         peer->address[1] == 0u &&
         peer->address[2] == 0u &&
         peer->address[3] == 1u;
}

static int peer_equal(const cnet_stream_peer *left,
                      const cnet_stream_peer *right) {
  if (left == NULL || right == NULL ||
      left->family != right->family ||
      left->port != right->port ||
      left->flow_info != right->flow_info ||
      left->scope_id != right->scope_id)
    return 0;
  return memcmp(
      left->address, right->address,
      left->family == CNET_DATAGRAM_ADDRESS_IPV4 ? 4u : 16u) == 0;
}

int main(void) {
  cnet_listener listener = {0};
  cnet_listener outbound = {0};
  cnet_stream_peer listener_bind = {0};
  cnet_stream_peer outbound_bind = {0};
  cnet_client client = {0};
  cnet_client accepted_client = {0};
  cnet_client_config client_config = test_client_config();
  cnet_connection connection = {0};
  cnet_connection accepted = {0};
  endpoint_probe client_probe = {0};
  endpoint_probe accepted_probe = {0};
  cnet_observer client_observer = {on_state, NULL, &client_probe, NULL};
  cnet_observer accepted_observer = {
      on_state, NULL, &accepted_probe, NULL};
  cnet_stream_peer listener_local = {0};
  cnet_stream_peer outbound_local = {0};
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
  outbound_bind = listener_bind;

  assert(cnet_listener_bind_peer(
             &listener, test_backend(), &listener_bind) == SALTS_OK);
  assert(cnet_listener_port(&listener, &port) == SALTS_OK);
  assert(port != 0u);
  assert(cnet_listener_local(&listener, &listener_local) == SALTS_OK);
  assert(listener_local.port == port);
  assert(peer_is_loopback_v4(&listener_local));
  assert(cnet_listener_wait(&listener, 0u, &ready) == SALTS_EBUSY);
  assert(cnet_listener_set_backlog(&listener, 16u) == SALTS_OK);
  assert(cnet_listener_tcp_option_set(
             &listener, CNET_TCP_SOCKET_KEEPALIVE_ENABLED, 1u) == SALTS_OK);
  assert(cnet_listener_tcp_option_get(
             &listener, CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
             &listener_keepalive) == SALTS_OK);
  assert(listener_keepalive == 1u);

  assert(cnet_listener_tcp_option_set(
             &listener, CNET_TCP_SOCKET_HOP_LIMIT, 51u) == SALTS_OK);
  assert(cnet_listener_tcp_option_get(
             &listener, CNET_TCP_SOCKET_HOP_LIMIT,
             &listener_hop_limit) == SALTS_OK);
  assert(listener_hop_limit == 51u);

  assert(cnet_listener_tcp_option_set(
             &listener, CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
             16384u) == SALTS_OK);
  assert(cnet_listener_tcp_option_get(
             &listener, CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
             &listener_receive_buffer) == SALTS_OK);
  assert(listener_receive_buffer != 0u);

  assert(cnet_listener_tcp_option_set(
             &listener, CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
             16384u) == SALTS_OK);
  assert(cnet_listener_tcp_option_get(
             &listener, CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
             &listener_send_buffer) == SALTS_OK);
  assert(listener_send_buffer != 0u);

  listener_keepalive_idle_status = cnet_listener_tcp_option_set(
      &listener, CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS, 2000u);
  assert(listener_keepalive_idle_status == SALTS_OK ||
         listener_keepalive_idle_status == SALTS_ENOTSUP);
  if (listener_keepalive_idle_status == SALTS_OK)
    assert(cnet_listener_tcp_option_get(
               &listener, CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS,
               &listener_keepalive_idle) == SALTS_OK);

  listener_keepalive_interval_status = cnet_listener_tcp_option_set(
      &listener, CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS, 2000u);
  assert(listener_keepalive_interval_status == SALTS_OK ||
         listener_keepalive_interval_status == SALTS_ENOTSUP);
  if (listener_keepalive_interval_status == SALTS_OK)
    assert(cnet_listener_tcp_option_get(
               &listener, CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS,
               &listener_keepalive_interval) == SALTS_OK);

  listener_keepalive_count_status = cnet_listener_tcp_option_set(
      &listener, CNET_TCP_SOCKET_KEEPALIVE_COUNT, 3u);
  assert(listener_keepalive_count_status == SALTS_OK ||
         listener_keepalive_count_status == SALTS_ENOTSUP);
  if (listener_keepalive_count_status == SALTS_OK)
    assert(cnet_listener_tcp_option_get(
               &listener, CNET_TCP_SOCKET_KEEPALIVE_COUNT,
               &listener_keepalive_count) == SALTS_OK);

  assert(cnet_listener_listen(&listener, 16u) == SALTS_OK);
  assert(cnet_listener_listen(&listener, 16u) == SALTS_EALREADY);
  assert(cnet_listener_set_backlog(&listener, 32u) == SALTS_OK);

  assert(cnet_client_init(&client, &client_config) == SALTS_OK);
  assert(cnet_client_init(&accepted_client, &client_config) == SALTS_OK);

  assert(cnet_listener_bind_peer(
             &outbound, test_backend(), &outbound_bind) == SALTS_OK);
  assert(cnet_listener_local(&outbound, &outbound_local) == SALTS_OK);
  assert(peer_is_loopback_v4(&outbound_local));
  assert(outbound_local.port != 0u);
  assert(cnet_listener_tcp_option_set(
             &outbound, CNET_TCP_SOCKET_KEEPALIVE_ENABLED, 1u) == SALTS_OK);
  assert(cnet_listener_tcp_option_set(
             &outbound, CNET_TCP_SOCKET_HOP_LIMIT, 54u) == SALTS_OK);

  assert(cnet_listener_connect_peer(
             &outbound, &client, &listener_local,
             &client_observer, &connection) == SALTS_OK);
  assert(outbound.impl == NULL);

  deadline = salts_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!accepted_done ||
         atomic_load_explicit(
             &client_probe.connected, memory_order_acquire) == 0 ||
         atomic_load_explicit(
             &accepted_probe.connected, memory_order_acquire) == 0) {
    size_t events = 0u;
    int status;

    status = cnet_client_poll(&client, 1u, &events);
    assert(status == SALTS_OK);
    status = cnet_client_poll(&accepted_client, 0u, &events);
    assert(status == SALTS_OK);

    if (!accepted_done) {
      status = cnet_listener_accept_peer(
          &listener, &accepted_client, &accepted_observer,
          &accepted, &accepted_remote);
      if (status == SALTS_OK)
        accepted_done = 1;
      else
        assert(status == SALTS_ETIMEDOUT);
    }

    assert(salts_monotonic_ms() < deadline);
  }

  assert(atomic_load_explicit(
             &client_probe.failed, memory_order_acquire) == 0);
  assert(atomic_load_explicit(
             &accepted_probe.failed, memory_order_acquire) == 0);

  assert(cnet_connection_local_peer(
             &client, connection, &client_local) == SALTS_OK);
  assert(cnet_connection_remote_peer(
             &client, connection, &client_remote) == SALTS_OK);
  assert(cnet_connection_local_peer(
             &accepted_client, accepted, &server_local) == SALTS_OK);
  assert(cnet_connection_remote_peer(
             &accepted_client, accepted, &server_remote) == SALTS_OK);

  assert(peer_is_loopback_v4(&client_local));
  assert(client_local.port != 0u);
  assert(peer_equal(&client_local, &outbound_local));
  assert(peer_equal(&client_remote, &listener_local));
  assert(peer_equal(&server_remote, &client_local));
  assert(server_local.port == listener_local.port);
  assert(peer_is_loopback_v4(&server_local));
  assert(peer_equal(&accepted_remote, &client_local));

  {
    uint64_t inherited = 0u;

    assert(cnet_connection_tcp_option_get(
               &accepted_client, accepted,
               CNET_TCP_SOCKET_KEEPALIVE_ENABLED,
               &inherited) == SALTS_OK);
    assert(inherited == listener_keepalive);

    assert(cnet_connection_tcp_option_get(
               &accepted_client, accepted,
               CNET_TCP_SOCKET_HOP_LIMIT,
               &inherited) == SALTS_OK);
    assert(inherited == listener_hop_limit);

    assert(cnet_connection_tcp_option_get(
               &accepted_client, accepted,
               CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES,
               &inherited) == SALTS_OK);
    assert(inherited == listener_receive_buffer);

    assert(cnet_connection_tcp_option_get(
               &accepted_client, accepted,
               CNET_TCP_SOCKET_SEND_BUFFER_BYTES,
               &inherited) == SALTS_OK);
    assert(inherited == listener_send_buffer);

    if (listener_keepalive_idle_status == SALTS_OK) {
      assert(cnet_connection_tcp_option_get(
                 &accepted_client, accepted,
                 CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS,
                 &inherited) == SALTS_OK);
      assert(inherited == listener_keepalive_idle);
    }
    if (listener_keepalive_interval_status == SALTS_OK) {
      assert(cnet_connection_tcp_option_get(
                 &accepted_client, accepted,
                 CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS,
                 &inherited) == SALTS_OK);
      assert(inherited == listener_keepalive_interval);
    }
    if (listener_keepalive_count_status == SALTS_OK) {
      assert(cnet_connection_tcp_option_get(
                 &accepted_client, accepted,
                 CNET_TCP_SOCKET_KEEPALIVE_COUNT,
                 &inherited) == SALTS_OK);
      assert(inherited == listener_keepalive_count);
    }
  }

  {
    uint64_t value = 0u;
    int status;

    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_KEEPALIVE_ENABLED, &value) == SALTS_OK);
    assert(value == 1u);
    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_HOP_LIMIT, &value) == SALTS_OK);
    assert(value == 54u);

    assert(cnet_connection_tcp_option_set(
               &client, connection,
               CNET_TCP_SOCKET_KEEPALIVE_ENABLED, 1u) == SALTS_OK);
    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_KEEPALIVE_ENABLED, &value) == SALTS_OK);
    assert(value == 1u);

    assert(cnet_connection_tcp_option_set(
               &client, connection,
               CNET_TCP_SOCKET_HOP_LIMIT, 55u) == SALTS_OK);
    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_HOP_LIMIT, &value) == SALTS_OK);
    assert(value == 55u);

    assert(cnet_connection_tcp_option_set(
               &client, connection,
               CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES, 8192u) == SALTS_OK);
    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES, &value) == SALTS_OK);
    assert(value != 0u);

    assert(cnet_connection_tcp_option_set(
               &client, connection,
               CNET_TCP_SOCKET_SEND_BUFFER_BYTES, 8192u) == SALTS_OK);
    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_SEND_BUFFER_BYTES, &value) == SALTS_OK);
    assert(value != 0u);

    status = cnet_connection_tcp_option_set(
        &client, connection,
        CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS, 2000u);
    assert(status == SALTS_OK || status == SALTS_ENOTSUP);
    if (status == SALTS_OK) {
      assert(cnet_connection_tcp_option_get(
                 &client, connection,
                 CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS, &value) == SALTS_OK);
      assert(value != 0u);
    }

    status = cnet_connection_tcp_option_set(
        &client, connection,
        CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS, 2000u);
    assert(status == SALTS_OK || status == SALTS_ENOTSUP);
    if (status == SALTS_OK) {
      assert(cnet_connection_tcp_option_get(
                 &client, connection,
                 CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS, &value) == SALTS_OK);
      assert(value != 0u);
    }

    status = cnet_connection_tcp_option_set(
        &client, connection,
        CNET_TCP_SOCKET_KEEPALIVE_COUNT, 3u);
    assert(status == SALTS_OK || status == SALTS_ENOTSUP);
    if (status == SALTS_OK) {
      assert(cnet_connection_tcp_option_get(
                 &client, connection,
                 CNET_TCP_SOCKET_KEEPALIVE_COUNT, &value) == SALTS_OK);
      assert(value != 0u);
    }

    assert(cnet_connection_tcp_option_set(
               &client, connection,
               CNET_TCP_SOCKET_HOP_LIMIT, 0u) != SALTS_OK);
  }

  assert(cnet_set_receive_slice_handler(
             &accepted_client, accepted,
             on_owned_receive, NULL) == SALTS_OK);
  assert(cnet_receive(&accepted_client, accepted, 1u) == SALTS_OK);
  assert(cnet_connection_shutdown(
             &accepted_client, accepted,
             CNET_TCP_SHUTDOWN_RECEIVE) == SALTS_EBUSY);

  assert(cnet_connection_shutdown(
             &client, connection,
             CNET_TCP_SHUTDOWN_RECEIVE) == SALTS_OK);
  assert(cnet_connection_shutdown(
             &client, connection,
             CNET_TCP_SHUTDOWN_RECEIVE) == SALTS_OK);
  assert(cnet_connection_shutdown(
             &client, connection,
             CNET_TCP_SHUTDOWN_SEND) == SALTS_OK);
  assert(cnet_connection_shutdown(
             &client, connection,
             CNET_TCP_SHUTDOWN_BOTH) == SALTS_OK);

  assert(cnet_close(&client, connection) == SALTS_OK);
  assert(cnet_close(&accepted_client, accepted) == SALTS_OK);

  deadline = salts_monotonic_ms() + TEST_TIMEOUT_MS;
  while (atomic_load_explicit(
             &client_probe.terminal, memory_order_acquire) == 0 ||
         atomic_load_explicit(
             &accepted_probe.terminal, memory_order_acquire) == 0) {
    size_t events = 0u;
    assert(cnet_client_poll(&client, 1u, &events) == SALTS_OK);
    assert(cnet_client_poll(
               &accepted_client, 1u, &events) == SALTS_OK);
    assert(salts_monotonic_ms() < deadline);
  }

  assert(cnet_connection_local_peer(
             &client, connection, &client_local) == SALTS_ENOENT);
  assert(cnet_connection_remote_peer(
             &client, connection, &client_remote) == SALTS_ENOENT);
  {
    uint64_t stale_value = 0u;
    assert(cnet_connection_tcp_option_get(
               &client, connection,
               CNET_TCP_SOCKET_HOP_LIMIT, &stale_value) == SALTS_ENOENT);
    assert(cnet_connection_tcp_option_set(
               &client, connection,
               CNET_TCP_SOCKET_HOP_LIMIT, 32u) == SALTS_ENOENT);
    assert(cnet_connection_shutdown(
               &client, connection,
               CNET_TCP_SHUTDOWN_BOTH) == SALTS_ENOENT);
  }

  assert(cnet_client_stop(&client, TEST_TIMEOUT_MS) == SALTS_OK);
  assert(cnet_client_destroy(&client) == SALTS_OK);
  assert(cnet_client_stop(
             &accepted_client, TEST_TIMEOUT_MS) == SALTS_OK);
  assert(cnet_client_destroy(&accepted_client) == SALTS_OK);

  assert(cnet_listener_close(&listener) == SALTS_OK);
  assert(cnet_listener_destroy(&listener) == SALTS_OK);
  return 0;
}
