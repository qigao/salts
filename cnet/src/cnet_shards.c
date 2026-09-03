#include "cnet_shards.h"

#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_shards_impl cnet_shards_impl;

typedef struct cnet_shard_record {
  cnet_shards_impl *shards;
  cnet_owner owner;
  cnet_session_table sessions;
  cnet_command_queue commands;
  cnet_event_queue events;
  atomic_int drive_status;
  uint32_t shard;
  bool owner_closed;
} cnet_shard_record;

struct cnet_shards_impl {
  cnet_shard_record *records;
  turbo_mutex_t admission_lock;
  size_t shard_count;
  size_t connection_capacity_per_shard;
  size_t max_event_payload_bytes;
  size_t active_connections;
  size_t next_shard;
  size_t max_command_payload_bytes;
  _Atomic(cnet_shards_event_sink_fn) event_sink;
  _Atomic(void *) event_sink_context;
  bool admission_open;
  bool stopping;
  bool stopped;
};

static cnet_shards_impl *cnet_shards_get(cnet_shards *shards) {
  return shards != NULL ? (cnet_shards_impl *)shards->impl : NULL;
}

static cnet_shard_record *cnet_shards_get_record(cnet_shards_impl *impl, uint32_t shard) {
  if (impl == NULL || (size_t)shard >= impl->shard_count) return NULL;
  return &impl->records[shard];
}

static int cnet_shards_first_error(cnet_shards_impl *impl) {
  size_t index;
  for (index = 0u; index < impl->shard_count; ++index) {
    const int status =
        atomic_load_explicit(&impl->records[index].drive_status, memory_order_acquire);
    if (status != TURBO_OK) return status;
  }
  return TURBO_OK;
}

static void cnet_shards_record_error(cnet_shard_record *record, int status) {
  int expected = TURBO_OK;
  if (record == NULL || status == TURBO_OK || status == TURBO_ETIMEDOUT) return;
  (void)atomic_compare_exchange_strong_explicit(&record->drive_status, &expected, status,
                                                memory_order_acq_rel, memory_order_acquire);
}

static int cnet_shards_publish_owner_event(void *context, const cnet_event *event) {
  cnet_shard_record *record = (cnet_shard_record *)context;
  cnet_shards_event_sink_fn sink =
      atomic_load_explicit(&record->shards->event_sink, memory_order_acquire);

  if (sink != NULL) {
    void *sink_context =
        atomic_load_explicit(&record->shards->event_sink_context, memory_order_relaxed);
    return sink(sink_context, record->shard, event);
  }
  return cnet_event_queue_publish(&record->events, event);
}

static void cnet_shards_cleanup_records(cnet_shards_impl *impl, size_t count) {
  size_t index;
  for (index = 0u; index < count; ++index) {
    cnet_shard_record *record = &impl->records[index];
    if (record->owner.impl != NULL) {
      (void)cnet_owner_close(&record->owner);
      (void)cnet_owner_destroy(&record->owner);
    }
    if (record->events.impl != NULL) {
      (void)cnet_event_queue_close(&record->events);
      (void)cnet_event_queue_destroy(&record->events);
    }
    if (record->commands.impl != NULL) {
      (void)cnet_command_queue_close(&record->commands);
      (void)cnet_command_queue_destroy(&record->commands);
    }
    if (record->sessions.impl != NULL) (void)cnet_session_table_destroy(&record->sessions);
  }
}

bool cnet_shard_connection_valid(cnet_shard_connection connection) {
  return cnet_session_handle_valid(connection.session);
}

int cnet_shards_init(cnet_shards *shards, const cnet_shards_config *config) {
  cnet_shards_impl *impl;
  size_t initialized = 0u;
  size_t index;
  int status = TURBO_OK;

  if (shards == NULL || config == NULL) return TURBO_EINVAL;
  if (shards->impl != NULL) return TURBO_EALREADY;
  if (config->shard_count != 1u || config->connection_capacity_per_shard == 0u ||
      config->command_capacity_per_shard == 0u || config->request_capacity_per_shard == 0u ||
      config->completion_batch_capacity == 0u || config->event_capacity_per_shard < 2u ||
      config->receive_buffer_bytes == 0u ||
      config->max_command_payload_bytes < sizeof(cnet_owner_connect_payload) ||
      config->shard_count > SIZE_MAX / sizeof(cnet_shard_record))
    return TURBO_EINVAL;

  impl = (cnet_shards_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->records = (cnet_shard_record *)calloc(config->shard_count, sizeof(*impl->records));
  if (impl->records == NULL) {
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->shard_count = config->shard_count;
  impl->connection_capacity_per_shard = config->connection_capacity_per_shard;
  impl->max_event_payload_bytes = config->receive_buffer_bytes > config->max_state_payload_bytes
                                      ? config->receive_buffer_bytes
                                      : config->max_state_payload_bytes;
  impl->max_command_payload_bytes = config->max_command_payload_bytes;
  impl->admission_open = true;
  atomic_init(&impl->event_sink, NULL);
  atomic_init(&impl->event_sink_context, NULL);
  turbo_mutex_init(&impl->admission_lock);

  for (index = 0u; index < impl->shard_count; ++index) {
    cnet_shard_record *record = &impl->records[index];
    const cnet_command_queue_config command_config = {config->command_capacity_per_shard,
                                                      config->max_command_payload_bytes};
    const cnet_event_queue_config event_config = {config->event_capacity_per_shard,
                                                  config->event_capacity_per_shard / 2u,
                                                  impl->max_event_payload_bytes};
    const cnet_owner_config owner_config = {
        .backend_kind = config->backend_kind,
        .connection_capacity = config->connection_capacity_per_shard,
        .request_capacity = config->request_capacity_per_shard,
        .completion_batch_capacity = config->completion_batch_capacity,
        .receive_buffer_bytes = config->receive_buffer_bytes,
        .receive_buffer_count = config->connection_capacity_per_shard,
        .sessions = &record->sessions,
        .commands = &record->commands,
        .events = &record->events,
        .publish_event = cnet_shards_publish_owner_event,
        .event_context = record};

    record->shards = impl;
    record->shard = (uint32_t)index;
    atomic_init(&record->drive_status, TURBO_OK);
    status = cnet_session_table_init(&record->sessions, config->connection_capacity_per_shard);
    if (status == TURBO_OK) status = cnet_command_queue_init(&record->commands, &command_config);
    if (status == TURBO_OK) status = cnet_event_queue_init(&record->events, &event_config);
    if (status == TURBO_OK) status = cnet_owner_init(&record->owner, &owner_config);
    ++initialized;
    if (status != TURBO_OK) break;
  }
  if (status != TURBO_OK) {
    cnet_shards_cleanup_records(impl, initialized);
    turbo_mutex_destroy(&impl->admission_lock);
    free(impl->records);
    free(impl);
    return status;
  }

  shards->impl = impl;
  return TURBO_OK;
}

int cnet_shards_poll(cnet_shards *shards, uint32_t timeout_ms) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  cnet_shard_record *record;
  int first_status;
  int status;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->stopping || impl->stopped) return TURBO_ESHUTDOWN;
  first_status = cnet_shards_first_error(impl);
  record = &impl->records[0];
  status = cnet_owner_drive(&record->owner, timeout_ms);
  cnet_shards_record_error(record, status);
  return first_status != TURBO_OK ? first_status : status;
}

int cnet_shards_wake(cnet_shards *shards) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  if (impl == NULL) return TURBO_EINVAL;
  return cnet_owner_wake(&impl->records[0].owner);
}

int cnet_shards_bind_event_sink(cnet_shards *shards, cnet_shards_event_sink_fn sink,
                                void *context) {
  cnet_shards_impl *impl = cnet_shards_get(shards);

  if (impl == NULL || sink == NULL || context == NULL) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->admission_lock);
  if (atomic_load_explicit(&impl->event_sink, memory_order_relaxed) != NULL) {
    turbo_mutex_unlock(&impl->admission_lock);
    return TURBO_EALREADY;
  }
  atomic_store_explicit(&impl->event_sink_context, context, memory_order_relaxed);
  atomic_store_explicit(&impl->event_sink, sink, memory_order_release);
  turbo_mutex_unlock(&impl->admission_lock);
  return TURBO_OK;
}

bool cnet_shards_get_layout(const cnet_shards *shards, cnet_shards_layout *out_layout) {
  const cnet_shards_impl *impl = shards != NULL ? (const cnet_shards_impl *)shards->impl : NULL;
  if (impl == NULL || out_layout == NULL) return false;
  *out_layout = (cnet_shards_layout){impl->shard_count, impl->connection_capacity_per_shard,
                                     impl->max_event_payload_bytes};
  return true;
}

int cnet_shards_connect(cnet_shards *shards, const cnet_owner_connect_payload *payload,
                        cnet_shard_connection *out_connection) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  cnet_shard_connection connection = {0};
  size_t offset;
  int status;

  if (out_connection == NULL) return TURBO_EINVAL;
  memset(out_connection, 0, sizeof(*out_connection));
  if (impl == NULL || payload == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->admission_lock);
  status = cnet_shards_first_error(impl);
  if (!impl->admission_open || status != TURBO_OK) {
    turbo_mutex_unlock(&impl->admission_lock);
    return status != TURBO_OK ? status : TURBO_ESHUTDOWN;
  }
  status = TURBO_ENOBUFS;
  for (offset = 0u; offset < impl->shard_count; ++offset) {
    const size_t shard = (impl->next_shard + offset) % impl->shard_count;
    status = cnet_session_table_reserve(&impl->records[shard].sessions, &connection.session);
    if (status == TURBO_ENOBUFS) continue;
    if (status != TURBO_OK) break;
    connection.shard = (uint32_t)shard;
    break;
  }
  if (status == TURBO_OK) {
    cnet_shard_record *record = &impl->records[connection.shard];
    const cnet_command command = {CNET_COMMAND_CONNECT, connection.session, payload,
                                  sizeof(*payload), 0u};
    status = cnet_command_queue_publish(&record->commands, &command);
    if (status == TURBO_OK) {
      impl->next_shard = ((size_t)connection.shard + 1u) % impl->shard_count;
      ++impl->active_connections;
      *out_connection = connection;
    } else {
      (void)cnet_session_table_release_reservation(&record->sessions, connection.session);
    }
  }
  turbo_mutex_unlock(&impl->admission_lock);
  return status;
}

static int cnet_shards_publish(cnet_shards_impl *impl, cnet_shard_connection connection,
                               cnet_command_kind kind, const void *data, size_t size,
                               size_t argument) {
  cnet_shard_record *record;
  cnet_session_state state = CNET_SESSION_FREE;
  cnet_command command;
  int status;

  if (!cnet_shard_connection_valid(connection)) return TURBO_ENOENT;
  record = cnet_shards_get_record(impl, connection.shard);
  if (record == NULL) return TURBO_ENOENT;

  turbo_mutex_lock(&impl->admission_lock);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->admission_lock);
    return TURBO_ESHUTDOWN;
  }
  status = cnet_shards_first_error(impl);
  if (status == TURBO_OK || kind == CNET_COMMAND_CLOSE)
    status = cnet_session_table_state(&record->sessions, connection.session, &state);
  if (status == TURBO_OK &&
      (kind == CNET_COMMAND_SEND || kind == CNET_COMMAND_SEND_CLOSE ||
       kind == CNET_COMMAND_RECEIVE) &&
      state != CNET_SESSION_OPEN)
    status = TURBO_EBUSY;
  if (status == TURBO_OK && kind == CNET_COMMAND_CLOSE && state == CNET_SESSION_DRAINING)
    status = TURBO_EALREADY;
  if (status == TURBO_OK && state == CNET_SESSION_TERMINAL) status = TURBO_EALREADY;
  if (status == TURBO_OK) {
    command = (cnet_command){kind, connection.session, data, size, argument};
    status = cnet_command_queue_publish(&record->commands, &command);
  }
  turbo_mutex_unlock(&impl->admission_lock);
  return status;
}

int cnet_shards_send(cnet_shards *shards, cnet_shard_connection connection, const void *data,
                     size_t size) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  if (impl == NULL || data == NULL || size == 0u) return TURBO_EINVAL;
  if (size > impl->max_command_payload_bytes) return TURBO_EMSGSIZE;
  return cnet_shards_publish(impl, connection, CNET_COMMAND_SEND, data, size, 0u);
}

int cnet_shards_send_and_close(cnet_shards *shards, cnet_shard_connection connection,
                               const void *data, size_t size) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  if (impl == NULL || data == NULL || size == 0u) return TURBO_EINVAL;
  if (size > impl->max_command_payload_bytes) return TURBO_EMSGSIZE;
  return cnet_shards_publish(impl, connection, CNET_COMMAND_SEND_CLOSE, data, size, 0u);
}

int cnet_shards_receive(cnet_shards *shards, cnet_shard_connection connection, size_t demand) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  if (impl == NULL || demand == 0u) return TURBO_EINVAL;
  return cnet_shards_publish(impl, connection, CNET_COMMAND_RECEIVE, NULL, 0u, demand);
}

int cnet_shards_close(cnet_shards *shards, cnet_shard_connection connection) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  if (impl == NULL) return TURBO_EINVAL;
  return cnet_shards_publish(impl, connection, CNET_COMMAND_CLOSE, NULL, 0u, 0u);
}

int cnet_shards_state(cnet_shards *shards, cnet_shard_connection connection,
                      cnet_session_state *out_state) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  cnet_shard_record *record;
  if (out_state == NULL) return TURBO_EINVAL;
  *out_state = CNET_SESSION_FREE;
  if (impl == NULL || !cnet_shard_connection_valid(connection)) return TURBO_ENOENT;
  record = cnet_shards_get_record(impl, connection.shard);
  return record != NULL ? cnet_session_table_state(&record->sessions, connection.session, out_state)
                        : TURBO_ENOENT;
}

int cnet_shards_take_event(cnet_shards *shards, uint32_t shard, cnet_event_view *out_event) {
  cnet_shard_record *record = cnet_shards_get_record(cnet_shards_get(shards), shard);
  return record != NULL ? cnet_event_queue_take(&record->events, out_event) : TURBO_EINVAL;
}

int cnet_shards_release_event(cnet_shards *shards, uint32_t shard, cnet_event_view *event) {
  cnet_shard_record *record = cnet_shards_get_record(cnet_shards_get(shards), shard);
  return record != NULL ? cnet_event_queue_release(&record->events, event) : TURBO_EINVAL;
}

int cnet_shards_recycle(cnet_shards *shards, cnet_shard_connection connection,
                        cnet_session_terminal *out_terminal) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  cnet_shard_record *record;
  int status;

  if (impl == NULL || out_terminal == NULL) return TURBO_EINVAL;
  memset(out_terminal, 0, sizeof(*out_terminal));
  if (!cnet_shard_connection_valid(connection)) return TURBO_ENOENT;
  record = cnet_shards_get_record(impl, connection.shard);
  if (record == NULL) return TURBO_ENOENT;

  turbo_mutex_lock(&impl->admission_lock);
  status = cnet_session_table_take_terminal(&record->sessions, connection.session, out_terminal);
  if (status == TURBO_OK)
    status = cnet_session_table_recycle(&record->sessions, connection.session);
  if (status == TURBO_OK) status = cnet_owner_release_session(&record->owner, connection.session);
  if (status == TURBO_OK) {
    if (impl->active_connections == 0u) status = TURBO_EPROTO;
    else --impl->active_connections;
  }
  turbo_mutex_unlock(&impl->admission_lock);
  return status;
}

int cnet_shards_stop(cnet_shards *shards, uint32_t timeout_ms) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  size_t index;
  int first_status;
  int status;

  (void)timeout_ms;
  if (impl == NULL) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->admission_lock);
  if (impl->stopped) {
    turbo_mutex_unlock(&impl->admission_lock);
    return TURBO_EALREADY;
  }
  if (!impl->stopping) {
    if (impl->active_connections != 0u) {
      turbo_mutex_unlock(&impl->admission_lock);
      return TURBO_EBUSY;
    }
    impl->admission_open = false;
    for (index = 0u; index < impl->shard_count; ++index) {
      status = cnet_command_queue_close(&impl->records[index].commands);
      if (status != TURBO_OK) {
        turbo_mutex_unlock(&impl->admission_lock);
        return status;
      }
    }
    impl->stopping = true;
  }
  turbo_mutex_unlock(&impl->admission_lock);

  first_status = cnet_shards_first_error(impl);
  for (index = 0u; index < impl->shard_count; ++index) {
    cnet_shard_record *record = &impl->records[index];
    if (!record->owner_closed) {
      status = cnet_owner_close(&record->owner);
      if (status != TURBO_OK) return status;
      record->owner_closed = true;
    }
    status = cnet_event_queue_close(&record->events);
    if (status != TURBO_OK && status != TURBO_EALREADY) return status;
  }
  impl->stopped = true;
  return first_status;
}

bool cnet_shards_stopped(const cnet_shards *shards) {
  const cnet_shards_impl *impl = shards != NULL ? (const cnet_shards_impl *)shards->impl : NULL;
  return impl != NULL && impl->stopped;
}

int cnet_shards_destroy(cnet_shards *shards) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  size_t index;
  int status;

  if (shards == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!impl->stopped || impl->active_connections != 0u) return TURBO_EBUSY;
  for (index = 0u; index < impl->shard_count; ++index) {
    cnet_shard_record *record = &impl->records[index];
    status = cnet_event_queue_destroy(&record->events);
    if (status != TURBO_OK) return status;
    status = cnet_owner_destroy(&record->owner);
    if (status != TURBO_OK) return status;
    status = cnet_command_queue_destroy(&record->commands);
    if (status != TURBO_OK) return status;
    status = cnet_session_table_destroy(&record->sessions);
    if (status != TURBO_OK) return status;
  }
  turbo_mutex_destroy(&impl->admission_lock);
  free(impl->records);
  free(impl);
  shards->impl = NULL;
  return TURBO_OK;
}
