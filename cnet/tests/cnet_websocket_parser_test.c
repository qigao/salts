#include "websocket_frame_parser.h"

#include "tinytest.h"

#include <stdint.h>
#include <string.h>

spec("WebSocket frame parser") {
  it("builds and parses one borrowed payload without masking") {
    uint8_t wire[32] = {0};
    ws_frame_t frame = {0};
    size_t header_size = 0u;
    size_t needed = 0u;

    check_equal(
        ws_frame_build_header(wire, sizeof(wire), WS_OPCODE_TEXT, 2u, 1, 0, NULL, &header_size),
        WS_PARSE_OK);
    check_equal(header_size, 2u);
    memcpy(wire + header_size, "hi", 2u);
    check_equal(ws_frame_peek_size(wire, header_size + 2u, &needed), WS_PARSE_OK);
    check_equal(needed, header_size + 2u);
    check_equal(ws_frame_parse(wire, header_size + 2u, &frame), WS_PARSE_OK);
    check_equal(frame.fin, 1u);
    check_equal(frame.opcode, WS_OPCODE_TEXT);
    check_equal(frame.masked, 0u);
    check_equal(frame.payload_len, 2u);
    check_equal(frame.payload, "hi", 2u);
    check_true(frame.payload == wire + header_size);
  }

  it("round trips a masked binary frame") {
    static const uint8_t key[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    static const uint8_t payload[5] = {1u, 2u, 3u, 4u, 5u};
    uint8_t wire[32] = {0};
    ws_frame_t frame = {0};
    size_t header_size = 0u;

    check_equal(ws_frame_build_header(wire, sizeof(wire), WS_OPCODE_BINARY, sizeof(payload), 1, 1,
                                      key, &header_size),
                WS_PARSE_OK);
    memcpy(wire + header_size, payload, sizeof(payload));
    for (size_t index = 0u; index < sizeof(payload); ++index)
      wire[header_size + index] ^= key[index % 4u];
    check_equal(ws_frame_parse(wire, header_size + sizeof(payload), &frame), WS_PARSE_OK);
    check_equal(frame.masking_key, key, sizeof(key));
    check_equal(
        ws_frame_unmask((uint8_t *)frame.payload, (size_t)frame.payload_len, frame.masking_key),
        WS_PARSE_OK);
    check_equal(frame.payload, payload, sizeof(payload));
  }

  it("builds and parses a canonical 64 bit payload length") {
    enum { PAYLOAD_BYTES = 65536 };
    static uint8_t wire[PAYLOAD_BYTES + WS_FRAME_MAX_HEADER_BYTES];
    ws_frame_t frame = {0};
    size_t header_size = 0u;
    size_t needed = 0u;

    check_equal(ws_frame_build_header(wire, sizeof(wire), WS_OPCODE_BINARY, PAYLOAD_BYTES, 1, 0,
                                      NULL, &header_size),
                WS_PARSE_OK);
    check_equal(header_size, 10u);
    check_equal(ws_frame_peek_size(wire, sizeof(wire), &needed), WS_PARSE_OK);
    check_equal(needed, (size_t)PAYLOAD_BYTES + header_size);
    check_equal(ws_frame_parse(wire, needed, &frame), WS_PARSE_OK);
    check_equal(frame.payload_len, (uint64_t)PAYLOAD_BYTES);
    check_true(frame.payload == wire + header_size);
  }

  it("distinguishes incomplete input from invalid arguments") {
    static const uint8_t one_byte[1] = {0x81u};
    static const uint8_t zero_key[4] = {0};
    uint8_t one_payload[1] = {0};
    ws_frame_t frame = {.opcode = 0xffu};
    size_t needed = 91u;

    check_equal(ws_frame_peek_size(one_byte, sizeof(one_byte), &needed), WS_PARSE_NEED_MORE);
    check_equal(needed, 91u);
    check_equal(ws_frame_parse(one_byte, sizeof(one_byte), &frame), WS_PARSE_NEED_MORE);
    check_equal(frame.opcode, 0xffu);
    check_equal(ws_frame_peek_size(NULL, 1u, &needed), WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_peek_size(one_byte, sizeof(one_byte), NULL), WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_parse(NULL, 1u, &frame), WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_parse(one_byte, sizeof(one_byte), NULL), WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_unmask(NULL, 1u, zero_key), WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_unmask(one_payload, 1u, NULL), WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_unmask(NULL, 0u, NULL), WS_PARSE_OK);
  }

  it("rejects reserved opcode and RSV bits before payload admission") {
    static const uint8_t reserved_data[2] = {0x83u, 0u};
    static const uint8_t reserved_control[2] = {0x8bu, 0u};
    static const uint8_t rsv[2] = {0xc1u, 0u};
    ws_frame_t frame = {0};
    size_t needed = 0u;

    check_equal(ws_frame_parse(reserved_data, sizeof(reserved_data), &frame),
                WS_PARSE_INVALID_OPCODE);
    check_equal(ws_frame_peek_size(reserved_control, sizeof(reserved_control), &needed),
                WS_PARSE_INVALID_OPCODE);
    check_equal(ws_frame_parse(rsv, sizeof(rsv), &frame), WS_PARSE_INVALID_RSV);
  }

  it("rejects non canonical and invalid 64 bit lengths") {
    static const uint8_t noncanonical_16[4] = {0x82u, 126u, 0u, 125u};
    static const uint8_t noncanonical_64[10] = {0x82u, 127u, 0u, 0u, 0u, 0u, 0u, 0u, 0xffu, 0xffu};
    static const uint8_t high_bit[10] = {0x82u, 127u, 0x80u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    ws_frame_t frame = {0};

    check_equal(ws_frame_parse(noncanonical_16, sizeof(noncanonical_16), &frame),
                WS_PARSE_NON_CANONICAL_LENGTH);
    check_equal(ws_frame_parse(noncanonical_64, sizeof(noncanonical_64), &frame),
                WS_PARSE_NON_CANONICAL_LENGTH);
    check_equal(ws_frame_parse(high_bit, sizeof(high_bit), &frame), WS_PARSE_INVALID_LENGTH);
  }

  it("rejects fragmented and oversized control frames") {
    static const uint8_t fragmented_ping[2] = {0x09u, 0u};
    static const uint8_t oversized_ping[4] = {0x89u, 126u, 0u, 126u};
    ws_frame_t frame = {0};

    check_equal(ws_frame_parse(fragmented_ping, sizeof(fragmented_ping), &frame),
                WS_PARSE_FRAGMENTED_CONTROL);
    check_equal(ws_frame_parse(oversized_ping, sizeof(oversized_ping), &frame),
                WS_PARSE_CONTROL_TOO_LARGE);
  }

  it("checks frame construction arguments and output capacity") {
    uint8_t header[14] = {0};
    size_t written = 77u;

    check_equal(
        ws_frame_build_header(NULL, sizeof(header), WS_OPCODE_TEXT, 0u, 1, 0, NULL, &written),
        WS_PARSE_INVALID_ARGUMENT);
    check_equal(ws_frame_build_header(header, 1u, WS_OPCODE_TEXT, 0u, 1, 0, NULL, &written),
                WS_PARSE_NEED_MORE);
    check_equal(ws_frame_build_header(header, sizeof(header), 0x03u, 0u, 1, 0, NULL, &written),
                WS_PARSE_INVALID_OPCODE);
    check_equal(
        ws_frame_build_header(header, sizeof(header), WS_OPCODE_PING, 126u, 1, 0, NULL, &written),
        WS_PARSE_CONTROL_TOO_LARGE);
    check_equal(
        ws_frame_build_header(header, sizeof(header), WS_OPCODE_PING, 0u, 0, 0, NULL, &written),
        WS_PARSE_FRAGMENTED_CONTROL);
    check_equal(
        ws_frame_build_header(header, sizeof(header), WS_OPCODE_TEXT, 0u, 1, 1, NULL, &written),
        WS_PARSE_INVALID_ARGUMENT);
  }
}
