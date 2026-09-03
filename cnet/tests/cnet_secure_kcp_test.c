#include "tinytest.h"
#include <cnet/cnet.h>

#include <stdlib.h>
#include <string.h>

enum {
  CNET_SECURE_TEST_PACKET_CAPACITY = 128,
  CNET_SECURE_TEST_PACKET_BYTES = 1600,
  CNET_SECURE_TEST_MESSAGE_BYTES = 700
};

static const uint8_t CNET_SECURE_TEST_PSK[CNET_KCP_PSK_BYTES] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xa9, 0xba,
    0xcb, 0xdc, 0xed, 0xfe, 0x0f, 0x1f, 0x2e, 0x3d, 0x4c, 0x5b, 0x6a,
    0x79, 0x88, 0x97, 0xa6, 0xb5, 0xc4, 0xd3, 0xe2, 0xf1, 0x01};

typedef struct cnet_secure_test_packet {
  size_t size;
  unsigned char data[CNET_SECURE_TEST_PACKET_BYTES];
} cnet_secure_test_packet;

typedef struct cnet_secure_test_queue {
  cnet_secure_test_packet *packets;
  size_t count;
} cnet_secure_test_queue;

typedef struct cnet_secure_test_peer {
  cnet_secure_test_queue *output;
  unsigned char received[CNET_SECURE_TEST_MESSAGE_BYTES];
  size_t received_size;
  int receive_count;
  int established_count;
} cnet_secure_test_peer;

static int cnet_secure_test_output(void *user, cnet_secure_kcp *session, const void *data,
                                   size_t size) {
  cnet_secure_test_peer *peer = (cnet_secure_test_peer *)user;
  cnet_secure_test_queue *queue = peer != NULL ? peer->output : NULL;
  (void)session;
  if (queue == NULL || data == NULL || size == 0u ||
      size > CNET_SECURE_TEST_PACKET_BYTES ||
      queue->count == CNET_SECURE_TEST_PACKET_CAPACITY)
    return SALTS_ENOBUFS;
  queue->packets[queue->count].size = size;
  memcpy(queue->packets[queue->count].data, data, size);
  ++queue->count;
  return SALTS_OK;
}

static void cnet_secure_test_receive(void *user, cnet_secure_kcp *session,
                                     const cnet_receive_view *view) {
  cnet_secure_test_peer *peer = (cnet_secure_test_peer *)user;
  (void)session;
  if (peer == NULL || view == NULL || view->size > sizeof(peer->received)) return;
  memcpy(peer->received, view->data, view->size);
  peer->received_size = view->size;
  ++peer->receive_count;
}

static void cnet_secure_test_established(void *user, cnet_secure_kcp *session) {
  cnet_secure_test_peer *peer = (cnet_secure_test_peer *)user;
  (void)session;
  if (peer != NULL) ++peer->established_count;
}

static cnet_secure_kcp_config cnet_secure_test_config(cnet_secure_kcp_role role,
                                                       cnet_secure_test_peer *peer) {
  cnet_secure_kcp_config config = CNET_SECURE_KCP_CONFIG_INIT;
  config.role = role;
  config.kcp.mtu = 576u;
  config.kcp.send_window = 32u;
  config.kcp.receive_window = 32u;
  config.kcp.interval_ms = 10u;
  config.kcp.send_segment_capacity = 64u;
  config.kcp.max_message_bytes = sizeof(peer->received);
  config.security.mode = CNET_KCP_SECURITY_PSK_V1;
  memcpy(config.security.pre_shared_key, CNET_SECURE_TEST_PSK,
         sizeof(config.security.pre_shared_key));
  config.security.handshake_retry_ms = 20u;
  config.security.fec.backend = CNET_KCP_FEC_REED_SOLOMON;
  config.security.fec.data_shards = 2u;
  config.security.fec.parity_shards = 1u;
  config.security.fec.max_payload_bytes = 624u;
  config.security.fec.receive_group_count = 4u;
  config.observer.output = cnet_secure_test_output;
  config.observer.on_receive = cnet_secure_test_receive;
  config.observer.on_established = cnet_secure_test_established;
  config.observer.user = peer;
  return config;
}

static int cnet_secure_test_queue_init(cnet_secure_test_queue *queue) {
  if (queue == NULL) return SALTS_EINVAL;
  queue->packets = (cnet_secure_test_packet *)calloc(CNET_SECURE_TEST_PACKET_CAPACITY,
                                                      sizeof(*queue->packets));
  return queue->packets != NULL ? SALTS_OK : SALTS_ENOMEM;
}

static void cnet_secure_test_queue_destroy(cnet_secure_test_queue *queue) {
  if (queue == NULL) return;
  free(queue->packets);
  memset(queue, 0, sizeof(*queue));
}

static int cnet_secure_test_deliver(cnet_secure_test_queue *queue, cnet_secure_kcp *target,
                                    int drop_first_fec_data,
                                    cnet_secure_test_packet *saved_frame) {
  size_t index;
  int dropped = 0;
  int status = SALTS_OK;
  if (queue == NULL || target == NULL) return SALTS_EINVAL;
  for (index = 0u; index < queue->count; ++index) {
    cnet_secure_test_packet *packet = &queue->packets[index];
    const int fec_data = packet->size >= 6u && memcmp(packet->data, "TKF1", 4u) == 0 &&
                         packet->data[5] == 1u;
    if (fec_data && saved_frame != NULL && saved_frame->size == 0u) *saved_frame = *packet;
    if (drop_first_fec_data && !dropped && fec_data) {
      dropped = 1;
      continue;
    }
    status = cnet_secure_kcp_input(target, packet->data, packet->size);
    if (status != SALTS_OK) break;
  }
  queue->count = 0u;
  return status;
}

spec("CNet secure KCP v1") {
  it("rejects an absent PSK without publishing a session") {
    cnet_secure_kcp session = {0};
    cnet_secure_test_queue output = {0};
    cnet_secure_test_peer peer = {0};
    cnet_secure_kcp_config config;

    check_equal(cnet_secure_test_queue_init(&output), SALTS_OK);
    peer.output = &output;
    config = cnet_secure_test_config(CNET_SECURE_KCP_CLIENT, &peer);
    memset(config.security.pre_shared_key, 0, sizeof(config.security.pre_shared_key));
    check_equal(cnet_secure_kcp_init(&session, &config), SALTS_EINVAL);
    check_null(session.impl);
    cnet_secure_test_queue_destroy(&output);
  }

  it("retries the identical authenticated client hello at the configured deadline") {
    cnet_secure_kcp client = {0};
    cnet_secure_test_queue output = {0};
    cnet_secure_test_peer peer = {0};
    cnet_secure_kcp_config config;
    cnet_secure_test_packet first;

    memset(&first, 0, sizeof(first));
    check_equal(cnet_secure_test_queue_init(&output), SALTS_OK);
    peer.output = &output;
    config = cnet_secure_test_config(CNET_SECURE_KCP_CLIENT, &peer);
    check_equal(cnet_secure_kcp_init(&client, &config), SALTS_OK);
    check_equal(cnet_secure_kcp_start(&client, 100u), SALTS_OK);
    check_equal(output.count, 1u);
    first = output.packets[0];
    check_equal(first.size, 64u);
    check_equal(first.data, "TKSH", 4u);
    output.count = 0u;
    check_equal(cnet_secure_kcp_update(&client, 119u), SALTS_OK);
    check_equal(output.count, 0u);
    check_equal(cnet_secure_kcp_update(&client, 120u), SALTS_OK);
    check_equal(output.count, 1u);
    check_equal(output.packets[0].size, first.size);
    check_equal(output.packets[0].data, first.data, first.size);
    check_equal(cnet_secure_kcp_destroy(&client), SALTS_OK);
    cnet_secure_test_queue_destroy(&output);
  }

  it("rejects a client hello authenticated by a different PSK") {
    cnet_secure_kcp client = {0};
    cnet_secure_kcp server = {0};
    cnet_secure_test_queue client_output = {0};
    cnet_secure_test_queue server_output = {0};
    cnet_secure_test_peer client_peer = {0};
    cnet_secure_test_peer server_peer = {0};
    cnet_secure_kcp_config client_config;
    cnet_secure_kcp_config server_config;

    check_equal(cnet_secure_test_queue_init(&client_output), SALTS_OK);
    check_equal(cnet_secure_test_queue_init(&server_output), SALTS_OK);
    client_peer.output = &client_output;
    server_peer.output = &server_output;
    client_config = cnet_secure_test_config(CNET_SECURE_KCP_CLIENT, &client_peer);
    server_config = cnet_secure_test_config(CNET_SECURE_KCP_SERVER, &server_peer);
    server_config.security.pre_shared_key[0] ^= 0x80u;
    check_equal(cnet_secure_kcp_init(&client, &client_config), SALTS_OK);
    check_equal(cnet_secure_kcp_init(&server, &server_config), SALTS_OK);
    check_equal(cnet_secure_kcp_start(&server, 100u), SALTS_OK);
    check_equal(cnet_secure_kcp_start(&client, 100u), SALTS_OK);
    check_equal(client_output.count, 1u);
    check_equal(cnet_secure_kcp_input(&server, client_output.packets[0].data,
                                      client_output.packets[0].size),
                SALTS_EPERM);
    check_equal(server_output.count, 0u);
    check_false(cnet_secure_kcp_established(&server));
    check_equal(cnet_secure_kcp_destroy(&server), SALTS_OK);
    check_equal(cnet_secure_kcp_destroy(&client), SALTS_OK);
    cnet_secure_test_queue_destroy(&server_output);
    cnet_secure_test_queue_destroy(&client_output);
  }

  it("recovers one lost authenticated KCP datagram and rejects replay and tamper") {
    cnet_secure_kcp client = {0};
    cnet_secure_kcp server = {0};
    cnet_secure_test_queue client_output = {0};
    cnet_secure_test_queue server_output = {0};
    cnet_secure_test_peer client_peer = {0};
    cnet_secure_test_peer server_peer = {0};
    cnet_secure_kcp_config client_config;
    cnet_secure_kcp_config server_config;
    cnet_secure_test_packet saved_frame;
    unsigned char message[CNET_SECURE_TEST_MESSAGE_BYTES];
    uint32_t conversation = 0u;
    size_t index;
    size_t attempt;

    memset(&saved_frame, 0, sizeof(saved_frame));
    for (index = 0u; index < sizeof(message); ++index) message[index] = (unsigned char)index;
    check_equal(cnet_secure_test_queue_init(&client_output), SALTS_OK);
    check_equal(cnet_secure_test_queue_init(&server_output), SALTS_OK);
    client_peer.output = &client_output;
    server_peer.output = &server_output;
    client_config = cnet_secure_test_config(CNET_SECURE_KCP_CLIENT, &client_peer);
    server_config = cnet_secure_test_config(CNET_SECURE_KCP_SERVER, &server_peer);
    check_equal(cnet_secure_kcp_init(&client, &client_config), SALTS_OK);
    check_equal(cnet_secure_kcp_init(&server, &server_config), SALTS_OK);
    check_equal(cnet_secure_kcp_start(&server, 100u), SALTS_OK);
    check_equal(cnet_secure_kcp_start(&client, 100u), SALTS_OK);
    check_equal(cnet_secure_test_deliver(&client_output, &server, 0, NULL), SALTS_OK);
    check_equal(cnet_secure_test_deliver(&server_output, &client, 0, NULL), SALTS_OK);
    check_true(cnet_secure_kcp_established(&client));
    check_true(cnet_secure_kcp_established(&server));
    check_equal(client_peer.established_count, 1);
    check_equal(server_peer.established_count, 1);
    check_equal(cnet_secure_kcp_conversation(&client, &conversation), SALTS_OK);
    check_true(conversation != 0u);

    check_equal(cnet_secure_kcp_send(&client, message, sizeof(message)), SALTS_OK);
    for (attempt = 0u; attempt < 100u && server_peer.receive_count == 0; ++attempt) {
      check_equal(cnet_secure_kcp_update(&client, (uint32_t)(200u + attempt * 10u)), SALTS_OK);
      check_equal(cnet_secure_test_deliver(&client_output, &server, attempt == 0u, &saved_frame),
                  SALTS_OK);
      check_equal(cnet_secure_kcp_update(&server, (uint32_t)(200u + attempt * 10u)), SALTS_OK);
      check_equal(cnet_secure_test_deliver(&server_output, &client, 0, NULL), SALTS_OK);
    }
    check_equal(server_peer.receive_count, 1);
    check_equal(server_peer.received_size, sizeof(message));
    check_equal(server_peer.received, message, sizeof(message));
    check_true(saved_frame.size != 0u);
    check_equal(cnet_secure_kcp_input(&server, saved_frame.data, saved_frame.size), SALTS_EALREADY);
    saved_frame.data[saved_frame.size - 1u] ^= 0x01u;
    check_equal(cnet_secure_kcp_input(&server, saved_frame.data, saved_frame.size), SALTS_EPERM);

    check_equal(cnet_secure_kcp_destroy(&server), SALTS_OK);
    check_equal(cnet_secure_kcp_destroy(&client), SALTS_OK);
    cnet_secure_test_queue_destroy(&server_output);
    cnet_secure_test_queue_destroy(&client_output);
  }
}
