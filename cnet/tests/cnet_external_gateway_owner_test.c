#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>
#include <salts/thread.h>
#include <tinytest.h>

#include "cnet_external_test_cleanup.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * #1051 P0: one host-owned progress lane, one NativeIO backend, inbound
 * listener plus independent inbound/outbound CNet clients.  All callbacks
 * execute directly on that lane; no Actor, second observe, or worker.
 *
 * This tests host-composed fixed Owner progress (#696).  It intentionally
 * does NOT claim native_io_sharded has a CNet backend-borrow registration API.
 */

enum { GATEWAY_BATCH = 8u, GATEWAY_TIMEOUT_MS = 5000u };
typedef struct gateway_probe {
  cnet_client *client;
  const void *owner_token;
  const char *expected;
  size_t expected_bytes;
  size_t received_bytes;
  size_t sent_bytes;
  size_t connected;
  size_t terminal;
  bool failed;
} gateway_probe;

static native_io_backend gateway_backend;
static cnet_client gateway_inbound_client;
static cnet_client gateway_outbound_client;
static cnet_listener gateway_listener;
static cnet_connection gateway_inbound_connection;
static cnet_connection gateway_outbound_connection;
static gateway_probe gateway_inbound_probe;
static gateway_probe gateway_outbound_probe;

static native_io_backend_kind gateway_backend_kind(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static cnet_client_config gateway_client_config(void) {
  cnet_client_config config;
  memset(&config, 0, sizeof(config));
  config.backend = gateway_backend_kind();
  config.connection_capacity = 1u;
  config.command_capacity = 8u;
  config.request_capacity = 8u;
  config.completion_batch_capacity = GATEWAY_BATCH;
  config.event_capacity = 8u;
  config.max_send_bytes = 4096u;
  config.receive_buffer_bytes = 4096u;
  config.connect_timeout_ms = GATEWAY_TIMEOUT_MS;
  config.read_timeout_ms = GATEWAY_TIMEOUT_MS;
  config.write_timeout_ms = GATEWAY_TIMEOUT_MS;
  return config;
}

static void gateway_on_state(void *user, cnet_connection connection,
                             cnet_connection_state state, const cnet_error *error) {
  gateway_probe *probe = (gateway_probe *)user;
  (void)connection;
  (void)error;
  check_warn(probe != NULL);
  if (probe == NULL) return;
  check_warn(cmeta_thread_current_token() == probe->owner_token);
  if (state == CNET_CONNECTION_CONNECTED) ++probe->connected;
  if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED)
    ++probe->terminal;
  if (state == CNET_CONNECTION_FAILED) probe->failed = true;
}

static void gateway_on_receive(void *user, cnet_connection connection,
                               const cnet_receive_view *view) {
  gateway_probe *probe = (gateway_probe *)user;
  size_t offset;
  check_warn(probe != NULL && view != NULL);
  if (probe == NULL || view == NULL) return;
  check_warn(cmeta_thread_current_token() == probe->owner_token);
  check_warn(probe->received_bytes <= probe->expected_bytes);
  check_warn(view->size <= probe->expected_bytes - probe->received_bytes);
  if (view->size > probe->expected_bytes - probe->received_bytes) {
    probe->failed = true;
    return;
  }
  for (offset = 0u; offset < view->size; ++offset)
    check_warn(((const uint8_t *)view->data)[offset] ==
               (uint8_t)probe->expected[probe->received_bytes + offset]);
  probe->received_bytes += view->size;
  if (probe->received_bytes < probe->expected_bytes)
    check_warn(cnet_receive(probe->client, connection, 1u) == SALTS_OK);
}

static void gateway_on_send(void *user, cnet_connection connection, size_t size) {
  gateway_probe *probe = (gateway_probe *)user;
  (void)connection;
  check_warn(probe != NULL);
  if (probe == NULL) return;
  check_warn(cmeta_thread_current_token() == probe->owner_token);
  probe->sent_bytes += size;
}

static cnet_observer gateway_observer(gateway_probe *probe) {
  cnet_observer observer;
  memset(&observer, 0, sizeof(observer));
  observer.on_state = gateway_on_state;
  observer.on_receive = gateway_on_receive;
  observer.on_send = gateway_on_send;
  observer.user = probe;
  return observer;
}

static int gateway_advance(cnet_client *client) {
  size_t events = 0u;
  return cnet_client_advance_external(client, &events);
}

static int gateway_drive(uint32_t max_wait_ms, bool *out_accepted) {
  native_io_completion completions[GATEWAY_BATCH];
  size_t count = 0u;
  uint32_t wait_ms = max_wait_ms, required = max_wait_ms;
  int status;
  if (out_accepted != NULL) *out_accepted = false;

  status = gateway_advance(&gateway_inbound_client);
  if (status != SALTS_OK) return status;
  status = gateway_advance(&gateway_outbound_client);
  if (status != SALTS_OK) return status;

  status = cnet_client_external_timeout(&gateway_inbound_client, max_wait_ms, &required);
  if (status != SALTS_OK) return status;
  if (required < wait_ms) wait_ms = required;
  status = cnet_client_external_timeout(&gateway_outbound_client, wait_ms, &required);
  if (status != SALTS_OK) return status;
  if (required < wait_ms) wait_ms = required;

  status = native_io_backend_observe(&gateway_backend, completions,
                                     GATEWAY_BATCH, wait_ms, &count);
  if (status != SALTS_OK && status != SALTS_ETIMEDOUT) return status;

  for (size_t index = 0u; index < count; ++index) {
    bool consumed = false;
    size_t routed = 0u;
    if (gateway_listener.impl != NULL) {
      status = cnet_listener_route_external_completion(&gateway_listener,
                                                        &completions[index], &consumed);
      if (status != SALTS_OK) return status;
      if (consumed && out_accepted != NULL) *out_accepted = true;
    }
    if (!consumed) {
      status = cnet_client_route_external_completion(&gateway_inbound_client,
                                                      &completions[index], &consumed, &routed);
      if (status != SALTS_OK) return status;
    }
    if (!consumed) {
      status = cnet_client_route_external_completion(&gateway_outbound_client,
                                                      &completions[index], &consumed, &routed);
      if (status != SALTS_OK) return status;
    }
    if (!consumed) return SALTS_EPROTO;
  }
  status = gateway_advance(&gateway_inbound_client);
  return status == SALTS_OK ? gateway_advance(&gateway_outbound_client) : status;
}

static void gateway_send(cnet_client *client, cnet_connection connection,
                         const char *data, size_t bytes) {
  mem_buffer_t *buffer = mem_get_buffer(mem_global(), bytes);
  check(buffer != NULL);
  memcpy(mem_buffer_data(buffer), data, bytes);
  mem_set_used(buffer, bytes);
  check(cnet_send_buffer(client, connection, buffer) == SALTS_OK);
  mem_buffer_release(buffer);
}

static void gateway_test(void) {
  native_io_backend_config backend_config = {
      gateway_backend_kind(), 16u, 32u, GATEWAY_BATCH};
  cnet_client_config client_config = gateway_client_config();
  cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
  cnet_stream_endpoint local = CNET_STREAM_ENDPOINT_INIT;
  cnet_connection rejected = {0};
  cnet_observer incoming = gateway_observer(&gateway_inbound_probe);
  cnet_observer outgoing = gateway_observer(&gateway_outbound_probe);
  native_io_request accept_request = {0};
  uint64_t deadline;
  int status;

  gateway_inbound_probe.client = &gateway_inbound_client;
  gateway_inbound_probe.expected = "client";
  gateway_inbound_probe.expected_bytes = 6u;
  gateway_outbound_probe.client = &gateway_outbound_client;
  gateway_outbound_probe.expected = "server";
  gateway_outbound_probe.expected_bytes = 6u;
  gateway_inbound_probe.owner_token = gateway_outbound_probe.owner_token =
      cmeta_thread_current_token();

  check(native_io_backend_init(&gateway_backend, &backend_config) == SALTS_OK);
  check(cnet_client_init_external(&gateway_inbound_client, &client_config,
                                   &gateway_backend) == SALTS_OK);
  check(cnet_client_init_external(&gateway_outbound_client, &client_config,
                                   &gateway_backend) == SALTS_OK);

  bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
  bind.address[0] = 127u;
  bind.address[3] = 1u;
  check(cnet_listener_open(&gateway_listener, gateway_backend_kind(),
                           CNET_DATAGRAM_ADDRESS_IPV4) == SALTS_OK);
  check(cnet_listener_bind_open_endpoint(&gateway_listener, &bind) == SALTS_OK);
  check(cnet_listener_local_endpoint(&gateway_listener, &local) == SALTS_OK);
  check(local.port != 0u);
  check(cnet_listener_listen(&gateway_listener, 8u) == SALTS_OK);
  check(cnet_listener_attach_external(&gateway_listener,
                                      &gateway_backend) == SALTS_OK);
  check(cnet_listener_submit_external_accept(&gateway_listener,
                                              &accept_request) == SALTS_OK);
  check(native_io_request_valid(accept_request));

  check(cnet_connect_endpoint(&gateway_outbound_client, &local, NULL,
                               &outgoing, &gateway_outbound_connection) == SALTS_OK);

  deadline = cmeta_monotonic_ms() + GATEWAY_TIMEOUT_MS;
  while (!gateway_inbound_probe.connected || !gateway_outbound_probe.connected) {
    bool ready = false;
    check(gateway_drive(20u, &ready) == SALTS_OK);
    if (ready) {
      check(gateway_inbound_connection.slot == 0u);
      check(cnet_listener_accept(&gateway_listener, &gateway_inbound_client,
                                  &incoming, &gateway_inbound_connection) == SALTS_OK);
    }
    check(cmeta_monotonic_ms() < deadline);
    check(!gateway_inbound_probe.failed && !gateway_outbound_probe.failed);
  }
  check(gateway_inbound_connection.slot != 0u);
  check(gateway_outbound_connection.slot != 0u);

  /* One connection already owns the inbound client's only capacity slot. */
  check(cnet_connect_endpoint(&gateway_inbound_client, &local, NULL,
                               &incoming, &rejected) == SALTS_ENOBUFS);
  check(rejected.slot == 0u && rejected.generation == 0u);

  check(cnet_receive(&gateway_inbound_client, gateway_inbound_connection, 1u) == SALTS_OK);
  check(cnet_receive(&gateway_outbound_client, gateway_outbound_connection, 1u) == SALTS_OK);
  gateway_send(&gateway_outbound_client, gateway_outbound_connection, "client", 6u);
  gateway_send(&gateway_inbound_client, gateway_inbound_connection, "server", 6u);

  deadline = cmeta_monotonic_ms() + GATEWAY_TIMEOUT_MS;
  while (gateway_inbound_probe.received_bytes != 6u ||
         gateway_outbound_probe.received_bytes != 6u ||
         gateway_inbound_probe.sent_bytes != 6u ||
         gateway_outbound_probe.sent_bytes != 6u) {
    check(gateway_drive(20u, NULL) == SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
    check(!gateway_inbound_probe.failed && !gateway_outbound_probe.failed);
  }

  status = cnet_listener_close(&gateway_listener);
  check(status == SALTS_OK);
  check(cnet_listener_destroy(&gateway_listener) == SALTS_OK);

  check(cnet_close(&gateway_inbound_client, gateway_inbound_connection) == SALTS_OK);
  check(cnet_close(&gateway_outbound_client, gateway_outbound_connection) == SALTS_OK);
  deadline = cmeta_monotonic_ms() + GATEWAY_TIMEOUT_MS;
  while (!gateway_inbound_probe.terminal || !gateway_outbound_probe.terminal) {
    check(gateway_drive(20u, NULL) == SALTS_OK);
    check(cmeta_monotonic_ms() < deadline);
  }
  check(!gateway_inbound_probe.failed && !gateway_outbound_probe.failed);
  check(gateway_inbound_probe.connected == 1u);
  check(gateway_outbound_probe.connected == 1u);
  check(gateway_inbound_probe.terminal == 1u);
  check(gateway_outbound_probe.terminal == 1u);
  check(cnet_client_stop_external(&gateway_inbound_client) == SALTS_OK);
  check(cnet_client_stop_external(&gateway_outbound_client) == SALTS_OK);
  check(cnet_client_destroy(&gateway_inbound_client) == SALTS_OK);
  check(cnet_client_destroy(&gateway_outbound_client) == SALTS_OK);
  check(native_io_backend_close(&gateway_backend) == SALTS_OK);
  check(native_io_backend_destroy(&gateway_backend) == SALTS_OK);
}

suite("CNet shared Owner server and client") {
  before_each() {
    check_null(gateway_backend.impl);
    memset(&gateway_inbound_probe, 0, sizeof(gateway_inbound_probe));
    memset(&gateway_outbound_probe, 0, sizeof(gateway_outbound_probe));
    gateway_inbound_connection = (cnet_connection){0};
    gateway_outbound_connection = (cnet_connection){0};
  }
  after_each() {
    external_test_cleanup cleanup = {
        &gateway_backend, &gateway_listener,
        {{&gateway_inbound_client, gateway_inbound_connection},
         {&gateway_outbound_client, gateway_outbound_connection}}};
    cleanup_external_test(&cleanup);
  }
  group("same lane") {
    it("both directions share one backend and callback owner") { gateway_test(); }
  }
}
