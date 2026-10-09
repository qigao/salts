#include <cnet/managed_dial.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdio.h>
#include <string.h>

/* Owner-driven real TCP reconnect: no background progress or application replay. */
enum { DIAL_TEST_MAX_MS = 4500u };
static cnet_client dial_client;
static cnet_manager dial_manager;
static cnet_managed_dial dial;
static cnet_listener dial_listener;
static size_t dial_failures, dial_connected, dial_sends;

static native_io_backend_kind dial_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static void dial_on_network(void *user, cnet_connection connection,
                            cnet_connection_state state, const cnet_error *error) {
  (void)user; (void)connection; (void)error;
  if (state == CNET_CONNECTION_FAILED) ++dial_failures;
  if (state == CNET_CONNECTION_CONNECTED) ++dial_connected;
}
static void dial_on_sent(void *user, cnet_connection connection, size_t bytes) {
  (void)user; (void)connection; (void)bytes;
  ++dial_sends;
}
static cnet_reconnect_failure_kind transient_network(void *user,
                                                      cnet_connection_state state,
                                                      const cnet_error *error) {
  (void)user; (void)state; (void)error;
  /* This test expressly authorizes retry of a refused *connection only*. */
  return CNET_RECONNECT_TRANSIENT;
}
spec("CNet owner-driven managed dial and reconnection") {
  it("uses real manager connect attempts without replaying application sends") {
    const cnet_client_config client_config = {
      .backend = dial_backend(), .connection_capacity = 1u,
      .command_capacity = 8u, .request_capacity = 8u,
      .completion_batch_capacity = 8u, .event_capacity = 8u,
      .max_send_bytes = 1024u, .receive_buffer_bytes = 1024u,
      .connect_timeout_ms = 1000u, .read_timeout_ms = 1000u,
      .write_timeout_ms = 1000u};
    const cnet_listener_config listener_config = {
      dial_backend(), "127.0.0.1", 0u, 8u};
    const cnet_manager_config manager_config = {
      sizeof(manager_config), CNET_MANAGER_VERSION, &dial_client, 1u, 1u};
    cnet_managed_dial_config config = {0};
    cnet_managed_dial_snapshot snap = {0};
    char uri[128];
    uint16_t port = 0u;
    size_t work = 0u, events = 0u;
    uint64_t next = 0u, until = cmeta_monotonic_ms() + DIAL_TEST_MAX_MS;
    size_t starts = 0u;
    int status;
    dial_failures = 0u;
    dial_connected = 0u;
    dial_sends = 0u;
    check_equal(cnet_client_init(&dial_client, &client_config), SALTS_OK);
    check_equal(cnet_listener_init(&dial_listener, &listener_config), SALTS_OK);
    check_equal(cnet_listener_port(&dial_listener, &port), SALTS_OK);
    check(port != 0u);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned)port);
    check_equal(cnet_listener_close(&dial_listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&dial_listener), SALTS_OK);
    check_equal(cnet_manager_init(&dial_manager, &manager_config), SALTS_OK);
    config.size = sizeof(config);
    config.version = CNET_MANAGED_DIAL_VERSION;
    config.client = &dial_client;
    config.manager = &dial_manager;
    config.connection.uri = uri;
    config.connection.observer = (cnet_observer){
        .on_state = dial_on_network, .on_send = dial_on_sent};
    config.recovery = (cnet_reconnect_config){
        sizeof(cnet_reconnect_config), CNET_RECOVERY_POLICY_VERSION, 2u,
        until, 10u, 20u, 17u};
    config.recovery_episode_ms = DIAL_TEST_MAX_MS;
    config.classify = transient_network;
    check_equal(cnet_managed_dial_init(&dial, &config), SALTS_OK);
    while (starts != 2u && cmeta_monotonic_ms() < until) {
      status = cnet_managed_dial_advance(&dial, cmeta_monotonic_ms(), &next);
      if (status == SALTS_OK) ++starts;
      else check(status == SALTS_EBUSY);
      if (starts != 2u) {
        check_equal(cnet_client_poll(&dial_client, 1u, &events), SALTS_OK);
        check_equal(cnet_manager_advance(&dial_manager, 1u, &work), SALTS_OK);
      }
      if (status == SALTS_EBUSY) cmeta_sleep_ms(1u);
    }
    check_equal(starts, (size_t)2u);
    while (dial_failures < 2u && cmeta_monotonic_ms() < until) {
      check_equal(cnet_client_poll(&dial_client, 1u, &events), SALTS_OK);
      check_equal(cnet_manager_advance(&dial_manager, 1u, &work), SALTS_OK);
    }
    check_equal(dial_failures, (size_t)2u);
    check_equal(dial_connected, (size_t)0u);
    check_equal(dial_sends, (size_t)0u); /* No implicit FMQ/HTTP DATA replay. */
    check_equal(cnet_managed_dial_get_snapshot(&dial, &snap), SALTS_OK);
    check_equal(snap.recovery.attempts, (uint32_t)2u);
    check_equal(cnet_managed_dial_advance(&dial, cmeta_monotonic_ms(), &next), SALTS_ENOBUFS);
    check_equal(cnet_managed_dial_seal(&dial), SALTS_OK);
    check_equal(cnet_manager_advance(&dial_manager, 1u, &work), SALTS_OK);
    check_equal(cnet_managed_dial_destroy(&dial), SALTS_OK);
    check_equal(cnet_client_stop(&dial_client, 2000u), SALTS_OK);
    check_equal(cnet_manager_destroy(&dial_manager), SALTS_OK);
    check_equal(cnet_client_destroy(&dial_client), SALTS_OK);
  }
  it("exposes a generation-safe READY capability after actual TCP connect") {
    const cnet_client_config client_config = {
      .backend = dial_backend(), .connection_capacity = 1u,
      .command_capacity = 8u, .request_capacity = 8u,
      .completion_batch_capacity = 8u, .event_capacity = 8u,
      .max_send_bytes = 1024u, .receive_buffer_bytes = 1024u,
      .connect_timeout_ms = 2000u, .read_timeout_ms = 2000u,
      .write_timeout_ms = 2000u};
    const cnet_listener_config listener_config = {
      dial_backend(), "127.0.0.1", 0u, 8u};
    const cnet_manager_config manager_config = {
      sizeof(manager_config), CNET_MANAGER_VERSION, &dial_client, 1u, 1u};
    cnet_managed_dial_config config = {0};
    cnet_managed_dial_snapshot snap = {0};
    cnet_reconnect_ticket stale = {0};
    char uri[128];
    uint16_t port = 0u;
    size_t work = 0u, events = 0u;
    uint64_t next = 0u, until = cmeta_monotonic_ms() + DIAL_TEST_MAX_MS;

    dial_failures = dial_connected = dial_sends = 0u;
    check_equal(cnet_client_init(&dial_client, &client_config), SALTS_OK);
    /* Keep listening for a real kernel TCP handshake, unlike the refused
     * listener case above. No protocol bytes or DATA retry are invented. */
    check_equal(cnet_listener_init(&dial_listener, &listener_config), SALTS_OK);
    check_equal(cnet_listener_port(&dial_listener, &port), SALTS_OK);
    check(port != 0u);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned)port);
    check_equal(cnet_manager_init(&dial_manager, &manager_config), SALTS_OK);
    config.size = sizeof(config);
    config.version = CNET_MANAGED_DIAL_VERSION;
    config.client = &dial_client;
    config.manager = &dial_manager;
    config.connection.uri = uri;
    config.connection.observer = (cnet_observer){
        .on_state = dial_on_network, .on_send = dial_on_sent};
    config.recovery = (cnet_reconnect_config){
        sizeof(cnet_reconnect_config), CNET_RECOVERY_POLICY_VERSION, 2u,
        until, 10u, 20u, 17u};
    config.recovery_episode_ms = DIAL_TEST_MAX_MS;
    config.classify = transient_network;
    check_equal(cnet_managed_dial_init(&dial, &config), SALTS_OK);
    check_equal(cnet_managed_dial_advance(&dial, cmeta_monotonic_ms(), &next),
                SALTS_OK);
    check_equal(cnet_managed_dial_get_snapshot(&dial, &snap), SALTS_OK);
    check(snap.recovery_ticket.state != 0u);
    check(snap.recovery_ticket.incarnation != 0u);
    check_equal(snap.recovery_ticket.generation, (uint64_t)1u);
    /* CONNECTING cannot be falsely published as protocol READY. */
    check_equal(cnet_managed_dial_protocol_ready(
        &dial, snap.recovery_ticket, cmeta_monotonic_ms()), SALTS_EBUSY);
    while (dial_connected == 0u && cmeta_monotonic_ms() < until) {
      check_equal(cnet_client_poll(&dial_client, 1u, &events), SALTS_OK);
      check_equal(cnet_manager_advance(&dial_manager, 1u, &work), SALTS_OK);
    }
    check_equal(dial_connected, (size_t)1u);
    check_equal(dial_failures, (size_t)0u);
    check_equal(cnet_managed_dial_get_snapshot(&dial, &snap), SALTS_OK);
    check(snap.recovery.awaiting_protocol && !snap.recovery.protocol_ready);
    stale = snap.recovery_ticket;
    ++stale.generation;
    check_equal(cnet_managed_dial_protocol_ready(
        &dial, stale, cmeta_monotonic_ms()), SALTS_ENOENT);
    check_equal(cnet_managed_dial_protocol_ready(
        &dial, snap.recovery_ticket, cmeta_monotonic_ms()), SALTS_OK);
    check_equal(cnet_managed_dial_protocol_ready(
        &dial, snap.recovery_ticket, cmeta_monotonic_ms()), SALTS_EALREADY);
    check_equal(cnet_managed_dial_get_snapshot(&dial, &snap), SALTS_OK);
    check(snap.recovery.protocol_ready && !snap.recovery.awaiting_protocol);
    check_equal(snap.recovery.attempts, (uint32_t)0u);
    check_equal(dial_sends, (size_t)0u);

    check_equal(cnet_managed_dial_seal(&dial), SALTS_OK);
    /* Permit real terminal and Manager retirement before dropping the dial
     * callback context; reconnect must never outlive its observer. */
    while (snap.managed.slot != 0u && cmeta_monotonic_ms() < until) {
      check_equal(cnet_client_poll(&dial_client, 1u, &events), SALTS_OK);
      check_equal(cnet_manager_advance(&dial_manager, 1u, &work), SALTS_OK);
      check_equal(cnet_managed_dial_get_snapshot(&dial, &snap), SALTS_OK);
    }
    check_equal(snap.managed.slot, (size_t)0u);
    check_equal(cnet_managed_dial_protocol_ready(
        &dial, snap.recovery_ticket, cmeta_monotonic_ms()), SALTS_ESHUTDOWN);
    check_equal(cnet_managed_dial_destroy(&dial), SALTS_OK);
    check_equal(cnet_client_stop(&dial_client, 2000u), SALTS_OK);
    check_equal(cnet_manager_destroy(&dial_manager), SALTS_OK);
    check_equal(cnet_client_destroy(&dial_client), SALTS_OK);
    check_equal(cnet_listener_close(&dial_listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&dial_listener), SALTS_OK);
  }
}
