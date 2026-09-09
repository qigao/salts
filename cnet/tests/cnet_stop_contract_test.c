#include "tinytest.h"

#include "cnet_test_internal.h"

#include <string.h>

enum {
  CNET_STOP_CONTRACT_TIMEOUT_MS = 5000,
  CNET_STOP_CONTRACT_FORCED_TIMEOUT_MS = 5
};

typedef struct cnet_stop_contract_probe {
  size_t send_count;
  size_t send_size;
  int send_status;
  uint64_t send_tag;
  size_t event_sequence;
  size_t terminal_sequence;
  size_t closed_sequence;
  size_t closed_count;
} cnet_stop_contract_probe;

static native_io_backend_kind cnet_stop_contract_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_datagram_peer cnet_stop_contract_peer(uint16_t port) {
  cnet_datagram_peer peer = {0};
  peer.family = CNET_DATAGRAM_ADDRESS_IPV4;
  peer.port = port;
  peer.address[0] = 127u;
  peer.address[3] = 1u;
  return peer;
}

static void cnet_stop_contract_receive(void *user, cnet_datagram *datagram,
                                       const cnet_datagram_peer *peer,
                                       const cnet_receive_view *view) {
  (void)user;
  (void)datagram;
  (void)peer;
  (void)view;
}

static void cnet_stop_contract_send(void *user, cnet_datagram *datagram,
                                    const cnet_datagram_peer *peer, size_t size, int status,
                                    uint64_t tag) {
  cnet_stop_contract_probe *probe = (cnet_stop_contract_probe *)user;
  (void)datagram;
  (void)peer;
  ++probe->send_count;
  probe->send_size = size;
  probe->send_status = status;
  probe->send_tag = tag;
}

static cnet_datagram_config cnet_stop_contract_datagram_config(
    cnet_stop_contract_probe *probe) {
  cnet_datagram_config config = CNET_DATAGRAM_CONFIG_INIT;
  config.backend = cnet_stop_contract_backend();
  config.host = "127.0.0.1";
  config.port = 0u;
  config.send_capacity = 1u;
  config.request_capacity = 2u;
  config.completion_batch_capacity = 1u;
  config.max_datagram_bytes = 256u;
  config.receive_buffer_bytes = 256u;
  config.observer.on_receive = cnet_stop_contract_receive;
  config.observer.on_send = cnet_stop_contract_send;
  config.observer.user = probe;
  return config;
}

static void cnet_stop_contract_packet_state(void *user, cnet_packet_endpoint *endpoint,
                                            cnet_packet_session session,
                                            cnet_packet_session_state state,
                                            const cnet_datagram_peer *peer,
                                            uint32_t conversation) {
  cnet_stop_contract_probe *probe = (cnet_stop_contract_probe *)user;
  (void)endpoint;
  (void)session;
  (void)peer;
  (void)conversation;
  if (state == CNET_PACKET_SESSION_CLOSED) {
    ++probe->closed_count;
    probe->closed_sequence = ++probe->event_sequence;
  }
}

static int cnet_stop_contract_packet_admit(void *user, cnet_packet_endpoint *endpoint,
                                           cnet_packet_protocol protocol,
                                           const cnet_datagram_peer *peer,
                                           uint32_t conversation) {
  (void)user;
  (void)endpoint;
  (void)protocol;
  (void)peer;
  (void)conversation;
  return SALTS_OK;
}

static void cnet_stop_contract_packet_receive(void *user, cnet_packet_endpoint *endpoint,
                                              cnet_packet_session session,
                                              const cnet_receive_view *view) {
  (void)user;
  (void)endpoint;
  (void)session;
  (void)view;
}

static void cnet_stop_contract_packet_error(void *user, cnet_packet_endpoint *endpoint,
                                            cnet_packet_session session, int status) {
  (void)user;
  (void)endpoint;
  (void)session;
  (void)status;
}

static void cnet_stop_contract_packet_terminal(void *user, cnet_packet_endpoint *endpoint,
                                               cnet_packet_session session, size_t size,
                                               int status, uint64_t tag) {
  cnet_stop_contract_probe *probe = (cnet_stop_contract_probe *)user;
  (void)endpoint;
  (void)session;
  ++probe->send_count;
  probe->send_size = size;
  probe->send_status = status;
  probe->send_tag = tag;
  probe->terminal_sequence = ++probe->event_sequence;
}

static cnet_packet_endpoint_config cnet_stop_contract_packet_config(
    cnet_stop_contract_probe *probe) {
  cnet_packet_endpoint_config config = CNET_PACKET_ENDPOINT_CONFIG_INIT;
  config.protocol = CNET_PACKET_UDP;
  config.session_capacity = 1u;
  config.datagram = cnet_stop_contract_datagram_config(NULL);
  config.datagram.observer = (cnet_datagram_observer){0};
  config.observer.on_admit = cnet_stop_contract_packet_admit;
  config.observer.on_state = cnet_stop_contract_packet_state;
  config.observer.on_receive = cnet_stop_contract_packet_receive;
  config.observer.on_error = cnet_stop_contract_packet_error;
  config.observer.user = probe;
  return config;
}

spec("CNet stop terminal drain contract") {
  it("drains an admitted datagram send before returning the first drive error") {
    static const char payload[] = "datagram-stop";
    cnet_datagram datagram = {0};
    cnet_datagram receiver = {0};
    cnet_stop_contract_probe probe = {0};
    cnet_datagram_config config = cnet_stop_contract_datagram_config(&probe);
    cnet_datagram_config receiver_config = cnet_stop_contract_datagram_config(NULL);
    cnet_datagram_peer peer;
    uint16_t receiver_port = 0u;
    size_t send_count_at_return;

    check_equal(cnet_datagram_init(&receiver, &receiver_config), SALTS_OK);
    check_equal(cnet_datagram_port(&receiver, &receiver_port), SALTS_OK);
    peer = cnet_stop_contract_peer(receiver_port);
    check_equal(cnet_datagram_init(&datagram, &config), SALTS_OK);
    check_equal(cnet_datagram_send(&datagram, &peer, payload, sizeof(payload) - 1u,
                                   UINT64_C(41)),
                SALTS_OK);
    check_equal(cnet_test_datagram_fail_next_drive(&datagram, SALTS_EIO), SALTS_OK);
    check_equal(cnet_datagram_stop(&datagram, CNET_STOP_CONTRACT_TIMEOUT_MS), SALTS_EIO);
    send_count_at_return = probe.send_count;
    check_equal(send_count_at_return, (size_t)1u);
    check_equal(probe.send_count, (size_t)1u);
    check_equal(probe.send_size, sizeof(payload) - 1u);
    check_equal(probe.send_tag, UINT64_C(41));
    check_true(probe.send_status == SALTS_OK || probe.send_status == SALTS_ECANCELED);
    check_equal(cnet_datagram_destroy(&datagram), SALTS_OK);
    check_equal(cnet_datagram_stop(&receiver, CNET_STOP_CONTRACT_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_datagram_destroy(&receiver), SALTS_OK);
  }

  it("preserves the first drive error across timeout and returns it after retry drain") {
    static const char payload[] = "datagram-retry";
    cnet_datagram datagram = {0};
    cnet_datagram receiver = {0};
    cnet_stop_contract_probe probe = {0};
    cnet_datagram_config config = cnet_stop_contract_datagram_config(&probe);
    cnet_datagram_config receiver_config = cnet_stop_contract_datagram_config(NULL);
    cnet_datagram_peer peer;
    uint16_t receiver_port = 0u;

    check_equal(cnet_datagram_init(&receiver, &receiver_config), SALTS_OK);
    check_equal(cnet_datagram_port(&receiver, &receiver_port), SALTS_OK);
    peer = cnet_stop_contract_peer(receiver_port);
    check_equal(cnet_datagram_init(&datagram, &config), SALTS_OK);
    check_equal(cnet_datagram_send(&datagram, &peer, payload, sizeof(payload) - 1u,
                                   UINT64_C(42)),
                SALTS_OK);
    check_equal(cnet_test_datagram_set_persistent_drive_failure(&datagram, SALTS_EIO),
                SALTS_OK);
    check_equal(cnet_datagram_stop(&datagram, CNET_STOP_CONTRACT_FORCED_TIMEOUT_MS),
                SALTS_ETIMEDOUT);
    check_equal(probe.send_count, (size_t)0u);
    check_equal(cnet_test_datagram_set_persistent_drive_failure(&datagram, SALTS_OK),
                SALTS_OK);
    check_equal(cnet_datagram_stop(&datagram, CNET_STOP_CONTRACT_TIMEOUT_MS), SALTS_EIO);
    check_equal(probe.send_count, (size_t)1u);
    check_equal(probe.send_size, sizeof(payload) - 1u);
    check_equal(probe.send_tag, UINT64_C(42));
    check_true(probe.send_status == SALTS_OK || probe.send_status == SALTS_ECANCELED);
    check_equal(cnet_datagram_destroy(&datagram), SALTS_OK);
    check_equal(cnet_datagram_stop(&receiver, CNET_STOP_CONTRACT_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_datagram_destroy(&receiver), SALTS_OK);
  }

  it("continues a completion batch after preserving its prefix error") {
    size_t callbacks = 0u;
    check_equal(cnet_test_datagram_process_mixed_batch(&callbacks), SALTS_EPROTO);
    check_equal(callbacks, (size_t)1u);
  }

  it("drains a tagged packet send before propagating the datagram stop error") {
    static const char payload[] = "packet-stop";
    cnet_packet_endpoint endpoint = {0};
    cnet_datagram receiver = {0};
    cnet_stop_contract_probe probe = {0};
    cnet_packet_endpoint_config config = cnet_stop_contract_packet_config(&probe);
    cnet_datagram_config receiver_config = cnet_stop_contract_datagram_config(NULL);
    cnet_packet_terminal_config terminal = CNET_PACKET_TERMINAL_CONFIG_INIT;
    cnet_datagram_peer peer;
    cnet_packet_session session = {0};
    uint16_t receiver_port = 0u;
    size_t terminal_count_at_return;

    check_equal(cnet_datagram_init(&receiver, &receiver_config), SALTS_OK);
    check_equal(cnet_datagram_port(&receiver, &receiver_port), SALTS_OK);
    peer = cnet_stop_contract_peer(receiver_port);
    terminal.send_capacity = 1u;
    terminal.on_send = cnet_stop_contract_packet_terminal;
    terminal.user = &probe;
    check_equal(cnet_packet_endpoint_init_ex(&endpoint, &config, &terminal), SALTS_OK);
    check_equal(cnet_packet_session_open(&endpoint, &peer, 0u, &session), SALTS_OK);
    check_equal(cnet_packet_send_tagged(&endpoint, session, payload, sizeof(payload) - 1u,
                                        UINT64_C(73)),
                SALTS_OK);
    check_equal(cnet_test_packet_endpoint_fail_next_datagram_drive(&endpoint, SALTS_EIO),
                SALTS_OK);
    check_equal(cnet_packet_endpoint_stop(&endpoint, CNET_STOP_CONTRACT_TIMEOUT_MS), SALTS_EIO);
    terminal_count_at_return = probe.send_count;
    check_equal(terminal_count_at_return, (size_t)1u);
    check_equal(probe.send_count, (size_t)1u);
    check_equal(probe.send_size, sizeof(payload) - 1u);
    check_equal(probe.send_tag, UINT64_C(73));
    check_true(probe.send_status == SALTS_OK || probe.send_status == SALTS_ECANCELED);
    check_equal(probe.closed_count, (size_t)1u);
    check_true(probe.terminal_sequence < probe.closed_sequence);
    check_equal(cnet_packet_endpoint_destroy(&endpoint), SALTS_OK);
    check_equal(cnet_datagram_stop(&receiver, CNET_STOP_CONTRACT_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_datagram_destroy(&receiver), SALTS_OK);
  }
}
