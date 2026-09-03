#include "tinytest.h"
#include <cnet/cnet.h>

#include <string.h>

enum { CNET_PACKET_TEST_TIMEOUT_MS = 5000 };

typedef struct cnet_packet_test_probe {
  unsigned char received[128];
  size_t received_size;
  cnet_packet_session session;
  int admit_count;
  int connecting_count;
  int open_count;
  int close_count;
  int receive_count;
  int error_count;
  int last_error;
} cnet_packet_test_probe;

static native_io_backend_kind cnet_packet_test_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return (native_io_backend_kind)0;
#endif
}

static cnet_datagram_peer cnet_packet_test_peer(uint16_t port) {
  cnet_datagram_peer peer = {0};
  peer.family = CNET_DATAGRAM_ADDRESS_IPV4;
  peer.port = port;
  peer.address[0] = 127u;
  peer.address[3] = 1u;
  return peer;
}

static int cnet_packet_test_admit(void *user, cnet_packet_endpoint *endpoint,
                                  cnet_packet_protocol protocol,
                                  const cnet_datagram_peer *peer, uint32_t conversation) {
  cnet_packet_test_probe *probe = (cnet_packet_test_probe *)user;
  (void)endpoint;
  (void)protocol;
  (void)peer;
  (void)conversation;
  ++probe->admit_count;
  return SALTS_OK;
}

static void cnet_packet_test_state(void *user, cnet_packet_endpoint *endpoint,
                                   cnet_packet_session session, cnet_packet_session_state state,
                                   const cnet_datagram_peer *peer, uint32_t conversation) {
  cnet_packet_test_probe *probe = (cnet_packet_test_probe *)user;
  (void)endpoint;
  (void)peer;
  (void)conversation;
  probe->session = session;
  if (state == CNET_PACKET_SESSION_CONNECTING) ++probe->connecting_count;
  if (state == CNET_PACKET_SESSION_OPEN) ++probe->open_count;
  if (state == CNET_PACKET_SESSION_CLOSED) ++probe->close_count;
}

static void cnet_packet_test_receive(void *user, cnet_packet_endpoint *endpoint,
                                     cnet_packet_session session,
                                     const cnet_receive_view *view) {
  cnet_packet_test_probe *probe = (cnet_packet_test_probe *)user;
  (void)endpoint;
  if (view == NULL || view->size > sizeof(probe->received)) return;
  probe->session = session;
  memcpy(probe->received, view->data, view->size);
  probe->received_size = view->size;
  ++probe->receive_count;
}

static void cnet_packet_test_error(void *user, cnet_packet_endpoint *endpoint,
                                   cnet_packet_session session, int status) {
  cnet_packet_test_probe *probe = (cnet_packet_test_probe *)user;
  (void)endpoint;
  probe->session = session;
  probe->last_error = status;
  ++probe->error_count;
}

static cnet_packet_endpoint_config cnet_packet_test_config(cnet_packet_protocol protocol,
                                                           cnet_packet_test_probe *probe) {
  cnet_packet_endpoint_config config = CNET_PACKET_ENDPOINT_CONFIG_INIT;
  config.protocol = protocol;
  config.session_capacity = 4u;
  config.datagram.backend = cnet_packet_test_backend();
  config.datagram.host = "127.0.0.1";
  config.datagram.port = 0u;
  config.datagram.send_capacity = 32u;
  config.datagram.request_capacity = 33u;
  config.datagram.completion_batch_capacity = 16u;
  config.datagram.max_datagram_bytes = 1500u;
  config.datagram.receive_buffer_bytes = 1500u;
  config.kcp.mtu = 512u;
  config.kcp.send_window = 32u;
  config.kcp.receive_window = 32u;
  config.kcp.send_segment_capacity = 64u;
  config.kcp.max_message_bytes = sizeof(probe->received);
  config.observer.on_admit = cnet_packet_test_admit;
  config.observer.on_state = cnet_packet_test_state;
  config.observer.on_receive = cnet_packet_test_receive;
  config.observer.on_error = cnet_packet_test_error;
  config.observer.user = probe;
  return config;
}

static void cnet_packet_test_round_trip(cnet_packet_protocol protocol, uint32_t conversation) {
  static const unsigned char message[] = "unified-packet";
  cnet_packet_endpoint left = {0};
  cnet_packet_endpoint right = {0};
  cnet_packet_test_probe left_probe = {0};
  cnet_packet_test_probe right_probe = {0};
  cnet_packet_endpoint_config left_config = cnet_packet_test_config(protocol, &left_probe);
  cnet_packet_endpoint_config right_config = cnet_packet_test_config(protocol, &right_probe);
  cnet_packet_session left_session = {0};
  cnet_datagram_peer right_peer;
  uint16_t right_port = 0u;
  size_t events = 0u;
  size_t attempts;

  check_equal(cnet_packet_endpoint_init(&left, &left_config), SALTS_OK);
  check_equal(cnet_packet_endpoint_init(&right, &right_config), SALTS_OK);
  check_equal(cnet_packet_endpoint_port(&right, &right_port), SALTS_OK);
  right_peer = cnet_packet_test_peer(right_port);
  check_equal(cnet_packet_session_open(&left, &right_peer, conversation, &left_session), SALTS_OK);
  check_true(cnet_packet_session_valid(left_session));
  check_equal(cnet_packet_send(&left, left_session, message, sizeof(message)), SALTS_OK);

  for (attempts = 0u; attempts < 1000u && right_probe.receive_count == 0; ++attempts) {
    check_equal(cnet_packet_poll(&left, 1u, &events), SALTS_OK);
    check_equal(cnet_packet_poll(&right, 1u, &events), SALTS_OK);
  }
  check_equal(right_probe.admit_count, 1);
  check_equal(right_probe.open_count, 1);
  check_equal(right_probe.receive_count, 1);
  check_equal(right_probe.received_size, sizeof(message));
  check_equal(right_probe.received, message, sizeof(message));
  check_equal(right_probe.error_count, 0);

  check_equal(cnet_packet_endpoint_stop(&right, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
  check_equal(cnet_packet_endpoint_stop(&left, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
  check_equal(cnet_packet_endpoint_destroy(&right), SALTS_OK);
  check_equal(cnet_packet_endpoint_destroy(&left), SALTS_OK);
}

static void cnet_packet_test_secure_config(cnet_packet_endpoint_config *config,
                                           unsigned char psk_byte) {
  memset(config->security.pre_shared_key, psk_byte,
         sizeof(config->security.pre_shared_key));
  config->security.mode = CNET_KCP_SECURITY_PSK_V1;
  config->security.handshake_retry_ms = 10u;
  config->security.fec.backend = CNET_KCP_FEC_REED_SOLOMON;
  config->security.fec.data_shards = 2u;
  config->security.fec.parity_shards = 1u;
  config->security.fec.max_payload_bytes = 624u;
  config->security.fec.receive_group_count = 4u;
  config->kcp.mtu = 576u;
}

static void cnet_packet_test_secure_round_trip(void) {
  static const unsigned char message[] = "authenticated-kcp";
  cnet_packet_endpoint left = {0};
  cnet_packet_endpoint right = {0};
  cnet_packet_test_probe left_probe = {0};
  cnet_packet_test_probe right_probe = {0};
  cnet_packet_endpoint_config left_config = cnet_packet_test_config(CNET_PACKET_KCP, &left_probe);
  cnet_packet_endpoint_config right_config = cnet_packet_test_config(CNET_PACKET_KCP, &right_probe);
  cnet_packet_session left_session = {0};
  cnet_datagram_peer right_peer;
  cnet_packet_session_info info = {0};
  uint16_t right_port = 0u;
  size_t events = 0u;
  size_t attempts;

  cnet_packet_test_secure_config(&left_config, 0x5au);
  cnet_packet_test_secure_config(&right_config, 0x5au);
  check_equal(cnet_packet_endpoint_init(&left, &left_config), SALTS_OK);
  check_equal(cnet_packet_endpoint_init(&right, &right_config), SALTS_OK);
  check_equal(cnet_packet_endpoint_port(&right, &right_port), SALTS_OK);
  right_peer = cnet_packet_test_peer(right_port);
  check_equal(cnet_packet_session_open(&left, &right_peer, 0u, &left_session), SALTS_OK);
  check_equal(left_probe.connecting_count, 1);
  check_equal(left_probe.open_count, 0);
  check_equal(cnet_packet_send(&left, left_session, message, sizeof(message)), SALTS_EBUSY);

  for (attempts = 0u; attempts < 1000u && left_probe.open_count == 0; ++attempts) {
    check_equal(cnet_packet_poll(&left, 1u, &events), SALTS_OK);
    check_equal(cnet_packet_poll(&right, 1u, &events), SALTS_OK);
  }
  check_equal(left_probe.open_count, 1);
  check_equal(right_probe.admit_count, 1);
  check_equal(right_probe.open_count, 1);
  check_equal(cnet_packet_session_get_info(&left, left_session, &info), SALTS_OK);
  check_true(info.conversation != 0u);
  check_equal(cnet_packet_send(&left, left_session, message, sizeof(message)), SALTS_OK);

  for (attempts = 0u; attempts < 1000u && right_probe.receive_count == 0; ++attempts) {
    check_equal(cnet_packet_poll(&left, 1u, &events), SALTS_OK);
    check_equal(cnet_packet_poll(&right, 1u, &events), SALTS_OK);
  }
  check_equal(right_probe.receive_count, 1);
  check_equal(right_probe.received_size, sizeof(message));
  check_equal(right_probe.received, message, sizeof(message));
  check_equal(left_probe.error_count, 0);
  check_equal(right_probe.error_count, 0);

  check_equal(cnet_packet_endpoint_stop(&right, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
  check_equal(cnet_packet_endpoint_stop(&left, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
  check_equal(cnet_packet_endpoint_destroy(&right), SALTS_OK);
  check_equal(cnet_packet_endpoint_destroy(&left), SALTS_OK);
}

spec("CNet unified UDP and KCP packet endpoint") {
  it("uses one endpoint contract for UDP") { cnet_packet_test_round_trip(CNET_PACKET_UDP, 0u); }

  it("uses one endpoint contract for KCP") {
    cnet_packet_test_round_trip(CNET_PACKET_KCP, UINT32_C(0x12345678));
  }

  it("uses the endpoint contract for authenticated KCP after handshake") {
    cnet_packet_test_secure_round_trip();
  }

  it("does not admit an unauthenticated KCP client hello") {
    cnet_packet_endpoint left = {0};
    cnet_packet_endpoint right = {0};
    cnet_packet_test_probe left_probe = {0};
    cnet_packet_test_probe right_probe = {0};
    cnet_packet_endpoint_config left_config = cnet_packet_test_config(CNET_PACKET_KCP, &left_probe);
    cnet_packet_endpoint_config right_config = cnet_packet_test_config(CNET_PACKET_KCP, &right_probe);
    cnet_packet_session left_session = {0};
    cnet_datagram_peer right_peer;
    uint16_t right_port = 0u;
    size_t events = 0u;
    size_t attempts;

    cnet_packet_test_secure_config(&left_config, 0x11u);
    cnet_packet_test_secure_config(&right_config, 0x22u);
    check_equal(cnet_packet_endpoint_init(&left, &left_config), SALTS_OK);
    check_equal(cnet_packet_endpoint_init(&right, &right_config), SALTS_OK);
    check_equal(cnet_packet_endpoint_port(&right, &right_port), SALTS_OK);
    right_peer = cnet_packet_test_peer(right_port);
    check_equal(cnet_packet_session_open(&left, &right_peer, 0u, &left_session), SALTS_OK);
    for (attempts = 0u; attempts < 50u; ++attempts) {
      check_equal(cnet_packet_poll(&left, 1u, &events), SALTS_OK);
      check_equal(cnet_packet_poll(&right, 1u, &events), SALTS_OK);
    }
    check_equal(left_probe.connecting_count, 1);
    check_equal(left_probe.open_count, 0);
    check_equal(right_probe.admit_count, 0);
    check_equal(right_probe.open_count, 0);
    check_equal(left_probe.error_count, 0);
    check_equal(right_probe.error_count, 0);

    check_equal(cnet_packet_endpoint_stop(&right, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_packet_endpoint_stop(&left, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_packet_endpoint_destroy(&right), SALTS_OK);
    check_equal(cnet_packet_endpoint_destroy(&left), SALTS_OK);
  }

  it("rejects stale sessions after bounded slot reuse") {
    cnet_packet_endpoint endpoint = {0};
    cnet_packet_test_probe probe = {0};
    cnet_packet_endpoint_config config = cnet_packet_test_config(CNET_PACKET_UDP, &probe);
    cnet_datagram_peer first_peer = cnet_packet_test_peer(10001u);
    cnet_datagram_peer second_peer = cnet_packet_test_peer(10002u);
    cnet_packet_session first = {0};
    cnet_packet_session second = {0};
    cnet_packet_session rejected = {0};
    cnet_packet_session_info info = {0};

    config.session_capacity = 1u;
    check_equal(cnet_packet_endpoint_init(&endpoint, &config), SALTS_OK);
    check_equal(cnet_packet_endpoint_init(&endpoint, &config), SALTS_EALREADY);
    check_not_null(endpoint.impl);
    check_equal(cnet_packet_session_open(&endpoint, &first_peer, 0u, &first), SALTS_OK);
    check_equal(cnet_packet_session_get_info(&endpoint, first, &info), SALTS_OK);
    check_equal(info.protocol, CNET_PACKET_UDP);
    check_equal(info.peer.port, first_peer.port);
    check_equal(info.conversation, 0u);
    check_equal(cnet_packet_session_open(&endpoint, &first_peer, 0u, &rejected), SALTS_EALREADY);
    check_false(cnet_packet_session_valid(rejected));
    check_equal(cnet_packet_session_open(&endpoint, &second_peer, 0u, &rejected), SALTS_ENOBUFS);
    check_false(cnet_packet_session_valid(rejected));
    check_equal(cnet_packet_session_close(&endpoint, first), SALTS_OK);
    check_equal(cnet_packet_session_open(&endpoint, &second_peer, 0u, &second), SALTS_OK);
    check_equal(first.slot, second.slot);
    check_true(first.generation != second.generation);
    check_equal(cnet_packet_send(&endpoint, first, "x", 1u), SALTS_ENOENT);
    check_equal(cnet_packet_session_get_info(&endpoint, first, &info), SALTS_ENOENT);
    check_equal(cnet_packet_endpoint_stop(&endpoint, CNET_PACKET_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_packet_endpoint_destroy(&endpoint), SALTS_OK);
  }
}
