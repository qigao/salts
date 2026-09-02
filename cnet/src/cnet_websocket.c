#include <cnet/websocket.h>

#include "websocket_frame_parser.h"

#include <turbo/error_codes.h>
#include <turbo/random.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_websocket_utf8_state {
  uint8_t remaining;
  uint8_t next_min;
  uint8_t next_max;
} cnet_websocket_utf8_state;

typedef struct cnet_websocket_impl {
  cnet_websocket *public_value;
  cnet_websocket_role role;
  cnet_websocket_state state;
  int last_error;
  size_t max_frame_bytes;
  size_t max_message_bytes;
  cnet_websocket_write_fn write;
  cnet_websocket_event_fn on_event;
  void *user;

  uint8_t *input;
  size_t input_capacity;
  size_t input_start;
  size_t input_size;

  uint8_t *message;
  size_t message_size;
  cnet_websocket_message_type inbound_message_type;

  uint8_t *output;
  size_t output_capacity;
  size_t output_size;

  cnet_websocket_message_type outbound_message_type;
  size_t outbound_message_size;
  cnet_websocket_utf8_state outbound_utf8;
  bool sent_close;
  bool received_close;
  bool close_event_delivered;
  bool operation_active;
  bool callback_active;
  bool write_active;
} cnet_websocket_impl;

static cnet_websocket_impl *cnet_websocket_get(cnet_websocket *websocket) {
  return websocket != NULL ? (cnet_websocket_impl *)websocket->impl : NULL;
}

static const cnet_websocket_impl *cnet_websocket_const_get(const cnet_websocket *websocket) {
  return websocket != NULL ? (const cnet_websocket_impl *)websocket->impl : NULL;
}

static bool cnet_websocket_utf8_update(cnet_websocket_utf8_state *state, const uint8_t *data,
                                       size_t size, bool final_chunk) {
  size_t index = 0u;

  if (state == NULL || (data == NULL && size != 0u)) return false;
  while (index < size) {
    const uint8_t first = data[index];

    if (state->remaining != 0u) {
      if (first < state->next_min || first > state->next_max) return false;
      --state->remaining;
      state->next_min = 0x80u;
      state->next_max = 0xbfu;
      ++index;
      continue;
    }
    if (first < 0x80u) {
      ++index;
      continue;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
      state->remaining = 1u;
      state->next_min = 0x80u;
      state->next_max = 0xbfu;
    } else if (first == 0xe0u) {
      state->remaining = 2u;
      state->next_min = 0xa0u;
      state->next_max = 0xbfu;
    } else if ((first >= 0xe1u && first <= 0xecu) || (first >= 0xeeu && first <= 0xefu)) {
      state->remaining = 2u;
      state->next_min = 0x80u;
      state->next_max = 0xbfu;
    } else if (first == 0xedu) {
      state->remaining = 2u;
      state->next_min = 0x80u;
      state->next_max = 0x9fu;
    } else if (first == 0xf0u) {
      state->remaining = 3u;
      state->next_min = 0x90u;
      state->next_max = 0xbfu;
    } else if (first >= 0xf1u && first <= 0xf3u) {
      state->remaining = 3u;
      state->next_min = 0x80u;
      state->next_max = 0xbfu;
    } else if (first == 0xf4u) {
      state->remaining = 3u;
      state->next_min = 0x80u;
      state->next_max = 0x8fu;
    } else {
      return false;
    }
    ++index;
  }
  return !final_chunk || state->remaining == 0u;
}

static bool cnet_websocket_utf8_valid(const uint8_t *data, size_t size) {
  cnet_websocket_utf8_state state = {0};
  return cnet_websocket_utf8_update(&state, data, size, true);
}

static bool cnet_websocket_close_code_valid(uint16_t code) {
  if (code >= 3000u && code <= 4999u) return true;
  if (code < 1000u || code > 1014u) return false;
  return code != 1004u && code != 1005u && code != 1006u;
}

static void cnet_websocket_record_error(cnet_websocket_impl *impl, int status) {
  if (impl->last_error == TURBO_OK && status != TURBO_OK) impl->last_error = status;
}

static void cnet_websocket_consume(cnet_websocket_impl *impl, size_t size) {
  impl->input_start += size;
  impl->input_size -= size;
  if (impl->input_size == 0u) impl->input_start = 0u;
}

static void cnet_websocket_emit_event(cnet_websocket_impl *impl, cnet_websocket_event_kind kind,
                                      cnet_websocket_message_type message_type, const uint8_t *data,
                                      size_t size, uint16_t close_code) {
  cnet_websocket_event event = {
      .kind = kind,
      .message_type = message_type,
      .data = data,
      .size = size,
      .close_code = close_code,
  };
  impl->callback_active = true;
  impl->on_event(impl->user, impl->public_value, &event);
  impl->callback_active = false;
}

static int cnet_websocket_write_output(cnet_websocket_impl *impl) {
  int status;
  if (impl->output_size == 0u) return TURBO_OK;
  impl->write_active = true;
  status = impl->write(impl->user, impl->output, impl->output_size);
  impl->write_active = false;
  if (status == TURBO_OK) impl->output_size = 0u;
  else if (status != TURBO_EBUSY) {
    impl->output_size = 0u;
    cnet_websocket_record_error(impl, status);
    impl->state = CNET_WEBSOCKET_FAILED;
  }
  return status;
}

static int cnet_websocket_emit_frame(cnet_websocket_impl *impl, uint8_t opcode, bool final_frame,
                                     const uint8_t *payload, size_t payload_size) {
  uint8_t masking_key[4] = {0};
  size_t header_size = 0u;
  int masked = impl->role == CNET_WEBSOCKET_CLIENT;
  int status;

  if (impl->output_size != 0u) return TURBO_EBUSY;
  if (payload_size > impl->max_frame_bytes) return TURBO_EMSGSIZE;
  if (payload == NULL && payload_size != 0u) return TURBO_EINVAL;
  if (masked) {
    status = turbo_platform_secure_random(masking_key, sizeof(masking_key));
    if (status != TURBO_OK) {
      cnet_websocket_record_error(impl, status);
      impl->state = CNET_WEBSOCKET_FAILED;
      return status;
    }
  }
  if (ws_frame_build_header(impl->output, impl->output_capacity, opcode, payload_size,
                            final_frame ? 1 : 0, masked, masked ? masking_key : NULL,
                            &header_size) != WS_PARSE_OK)
    return TURBO_EPROTO;
  if (payload_size != 0u) memcpy(impl->output + header_size, payload, payload_size);
  if (masked) {
    status = ws_frame_unmask(impl->output + header_size, payload_size, masking_key);
    if (status != WS_PARSE_OK) return TURBO_EPROTO;
  }
  impl->output_size = header_size + payload_size;
  status = cnet_websocket_write_output(impl);
  return status == TURBO_EBUSY ? TURBO_OK : status;
}

static int cnet_websocket_fail(cnet_websocket_impl *impl, int status, uint16_t close_code) {
  uint8_t payload[2];

  cnet_websocket_record_error(impl, status);
  impl->state = CNET_WEBSOCKET_FAILED;
  impl->input_start = 0u;
  impl->input_size = 0u;
  impl->message_size = 0u;
  impl->inbound_message_type = CNET_WEBSOCKET_MESSAGE_NONE;
  if (!impl->sent_close && impl->output_size == 0u) {
    payload[0] = (uint8_t)(close_code >> 8u);
    payload[1] = (uint8_t)close_code;
    if (cnet_websocket_emit_frame(impl, WS_OPCODE_CLOSE, true, payload, sizeof(payload)) ==
        TURBO_OK)
      impl->sent_close = true;
  }
  impl->state = CNET_WEBSOCKET_FAILED;
  return status;
}

static int cnet_websocket_append_message(cnet_websocket_impl *impl, const uint8_t *data,
                                         size_t size) {
  if (size > impl->max_message_bytes - impl->message_size) return TURBO_EMSGSIZE;
  if (size != 0u) memcpy(impl->message + impl->message_size, data, size);
  impl->message_size += size;
  return TURBO_OK;
}

static int cnet_websocket_deliver_data(cnet_websocket_impl *impl, const ws_frame_t *frame,
                                       size_t wire_size) {
  const cnet_websocket_message_type frame_type =
      frame->opcode == WS_OPCODE_TEXT ? CNET_WEBSOCKET_MESSAGE_TEXT : CNET_WEBSOCKET_MESSAGE_BINARY;
  int status;

  if (frame->opcode == WS_OPCODE_CONTINUATION) {
    if (impl->inbound_message_type == CNET_WEBSOCKET_MESSAGE_NONE)
      return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
    status = cnet_websocket_append_message(impl, frame->payload, (size_t)frame->payload_len);
    if (status != TURBO_OK)
      return cnet_websocket_fail(impl, status, CNET_WEBSOCKET_CLOSE_MESSAGE_TOO_BIG);
    cnet_websocket_consume(impl, wire_size);
    if (!frame->fin) return TURBO_OK;
    if (impl->inbound_message_type == CNET_WEBSOCKET_MESSAGE_TEXT &&
        !cnet_websocket_utf8_valid(impl->message, impl->message_size))
      return cnet_websocket_fail(impl, TURBO_ECHARSET, CNET_WEBSOCKET_CLOSE_INVALID_TEXT);
    cnet_websocket_emit_event(impl, CNET_WEBSOCKET_EVENT_MESSAGE, impl->inbound_message_type,
                              impl->message, impl->message_size, 0u);
    impl->message_size = 0u;
    impl->inbound_message_type = CNET_WEBSOCKET_MESSAGE_NONE;
    return TURBO_OK;
  }

  if (impl->inbound_message_type != CNET_WEBSOCKET_MESSAGE_NONE)
    return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
  if (!frame->fin) {
    status = cnet_websocket_append_message(impl, frame->payload, (size_t)frame->payload_len);
    if (status != TURBO_OK)
      return cnet_websocket_fail(impl, status, CNET_WEBSOCKET_CLOSE_MESSAGE_TOO_BIG);
    impl->inbound_message_type = frame_type;
    cnet_websocket_consume(impl, wire_size);
    return TURBO_OK;
  }

  if (frame_type == CNET_WEBSOCKET_MESSAGE_TEXT &&
      !cnet_websocket_utf8_valid(frame->payload, (size_t)frame->payload_len))
    return cnet_websocket_fail(impl, TURBO_ECHARSET, CNET_WEBSOCKET_CLOSE_INVALID_TEXT);
  cnet_websocket_consume(impl, wire_size);
  cnet_websocket_emit_event(impl, CNET_WEBSOCKET_EVENT_MESSAGE, frame_type, frame->payload,
                            (size_t)frame->payload_len, 0u);
  return TURBO_OK;
}

static int cnet_websocket_deliver_close(cnet_websocket_impl *impl, const ws_frame_t *frame,
                                        size_t wire_size) {
  uint16_t code = 0u;
  const uint8_t *reason = NULL;
  size_t reason_size = 0u;
  int status = TURBO_OK;

  if (frame->payload_len == 1u)
    return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
  if (frame->payload_len >= 2u) {
    code = (uint16_t)(((uint16_t)frame->payload[0] << 8u) | frame->payload[1]);
    reason = frame->payload + 2u;
    reason_size = (size_t)frame->payload_len - 2u;
    if (!cnet_websocket_close_code_valid(code))
      return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
    if (!cnet_websocket_utf8_valid(reason, reason_size))
      return cnet_websocket_fail(impl, TURBO_ECHARSET, CNET_WEBSOCKET_CLOSE_INVALID_TEXT);
  }

  cnet_websocket_consume(impl, wire_size);
  impl->received_close = true;
  impl->state = CNET_WEBSOCKET_CLOSING;
  if (!impl->sent_close) {
    status = cnet_websocket_emit_frame(impl, WS_OPCODE_CLOSE, true, frame->payload,
                                       (size_t)frame->payload_len);
    if (status == TURBO_OK) impl->sent_close = true;
  }
  if (!impl->close_event_delivered) {
    impl->close_event_delivered = true;
    cnet_websocket_emit_event(impl, CNET_WEBSOCKET_EVENT_CLOSE, CNET_WEBSOCKET_MESSAGE_NONE, reason,
                              reason_size, code);
  }
  if (status == TURBO_OK && impl->sent_close)
    impl->state = impl->output_size == 0u ? CNET_WEBSOCKET_CLOSED : CNET_WEBSOCKET_CLOSING;
  return status;
}

static int cnet_websocket_process_input(cnet_websocket_impl *impl) {
  while (impl->input_size != 0u && impl->output_size == 0u) {
    uint8_t *wire = impl->input + impl->input_start;
    ws_frame_t frame = {0};
    size_t needed = 0u;
    size_t header_size;
    size_t payload_size;
    ws_parse_result_t parse_status = ws_frame_peek_size(wire, impl->input_size, &needed);
    int status;

    if (parse_status == WS_PARSE_NEED_MORE) return TURBO_OK;
    if (parse_status != WS_PARSE_OK)
      return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
    header_size = 2u +
                  ((wire[1] & 0x7fu) == 126u   ? 2u
                   : (wire[1] & 0x7fu) == 127u ? 8u
                                               : 0u) +
                  ((wire[1] & 0x80u) != 0u ? 4u : 0u);
    payload_size = needed - header_size;
    if (payload_size > impl->max_frame_bytes)
      return cnet_websocket_fail(impl, TURBO_EMSGSIZE, CNET_WEBSOCKET_CLOSE_MESSAGE_TOO_BIG);
    if (needed > impl->input_size) return TURBO_OK;
    parse_status = ws_frame_parse(wire, impl->input_size, &frame);
    if (parse_status != WS_PARSE_OK)
      return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
    if ((impl->role == CNET_WEBSOCKET_SERVER && !frame.masked) ||
        (impl->role == CNET_WEBSOCKET_CLIENT && frame.masked))
      return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);
    if (frame.masked && ws_frame_unmask((uint8_t *)frame.payload, (size_t)frame.payload_len,
                                        frame.masking_key) != WS_PARSE_OK)
      return cnet_websocket_fail(impl, TURBO_EPROTO, CNET_WEBSOCKET_CLOSE_PROTOCOL_ERROR);

    if (impl->state == CNET_WEBSOCKET_CLOSING && frame.opcode != WS_OPCODE_CLOSE) {
      cnet_websocket_consume(impl, needed);
      continue;
    }
    if (frame.opcode == WS_OPCODE_CLOSE)
      status = cnet_websocket_deliver_close(impl, &frame, needed);
    else if (frame.opcode == WS_OPCODE_PING) {
      status = cnet_websocket_emit_frame(impl, WS_OPCODE_PONG, true, frame.payload,
                                         (size_t)frame.payload_len);
      cnet_websocket_consume(impl, needed);
      cnet_websocket_emit_event(impl, CNET_WEBSOCKET_EVENT_PING, CNET_WEBSOCKET_MESSAGE_NONE,
                                frame.payload, (size_t)frame.payload_len, 0u);
    } else if (frame.opcode == WS_OPCODE_PONG) {
      cnet_websocket_consume(impl, needed);
      cnet_websocket_emit_event(impl, CNET_WEBSOCKET_EVENT_PONG, CNET_WEBSOCKET_MESSAGE_NONE,
                                frame.payload, (size_t)frame.payload_len, 0u);
      status = TURBO_OK;
    } else {
      status = cnet_websocket_deliver_data(impl, &frame, needed);
    }
    if (status != TURBO_OK) return status;
    if (impl->state == CNET_WEBSOCKET_FAILED) return impl->last_error;
    if (impl->state == CNET_WEBSOCKET_CLOSED) return TURBO_OK;
  }
  return TURBO_OK;
}

int cnet_websocket_init(cnet_websocket *websocket, const cnet_websocket_config *config) {
  cnet_websocket_impl *impl;
  size_t output_capacity;
  size_t total_capacity;

  if (websocket == NULL || config == NULL || websocket->impl != NULL ||
      config->size != sizeof(*config) ||
      (config->role != CNET_WEBSOCKET_CLIENT && config->role != CNET_WEBSOCKET_SERVER) ||
      config->max_frame_bytes < CNET_WEBSOCKET_MIN_FRAME_BYTES ||
      config->max_message_bytes < config->max_frame_bytes ||
      config->max_frame_bytes > (size_t)INT64_MAX ||
      config->max_frame_bytes > SIZE_MAX - CNET_WEBSOCKET_MAX_HEADER_BYTES ||
      config->write == NULL || config->on_event == NULL)
    return TURBO_EINVAL;
  output_capacity = config->max_frame_bytes + CNET_WEBSOCKET_MAX_HEADER_BYTES;
  if (config->max_buffered_input_bytes < output_capacity) return TURBO_EINVAL;
  if (config->max_buffered_input_bytes > SIZE_MAX - config->max_message_bytes ||
      config->max_buffered_input_bytes + config->max_message_bytes > SIZE_MAX - output_capacity)
    return TURBO_ERANGE;
  total_capacity = config->max_buffered_input_bytes + config->max_message_bytes + output_capacity;
  if (total_capacity == 0u) return TURBO_ERANGE;

  impl = (cnet_websocket_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->input = (uint8_t *)malloc(config->max_buffered_input_bytes);
  impl->message = (uint8_t *)malloc(config->max_message_bytes);
  impl->output = (uint8_t *)malloc(output_capacity);
  if (impl->input == NULL || impl->message == NULL || impl->output == NULL) {
    free(impl->output);
    free(impl->message);
    free(impl->input);
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->public_value = websocket;
  impl->role = config->role;
  impl->state = CNET_WEBSOCKET_OPEN;
  impl->max_frame_bytes = config->max_frame_bytes;
  impl->max_message_bytes = config->max_message_bytes;
  impl->input_capacity = config->max_buffered_input_bytes;
  impl->output_capacity = output_capacity;
  impl->write = config->write;
  impl->on_event = config->on_event;
  impl->user = config->user;
  websocket->impl = impl;
  return TURBO_OK;
}

int cnet_websocket_destroy(cnet_websocket *websocket) {
  cnet_websocket_impl *impl;
  if (websocket == NULL) return TURBO_EINVAL;
  impl = cnet_websocket_get(websocket);
  if (impl == NULL) return TURBO_OK;
  if (impl->operation_active || impl->callback_active || impl->write_active) return TURBO_EBUSY;
  free(impl->output);
  free(impl->message);
  free(impl->input);
  free(impl);
  websocket->impl = NULL;
  return TURBO_OK;
}

int cnet_websocket_state_get(const cnet_websocket *websocket, cnet_websocket_state *out_state) {
  const cnet_websocket_impl *impl = cnet_websocket_const_get(websocket);
  if (impl == NULL || out_state == NULL) return TURBO_EINVAL;
  *out_state = impl->state;
  return TURBO_OK;
}

int cnet_websocket_last_error(const cnet_websocket *websocket, int *out_status) {
  const cnet_websocket_impl *impl = cnet_websocket_const_get(websocket);
  if (impl == NULL || out_status == NULL) return TURBO_EINVAL;
  *out_status = impl->last_error;
  return TURBO_OK;
}

bool cnet_websocket_has_pending_output(const cnet_websocket *websocket) {
  const cnet_websocket_impl *impl = cnet_websocket_const_get(websocket);
  return impl != NULL && impl->output_size != 0u;
}

int cnet_websocket_feed(cnet_websocket *websocket, const void *data, size_t size) {
  cnet_websocket_impl *impl = cnet_websocket_get(websocket);
  int status;

  if (impl == NULL || (data == NULL && size != 0u)) return TURBO_EINVAL;
  if (impl->operation_active || impl->callback_active || impl->write_active) return TURBO_EBUSY;
  if (impl->state == CNET_WEBSOCKET_FAILED || impl->state == CNET_WEBSOCKET_CLOSED)
    return TURBO_ESHUTDOWN;
  if (impl->output_size != 0u) return TURBO_EBUSY;
  if (size > impl->input_capacity - impl->input_size) return TURBO_ENOSPC;

  if (size > impl->input_capacity - (impl->input_start + impl->input_size)) {
    if (impl->input_size != 0u)
      memmove(impl->input, impl->input + impl->input_start, impl->input_size);
    impl->input_start = 0u;
  }
  if (size != 0u) memcpy(impl->input + impl->input_start + impl->input_size, data, size);
  impl->input_size += size;
  impl->operation_active = true;
  status = cnet_websocket_process_input(impl);
  impl->operation_active = false;
  return status;
}

int cnet_websocket_flush(cnet_websocket *websocket) {
  cnet_websocket_impl *impl = cnet_websocket_get(websocket);
  int status;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->operation_active || impl->callback_active || impl->write_active) return TURBO_EBUSY;
  impl->operation_active = true;
  status = cnet_websocket_write_output(impl);
  if (status == TURBO_OK && impl->output_size == 0u && impl->sent_close && impl->received_close &&
      impl->state == CNET_WEBSOCKET_CLOSING)
    impl->state = CNET_WEBSOCKET_CLOSED;
  if (status == TURBO_OK && impl->state != CNET_WEBSOCKET_FAILED &&
      impl->state != CNET_WEBSOCKET_CLOSED)
    status = cnet_websocket_process_input(impl);
  impl->operation_active = false;
  return status;
}

static int cnet_websocket_send_frame(cnet_websocket_impl *impl, uint8_t opcode, const void *data,
                                     size_t size) {
  if (size > CNET_WEBSOCKET_MAX_CONTROL_BYTES || size > impl->max_frame_bytes)
    return TURBO_EMSGSIZE;
  if (data == NULL && size != 0u) return TURBO_EINVAL;
  return cnet_websocket_emit_frame(impl, opcode, true, (const uint8_t *)data, size);
}

int cnet_websocket_send_fragment(cnet_websocket *websocket,
                                 cnet_websocket_message_type message_type, const void *data,
                                 size_t size, bool final_fragment) {
  cnet_websocket_impl *impl = cnet_websocket_get(websocket);
  cnet_websocket_message_type prior_type;
  cnet_websocket_utf8_state next_utf8;
  size_t prior_size;
  uint8_t opcode;
  int status;
  bool nested;

  if (impl == NULL ||
      (message_type != CNET_WEBSOCKET_MESSAGE_TEXT &&
       message_type != CNET_WEBSOCKET_MESSAGE_BINARY) ||
      (data == NULL && size != 0u))
    return TURBO_EINVAL;
  if (impl->write_active || (impl->operation_active && !impl->callback_active)) return TURBO_EBUSY;
  if (impl->state != CNET_WEBSOCKET_OPEN) return TURBO_ESHUTDOWN;
  if (impl->output_size != 0u) return TURBO_EBUSY;
  if (size > impl->max_frame_bytes) return TURBO_EMSGSIZE;
  if (impl->outbound_message_type != CNET_WEBSOCKET_MESSAGE_NONE &&
      impl->outbound_message_type != message_type)
    return TURBO_EPROTO;
  if (size > impl->max_message_bytes - impl->outbound_message_size) return TURBO_EMSGSIZE;

  prior_type = impl->outbound_message_type;
  prior_size = impl->outbound_message_size;
  next_utf8 = impl->outbound_utf8;
  if (message_type == CNET_WEBSOCKET_MESSAGE_TEXT) {
    if (prior_type == CNET_WEBSOCKET_MESSAGE_NONE) memset(&next_utf8, 0, sizeof(next_utf8));
    if (!cnet_websocket_utf8_update(&next_utf8, (const uint8_t *)data, size, final_fragment))
      return TURBO_ECHARSET;
  }
  opcode = prior_type == CNET_WEBSOCKET_MESSAGE_NONE
               ? (message_type == CNET_WEBSOCKET_MESSAGE_TEXT ? WS_OPCODE_TEXT : WS_OPCODE_BINARY)
               : WS_OPCODE_CONTINUATION;
  nested = impl->callback_active;
  if (!nested) impl->operation_active = true;
  status = cnet_websocket_emit_frame(impl, opcode, final_fragment, (const uint8_t *)data, size);
  if (status == TURBO_OK) {
    if (final_fragment) {
      impl->outbound_message_type = CNET_WEBSOCKET_MESSAGE_NONE;
      impl->outbound_message_size = 0u;
      memset(&impl->outbound_utf8, 0, sizeof(impl->outbound_utf8));
    } else {
      impl->outbound_message_type = message_type;
      impl->outbound_message_size = prior_size + size;
      if (message_type == CNET_WEBSOCKET_MESSAGE_TEXT) impl->outbound_utf8 = next_utf8;
    }
  }
  if (!nested) impl->operation_active = false;
  return status;
}

int cnet_websocket_send_text(cnet_websocket *websocket, const void *data, size_t size) {
  return cnet_websocket_send_fragment(websocket, CNET_WEBSOCKET_MESSAGE_TEXT, data, size, true);
}

int cnet_websocket_send_binary(cnet_websocket *websocket, const void *data, size_t size) {
  return cnet_websocket_send_fragment(websocket, CNET_WEBSOCKET_MESSAGE_BINARY, data, size, true);
}

static int cnet_websocket_send_control(cnet_websocket *websocket, uint8_t opcode, const void *data,
                                       size_t size) {
  cnet_websocket_impl *impl = cnet_websocket_get(websocket);
  int status;
  bool nested;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->write_active || (impl->operation_active && !impl->callback_active)) return TURBO_EBUSY;
  if (impl->state != CNET_WEBSOCKET_OPEN) return TURBO_ESHUTDOWN;
  if (impl->output_size != 0u) return TURBO_EBUSY;
  nested = impl->callback_active;
  if (!nested) impl->operation_active = true;
  status = cnet_websocket_send_frame(impl, opcode, data, size);
  if (!nested) impl->operation_active = false;
  return status;
}

int cnet_websocket_send_ping(cnet_websocket *websocket, const void *data, size_t size) {
  return cnet_websocket_send_control(websocket, WS_OPCODE_PING, data, size);
}

int cnet_websocket_send_pong(cnet_websocket *websocket, const void *data, size_t size) {
  return cnet_websocket_send_control(websocket, WS_OPCODE_PONG, data, size);
}

int cnet_websocket_close(cnet_websocket *websocket, uint16_t code, const void *reason,
                         size_t reason_size) {
  cnet_websocket_impl *impl = cnet_websocket_get(websocket);
  uint8_t payload[CNET_WEBSOCKET_MAX_CONTROL_BYTES];
  size_t payload_size = 0u;
  int status;
  bool nested;

  if (impl == NULL || (reason == NULL && reason_size != 0u)) return TURBO_EINVAL;
  if ((code == 0u && reason_size != 0u) || (code != 0u && !cnet_websocket_close_code_valid(code)))
    return TURBO_EINVAL;
  if (reason_size > CNET_WEBSOCKET_MAX_CONTROL_BYTES - 2u) return TURBO_EMSGSIZE;
  if (!cnet_websocket_utf8_valid((const uint8_t *)reason, reason_size)) return TURBO_ECHARSET;
  if (impl->write_active || (impl->operation_active && !impl->callback_active)) return TURBO_EBUSY;
  if (impl->state == CNET_WEBSOCKET_CLOSING) return TURBO_EALREADY;
  if (impl->state != CNET_WEBSOCKET_OPEN) return TURBO_ESHUTDOWN;
  if (impl->output_size != 0u) return TURBO_EBUSY;

  if (code != 0u) {
    payload[0] = (uint8_t)(code >> 8u);
    payload[1] = (uint8_t)code;
    payload_size = 2u;
    if (reason_size != 0u) memcpy(payload + payload_size, reason, reason_size);
    payload_size += reason_size;
  }
  nested = impl->callback_active;
  if (!nested) impl->operation_active = true;
  status = cnet_websocket_emit_frame(impl, WS_OPCODE_CLOSE, true, payload, payload_size);
  if (status == TURBO_OK) {
    impl->sent_close = true;
    impl->state = impl->received_close ? CNET_WEBSOCKET_CLOSED : CNET_WEBSOCKET_CLOSING;
  }
  if (!nested) impl->operation_active = false;
  return status;
}

int cnet_websocket_transport_closed(cnet_websocket *websocket) {
  cnet_websocket_impl *impl = cnet_websocket_get(websocket);
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->operation_active || impl->callback_active || impl->write_active) return TURBO_EBUSY;
  if (impl->state == CNET_WEBSOCKET_CLOSED) return TURBO_EALREADY;
  impl->operation_active = true;
  impl->output_size = 0u;
  impl->input_start = 0u;
  impl->input_size = 0u;
  impl->message_size = 0u;
  if (impl->state != CNET_WEBSOCKET_FAILED) {
    impl->state = CNET_WEBSOCKET_CLOSED;
    if (!impl->close_event_delivered) {
      impl->close_event_delivered = true;
      cnet_websocket_emit_event(impl, CNET_WEBSOCKET_EVENT_CLOSE, CNET_WEBSOCKET_MESSAGE_NONE, NULL,
                                0u, CNET_WEBSOCKET_CLOSE_ABNORMAL);
    }
  }
  impl->operation_active = false;
  return TURBO_OK;
}
