#include <cnet/websocket.h>

#include "tinytest.h"
#include "websocket_frame_parser.h"

#include <turbo/error_codes.h>

#include <stdint.h>
#include <string.h>

enum {
  TEST_FRAME_BYTES = 256,
  TEST_MESSAGE_BYTES = 512,
  TEST_INPUT_BYTES = 1024,
  TEST_OUTPUT_FRAMES = 8
};

typedef struct websocket_probe {
  uint8_t output[TEST_OUTPUT_FRAMES][TEST_FRAME_BYTES + 14u];
  size_t output_size[TEST_OUTPUT_FRAMES];
  size_t output_count;
  int busy_writes;
  int write_status;
  cnet_websocket_event events[TEST_OUTPUT_FRAMES];
  uint8_t event_data[TEST_OUTPUT_FRAMES][TEST_MESSAGE_BYTES];
  size_t event_count;
  bool nested_event_ops;
  int nested_send_status;
  int nested_destroy_status;
} websocket_probe;

static int websocket_test_write(void *user, const uint8_t *data, size_t size) {
  websocket_probe *probe = (websocket_probe *)user;
  if (probe->busy_writes > 0) {
    --probe->busy_writes;
    return TURBO_EBUSY;
  }
  if (probe->write_status != TURBO_OK) return probe->write_status;
  if (probe->output_count >= TEST_OUTPUT_FRAMES || size > sizeof(probe->output[0]))
    return TURBO_ENOSPC;
  memcpy(probe->output[probe->output_count], data, size);
  probe->output_size[probe->output_count] = size;
  ++probe->output_count;
  return TURBO_OK;
}

static void websocket_test_event(void *user, cnet_websocket *websocket,
                                 const cnet_websocket_event *event) {
  websocket_probe *probe = (websocket_probe *)user;
  const size_t index = probe->event_count;
  (void)websocket;
  if (index >= TEST_OUTPUT_FRAMES || event->size > sizeof(probe->event_data[0])) return;
  probe->events[index] = *event;
  if (event->size != 0u) {
    memcpy(probe->event_data[index], event->data, event->size);
    probe->events[index].data = probe->event_data[index];
  }
  ++probe->event_count;
  if (probe->nested_event_ops) {
    probe->nested_event_ops = false;
    probe->nested_send_status = cnet_websocket_send_pong(websocket, "nested", 6u);
    probe->nested_destroy_status = cnet_websocket_destroy(websocket);
  }
}

static cnet_websocket_config websocket_test_config(websocket_probe *probe,
                                                   cnet_websocket_role role) {
  cnet_websocket_config config = {0};
  config.size = sizeof(config);
  config.role = role;
  config.max_frame_bytes = TEST_FRAME_BYTES;
  config.max_message_bytes = TEST_MESSAGE_BYTES;
  config.max_buffered_input_bytes = TEST_INPUT_BYTES;
  config.write = websocket_test_write;
  config.on_event = websocket_test_event;
  config.user = probe;
  return config;
}

static size_t websocket_test_frame(uint8_t *out, size_t capacity, uint8_t opcode, int fin,
                                   int masked, const void *payload, size_t payload_size) {
  static const uint8_t key[4] = {0x12u, 0x34u, 0x56u, 0x78u};
  size_t header_size = 0u;
  if (ws_frame_build_header(out, capacity, opcode, payload_size, fin, masked, masked ? key : NULL,
                            &header_size) != WS_PARSE_OK ||
      payload_size > capacity - header_size)
    return 0u;
  if (payload_size != 0u) memcpy(out + header_size, payload, payload_size);
  if (masked) {
    for (size_t index = 0u; index < payload_size; ++index)
      out[header_size + index] ^= key[index % 4u];
  }
  return header_size + payload_size;
}

static uint16_t websocket_test_close_code(const uint8_t *wire, size_t wire_size) {
  ws_frame_t frame = {0};
  if (ws_frame_parse(wire, wire_size, &frame) != WS_PARSE_OK || frame.opcode != WS_OPCODE_CLOSE ||
      frame.payload_len < 2u)
    return 0u;
  if (frame.masked)
    (void)ws_frame_unmask((uint8_t *)frame.payload, (size_t)frame.payload_len, frame.masking_key);
  return (uint16_t)(((uint16_t)frame.payload[0] << 8u) | frame.payload[1]);
}

spec("CNet WebSocket session") {
  it("requires explicit valid limits and callbacks") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);

    check_equal(cnet_websocket_init(NULL, &config), TURBO_EINVAL);
    check_equal(cnet_websocket_init(&websocket, NULL), TURBO_EINVAL);
    config.size = 0u;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_EINVAL);
    config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    config.max_frame_bytes = config.max_message_bytes + 1u;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_EINVAL);
    config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    config.max_buffered_input_bytes = config.max_frame_bytes + 13u;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_EINVAL);
#if SIZE_MAX > INT64_MAX
    config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    config.max_frame_bytes = (size_t)INT64_MAX + 1u;
    config.max_message_bytes = config.max_frame_bytes;
    config.max_buffered_input_bytes = config.max_frame_bytes + CNET_WEBSOCKET_MAX_HEADER_BYTES;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_EINVAL);
#endif
    config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    config.max_frame_bytes = CNET_WEBSOCKET_MIN_FRAME_BYTES - 1u;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_EINVAL);
    config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    config.max_frame_bytes = CNET_WEBSOCKET_MIN_FRAME_BYTES;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
    config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
    check_null(websocket.impl);
  }

  it("delivers masked server input split across feed calls") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[64];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 1, "hello", 5u);

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, 1u), TURBO_OK);
    check_equal(probe.event_count, 0u);
    check_equal(cnet_websocket_feed(&websocket, wire + 1u, wire_size - 1u), TURBO_OK);
    check_equal(probe.event_count, 1u);
    check_equal(probe.events[0].kind, CNET_WEBSOCKET_EVENT_MESSAGE);
    check_equal(probe.events[0].message_type, CNET_WEBSOCKET_MESSAGE_TEXT);
    check_equal(probe.events[0].data, "hello", 5u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("processes coalesced binary and pong frames") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[128];
    size_t first = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_BINARY, 1, 1, "\x01\x02", 2u);
    size_t second =
        websocket_test_frame(wire + first, sizeof(wire) - first, WS_OPCODE_PONG, 1, 1, "ok", 2u);

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, first + second), TURBO_OK);
    check_equal(probe.event_count, 2u);
    check_equal(probe.events[0].message_type, CNET_WEBSOCKET_MESSAGE_BINARY);
    check_equal(probe.events[1].kind, CNET_WEBSOCKET_EVENT_PONG);
    check_equal(probe.events[1].data, "ok", 2u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("reassembles fragments while answering an interleaved ping") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[128];
    size_t total = 0u;
    ws_frame_t pong = {0};

    total +=
        websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_TEXT, 0, 1, "hel", 3u);
    total +=
        websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_PING, 1, 1, "?", 1u);
    total += websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_CONTINUATION, 1, 1,
                                  "lo", 2u);
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, total), TURBO_OK);
    check_equal(probe.output_count, 1u);
    check_equal(ws_frame_parse(probe.output[0], probe.output_size[0], &pong), WS_PARSE_OK);
    check_equal(pong.opcode, WS_OPCODE_PONG);
    check_equal(pong.masked, 0u);
    check_equal(pong.payload, "?", 1u);
    check_equal(probe.event_count, 2u);
    check_equal(probe.events[0].kind, CNET_WEBSOCKET_EVENT_PING);
    check_equal(probe.events[1].kind, CNET_WEBSOCKET_EVENT_MESSAGE);
    check_equal(probe.events[1].data, "hello", 5u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("fails role masking violations with protocol close") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 0, "bad", 3u);
    int last_error = TURBO_OK;

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_EPROTO);
    check_equal(cnet_websocket_last_error(&websocket, &last_error), TURBO_OK);
    check_equal(last_error, TURBO_EPROTO);
    check_equal(probe.output_count, 1u);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1002u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("rejects masked server input in client role") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_CLIENT);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 1, "bad", 3u);

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_EPROTO);
    check_equal(probe.output_count, 1u);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1002u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("fails invalid reassembled UTF-8 with close code 1007") {
    static const uint8_t invalid[] = {0xc0u, 0xafu};
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[64];
    size_t wire_size =
        websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 1, invalid, sizeof(invalid));

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_ECHARSET);
    check_equal(probe.event_count, 0u);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1007u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("rejects invalid inbound fragmentation order") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[64];
    size_t wire_size =
        websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CONTINUATION, 1, 1, "orphan", 6u);

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_EPROTO);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1002u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("rejects a new data message during fragmented input") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[64];
    size_t total = 0u;

    total +=
        websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_TEXT, 0, 1, "a", 1u);
    total +=
        websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_BINARY, 1, 1, "b", 1u);
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, total), TURBO_EPROTO);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1002u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("bounds a reassembled fragmented message") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[64];
    size_t total = 0u;

    config.max_frame_bytes = 4u;
    config.max_message_bytes = 5u;
    total += websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_BINARY, 0, 1,
                                  "1234", 4u);
    total += websocket_test_frame(wire + total, sizeof(wire) - total, WS_OPCODE_CONTINUATION, 1, 1,
                                  "56", 2u);
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, total), TURBO_EMSGSIZE);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1009u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("rejects an announced frame beyond the configured limit without its payload") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t header[8] = {0};
    static const uint8_t masking_key[4] = {1u, 2u, 3u, 4u};
    size_t header_size = 0u;

    check_equal(ws_frame_build_header(header, sizeof(header), WS_OPCODE_BINARY,
                                      TEST_FRAME_BYTES + 1u, 1, 1, masking_key, &header_size),
                WS_PARSE_OK);
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, header, header_size), TURBO_EMSGSIZE);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1009u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("masks client output and retains one frame across transport backpressure") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {.busy_writes = 1};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_CLIENT);
    ws_frame_t frame = {0};

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_send_text(&websocket, "hello", 5u), TURBO_OK);
    check_true(cnet_websocket_has_pending_output(&websocket));
    check_equal(cnet_websocket_send_binary(&websocket, "x", 1u), TURBO_EBUSY);
    check_equal(cnet_websocket_flush(&websocket), TURBO_OK);
    check_false(cnet_websocket_has_pending_output(&websocket));
    check_equal(probe.output_count, 1u);
    check_equal(ws_frame_parse(probe.output[0], probe.output_size[0], &frame), WS_PARSE_OK);
    check_equal(frame.masked, 1u);
    check_equal(
        ws_frame_unmask((uint8_t *)frame.payload, (size_t)frame.payload_len, frame.masking_key),
        WS_PARSE_OK);
    check_equal(frame.payload, "hello", 5u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("does not consume caller input while output is backpressured") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {.busy_writes = 1};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t ping[32];
    uint8_t message[32];
    size_t ping_size = websocket_test_frame(ping, sizeof(ping), WS_OPCODE_PING, 1, 1, "?", 1u);
    size_t message_size =
        websocket_test_frame(message, sizeof(message), WS_OPCODE_TEXT, 1, 1, "later", 5u);

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, ping, ping_size), TURBO_OK);
    check_true(cnet_websocket_has_pending_output(&websocket));
    check_equal(cnet_websocket_feed(&websocket, message, message_size), TURBO_EBUSY);
    check_equal(probe.event_count, 1u);
    check_equal(cnet_websocket_flush(&websocket), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, message, message_size), TURBO_OK);
    check_equal(probe.event_count, 2u);
    check_equal(probe.events[1].data, "later", 5u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("keeps feed input atomic when the configured buffer is full") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    uint8_t rejected[32] = {0};
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 1, "ok", 2u);

    config.max_frame_bytes = 8u;
    config.max_message_bytes = 8u;
    config.max_buffered_input_bytes = 22u;
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, 1u), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, rejected, sizeof(rejected)), TURBO_ENOSPC);
    check_equal(cnet_websocket_feed(&websocket, wire + 1u, wire_size - 1u), TURBO_OK);
    check_equal(probe.event_count, 1u);
    check_equal(probe.events[0].data, "ok", 2u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("validates outbound text across fragment boundaries") {
    static const uint8_t invalid[] = {0xc0u, 0xafu};
    static const uint8_t first[] = {0xe4u, 0xb8u};
    static const uint8_t final[] = {0xadu};
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    ws_frame_t frame = {0};

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_send_text(&websocket, invalid, sizeof(invalid)), TURBO_ECHARSET);
    check_equal(probe.output_count, 0u);
    check_equal(cnet_websocket_send_fragment(&websocket, CNET_WEBSOCKET_MESSAGE_TEXT, first,
                                             sizeof(first), false),
                TURBO_OK);
    check_equal(cnet_websocket_send_fragment(&websocket, CNET_WEBSOCKET_MESSAGE_TEXT, final,
                                             sizeof(final), true),
                TURBO_OK);
    check_equal(probe.output_count, 2u);
    check_equal(ws_frame_parse(probe.output[0], probe.output_size[0], &frame), WS_PARSE_OK);
    check_equal(frame.opcode, WS_OPCODE_TEXT);
    check_equal(frame.fin, 0u);
    check_equal(ws_frame_parse(probe.output[1], probe.output_size[1], &frame), WS_PARSE_OK);
    check_equal(frame.opcode, WS_OPCODE_CONTINUATION);
    check_equal(frame.fin, 1u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("echoes a valid peer close and completes the closing handshake") {
    static const uint8_t close_payload[] = {0x03u, 0xe8u, 'b', 'y', 'e'};
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[64];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CLOSE, 1, 1,
                                            close_payload, sizeof(close_payload));
    cnet_websocket_state state = CNET_WEBSOCKET_OPEN;

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_OK);
    check_equal(probe.event_count, 1u);
    check_equal(probe.events[0].kind, CNET_WEBSOCKET_EVENT_CLOSE);
    check_equal(probe.events[0].close_code, 1000u);
    check_equal(probe.events[0].data, "bye", 3u);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1000u);
    check_equal(cnet_websocket_state_get(&websocket, &state), TURBO_OK);
    check_equal(state, CNET_WEBSOCKET_CLOSED);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("stays closing until a backpressured peer close echo is transferred") {
    static const uint8_t close_payload[] = {0x03u, 0xe8u};
    cnet_websocket websocket = {0};
    websocket_probe probe = {.busy_writes = 1};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CLOSE, 1, 1,
                                            close_payload, sizeof(close_payload));
    cnet_websocket_state state = CNET_WEBSOCKET_OPEN;

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_OK);
    check_true(cnet_websocket_has_pending_output(&websocket));
    check_equal(cnet_websocket_state_get(&websocket, &state), TURBO_OK);
    check_equal(state, CNET_WEBSOCKET_CLOSING);
    check_equal(cnet_websocket_flush(&websocket), TURBO_OK);
    check_false(cnet_websocket_has_pending_output(&websocket));
    check_equal(cnet_websocket_state_get(&websocket, &state), TURBO_OK);
    check_equal(state, CNET_WEBSOCKET_CLOSED);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("commits peer closing state before delivering its close event") {
    static const uint8_t close_payload[] = {0x03u, 0xe8u};
    cnet_websocket websocket = {0};
    websocket_probe probe = {.nested_event_ops = true};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CLOSE, 1, 1,
                                            close_payload, sizeof(close_payload));

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_OK);
    check_equal(probe.nested_send_status, TURBO_ESHUTDOWN);
    check_equal(probe.nested_destroy_status, TURBO_EBUSY);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("rejects invalid close payloads and reasons") {
    static const uint8_t invalid_code[] = {0x03u, 0xedu};
    static const uint8_t invalid_reason[] = {0x03u, 0xe8u, 0xc0u, 0xafu};
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CLOSE, 1, 1, "x", 1u);

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_EPROTO);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1002u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);

    memset(&probe, 0, sizeof(probe));
    wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CLOSE, 1, 1, invalid_code,
                                     sizeof(invalid_code));
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_EPROTO);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1002u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);

    memset(&probe, 0, sizeof(probe));
    wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_CLOSE, 1, 1, invalid_reason,
                                     sizeof(invalid_reason));
    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_ECHARSET);
    check_equal(websocket_test_close_code(probe.output[0], probe.output_size[0]), 1007u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("allows callback sends but rejects recursive lifecycle mutation") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {.nested_event_ops = true};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 1, "event", 5u);
    ws_frame_t frame = {0};

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_OK);
    check_equal(probe.nested_send_status, TURBO_OK);
    check_equal(probe.nested_destroy_status, TURBO_EBUSY);
    check_equal(probe.output_count, 1u);
    check_equal(ws_frame_parse(probe.output[0], probe.output_size[0], &frame), WS_PARSE_OK);
    check_equal(frame.opcode, WS_OPCODE_PONG);
    check_equal(frame.payload, "nested", 6u);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("propagates a terminal nested callback write error from feed") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {.write_status = TURBO_EIO, .nested_event_ops = true};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    uint8_t wire[32];
    size_t wire_size = websocket_test_frame(wire, sizeof(wire), WS_OPCODE_TEXT, 1, 1, "event", 5u);
    cnet_websocket_state state = CNET_WEBSOCKET_OPEN;
    int last_error = TURBO_OK;

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_feed(&websocket, wire, wire_size), TURBO_EIO);
    check_equal(probe.nested_send_status, TURBO_EIO);
    check_equal(probe.nested_destroy_status, TURBO_EBUSY);
    check_equal(cnet_websocket_state_get(&websocket, &state), TURBO_OK);
    check_equal(state, CNET_WEBSOCKET_FAILED);
    check_equal(cnet_websocket_last_error(&websocket, &last_error), TURBO_OK);
    check_equal(last_error, TURBO_EIO);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("records terminal transport write errors") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {.write_status = TURBO_EIO};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_SERVER);
    cnet_websocket_state state = CNET_WEBSOCKET_OPEN;
    int last_error = TURBO_OK;

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_send_ping(&websocket, "x", 1u), TURBO_EIO);
    check_equal(cnet_websocket_state_get(&websocket, &state), TURBO_OK);
    check_equal(state, CNET_WEBSOCKET_FAILED);
    check_equal(cnet_websocket_last_error(&websocket, &last_error), TURBO_OK);
    check_equal(last_error, TURBO_EIO);
    check_false(cnet_websocket_has_pending_output(&websocket));
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }

  it("reports abnormal transport close without putting 1006 on the wire") {
    cnet_websocket websocket = {0};
    websocket_probe probe = {0};
    cnet_websocket_config config = websocket_test_config(&probe, CNET_WEBSOCKET_CLIENT);
    cnet_websocket_state state = CNET_WEBSOCKET_OPEN;

    check_equal(cnet_websocket_init(&websocket, &config), TURBO_OK);
    check_equal(cnet_websocket_transport_closed(&websocket), TURBO_OK);
    check_equal(probe.event_count, 1u);
    check_equal(probe.events[0].kind, CNET_WEBSOCKET_EVENT_CLOSE);
    check_equal(probe.events[0].close_code, 1006u);
    check_equal(probe.output_count, 0u);
    check_equal(cnet_websocket_state_get(&websocket, &state), TURBO_OK);
    check_equal(state, CNET_WEBSOCKET_CLOSED);
    check_equal(cnet_websocket_destroy(&websocket), TURBO_OK);
  }
}
