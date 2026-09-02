#ifndef WEBSOCKET_FRAME_PARSER_H
#define WEBSOCKET_FRAME_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { WS_FRAME_MAX_HEADER_BYTES = 14 };

typedef enum ws_parse_result {
  WS_PARSE_OK = 0,
  WS_PARSE_NEED_MORE,
  WS_PARSE_INVALID_ARGUMENT,
  WS_PARSE_INVALID_OPCODE,
  WS_PARSE_INVALID_RSV,
  WS_PARSE_CONTROL_TOO_LARGE,
  WS_PARSE_FRAGMENTED_CONTROL,
  WS_PARSE_NON_CANONICAL_LENGTH,
  WS_PARSE_INVALID_LENGTH
} ws_parse_result_t;

typedef enum websocket_opcode {
  WS_OPCODE_CONTINUATION = 0x0,
  WS_OPCODE_TEXT = 0x1,
  WS_OPCODE_BINARY = 0x2,
  WS_OPCODE_CLOSE = 0x8,
  WS_OPCODE_PING = 0x9,
  WS_OPCODE_PONG = 0xA
} websocket_opcode_t;

/** Zero-copy frame view; payload is invalidated with the input buffer. */
typedef struct ws_frame {
  uint8_t fin;
  uint8_t opcode;
  uint8_t masked;
  uint8_t masking_key[4];
  uint64_t payload_len;
  const uint8_t *payload;
  size_t header_len;
} ws_frame_t;

static inline int ws_is_control(uint8_t opcode) {
  return opcode == WS_OPCODE_CLOSE || opcode == WS_OPCODE_PING || opcode == WS_OPCODE_PONG;
}

static inline int ws_is_data(uint8_t opcode) {
  return opcode == WS_OPCODE_CONTINUATION || opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY;
}

/** Determine the complete frame size after enough length bytes are present. */
ws_parse_result_t turbo_wsparser_frame_peek_size(const uint8_t *data, size_t len,
                                                 size_t *out_needed);

/** Parse one complete frame into a borrowed view. Failure leaves frame unchanged. */
ws_parse_result_t turbo_wsparser_frame_parse(const uint8_t *data, size_t len, ws_frame_t *frame);

/** Unmask payload in place. NULL/NULL is accepted only for a zero-length payload. */
ws_parse_result_t turbo_wsparser_frame_unmask(uint8_t *payload, size_t len,
                                              const uint8_t masking_key[4]);

size_t turbo_wsparser_frame_header_len(uint64_t payload_len, int masked);

/** Build a checked header; failure leaves out_written unchanged. */
ws_parse_result_t turbo_wsparser_frame_build_header(uint8_t *buffer, size_t capacity,
                                                    uint8_t opcode, uint64_t payload_len, int fin,
                                                    int masked, const uint8_t masking_key[4],
                                                    size_t *out_written);

/* Private source compatibility aliases; emitted symbols keep the project prefix. */
#define ws_frame_peek_size turbo_wsparser_frame_peek_size
#define ws_frame_parse turbo_wsparser_frame_parse
#define ws_frame_unmask turbo_wsparser_frame_unmask
#define ws_frame_header_len turbo_wsparser_frame_header_len
#define ws_frame_build_header turbo_wsparser_frame_build_header

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKET_FRAME_PARSER_H */
