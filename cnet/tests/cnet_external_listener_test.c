#include <cnet/cnet.h>

#include <salts/error_codes.h>
#include <salts/clock.h>

#include <tinytest.h>
#include "cnet_external_test_cleanup.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET test_socket;
#define TEST_INVALID_SOCKET INVALID_SOCKET
static void close_test_socket(test_socket socket_value) {
  if (socket_value != INVALID_SOCKET)
    check_warn(closesocket(socket_value) == 0);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int test_socket;
#define TEST_INVALID_SOCKET (-1)
static void close_test_socket(test_socket socket_value) {
  if (socket_value >= 0)
    check_warn(close(socket_value) == 0);
}
#endif

enum { TEST_BATCH = 8, TEST_TIMEOUT_MS = 3000 };

typedef struct state_probe {
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
  config.command_capacity = 8u;
  config.request_capacity = 8u;
  config.completion_batch_capacity = 4u;
  config.event_capacity = 8u;
  config.max_send_bytes = 4096u;
  config.receive_buffer_bytes = 4096u;
  config.connect_timeout_ms = TEST_TIMEOUT_MS;
  config.read_timeout_ms = TEST_TIMEOUT_MS;
  config.write_timeout_ms = TEST_TIMEOUT_MS;
  return config;
}

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state,
                     const cnet_error *error) {
  state_probe *probe = (state_probe *)user;
  (void)connection;
  check_warn(probe != NULL);
  if (probe == NULL) return;
  if (state == CNET_CONNECTION_CONNECTED)
    probe->connected = true;
  if (state == CNET_CONNECTION_CLOSED ||
      state == CNET_CONNECTION_FAILED)
    probe->terminal = true;
  if (state == CNET_CONNECTION_FAILED || error != NULL)
    probe->failed = true;
}

static test_socket peer = TEST_INVALID_SOCKET;

static test_socket connect_raw_peer(uint16_t port) {
  struct sockaddr_in address;
  peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  check(peer != TEST_INVALID_SOCKET);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  check(connect(peer, (const struct sockaddr *)&address,
                 (int)sizeof(address)) == 0);
  return peer;
}

static int drive_shared_once(cnet_listener *listener,
                             cnet_client *client,
                             native_io_backend *backend,
                             uint32_t timeout_ms,
                             bool *out_listener_consumed) {
  native_io_completion completions[TEST_BATCH];
  size_t completion_count = 0u;
  size_t events = 0u;
  size_t index;
  int status;

  if (out_listener_consumed != NULL)
    *out_listener_consumed = false;

  status = cnet_client_advance_external(client, &events);
  if (status != SALTS_OK) return status;

  status = native_io_backend_observe(
      backend, completions, TEST_BATCH,
      timeout_ms, &completion_count);
  if (status == SALTS_ETIMEDOUT)
    return cnet_client_advance_external(client, &events);
  if (status != SALTS_OK) return status;

  for (index = 0u; index < completion_count; ++index) {
    bool consumed = false;
    size_t routed_events = 0u;

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

    status = cnet_client_route_external_completion(
        client, &completions[index],
        &consumed, &routed_events);
    if (status != SALTS_OK) return status;
    if (!consumed) return SALTS_EPROTO;
  }

  return cnet_client_advance_external(client, &events);
}

static native_io_backend backend;
static cnet_client client;
static cnet_listener listener;
static cnet_connection accepted;
static state_probe probe;

static void test_external_listener_accept_shared_progress(void) {
  native_io_backend_config backend_config = {
      test_backend_kind(), 8u, 16u, TEST_BATCH};
  cnet_client_config client_config = test_client_config();
  cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint local = CNET_STREAM_ENDPOINT_INIT;
  cnet_observer observer = {0};
  native_io_request first = {0};
  native_io_request duplicate = {0};
  native_io_request second = {0};
  uint64_t deadline;
  bool listener_consumed = false;
  int status;

  check(native_io_backend_init(
             &backend, &backend_config) == SALTS_OK);
  check(cnet_client_init_external(
             &client, &client_config, &backend) == SALTS_OK);

  bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
  bind.address[0] = 127u;
  bind.address[3] = 1u;
  check(cnet_listener_open(
             &listener, test_backend_kind(),
             CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  check(cnet_listener_bind_open_endpoint(
             &listener, &bind) == SALTS_OK);
  check(cnet_listener_local_endpoint(
             &listener, &local) == SALTS_OK);
  check(local.port != 0u);
  check(cnet_listener_listen(&listener, 8u) == SALTS_OK);

  check(cnet_listener_attach_external(
             &listener, &backend) == SALTS_OK);
  {
    int ready = 0;
    check(cnet_listener_wait(
               &listener, 0u, &ready) == SALTS_ENOTSUP);
  }

  status = cnet_listener_submit_external_accept(
      &listener, &first);
  if (status != SALTS_OK)
    fprintf(stderr,
            "initial external listener accept submit failed: %d\n",
            status);
  check(status == SALTS_OK);
  check(native_io_request_valid(first));
  check(cnet_listener_submit_external_accept(
             &listener, &duplicate) == SALTS_OK);
  check(duplicate.slot == first.slot);
  check(duplicate.generation == first.generation);

  peer = connect_raw_peer(local.port);
  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!listener_consumed) {
    check(drive_shared_once(
               &listener, &client, &backend,
               50u, &listener_consumed) == SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
  }

  check(cnet_listener_submit_external_accept(
             &listener, &second) == SALTS_EALREADY);
  check(!native_io_request_valid(second));

  observer.on_state = on_state;
  observer.user = &probe;
  check(cnet_listener_accept(
             &listener, &client, &observer,
             &accepted) == SALTS_OK);
  check(accepted.slot != 0u);
  check(accepted.generation != 0u);

  {
    size_t events = 0u;
    check(cnet_client_advance_external(
               &client, &events) == SALTS_OK);
  }
  check(probe.connected);
  check(!probe.failed);

  /*
   * A fresh accept is cancelled by listener close, but close cannot release
   * the attached listener endpoint until the authoritative cancellation
   * completion has been observed and routed.
   */
  check(cnet_listener_submit_external_accept(
             &listener, &second) == SALTS_OK);
  check(native_io_request_valid(second));
  check(cnet_listener_close(&listener) == SALTS_EBUSY);

  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  for (;;) {
    status = drive_shared_once(
        &listener, &client, &backend, 50u,
        &listener_consumed);
    check(status == SALTS_OK);
    status = cnet_listener_close(&listener);
    if (status == SALTS_OK)
      break;
    check(status == SALTS_EBUSY);
    check(cmeta_monotonic_ms() < deadline);
  }

  check(cnet_listener_destroy(&listener) == SALTS_OK);

  check(cnet_close(&client, accepted) == SALTS_OK);
  close_test_socket(peer);
  peer = TEST_INVALID_SOCKET;

  deadline = cmeta_monotonic_ms() + TEST_TIMEOUT_MS;
  while (!probe.terminal) {
    check(drive_shared_once(
               &listener, &client, &backend,
               50u, NULL) == SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
  }
  check(!probe.failed);

  check(cnet_client_stop_external(&client) == SALTS_OK);
  check(cnet_client_destroy(&client) == SALTS_OK);
  check(native_io_backend_close(&backend) == SALTS_OK);
  check(native_io_backend_destroy(&backend) == SALTS_OK);
}

suite("CNet external listener") {
    before_each() {
        check_null(backend.impl);
        accepted = (cnet_connection){0};
        probe = (state_probe){0};
    }
    after_each() {
        close_test_socket(peer);
        peer = TEST_INVALID_SOCKET;
        external_test_cleanup cleanup = {&backend, &listener, {{&client, accepted}, {NULL, {0}}}};
        cleanup_external_test(&cleanup);
    }

    group("shared accept progress") {
        it("external listener accept shared progress") { test_external_listener_accept_shared_progress(); }
    }
}
