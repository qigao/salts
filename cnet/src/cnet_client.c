#include <cnet/cnet.h>

#include "cnet_client_internal.h"
#include "cnet_dispatcher.h"
#include "cnet_module.h"
#include "cnet_shards.h"
#include "cnet_transport.h"
#include "cnet_uri.h"

#include <salts/clock.h>
#include <salts/thread.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_client_impl cnet_client_impl;

typedef struct cnet_client_record {
  cnet_client_impl *client;
  cnet_shard_connection internal;
  cnet_connection public_handle;
  cnet_observer observer;
  cnet_uri_scheme scheme;
  size_t negotiated_alpn_size;
  char negotiated_alpn[CNET_TLS_ALPN_NAME_MAX_BYTES + 1u];
  bool active;
  bool connected;
  bool write_pending;
  bool closing_pending;
} cnet_client_record;

struct cnet_client_impl {
  cnet_shards shards;
  cnet_dispatcher dispatcher;
  cnet_client_record *records;
  salts_mutex_t lock;
  size_t shard_count;
  size_t capacity_per_shard;
  size_t record_count;
  size_t connection_capacity;
  size_t active_count;
  size_t max_send_bytes;
  size_t tls_io_buffer_bytes;
  cnet_stream_socket_options socket_options;
  uint32_t connect_timeout_ms;
  uint32_t read_timeout_ms;
  uint32_t write_timeout_ms;
  uint32_t tls_handshake_timeout_ms;
  atomic_int callback_error;
  atomic_int external_wake_pending;
  size_t poll_callback_count;
  bool admission_open;
  bool poll_active;
  bool stop_active;
  bool stopped;
};

static SALTS_THREAD_LOCAL cnet_client_impl *cnet_active_callback_client;

static cnet_client_impl *cnet_client_get(cnet_client *client) {
  return client != NULL ? (cnet_client_impl *)client->impl : NULL;
}

static bool cnet_power_of_two(size_t value) { return value != 0u && (value & (value - 1u)) == 0u; }

static bool cnet_client_config_valid(const cnet_client_config *config) {
  if (config == NULL || !native_io_backend_kind_supported(config->backend) ||
      config->connection_capacity == 0u || !cnet_power_of_two(config->command_capacity) ||
      config->request_capacity == 0u || config->completion_batch_capacity == 0u ||
      config->completion_batch_capacity > config->request_capacity ||
      !cnet_power_of_two(config->event_capacity) || config->event_capacity < 2u ||
      config->max_send_bytes == 0u || config->receive_buffer_bytes == 0u ||
      (config->command_buffer_bytes != 0u &&
       config->command_buffer_bytes < config->max_send_bytes) ||
      ((config->tls_io_buffer_bytes == 0u) != (config->tls_handshake_timeout_ms == 0u)) ||
      (config->tls_io_buffer_bytes != 0u &&
       (config->tls_io_buffer_bytes < CNET_TLS_MIN_IO_BUFFER_BYTES ||
        config->tls_io_buffer_bytes > INT_MAX)))
    return false;
  return config->connection_capacity <= UINT32_MAX &&
         config->connection_capacity <= SIZE_MAX / sizeof(cnet_client_record);
}

static uint32_t cnet_client_remaining_ms(uint64_t started_ms, uint32_t timeout_ms) {
  const uint64_t elapsed = salts_monotonic_ms() - started_ms;
  return elapsed >= timeout_ms ? 0u : timeout_ms - (uint32_t)elapsed;
}

static void cnet_client_record_error(cnet_client_impl *impl, int status) {
  int expected = SALTS_OK;
  if (status == SALTS_OK) return;
  (void)atomic_compare_exchange_strong_explicit(&impl->callback_error, &expected, status,
                                                memory_order_acq_rel, memory_order_acquire);
}

static int cnet_client_dispatch(void *context, uint32_t shard, const cnet_event *event) {
  return cnet_dispatcher_publish((cnet_dispatcher *)context, shard, event);
}

static bool cnet_client_salts_status(int status) {
  if (status == SALTS_OK) return true;
  switch (status) {
#define CNET_STATUS_CASE(name, value, name_text, message_text)                                     \
  case name:                                                                                       \
    return true;
    SALTS_ERROR_CODE_ITEMS(CNET_STATUS_CASE)
#undef CNET_STATUS_CASE
  default:
    return false;
  }
}

static const char *cnet_client_stage_name(cnet_session_stage stage) {
  switch (stage) {
  case CNET_SESSION_STAGE_RESOLVE:
    return "resolve";
  case CNET_SESSION_STAGE_CONNECT:
    return "connect";
  case CNET_SESSION_STAGE_HANDSHAKE:
    return "handshake";
  case CNET_SESSION_STAGE_READ:
    return "read";
  case CNET_SESSION_STAGE_WRITE:
    return "write";
  case CNET_SESSION_STAGE_SHUTDOWN:
    return "shutdown";
  case CNET_SESSION_STAGE_CALLBACK:
    return "callback";
  default:
    return "unknown";
  }
}

static cnet_connection_state cnet_client_state(cnet_event_state state) {
  switch (state) {
  case CNET_EVENT_STATE_CONNECTED:
    return CNET_CONNECTION_CONNECTED;
  case CNET_EVENT_STATE_CLOSING:
    return CNET_CONNECTION_CLOSING;
  case CNET_EVENT_STATE_CLOSED:
    return CNET_CONNECTION_CLOSED;
  case CNET_EVENT_STATE_FAILED:
    return CNET_CONNECTION_FAILED;
  default:
    return CNET_CONNECTION_FAILED;
  }
}

static bool cnet_client_terminal(cnet_event_state state) {
  return state == CNET_EVENT_STATE_CLOSED || state == CNET_EVENT_STATE_FAILED;
}

static void cnet_client_observe(void *context, const cnet_dispatch_view *view) {
  cnet_client_record *record = (cnet_client_record *)context;
  cnet_client_impl *impl = record->client;
  cnet_client_impl *previous = cnet_active_callback_client;

  cnet_active_callback_client = impl;
  if (view->kind == CNET_EVENT_RECEIVE) {
    if (record->observer.on_receive != NULL) {
      const cnet_receive_view public_view = {view->data, view->size,
                                             record->scheme == CNET_URI_UDP ? CNET_MESSAGE_DATAGRAM
                                                                            : CNET_MESSAGE_BYTES};
      record->observer.on_receive(record->observer.user, record->public_handle, &public_view);
      ++impl->poll_callback_count;
    }
  } else if (view->kind == CNET_EVENT_SEND) {
    salts_mutex_lock(&impl->lock);
    if (record->active && record->internal.session.slot == view->session.slot &&
        record->internal.session.generation == view->session.generation)
      record->write_pending = false;
    salts_mutex_unlock(&impl->lock);
    if (record->observer.on_send != NULL) {
      record->observer.on_send(record->observer.user, record->public_handle, view->argument);
      ++impl->poll_callback_count;
    }
  } else if (view->kind == CNET_EVENT_STATE && record->observer.on_state != NULL) {
    cnet_error error;
    const cnet_error *error_view = NULL;
    if (view->state == CNET_EVENT_STATE_CONNECTED) {
      salts_mutex_lock(&impl->lock);
      record->connected = true;
      record->negotiated_alpn_size = view->size;
      if (view->size != 0u && view->size <= CNET_TLS_ALPN_NAME_MAX_BYTES) {
        memcpy(record->negotiated_alpn, view->data, view->size);
        record->negotiated_alpn[view->size] = '\0';
      }
      salts_mutex_unlock(&impl->lock);
    }
    if (view->state == CNET_EVENT_STATE_FAILED) {
      error.status = cnet_client_salts_status(view->status) ? view->status : SALTS_EIO;
      error.native_status = cnet_client_salts_status(view->status) ? 0 : view->status;
      error.stage = cnet_client_stage_name(view->stage);
      error_view = &error;
    }
    record->observer.on_state(record->observer.user, record->public_handle,
                              cnet_client_state(view->state), error_view);
    ++impl->poll_callback_count;
  }
  cnet_active_callback_client = previous;

  if (view->kind == CNET_EVENT_STATE && cnet_client_terminal(view->state)) {
    salts_mutex_lock(&impl->lock);
    if (record->active && record->internal.session.slot == view->session.slot &&
        record->internal.session.generation == view->session.generation) {
      record->active = false;
      record->connected = false;
      record->write_pending = false;
      record->closing_pending = false;
      record->observer = (cnet_observer){0};
      record->negotiated_alpn_size = 0u;
      record->negotiated_alpn[0] = '\0';
      if (impl->active_count == 0u) cnet_client_record_error(impl, SALTS_EPROTO);
      else --impl->active_count;
    }
    salts_mutex_unlock(&impl->lock);
  }
}

static void cnet_client_cleanup_init(cnet_client_impl *impl) {
  if (impl == NULL) return;
  if (impl->shards.impl != NULL) (void)cnet_shards_stop(&impl->shards, 5000u);
  if (impl->dispatcher.impl != NULL) {
    (void)cnet_dispatcher_drain(&impl->dispatcher, 0u);
    (void)cnet_dispatcher_destroy(&impl->dispatcher);
  }
  if (impl->shards.impl != NULL) (void)cnet_shards_destroy(&impl->shards);
  salts_mutex_destroy(&impl->lock);
  free(impl->records);
  free(impl);
  (void)cnet_module_shutdown();
}

int cnet_client_init(cnet_client *client, const cnet_client_config *config) {
  cnet_client_impl *impl;
  cnet_shards_config shards_config;
  size_t max_command_payload_bytes;
  size_t command_buffer_bytes;
  size_t index;
  int status;

  if (client == NULL || config == NULL) return SALTS_EINVAL;
  if (client->impl != NULL) return SALTS_EALREADY;
  if (!cnet_client_config_valid(config)) return SALTS_EINVAL;
  status = cnet_module_init();
  if (status != SALTS_OK) return status;

  impl = (cnet_client_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) {
    (void)cnet_module_shutdown();
    return SALTS_ENOMEM;
  }
  impl->shard_count = 1u;
  impl->capacity_per_shard = config->connection_capacity;
  impl->record_count = config->connection_capacity;
  impl->connection_capacity = config->connection_capacity;
  impl->max_send_bytes = config->max_send_bytes;
  impl->tls_io_buffer_bytes = config->tls_io_buffer_bytes;
  impl->connect_timeout_ms = config->connect_timeout_ms;
  impl->read_timeout_ms = config->read_timeout_ms;
  impl->write_timeout_ms = config->write_timeout_ms;
  impl->tls_handshake_timeout_ms = config->tls_handshake_timeout_ms;
  impl->socket_options = (cnet_stream_socket_options)CNET_STREAM_SOCKET_OPTIONS_INIT;
  impl->admission_open = true;
  atomic_init(&impl->callback_error, SALTS_OK);
  atomic_init(&impl->external_wake_pending, 0);
  salts_mutex_init(&impl->lock);
  impl->records = (cnet_client_record *)calloc(impl->record_count, sizeof(*impl->records));
  if (impl->records == NULL) {
    cnet_client_cleanup_init(impl);
    return SALTS_ENOMEM;
  }
  for (index = 0u; index < impl->record_count; ++index)
    impl->records[index].client = impl;

  max_command_payload_bytes = config->max_send_bytes > sizeof(cnet_owner_connect_payload)
                                  ? config->max_send_bytes
                                  : sizeof(cnet_owner_connect_payload);
  if (config->command_buffer_bytes != 0u &&
      config->command_buffer_bytes < max_command_payload_bytes) {
    cnet_client_cleanup_init(impl);
    return SALTS_EINVAL;
  }
  if (config->command_buffer_bytes == 0u &&
      config->command_capacity > SIZE_MAX / max_command_payload_bytes) {
    cnet_client_cleanup_init(impl);
    return SALTS_ERANGE;
  }
  command_buffer_bytes = config->command_buffer_bytes != 0u
                             ? config->command_buffer_bytes
                             : config->command_capacity * max_command_payload_bytes;
  shards_config = (cnet_shards_config){
      .backend_kind = config->backend,
      .shard_count = 1u,
      .connection_capacity_per_shard = impl->capacity_per_shard,
      .command_capacity_per_shard = config->command_capacity,
      .request_capacity_per_shard = config->request_capacity,
      .completion_batch_capacity = config->completion_batch_capacity,
      .event_capacity_per_shard = config->event_capacity,
      .receive_buffer_bytes = config->receive_buffer_bytes,
      .max_state_payload_bytes =
          config->tls_io_buffer_bytes != 0u ? CNET_TLS_ALPN_NAME_MAX_BYTES : 0u,
      .max_command_payload_bytes = max_command_payload_bytes,
      .command_buffer_bytes = command_buffer_bytes};
  status = cnet_shards_init(&impl->shards, &shards_config);
  if (status != SALTS_OK) {
    cnet_client_cleanup_init(impl);
    return status;
  }
  status = cnet_dispatcher_init(&impl->dispatcher, &impl->shards);
  if (status == SALTS_OK)
    status = cnet_shards_bind_event_sink(&impl->shards, cnet_client_dispatch, &impl->dispatcher);
  if (status != SALTS_OK) {
    cnet_client_cleanup_init(impl);
    return status;
  }
  client->impl = impl;
  return SALTS_OK;
}

int cnet_client_set_stream_socket_options(cnet_client *client,
                                          const cnet_stream_socket_options *options) {
  cnet_client_impl *impl = cnet_client_get(client);
  int status = cnet_stream_socket_options_validate(options);
  if (status != SALTS_OK) return status;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open || impl->stopped) status = SALTS_ESHUTDOWN;
  else if (impl->active_count != 0u || impl->poll_active || impl->stop_active) status = SALTS_EBUSY;
  else impl->socket_options = *options;
  salts_mutex_unlock(&impl->lock);
  return status;
}

static cnet_client_record *cnet_client_find_record(cnet_client_impl *impl,
                                                   cnet_connection connection,
                                                   cnet_shard_connection *out_internal) {
  cnet_client_record *record;
  size_t index;
  if (connection.slot == 0u || connection.generation == 0u ||
      (size_t)connection.slot > impl->record_count)
    return NULL;
  index = (size_t)connection.slot - 1u;
  record = &impl->records[index];
  if (!record->active || record->public_handle.generation != connection.generation) return NULL;
  if (out_internal != NULL) *out_internal = record->internal;
  return record;
}

static int cnet_client_admit(cnet_client_impl *impl, const cnet_owner_connect_payload *payload,
                             cnet_uri_scheme scheme, const cnet_observer *observer,
                             cnet_connection *out_connection, bool *out_transferred) {
  cnet_owner_connect_payload admitted_payload;
  cnet_shard_connection internal = {0};
  cnet_client_record *record;
  size_t index;
  int status;

  if (out_transferred != NULL) *out_transferred = false;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_ESHUTDOWN;
  }
  if (impl->active_count >= impl->connection_capacity) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_ENOBUFS;
  }
  admitted_payload = *payload;
  admitted_payload.socket_options = impl->socket_options;
  status = cnet_shards_connect(&impl->shards, &admitted_payload, &internal);
  if (status != SALTS_OK) {
    salts_mutex_unlock(&impl->lock);
    return status;
  }
  if (out_transferred != NULL) *out_transferred = true;
  index = (size_t)internal.shard * impl->capacity_per_shard + (size_t)internal.session.slot - 1u;
  record = &impl->records[index];
  record->internal = internal;
  record->public_handle = (cnet_connection){(uint32_t)(index + 1u), internal.session.generation};
  record->observer = *observer;
  record->scheme = scheme;
  record->active = true;
  record->connected = false;
  record->write_pending = false;
  record->closing_pending = false;
  ++impl->active_count;
  status = cnet_dispatcher_register(&impl->dispatcher, internal, cnet_client_observe, record);
  if (status == SALTS_OK) {
    *out_connection = record->public_handle;
  } else {
    record->active = false;
    record->observer = (cnet_observer){0};
    --impl->active_count;
    (void)cnet_shards_close(&impl->shards, internal);
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_connect(cnet_client *client, const cnet_connect_options *options,
                 cnet_connection *out_connection) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_owner_connect_payload payload = {0};
  cnet_uri uri = {0};
  bool transferred = false;
  int status;

  if (out_connection == NULL) return SALTS_EINVAL;
  *out_connection = (cnet_connection){0};
  if (impl == NULL || options == NULL || options->uri == NULL || options->observer.on_state == NULL)
    return SALTS_EINVAL;
  status = cnet_uri_parse(options->uri, &uri);
  if (status != SALTS_OK) return status;
  if (options->tls != NULL && options->tls_client != NULL) return SALTS_EINVAL;
  if ((options->tls != NULL || options->tls_client != NULL) && uri.scheme != CNET_URI_TLS)
    return SALTS_EINVAL;
  if (uri.scheme == CNET_URI_TLS && impl->tls_io_buffer_bytes == 0u) return SALTS_ENOTSUP;
  payload.scheme = uri.scheme;
  payload.connect_timeout_ms = impl->connect_timeout_ms;
  payload.read_timeout_ms = impl->read_timeout_ms;
  payload.write_timeout_ms = impl->write_timeout_ms;
  if (uri.scheme == CNET_URI_TLS) {
    const char *profile_server_name = cnet_tls_client_server_name(options->tls_client);
    const char *server_name = options->tls != NULL && options->tls->server_name != NULL
                                  ? options->tls->server_name
                              : profile_server_name != NULL ? profile_server_name
                                                            : uri.host;
    if (options->tls_client != NULL) {
      payload.tls_context = cnet_tls_client_context(options->tls_client);
      if (payload.tls_context == NULL) return SALTS_EINVAL;
      cnet_tls_context_retain(payload.tls_context);
    } else {
      status = cnet_tls_client_context_create(options->tls, &payload.tls_context);
      if (status != SALTS_OK) return status;
    }
    memcpy(payload.tls_server_name, server_name, strlen(server_name) + 1u);
    payload.tls_io_buffer_bytes = impl->tls_io_buffer_bytes;
    payload.tls_handshake_timeout_ms = impl->tls_handshake_timeout_ms;
  }
  if (uri.scheme == CNET_URI_PIPE) memcpy(payload.pipe_name, uri.path, strlen(uri.path) + 1u);
  else {
    status = cnet_transport_parse_numeric_address(uri.host, uri.port, payload.address,
                                                  sizeof(payload.address), &payload.address_length);
    if (status == SALTS_ENOENT) {
      memcpy(payload.host, uri.host, strlen(uri.host) + 1u);
      payload.port = uri.port;
    } else if (status != SALTS_OK) {
      if (payload.tls_context != NULL) cnet_tls_context_release(payload.tls_context);
      return status;
    }
  }

  status = cnet_client_admit(impl, &payload, uri.scheme, &options->observer, out_connection,
                             &transferred);
  if (!transferred && payload.tls_context != NULL) cnet_tls_context_release(payload.tls_context);
  return status;
}

int cnet_client_adopt_tcp(cnet_client *client, uintptr_t native_socket,
                          const cnet_observer *observer, cnet_connection *out_connection) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_owner_connect_payload payload = {0};
  bool transferred = false;
  int status;

  if (out_connection == NULL) {
    cnet_transport_close_socket(native_socket);
    return SALTS_EINVAL;
  }
  *out_connection = (cnet_connection){0};
  if (impl == NULL || observer == NULL || observer->on_state == NULL ||
      native_socket == UINTPTR_MAX) {
    cnet_transport_close_socket(native_socket);
    return SALTS_EINVAL;
  }
  payload.scheme = CNET_URI_TCP;
  payload.adopted_socket = native_socket;
  payload.read_timeout_ms = impl->read_timeout_ms;
  payload.write_timeout_ms = impl->write_timeout_ms;
  payload.adopted = true;
  status = cnet_client_admit(impl, &payload, CNET_URI_TCP, observer, out_connection, &transferred);
  if (!transferred) cnet_transport_close_socket(native_socket);
  return status;
}

int cnet_client_adopt_tls_server(cnet_client *client, uintptr_t native_socket,
                                 cnet_tls_context *context, const cnet_observer *observer,
                                 cnet_connection *out_connection) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_owner_connect_payload payload = {0};
  bool transferred = false;
  int status;

  if (out_connection == NULL) {
    cnet_transport_close_socket(native_socket);
    return SALTS_EINVAL;
  }
  *out_connection = (cnet_connection){0};
  if (impl == NULL || observer == NULL || observer->on_state == NULL || context == NULL ||
      native_socket == UINTPTR_MAX) {
    cnet_transport_close_socket(native_socket);
    return SALTS_EINVAL;
  }
  if (impl->tls_io_buffer_bytes == 0u) {
    cnet_transport_close_socket(native_socket);
    return SALTS_ENOTSUP;
  }

  cnet_tls_context_retain(context);
  payload.scheme = CNET_URI_TLS;
  payload.adopted_socket = native_socket;
  payload.read_timeout_ms = impl->read_timeout_ms;
  payload.write_timeout_ms = impl->write_timeout_ms;
  payload.tls_handshake_timeout_ms = impl->tls_handshake_timeout_ms;
  payload.tls_io_buffer_bytes = impl->tls_io_buffer_bytes;
  payload.tls_context = context;
  payload.adopted = true;
  payload.tls_server = true;
  status = cnet_client_admit(impl, &payload, CNET_URI_TLS, observer, out_connection, &transferred);
  if (!transferred) {
    cnet_tls_context_release(context);
    cnet_transport_close_socket(native_socket);
  }
  return status;
}

int cnet_tls_negotiated_alpn(cnet_client *client, cnet_connection connection, char *buffer,
                             size_t capacity, size_t *out_size) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_client_record *record;
  int status = SALTS_OK;
  if (out_size == NULL) return SALTS_EINVAL;
  *out_size = 0u;
  if (impl == NULL || buffer == NULL || capacity == 0u) return SALTS_EINVAL;

  salts_mutex_lock(&impl->lock);
  record = cnet_client_find_record(impl, connection, NULL);
  if (record == NULL) status = SALTS_ENOENT;
  else if (record->scheme != CNET_URI_TLS) status = SALTS_ENOTSUP;
  else if (!record->connected) status = SALTS_ENOTCONN;
  else if (record->negotiated_alpn_size == 0u) status = SALTS_ENOENT;
  else if (capacity <= record->negotiated_alpn_size) status = SALTS_EMSGSIZE;
  else {
    memcpy(buffer, record->negotiated_alpn, record->negotiated_alpn_size + 1u);
    *out_size = record->negotiated_alpn_size;
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_tls_peer_certificate_sha256(cnet_client *client, cnet_connection connection,
                                     char buffer[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY]) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_shard_connection internal = {0};
  cnet_client_record *record;
  int status;
  if (impl == NULL || buffer == NULL) return SALTS_EINVAL;
  buffer[0] = '\0';
  salts_mutex_lock(&impl->lock);
  record = cnet_client_find_record(impl, connection, &internal);
  if (record == NULL) status = SALTS_ENOENT;
  else if (record->scheme != CNET_URI_TLS) status = SALTS_ENOTSUP;
  else if (!record->connected) status = SALTS_ENOTCONN;
  else status = cnet_shards_tls_peer_certificate_sha256(&impl->shards, internal, buffer);
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_tls_export_channel_binding(cnet_client *client, cnet_connection connection,
                                    uint8_t output[CNET_TLS_CHANNEL_BINDING_BYTES]) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_shard_connection internal = {0};
  cnet_client_record *record;
  int status;
  if (impl == NULL || output == NULL) return SALTS_EINVAL;
  memset(output, 0, CNET_TLS_CHANNEL_BINDING_BYTES);
  salts_mutex_lock(&impl->lock);
  record = cnet_client_find_record(impl, connection, &internal);
  if (record == NULL) status = SALTS_ENOENT;
  else if (record->scheme != CNET_URI_TLS) status = SALTS_ENOTSUP;
  else if (!record->connected) status = SALTS_ENOTCONN;
  else status = cnet_shards_tls_export_channel_binding(&impl->shards, internal, output);
  salts_mutex_unlock(&impl->lock);
  return status;
}

static int cnet_client_operation(cnet_client_impl *impl, cnet_connection connection,
                                 cnet_shard_connection *out_internal,
                                 bool require_receive_observer) {
  cnet_client_record *record;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_ESHUTDOWN;
  }
  record = cnet_client_find_record(impl, connection, out_internal);
  if (record == NULL) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_ENOENT;
  }
  if (require_receive_observer && record->observer.on_receive == NULL) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EINVAL;
  }
  if (require_receive_observer && record->closing_pending) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  salts_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

typedef struct cnet_client_send_input {
  const void *data;
  const cnet_const_buffer *segments;
  size_t size;
  size_t segment_count;
  bool close_after_send;
} cnet_client_send_input;

static int cnet_client_send_admit(cnet_client_impl *impl, cnet_connection connection,
                                  const cnet_client_send_input *input) {
  cnet_shard_connection internal = {0};
  cnet_client_record *record;
  int status;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open) status = SALTS_ESHUTDOWN;
  else {
    record = cnet_client_find_record(impl, connection, &internal);
    if (record == NULL) status = SALTS_ENOENT;
    else if (record->write_pending || record->closing_pending) status = SALTS_EBUSY;
    else {
      if (input->segments != NULL)
        status = cnet_shards_sendv(&impl->shards, internal, input->segments, input->segment_count,
                                   input->size);
      else if (input->close_after_send)
        status = cnet_shards_send_and_close(&impl->shards, internal, input->data, input->size);
      else status = cnet_shards_send(&impl->shards, internal, input->data, input->size);
      if (status == SALTS_OK) {
        record->write_pending = true;
        record->closing_pending = input->close_after_send;
      }
    }
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_send(cnet_client *client, cnet_connection connection, const void *data, size_t size) {
  cnet_client_impl *impl = cnet_client_get(client);
  const cnet_client_send_input input = {.data = data, .size = size};
  if (impl == NULL || data == NULL || size == 0u) return SALTS_EINVAL;
  if (size > impl->max_send_bytes) return SALTS_EMSGSIZE;
  return cnet_client_send_admit(impl, connection, &input);
}

int cnet_sendv(cnet_client *client, cnet_connection connection, const cnet_const_buffer *segments,
               size_t segment_count) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_client_send_input input = {.segments = segments, .segment_count = segment_count};
  if (impl == NULL || segments == NULL || segment_count == 0u) return SALTS_EINVAL;
  for (size_t index = 0u; index < segment_count; ++index) {
    if (segments[index].data == NULL || segments[index].size == 0u) return SALTS_EINVAL;
    if (segments[index].size > impl->max_send_bytes - input.size) return SALTS_EMSGSIZE;
    input.size += segments[index].size;
  }
  return cnet_client_send_admit(impl, connection, &input);
}

int cnet_send_and_close(cnet_client *client, cnet_connection connection, const void *data,
                        size_t size) {
  cnet_client_impl *impl = cnet_client_get(client);
  const cnet_client_send_input input = {.data = data, .size = size, .close_after_send = true};
  if (impl == NULL || data == NULL || size == 0u) return SALTS_EINVAL;
  if (size > impl->max_send_bytes) return SALTS_EMSGSIZE;
  return cnet_client_send_admit(impl, connection, &input);
}

int cnet_receive(cnet_client *client, cnet_connection connection, size_t demand) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_shard_connection internal = {0};
  int status;
  if (impl == NULL || demand == 0u) return SALTS_EINVAL;
  status = cnet_client_operation(impl, connection, &internal, true);
  return status == SALTS_OK ? cnet_shards_receive(&impl->shards, internal, demand) : status;
}

int cnet_close(cnet_client *client, cnet_connection connection) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_shard_connection internal = {0};
  cnet_client_record *record;
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open) status = SALTS_ESHUTDOWN;
  else {
    record = cnet_client_find_record(impl, connection, &internal);
    if (record == NULL) status = SALTS_ENOENT;
    else if (record->closing_pending) status = SALTS_EALREADY;
    else {
      status = cnet_shards_close(&impl->shards, internal);
      if (status == SALTS_OK) record->closing_pending = true;
    }
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_client_poll(cnet_client *client, uint32_t timeout_ms, size_t *out_events) {
  cnet_client_impl *impl = cnet_client_get(client);
  const uint64_t started_ms = salts_monotonic_ms();
  uint32_t remaining_ms = timeout_ms;
  int status;
  if (out_events == NULL) return SALTS_EINVAL;
  *out_events = 0u;
  if (impl == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open || impl->stopped) status = SALTS_ESHUTDOWN;
  else if (impl->poll_active || impl->stop_active) status = SALTS_EBUSY;
  else {
    impl->poll_active = true;
    impl->poll_callback_count = 0u;
    status = SALTS_OK;
  }
  salts_mutex_unlock(&impl->lock);
  if (status != SALTS_OK) return status;

  for (;;) {
    bool externally_woken = false;
    if (atomic_exchange_explicit(&impl->external_wake_pending, 0, memory_order_acq_rel) != 0) break;
    status = cnet_shards_poll(&impl->shards, remaining_ms);
    if (status == SALTS_OK)
      status = atomic_load_explicit(&impl->callback_error, memory_order_acquire);
    /* A callback may publish a wake after the backend selected its callback
       event. Preserve that edge for the next poll so native wake coalescing
       cannot hide later external work. */
    if (status == SALTS_OK && impl->poll_callback_count == 0u)
      externally_woken =
          atomic_exchange_explicit(&impl->external_wake_pending, 0, memory_order_acq_rel) != 0;
    if (status != SALTS_OK || impl->poll_callback_count != 0u || timeout_ms == 0u ||
        externally_woken)
      break;
    {
      const uint64_t elapsed_ms = salts_monotonic_ms() - started_ms;
      if (elapsed_ms >= timeout_ms) break;
      remaining_ms = timeout_ms - (uint32_t)elapsed_ms;
    }
  }

  salts_mutex_lock(&impl->lock);
  *out_events = impl->poll_callback_count;
  impl->poll_active = false;
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_client_wake(cnet_client *client) {
  cnet_client_impl *impl = cnet_client_get(client);
  bool admission_open;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  admission_open = impl->admission_open && !impl->stopped;
  salts_mutex_unlock(&impl->lock);
  if (!admission_open) return SALTS_ESHUTDOWN;
  atomic_store_explicit(&impl->external_wake_pending, 1, memory_order_release);
  return cnet_shards_wake(&impl->shards);
}

int cnet_client_stop(cnet_client *client, uint32_t timeout_ms) {
  cnet_client_impl *impl = cnet_client_get(client);
  const uint64_t started_ms = salts_monotonic_ms();
  bool fully_stopped;
  int first_status = SALTS_OK;
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (impl->stopped) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EALREADY;
  }
  if (impl->stop_active) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  impl->stop_active = true;
  impl->admission_open = false;
  salts_mutex_unlock(&impl->lock);

  status =
      cnet_dispatcher_drain(&impl->dispatcher, cnet_client_remaining_ms(started_ms, timeout_ms));
  if (status == SALTS_EALREADY) status = SALTS_OK;
  if (status != SALTS_OK) first_status = status;
  status = atomic_load_explicit(&impl->callback_error, memory_order_acquire);
  if (first_status == SALTS_OK && status != SALTS_OK) first_status = status;
  if (cnet_dispatcher_drained(&impl->dispatcher)) {
    status = cnet_shards_stop(&impl->shards, cnet_client_remaining_ms(started_ms, timeout_ms));
    if (status == SALTS_EALREADY) status = SALTS_OK;
    if (first_status == SALTS_OK && status != SALTS_OK) first_status = status;
  }

  fully_stopped = cnet_dispatcher_drained(&impl->dispatcher) && cnet_shards_stopped(&impl->shards);
  salts_mutex_lock(&impl->lock);
  impl->stop_active = false;
  if (fully_stopped) impl->stopped = true;
  salts_mutex_unlock(&impl->lock);
  return first_status;
}

int cnet_client_destroy(cnet_client *client) {
  cnet_client_impl *impl = cnet_client_get(client);
  int status;
  if (client == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (!impl->stopped || impl->stop_active) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  salts_mutex_unlock(&impl->lock);

  status = cnet_dispatcher_destroy(&impl->dispatcher);
  if (status != SALTS_OK) return status;
  status = cnet_shards_destroy(&impl->shards);
  if (status != SALTS_OK) return status;
  salts_mutex_destroy(&impl->lock);
  free(impl->records);
  free(impl);
  client->impl = NULL;
  return cnet_module_shutdown();
}
