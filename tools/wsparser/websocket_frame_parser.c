#include "websocket_frame_parser.h"

#include <limits.h>
#include <string.h>

typedef struct ws_decoded_header {
  uint8_t fin;
  uint8_t opcode;
  uint8_t masked;
  uint64_t payload_len;
  size_t base_header_len;
  size_t total_size;
} ws_decoded_header;

static int ws_opcode_valid(uint8_t opcode) { return ws_is_data(opcode) || ws_is_control(opcode); }

static ws_parse_result_t ws_frame_decode_header(const uint8_t *data, size_t len,
                                                ws_decoded_header *decoded) {
  ws_decoded_header value = {0};
  uint8_t length_code;

  if (data == NULL || decoded == NULL) return WS_PARSE_INVALID_ARGUMENT;
  if (len < 2u) return WS_PARSE_NEED_MORE;

  value.fin = (uint8_t)((data[0] & 0x80u) != 0u);
  value.opcode = (uint8_t)(data[0] & 0x0fu);
  value.masked = (uint8_t)((data[1] & 0x80u) != 0u);
  if ((data[0] & 0x70u) != 0u) return WS_PARSE_INVALID_RSV;
  if (!ws_opcode_valid(value.opcode)) return WS_PARSE_INVALID_OPCODE;
  if (ws_is_control(value.opcode) && !value.fin) return WS_PARSE_FRAGMENTED_CONTROL;

  length_code = (uint8_t)(data[1] & 0x7fu);
  value.base_header_len = 2u;
  if (length_code <= 125u) {
    value.payload_len = length_code;
  } else if (length_code == 126u) {
    if (len < 4u) return WS_PARSE_NEED_MORE;
    value.payload_len = ((uint64_t)data[2] << 8u) | (uint64_t)data[3];
    value.base_header_len = 4u;
    if (value.payload_len < 126u) return WS_PARSE_NON_CANONICAL_LENGTH;
  } else {
    if (len < 10u) return WS_PARSE_NEED_MORE;
    if ((data[2] & 0x80u) != 0u) return WS_PARSE_INVALID_LENGTH;
    for (size_t index = 0u; index < 8u; ++index)
      value.payload_len = (value.payload_len << 8u) | (uint64_t)data[2u + index];
    value.base_header_len = 10u;
    if (value.payload_len <= UINT16_MAX) return WS_PARSE_NON_CANONICAL_LENGTH;
  }

  if (ws_is_control(value.opcode) && value.payload_len > 125u) return WS_PARSE_CONTROL_TOO_LARGE;
  if (value.masked) value.base_header_len += 4u;
  if (value.payload_len > (uint64_t)(SIZE_MAX - value.base_header_len))
    return WS_PARSE_INVALID_LENGTH;
  value.total_size = value.base_header_len + (size_t)value.payload_len;
  *decoded = value;
  return WS_PARSE_OK;
}

ws_parse_result_t salts_wsparser_frame_peek_size(const uint8_t *data, size_t len,
                                                 size_t *out_needed) {
  ws_decoded_header decoded;
  ws_parse_result_t status;
  if (out_needed == NULL || data == NULL) return WS_PARSE_INVALID_ARGUMENT;
  status = ws_frame_decode_header(data, len, &decoded);
  if (status == WS_PARSE_OK) *out_needed = decoded.total_size;
  return status;
}

ws_parse_result_t salts_wsparser_frame_parse(const uint8_t *data, size_t len, ws_frame_t *frame) {
  ws_decoded_header decoded;
  ws_frame_t parsed = {0};
  ws_parse_result_t status;

  if (data == NULL || frame == NULL) return WS_PARSE_INVALID_ARGUMENT;
  status = ws_frame_decode_header(data, len, &decoded);
  if (status != WS_PARSE_OK) return status;
  if (len < decoded.total_size) return WS_PARSE_NEED_MORE;

  parsed.fin = decoded.fin;
  parsed.opcode = decoded.opcode;
  parsed.masked = decoded.masked;
  parsed.payload_len = decoded.payload_len;
  parsed.header_len = decoded.base_header_len;
  if (decoded.masked)
    memcpy(parsed.masking_key, data + decoded.base_header_len - 4u, sizeof(parsed.masking_key));
  parsed.payload = decoded.payload_len != 0u ? data + decoded.base_header_len : NULL;
  *frame = parsed;
  return WS_PARSE_OK;
}

ws_parse_result_t salts_wsparser_frame_unmask(uint8_t *payload, size_t len,
                                              const uint8_t masking_key[4]) {
  size_t index = 0u;
  uint32_t mask;

  if (len == 0u) return WS_PARSE_OK;
  if (payload == NULL || masking_key == NULL) return WS_PARSE_INVALID_ARGUMENT;
  memcpy(&mask, masking_key, sizeof(mask));
  while (len - index >= sizeof(uint32_t)) {
    uint32_t word;
    memcpy(&word, payload + index, sizeof(word));
    word ^= mask;
    memcpy(payload + index, &word, sizeof(word));
    index += sizeof(uint32_t);
  }
  while (index < len) {
    payload[index] ^= masking_key[index % 4u];
    ++index;
  }
  return WS_PARSE_OK;
}

size_t salts_wsparser_frame_header_len(uint64_t payload_len, int masked) {
  size_t length = 2u;
  if (payload_len > 125u) length += payload_len <= UINT16_MAX ? 2u : 8u;
  if (masked) length += 4u;
  return length;
}

ws_parse_result_t salts_wsparser_frame_build_header(uint8_t *buffer, size_t capacity,
                                                    uint8_t opcode, uint64_t payload_len, int fin,
                                                    int masked, const uint8_t masking_key[4],
                                                    size_t *out_written) {
  uint8_t header[WS_FRAME_MAX_HEADER_BYTES] = {0};
  const size_t header_len = ws_frame_header_len(payload_len, masked);
  size_t offset = 2u;

  if (buffer == NULL || out_written == NULL) return WS_PARSE_INVALID_ARGUMENT;
  if (!ws_opcode_valid(opcode)) return WS_PARSE_INVALID_OPCODE;
  if (ws_is_control(opcode) && !fin) return WS_PARSE_FRAGMENTED_CONTROL;
  if (ws_is_control(opcode) && payload_len > 125u) return WS_PARSE_CONTROL_TOO_LARGE;
  if (payload_len > INT64_MAX) return WS_PARSE_INVALID_LENGTH;
  if (masked && masking_key == NULL) return WS_PARSE_INVALID_ARGUMENT;
  if (capacity < header_len) return WS_PARSE_NEED_MORE;

  header[0] = (uint8_t)((fin ? 0x80u : 0u) | opcode);
  if (payload_len <= 125u) {
    header[1] = (uint8_t)payload_len;
  } else if (payload_len <= UINT16_MAX) {
    header[1] = 126u;
    header[2] = (uint8_t)(payload_len >> 8u);
    header[3] = (uint8_t)payload_len;
    offset = 4u;
  } else {
    header[1] = 127u;
    for (size_t index = 0u; index < 8u; ++index)
      header[2u + index] = (uint8_t)(payload_len >> (56u - index * 8u));
    offset = 10u;
  }
  if (masked) {
    header[1] |= 0x80u;
    memcpy(header + offset, masking_key, 4u);
  }
  memcpy(buffer, header, header_len);
  *out_written = header_len;
  return WS_PARSE_OK;
}
