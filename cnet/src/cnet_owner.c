#include "cnet_owner.h"

#include <salts/clock.h>
#include <salts/deadline_queue.h>
#include <salts/error_codes.h>

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
#else
  #include <sys/socket.h>
#endif

#define CNET_OWNER_SESSION_DEADLINE_TOKEN (UINT64_C(1) << 63u)

typedef enum cnet_owner_request_role {
  CNET_OWNER_REQUEST_NONE = 0,
  CNET_OWNER_REQUEST_CONNECT,
  CNET_OWNER_REQUEST_SEND,
  CNET_OWNER_REQUEST_RECEIVE,
  CNET_OWNER_REQUEST_TLS_READ,
  CNET_OWNER_REQUEST_TLS_WRITE
} cnet_owner_request_role;

typedef struct cnet_owner_impl cnet_owner_impl;

typedef struct cnet_owner_session {
  cnet_session_handle handle;
  cnet_owner_connect_payload peer;
  cnet_transport transport;
  cnet_tls_state tls;
  cnet_command_view tls_send_command;
  cnet_resolver_query resolve_query;
  size_t active_requests;
  int pending_status;
  cnet_session_stage pending_stage;
  cnet_session_stage session_deadline_stage;
  unsigned char *receive_buffer;
  size_t receive_demand;
  bool occupied;
  bool close_requested;
  bool read_active;
  bool write_active;
  bool resolve_active;
  bool tls_send_accepted;
  bool tls_close_after_send;
  bool tls_shutdown_after_flush;
  salts_deadline_id connect_deadline;
} cnet_owner_session;

typedef struct cnet_owner_request {
  cnet_owner_impl *owner;
  native_io_coroutine_task coroutine;
  native_io_operation operation;
  size_t requested_size;
  size_t completed_size;
  cnet_session_handle session;
  cnet_command_view command;
  cnet_owner_request_role role;
  cnet_session_stage stage;
  bool active;
  bool close_after_send;
  salts_deadline_id deadline;
} cnet_owner_request;

typedef struct cnet_owner_pending_event {
  cnet_event event;
} cnet_owner_pending_event;

enum { CNET_OWNER_RESOLVER_POLL_INTERVAL_MS = 1u };

struct cnet_owner_impl {
  native_io_backend backend;
  cnet_resolver resolver;
  salts_deadline_queue deadlines;
  native_io_backend_kind backend_kind;
  cnet_session_table *sessions;
  cnet_command_queue *commands;
  cnet_event_queue *events;
  cnet_owner_event_publish_fn publish_event;
  void *event_context;
  cnet_owner_session *session_records;
  cnet_owner_request *request_records;
  uint32_t *free_requests;
  cnet_session_handle *receive_rearms;
  cnet_owner_pending_event *pending_events;
  unsigned char *receive_storage;
  native_io_completion *completions;
  size_t connection_capacity;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t pending_event_capacity;
  size_t pending_event_count;
  uint64_t published_event_count;
  size_t receive_buffer_bytes;
  size_t occupied_sessions;
  size_t active_requests;
  size_t free_request_count;
  size_t receive_rearm_head;
  size_t receive_rearm_count;
  cnet_owner_now_ms_fn now_ms;
  void *clock_context;
  bool closed;
  bool resolver_closed;
  int coroutine_status;
};

static uint64_t cnet_owner_system_now(void *context) {
  (void)context;
  return salts_monotonic_ms();
}

static uint64_t cnet_owner_deadline_after(cnet_owner_impl *impl, uint32_t timeout_ms) {
  const uint64_t now_ms = impl->now_ms(impl->clock_context);
  return timeout_ms > UINT64_MAX - now_ms ? UINT64_MAX : now_ms + timeout_ms;
}

static cnet_session_stage cnet_owner_request_stage(const cnet_owner_session *session,
                                                   cnet_owner_request_role role) {
  if (role == CNET_OWNER_REQUEST_RECEIVE) return CNET_SESSION_STAGE_READ;
  if (role == CNET_OWNER_REQUEST_TLS_READ)
    return session->tls.handshake_complete ? CNET_SESSION_STAGE_READ : CNET_SESSION_STAGE_HANDSHAKE;
  if (role == CNET_OWNER_REQUEST_SEND) return CNET_SESSION_STAGE_WRITE;
  if (role == CNET_OWNER_REQUEST_TLS_WRITE)
    return session->tls.handshake_complete ? CNET_SESSION_STAGE_WRITE
                                           : CNET_SESSION_STAGE_HANDSHAKE;
  return CNET_SESSION_STAGE_CONNECT;
}

static uint32_t cnet_owner_request_timeout(const cnet_owner_session *session,
                                           cnet_owner_request_role role) {
  if (role == CNET_OWNER_REQUEST_RECEIVE) return session->peer.read_timeout_ms;
  if (role == CNET_OWNER_REQUEST_SEND) return session->peer.write_timeout_ms;
  if (role == CNET_OWNER_REQUEST_TLS_READ)
    return session->tls.handshake_complete ? session->peer.read_timeout_ms : 0u;
  if (role == CNET_OWNER_REQUEST_TLS_WRITE)
    return session->tls.handshake_complete ? session->peer.write_timeout_ms : 0u;
  return 0u;
}

static int cnet_owner_cancel_deadline(cnet_owner_impl *impl, salts_deadline_id *deadline) {
  salts_deadline_event discarded = {0};
  int status;
  if (*deadline == 0u) return SALTS_OK;
  status = salts_deadline_queue_cancel(&impl->deadlines, *deadline, &discarded);
  if (status == SALTS_OK) *deadline = 0u;
  return status == SALTS_ENOENT ? SALTS_EPROTO : status;
}

static cnet_owner_impl *cnet_owner_get(cnet_owner *owner) {
  return owner != NULL ? (cnet_owner_impl *)owner->impl : NULL;
}

static int cnet_owner_publish_event(cnet_owner_impl *impl, const cnet_event *event) {
  const int status = impl->publish_event != NULL ? impl->publish_event(impl->event_context, event)
                                                 : cnet_event_queue_publish(impl->events, event);
  if (status == SALTS_OK) ++impl->published_event_count;
  return status;
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

static void cnet_owner_record_failure(cnet_owner_session *session, int status,
                                      cnet_session_stage stage) {
  if (session->pending_status == SALTS_OK && status < SALTS_OK) {
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
    publish_status = cnet_owner_publish_event(impl, &event);
    if (publish_status == SALTS_OK) return SALTS_OK;
    if (publish_status != SALTS_ENOBUFS) return publish_status;
  }
  if (impl->pending_event_count == impl->pending_event_capacity) return SALTS_ENOBUFS;
  impl->pending_events[impl->pending_event_count++].event = event;
  return SALTS_OK;
}

static int cnet_owner_queue_connected_event(cnet_owner_impl *impl, cnet_owner_session *session) {
  const unsigned char *alpn = NULL;
  size_t alpn_size = 0u;
  cnet_event event;
  int status;
  if (session->peer.scheme == CNET_URI_TLS) {
    status = cnet_tls_get_negotiated_alpn(&session->tls, &alpn, &alpn_size);
    if (status != SALTS_OK && status != SALTS_ENOENT) return status;
  }
  event = (cnet_event){CNET_EVENT_STATE,
                       session->handle,
                       CNET_EVENT_STATE_CONNECTED,
                       SALTS_OK,
                       CNET_SESSION_STAGE_NONE,
                       alpn,
                       alpn_size,
                       0u};
  if (impl->pending_event_count == 0u) {
    status = cnet_owner_publish_event(impl, &event);
    if (status == SALTS_OK) return SALTS_OK;
    if (status != SALTS_ENOBUFS) return status;
  }
  if (impl->pending_event_count == impl->pending_event_capacity) return SALTS_ENOBUFS;
  impl->pending_events[impl->pending_event_count++].event = event;
  return SALTS_OK;
}

static int cnet_owner_start_request(cnet_owner_impl *impl, cnet_owner_session *session,
                                    cnet_command_view *command, cnet_owner_request_role role,
                                    const native_io_operation *operation, bool close_after_send);
static int cnet_owner_arm_receive(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_tls_pump(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_complete(cnet_owner_impl *impl, cnet_owner_request *request,
                               const native_io_completion *completion);
static void cnet_owner_coroutine_entry(native_io_coroutine *coroutine, void *user_data);

static int cnet_owner_flush_state_events(cnet_owner_impl *impl, bool *out_blocked) {
  size_t published = 0u;

  *out_blocked = false;
  while (published < impl->pending_event_count) {
    const int status = cnet_owner_publish_event(impl, &impl->pending_events[published].event);
    if (status == SALTS_ENOBUFS) {
      *out_blocked = true;
      break;
    }
    if (status != SALTS_OK) return status;
    ++published;
  }
  if (published != 0u) {
    impl->pending_event_count -= published;
    if (impl->pending_event_count != 0u)
      memmove(impl->pending_events, &impl->pending_events[published],
              impl->pending_event_count * sizeof(*impl->pending_events));
  }
  return SALTS_OK;
}

static int cnet_owner_queue_receive_rearm(cnet_owner_impl *impl, cnet_session_handle session) {
  size_t tail;
  /* One session can own only one active read, so it can contribute at most one
   * handle before this owner-local queue is drained. */
  if (impl->receive_rearm_count == impl->connection_capacity) return SALTS_EPROTO;
  tail = (impl->receive_rearm_head + impl->receive_rearm_count) % impl->connection_capacity;
  impl->receive_rearms[tail] = session;
  ++impl->receive_rearm_count;
  return SALTS_OK;
}

static int cnet_owner_arm_pending_receives(cnet_owner_impl *impl) {
  while (impl->receive_rearm_count != 0u) {
    const cnet_session_handle handle = impl->receive_rearms[impl->receive_rearm_head];
    cnet_owner_session *session;
    int status;
    impl->receive_rearms[impl->receive_rearm_head] = (cnet_session_handle){0};
    impl->receive_rearm_head = (impl->receive_rearm_head + 1u) % impl->connection_capacity;
    --impl->receive_rearm_count;
    session = cnet_owner_find_session(impl, handle);
    if (session == NULL) return SALTS_EPROTO;
    if (session->receive_demand == 0u || session->read_active || session->close_requested) continue;
    status = cnet_owner_arm_receive(impl, session);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int cnet_owner_cancel_session_requests(cnet_owner_impl *impl, cnet_session_handle session) {
  size_t index;
  int first_error = SALTS_OK;
  for (index = 0u; index < impl->request_capacity; ++index) {
    cnet_owner_request *request = &impl->request_records[index];
    int status;
    if (!request->active || request->session.slot != session.slot ||
        request->session.generation != session.generation)
      continue;
    status = native_io_backend_cancel_coroutine(&impl->backend, request->coroutine);
    if (status != SALTS_OK && status != SALTS_EALREADY && first_error == SALTS_OK)
      first_error = status;
  }
  return first_error;
}

static int cnet_owner_cancel_receive_requests(cnet_owner_impl *impl, cnet_session_handle session) {
  size_t index;
  int first_error = SALTS_OK;
  for (index = 0u; index < impl->request_capacity; ++index) {
    cnet_owner_request *request = &impl->request_records[index];
    int status;
    if (!request->active ||
        (request->role != CNET_OWNER_REQUEST_RECEIVE &&
         request->role != CNET_OWNER_REQUEST_TLS_READ) ||
        request->session.slot != session.slot || request->session.generation != session.generation)
      continue;
    status = native_io_backend_cancel_coroutine(&impl->backend, request->coroutine);
    if (status != SALTS_OK && status != SALTS_EALREADY && first_error == SALTS_OK)
      first_error = status;
  }
  return first_error;
}

static int cnet_owner_release_tls_send(cnet_owner_impl *impl, cnet_owner_session *session) {
  int status;
  if (session->tls_send_command._sequence == 0u) return SALTS_OK;
  status = cnet_command_queue_release(impl->commands, &session->tls_send_command);
  if (status == SALTS_OK) {
    session->tls_send_accepted = false;
    session->tls_close_after_send = false;
  }
  return status;
}

static int cnet_owner_finalize_session(cnet_owner_impl *impl, cnet_owner_session *session) {
  int close_status;
  int status;
  if (session->pending_status != SALTS_OK) {
    status = cnet_owner_release_tls_send(impl, session);
    if (status != SALTS_OK) return status;
  }
  if (session->active_requests != 0u || session->resolve_active ||
      session->tls_send_command._sequence != 0u ||
      (!session->close_requested && session->pending_status == SALTS_OK))
    return SALTS_OK;
  status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
  if (status != SALTS_OK) return status;
  if (session->transport.native_open || session->transport.attached) {
    close_status = cnet_transport_close(&session->transport, &impl->backend);
    if (close_status != SALTS_OK) {
      cnet_owner_record_failure(session, close_status, CNET_SESSION_STAGE_SHUTDOWN);
      return close_status;
    }
  }
  cnet_tls_state_destroy(&session->tls);
  cnet_tls_context_release(session->peer.tls_context);
  session->peer.tls_context = NULL;
  if (session->pending_status != SALTS_OK) {
    status = cnet_session_table_fail(impl->sessions, session->handle, session->pending_status,
                                     session->pending_stage);
    if (status != SALTS_OK) return status;
    return cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_FAILED,
                                        session->pending_status, session->pending_stage);
  }
  status = cnet_session_table_finish_close(impl->sessions, session->handle);
  if (status != SALTS_OK) return status;
  return cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSED, SALTS_OK,
                                      CNET_SESSION_STAGE_NONE);
}

static cnet_owner_request *cnet_owner_acquire_request(cnet_owner_impl *impl) {
  uint32_t index;
  cnet_owner_request *request;
  if (impl->free_request_count == 0u) return NULL;
  index = impl->free_requests[--impl->free_request_count];
  if ((size_t)index >= impl->request_capacity) return NULL;
  request = &impl->request_records[index];
  if (request->active) return NULL;
  memset(request, 0, sizeof(*request));
  request->owner = impl;
  request->active = true;
  return request;
}

static int cnet_owner_release_request(cnet_owner_request *request) {
  cnet_owner_impl *impl;
  cnet_owner_session *session;
  cnet_owner_request_role role;
  size_t index;
  int status;
  if (request == NULL || !request->active || request->owner == NULL) return SALTS_EPROTO;
  impl = request->owner;
  session = cnet_owner_find_session(impl, request->session);
  if (session == NULL || session->active_requests == 0u || impl->active_requests == 0u ||
      impl->free_request_count >= impl->request_capacity)
    return SALTS_EPROTO;
  index = (size_t)(request - impl->request_records);
  if (index >= impl->request_capacity) return SALTS_EPROTO;
  role = request->role;
  status = cnet_owner_cancel_deadline(impl, &request->deadline);
  if (status != SALTS_OK) return status;
  if (request->command._sequence != 0u) {
    status = cnet_command_queue_release(impl->commands, &request->command);
    if (status != SALTS_OK) return status;
  }
  --session->active_requests;
  --impl->active_requests;
  if (role == CNET_OWNER_REQUEST_RECEIVE || role == CNET_OWNER_REQUEST_TLS_READ)
    session->read_active = false;
  if (role == CNET_OWNER_REQUEST_SEND || role == CNET_OWNER_REQUEST_TLS_WRITE)
    session->write_active = false;
  memset(request, 0, sizeof(*request));
  impl->free_requests[impl->free_request_count++] = (uint32_t)index;
  return SALTS_OK;
}

static int cnet_owner_take_coroutine_status(cnet_owner_impl *impl) {
  const int status = impl->coroutine_status;
  impl->coroutine_status = SALTS_OK;
  return status;
}

static int cnet_owner_arm_receive(cnet_owner_impl *impl, cnet_owner_session *session) {
  native_io_operation operation;
  native_io_operation_kind operation_kind;
  int status;

  if (session->receive_demand == 0u || session->read_active || session->close_requested)
    return SALTS_OK;
  if (session->peer.scheme == CNET_URI_TLS) return cnet_owner_tls_pump(impl, session);
  operation_kind = session->peer.scheme == CNET_URI_TCP   ? NATIVE_IO_OPERATION_TCP_RECV
                   : session->peer.scheme == CNET_URI_UDP ? NATIVE_IO_OPERATION_UDP_RECV_FROM
                                                          : NATIVE_IO_OPERATION_PIPE_READ;
  operation = (native_io_operation){.kind = operation_kind,
                                    .endpoint = cnet_transport_read_endpoint(&session->transport),
                                    .buffer = session->receive_buffer,
                                    .length = impl->receive_buffer_bytes};
  status =
      cnet_owner_start_request(impl, session, NULL, CNET_OWNER_REQUEST_RECEIVE, &operation, false);
  if (status != SALTS_OK) return status;
  if (session->read_active) --session->receive_demand;
  return SALTS_OK;
}

static int cnet_owner_fail_accepted_command(cnet_owner_impl *impl, cnet_owner_session *session,
                                            cnet_command_view *command, int status,
                                            cnet_session_stage stage) {
  int release_status = cnet_command_queue_release(impl->commands, command);
  if (release_status != SALTS_OK) return release_status;
  if (session->peer.adopted) {
    cnet_transport_close_socket(session->peer.adopted_socket);
    session->peer.adopted_socket = UINTPTR_MAX;
    session->peer.adopted = false;
  }
  cnet_owner_record_failure(session, status, stage);
  if (session->active_requests != 0u) {
    const int cancel_status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (cancel_status != SALTS_OK)
      cnet_owner_record_failure(session, cancel_status, CNET_SESSION_STAGE_SHUTDOWN);
    return SALTS_OK;
  }
  return cnet_owner_finalize_session(impl, session);
}

static int cnet_owner_fail_session(cnet_owner_impl *impl, cnet_owner_session *session, int status,
                                   cnet_session_stage stage) {
  cnet_owner_record_failure(session, status, stage);
  if (session->active_requests != 0u) {
    const int cancel_status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (cancel_status != SALTS_OK)
      cnet_owner_record_failure(session, cancel_status, CNET_SESSION_STAGE_SHUTDOWN);
    return SALTS_OK;
  }
  return cnet_owner_finalize_session(impl, session);
}

static int cnet_owner_fail_started_request(cnet_owner_request *request, int failure) {
  cnet_owner_impl *impl;
  cnet_owner_session *session;
  cnet_session_stage stage;
  int status;
  if (request == NULL || !request->active || request->owner == NULL) return SALTS_EPROTO;
  impl = request->owner;
  session = cnet_owner_find_session(impl, request->session);
  if (session == NULL) return SALTS_EPROTO;
  stage = request->stage;
  status = cnet_owner_release_request(request);
  if (status != SALTS_OK) return status;
  return cnet_owner_fail_session(impl, session, failure, stage);
}

static int cnet_owner_start_request(cnet_owner_impl *impl, cnet_owner_session *session,
                                    cnet_command_view *command, cnet_owner_request_role role,
                                    const native_io_operation *operation, bool close_after_send) {
  cnet_owner_request *request;
  const uint32_t timeout_ms = cnet_owner_request_timeout(session, role);
  const cnet_session_stage stage = cnet_owner_request_stage(session, role);
  size_t index;
  int status;

  request = cnet_owner_acquire_request(impl);
  if (request == NULL)
    return command != NULL
               ? cnet_owner_fail_accepted_command(impl, session, command, SALTS_ENOBUFS, stage)
               : cnet_owner_fail_session(impl, session, SALTS_ENOBUFS, stage);
  request->session = session->handle;
  request->operation = *operation;
  request->requested_size = operation->length;
  request->role = role;
  request->stage = stage;
  request->close_after_send = close_after_send;
  if (command != NULL) {
    request->command = *command;
    memset(command, 0, sizeof(*command));
  }
  ++session->active_requests;
  ++impl->active_requests;
  if (role == CNET_OWNER_REQUEST_RECEIVE || role == CNET_OWNER_REQUEST_TLS_READ)
    session->read_active = true;
  if (role == CNET_OWNER_REQUEST_SEND || role == CNET_OWNER_REQUEST_TLS_WRITE)
    session->write_active = true;
  status = native_io_backend_spawn_coroutine(&impl->backend, cnet_owner_coroutine_entry, request,
                                             &request->coroutine);
  if (status != SALTS_OK) {
    if (request->active) return cnet_owner_fail_started_request(request, status);
    return status;
  }
  if (!request->active) return cnet_owner_take_coroutine_status(impl);
  index = (size_t)(request - impl->request_records);
  if (timeout_ms != 0u) {
    status =
        salts_deadline_queue_schedule(&impl->deadlines, cnet_owner_deadline_after(impl, timeout_ms),
                                      index + 1u, &request->deadline);
    if (status != SALTS_OK) {
      cnet_owner_record_failure(session, status, stage);
      status = cnet_owner_cancel_session_requests(impl, session->handle);
      if (status != SALTS_OK)
        cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
      return status;
    }
  }
  return cnet_owner_take_coroutine_status(impl);
}

static int cnet_owner_tls_start_write(cnet_owner_impl *impl, cnet_owner_session *session,
                                      bool *out_started) {
  native_io_operation operation;
  size_t size = 0u;
  int status;
  if (out_started == NULL) return SALTS_EINVAL;
  *out_started = false;
  if (session->write_active) return SALTS_OK;
  status = cnet_tls_take_cipher(&session->tls, session->tls.write_buffer,
                                session->tls.io_buffer_bytes, &size);
  if (status == SALTS_ENOENT) return SALTS_OK;
  if (status != SALTS_OK) return status;
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_TCP_SEND,
                                    .endpoint = cnet_transport_write_endpoint(&session->transport),
                                    .buffer = session->tls.write_buffer,
                                    .length = size};
  status = cnet_owner_start_request(impl, session, NULL, CNET_OWNER_REQUEST_TLS_WRITE, &operation,
                                    false);
  if (status == SALTS_OK) *out_started = true;
  return status;
}

static int cnet_owner_tls_start_read(cnet_owner_impl *impl, cnet_owner_session *session) {
  native_io_operation operation;
  size_t capacity;
  if (session->read_active || session->close_requested) return SALTS_OK;
  capacity = cnet_tls_cipher_input_capacity(&session->tls);
  if (capacity == 0u) return SALTS_EPROTO;
  if (capacity > session->tls.io_buffer_bytes) capacity = session->tls.io_buffer_bytes;
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_TCP_RECV,
                                    .endpoint = cnet_transport_read_endpoint(&session->transport),
                                    .buffer = session->tls.read_buffer,
                                    .length = capacity};
  return cnet_owner_start_request(impl, session, NULL, CNET_OWNER_REQUEST_TLS_READ, &operation,
                                  false);
}

static int cnet_owner_tls_publish_send(cnet_owner_impl *impl, cnet_owner_session *session) {
  const size_t size = session->tls_send_command.size;
  const cnet_event event = {CNET_EVENT_SEND,
                            session->handle,
                            CNET_EVENT_STATE_NONE,
                            SALTS_OK,
                            CNET_SESSION_STAGE_NONE,
                            NULL,
                            0u,
                            size};
  int status = cnet_owner_release_tls_send(impl, session);
  if (status != SALTS_OK) return status;
  status = cnet_owner_publish_event(impl, &event);
  if (status == SALTS_ENOBUFS) {
    if (impl->pending_event_count == impl->pending_event_capacity) return SALTS_ENOBUFS;
    impl->pending_events[impl->pending_event_count++].event = event;
    return SALTS_OK;
  }
  return status;
}

static int cnet_owner_tls_pump(cnet_owner_impl *impl, cnet_owner_session *session) {
  bool started = false;
  int status;

  if (!session->tls.handshake_complete) {
    bool complete = false;
    status = cnet_tls_handshake(&session->tls, &complete);
    if (status != SALTS_OK) return status;
    status = cnet_owner_tls_start_write(impl, session, &started);
    if (status != SALTS_OK) return status;
    if (!complete) return cnet_owner_tls_start_read(impl, session);

    status = cnet_session_table_transition(impl->sessions, session->handle, CNET_SESSION_OPEN);
    if (status != SALTS_OK) return status;
    status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
    if (status != SALTS_OK) return status;
    session->session_deadline_stage = CNET_SESSION_STAGE_NONE;
    status = cnet_owner_queue_connected_event(impl, session);
    if (status != SALTS_OK) return status;
  }

  if (session->tls_send_command._sequence != 0u && !session->tls_send_accepted) {
    bool complete = false;
    status = cnet_tls_write(&session->tls, session->tls_send_command.data,
                            session->tls_send_command.size, &complete);
    if (status != SALTS_OK) return status;
    session->tls_send_accepted = complete;
  }
  status = cnet_owner_tls_start_write(impl, session, &started);
  if (status != SALTS_OK) return status;
  if (session->tls_send_command._sequence != 0u && session->tls_send_accepted &&
      !session->write_active && !started) {
    const bool close_after_send = session->tls_close_after_send;
    status = cnet_owner_tls_publish_send(impl, session);
    if (status != SALTS_OK) return status;
    if (close_after_send) session->tls_shutdown_after_flush = true;
  }

  if (!session->close_requested && session->receive_demand != 0u) {
    size_t plaintext_size = 0u;
    bool peer_closed = false;
    status = cnet_tls_read(&session->tls, session->receive_buffer, impl->receive_buffer_bytes,
                           &plaintext_size, &peer_closed);
    if (status != SALTS_OK) return status;
    status = cnet_owner_tls_start_write(impl, session, &started);
    if (status != SALTS_OK) return status;
    if (plaintext_size != 0u) {
      const cnet_event event = {
          CNET_EVENT_RECEIVE,      session->handle,         CNET_EVENT_STATE_NONE, SALTS_OK,
          CNET_SESSION_STAGE_NONE, session->receive_buffer, plaintext_size,        0u};
      --session->receive_demand;
      if (session->receive_demand != 0u) {
        status = cnet_owner_queue_receive_rearm(impl, session->handle);
        if (status != SALTS_OK) return status;
      }
      status = cnet_owner_publish_event(impl, &event);
      if (status == SALTS_ENOBUFS) {
        if (impl->pending_event_count == impl->pending_event_capacity) return SALTS_ENOBUFS;
        impl->pending_events[impl->pending_event_count++].event = event;
        return SALTS_OK;
      }
      return status;
    }
    if (peer_closed) {
      status = cnet_session_table_begin_close(impl->sessions, session->handle);
      if (status != SALTS_OK && status != SALTS_EALREADY) return status;
      session->close_requested = true;
      session->receive_demand = 0u;
      session->tls_shutdown_after_flush = true;
      if (status == SALTS_OK) {
        status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING,
                                              SALTS_OK, CNET_SESSION_STAGE_NONE);
        if (status != SALTS_OK) return status;
      }
    } else {
      return cnet_owner_tls_start_read(impl, session);
    }
  }

  if (session->close_requested && session->tls_send_command._sequence == 0u) {
    bool notify_generated = false;
    status = cnet_tls_shutdown(&session->tls, &notify_generated);
    if (status != SALTS_OK) return status;
    if (notify_generated) session->tls_shutdown_after_flush = true;
    status = cnet_owner_tls_start_write(impl, session, &started);
    if (status != SALTS_OK) return status;
    if (session->tls_shutdown_after_flush && !session->write_active && !started)
      return cnet_owner_finalize_session(impl, session);
  }
  return SALTS_OK;
}

static int cnet_owner_start_tls(cnet_owner_impl *impl, cnet_owner_session *session) {
  int status;
  if (session->peer.scheme != CNET_URI_TLS) return SALTS_EINVAL;

  status = cnet_session_table_transition(impl->sessions, session->handle,
                                         CNET_SESSION_PROTOCOL_HANDSHAKING);
  if (status == SALTS_OK) status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
  if (status == SALTS_OK)
    status = cnet_tls_state_init(&session->tls, session->peer.tls_context, session->peer.tls_server,
                                 session->peer.tls_server_name, session->peer.tls_io_buffer_bytes);
  if (status != SALTS_OK) return status;
  session->peer.tls_context = NULL;
  session->session_deadline_stage = CNET_SESSION_STAGE_HANDSHAKE;
  status = salts_deadline_queue_schedule(
      &impl->deadlines, cnet_owner_deadline_after(impl, session->peer.tls_handshake_timeout_ms),
      CNET_OWNER_SESSION_DEADLINE_TOKEN | (uint64_t)session->handle.slot,
      &session->connect_deadline);
  if (status != SALTS_OK) return status;
  return cnet_owner_tls_pump(impl, session);
}

static int cnet_owner_start_transport(cnet_owner_impl *impl, cnet_owner_session *session,
                                      cnet_command_view *command) {
  native_io_operation operation;
  int status;

  if (session->peer.scheme != CNET_URI_TCP && session->peer.scheme != CNET_URI_TLS) {
    status =
        session->peer.scheme == CNET_URI_UDP
            ? cnet_transport_udp_connect(&session->transport, &impl->backend, impl->backend_kind,
                                         session->peer.address, session->peer.address_length)
            : cnet_transport_pipe_connect(&session->transport, &impl->backend, impl->backend_kind,
                                          session->peer.pipe_name);
    if (status == SALTS_OK)
      status = cnet_session_table_transition(impl->sessions, session->handle, CNET_SESSION_OPEN);
    if (status == SALTS_OK) status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
    if (status != SALTS_OK)
      return command != NULL
                 ? cnet_owner_fail_accepted_command(impl, session, command, status,
                                                    CNET_SESSION_STAGE_CONNECT)
                 : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_CONNECT);
    if (command != NULL) {
      status = cnet_command_queue_release(impl->commands, command);
      if (status != SALTS_OK) return status;
    }
    return cnet_owner_queue_connected_event(impl, session);
  }

  status = cnet_transport_tcp_prepare_connect(&session->transport, &impl->backend,
                                              impl->backend_kind, session->peer.address,
                                              session->peer.address_length,
                                              &session->peer.socket_options, 0u, &operation);
  if (status != SALTS_OK)
    return command != NULL
               ? cnet_owner_fail_accepted_command(impl, session, command, status,
                                                  CNET_SESSION_STAGE_CONNECT)
               : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_CONNECT);
  return cnet_owner_start_request(impl, session, command, CNET_OWNER_REQUEST_CONNECT, &operation,
                                  false);
}

static int cnet_owner_connect(cnet_owner_impl *impl, cnet_command_view *command) {
  const cnet_owner_connect_payload *payload;
  const cnet_session_handle handle = command->connection;
  cnet_owner_session *session;
  cnet_session_state state = CNET_SESSION_FREE;
  bool has_address;
  bool has_host;
  bool host_present;
  bool has_pipe;
  bool pipe_present;
  bool has_adopted;
  bool tls_name_present;
  bool tls_name_valid;
  bool tls_valid;
  int status;

  if (command->size != sizeof(cnet_owner_connect_payload) || command->data == NULL) {
    status = cnet_command_queue_release(impl->commands, command);
    if (status != SALTS_OK) return status;
    return cnet_session_table_fail(impl->sessions, handle, SALTS_EINVAL,
                                   CNET_SESSION_STAGE_CONNECT);
  }
  payload = (const cnet_owner_connect_payload *)command->data;
  has_adopted = payload->adopted && payload->adopted_socket != UINTPTR_MAX;
  has_address =
      payload->address_length != 0u && payload->address_length <= CNET_OWNER_ADDRESS_CAPACITY;
  host_present = payload->host[0] != '\0';
  has_host = host_present && payload->port != 0u &&
             memchr(payload->host, '\0', sizeof(payload->host)) != NULL;
  pipe_present = payload->pipe_name[0] != '\0';
  has_pipe = pipe_present && memchr(payload->pipe_name, '\0', sizeof(payload->pipe_name)) != NULL;
  tls_name_present = payload->tls_server_name[0] != '\0';
  tls_name_valid = tls_name_present &&
                   memchr(payload->tls_server_name, '\0', sizeof(payload->tls_server_name)) != NULL;
  tls_valid = payload->scheme == CNET_URI_TLS
                  ? payload->tls_context != NULL &&
                        payload->tls_io_buffer_bytes >= CNET_TLS_MIN_IO_BUFFER_BYTES &&
                        payload->tls_io_buffer_bytes <= INT_MAX &&
                        payload->tls_handshake_timeout_ms != 0u &&
                        (payload->tls_server ? payload->adopted && !tls_name_present
                                             : !payload->adopted && tls_name_valid)
                  : payload->tls_context == NULL && payload->tls_io_buffer_bytes == 0u &&
                        payload->tls_handshake_timeout_ms == 0u && !payload->tls_server &&
                        !tls_name_present;
  if ((payload->scheme != CNET_URI_TCP && payload->scheme != CNET_URI_UDP &&
       payload->scheme != CNET_URI_TLS && payload->scheme != CNET_URI_PIPE) ||
      !tls_valid ||
      (payload->adopted
           ? (payload->scheme != CNET_URI_TCP && payload->scheme != CNET_URI_TLS) || !has_adopted ||
                 payload->address_length != 0u || host_present || payload->port != 0u ||
                 pipe_present || payload->connect_timeout_ms != 0u
       : payload->scheme == CNET_URI_PIPE
           ? payload->address_length != 0u || host_present || payload->port != 0u || !has_pipe
           : pipe_present || (payload->address_length != 0u
                                  ? !has_address || host_present || payload->port != 0u
                                  : !has_host))) {
    if (payload->adopted) cnet_transport_close_socket(payload->adopted_socket);
    cnet_tls_context_release(payload->tls_context);
    status = cnet_command_queue_release(impl->commands, command);
    if (status != SALTS_OK) return status;
    return cnet_session_table_fail(impl->sessions, handle, SALTS_EINVAL,
                                   CNET_SESSION_STAGE_CONNECT);
  }
  if ((size_t)command->connection.slot > impl->connection_capacity) {
    if (payload->adopted) cnet_transport_close_socket(payload->adopted_socket);
    cnet_tls_context_release(payload->tls_context);
    (void)cnet_command_queue_release(impl->commands, command);
    return SALTS_ENOBUFS;
  }
  session = &impl->session_records[command->connection.slot - 1u];
  if (session->occupied) {
    if (payload->adopted) cnet_transport_close_socket(payload->adopted_socket);
    cnet_tls_context_release(payload->tls_context);
    (void)cnet_command_queue_release(impl->commands, command);
    return SALTS_EPROTO;
  }
  status = cnet_session_table_state(impl->sessions, command->connection, &state);
  if (status != SALTS_OK || state != CNET_SESSION_RESERVED) {
    if (payload->adopted) cnet_transport_close_socket(payload->adopted_socket);
    cnet_tls_context_release(payload->tls_context);
    (void)cnet_command_queue_release(impl->commands, command);
    return status != SALTS_OK ? status : SALTS_EPROTO;
  }

  memset(session, 0, sizeof(*session));
  session->handle = command->connection;
  session->peer = *payload;
  if (session->peer.socket_options.size == 0u)
    session->peer.socket_options =
        (cnet_stream_socket_options)CNET_STREAM_SOCKET_OPTIONS_INIT;
  session->receive_buffer =
      &impl->receive_storage[(size_t)(session->handle.slot - 1u) * impl->receive_buffer_bytes];
  session->transport.native_handle = UINTPTR_MAX;
  session->occupied = true;
  ++impl->occupied_sessions;
  if (session->peer.connect_timeout_ms != 0u) {
    status = salts_deadline_queue_schedule(
        &impl->deadlines, cnet_owner_deadline_after(impl, session->peer.connect_timeout_ms),
        CNET_OWNER_SESSION_DEADLINE_TOKEN | (uint64_t)session->handle.slot,
        &session->connect_deadline);
    if (status != SALTS_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              session->peer.scheme == CNET_URI_TLS
                                                  ? CNET_SESSION_STAGE_HANDSHAKE
                                                  : CNET_SESSION_STAGE_CONNECT);
  }
  status = cnet_session_table_transition(impl->sessions, session->handle,
                                         has_host ? CNET_SESSION_RESOLVING
                                                  : CNET_SESSION_TRANSPORT_CONNECTING);
  if (status != SALTS_OK)
    return cnet_owner_fail_accepted_command(impl, session, command, status,
                                            CNET_SESSION_STAGE_CONNECT);

  if (has_adopted) {
    status = cnet_transport_adopt_tcp(&session->transport, &impl->backend,
                                      session->peer.adopted_socket,
                                      &session->peer.socket_options);
    session->peer.adopted_socket = UINTPTR_MAX;
    session->peer.adopted = false;
    if (status == SALTS_OK) {
      if (session->peer.scheme == CNET_URI_TLS) status = cnet_owner_start_tls(impl, session);
      else {
        status = cnet_session_table_transition(impl->sessions, session->handle, CNET_SESSION_OPEN);
        if (status == SALTS_OK)
          status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
      }
    }
    if (status != SALTS_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              session->peer.scheme == CNET_URI_TLS
                                                  ? CNET_SESSION_STAGE_HANDSHAKE
                                                  : CNET_SESSION_STAGE_CONNECT);
    status = cnet_command_queue_release(impl->commands, command);
    if (status != SALTS_OK) return status;
    return session->peer.scheme == CNET_URI_TLS ? SALTS_OK
                                                : cnet_owner_queue_connected_event(impl, session);
  }
  if (has_host) {
    status = cnet_resolver_submit(
        &impl->resolver, session->peer.host, session->peer.port,
        (session->peer.scheme == CNET_URI_TCP || session->peer.scheme == CNET_URI_TLS) ? SOCK_STREAM
                                                                                       : SOCK_DGRAM,
        (uintptr_t)(session->handle.slot - 1u), &session->resolve_query);
    if (status != SALTS_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              CNET_SESSION_STAGE_RESOLVE);
    session->resolve_active = true;
    status = cnet_command_queue_release(impl->commands, command);
    return status;
  }
  return cnet_owner_start_transport(impl, session, command);
}

static int cnet_owner_process_resolver(cnet_owner_impl *impl, size_t *out_processed) {
  for (;;) {
    cnet_resolver_result result = {0};
    cnet_owner_session *session;
    size_t session_index;
    int status = cnet_resolver_take(&impl->resolver, &result);
    if (status == SALTS_ETIMEDOUT || status == SALTS_EOF) return SALTS_OK;
    if (status != SALTS_OK) return status;
    ++*out_processed;
    session_index = (size_t)result.user_data;
    if (session_index >= impl->connection_capacity) return SALTS_EPROTO;
    session = &impl->session_records[session_index];
    if (!session->occupied || !session->resolve_active ||
        session->resolve_query.slot != result.query.slot ||
        session->resolve_query.generation != result.query.generation)
      return SALTS_EPROTO;
    session->resolve_active = false;
    memset(&session->resolve_query, 0, sizeof(session->resolve_query));
    if (session->close_requested) {
      status = cnet_owner_finalize_session(impl, session);
    } else if (session->pending_status != SALTS_OK) {
      status = cnet_owner_finalize_session(impl, session);
    } else if (result.status != SALTS_OK) {
      status = cnet_owner_fail_session(impl, session, result.status, CNET_SESSION_STAGE_RESOLVE);
    } else {
      if (result.address_length == 0u || result.address_length > sizeof(session->peer.address))
        return SALTS_EPROTO;
      session->peer.address_length = result.address_length;
      memcpy(session->peer.address, result.address, result.address_length);
      status = cnet_session_table_transition(impl->sessions, session->handle,
                                             CNET_SESSION_TRANSPORT_CONNECTING);
      if (status == SALTS_OK) status = cnet_owner_start_transport(impl, session, NULL);
    }
    if (status != SALTS_OK) return status;
    if (impl->pending_event_count != 0u) return SALTS_OK;
  }
}

static int cnet_owner_send(cnet_owner_impl *impl, cnet_command_view *command,
                           bool close_after_send) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  native_io_operation operation;
  native_io_operation_kind operation_kind;
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (session == NULL) return cnet_command_queue_release(impl->commands, command);
  status = cnet_session_table_state(impl->sessions, command->connection, &state);
  if (status != SALTS_OK || state != CNET_SESSION_OPEN)
    return cnet_command_queue_release(impl->commands, command);
  if ((session->peer.scheme == CNET_URI_TLS && session->tls_send_command._sequence != 0u) ||
      (session->peer.scheme != CNET_URI_TLS && session->write_active))
    return cnet_owner_fail_accepted_command(impl, session, command, SALTS_EBUSY,
                                            CNET_SESSION_STAGE_WRITE);
  if (close_after_send) {
    status = cnet_session_table_begin_close(impl->sessions, session->handle);
    if (status != SALTS_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              CNET_SESSION_STAGE_SHUTDOWN);
    session->close_requested = true;
    session->receive_demand = 0u;
    status = cnet_owner_cancel_receive_requests(impl, session->handle);
    if (status != SALTS_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              CNET_SESSION_STAGE_SHUTDOWN);
    status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING, SALTS_OK,
                                          CNET_SESSION_STAGE_NONE);
    if (status != SALTS_OK)
      return cnet_owner_fail_accepted_command(impl, session, command, status,
                                              CNET_SESSION_STAGE_CALLBACK);
  }
  if (session->peer.scheme == CNET_URI_TLS) {
    session->tls_send_command = *command;
    memset(command, 0, sizeof(*command));
    session->tls_send_accepted = false;
    session->tls_close_after_send = close_after_send;
    status = cnet_owner_tls_pump(impl, session);
    return status == SALTS_OK
               ? SALTS_OK
               : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_WRITE);
  }
  operation_kind = session->peer.scheme == CNET_URI_TCP   ? NATIVE_IO_OPERATION_TCP_SEND
                   : session->peer.scheme == CNET_URI_UDP ? NATIVE_IO_OPERATION_UDP_SEND_TO
                                                          : NATIVE_IO_OPERATION_PIPE_WRITE;
  operation = (native_io_operation){.kind = operation_kind,
                                    .endpoint = cnet_transport_write_endpoint(&session->transport),
                                    .buffer = (void *)command->data,
                                    .length = command->size};
  return cnet_owner_start_request(impl, session, command, CNET_OWNER_REQUEST_SEND, &operation,
                                  close_after_send);
}

static int cnet_owner_receive(cnet_owner_impl *impl, cnet_command_view *command) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (session == NULL) return cnet_command_queue_release(impl->commands, command);
  status = cnet_session_table_state(impl->sessions, command->connection, &state);
  if (status != SALTS_OK || state != CNET_SESSION_OPEN || session->close_requested)
    return cnet_command_queue_release(impl->commands, command);
  if (command->argument > SIZE_MAX - session->receive_demand)
    return cnet_owner_fail_accepted_command(impl, session, command, SALTS_ERANGE,
                                            CNET_SESSION_STAGE_READ);
  session->receive_demand += command->argument;
  status = cnet_command_queue_release(impl->commands, command);
  if (status != SALTS_OK) return status;
  return cnet_owner_arm_receive(impl, session);
}

static int cnet_owner_close_session(cnet_owner_impl *impl, cnet_command_view *command) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  int status = cnet_command_queue_release(impl->commands, command);
  if (status != SALTS_OK || session == NULL) return status;
  status = cnet_session_table_begin_close(impl->sessions, session->handle);
  if (status == SALTS_EALREADY) return SALTS_OK;
  if (status != SALTS_OK) return status;
  session->close_requested = true;
  session->receive_demand = 0u;
  status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
  if (status != SALTS_OK) return status;
  status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING, SALTS_OK,
                                        CNET_SESSION_STAGE_NONE);
  if (status != SALTS_OK) return status;
  if (session->resolve_active) {
    status = cnet_resolver_cancel(&impl->resolver, session->resolve_query);
    if (status != SALTS_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
    return SALTS_OK;
  }
  if (session->peer.scheme == CNET_URI_TLS && session->tls.handshake_complete) {
    status = cnet_owner_cancel_receive_requests(impl, session->handle);
    if (status != SALTS_OK)
      return cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_SHUTDOWN);
    status = cnet_owner_tls_pump(impl, session);
    return status == SALTS_OK
               ? SALTS_OK
               : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_SHUTDOWN);
  }
  if (session->active_requests != 0u) {
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != SALTS_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
    return SALTS_OK;
  }
  return cnet_owner_finalize_session(impl, session);
}

static int cnet_owner_process_commands(cnet_owner_impl *impl, size_t *out_processed) {
  for (;;) {
    cnet_command_view command = {0};
    int status = cnet_command_queue_take(impl->commands, &command);
    if (status == SALTS_ETIMEDOUT || status == SALTS_EOF) return SALTS_OK;
    if (status != SALTS_OK) return status;
    ++*out_processed;
    if (command.kind == CNET_COMMAND_CONNECT) status = cnet_owner_connect(impl, &command);
    else if (command.kind == CNET_COMMAND_SEND) status = cnet_owner_send(impl, &command, false);
    else if (command.kind == CNET_COMMAND_SEND_CLOSE)
      status = cnet_owner_send(impl, &command, true);
    else if (command.kind == CNET_COMMAND_RECEIVE) status = cnet_owner_receive(impl, &command);
    else if (command.kind == CNET_COMMAND_CLOSE) status = cnet_owner_close_session(impl, &command);
    else status = cnet_command_queue_release(impl->commands, &command);
    if (status != SALTS_OK) return status;
    if (impl->pending_event_count != 0u) return SALTS_OK;
  }
}

static int cnet_owner_complete(cnet_owner_impl *impl, cnet_owner_request *request,
                               const native_io_completion *completion) {
  cnet_owner_session *session;
  cnet_owner_request_role role;
  cnet_session_stage request_stage;
  size_t requested_size;
  bool close_after_send;
  int status;
  if (request == NULL || !request->active || request->owner != impl) return SALTS_EPROTO;
  session = cnet_owner_find_session(impl, request->session);
  if (session == NULL || session->active_requests == 0u || impl->active_requests == 0u)
    return SALTS_EPROTO;
  role = request->role;
  request_stage = request->stage;
  requested_size = request->requested_size;
  close_after_send = request->close_after_send;
  status = cnet_owner_release_request(request);
  if (status != SALTS_OK) return status;
  if (session->pending_status != SALTS_OK) return cnet_owner_finalize_session(impl, session);

  if (role == CNET_OWNER_REQUEST_CONNECT) {
    if (session->close_requested) return cnet_owner_finalize_session(impl, session);
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      if (session->peer.scheme == CNET_URI_TLS) {
        status = cnet_owner_start_tls(impl, session);
        if (status == SALTS_OK) return SALTS_OK;
        return cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_HANDSHAKE);
      }
      status = cnet_session_table_transition(impl->sessions, session->handle, CNET_SESSION_OPEN);
      if (status != SALTS_OK) return status;
      status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
      if (status != SALTS_OK) return status;
      return cnet_owner_queue_connected_event(impl, session);
    }
    cnet_owner_record_failure(session,
                              completion->status < SALTS_OK ? completion->status : SALTS_EIO,
                              CNET_SESSION_STAGE_CONNECT);
    return cnet_owner_finalize_session(impl, session);
  }

  if (role == CNET_OWNER_REQUEST_TLS_READ) {
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      if (completion->bytes == 0u || completion->bytes > session->tls.io_buffer_bytes)
        return cnet_owner_fail_session(impl, session, SALTS_EIO, request_stage);
      status = cnet_tls_feed_cipher(&session->tls, session->tls.read_buffer, completion->bytes);
      if (status == SALTS_OK) status = cnet_owner_tls_pump(impl, session);
      return status == SALTS_OK ? SALTS_OK
                                : cnet_owner_fail_session(impl, session, status, request_stage);
    }
    if (completion->kind == NATIVE_IO_COMPLETION_CANCELLED) {
      if (session->close_requested && session->tls.handshake_complete) {
        status = cnet_owner_tls_pump(impl, session);
        return status == SALTS_OK
                   ? SALTS_OK
                   : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_SHUTDOWN);
      }
      return cnet_owner_finalize_session(impl, session);
    }
    return cnet_owner_fail_session(
        impl, session,
        completion->kind == NATIVE_IO_COMPLETION_EOF
            ? (session->tls.handshake_complete ? SALTS_EPROTO : SALTS_ECONNABORTED)
            : (completion->status < SALTS_OK ? completion->status : SALTS_EIO),
        request_stage);
  }

  if (role == CNET_OWNER_REQUEST_TLS_WRITE) {
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      status = cnet_owner_tls_pump(impl, session);
      return status == SALTS_OK ? SALTS_OK
                                : cnet_owner_fail_session(impl, session, status, request_stage);
    }
    if (completion->kind == NATIVE_IO_COMPLETION_CANCELLED && session->close_requested)
      return cnet_owner_finalize_session(impl, session);
    return cnet_owner_fail_session(impl, session,
                                   completion->status < SALTS_OK ? completion->status : SALTS_EIO,
                                   request_stage);
  }

  if (role == CNET_OWNER_REQUEST_RECEIVE) {
    if (session->close_requested) return cnet_owner_finalize_session(impl, session);
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      const cnet_event event = {
          CNET_EVENT_RECEIVE,      session->handle,         CNET_EVENT_STATE_NONE, SALTS_OK,
          CNET_SESSION_STAGE_NONE, session->receive_buffer, completion->bytes};
      if (session->receive_demand != 0u) {
        status = cnet_owner_queue_receive_rearm(impl, session->handle);
        if (status != SALTS_OK) return status;
      }
      status = cnet_owner_publish_event(impl, &event);
      if (status == SALTS_ENOBUFS) {
        if (impl->pending_event_count == impl->pending_event_capacity) return SALTS_ENOBUFS;
        impl->pending_events[impl->pending_event_count++].event = event;
        return SALTS_OK;
      }
      if (status != SALTS_OK) return status;
      /* A later completion in this observed batch may still own the backend slot
       * selected by a new request. The next drive rearms after every record settles. */
      return SALTS_OK;
    }
    if (completion->kind == NATIVE_IO_COMPLETION_EOF) {
      status = cnet_session_table_begin_close(impl->sessions, session->handle);
      if (status != SALTS_OK && status != SALTS_EALREADY) return status;
      session->close_requested = true;
      session->receive_demand = 0u;
      if (status == SALTS_OK) {
        status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING,
                                              SALTS_OK, CNET_SESSION_STAGE_NONE);
        if (status != SALTS_OK) return status;
      }
      return cnet_owner_finalize_session(impl, session);
    }
    if (completion->kind == NATIVE_IO_COMPLETION_CANCELLED)
      return cnet_owner_finalize_session(impl, session);
    cnet_owner_record_failure(session,
                              completion->status < SALTS_OK ? completion->status : SALTS_EIO,
                              CNET_SESSION_STAGE_READ);
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != SALTS_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
    return cnet_owner_finalize_session(impl, session);
  }

  if (completion->kind == NATIVE_IO_COMPLETION_OK) {
    const cnet_event event = {CNET_EVENT_SEND,
                              session->handle,
                              CNET_EVENT_STATE_NONE,
                              SALTS_OK,
                              CNET_SESSION_STAGE_NONE,
                              NULL,
                              0u,
                              requested_size};
    status = cnet_owner_publish_event(impl, &event);
    if (status == SALTS_ENOBUFS) {
      if (impl->pending_event_count == impl->pending_event_capacity) return SALTS_ENOBUFS;
      impl->pending_events[impl->pending_event_count++].event = event;
    } else if (status != SALTS_OK) {
      return status;
    }
  } else if (close_after_send || !session->close_requested) {
    cnet_owner_record_failure(session,
                              completion->status < SALTS_OK ? completion->status : SALTS_EIO,
                              CNET_SESSION_STAGE_WRITE);
    status = cnet_owner_cancel_session_requests(impl, session->handle);
    if (status != SALTS_OK) cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
  }
  return cnet_owner_finalize_session(impl, session);
}

static void cnet_owner_coroutine_entry(native_io_coroutine *coroutine, void *user_data) {
  cnet_owner_request *request = (cnet_owner_request *)user_data;
  cnet_owner_impl *impl = request != NULL ? request->owner : NULL;
  native_io_completion completion = {0};
  int status;
  if (impl == NULL || !request->active) return;
  do {
    status = native_io_coroutine_await(coroutine, &request->operation, &completion);
    if (status != SALTS_OK ||
        (request->role != CNET_OWNER_REQUEST_SEND &&
         request->role != CNET_OWNER_REQUEST_TLS_WRITE) ||
        completion.kind != NATIVE_IO_COMPLETION_OK)
      break;
    if (completion.bytes == 0u || completion.bytes > request->operation.length) {
      status = SALTS_EIO;
      break;
    }
    request->completed_size += completion.bytes;
    if (request->completed_size == request->requested_size) break;
    if (request->operation.kind == NATIVE_IO_OPERATION_UDP_SEND_TO) {
      status = SALTS_EIO;
      break;
    }
    request->operation.buffer = (unsigned char *)request->operation.buffer + completion.bytes;
    request->operation.length -= completion.bytes;
  } while (true);
  if (status == SALTS_OK &&
      (request->role == CNET_OWNER_REQUEST_SEND || request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
      completion.kind == NATIVE_IO_COMPLETION_OK)
    completion.bytes = request->completed_size;
  if (status == SALTS_OK) status = cnet_owner_complete(impl, request, &completion);
  else status = cnet_owner_fail_started_request(request, status);
  if (status != SALTS_OK && impl->coroutine_status == SALTS_OK) impl->coroutine_status = status;
}

static int cnet_owner_process_deadlines(cnet_owner_impl *impl) {
  salts_deadline_event next = {0};
  uint64_t now_ms;
  int status = salts_deadline_queue_peek(&impl->deadlines, &next);
  if (status == SALTS_ETIMEDOUT) return SALTS_OK;
  if (status != SALTS_OK) return status;
  now_ms = impl->now_ms(impl->clock_context);
  for (;;) {
    salts_deadline_event deadline = {0};
    status = salts_deadline_queue_take_ready(&impl->deadlines, now_ms, &deadline);
    if (status == SALTS_ETIMEDOUT) return SALTS_OK;
    if (status != SALTS_OK) return status;
    if ((deadline.token & CNET_OWNER_SESSION_DEADLINE_TOKEN) != 0u) {
      const size_t slot = (size_t)(deadline.token & ~CNET_OWNER_SESSION_DEADLINE_TOKEN);
      cnet_owner_session *session;
      if (slot == 0u || slot > impl->connection_capacity) return SALTS_EPROTO;
      session = &impl->session_records[slot - 1u];
      if (!session->occupied || session->connect_deadline != deadline.id) return SALTS_EPROTO;
      session->connect_deadline = 0u;
      cnet_owner_record_failure(session, SALTS_ETIMEDOUT,
                                session->session_deadline_stage != CNET_SESSION_STAGE_NONE
                                    ? session->session_deadline_stage
                                : session->resolve_active ? CNET_SESSION_STAGE_RESOLVE
                                                          : CNET_SESSION_STAGE_CONNECT);
      if (session->resolve_active) {
        status = cnet_resolver_cancel(&impl->resolver, session->resolve_query);
        if (status != SALTS_OK) return status;
      }
      status = cnet_owner_cancel_session_requests(impl, session->handle);
      if (status != SALTS_OK) return status;
      status = cnet_owner_finalize_session(impl, session);
      if (status != SALTS_OK) return status;
    } else {
      const size_t slot = (size_t)deadline.token;
      cnet_owner_request *request;
      cnet_owner_session *session;
      if (slot == 0u || slot > impl->request_capacity) return SALTS_EPROTO;
      request = &impl->request_records[slot - 1u];
      if (!request->active || request->deadline != deadline.id) return SALTS_EPROTO;
      request->deadline = 0u;
      session = cnet_owner_find_session(impl, request->session);
      if (session == NULL) return SALTS_EPROTO;
      cnet_owner_record_failure(session, SALTS_ETIMEDOUT, request->stage);
      status = cnet_owner_cancel_session_requests(impl, session->handle);
      if (status != SALTS_OK) return status;
    }
  }
}

static uint32_t cnet_owner_observe_timeout(cnet_owner_impl *impl, uint32_t requested_ms,
                                           bool processed) {
  salts_deadline_event deadline = {0};
  uint64_t now_ms;
  uint64_t remaining;
  uint32_t result = requested_ms;
  if (processed) return 0u;
  if (salts_deadline_queue_peek(&impl->deadlines, &deadline) == SALTS_OK) {
    now_ms = impl->now_ms(impl->clock_context);
    remaining = deadline.deadline_ms > now_ms ? deadline.deadline_ms - now_ms : 0u;
    if (remaining > UINT32_MAX) remaining = UINT32_MAX;
    if (remaining < result) result = (uint32_t)remaining;
  }
  if (cnet_resolver_has_pending(&impl->resolver) && result > CNET_OWNER_RESOLVER_POLL_INTERVAL_MS)
    result = CNET_OWNER_RESOLVER_POLL_INTERVAL_MS;
  return result;
}

int cnet_owner_init(cnet_owner *owner, const cnet_owner_config *config) {
  cnet_owner_impl *impl;
  native_io_backend_config backend_config;
  cnet_event_queue_config event_config;
  cnet_resolver_config resolver_config;
  int status;
  if (owner == NULL) return SALTS_EINVAL;
  if (owner->impl != NULL) return SALTS_EALREADY;
  if (config == NULL || config->sessions == NULL || config->commands == NULL ||
      config->events == NULL || config->connection_capacity == 0u ||
      config->request_capacity == 0u || config->completion_batch_capacity == 0u ||
      config->receive_buffer_bytes == 0u ||
      config->receive_buffer_count != config->connection_capacity ||
      config->completion_batch_capacity > config->request_capacity ||
      config->connection_capacity > UINT32_MAX / 2u || config->request_capacity > UINT32_MAX)
    return SALTS_EINVAL;
  if (!cnet_event_queue_get_config(config->events, &event_config) ||
      config->receive_buffer_bytes > event_config.max_payload_bytes)
    return SALTS_EINVAL;
  if (config->completion_batch_capacity > SIZE_MAX - 2u ||
      config->connection_capacity > SIZE_MAX - config->request_capacity ||
      config->connection_capacity > SIZE_MAX / sizeof(cnet_owner_session) ||
      config->connection_capacity > SIZE_MAX / sizeof(cnet_session_handle) ||
      config->connection_capacity > SIZE_MAX / config->receive_buffer_bytes ||
      config->request_capacity > SIZE_MAX / sizeof(cnet_owner_request) ||
      config->request_capacity > SIZE_MAX / sizeof(uint32_t) ||
      config->completion_batch_capacity > SIZE_MAX / sizeof(native_io_completion) ||
      config->completion_batch_capacity + 2u > SIZE_MAX / sizeof(cnet_owner_pending_event))
    return SALTS_ERANGE;
  impl = (cnet_owner_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->session_records =
      (cnet_owner_session *)calloc(config->connection_capacity, sizeof(*impl->session_records));
  impl->request_records =
      (cnet_owner_request *)calloc(config->request_capacity, sizeof(*impl->request_records));
  impl->free_requests = (uint32_t *)calloc(config->request_capacity, sizeof(*impl->free_requests));
  impl->receive_rearms =
      (cnet_session_handle *)calloc(config->connection_capacity, sizeof(*impl->receive_rearms));
  impl->pending_events = (cnet_owner_pending_event *)calloc(config->completion_batch_capacity + 2u,
                                                            sizeof(*impl->pending_events));
  impl->receive_storage =
      (unsigned char *)calloc(config->connection_capacity, config->receive_buffer_bytes);
  impl->completions =
      (native_io_completion *)calloc(config->completion_batch_capacity, sizeof(*impl->completions));
  if (impl->session_records == NULL || impl->request_records == NULL ||
      impl->free_requests == NULL || impl->receive_rearms == NULL || impl->pending_events == NULL ||
      impl->receive_storage == NULL || impl->completions == NULL) {
    free(impl->completions);
    free(impl->receive_storage);
    free(impl->pending_events);
    free(impl->receive_rearms);
    free(impl->free_requests);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return SALTS_ENOMEM;
  }
  backend_config =
      (native_io_backend_config){config->backend_kind, config->connection_capacity * 2u,
                                 config->request_capacity, config->completion_batch_capacity};
  status = native_io_backend_init(&impl->backend, &backend_config);
  if (status != SALTS_OK) {
    free(impl->completions);
    free(impl->receive_storage);
    free(impl->pending_events);
    free(impl->receive_rearms);
    free(impl->free_requests);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return status;
  }
  resolver_config = (cnet_resolver_config){config->connection_capacity};
  status = cnet_resolver_init(&impl->resolver, &resolver_config);
  if (status != SALTS_OK) {
    (void)native_io_backend_close(&impl->backend);
    (void)native_io_backend_destroy(&impl->backend);
    free(impl->completions);
    free(impl->receive_storage);
    free(impl->pending_events);
    free(impl->receive_rearms);
    free(impl->free_requests);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return status;
  }
  status = salts_deadline_queue_init(&impl->deadlines,
                                     config->connection_capacity + config->request_capacity);
  if (status != SALTS_OK) {
    (void)cnet_resolver_close(&impl->resolver, 0u);
    (void)cnet_resolver_destroy(&impl->resolver);
    (void)native_io_backend_close(&impl->backend);
    (void)native_io_backend_destroy(&impl->backend);
    free(impl->completions);
    free(impl->receive_storage);
    free(impl->pending_events);
    free(impl->receive_rearms);
    free(impl->free_requests);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return status;
  }
  impl->backend_kind = config->backend_kind;
  impl->sessions = config->sessions;
  impl->commands = config->commands;
  impl->events = config->events;
  impl->publish_event = config->publish_event;
  impl->event_context = config->event_context;
  impl->connection_capacity = config->connection_capacity;
  impl->request_capacity = config->request_capacity;
  impl->free_request_count = config->request_capacity;
  for (size_t index = 0u; index < config->request_capacity; ++index)
    impl->free_requests[index] = (uint32_t)(config->request_capacity - index - 1u);
  impl->completion_batch_capacity = config->completion_batch_capacity;
  impl->pending_event_capacity = config->completion_batch_capacity + 2u;
  impl->receive_buffer_bytes = config->receive_buffer_bytes;
  impl->now_ms = config->now_ms != NULL ? config->now_ms : cnet_owner_system_now;
  impl->clock_context = config->clock_context;
  owner->impl = impl;
  return SALTS_OK;
}

int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  uint64_t started_ms;
  uint64_t published_before;
  size_t processed = 0u;
  bool event_blocked = false;
  int status;
  if (impl == NULL || impl->closed) return SALTS_EINVAL;
  started_ms = salts_monotonic_ms();
  published_before = impl->published_event_count;
  status = cnet_owner_flush_state_events(impl, &event_blocked);
  if (status != SALTS_OK) return status;
  if (event_blocked || impl->published_event_count != published_before) return SALTS_OK;
  status = cnet_owner_arm_pending_receives(impl);
  if (status != SALTS_OK) return status;
  status = cnet_owner_process_commands(impl, &processed);
  if (status != SALTS_OK) return status;
  if (impl->pending_event_count != 0u || impl->published_event_count != published_before)
    return SALTS_OK;
  status = cnet_owner_process_deadlines(impl);
  if (status != SALTS_OK) return status;
  if (impl->pending_event_count != 0u || impl->published_event_count != published_before)
    return SALTS_OK;
  if (cnet_resolver_has_pending(&impl->resolver)) {
    status = cnet_resolver_poll(&impl->resolver);
    if (status != SALTS_OK) return status;
    status = cnet_owner_process_resolver(impl, &processed);
    if (status != SALTS_OK) return status;
    if (impl->published_event_count != published_before) return SALTS_OK;
  }
  /* Work that did not start asynchronous I/O must not turn a drive into an idle wait. */
  if (processed != 0u && impl->active_requests == 0u && !cnet_resolver_has_pending(&impl->resolver))
    return SALTS_OK;

  for (;;) {
    const uint64_t elapsed_ms = salts_monotonic_ms() - started_ms;
    const uint32_t remaining_ms = elapsed_ms >= timeout_ms ? 0u : timeout_ms - (uint32_t)elapsed_ms;
    size_t completion_count = 0u;

    status = native_io_backend_observe(
        &impl->backend, impl->completions, impl->completion_batch_capacity,
        cnet_owner_observe_timeout(impl, remaining_ms, false), &completion_count);
    if (status == SALTS_ETIMEDOUT) {
      if (cnet_resolver_has_pending(&impl->resolver)) {
        status = cnet_resolver_poll(&impl->resolver);
        if (status != SALTS_OK) return status;
        status = cnet_owner_process_resolver(impl, &processed);
        if (status != SALTS_OK) return status;
      }
      return cnet_owner_process_deadlines(impl);
    }
    if (status != SALTS_OK) return status;
    status = cnet_owner_take_coroutine_status(impl);
    if (status != SALTS_OK) return status;
    if (completion_count != 0u) return SALTS_EPROTO;
    if (impl->published_event_count != published_before) return SALTS_OK;
    status = cnet_owner_process_deadlines(impl);
    if (status != SALTS_OK) return status;
    if (impl->pending_event_count != 0u || impl->published_event_count != published_before)
      return SALTS_OK;
    if (remaining_ms == 0u || impl->active_requests == 0u) return SALTS_OK;
  }
}

int cnet_owner_wake(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL) return SALTS_EINVAL;
  return native_io_backend_wake(&impl->backend);
}

bool cnet_owner_get_coroutine_stats(const cnet_owner *owner, native_io_coroutine_stats *out_stats) {
  const cnet_owner_impl *impl = owner != NULL ? (const cnet_owner_impl *)owner->impl : NULL;
  return impl != NULL && native_io_backend_get_coroutine_stats(&impl->backend, out_stats);
}

int cnet_owner_tls_peer_certificate_sha256(
    cnet_owner *owner, cnet_session_handle session_handle,
    char buffer[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY]) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  if (impl == NULL || buffer == NULL) return SALTS_EINVAL;
  buffer[0] = '\0';
  session = cnet_owner_find_session(impl, session_handle);
  if (session == NULL) return SALTS_ENOENT;
  if (session->peer.scheme != CNET_URI_TLS) return SALTS_ENOTSUP;
  return cnet_tls_state_peer_certificate_sha256(&session->tls, buffer);
}

int cnet_owner_tls_export_channel_binding(
    cnet_owner *owner, cnet_session_handle session_handle,
    uint8_t output[CNET_TLS_CHANNEL_BINDING_BYTES]) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  if (impl == NULL || output == NULL) return SALTS_EINVAL;
  memset(output, 0, CNET_TLS_CHANNEL_BINDING_BYTES);
  session = cnet_owner_find_session(impl, session_handle);
  if (session == NULL) return SALTS_ENOENT;
  if (session->peer.scheme != CNET_URI_TLS) return SALTS_ENOTSUP;
  return cnet_tls_state_export_channel_binding(&session->tls, output);
}

int cnet_owner_release_session(cnet_owner *owner, cnet_session_handle session_handle) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session = cnet_owner_find_session(impl, session_handle);
  cnet_session_state state = CNET_SESSION_FREE;
  int status;
  if (session == NULL) return SALTS_ENOENT;
  if (session->active_requests != 0u || session->resolve_active ||
      session->connect_deadline != 0u || cnet_transport_active(&session->transport))
    return SALTS_EBUSY;
  status = cnet_session_table_state(impl->sessions, session_handle, &state);
  if (status != SALTS_ENOENT) return SALTS_EBUSY;
  memset(session, 0, sizeof(*session));
  --impl->occupied_sessions;
  return SALTS_OK;
}

int cnet_owner_close(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->closed) return SALTS_EALREADY;
  if (impl->occupied_sessions != 0u || impl->active_requests != 0u ||
      impl->receive_rearm_count != 0u || impl->pending_event_count != 0u ||
      salts_deadline_queue_size(&impl->deadlines) != 0u)
    return SALTS_EBUSY;
  if (!impl->resolver_closed) {
    status = cnet_resolver_close(&impl->resolver, 0u);
    if (status != SALTS_OK) return status;
    impl->resolver_closed = true;
  }
  status = native_io_backend_close(&impl->backend);
  if (status == SALTS_OK) impl->closed = true;
  return status;
}

int cnet_owner_destroy(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  int status;
  if (owner == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  if (!impl->closed || impl->occupied_sessions != 0u || impl->active_requests != 0u)
    return SALTS_EBUSY;
  status = cnet_resolver_destroy(&impl->resolver);
  if (status != SALTS_OK) return status;
  status = native_io_backend_destroy(&impl->backend);
  if (status != SALTS_OK) return status;
  status = salts_deadline_queue_destroy(&impl->deadlines);
  if (status != SALTS_OK) return status;
  free(impl->completions);
  free(impl->receive_storage);
  free(impl->pending_events);
  free(impl->receive_rearms);
  free(impl->free_requests);
  free(impl->request_records);
  free(impl->session_records);
  free(impl);
  owner->impl = NULL;
  return SALTS_OK;
}
