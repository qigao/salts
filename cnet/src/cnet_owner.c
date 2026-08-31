#include "cnet_owner.h"

#include <turbo/error_codes.h>

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef enum cnet_owner_request_role {
  CNET_OWNER_REQUEST_NONE = 0,
  CNET_OWNER_REQUEST_CONNECT,
  CNET_OWNER_REQUEST_SEND,
  CNET_OWNER_REQUEST_RECEIVE
} cnet_owner_request_role;

typedef struct cnet_owner_session {
  cnet_session_handle handle;
  cnet_owner_connect_payload peer;
  cnet_transport transport;
  size_t active_requests;
  int pending_status;
  cnet_session_stage pending_stage;
  unsigned char *receive_buffer;
  size_t receive_demand;
  bool occupied;
  bool close_requested;
  bool read_active;
} cnet_owner_session;

typedef struct cnet_owner_request {
  native_io_request request;
  cnet_session_handle session;
  cnet_command_view command;
  cnet_owner_request_role role;
  bool active;
} cnet_owner_request;

typedef struct cnet_owner_pending_event {
  cnet_event event;
} cnet_owner_pending_event;

typedef struct cnet_owner_impl {
  native_io_backend backend;
  native_io_backend_kind backend_kind;
  cnet_session_table *sessions;
  cnet_command_queue *commands;
  cnet_event_queue *events;
  cnet_owner_session *session_records;
  cnet_owner_request *request_records;
  cnet_owner_pending_event *pending_events;
  unsigned char *receive_storage;
  native_io_completion *completions;
  size_t connection_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t pending_event_capacity;
  size_t pending_event_count;
  size_t receive_buffer_bytes;
  size_t occupied_sessions;
  size_t active_requests;
  bool closed;
} cnet_owner_impl;

static cnet_owner_impl *cnet_owner_get(cnet_owner *owner) {
  return owner != NULL ? (cnet_owner_impl *)owner->impl : NULL;
}

static cnet_owner_session *cnet_owner_find_session(cnet_owner_impl *impl,
                                                   cnet_session_handle handle) {
  cnet_owner_session *record;
  if (impl == NULL || !cnet_session_handle_valid(handle) ||
      (size_t)handle.slot > impl->connection_capacity)
    return NULL;
  record = &impl->session_records[handle.slot - 1u];
  return record->occupied && record->handle.slot == handle.slot &&
                 record->handle.generation == handle.generation
             ? record
             : NULL;
}

static cnet_owner_request *cnet_owner_find_request(cnet_owner_impl *impl,
                                                   native_io_request handle) {
  cnet_owner_request *record;
  if (!native_io_request_valid(handle) || (size_t)handle.slot > impl->request_capacity) return NULL;
  record = &impl->request_records[handle.slot - 1u];
  return record->active && record->request.slot == handle.slot &&
                 record->request.generation == handle.generation
             ? record
             : NULL;
}

static void cnet_owner_record_failure(cnet_owner_session *session, int status,
                                      cnet_session_stage stage) {
  if (session->pending_status == TURBO_OK && status < TURBO_OK) {
    session->pending_status = status;
    session->pending_stage = stage;
  }
}

static int cnet_owner_queue_state_event(cnet_owner_impl *impl, cnet_session_handle session,
                                        cnet_event_state state, int status,
                                        cnet_session_stage stage) {
  const cnet_event event = {CNET_EVENT_STATE, session, state, status, stage, NULL, 0u};
  int publish_status;

  if (impl->pending_event_count == 0u) {
    publish_status = cnet_event_queue_publish(impl->events, &event);
    if (publish_status == TURBO_OK) return TURBO_OK;
    if (publish_status != TURBO_ENOBUFS) return publish_status;
  }
  if (impl->pending_event_count == impl->pending_event_capacity) return TURBO_ENOBUFS;
  impl->pending_events[impl->pending_event_count++].event = event;
  return TURBO_OK;
}

static int cnet_owner_store_request(cnet_owner_impl *impl, native_io_request request_handle,
                                    cnet_session_handle session_handle, cnet_command_view *command,
                                    cnet_owner_request_role role);
static int cnet_owner_arm_receive(cnet_owner_impl *impl, cnet_owner_session *session);

static int cnet_owner_flush_state_events(cnet_owner_impl *impl, bool *out_blocked) {
  size_t published = 0u;

  *out_blocked = false;
  while (published < impl->pending_event_count) {
    const int status =
        cnet_event_queue_publish(impl->events, &impl->pending_events[published].event);
    if (status == TURBO_ENOBUFS) {
      *out_blocked = true;
      break;
    }
    if (status != TURBO_OK) return status;
    ++published;
  }
  if (published != 0u) {
    impl->pending_event_count -= published;
    if (impl->pending_event_count != 0u)
      memmove(impl->pending_events, &impl->pending_events[published],
              impl->pending_event_count * sizeof(*impl->pending_events));
  }
  return TURBO_OK;
}

static int cnet_owner_arm_all_receives(cnet_owner_impl *impl) {
  size_t index;
  for (index = 0u; index < impl->connection_capacity; ++index) {
    cnet_owner_session *session = &impl->session_records[index];
    int status;
    if (!session->occupied || session->receive_demand == 0u || session->read_active ||
        session->close_requested)
      continue;
    status = cnet_owner_arm_receive(impl, session);
    if (status != TURBO_OK) return status;
  }
  return TURBO_OK;
}

static int cnet_owner_cancel_session_requests(cnet_owner_impl *impl, cnet_session_handle session) {
  size_t index;
  int first_error = TURBO_OK;
  for (index = 0u; index < impl->request_capacity; ++index) {
    cnet_owner_request *request = &impl->request_records[index];
    int status;
    if (!request->active || request->session.slot != session.slot ||
        request->session.generation != session.generation)
      continue;
    status = native_io_backend_cancel(&impl->backend, request->request);
    if (status != TURBO_OK && status != TURBO_EALREADY && first_error == TURBO_OK)
      first_error = status;
  }
  return first_error;
}

static int cnet_owner_finalize_session(cnet_owner_impl *impl, cnet_owner_session *session) {
  int close_status;
  int status;
  if (session->active_requests != 0u ||
      (!session->close_requested && session->pending_status == TURBO_OK))
    return TURBO_OK;
  if (session->transport.native_open || session->transport.attached) {
    close_status = cnet_transport_close(&session->transport, &impl->backend);
    if (close_status != TURBO_OK) {
      cnet_owner_record_failure(session, close_status, CNET_SESSION_STAGE_SHUTDOWN);
      return close_status;
    }
  }
  if (session->pending_status != TURBO_OK) {
    status = cnet_session_table_fail(impl->sessions, session->handle, session->pending_status,
                                     session->pending_stage);
    if (status != TURBO_OK) return status;
    return cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_FAILED,
                                        session->pending_status, session->pending_stage);
  }
  status = cnet_session_table_finish_close(impl->sessions, session->handle);
  if (status != TURBO_OK) return status;
  return cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSED, TURBO_OK,
                                      CNET_SESSION_STAGE_NONE);
}

static int cnet_owner_store_request(cnet_owner_impl *impl, native_io_request request_handle,
                                    cnet_session_handle session_handle, cnet_command_view *command,
                                    cnet_owner_request_role role) {
  cnet_owner_request *record;
  cnet_owner_session *session = cnet_owner_find_session(impl, session_handle);
  if (session == NULL || !native_io_request_valid(request_handle) ||
      (size_t)request_handle.slot > impl->request_capacity)
    return TURBO_EPROTO;
  record = &impl->request_records[request_handle.slot - 1u];
  if (record->active) return TURBO_EPROTO;
  *record = (cnet_owner_request){
      .request = request_handle, .session = session_handle, .role = role, .active = true};
  if (command != NULL) {
    record->command = *command;
    memset(command, 0, sizeof(*command));
  }
  ++session->active_requests;
  ++impl->active_requests;
  if (role == CNET_OWNER_REQUEST_RECEIVE) session->read_active = true;
  return TURBO_OK;
}

static int cnet_owner_arm_receive(cnet_owner_impl *impl, cnet_owner_session *session) {
  native_io_operation operation;
  native_io_operation_kind operation_kind;
  native_io_request request = {0};
  int status;

  if (session->receive_demand == 0u || session->read_active || session->close_requested)
    return TURBO_OK;
  operation_kind = session->peer.scheme == CNET_URI_TCP   ? NATIVE_IO_OPERATION_TCP_RECV
                   : session->peer.scheme == CNET_URI_UDP ? NATIVE_IO_OPERATION_UDP_RECV_FROM
                                                          : NATIVE_IO_OPERATION_PIPE_READ;
  operation = (native_io_operation){.kind = operation_kind,
                                    .endpoint = cnet_transport_read_endpoint(&session->transport),
                                    .buffer = session->receive_buffer,
                                    .length = impl->receive_buffer_bytes};
  status = native_io_backend_submit(&impl->backend, &operation, &request);
  if (status != TURBO_OK) {
    cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_READ);
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != TURBO_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
    return cnet_owner_finalize_session(impl, session);
  }
  status =
      cnet_owner_store_request(impl, request, session->handle, NULL, CNET_OWNER_REQUEST_RECEIVE);
  if (status != TURBO_OK) return status;
  --session->receive_demand;
  return TURBO_OK;
}

static int cnet_owner_fail_accepted_command(cnet_owner_impl *impl, cnet_owner_session *session,
                                            cnet_command_view *command, int status,
                                            cnet_session_stage stage) {
  int release_status = cnet_command_queue_release(impl->commands, command);
  if (release_status != TURBO_OK) return release_status;
  cnet_owner_record_failure(session, status, stage);
  if (session->active_requests != 0u) {
    const int cancel_status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (cancel_status != TURBO_OK)
      cnet_owner_record_failure(session, cancel_status, CNET_SESSION_STAGE_SHUTDOWN);
    return TURBO_OK;
  }
  return cnet_owner_finalize_session(impl, session);
}

static int cnet_owner_connect(cnet_owner_impl *impl, cnet_command_view *command) {
  const cnet_owner_connect_payload *payload;
  const cnet_session_handle handle = command->connection;
  cnet_owner_session *session;
  native_io_request request = {0};
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (command->size != sizeof(cnet_owner_connect_payload) || command->data == NULL) {
    status = cnet_command_queue_release(impl->commands, command);
    if (status != TURBO_OK) return status;
    return cnet_session_table_fail(impl->sessions, handle, TURBO_EINVAL,
                                   CNET_SESSION_STAGE_CONNECT);
  }
  payload = (const cnet_owner_connect_payload *)command->data;
  if ((payload->scheme != CNET_URI_TCP && payload->scheme != CNET_URI_UDP &&
       payload->scheme != CNET_URI_PIPE) ||
      (payload->scheme == CNET_URI_PIPE
           ? payload->address_length != 0u || payload->pipe_read_handle == UINTPTR_MAX ||
                 payload->pipe_write_handle == UINTPTR_MAX
           : payload->address_length == 0u ||
                 payload->address_length > CNET_OWNER_ADDRESS_CAPACITY)) {
    status = cnet_command_queue_release(impl->commands, command);
    if (status != TURBO_OK) return status;
    return cnet_session_table_fail(impl->sessions, handle, TURBO_EINVAL,
                                   CNET_SESSION_STAGE_CONNECT);
  }
  if ((size_t)command->connection.slot > impl->connection_capacity) {
    (void)cnet_command_queue_release(impl->commands, command);
    return TURBO_ENOBUFS;
  }
  session = &impl->session_records[command->connection.slot - 1u];
  if (session->occupied) {
    (void)cnet_command_queue_release(impl->commands, command);
    return TURBO_EPROTO;
  }
  status = cnet_session_table_state(impl->sessions, command->connection, &state);
  if (status != TURBO_OK || state != CNET_SESSION_RESERVED) {
    (void)cnet_command_queue_release(impl->commands, command);
    return status != TURBO_OK ? status : TURBO_EPROTO;
  }

  memset(session, 0, sizeof(*session));
  session->handle = command->connection;
  session->peer = *payload;
  session->receive_buffer =
      &impl->receive_storage[(size_t)(session->handle.slot - 1u) * impl->receive_buffer_bytes];
  session->transport.native_handle = UINTPTR_MAX;
  session->occupied = true;
  ++impl->occupied_sessions;
  status = cnet_session_table_transition(impl->sessions, session->handle,
                                         CNET_SESSION_TRANSPORT_CONNECTING);
  if (status != TURBO_OK)
    return cnet_owner_fail_accepted_command(impl, session, command, status,
                                            CNET_SESSION_STAGE_CONNECT);

  if (payload->scheme != CNET_URI_TCP) {
    status =
        payload->scheme == CNET_URI_UDP
            ? cnet_transport_udp_connect(&session->transport, &impl->backend, impl->backend_kind,
                                         session->peer.address, session->peer.address_length)
            : cnet_transport_adopt_pipe(&session->transport, &impl->backend,
                                        session->peer.pipe_read_handle,
                                        session->peer.pipe_write_handle);
    if (status == TURBO_OK)
      status = cnet_session_table_transition(impl->sessions, session->handle, CNET_SESSION_OPEN);
    if (status != TURBO_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              CNET_SESSION_STAGE_CONNECT);
    status = cnet_command_queue_release(impl->commands, command);
    if (status != TURBO_OK) return status;
    return cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CONNECTED, TURBO_OK,
                                        CNET_SESSION_STAGE_NONE);
  }

  status =
      cnet_transport_tcp_connect(&session->transport, &impl->backend, impl->backend_kind,
                                 session->peer.address, session->peer.address_length, 0u, &request);
  if (status != TURBO_OK)
    return cnet_owner_fail_accepted_command(impl, session, command, status,
                                            CNET_SESSION_STAGE_CONNECT);
  return cnet_owner_store_request(impl, request, session->handle, command,
                                  CNET_OWNER_REQUEST_CONNECT);
}

static int cnet_owner_send(cnet_owner_impl *impl, cnet_command_view *command) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  native_io_operation operation;
  native_io_operation_kind operation_kind;
  native_io_request request = {0};
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (session == NULL) return cnet_command_queue_release(impl->commands, command);
  status = cnet_session_table_state(impl->sessions, command->connection, &state);
  if (status != TURBO_OK || state != CNET_SESSION_OPEN)
    return cnet_command_queue_release(impl->commands, command);
  operation_kind = session->peer.scheme == CNET_URI_TCP   ? NATIVE_IO_OPERATION_TCP_SEND
                   : session->peer.scheme == CNET_URI_UDP ? NATIVE_IO_OPERATION_UDP_SEND_TO
                                                          : NATIVE_IO_OPERATION_PIPE_WRITE;
  operation = (native_io_operation){.kind = operation_kind,
                                    .endpoint = cnet_transport_write_endpoint(&session->transport),
                                    .buffer = (void *)command->data,
                                    .length = command->size};
  status = native_io_backend_submit(&impl->backend, &operation, &request);
  if (status != TURBO_OK)
    return cnet_owner_fail_accepted_command(impl, session, command, status,
                                            CNET_SESSION_STAGE_WRITE);
  return cnet_owner_store_request(impl, request, session->handle, command, CNET_OWNER_REQUEST_SEND);
}

static int cnet_owner_receive(cnet_owner_impl *impl, cnet_command_view *command) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (session == NULL) return cnet_command_queue_release(impl->commands, command);
  status = cnet_session_table_state(impl->sessions, command->connection, &state);
  if (status != TURBO_OK || state != CNET_SESSION_OPEN || session->close_requested)
    return cnet_command_queue_release(impl->commands, command);
  if (command->argument > SIZE_MAX - session->receive_demand)
    return cnet_owner_fail_accepted_command(impl, session, command, TURBO_ERANGE,
                                            CNET_SESSION_STAGE_READ);
  session->receive_demand += command->argument;
  status = cnet_command_queue_release(impl->commands, command);
  if (status != TURBO_OK) return status;
  return cnet_owner_arm_receive(impl, session);
}

static int cnet_owner_close_session(cnet_owner_impl *impl, cnet_command_view *command) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  int status = cnet_command_queue_release(impl->commands, command);
  if (status != TURBO_OK || session == NULL) return status;
  status = cnet_session_table_begin_close(impl->sessions, session->handle);
  if (status == TURBO_EALREADY) return TURBO_OK;
  if (status != TURBO_OK) return status;
  session->close_requested = true;
  session->receive_demand = 0u;
  status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING, TURBO_OK,
                                        CNET_SESSION_STAGE_NONE);
  if (status != TURBO_OK) return status;
  if (session->active_requests != 0u) {
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != TURBO_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
    return TURBO_OK;
  }
  return cnet_owner_finalize_session(impl, session);
}

static int cnet_owner_process_commands(cnet_owner_impl *impl, size_t *out_processed) {
  for (;;) {
    cnet_command_view command = {0};
    int status = cnet_command_queue_take(impl->commands, &command);
    if (status == TURBO_ETIMEDOUT || status == TURBO_EOF) return TURBO_OK;
    if (status != TURBO_OK) return status;
    ++*out_processed;
    if (command.kind == CNET_COMMAND_CONNECT) status = cnet_owner_connect(impl, &command);
    else if (command.kind == CNET_COMMAND_SEND) status = cnet_owner_send(impl, &command);
    else if (command.kind == CNET_COMMAND_RECEIVE) status = cnet_owner_receive(impl, &command);
    else if (command.kind == CNET_COMMAND_CLOSE) status = cnet_owner_close_session(impl, &command);
    else status = cnet_command_queue_release(impl->commands, &command);
    if (status != TURBO_OK) return status;
    if (impl->pending_event_count != 0u) return TURBO_OK;
  }
}

static int cnet_owner_complete(cnet_owner_impl *impl, const native_io_completion *completion) {
  cnet_owner_request *request = cnet_owner_find_request(impl, completion->request);
  cnet_owner_session *session;
  cnet_owner_request_role role;
  int status;
  if (request == NULL) return TURBO_EPROTO;
  session = cnet_owner_find_session(impl, request->session);
  if (session == NULL || session->active_requests == 0u || impl->active_requests == 0u)
    return TURBO_EPROTO;
  role = request->role;
  if (request->command._sequence != 0u) {
    status = cnet_command_queue_release(impl->commands, &request->command);
    if (status != TURBO_OK) return status;
  }
  request->active = false;
  request->role = CNET_OWNER_REQUEST_NONE;
  --session->active_requests;
  --impl->active_requests;
  if (role == CNET_OWNER_REQUEST_RECEIVE) session->read_active = false;

  if (role == CNET_OWNER_REQUEST_CONNECT) {
    if (session->close_requested) return cnet_owner_finalize_session(impl, session);
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      status = cnet_session_table_transition(impl->sessions, session->handle, CNET_SESSION_OPEN);
      if (status != TURBO_OK) return status;
      return cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CONNECTED,
                                          TURBO_OK, CNET_SESSION_STAGE_NONE);
    }
    cnet_owner_record_failure(session,
                              completion->status < TURBO_OK ? completion->status : TURBO_EIO,
                              CNET_SESSION_STAGE_CONNECT);
    return cnet_owner_finalize_session(impl, session);
  }

  if (role == CNET_OWNER_REQUEST_RECEIVE) {
    if (session->close_requested) return cnet_owner_finalize_session(impl, session);
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      const cnet_event event = {
          CNET_EVENT_RECEIVE,      session->handle,         CNET_EVENT_STATE_NONE, TURBO_OK,
          CNET_SESSION_STAGE_NONE, session->receive_buffer, completion->bytes};
      status = cnet_event_queue_publish(impl->events, &event);
      if (status == TURBO_ENOBUFS) {
        if (impl->pending_event_count == impl->pending_event_capacity) return TURBO_ENOBUFS;
        impl->pending_events[impl->pending_event_count++].event = event;
        return TURBO_OK;
      }
      if (status != TURBO_OK) return status;
      return cnet_owner_arm_receive(impl, session);
    }
    if (completion->kind == NATIVE_IO_COMPLETION_EOF) {
      status = cnet_session_table_begin_close(impl->sessions, session->handle);
      if (status != TURBO_OK && status != TURBO_EALREADY) return status;
      session->close_requested = true;
      session->receive_demand = 0u;
      if (status == TURBO_OK) {
        status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING,
                                              TURBO_OK, CNET_SESSION_STAGE_NONE);
        if (status != TURBO_OK) return status;
      }
      return cnet_owner_finalize_session(impl, session);
    }
    if (completion->kind == NATIVE_IO_COMPLETION_CANCELLED)
      return cnet_owner_finalize_session(impl, session);
    cnet_owner_record_failure(session,
                              completion->status < TURBO_OK ? completion->status : TURBO_EIO,
                              CNET_SESSION_STAGE_READ);
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != TURBO_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
    return cnet_owner_finalize_session(impl, session);
  }

  if (completion->kind != NATIVE_IO_COMPLETION_OK && !session->close_requested) {
    cnet_owner_record_failure(session,
                              completion->status < TURBO_OK ? completion->status : TURBO_EIO,
                              CNET_SESSION_STAGE_WRITE);
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != TURBO_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
  }
  return cnet_owner_finalize_session(impl, session);
}

int cnet_owner_init(cnet_owner *owner, const cnet_owner_config *config) {
  cnet_owner_impl *impl;
  native_io_backend_config backend_config;
  cnet_event_queue_config event_config;
  int status;
  if (owner == NULL) return TURBO_EINVAL;
  if (owner->impl != NULL) return TURBO_EALREADY;
  if (config == NULL || config->sessions == NULL || config->commands == NULL ||
      config->events == NULL || config->connection_capacity == 0u ||
      config->request_capacity == 0u || config->completion_batch_capacity == 0u ||
      config->receive_buffer_bytes == 0u ||
      config->receive_buffer_count != config->connection_capacity ||
      config->completion_batch_capacity > config->request_capacity ||
      config->connection_capacity > UINT32_MAX / 2u || config->request_capacity > UINT32_MAX)
    return TURBO_EINVAL;
  if (!cnet_event_queue_get_config(config->events, &event_config) ||
      config->receive_buffer_bytes > event_config.max_payload_bytes)
    return TURBO_EINVAL;
  if (config->completion_batch_capacity > SIZE_MAX - 2u ||
      config->connection_capacity > SIZE_MAX / sizeof(cnet_owner_session) ||
      config->connection_capacity > SIZE_MAX / config->receive_buffer_bytes ||
      config->request_capacity > SIZE_MAX / sizeof(cnet_owner_request) ||
      config->completion_batch_capacity > SIZE_MAX / sizeof(native_io_completion) ||
      config->completion_batch_capacity + 2u > SIZE_MAX / sizeof(cnet_owner_pending_event))
    return TURBO_ERANGE;
  impl = (cnet_owner_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->session_records =
      (cnet_owner_session *)calloc(config->connection_capacity, sizeof(*impl->session_records));
  impl->request_records =
      (cnet_owner_request *)calloc(config->request_capacity, sizeof(*impl->request_records));
  impl->pending_events = (cnet_owner_pending_event *)calloc(config->completion_batch_capacity + 2u,
                                                            sizeof(*impl->pending_events));
  impl->receive_storage =
      (unsigned char *)calloc(config->connection_capacity, config->receive_buffer_bytes);
  impl->completions =
      (native_io_completion *)calloc(config->completion_batch_capacity, sizeof(*impl->completions));
  if (impl->session_records == NULL || impl->request_records == NULL ||
      impl->pending_events == NULL || impl->receive_storage == NULL || impl->completions == NULL) {
    free(impl->completions);
    free(impl->receive_storage);
    free(impl->pending_events);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return TURBO_ENOMEM;
  }
  backend_config =
      (native_io_backend_config){config->backend_kind, config->connection_capacity * 2u,
                                 config->request_capacity, config->completion_batch_capacity};
  status = native_io_backend_init(&impl->backend, &backend_config);
  if (status != TURBO_OK) {
    free(impl->completions);
    free(impl->receive_storage);
    free(impl->pending_events);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return status;
  }
  impl->backend_kind = config->backend_kind;
  impl->sessions = config->sessions;
  impl->commands = config->commands;
  impl->events = config->events;
  impl->connection_capacity = config->connection_capacity;
  impl->request_capacity = config->request_capacity;
  impl->completion_batch_capacity = config->completion_batch_capacity;
  impl->pending_event_capacity = config->completion_batch_capacity + 2u;
  impl->receive_buffer_bytes = config->receive_buffer_bytes;
  owner->impl = impl;
  return TURBO_OK;
}

int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  size_t processed = 0u;
  size_t completion_count = 0u;
  size_t index;
  bool event_blocked = false;
  int status;
  if (impl == NULL || impl->closed) return TURBO_EINVAL;
  status = cnet_owner_flush_state_events(impl, &event_blocked);
  if (status != TURBO_OK) return status;
  if (event_blocked) return TURBO_OK;
  status = cnet_owner_arm_all_receives(impl);
  if (status != TURBO_OK) return status;
  status = cnet_owner_process_commands(impl, &processed);
  if (status != TURBO_OK) return status;
  if (impl->pending_event_count != 0u) return TURBO_OK;
  status =
      native_io_backend_observe(&impl->backend, impl->completions, impl->completion_batch_capacity,
                                processed != 0u ? 0u : timeout_ms, &completion_count);
  if (status == TURBO_ETIMEDOUT) return TURBO_OK;
  if (status != TURBO_OK) return status;
  for (index = 0u; index < completion_count; ++index) {
    status = cnet_owner_complete(impl, &impl->completions[index]);
    if (status != TURBO_OK) return status;
  }
  return TURBO_OK;
}

int cnet_owner_wake(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  return impl != NULL ? native_io_backend_wake(&impl->backend) : TURBO_EINVAL;
}

int cnet_owner_release_session(cnet_owner *owner, cnet_session_handle session_handle) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session = cnet_owner_find_session(impl, session_handle);
  cnet_session_state state = CNET_SESSION_FREE;
  int status;
  if (session == NULL) return TURBO_ENOENT;
  if (session->active_requests != 0u || cnet_transport_active(&session->transport))
    return TURBO_EBUSY;
  status = cnet_session_table_state(impl->sessions, session_handle, &state);
  if (status != TURBO_ENOENT) return TURBO_EBUSY;
  memset(session, 0, sizeof(*session));
  --impl->occupied_sessions;
  return TURBO_OK;
}

int cnet_owner_close(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  int status;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->closed) return TURBO_EALREADY;
  if (impl->occupied_sessions != 0u || impl->active_requests != 0u ||
      impl->pending_event_count != 0u)
    return TURBO_EBUSY;
  status = native_io_backend_close(&impl->backend);
  if (status == TURBO_OK) impl->closed = true;
  return status;
}

int cnet_owner_destroy(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  int status;
  if (owner == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!impl->closed || impl->occupied_sessions != 0u || impl->active_requests != 0u)
    return TURBO_EBUSY;
  status = native_io_backend_destroy(&impl->backend);
  if (status != TURBO_OK) return status;
  free(impl->completions);
  free(impl->receive_storage);
  free(impl->pending_events);
  free(impl->request_records);
  free(impl->session_records);
  free(impl);
  owner->impl = NULL;
  return TURBO_OK;
}
