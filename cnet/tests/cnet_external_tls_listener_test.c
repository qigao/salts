#include <cnet/cnet.h>

#include <salts/clock.h>
#include <salts/error_codes.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { TEST_BATCH = 16, TEST_TIMEOUT_MS = 5000 };

#ifndef CNET_EXTERNAL_TLS_CA
#error CNET_EXTERNAL_TLS_CA is required
#endif
#ifndef CNET_EXTERNAL_TLS_CERT
#error CNET_EXTERNAL_TLS_CERT is required
#endif
#ifndef CNET_EXTERNAL_TLS_KEY
#error CNET_EXTERNAL_TLS_KEY is required
#endif

typedef struct state_probe {
  cnet_client *client;
  cnet_connection connection;
  bool connected;
  bool terminal;
  bool failed;
} state_probe;

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
  config.connection_capacity = 1u;
  config.command_capacity = 16u;
  config.request_capacity = 16u;
  config.completion_batch_capacity = TEST_BATCH;
  config.event_capacity = 32u;
  config.max_send_bytes = 64u * 1024u;
  config.receive_buffer_bytes = 32u * 1024u;
  config.connect_timeout_ms = TEST_TIMEOUT_MS;
  config.read_timeout_ms = TEST_TIMEOUT_MS;
  config.write_timeout_ms = TEST_TIMEOUT_MS;
  config.tls_io_buffer_bytes = CNET_TLS_MIN_IO_BUFFER_BYTES;
  config.tls_handshake_timeout_ms = TEST_TIMEOUT_MS;
  return config;
}

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state,
                     const cnet_error *error) {
  state_probe *probe = (state_probe *)user;
  assert(probe != NULL);
  probe->connection = connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    char version[16] = {0};
    size_t version_size = 0u;
    int status = cnet_tls_negotiated_version(
        probe->client, connection, version, sizeof(version), &version_size);
    if (status != SALTS_OK || strcmp(version, "TLSv1.3") != 0) {
      probe->failed = true;
      return;
    }
    probe->connected = true;
  }
  if (state == CNET_CONNECTION_CLOSED ||
      state == CNET_CONNECTION_FAILED)
    probe->terminal = true;
  if (state == CNET_CONNECTION_FAILED || error != NULL)
    probe->failed = true;
}

static int advance_client(cnet_client *client) {
  size_t events = 0u;
  return cnet_client_advance_external(client, &events);
}

static int route_client_completion(
    cnet_client *client,
    const native_io_completion *completion,
    bool *out_consumed) {
  size_t events = 0u;
  return cnet_client_route_external_completion(
      client, completion, out_consumed, &events);
}

static int drive_shared_once(cnet_listener *listener,
                             cnet_client *server,
                             cnet_client *client,
                             native_io_backend *backend,
                             uint32_t timeout_ms,
                             bool *out_listener_consumed) {
  native_io_completion completions[TEST_BATCH];
  size_t completion_count = 0u;
  size_t index;
  int status;

  if (out_listener_consumed != NULL)
    *out_listener_consumed = false;

  status = advance_client(server);
  if (status != SALTS_OK) return status;
  status = advance_client(client);
  if (status != SALTS_OK) return status;

  status = native_io_backend_observe(
      backend, completions, TEST_BATCH, timeout_ms, &completion_count);
  if (status == SALTS_ETIMEDOUT) {
    status = advance_client(server);
    if (status != SALTS_OK) return status;
    return advance_client(client);
  }
  if (status != SALTS_OK) return status;

  for (index = 0u; index < completion_count; ++index) {
    bool consumed = false;

    if (listener != NULL && listener->impl != NULL) {
      status = cnet_listener_route_external_completion(
          listener, &completions[index], &consumed);
      if (status != SALTS_OK) return status;
    }
    if (consumed) {
      if (out_listener_consumed != NULL)
        *out_listener_consumed = true;
      continue;
    }

    status = route_client_completion(server, &completions[index], &consumed);
    if (status != SALTS_OK) return status;
    if (consumed) continue;

    status = route_client_completion(client, &completions[index], &consumed);
    if (status != SALTS_OK) return status;
    if (!consumed) return SALTS_EPROTO;
  }

  status = advance_client(server);
  if (status != SALTS_OK) return status;
  return advance_client(client);
}

static void test_external_tls_listener_shared_progress(void) {
  static const char *alpn[] = {"http/1.1"};
  native_io_backend backend = {0};
  native_io_backend_config backend_config = {
      test_backend_kind(), 32u, 64u, TEST_BATCH};
  cnet_client server = {0};
  cnet_client client = {0};
  cnet_client_config server_config = test_client_config();
  cnet_client_config client_config = test_client_config();
  cnet_listener listener = {0};
  cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint local = CNET_STREAM_ENDPOINT_INIT;
  cnet_tls_server tls_server = {0};
  cnet_tls_client tls_client = {0};
  cnet_tls_server_config server_tls_config = {
      .size = sizeof(server_tls_config),
      .cert_file = CNET_EXTERNAL_TLS_CERT,
      .key_file = CNET_EXTERNAL_TLS_KEY,
      .client_auth = CNET_TLS_CLIENT_AUTH_NONE,
      .minimum_version = CNET_TLS_PROTOCOL_VERSION_1_3,
      .maximum_version = CNET_TLS_PROTOCOL_VERSION_1_3,
      .alpn_protocols = alpn,
      .alpn_protocol_count = 1u};
  cnet_tls_client_config client_tls_config = {
      .size = sizeof(client_tls_config),
      .ca_file = CNET_EXTERNAL_TLS_CA,
      .server_name = "127.0.0.1",
      .minimum_version = CNET_TLS_PROTOCOL_VERSION_1_3,
      .maximum_version = CNET_TLS_PROTOCOL_VERSION_1_3,
      .alpn_protocols = alpn,
      .alpn_protocol_count = 1u};
  cnet_connect_options connect_options;
  cnet_observer server_observer = {0};
  state_probe server_probe = {0};
  state_probe client_probe = {0};
  native_io_request first = {0};
  native_io_request duplicate = {0};
  cnet_connection outbound = {0};
  cnet_connection accepted = {0};
  bool listener_consumed = false;
  bool accepted_started = false;
  uint64_t deadline;
  char uri[64];

  assert(native_io_backend_init(&backend, &backend_config) == SALTS_OK);
  assert(cnet_client_init_external(
             &server, &server_config, &backend) == SALTS_OK);
  assert(cnet_client_init_external(
             &client, &client_config, &backend) == SALTS_OK);

  assert(cnet_tls_server_init(
             &tls_server, &server_tls_config) == SALTS_OK);
  assert(cnet_tls_client_init(
             &tls_client, &client_tls_config) == SALTS_OK);

  bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
  bind.address[0] = 127u;
  bind.address[3] = 1u;
  assert(cnet_listener_open(
             &listener, test_backend_kind(),
             CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  assert(cnet_listener_bind_open_endpoint(&listener, &bind) == SALTS_OK);
  assert(cnet_listener_local_endpoint(&listener, &local) == SALTS_OK);
  assert(local.port != 0u);
  assert(cnet_listener_listen(&listener, 8u) == SALTS_OK);
  assert(cnet_listener_attach_external(&listener, &backend) == SALTS_OK);

  {
    int ready = 0;
    assert(cnet_listener_wait(&listener, 0u, &ready) == SALTS_ENOTSUP);
  }

  assert(cnet_listener_submit_external_accept(
             &listener, &first) == SALTS_OK);
  assert(native_io_request_valid(first));
  assert(cnet_listener_submit_external_accept(
             &listener, &duplicate) == SALTS_OK);
  assert(duplicate.slot == first.slot);
  assert(duplicate.generation == first.generation);

  assert(snprintf(uri, sizeof(uri), "tls://127.0.0.1:%u",
                  (unsigned int)local.port) > 0);

  client_probe.client = &client;
  connect_options = (cnet_connect_options){
      .uri = uri,
      .observer = {
          .on_state = on_state,
          .user = &client_probe},
      .tls_client = &tls_client};
  assert(cnet_connect(&client, &connect_options, &outbound) == SALTS_OK);
  assert(cnet_tls_client_destroy(&tls_client) == SALTS_OK);

  server_probe.client = &server;
  server_observer.on_state = on_state;
  server_observer.user = &server_probe;

  deadline = salts_monotonic_ms() + TEST_TIMEOUT_MS;
  while ((!client_probe.connected || !server_probe.connected) &&
         !client_probe.failed && !server_probe.failed &&
         salts_monotonic_ms() < deadline) {
    assert(drive_shared_once(
               &listener, &server, &client, &backend,
               25u, &listener_consumed) == SALTS_OK);

    if (listener_consumed && !accepted_started) {
      native_io_request blocked = {0};
      assert(cnet_listener_submit_external_accept(
                 &listener, &blocked) == SALTS_EALREADY);
      assert(!native_io_request_valid(blocked));
      assert(cnet_listener_accept_tls(
                 &listener, &server, &tls_server,
                 &server_observer, &accepted) == SALTS_OK);
      assert(native_io_request_valid((native_io_request){
          accepted.slot, accepted.generation}));
      accepted_started = true;
    }
  }

  assert(listener_consumed);
  assert(accepted_started);
  assert(client_probe.connected);
  assert(server_probe.connected);
  assert(!client_probe.failed);
  assert(!server_probe.failed);

  assert(cnet_close(&client, outbound) == SALTS_OK);
  assert(cnet_close(&server, accepted) == SALTS_OK);

  deadline = salts_monotonic_ms() + TEST_TIMEOUT_MS;
  while ((!client_probe.terminal || !server_probe.terminal) &&
         salts_monotonic_ms() < deadline) {
    assert(drive_shared_once(
               &listener, &server, &client, &backend,
               25u, NULL) == SALTS_OK);
  }
  assert(client_probe.terminal);
  assert(server_probe.terminal);
  assert(!client_probe.failed);
  assert(!server_probe.failed);

  assert(cnet_listener_close(&listener) == SALTS_OK);
  assert(cnet_listener_destroy(&listener) == SALTS_OK);
  assert(cnet_tls_server_destroy(&tls_server) == SALTS_OK);
  assert(cnet_client_stop_external(&server) == SALTS_OK);
  assert(cnet_client_destroy(&server) == SALTS_OK);
  assert(cnet_client_stop_external(&client) == SALTS_OK);
  assert(cnet_client_destroy(&client) == SALTS_OK);
  assert(native_io_backend_close(&backend) == SALTS_OK);
  assert(native_io_backend_destroy(&backend) == SALTS_OK);
}

int main(void) {
  test_external_tls_listener_shared_progress();
  return 0;
}
