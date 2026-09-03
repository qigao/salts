#include "tinytest.h"
#include <cnet/cnet.h>

#include <string.h>

enum {
  CNET_KCP_TEST_PACKET_CAPACITY = 64,
  CNET_KCP_TEST_PACKET_BYTES = 1500,
  CNET_KCP_TEST_MESSAGE_BYTES = 4096,
  CNET_KCP_TEST_STEP_MS = 10,
  CNET_KCP_TEST_DEADLINE_MS = 10000
};

typedef struct cnet_kcp_test_packet {
  unsigned char data[CNET_KCP_TEST_PACKET_BYTES];
  size_t size;
} cnet_kcp_test_packet;

typedef struct cnet_kcp_test_link {
  cnet_kcp_test_packet packets[CNET_KCP_TEST_PACKET_CAPACITY];
  size_t count;
  size_t drop_remaining;
  int output_status;
} cnet_kcp_test_link;

typedef struct cnet_kcp_test_endpoint {
  cnet_kcp_test_link *outbound;
  unsigned char received[CNET_KCP_TEST_MESSAGE_BYTES];
  size_t received_size;
  int receive_count;
} cnet_kcp_test_endpoint;

static int cnet_kcp_test_output(void *user, cnet_kcp *session, const void *data, size_t size) {
  cnet_kcp_test_endpoint *endpoint = (cnet_kcp_test_endpoint *)user;
  cnet_kcp_test_link *link = endpoint->outbound;
  cnet_kcp_test_packet *packet;
  (void)session;
  if (link->output_status != SALTS_OK) return link->output_status;
  if (link->drop_remaining != 0u) {
    --link->drop_remaining;
    return SALTS_OK;
  }
  if (link->count == CNET_KCP_TEST_PACKET_CAPACITY || size > CNET_KCP_TEST_PACKET_BYTES)
    return SALTS_ENOBUFS;
  packet = &link->packets[link->count++];
  memcpy(packet->data, data, size);
  packet->size = size;
  return SALTS_OK;
}

static void cnet_kcp_test_receive(void *user, cnet_kcp *session,
                                  const cnet_receive_view *view) {
  cnet_kcp_test_endpoint *endpoint = (cnet_kcp_test_endpoint *)user;
  (void)session;
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
      view->size > sizeof(endpoint->received))
    return;
  memcpy(endpoint->received, view->data, view->size);
  endpoint->received_size = view->size;
  ++endpoint->receive_count;
}

static cnet_kcp_config cnet_kcp_test_config(cnet_kcp_test_endpoint *endpoint) {
  cnet_kcp_config config = CNET_KCP_CONFIG_INIT;
  config.conversation = UINT32_C(0x10203040);
  config.mtu = 512u;
  config.send_window = 64u;
  config.receive_window = 64u;
  config.interval_ms = CNET_KCP_TEST_STEP_MS;
  config.fast_resend = 2u;
  config.no_congestion_window = true;
  config.stream_mode = false;
  config.send_segment_capacity = 128u;
  config.max_message_bytes = CNET_KCP_TEST_MESSAGE_BYTES;
  config.observer.output = cnet_kcp_test_output;
  config.observer.on_receive = cnet_kcp_test_receive;
  config.observer.user = endpoint;
  return config;
}

static int cnet_kcp_test_deliver(cnet_kcp_test_link *link, cnet_kcp *destination) {
  size_t index;
  for (index = 0u; index < link->count; ++index) {
    const int status =
        cnet_kcp_input(destination, link->packets[index].data, link->packets[index].size);
    if (status != SALTS_OK) return status;
  }
  link->count = 0u;
  return SALTS_OK;
}

spec("CNet bounded KCP session") {
  it("rejects malformed configuration without publishing an object") {
    cnet_kcp session = {0};
    cnet_kcp_test_link link = {0};
    cnet_kcp_test_endpoint endpoint = {.outbound = &link};
    cnet_kcp_config config = cnet_kcp_test_config(&endpoint);

    config.conversation = 0u;
    check_equal(cnet_kcp_init(&session, &config), SALTS_EINVAL);
    check_null(session.impl);
    config = cnet_kcp_test_config(&endpoint);
    config.send_segment_capacity = 0u;
    check_equal(cnet_kcp_init(&session, &config), SALTS_EINVAL);
    check_null(session.impl);
    config = cnet_kcp_test_config(&endpoint);
    config.max_message_bytes = 0u;
    check_equal(cnet_kcp_init(&session, &config), SALTS_EINVAL);
    check_null(session.impl);
  }

  it("reassembles a fragmented message across two caller-driven sessions") {
    cnet_kcp left = {0};
    cnet_kcp right = {0};
    cnet_kcp_test_link left_to_right = {0};
    cnet_kcp_test_link right_to_left = {0};
    cnet_kcp_test_endpoint left_endpoint = {.outbound = &left_to_right};
    cnet_kcp_test_endpoint right_endpoint = {.outbound = &right_to_left};
    cnet_kcp_config left_config = cnet_kcp_test_config(&left_endpoint);
    cnet_kcp_config right_config = cnet_kcp_test_config(&right_endpoint);
    unsigned char message[CNET_KCP_TEST_MESSAGE_BYTES];
    uint32_t now;
    size_t index;

    left_to_right.drop_remaining = 1u;
    for (index = 0u; index < sizeof(message); ++index) message[index] = (unsigned char)index;
    check_equal(cnet_kcp_init(&left, &left_config), SALTS_OK);
    check_equal(cnet_kcp_init(&right, &right_config), SALTS_OK);
    check_equal(cnet_kcp_send(&left, message, sizeof(message)), SALTS_OK);

    for (now = 0u; now <= CNET_KCP_TEST_DEADLINE_MS && right_endpoint.receive_count == 0;
         now += CNET_KCP_TEST_STEP_MS) {
      check_equal(cnet_kcp_update(&left, now), SALTS_OK);
      check_equal(cnet_kcp_update(&right, now), SALTS_OK);
      check_equal(cnet_kcp_test_deliver(&left_to_right, &right), SALTS_OK);
      check_equal(cnet_kcp_test_deliver(&right_to_left, &left), SALTS_OK);
    }
    check_equal(right_endpoint.receive_count, 1);
    check_equal(right_endpoint.received_size, sizeof(message));
    check_equal(right_endpoint.received, message, sizeof(message));
    check_equal(cnet_kcp_destroy(&right), SALTS_OK);
    check_equal(cnet_kcp_destroy(&left), SALTS_OK);
  }

  it("enforces segment admission and reports transport output failure") {
    static const unsigned char payload[] = "bounded";
    cnet_kcp session = {0};
    cnet_kcp_test_link link = {0};
    cnet_kcp_test_endpoint endpoint = {.outbound = &link};
    cnet_kcp_config config = cnet_kcp_test_config(&endpoint);
    uint32_t next_update = UINT32_MAX;

    config.send_segment_capacity = 1u;
    check_equal(cnet_kcp_init(&session, &config), SALTS_OK);
    check_equal(cnet_kcp_init(&session, &config), SALTS_EALREADY);
    check_not_null(session.impl);
    check_equal(cnet_kcp_send(&session, payload, sizeof(payload)), SALTS_OK);
    check_equal(cnet_kcp_send(&session, payload, sizeof(payload)), SALTS_ENOBUFS);
    check_equal(cnet_kcp_check(&session, 0u, &next_update), SALTS_OK);
    check_equal(next_update, 0u);
    link.output_status = SALTS_EIO;
    check_equal(cnet_kcp_update(&session, 0u), SALTS_EIO);
    check_equal(cnet_kcp_destroy(&session), SALTS_OK);
    check_null(session.impl);
  }
}
