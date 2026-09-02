#include "chttp_internal.h"
#include "chttp_tls.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct chttp_client_impl chttp_client_impl;

typedef enum chttp_slot_state {
  CHTTP_SLOT_FREE = 0,
  CHTTP_SLOT_CONNECTING,
  CHTTP_SLOT_BUSY,
  CHTTP_SLOT_IDLE,
  CHTTP_SLOT_CLOSING,
  CHTTP_SLOT_TERMINAL
} chttp_slot_state;

typedef struct chttp_slot {
  chttp_client_impl *client;
  chttp_request public_handle;
  cnet_connection connection;
  chttp_response_parser response_parser;
  unsigned char *request_data;
  char *connection_uri;
  char *authority;
  chttp_tls_profile_impl *tls_profile;
  size_t request_size;
  chttp_complete_fn on_complete;
  void *user;
  uint32_t generation;
  chttp_slot_state state;
  bool result_delivered;
  bool cancel_requested;
  bool close_admitted;
  bool close_pending;
  bool receive_armed;
} chttp_slot;

struct chttp_client_impl {
  cnet_client network;
  chttp_slot *slots;
  chttp_limits limits;
  size_t request_capacity;
  size_t completion_count;
  bool admission_open;
  bool poll_active;
  bool callback_active;
  bool stop_active;
  bool stopped;
};

static chttp_client_impl *chttp_client_get(chttp_async_client *client) {
  return client != NULL ? (chttp_client_impl *)client->impl : NULL;
}

static uint32_t chttp_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static char *chttp_copy_text(const char *text) {
  size_t size;
  char *copy;
  if (text == NULL) return NULL;
  size = strlen(text);
  if (size == SIZE_MAX) return NULL;
  copy = (char *)malloc(size + 1u);
  if (copy == NULL) return NULL;
  memcpy(copy, text, size + 1u);
  return copy;
}

static bool chttp_config_valid(const chttp_client_config *config) {
  if (config == NULL || config->request_capacity == 0u ||
      config->request_capacity > config->network.connection_capacity ||
      config->request_capacity > UINT32_MAX || config->max_start_line_bytes <= 15u ||
      config->max_header_count < 3u || config->max_header_bytes == 0u ||
      config->max_request_body_bytes == 0u || config->max_response_body_bytes == 0u ||
      config->max_informational_responses == 0u || config->network.max_send_bytes == 0u)
    return false;
  if (config->request_capacity > SIZE_MAX / sizeof(chttp_slot)) return false;
  if (config->max_header_bytes == SIZE_MAX || config->max_start_line_bytes == SIZE_MAX)
    return false;
  return config->max_header_count <= (SIZE_MAX - config->max_header_bytes - 1u) / 2u;
}

static int chttp_stream_uri_supported(const char *uri, bool has_tls_profile) {
  if (uri == NULL) return TURBO_EINVAL;
  if (strncmp(uri, "tls://", sizeof("tls://") - 1u) == 0) return TURBO_OK;
  if (strncmp(uri, "tcp://", sizeof("tcp://") - 1u) == 0 ||
      strncmp(uri, "pipe://", sizeof("pipe://") - 1u) == 0)
    return has_tls_profile ? TURBO_EINVAL : TURBO_OK;
  return TURBO_ENOTSUP;
}

static void chttp_slot_release(chttp_slot *slot) {
  chttp_client_impl *client;
  uint32_t generation;
  if (slot == NULL) return;
  client = slot->client;
  generation = slot->generation;
  free(slot->request_data);
  free(slot->authority);
  free(slot->connection_uri);
  chttp_tls_profile_release(slot->tls_profile);
  chttp_response_parser_destroy(&slot->response_parser);
  *slot = (chttp_slot){.client = client, .generation = generation};
}

static chttp_slot *chttp_slot_find_free(chttp_client_impl *impl) {
  size_t index;
  for (index = 0u; index < impl->request_capacity; ++index)
    if (impl->slots[index].state == CHTTP_SLOT_FREE) return &impl->slots[index];
  return NULL;
}

/* The scan is O(request_capacity), whose configured hard bound also limits CNet connections. */
static chttp_slot *chttp_slot_find_idle(chttp_client_impl *impl,
                                        const chttp_request_options *options,
                                        const chttp_tls_profile_impl *tls_profile) {
  size_t index;
  for (index = 0u; index < impl->request_capacity; ++index) {
    chttp_slot *slot = &impl->slots[index];
    if (slot->state == CHTTP_SLOT_IDLE && slot->connection_uri != NULL && slot->authority != NULL &&
        strcmp(slot->connection_uri, options->connection_uri) == 0 &&
        strcmp(slot->authority, options->authority) == 0 && slot->tls_profile == tls_profile)
      return slot;
  }
  return NULL;
}

static chttp_slot *chttp_slot_find_any_idle(chttp_client_impl *impl) {
  size_t index;
  for (index = 0u; index < impl->request_capacity; ++index)
    if (impl->slots[index].state == CHTTP_SLOT_IDLE) return &impl->slots[index];
  return NULL;
}

static chttp_slot *chttp_slot_find(chttp_client_impl *impl, chttp_request request) {
  chttp_slot *slot;
  if (impl == NULL || request.slot == 0u || request.slot > impl->request_capacity ||
      request.generation == 0u)
    return NULL;
  slot = &impl->slots[request.slot - 1u];
  return slot->state != CHTTP_SLOT_FREE && slot->state != CHTTP_SLOT_TERMINAL &&
                 slot->generation == request.generation
             ? slot
             : NULL;
}

static void chttp_slot_deliver(chttp_slot *slot, const chttp_response_view *response, int status,
                               int native_status, const char *stage) {
  chttp_client_impl *impl;
  chttp_error error;
  const chttp_error *error_view = NULL;
  if (slot == NULL || slot->state == CHTTP_SLOT_FREE || slot->state == CHTTP_SLOT_TERMINAL ||
      slot->result_delivered)
    return;
  impl = slot->client;
  slot->result_delivered = true;
  if (status != TURBO_OK) {
    error = (chttp_error){.status = status, .native_status = native_status, .stage = stage};
    error_view = &error;
    response = NULL;
  }
  impl->callback_active = true;
  slot->on_complete(slot->user, slot->public_handle, response, error_view);
  impl->callback_active = false;
  ++impl->completion_count;
}

static int chttp_slot_try_close(chttp_slot *slot) {
  int status;
  if (slot == NULL || slot->state == CHTTP_SLOT_FREE || slot->state == CHTTP_SLOT_TERMINAL ||
      slot->close_admitted)
    return TURBO_OK;
  status = cnet_close(&slot->client->network, slot->connection);
  if (status == TURBO_OK) {
    slot->close_admitted = true;
    slot->close_pending = false;
    slot->state = CHTTP_SLOT_CLOSING;
    return TURBO_OK;
  }
  if (status == TURBO_EALREADY || status == TURBO_ENOENT || status == TURBO_ESHUTDOWN) {
    slot->close_admitted = true;
    slot->close_pending = false;
    slot->state = CHTTP_SLOT_CLOSING;
    return TURBO_OK;
  }
  slot->close_pending = true;
  slot->state = CHTTP_SLOT_CLOSING;
  return status;
}

static void chttp_slot_fail_and_close(chttp_slot *slot, int status, int native_status,
                                      const char *stage) {
  chttp_slot_deliver(slot, NULL, status, native_status, stage);
  (void)chttp_slot_try_close(slot);
}

static int chttp_slot_arm_receive(chttp_slot *slot) {
  int status;
  if (slot->receive_armed) return TURBO_OK;
  status = cnet_receive(&slot->client->network, slot->connection, 1u);
  if (status == TURBO_OK) slot->receive_armed = true;
  return status;
}

static void chttp_cnet_state(void *user, cnet_connection connection, cnet_connection_state state,
                             const cnet_error *error) {
  chttp_slot *slot = (chttp_slot *)user;
  int status;
  if (slot == NULL || slot->state == CHTTP_SLOT_FREE || slot->state == CHTTP_SLOT_TERMINAL ||
      slot->connection.slot != connection.slot ||
      slot->connection.generation != connection.generation)
    return;

  if (state == CNET_CONNECTION_CONNECTED) {
    if (slot->state != CHTTP_SLOT_CONNECTING || slot->result_delivered || slot->cancel_requested ||
        slot->client->stop_active)
      return;
    slot->state = CHTTP_SLOT_BUSY;
    if (slot->request_data == NULL || slot->request_size == 0u) {
      chttp_slot_fail_and_close(slot, TURBO_EPROTO, 0, "request-state");
      return;
    }
    status = cnet_send(&slot->client->network, connection, slot->request_data, slot->request_size);
    if (status != TURBO_OK) {
      chttp_slot_fail_and_close(slot, status, 0, "send-admission");
      return;
    }
    free(slot->request_data);
    slot->request_data = NULL;
    slot->request_size = 0u;
    status = chttp_slot_arm_receive(slot);
    if (status != TURBO_OK) chttp_slot_fail_and_close(slot, status, 0, "receive-admission");
    return;
  }

  if (state != CNET_CONNECTION_CLOSED && state != CNET_CONNECTION_FAILED) return;
  slot->receive_armed = false;
  if (!slot->result_delivered) {
    if (slot->cancel_requested) chttp_slot_deliver(slot, NULL, TURBO_ECANCELED, 0, "cancel");
    else if (slot->client->stop_active)
      chttp_slot_deliver(slot, NULL, TURBO_ESHUTDOWN, 0, "shutdown");
    else if (state == CNET_CONNECTION_FAILED) {
      const int failure_status = error != NULL ? error->status : TURBO_EIO;
      const int native_status = error != NULL ? error->native_status : 0;
      const char *stage = error != NULL && error->stage != NULL ? error->stage : "transport";
      chttp_slot_deliver(slot, NULL, failure_status, native_status, stage);
    } else {
      status = chttp_response_parser_finish(&slot->response_parser);
      if (status == TURBO_OK && slot->response_parser.complete)
        chttp_slot_deliver(slot, &slot->response_parser.response, TURBO_OK, 0, NULL);
      else
        chttp_slot_deliver(slot, NULL, status, slot->response_parser.parser_status,
                           slot->response_parser.failure_stage != NULL
                               ? slot->response_parser.failure_stage
                               : "eof");
    }
  }
  slot->state = CHTTP_SLOT_TERMINAL;
  slot->close_pending = false;
}

static void chttp_cnet_receive(void *user, cnet_connection connection,
                               const cnet_receive_view *view) {
  chttp_slot *slot = (chttp_slot *)user;
  int status;
  if (slot == NULL || view == NULL || slot->state == CHTTP_SLOT_FREE ||
      slot->state == CHTTP_SLOT_TERMINAL || slot->connection.slot != connection.slot ||
      slot->connection.generation != connection.generation)
    return;
  slot->receive_armed = false;
  if (slot->state == CHTTP_SLOT_IDLE) {
    (void)chttp_slot_try_close(slot);
    return;
  }
  if (slot->result_delivered) {
    (void)chttp_slot_try_close(slot);
    return;
  }
  if (slot->cancel_requested || slot->client->stop_active) {
    (void)chttp_slot_try_close(slot);
    return;
  }
  if (view->kind != CNET_MESSAGE_BYTES) {
    chttp_slot_fail_and_close(slot, TURBO_ENOTSUP, 0, "transport-kind");
    return;
  }
  status = chttp_response_parser_execute(&slot->response_parser, view->data, view->size);
  if (status != TURBO_OK) {
    chttp_slot_fail_and_close(slot, status, slot->response_parser.parser_status,
                              slot->response_parser.failure_stage != NULL
                                  ? slot->response_parser.failure_stage
                                  : "parse");
    return;
  }
  if (slot->response_parser.complete) {
    const bool keep_alive = slot->response_parser.response.protocol_keep_alive != 0;
    chttp_slot_deliver(slot, &slot->response_parser.response, TURBO_OK, 0, NULL);
    if (keep_alive && !slot->cancel_requested && !slot->client->stop_active) {
      chttp_response_parser_destroy(&slot->response_parser);
      slot->on_complete = NULL;
      slot->user = NULL;
      slot->state = CHTTP_SLOT_IDLE;
      status = chttp_slot_arm_receive(slot);
      if (status != TURBO_OK) (void)chttp_slot_try_close(slot);
    } else {
      (void)chttp_slot_try_close(slot);
    }
    return;
  }
  status = chttp_slot_arm_receive(slot);
  if (status != TURBO_OK) chttp_slot_fail_and_close(slot, status, 0, "receive-admission");
}

static int chttp_retry_pending_closes(chttp_client_impl *impl) {
  size_t index;
  int first_status = TURBO_OK;
  for (index = 0u; index < impl->request_capacity; ++index) {
    chttp_slot *slot = &impl->slots[index];
    int status;
    if (slot->state == CHTTP_SLOT_FREE || slot->state == CHTTP_SLOT_TERMINAL ||
        !slot->close_pending)
      continue;
    status = chttp_slot_try_close(slot);
    if (status != TURBO_OK && status != TURBO_ENOBUFS && first_status == TURBO_OK)
      first_status = status;
  }
  return first_status;
}

static void chttp_reap_terminal_slots(chttp_client_impl *impl) {
  size_t index;
  for (index = 0u; index < impl->request_capacity; ++index)
    if (impl->slots[index].state == CHTTP_SLOT_TERMINAL) chttp_slot_release(&impl->slots[index]);
}

int chttp_async_client_init(chttp_async_client *client, const chttp_client_config *config) {
  chttp_client_impl *impl;
  size_t index;
  int status;
  if (client == NULL || config == NULL) return TURBO_EINVAL;
  if (client->impl != NULL) return TURBO_EALREADY;
  if (!chttp_config_valid(config)) return TURBO_EINVAL;
  impl = (chttp_client_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->slots = (chttp_slot *)calloc(config->request_capacity, sizeof(*impl->slots));
  if (impl->slots == NULL) {
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->request_capacity = config->request_capacity;
  impl->limits = (chttp_limits){.max_start_line_bytes = config->max_start_line_bytes,
                                .max_header_count = config->max_header_count,
                                .max_header_bytes = config->max_header_bytes,
                                .max_request_body_bytes = config->max_request_body_bytes,
                                .max_response_body_bytes = config->max_response_body_bytes,
                                .max_informational_responses = config->max_informational_responses,
                                .max_request_bytes = config->network.max_send_bytes};
  for (index = 0u; index < impl->request_capacity; ++index)
    impl->slots[index].client = impl;
  status = cnet_client_init(&impl->network, &config->network);
  if (status != TURBO_OK) {
    free(impl->slots);
    free(impl);
    return status;
  }
  impl->admission_open = true;
  client->impl = impl;
  return TURBO_OK;
}

int chttp_async_client_submit(chttp_async_client *client, const chttp_request_options *options,
                              chttp_request *out_request) {
  chttp_client_impl *impl = chttp_client_get(client);
  chttp_slot *slot;
  unsigned char *request_data = NULL;
  size_t request_size = 0u;
  cnet_connect_options connect_options;
  chttp_tls_profile_impl *tls_profile = NULL;
  size_t slot_index;
  int status;
  if (out_request == NULL) return TURBO_EINVAL;
  *out_request = (chttp_request){0};
  if (impl == NULL || options == NULL) return TURBO_EINVAL;
  if (impl->callback_active) return TURBO_EBUSY;
  if (!impl->admission_open) return TURBO_ESHUTDOWN;
  status = chttp_stream_uri_supported(options->connection_uri, options->tls != NULL);
  if (status != TURBO_OK) return status;
  status = chttp_request_build(options, &impl->limits, &request_data, &request_size);
  if (status != TURBO_OK) return status;
  status = chttp_tls_profile_acquire(options->tls, &tls_profile);
  if (status != TURBO_OK) {
    free(request_data);
    return status;
  }

  slot = chttp_slot_find_idle(impl, options, tls_profile);
  if (slot != NULL) {
    chttp_tls_profile_release(tls_profile);
    if (!slot->receive_armed) {
      free(request_data);
      (void)chttp_slot_try_close(slot);
      return TURBO_ENOBUFS;
    }
    status = chttp_response_parser_init(&slot->response_parser, options->method, &impl->limits);
    if (status != TURBO_OK) {
      free(request_data);
      return status;
    }
    slot_index = (size_t)(slot - impl->slots);
    slot->generation = chttp_next_generation(slot->generation);
    slot->public_handle =
        (chttp_request){.slot = (uint32_t)(slot_index + 1u), .generation = slot->generation};
    slot->request_data = request_data;
    slot->request_size = request_size;
    slot->on_complete = options->on_complete;
    slot->user = options->user;
    slot->result_delivered = false;
    slot->cancel_requested = false;
    slot->close_admitted = false;
    slot->close_pending = false;
    slot->state = CHTTP_SLOT_BUSY;
    status = cnet_send(&impl->network, slot->connection, slot->request_data, slot->request_size);
    if (status != TURBO_OK) {
      free(slot->request_data);
      slot->request_data = NULL;
      slot->request_size = 0u;
      chttp_response_parser_destroy(&slot->response_parser);
      slot->on_complete = NULL;
      slot->user = NULL;
      slot->result_delivered = true;
      slot->state = CHTTP_SLOT_IDLE;
      return status;
    }
    free(slot->request_data);
    slot->request_data = NULL;
    slot->request_size = 0u;
    *out_request = slot->public_handle;
    return TURBO_OK;
  }

  slot = chttp_slot_find_free(impl);
  if (slot == NULL) {
    chttp_slot *idle = chttp_slot_find_any_idle(impl);
    free(request_data);
    chttp_tls_profile_release(tls_profile);
    if (idle != NULL) {
      status = chttp_slot_try_close(idle);
      if (status != TURBO_OK && status != TURBO_ENOBUFS) return status;
    }
    return TURBO_ENOBUFS;
  }
  status = chttp_response_parser_init(&slot->response_parser, options->method, &impl->limits);
  if (status != TURBO_OK) {
    free(request_data);
    chttp_tls_profile_release(tls_profile);
    return status;
  }
  slot_index = (size_t)(slot - impl->slots);
  slot->generation = chttp_next_generation(slot->generation);
  slot->public_handle =
      (chttp_request){.slot = (uint32_t)(slot_index + 1u), .generation = slot->generation};
  slot->request_data = request_data;
  slot->request_size = request_size;
  slot->on_complete = options->on_complete;
  slot->user = options->user;
  slot->tls_profile = tls_profile;
  slot->state = CHTTP_SLOT_CONNECTING;
  slot->connection_uri = chttp_copy_text(options->connection_uri);
  slot->authority = chttp_copy_text(options->authority);
  if (slot->connection_uri == NULL || slot->authority == NULL) {
    chttp_slot_release(slot);
    return TURBO_ENOMEM;
  }
  connect_options = (cnet_connect_options){
      .uri = options->connection_uri,
      .observer = {.on_state = chttp_cnet_state, .on_receive = chttp_cnet_receive, .user = slot},
      .tls_client = chttp_tls_profile_client(slot->tls_profile)};
  status = cnet_connect(&impl->network, &connect_options, &slot->connection);
  if (status != TURBO_OK) {
    chttp_slot_release(slot);
    return status;
  }
  *out_request = slot->public_handle;
  return TURBO_OK;
}

int chttp_async_request_cancel(chttp_async_client *client, chttp_request request) {
  chttp_client_impl *impl = chttp_client_get(client);
  chttp_slot *slot = chttp_slot_find(impl, request);
  int status;
  if (impl == NULL) return TURBO_EINVAL;
  if (slot == NULL) return TURBO_ENOENT;
  if (slot->result_delivered) return slot->state == CHTTP_SLOT_IDLE ? TURBO_ENOENT : TURBO_EALREADY;
  if (slot->cancel_requested) return TURBO_EALREADY;
  status = cnet_close(&impl->network, slot->connection);
  if (status != TURBO_OK) return status;
  slot->cancel_requested = true;
  slot->close_admitted = true;
  slot->state = CHTTP_SLOT_CLOSING;
  return TURBO_OK;
}

int chttp_async_client_poll(chttp_async_client *client, uint32_t timeout_ms,
                            size_t *out_completions) {
  chttp_client_impl *impl = chttp_client_get(client);
  size_t network_events = 0u;
  int status;
  int close_status;
  if (impl == NULL || out_completions == NULL) return TURBO_EINVAL;
  *out_completions = 0u;
  if (impl->poll_active || impl->callback_active) return TURBO_EBUSY;
  if (impl->stopped) return TURBO_ESHUTDOWN;
  impl->poll_active = true;
  impl->completion_count = 0u;
  close_status = chttp_retry_pending_closes(impl);
  status = cnet_client_poll(&impl->network, timeout_ms, &network_events);
  if (status == TURBO_OK) {
    const int after_status = chttp_retry_pending_closes(impl);
    if (close_status == TURBO_OK) close_status = after_status;
  }
  chttp_reap_terminal_slots(impl);
  *out_completions = impl->completion_count;
  impl->poll_active = false;
  if (status != TURBO_OK) return status;
  return close_status;
}

int chttp_async_client_stop(chttp_async_client *client, uint32_t timeout_ms) {
  chttp_client_impl *impl = chttp_client_get(client);
  int status;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->callback_active || impl->poll_active) return TURBO_EBUSY;
  if (impl->stopped) return TURBO_OK;
  impl->admission_open = false;
  impl->stop_active = true;
  impl->completion_count = 0u;
  status = cnet_client_stop(&impl->network, timeout_ms);
  chttp_reap_terminal_slots(impl);
  if (status == TURBO_OK) impl->stopped = true;
  return status;
}

int chttp_async_client_destroy(chttp_async_client *client) {
  chttp_client_impl *impl;
  size_t index;
  int status;
  if (client == NULL) return TURBO_EINVAL;
  impl = chttp_client_get(client);
  if (impl == NULL) return TURBO_OK;
  if (impl->callback_active || impl->poll_active || !impl->stopped) return TURBO_EBUSY;
  status = cnet_client_destroy(&impl->network);
  if (status != TURBO_OK) return status;
  for (index = 0u; index < impl->request_capacity; ++index)
    chttp_slot_release(&impl->slots[index]);
  free(impl->slots);
  free(impl);
  client->impl = NULL;
  return TURBO_OK;
}
