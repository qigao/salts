#if defined(CONSUME_NETWORK_CPP)

  #include <cnet/cnet.h>
  #include <cnet/websocket.h>
  #include <cstring>

struct InstalledWebSocketProbe {
  uint8_t frame[CNET_WEBSOCKET_MAX_HEADER_BYTES + CNET_WEBSOCKET_MIN_FRAME_BYTES]{};
  size_t frame_size{};
  size_t event_count{};
};

static int installed_websocket_write(void *user, const uint8_t *data, size_t size) {
  auto *probe = static_cast<InstalledWebSocketProbe *>(user);
  if (probe == nullptr || data == nullptr || size > sizeof(probe->frame)) return SALTS_ENOSPC;
  std::memcpy(probe->frame, data, size);
  probe->frame_size = size;
  return SALTS_OK;
}

static void installed_websocket_event(void *user, cnet_websocket *,
                                      const cnet_websocket_event *event) {
  auto *probe = static_cast<InstalledWebSocketProbe *>(user);
  if (probe != nullptr && event != nullptr) ++probe->event_count;
}

int main() {
  static const uint8_t inbound_text[] = {0x81u, 0x01u, 'x'};
  cnet_tls_client tls_client{};
  cnet_tls_server tls_server{};
  cnet_tls_client_config tls_client_config{};
  cnet_websocket websocket{};
  InstalledWebSocketProbe probe{};
  cnet_websocket_config websocket_config{};
  if (tls_client.impl != nullptr || tls_server.impl != nullptr || tls_client_config.size != 0u ||
      websocket.impl != nullptr)
    return 1;
  websocket_config.size = sizeof(websocket_config);
  websocket_config.role = CNET_WEBSOCKET_CLIENT;
  websocket_config.max_frame_bytes = CNET_WEBSOCKET_MIN_FRAME_BYTES;
  websocket_config.max_message_bytes = CNET_WEBSOCKET_MIN_FRAME_BYTES;
  websocket_config.max_buffered_input_bytes =
      CNET_WEBSOCKET_MAX_HEADER_BYTES + CNET_WEBSOCKET_MIN_FRAME_BYTES;
  websocket_config.write = installed_websocket_write;
  websocket_config.on_event = installed_websocket_event;
  websocket_config.user = &probe;
  if (cnet_websocket_init(&websocket, &websocket_config) != SALTS_OK) return 2;
  if (cnet_websocket_send_text(&websocket, "x", 1u) != SALTS_OK || probe.frame_size == 0u) {
    (void)cnet_websocket_destroy(&websocket);
    return 3;
  }
  if (cnet_websocket_feed(&websocket, inbound_text, sizeof(inbound_text)) != SALTS_OK ||
      probe.event_count != 1u) {
    (void)cnet_websocket_destroy(&websocket);
    return 4;
  }
  return cnet_websocket_destroy(&websocket) == SALTS_OK ? 0 : 5;
}

#else

  #include <cstl/typed.h>
  #include <salts_uuid.h>

static_assert(sizeof(Salts::UUID) == SALTS_UUID_SIZE);

Struct(InstalledCppPayload, (TYPE(Vec, int), values));

int main() {
  const cmeta_field_desc *field = cmeta_struct_find_field(InstalledCppPayload_meta(), "values");
  InstalledCppPayload payload{};
  const int input = 9;
  int output = 0;
  int status = 0;

  if (field == nullptr || field->declared_type == nullptr) return 1;
  if (cmeta_container_bind_types(&payload.values, field->declared_type) != CMETA_OK) return 2;
  if (vec_init(&payload.values, 1u) != STL_OK) return 3;
  if (vec_push(&payload.values, &input) != STL_OK) {
    status = 4;
    goto cleanup;
  }
  if (vec_pop(&payload.values, &output) != STL_OK || output != input) status = 5;

cleanup:
  vec_destroy(&payload.values);
  return status;
}

#endif
