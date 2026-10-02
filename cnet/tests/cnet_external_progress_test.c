#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
  TEST_TIMEOUT_MS = 5000u,
  TEST_BATCH = 8u
};

typedef struct external_probe {
  bool connected;
  bool terminal;
  bool failed;
  size_t callbacks;
} external_probe;

static native_io_backend_kind test_backend_kind(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static cnet_client_config test_client_config(void) {
  cnet_client_config config;
  memset(&config, 0, sizeof(config));
  config.backend = test_backend_kind();
  config.connection_capacity = 2u;
  config.command_capacity = 8u;
  config.request_capacity = 8u;
  config.completion_batch_capacity = TEST_BATCH;
  config.event_capacity = 8u;
  config.max_send_bytes = 4096u;
  config.receive_buffer_bytes = 4096u;
  config.connect_timeout_ms = 1000u;
  config.read_timeout_ms = 1000u;
  config.write_timeout_ms = 1000u;
  return config;
}

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state,
                     const cnet_error *error) {
  external_probe *probe = (external_probe *)user;
  (void)connection;
  assert(probe != NULL);
  ++probe->callbacks;
  if (state == CNET_CONNECTION_CONNECTED) {
    probe->connected = true;
  } else if (state == CNET_CONNECTION_CLOSED ||
             state == CNET_CONNECTION_FAILED) {
    probe->terminal = true;
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      probe->failed = true;
  }
}

static int drive_external_once(cnet_client *client,
                               native_io_backend *backend,
                               uint32_t wait_ms) {
  native_io_completion completions[TEST_BATCH];
  size_t completion_count = 0u;
  size_t events = 0u;
  size_t i;
  int status;

  status = cnet_client_advance_external(client, &events);
  if (status != SALTS_OK) return status;

  status = native_io_backend_observe(
      backend, completions, TEST_BATCH, wait_ms, &completion_count);
  if (status == SALTS_ETIMEDOUT) {
    status = cnet_client_advance_external(client, &events);
    return status;
  }
  if (status != SALTS_OK) return status;

  for (i = 0u; i < completion_count; ++i) {
    bool consumed = false;
    size_t routed_events = 0u;
    status = cnet_client_route_external_completion(
        client, &completions[i], &consumed, &routed_events);
    if (status != SALTS_OK) return status;
    if (!consumed) return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static void test_external_native_io_progress(void) {
  native_io_backend backend = {0};
  native_io_backend_config backend_config = {
      test_backend_kind(), 8u, 16u, TEST_BATCH};
  native_io_backend_config observed_config = {0};
  cnet_client client = {0};
  cnet_client_config client_config = test_client_config();
  cnet_listener listener = {0};
  cnet_listener outbound = {0};
  cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint remote = CNET_STREAM_ENDPOINT_INIT;
  cnet_observer observer = {0};
  cnet_connection connection = {0};
  external_probe probe = {0};
  native_io_completion unrelated = {0};
  uint64_t deadline;
  uint32_t wait_ms = 0u;
  bool consumed = true;
  size_t events = 0u;

  assert(native_io_backend_init(
             &backend, &backend_config) == SALTS_OK);
  assert(native_io_backend_get_config(
             &backend, &observed_config));
  assert(observed_config.kind == test_backend_kind());
  assert(observed_config.endpoint_capacity == 8u);
  assert(observed_config.request_capacity == 16u);
  assert(observed_config.completion_batch_capacity == TEST_BATCH);

  assert(cnet_client_init_external(
             &client, &client_config, &backend) == SALTS_OK);
  assert(cnet_client_poll(
             &client, 0u, &events) == SALTS_ENOTSUP);
  assert(cnet_client_stop(
             &client, 0u) == SALTS_ENOTSUP);

  /*
   * Unrelated completions must be ignored without corrupting shared-backend
   * routing state. The embedding runtime may have other backend consumers.
   */
  unrelated.user_data = 1u;
  assert(cnet_client_route_external_completion(
             &client, &unrelated, &consumed, &events) == SALTS_OK);
  assert(!consumed);
  assert(events == 0u);

  bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
  bind.address[0] = 127u;
  bind.address[3] = 1u;
  assert(cnet_listener_open(
             &listener, test_backend_kind(),
             CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  assert(cnet_listener_bind_open_endpoint(
             &listener, &bind) == SALTS_OK);
  assert(cnet_listener_local_endpoint(
             &listener, &remote) == SALTS_OK);
  assert(remote.port != 0u);
  assert(cnet_listener_listen(&listener, 8u) == SALTS_OK);

  assert(cnet_listener_open(
             &outbound, test_backend_kind(),
             CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  observer.on_state = on_state;
  observer.user = &probe;
  assert(cnet_listener_connect_endpoint(
             &outbound, &client, &remote,
             &observer, &connection) == SALTS_OK);
  assert(outbound.impl == NULL);

  /* A live connection prevents external stop until terminal completion. */
  assert(cnet_client_stop_external(&client) == SALTS_EBUSY);

  deadline = salts_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!probe.connected && !probe.failed) {
    assert(cnet_client_external_timeout(
               &client, 50u, &wait_ms) == SALTS_OK);
    assert(wait_ms <= 50u);
    assert(drive_external_once(
               &client, &backend, wait_ms) == SALTS_OK);
    assert(salts_monotonic_ms() < deadline);
  }
  assert(probe.connected);
  assert(!probe.failed);

  assert(cnet_close(&client, connection) == SALTS_OK);
  deadline = salts_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!probe.terminal) {
    assert(cnet_client_external_timeout(
               &client, 50u, &wait_ms) == SALTS_OK);
    assert(drive_external_once(
               &client, &backend, wait_ms) == SALTS_OK);
    assert(salts_monotonic_ms() < deadline);
  }
  assert(!probe.failed);

  assert(cnet_client_stop_external(&client) == SALTS_OK);
  assert(cnet_client_destroy(&client) == SALTS_OK);

  /* CNet borrowed but did not close/destroy the shared backend. */
  memset(&observed_config, 0, sizeof(observed_config));
  assert(native_io_backend_get_config(
             &backend, &observed_config));
  assert(observed_config.kind == test_backend_kind());

  assert(cnet_listener_close(&listener) == SALTS_OK);
  assert(cnet_listener_destroy(&listener) == SALTS_OK);
  assert(native_io_backend_close(&backend) == SALTS_OK);
  assert(native_io_backend_destroy(&backend) == SALTS_OK);
}

static void test_external_backend_contract_rejects_mismatch(void) {
  native_io_backend backend = {0};
  native_io_backend_config backend_config = {
      test_backend_kind(), 2u, 2u, 1u};
  cnet_client client = {0};
  cnet_client_config client_config = test_client_config();

  assert(native_io_backend_init(
             &backend, &backend_config) == SALTS_OK);
  assert(cnet_client_init_external(
             &client, &client_config, &backend) == SALTS_EINVAL);
  assert(client.impl == NULL);
  assert(native_io_backend_close(&backend) == SALTS_OK);
  assert(native_io_backend_destroy(&backend) == SALTS_OK);
}

int main(void) {
  test_external_native_io_progress();
  test_external_backend_contract_rejects_mismatch();
  return 0;
}
