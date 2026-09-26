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
  /** Current logical TLS plaintext head; ciphertext NativeIO requests are separate. */
  cnet_write_view tls_send_write;
  cnet_resolver_query resolve_query;
  size_t active_requests;
  int pending_status;
  cnet_session_stage pending_stage;
  cnet_session_stage session_deadline_stage;
  unsigned char *receive_buffer;
  size_t receive_demand;
  bool occupied;
  bool close_requested;
  bool owner_work_queued;
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
  native_io_request native_request;
  native_io_operation operation;
  size_t requested_size;
  size_t submitted_size;
  size_t completed_size;
  cnet_session_handle session;
  cnet_command_view command;
  cnet_write_view write;
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
  cnet_write_queue writes;
  native_io_backend_kind backend_kind;
  cnet_session_table *sessions;
  cnet_command_queue *commands;
  cnet_event_queue *events;
  cnet_owner_event_publish_fn publish_event;
  void *event_context;
  cnet_owner_session *session_records;
  cnet_owner_request *request_records;
  uint32_t *free_requests;
  cnet_session_handle *session_work;
  cnet_owner_pending_event *pending_events;
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
  size_t session_work_head;
  size_t session_work_count;
  cnet_owner_now_ms_fn now_ms;
  void *clock_context;
#if defined(CNET_INTERNAL_PROFILING)
  cnet_owner_profile profile;
  bool profile_active;
#endif
#if defined(CNET_INTERNAL_TESTING)
  bool test_force_cancel_ealready_once;
  size_t test_send_chunk_bytes;
#endif
  bool closed;
  bool resolver_closed;
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

#if defined(CNET_INTERNAL_PROFILING)
static uint64_t cnet_owner_profile_start(const cnet_owner_impl *impl) {
  return impl->profile_active ? salts_hrtime() : 0u;
}

static void cnet_owner_profile_finish(cnet_owner_impl *impl, uint64_t started_ns,
                                      uint64_t *elapsed_ns, uint64_t *calls) {
  uint64_t elapsed;
  if (!impl->profile_active) return;
  elapsed = salts_hrtime() - started_ns;
  *elapsed_ns = elapsed > UINT64_MAX - *elapsed_ns ? UINT64_MAX : *elapsed_ns + elapsed;
  if (*calls != UINT64_MAX) ++*calls;
}
#endif

static int cnet_owner_publish_event(cnet_owner_impl *impl, const cnet_event *event) {
#if defined(CNET_INTERNAL_PROFILING)
  const uint64_t profile_started = cnet_owner_profile_start(impl);
#endif
  const int status = impl->publish_event != NULL ? impl->publish_event(impl->event_context, event)
                                                 : cnet_event_queue_publish(impl->events, event);
#if defined(CNET_INTERNAL_PROFILING)
  cnet_owner_profile_finish(impl, profile_started, &impl->profile.event_publish_ns,
                            &impl->profile.event_publish_calls);
#endif
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
                                    cnet_command_view *command, cnet_write_view *write,
                                    cnet_owner_request_role role,
                                    const native_io_operation *operation, bool close_after_send);
static int cnet_owner_start_queued_write(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_arm_receive(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_tls_pump(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_tls_start_read(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_tls_background_progress(cnet_owner_impl *impl,
                                              cnet_owner_session *session);
static int cnet_owner_complete(cnet_owner_impl *impl, cnet_owner_request *request,
                               const native_io_completion *completion);
static int cnet_owner_progress_close(cnet_owner_impl *impl, cnet_owner_session *session);
static int cnet_owner_finish_write_admission(cnet_owner_impl *impl,
                                             cnet_owner_session *session,
                                             cnet_write_handle admitted);
static int cnet_owner_discard_queued_writes(cnet_owner_impl *impl, cnet_owner_session *session,
                                            bool keep_active_head);

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

static int cnet_owner_queue_session_work(cnet_owner_impl *impl,
                                         cnet_session_handle session_handle) {
  cnet_owner_session *session = cnet_owner_find_session(impl, session_handle);
  size_t tail;
  if (session == NULL) return SALTS_ENOENT;
  if (session->owner_work_queued) return SALTS_OK;
  if (impl->session_work_count == impl->connection_capacity) return SALTS_ENOBUFS;
  tail = (impl->session_work_head + impl->session_work_count) % impl->connection_capacity;
  impl->session_work[tail] = session_handle;
  ++impl->session_work_count;
  session->owner_work_queued = true;
  return SALTS_OK;
}

static int cnet_owner_process_session_work(cnet_owner_impl *impl) {
  while (impl->session_work_count != 0u) {
    const cnet_session_handle handle = impl->session_work[impl->session_work_head];
    cnet_owner_session *session;
    int status;
    impl->session_work[impl->session_work_head] = (cnet_session_handle){0};
    impl->session_work_head = (impl->session_work_head + 1u) % impl->connection_capacity;
    --impl->session_work_count;
    session = cnet_owner_find_session(impl, handle);
    if (session == NULL) return SALTS_EPROTO;
    session->owner_work_queued = false;
    {
      cnet_session_state state = CNET_SESSION_FREE;
      status = cnet_session_table_state(impl->sessions, handle, &state);
      if (status != SALTS_OK) return status;
      if (state == CNET_SESSION_TERMINAL) continue;
    }
    if (session->close_requested) {
      status = cnet_owner_progress_close(impl, session);
      if (status != SALTS_OK) return status;
      continue;
    }
    status = cnet_owner_start_queued_write(impl, session);
    if (status != SALTS_OK) return status;
    if (session->peer.scheme == CNET_URI_TLS && session->receive_demand == 0u &&
        !session->close_requested) {
      status = cnet_owner_tls_background_progress(impl, session);
      if (status != SALTS_OK) return status;
      continue;
    }
    if (session->receive_demand == 0u || session->read_active) continue;
    status = cnet_owner_arm_receive(impl, session);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int cnet_owner_cancel_native_request(cnet_owner_impl *impl,
                                            cnet_owner_request *request) {
  int status;
  if (impl == NULL || request == NULL || !request->active || request->owner != impl ||
      !native_io_request_valid(request->native_request))
    return SALTS_EPROTO;
  status = native_io_backend_cancel(&impl->backend, request->native_request);
#if defined(CNET_INTERNAL_TESTING)
  if (impl->test_force_cancel_ealready_once &&
      (status == SALTS_OK || status == SALTS_EALREADY)) {
    impl->test_force_cancel_ealready_once = false;
    return SALTS_EALREADY;
  }
#endif
  return status;
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
    status = cnet_owner_cancel_native_request(impl, request);
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
    status = cnet_owner_cancel_native_request(impl, request);
    if (status != SALTS_OK && status != SALTS_EALREADY && first_error == SALTS_OK)
      first_error = status;
  }
  return first_error;
}

static int cnet_owner_release_tls_send(cnet_owner_impl *impl, cnet_owner_session *session) {
  int status;
  if (!cnet_write_handle_valid(session->tls_send_write.handle)) return SALTS_OK;
  status = cnet_write_queue_settle(&impl->writes, &session->tls_send_write);
  if (status == SALTS_OK) session->tls_send_accepted = false;
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
      cnet_write_handle_valid(session->tls_send_write.handle) ||
      (!session->close_requested && session->pending_status == SALTS_OK))
    return SALTS_OK;
  if (impl->writes.impl != NULL &&
      (session->close_requested || session->pending_status != SALTS_OK)) {
    status = cnet_owner_discard_queued_writes(impl, session, false);
    if (status != SALTS_OK) return status;
  }
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
  if (cnet_write_handle_valid(request->write.handle)) {
    status = cnet_write_queue_settle(&impl->writes, &request->write);
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

static int cnet_owner_receive_operation_kind(cnet_uri_scheme scheme,
                                             native_io_operation_kind *out_kind) {
  if (out_kind == NULL) return SALTS_EINVAL;
  switch (scheme) {
  case CNET_URI_TCP:
  case CNET_URI_VSOCK:
    *out_kind = NATIVE_IO_OPERATION_STREAM_RECV;
    return SALTS_OK;
  case CNET_URI_UDP:
    *out_kind = NATIVE_IO_OPERATION_UDP_RECV_FROM;
    return SALTS_OK;
  case CNET_URI_PIPE:
    *out_kind = NATIVE_IO_OPERATION_PIPE_READ;
    return SALTS_OK;
  case CNET_URI_NONE:
  case CNET_URI_TLS:
    return SALTS_EINVAL;
  }
  return SALTS_EINVAL;
}

static int cnet_owner_send_operation_kind(cnet_uri_scheme scheme,
                                          native_io_operation_kind *out_kind) {
  if (out_kind == NULL) return SALTS_EINVAL;
  switch (scheme) {
  case CNET_URI_TCP:
  case CNET_URI_VSOCK:
    *out_kind = NATIVE_IO_OPERATION_STREAM_SEND;
    return SALTS_OK;
  case CNET_URI_UDP:
    *out_kind = NATIVE_IO_OPERATION_UDP_SEND_TO;
    return SALTS_OK;
  case CNET_URI_PIPE:
    *out_kind = NATIVE_IO_OPERATION_PIPE_WRITE;
    return SALTS_OK;
  case CNET_URI_NONE:
  case CNET_URI_TLS:
    return SALTS_EINVAL;
  }
  return SALTS_EINVAL;
}

static int cnet_owner_tls_adopt_write_head(cnet_owner_impl *impl,
                                           cnet_owner_session *session) {
  int status;
  if (cnet_write_handle_valid(session->tls_send_write.handle)) return SALTS_OK;
  status = cnet_write_queue_peek(&impl->writes, session->handle, &session->tls_send_write);
  if (status == SALTS_ETIMEDOUT) return SALTS_OK;
  if (status != SALTS_OK) return status;
  session->tls_send_accepted = false;
  return SALTS_OK;
}

static int cnet_owner_start_queued_write(cnet_owner_impl *impl, cnet_owner_session *session) {
  cnet_write_view write = {0};
  native_io_operation operation;
  native_io_operation_kind operation_kind;
  int status;

  if (session->write_active || session->close_requested) return SALTS_OK;
  if (session->peer.scheme == CNET_URI_TLS) {
    status = cnet_owner_tls_adopt_write_head(impl, session);
    if (status != SALTS_OK) return status;
    return cnet_owner_tls_pump(impl, session);
  }
  status = cnet_write_queue_peek(&impl->writes, session->handle, &write);
  if (status == SALTS_ETIMEDOUT) return SALTS_OK;
  if (status != SALTS_OK) return status;
  if (write.remaining == 0u) return SALTS_EPROTO;
  status = cnet_owner_send_operation_kind(session->peer.scheme, &operation_kind);
  if (status != SALTS_OK) return status;

  operation = (native_io_operation){.kind = operation_kind,
                                    .endpoint = cnet_transport_write_endpoint(&session->transport),
                                    .buffer = (void *)write.data,
                                    .length = write.remaining};
  return cnet_owner_start_request(impl, session, NULL, &write, CNET_OWNER_REQUEST_SEND,
                                  &operation, write.close_after_send);
}

static int cnet_owner_arm_receive(cnet_owner_impl *impl, cnet_owner_session *session) {
  native_io_operation operation;
  native_io_operation_kind operation_kind;
  int status;

  if (session->receive_demand == 0u || session->read_active || session->close_requested)
    return SALTS_OK;
  if (session->peer.scheme == CNET_URI_TLS) return cnet_owner_tls_pump(impl, session);
  status = cnet_owner_receive_operation_kind(session->peer.scheme, &operation_kind);
  if (status != SALTS_OK) return status;
  operation = (native_io_operation){.kind = operation_kind,
                                    .endpoint = cnet_transport_read_endpoint(&session->transport),
                                    .buffer = session->receive_buffer,
                                    .length = impl->receive_buffer_bytes};
  status =
      cnet_owner_start_request(impl, session, NULL, NULL, CNET_OWNER_REQUEST_RECEIVE, &operation, false);
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
  release_status = cnet_owner_discard_queued_writes(impl, session, session->write_active);
  if (release_status != SALTS_OK) return release_status;
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
  int discard_status;
  cnet_owner_record_failure(session, status, stage);
  discard_status = cnet_owner_discard_queued_writes(impl, session, session->write_active);
  if (discard_status != SALTS_OK) return discard_status;
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

static int cnet_owner_submit_request(cnet_owner_impl *impl, cnet_owner_request *request,
                                     bool first_submit) {
  native_io_operation submitted;
  native_io_request native_request = {0};
  size_t index;
  int status;

  if (impl == NULL || request == NULL || !request->active || request->owner != impl)
    return SALTS_EPROTO;
  index = (size_t)(request - impl->request_records);
  if (index >= impl->request_capacity) return SALTS_EPROTO;

  submitted = request->operation;
  submitted.user_data = (uintptr_t)(index + 1u);
#if defined(CNET_INTERNAL_TESTING)
  if (impl->test_send_chunk_bytes != 0u &&
      (request->role == CNET_OWNER_REQUEST_SEND ||
       request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
      submitted.length > impl->test_send_chunk_bytes)
    submitted.length = impl->test_send_chunk_bytes;
#endif
  request->submitted_size = submitted.length;
#if defined(CNET_INTERNAL_PROFILING)
  {
    const uint64_t profile_started = cnet_owner_profile_start(impl);
    status = native_io_backend_prepare(&impl->backend, &submitted, &native_request);
    if (first_submit)
      cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_start_ns,
                                &impl->profile.request_start_calls);
    else
      cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_resubmit_ns,
                                &impl->profile.request_resubmit_calls);
  }
#else
  status = native_io_backend_prepare(&impl->backend, &submitted, &native_request);
  (void)first_submit;
#endif
  if (status == SALTS_OK) request->native_request = native_request;
  return status;
}

static int cnet_owner_start_request(cnet_owner_impl *impl, cnet_owner_session *session,
                                    cnet_command_view *command, cnet_write_view *write,
                                    cnet_owner_request_role role,
                                    const native_io_operation *operation, bool close_after_send) {
  cnet_owner_request *request;
  const uint32_t timeout_ms = cnet_owner_request_timeout(session, role);
  const cnet_session_stage stage = cnet_owner_request_stage(session, role);
#if defined(CNET_INTERNAL_PROFILING)
  uint64_t lifecycle_started;
#endif
  size_t index;
  int result;
  int status;

  request = cnet_owner_acquire_request(impl);
  if (request == NULL) {
    if (write != NULL && cnet_write_handle_valid(write->handle)) {
      const int settle_status = cnet_write_queue_settle(&impl->writes, write);
      if (settle_status != SALTS_OK) return settle_status;
    }
    return command != NULL
               ? cnet_owner_fail_accepted_command(impl, session, command, SALTS_ENOBUFS, stage)
               : cnet_owner_fail_session(impl, session, SALTS_ENOBUFS, stage);
  }
#if defined(CNET_INTERNAL_PROFILING)
  lifecycle_started = cnet_owner_profile_start(impl);
#endif
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
  if (write != NULL) {
    request->write = *write;
    memset(write, 0, sizeof(*write));
  }
  ++session->active_requests;
  ++impl->active_requests;
  if (role == CNET_OWNER_REQUEST_RECEIVE || role == CNET_OWNER_REQUEST_TLS_READ)
    session->read_active = true;
  if (role == CNET_OWNER_REQUEST_SEND || role == CNET_OWNER_REQUEST_TLS_WRITE)
    session->write_active = true;
  index = (size_t)(request - impl->request_records);
  status = cnet_owner_submit_request(impl, request, true);
  if (status != SALTS_OK) {
    result = cnet_owner_fail_started_request(request, status);
    goto finish;
  }
  if (timeout_ms != 0u) {
    status =
        salts_deadline_queue_schedule(&impl->deadlines, cnet_owner_deadline_after(impl, timeout_ms),
                                      index + 1u, &request->deadline);
    if (status != SALTS_OK) {
      cnet_owner_record_failure(session, status, stage);
      status = cnet_owner_cancel_session_requests(impl, session->handle);
      if (status != SALTS_OK)
        cnet_owner_record_failure(session, status, CNET_SESSION_STAGE_SHUTDOWN);
      result = status;
      goto finish;
    }
  }
  result = SALTS_OK;

finish:
#if defined(CNET_INTERNAL_PROFILING)
  cnet_owner_profile_finish(impl, lifecycle_started, &impl->profile.request_lifecycle_ns,
                            &impl->profile.request_lifecycle_calls);
#endif
  return result;
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
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_STREAM_SEND,
                                    .endpoint = cnet_transport_write_endpoint(&session->transport),
                                    .buffer = session->tls.write_buffer,
                                    .length = size};
  status = cnet_owner_start_request(impl, session, NULL, NULL, CNET_OWNER_REQUEST_TLS_WRITE, &operation,
                                    false);
  if (status == SALTS_OK) *out_started = true;
  return status;
}

static int cnet_owner_tls_start_read(cnet_owner_impl *impl, cnet_owner_session *session) {
  native_io_operation operation;
  size_t capacity;
  if (session->read_active || session->close_requested) return SALTS_OK;
  capacity = cnet_tls_cipher_input_capacity(&session->tls);
  if (capacity == 0u) return SALTS_OK;
  if (capacity > session->tls.io_buffer_bytes) capacity = session->tls.io_buffer_bytes;
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_STREAM_RECV,
                                    .endpoint = cnet_transport_read_endpoint(&session->transport),
                                    .buffer = session->tls.read_buffer,
                                    .length = capacity};
  return cnet_owner_start_request(impl, session, NULL, NULL, CNET_OWNER_REQUEST_TLS_READ, &operation,
                                  false);
}

static int cnet_owner_tls_begin_peer_close(cnet_owner_impl *impl,
                                           cnet_owner_session *session) {
  int status = cnet_session_table_begin_close(impl->sessions, session->handle);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  session->close_requested = true;
  session->receive_demand = 0u;
  session->tls_shutdown_after_flush = true;
  if (status == SALTS_OK) {
    status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING,
                                          SALTS_OK, CNET_SESSION_STAGE_NONE);
    if (status != SALTS_OK) return status;
  }
  return cnet_owner_tls_pump(impl, session);
}

static int cnet_owner_tls_background_progress(cnet_owner_impl *impl,
                                              cnet_owner_session *session) {
  bool peer_closed = false;
  bool plaintext_pending = false;
  bool started = false;
  int status;

  if (impl == NULL || session == NULL || session->peer.scheme != CNET_URI_TLS)
    return SALTS_EINVAL;
  if (session->close_requested || session->read_active || session->receive_demand != 0u)
    return SALTS_OK;

  status = cnet_tls_probe_peer_close(&session->tls, &peer_closed, &plaintext_pending);
  if (status != SALTS_OK) return status;
  status = cnet_owner_tls_start_write(impl, session, &started);
  if (status != SALTS_OK) return status;
  if (peer_closed) return cnet_owner_tls_begin_peer_close(impl, session);
  if (plaintext_pending || session->read_active) return SALTS_OK;
  return cnet_owner_tls_start_read(impl, session);
}

static int cnet_owner_tls_publish_send(cnet_owner_impl *impl, cnet_owner_session *session) {
  const size_t size = session->tls_send_write.size;
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

  if (!session->close_requested &&
      !cnet_write_handle_valid(session->tls_send_write.handle)) {
    status = cnet_owner_tls_adopt_write_head(impl, session);
    if (status != SALTS_OK) return status;
  }

  if (cnet_write_handle_valid(session->tls_send_write.handle) && !session->tls_send_accepted) {
    bool complete = false;
    status = cnet_tls_write(&session->tls, session->tls_send_write.data,
                            session->tls_send_write.remaining, &complete);
    if (status != SALTS_OK) return status;
    session->tls_send_accepted = complete;
  }
  status = cnet_owner_tls_start_write(impl, session, &started);
  if (status != SALTS_OK) return status;
  if (cnet_write_handle_valid(session->tls_send_write.handle) && session->tls_send_accepted &&
      !session->write_active && !started) {
    const bool close_after_send = session->tls_send_write.close_after_send;
    status = cnet_owner_tls_publish_send(impl, session);
    if (status != SALTS_OK) return status;
    if (close_after_send) {
      status = cnet_session_table_begin_close(impl->sessions, session->handle);
      if (status != SALTS_OK && status != SALTS_EALREADY) return status;
      session->close_requested = true;
      session->receive_demand = 0u;
      status = cnet_owner_cancel_receive_requests(impl, session->handle);
      if (status != SALTS_OK) return status;
      status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING,
                                            SALTS_OK, CNET_SESSION_STAGE_NONE);
      if (status != SALTS_OK) return status;
      session->tls_shutdown_after_flush = true;
    } else {
      size_t queued_writes = 0u;
      status = cnet_write_queue_count(&impl->writes, session->handle, &queued_writes);
      if (status != SALTS_OK) return status;
      if (queued_writes != 0u) {
        status = cnet_owner_queue_session_work(impl, session->handle);
        if (status != SALTS_OK) return status;
      }
    }
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
        status = cnet_owner_queue_session_work(impl, session->handle);
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
      return cnet_owner_tls_begin_peer_close(impl, session);
    } else {
      return cnet_owner_tls_start_read(impl, session);
    }
  }

  if (session->close_requested && !cnet_write_handle_valid(session->tls_send_write.handle)) {
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

static int cnet_owner_start_tls(cnet_owner_impl *impl, cnet_owner_session *session,
                                bool publish_handshaking) {
  int status;
  if (session->peer.scheme != CNET_URI_TLS) return SALTS_EINVAL;

  status = cnet_session_table_transition(impl->sessions, session->handle,
                                         CNET_SESSION_PROTOCOL_HANDSHAKING);
  if (status == SALTS_OK && publish_handshaking) {
    status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_TLS_HANDSHAKING,
                                          SALTS_OK, CNET_SESSION_STAGE_NONE);
  }
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
  native_io_operation operation = {0};
  bool connected_immediately = false;
  int status;

  switch (session->peer.scheme) {
  case CNET_URI_UDP:
    status = cnet_transport_udp_connect(&session->transport, &impl->backend, impl->backend_kind,
                                        session->peer.address, session->peer.address_length);
    connected_immediately = true;
    break;
  case CNET_URI_PIPE:
    status = cnet_transport_pipe_connect(&session->transport, &impl->backend, impl->backend_kind,
                                         session->peer.pipe_name);
    connected_immediately = true;
    break;
  case CNET_URI_TCP:
  case CNET_URI_TLS:
    status = cnet_transport_tcp_prepare_connect(
        &session->transport, &impl->backend, impl->backend_kind, session->peer.address,
        session->peer.address_length, &session->peer.socket_options, 0u, &operation);
    break;
  case CNET_URI_VSOCK:
    status = cnet_transport_vsock_prepare_connect(
        &session->transport, &impl->backend, impl->backend_kind, session->peer.address,
        session->peer.address_length, &session->peer.socket_options, 0u, &operation);
    break;
  case CNET_URI_NONE:
    status = SALTS_EINVAL;
    break;
  default:
    status = SALTS_EINVAL;
    break;
  }

  if (connected_immediately) {
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

  if (status != SALTS_OK)
    return command != NULL
               ? cnet_owner_fail_accepted_command(impl, session, command, status,
                                                  CNET_SESSION_STAGE_CONNECT)
               : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_CONNECT);
  return cnet_owner_start_request(impl, session, command, NULL, CNET_OWNER_REQUEST_CONNECT, &operation,
                                  false);
}

static bool cnet_owner_connect_endpoint_valid(const cnet_owner_connect_payload *payload,
                                              bool has_adopted, bool has_address,
                                              bool host_present, bool has_host,
                                              bool pipe_present, bool has_pipe) {
  if (payload->adopted)
    return (payload->scheme == CNET_URI_TCP || payload->scheme == CNET_URI_TLS ||
            payload->scheme == CNET_URI_VSOCK) &&
           has_adopted && payload->address_length == 0u && !host_present && payload->port == 0u &&
           !pipe_present && payload->connect_timeout_ms == 0u;

  switch (payload->scheme) {
  case CNET_URI_PIPE:
    return payload->address_length == 0u && !host_present && payload->port == 0u && has_pipe;
  case CNET_URI_VSOCK:
    return has_address && !host_present && payload->port == 0u && !pipe_present;
  case CNET_URI_TCP:
  case CNET_URI_TLS:
  case CNET_URI_UDP:
    return !pipe_present && (payload->address_length != 0u
                                 ? has_address && !host_present && payload->port == 0u
                                 : has_host);
  case CNET_URI_NONE:
    return false;
  }
  return false;
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
       payload->scheme != CNET_URI_TLS && payload->scheme != CNET_URI_PIPE &&
       payload->scheme != CNET_URI_VSOCK) ||
      !tls_valid || !cnet_owner_connect_endpoint_valid(payload, has_adopted, has_address,
                                                       host_present, has_host, pipe_present,
                                                       has_pipe)) {
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
  session->receive_buffer = (unsigned char *)malloc(impl->receive_buffer_bytes);
  session->transport.native_handle = UINTPTR_MAX;
  session->occupied = true;
  ++impl->occupied_sessions;
  if (session->receive_buffer == NULL)
    return cnet_owner_fail_accepted_command(impl, session, command, SALTS_ENOMEM,
                                            CNET_SESSION_STAGE_CONNECT);
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
    status = session->peer.scheme == CNET_URI_VSOCK
                 ? cnet_transport_adopt_vsock(&session->transport, &impl->backend,
                                              session->peer.adopted_socket,
                                              &session->peer.socket_options)
                 : cnet_transport_adopt_tcp(&session->transport, &impl->backend,
                                            session->peer.adopted_socket,
                                            &session->peer.socket_options);
    session->peer.adopted_socket = UINTPTR_MAX;
    session->peer.adopted = false;
    if (status == SALTS_OK) {
      if (session->peer.scheme == CNET_URI_TLS) status = cnet_owner_start_tls(impl, session, false);
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

static int cnet_owner_start_tls_command(cnet_owner_impl *impl, cnet_command_view *command) {
  const cnet_owner_start_tls_payload *payload;
  cnet_owner_session *session;
  bool name_present;
  bool valid;
  size_t queued_writes = 0u;
  int status;

  if (command->size != sizeof(cnet_owner_start_tls_payload) || command->data == NULL)
    return cnet_command_queue_release(impl->commands, command);
  payload = (const cnet_owner_start_tls_payload *)command->data;
  session = cnet_owner_find_session(impl, command->connection);
  if (session == NULL) {
    cnet_tls_context_release(payload->tls_context);
    return cnet_command_queue_release(impl->commands, command);
  }
  name_present = payload->tls_server_name[0] != '\0';
  status = cnet_write_queue_count(&impl->writes, session->handle, &queued_writes);
  if (status != SALTS_OK) {
    cnet_tls_context_release(payload->tls_context);
    (void)cnet_command_queue_release(impl->commands, command);
    return status;
  }
  valid =
      payload->tls_context != NULL &&
      payload->tls_io_buffer_bytes >= CNET_TLS_MIN_IO_BUFFER_BYTES &&
      payload->tls_io_buffer_bytes <= INT_MAX && payload->tls_handshake_timeout_ms != 0u &&
      (payload->tls_server ? !name_present
                           : name_present && memchr(payload->tls_server_name, '\0',
                                                    sizeof(payload->tls_server_name)) != NULL) &&
      session->peer.scheme == CNET_URI_TCP && session->active_requests == 0u &&
      session->receive_demand == 0u && !session->read_active && !session->write_active &&
      !session->resolve_active && !session->close_requested && queued_writes == 0u &&
      !cnet_write_handle_valid(session->tls_send_write.handle);
  if (!valid) {
    cnet_tls_context_release(payload->tls_context);
    status = cnet_command_queue_release(impl->commands, command);
    if (status != SALTS_OK) return status;
    return cnet_owner_fail_session(impl, session, SALTS_EPROTO, CNET_SESSION_STAGE_HANDSHAKE);
  }

  session->peer.scheme = CNET_URI_TLS;
  session->peer.tls_context = payload->tls_context;
  memcpy(session->peer.tls_server_name, payload->tls_server_name,
         sizeof(session->peer.tls_server_name));
  session->peer.tls_handshake_timeout_ms = payload->tls_handshake_timeout_ms;
  session->peer.tls_io_buffer_bytes = payload->tls_io_buffer_bytes;
  session->peer.tls_server = payload->tls_server;
  status = cnet_command_queue_release(impl->commands, command);
  if (status != SALTS_OK) return status;
  status = cnet_owner_start_tls(impl, session, true);
  return status == SALTS_OK
             ? SALTS_OK
             : cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_HANDSHAKE);
}

static int cnet_owner_discard_queued_writes(cnet_owner_impl *impl, cnet_owner_session *session,
                                            bool keep_active_head) {
  size_t discarded = 0u;
  int status;
  if (impl == NULL || session == NULL || impl->writes.impl == NULL) return SALTS_OK;
  status = cnet_write_queue_discard(&impl->writes, session->handle, keep_active_head, &discarded);
  if (status == SALTS_OK && !keep_active_head) {
    memset(&session->tls_send_write, 0, sizeof(session->tls_send_write));
    session->tls_send_accepted = false;
  }
  return status;
}

static int cnet_owner_progress_close(cnet_owner_impl *impl, cnet_owner_session *session) {
  int status;
  if (impl == NULL || session == NULL || !session->close_requested) return SALTS_EPROTO;
  session->receive_demand = 0u;
  status = cnet_owner_cancel_deadline(impl, &session->connect_deadline);
  if (status != SALTS_OK) return status;
  status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING, SALTS_OK,
                                        CNET_SESSION_STAGE_NONE);
  if (status != SALTS_OK) return status;
  status = cnet_owner_discard_queued_writes(impl, session, session->write_active);
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

static int cnet_owner_close_session(cnet_owner_impl *impl, cnet_command_view *command) {
  cnet_owner_session *session = cnet_owner_find_session(impl, command->connection);
  int status = cnet_command_queue_release(impl->commands, command);
  if (status != SALTS_OK || session == NULL) return status;
  status = cnet_session_table_begin_close(impl->sessions, session->handle);
  if (status == SALTS_EALREADY) return SALTS_OK;
  if (status != SALTS_OK) return status;
  session->close_requested = true;
  return cnet_owner_progress_close(impl, session);
}

static int cnet_owner_process_commands(cnet_owner_impl *impl, size_t *out_processed) {
  for (;;) {
    cnet_command_view command = {0};
    int status = cnet_command_queue_take(impl->commands, &command);
    if (status == SALTS_ETIMEDOUT || status == SALTS_EOF) return SALTS_OK;
    if (status != SALTS_OK) return status;
    ++*out_processed;
    if (command.kind == CNET_COMMAND_CONNECT) status = cnet_owner_connect(impl, &command);
    else if (command.kind == CNET_COMMAND_RECEIVE) status = cnet_owner_receive(impl, &command);
    else if (command.kind == CNET_COMMAND_START_TLS)
      status = cnet_owner_start_tls_command(impl, &command);
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
        status = cnet_owner_start_tls(impl, session, false);
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
      if (status == SALTS_OK && session->occupied && !session->close_requested &&
          session->receive_demand == 0u)
        status = cnet_owner_queue_session_work(impl, session->handle);
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
            ? SALTS_ECONNABORTED
            : (completion->status < SALTS_OK ? completion->status : SALTS_EIO),
        request_stage);
  }

  if (role == CNET_OWNER_REQUEST_TLS_WRITE) {
    if (completion->kind == NATIVE_IO_COMPLETION_OK) {
      status = cnet_owner_tls_pump(impl, session);
      if (status == SALTS_OK && session->occupied && !session->close_requested &&
          session->receive_demand == 0u)
        status = cnet_owner_queue_session_work(impl, session->handle);
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
        status = cnet_owner_queue_session_work(impl, session->handle);
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
    if (role == CNET_OWNER_REQUEST_SEND && !close_after_send && !session->close_requested &&
        session->peer.scheme != CNET_URI_TLS && impl->writes.impl != NULL) {
      size_t queued_writes = 0u;
      status = cnet_write_queue_count(&impl->writes, session->handle, &queued_writes);
      if (status != SALTS_OK) return status;
      if (queued_writes != 0u) {
        status = cnet_owner_queue_session_work(impl, session->handle);
        if (status != SALTS_OK) return status;
      }
    }
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
    if (role == CNET_OWNER_REQUEST_SEND && close_after_send && !session->close_requested) {
      status = cnet_session_table_begin_close(impl->sessions, session->handle);
      if (status != SALTS_OK && status != SALTS_EALREADY) return status;
      session->close_requested = true;
      session->receive_demand = 0u;
      status = cnet_owner_cancel_receive_requests(impl, session->handle);
      if (status != SALTS_OK)
        return cnet_owner_fail_session(impl, session, status, CNET_SESSION_STAGE_SHUTDOWN);
      status = cnet_owner_queue_state_event(impl, session->handle, CNET_EVENT_STATE_CLOSING,
                                            SALTS_OK, CNET_SESSION_STAGE_NONE);
      if (status != SALTS_OK) return status;
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

static bool cnet_owner_native_request_equal(native_io_request left, native_io_request right) {
  return left.slot == right.slot && left.generation == right.generation;
}

static bool cnet_owner_native_endpoint_equal(native_io_endpoint left, native_io_endpoint right) {
  return left.slot == right.slot && left.generation == right.generation;
}

static int cnet_owner_finish_direct_completion(cnet_owner_impl *impl,
                                               cnet_owner_request *request,
                                               const native_io_completion *completion) {
  int status;
#if defined(CNET_INTERNAL_PROFILING)
  const uint64_t profile_started = cnet_owner_profile_start(impl);
#endif
  status = cnet_owner_complete(impl, request, completion);
#if defined(CNET_INTERNAL_PROFILING)
  cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_completion_ns,
                            &impl->profile.request_completion_calls);
#endif
  return status;
}

static int cnet_owner_fail_direct_request(cnet_owner_impl *impl, cnet_owner_request *request,
                                          int failure) {
  int status;
#if defined(CNET_INTERNAL_PROFILING)
  const uint64_t profile_started = cnet_owner_profile_start(impl);
#endif
  status = cnet_owner_fail_started_request(request, failure);
#if defined(CNET_INTERNAL_PROFILING)
  cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_completion_ns,
                            &impl->profile.request_completion_calls);
#endif
  return status;
}

static int cnet_owner_route_completion(cnet_owner_impl *impl,
                                       const native_io_completion *completion) {
  cnet_owner_request *request;
  size_t index;

  if (completion == NULL || completion->user_data == 0u ||
      completion->user_data > (uintptr_t)impl->request_capacity)
    return SALTS_EPROTO;
  index = (size_t)completion->user_data - 1u;
  request = &impl->request_records[index];
  if (!request->active || request->owner != impl ||
      !native_io_request_valid(request->native_request) ||
      !cnet_owner_native_request_equal(request->native_request, completion->request) ||
      !cnet_owner_native_endpoint_equal(request->operation.endpoint, completion->endpoint))
    return SALTS_EPROTO;

  if ((request->role == CNET_OWNER_REQUEST_SEND ||
       request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
      completion->kind == NATIVE_IO_COMPLETION_OK) {
    cnet_owner_session *session = cnet_owner_find_session(impl, request->session);
    native_io_completion terminal = *completion;
    int status;

    if (session == NULL) return SALTS_EPROTO;
    /* Cancellation may lose to a partial success already queued by the OS. */
    if (session->pending_status != SALTS_OK)
      return cnet_owner_finish_direct_completion(impl, request, completion);
    if (completion->bytes == 0u || completion->bytes > request->submitted_size)
      return cnet_owner_fail_direct_request(impl, request, SALTS_EIO);
    if (cnet_write_handle_valid(request->write.handle)) {
      status = cnet_write_queue_advance(&impl->writes, &request->write, completion->bytes);
      if (status != SALTS_OK) return cnet_owner_fail_direct_request(impl, request, status);
    }
    request->completed_size += completion->bytes;
    if (request->completed_size < request->requested_size) {
      if (request->operation.kind == NATIVE_IO_OPERATION_UDP_SEND_TO)
        return cnet_owner_fail_direct_request(impl, request, SALTS_EIO);
      request->operation.buffer =
          (unsigned char *)request->operation.buffer + completion->bytes;
      request->operation.length -= completion->bytes;
      status = cnet_owner_submit_request(impl, request, false);
      return status == SALTS_OK ? SALTS_OK
                                : cnet_owner_fail_direct_request(impl, request, status);
    }
    if (request->completed_size != request->requested_size)
      return cnet_owner_fail_direct_request(impl, request, SALTS_EIO);
    terminal.bytes = request->completed_size;
    return cnet_owner_finish_direct_completion(impl, request, &terminal);
  }

  return cnet_owner_finish_direct_completion(impl, request, completion);
}

static int cnet_owner_process_completion_batch(cnet_owner_impl *impl,
                                                const native_io_completion *events,
                                                size_t count) {
  int first_error = SALTS_OK;
  size_t index;
  if (impl == NULL || (events == NULL && count != 0u)) return SALTS_EINVAL;
  for (index = 0u; index < count; ++index) {
    const int status = cnet_owner_route_completion(impl, &events[index]);
    if (status != SALTS_OK && first_error == SALTS_OK) first_error = status;
  }
  return first_error;
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
      config->write_capacity == 0u || config->write_capacity_per_connection == 0u ||
      config->write_capacity_per_connection > config->write_capacity ||
      config->max_write_bytes == 0u || config->write_buffer_bytes < config->max_write_bytes ||
      config->completion_batch_capacity > config->request_capacity ||
      config->connection_capacity > UINT32_MAX / 2u || config->request_capacity > UINT32_MAX)
    return SALTS_EINVAL;
  if (config->connection_capacity > SIZE_MAX / config->write_capacity_per_connection)
    return SALTS_EINVAL;
  if (config->write_capacity >
      config->connection_capacity * config->write_capacity_per_connection)
    return SALTS_EINVAL;
  if (!cnet_event_queue_get_config(config->events, &event_config) ||
      config->receive_buffer_bytes > event_config.max_payload_bytes)
    return SALTS_EINVAL;
  if (config->completion_batch_capacity > SIZE_MAX - 2u ||
      config->connection_capacity > SIZE_MAX - config->request_capacity ||
      config->connection_capacity > SIZE_MAX / sizeof(cnet_owner_session) ||
      config->connection_capacity > SIZE_MAX / sizeof(cnet_session_handle) ||
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
  impl->session_work =
      (cnet_session_handle *)calloc(config->connection_capacity, sizeof(*impl->session_work));
  impl->pending_events = (cnet_owner_pending_event *)calloc(config->completion_batch_capacity + 2u,
                                                            sizeof(*impl->pending_events));
  impl->completions =
      (native_io_completion *)calloc(config->completion_batch_capacity, sizeof(*impl->completions));
  if (impl->session_records == NULL || impl->request_records == NULL ||
      impl->free_requests == NULL || impl->session_work == NULL || impl->pending_events == NULL ||
      impl->completions == NULL) {
    free(impl->completions);
    free(impl->pending_events);
    free(impl->session_work);
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
    free(impl->pending_events);
    free(impl->session_work);
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
    free(impl->pending_events);
    free(impl->session_work);
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
    free(impl->pending_events);
    free(impl->session_work);
    free(impl->free_requests);
    free(impl->request_records);
    free(impl->session_records);
    free(impl);
    return status;
  }
  {
    const cnet_write_queue_config write_config = {
        .connection_capacity = config->connection_capacity,
        .capacity = config->write_capacity,
        .per_connection_capacity = config->write_capacity_per_connection,
        .max_payload_bytes = config->max_write_bytes,
        .payload_capacity_bytes = config->write_buffer_bytes};
    status = cnet_write_queue_init(&impl->writes, &write_config);
    if (status != SALTS_OK) {
      (void)salts_deadline_queue_destroy(&impl->deadlines);
      (void)cnet_resolver_close(&impl->resolver, 0u);
      (void)cnet_resolver_destroy(&impl->resolver);
      (void)native_io_backend_close(&impl->backend);
      (void)native_io_backend_destroy(&impl->backend);
      free(impl->completions);
      free(impl->pending_events);
      free(impl->session_work);
      free(impl->free_requests);
      free(impl->request_records);
      free(impl->session_records);
      free(impl);
      return status;
    }
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

#if defined(CNET_INTERNAL_PROFILING)
static int cnet_owner_drive_once(cnet_owner *owner, uint32_t timeout_ms) {
#else
int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms) {
#endif
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
#if defined(CNET_INTERNAL_PROFILING)
  {
    const bool profile_active = impl->profile_active;
    const uint64_t request_ns_before = impl->profile.request_lifecycle_ns;
    const uint64_t request_calls_before = impl->profile.request_lifecycle_calls;
    const uint64_t profile_started = cnet_owner_profile_start(impl);
    status = cnet_owner_process_session_work(impl);
    cnet_owner_profile_finish(impl, profile_started, &impl->profile.receive_rearm_stage_ns,
                              &impl->profile.receive_rearm_stage_calls);
    if (profile_active) {
      impl->profile.receive_rearm_request_lifecycle_ns +=
          impl->profile.request_lifecycle_ns - request_ns_before;
      impl->profile.receive_rearm_request_lifecycle_calls +=
          impl->profile.request_lifecycle_calls - request_calls_before;
    }
  }
#else
  status = cnet_owner_process_session_work(impl);
#endif
  if (status != SALTS_OK) return status;
#if defined(CNET_INTERNAL_PROFILING)
  {
    const bool profile_active = impl->profile_active;
    const uint64_t request_ns_before = impl->profile.request_lifecycle_ns;
    const uint64_t request_calls_before = impl->profile.request_lifecycle_calls;
    const uint64_t profile_started = cnet_owner_profile_start(impl);
    status = cnet_owner_process_commands(impl, &processed);
    cnet_owner_profile_finish(impl, profile_started, &impl->profile.command_stage_ns,
                              &impl->profile.command_stage_calls);
    if (profile_active) {
      impl->profile.command_request_lifecycle_ns +=
          impl->profile.request_lifecycle_ns - request_ns_before;
      impl->profile.command_request_lifecycle_calls +=
          impl->profile.request_lifecycle_calls - request_calls_before;
    }
  }
#else
  status = cnet_owner_process_commands(impl, &processed);
#endif
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

#if defined(CNET_INTERNAL_PROFILING)
    {
      const uint64_t profile_started = cnet_owner_profile_start(impl);
      status = native_io_backend_observe(
          &impl->backend, impl->completions, impl->completion_batch_capacity,
          cnet_owner_observe_timeout(impl, remaining_ms, false), &completion_count);
      cnet_owner_profile_finish(impl, profile_started, &impl->profile.observe_ns,
                                &impl->profile.observe_calls);
    }
#else
    status = native_io_backend_observe(
        &impl->backend, impl->completions, impl->completion_batch_capacity,
        cnet_owner_observe_timeout(impl, remaining_ms, false), &completion_count);
#endif
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
    if (completion_count == 0u) return SALTS_OK;
    status = cnet_owner_process_completion_batch(impl, impl->completions, completion_count);
    if (status != SALTS_OK) return status;
    if (impl->published_event_count != published_before) return SALTS_OK;
    status = cnet_owner_process_deadlines(impl);
    if (status != SALTS_OK) return status;
    if (impl->pending_event_count != 0u || impl->published_event_count != published_before)
      return SALTS_OK;
    if (remaining_ms == 0u || impl->active_requests == 0u) return SALTS_OK;
  }
}

#if defined(CNET_INTERNAL_PROFILING)
int cnet_owner_drive(cnet_owner *owner, uint32_t timeout_ms) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  const uint64_t profile_started = impl != NULL ? cnet_owner_profile_start(impl) : 0u;
  const int status = cnet_owner_drive_once(owner, timeout_ms);
  if (impl != NULL)
    cnet_owner_profile_finish(impl, profile_started, &impl->profile.owner_drive_ns,
                              &impl->profile.owner_drive_calls);
  return status;
}
#endif

static int cnet_owner_send_direct_ready(cnet_owner_impl *impl,
                                        cnet_session_handle session_handle,
                                        cnet_owner_session **out_session) {
  cnet_owner_session *session;
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (out_session == NULL) return SALTS_EINVAL;
  *out_session = NULL;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
  if (impl->writes.impl == NULL) return SALTS_ENOTSUP;
  session = cnet_owner_find_session(impl, session_handle);
  if (session == NULL) return SALTS_ENOENT;
  status = cnet_session_table_state(impl->sessions, session_handle, &state);
  if (status != SALTS_OK) return status;
  if (state != CNET_SESSION_OPEN || session->close_requested) return SALTS_EBUSY;
  *out_session = session;
  return SALTS_OK;
}

static int cnet_owner_finish_write_admission(cnet_owner_impl *impl,
                                             cnet_owner_session *session,
                                             cnet_write_handle admitted) {
  int status;
  if (session->write_active || session->owner_work_queued) return SALTS_OK;
  status = cnet_owner_queue_session_work(impl, session->handle);
  if (status == SALTS_OK) return SALTS_OK;
  (void)cnet_write_queue_cancel_tail(&impl->writes, session->handle, admitted);
  return status;
}

int cnet_owner_send_copy_direct(cnet_owner *owner, cnet_session_handle session_handle,
                                const void *data, size_t size) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  cnet_write_handle handle = {0};
  int status = cnet_owner_send_direct_ready(impl, session_handle, &session);
  if (status != SALTS_OK) return status;
  status = cnet_write_queue_enqueue_copy(&impl->writes, session_handle, data, size, false, &handle);
  if (status != SALTS_OK) return status;
  return cnet_owner_finish_write_admission(impl, session, handle);
}

int cnet_owner_send_buffer_direct(cnet_owner *owner, cnet_session_handle session_handle,
                                  mem_buffer_t *buffer) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  cnet_write_handle handle = {0};
  int status = cnet_owner_send_direct_ready(impl, session_handle, &session);
  if (status != SALTS_OK) return status;
  status = cnet_write_queue_enqueue_buffer(&impl->writes, session_handle, buffer, false, &handle);
  if (status != SALTS_OK) return status;
  return cnet_owner_finish_write_admission(impl, session, handle);
}

int cnet_owner_sendv_direct(cnet_owner *owner, cnet_session_handle session_handle,
                            const cnet_const_buffer *segments, size_t segment_count) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  cnet_write_handle handle = {0};
  int status = cnet_owner_send_direct_ready(impl, session_handle, &session);
  if (status != SALTS_OK) return status;
  status = cnet_write_queue_enqueuev_copy(&impl->writes, session_handle, segments, segment_count,
                                          false, &handle);
  if (status != SALTS_OK) return status;
  return cnet_owner_finish_write_admission(impl, session, handle);
}

int cnet_owner_send_close_direct(cnet_owner *owner, cnet_session_handle session_handle,
                                 const void *data, size_t size) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  cnet_write_handle handle = {0};
  int status = cnet_owner_send_direct_ready(impl, session_handle, &session);
  if (status != SALTS_OK) return status;
  status = cnet_write_queue_enqueue_copy(&impl->writes, session_handle, data, size, true, &handle);
  if (status != SALTS_OK) return status;
  return cnet_owner_finish_write_admission(impl, session, handle);
}

int cnet_owner_receive_direct(cnet_owner *owner, cnet_session_handle session_handle,
                              size_t demand) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  cnet_session_state state = CNET_SESSION_FREE;
  bool queue_rearm;
  int status;

  if (impl == NULL || demand == 0u) return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
  session = cnet_owner_find_session(impl, session_handle);
  if (session == NULL) return SALTS_ENOENT;
  status = cnet_session_table_state(impl->sessions, session_handle, &state);
  if (status != SALTS_OK) return status;
  if (state != CNET_SESSION_OPEN || session->close_requested) return SALTS_EBUSY;
  if (demand > SIZE_MAX - session->receive_demand) return SALTS_ERANGE;

  queue_rearm = !session->read_active && session->receive_demand == 0u;
  session->receive_demand += demand;
  if (!queue_rearm) return SALTS_OK;

  status = cnet_owner_queue_session_work(impl, session_handle);
  if (status != SALTS_OK) session->receive_demand -= demand;
  return status;
}

int cnet_owner_close_direct(cnet_owner *owner, cnet_session_handle session_handle) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  cnet_owner_session *session;
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (impl == NULL) return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
  session = cnet_owner_find_session(impl, session_handle);
  if (session == NULL) return SALTS_ENOENT;
  status = cnet_session_table_state(impl->sessions, session_handle, &state);
  if (status != SALTS_OK) return status;
  if (state == CNET_SESSION_DRAINING || state == CNET_SESSION_TERMINAL) return SALTS_EALREADY;
  if (state != CNET_SESSION_OPEN) return SALTS_EBUSY;
  if (session->active_requests != 0u || session->receive_demand != 0u || session->read_active ||
      session->write_active || session->resolve_active || session->close_requested ||
      cnet_write_handle_valid(session->tls_send_write.handle))
    return SALTS_EBUSY;
  if (!session->owner_work_queued && impl->session_work_count == impl->connection_capacity)
    return SALTS_ENOBUFS;

  status = cnet_session_table_begin_close(impl->sessions, session_handle);
  if (status != SALTS_OK) return status;
  session->close_requested = true;
  status = cnet_owner_queue_session_work(impl, session_handle);
  return status;
}

int cnet_owner_wake(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL) return SALTS_EINVAL;
  return native_io_backend_wake(&impl->backend);
}

#if defined(CNET_INTERNAL_TESTING)
bool cnet_owner_test_backend_stats(const cnet_owner *owner,
                                   native_io_backend_stats *out_native,
                                   native_io_coroutine_stats *out_coroutine) {
  const cnet_owner_impl *impl = owner != NULL ? (const cnet_owner_impl *)owner->impl : NULL;
  if (impl == NULL || out_native == NULL || out_coroutine == NULL) return false;
  return native_io_backend_get_stats(&impl->backend, out_native) &&
         native_io_backend_get_coroutine_stats(&impl->backend, out_coroutine);
}

bool cnet_owner_test_get_request_snapshot(const cnet_owner *owner, size_t request_index,
                                          cnet_owner_test_request_snapshot *out_snapshot) {
  const cnet_owner_impl *impl = owner != NULL ? (const cnet_owner_impl *)owner->impl : NULL;
  const cnet_owner_request *request;
  if (impl == NULL || out_snapshot == NULL || request_index >= impl->request_capacity) return false;
  request = &impl->request_records[request_index];
  *out_snapshot = (cnet_owner_test_request_snapshot){
      .token = (uintptr_t)(request_index + 1u),
      .native_request = request->native_request,
      .endpoint = request->operation.endpoint,
      .active = request->active};
  return true;
}

int cnet_owner_test_observe_raw(cnet_owner *owner, native_io_completion *events,
                                size_t event_capacity, uint32_t timeout_ms,
                                size_t *out_count) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL || events == NULL || event_capacity == 0u || out_count == NULL)
    return SALTS_EINVAL;
  return native_io_backend_observe(&impl->backend, events, event_capacity, timeout_ms, out_count);
}

int cnet_owner_test_process_completion_batch(cnet_owner *owner,
                                             const native_io_completion *events,
                                             size_t count) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL) return SALTS_EINVAL;
  return cnet_owner_process_completion_batch(impl, events, count);
}

int cnet_owner_test_force_cancel_ealready_once(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->test_force_cancel_ealready_once) return SALTS_EALREADY;
  impl->test_force_cancel_ealready_once = true;
  return SALTS_OK;
}

int cnet_owner_test_process_deadlines(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  return impl != NULL ? cnet_owner_process_deadlines(impl) : SALTS_EINVAL;
}

int cnet_owner_test_set_send_chunk_bytes(cnet_owner *owner, size_t bytes) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL) return SALTS_EINVAL;
  impl->test_send_chunk_bytes = bytes;
  return SALTS_OK;
}
#endif

#if defined(CNET_INTERNAL_PROFILING)
int cnet_owner_profile_begin(cnet_owner *owner) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (impl == NULL || impl->closed) return SALTS_EINVAL;
  if (impl->profile_active) return SALTS_EALREADY;
  memset(&impl->profile, 0, sizeof(impl->profile));
  impl->profile_active = true;
  return SALTS_OK;
}

int cnet_owner_profile_take(cnet_owner *owner, cnet_owner_profile *out_profile) {
  cnet_owner_impl *impl = cnet_owner_get(owner);
  if (out_profile == NULL) return SALTS_EINVAL;
  memset(out_profile, 0, sizeof(*out_profile));
  if (impl == NULL || impl->closed) return SALTS_EINVAL;
  if (!impl->profile_active) return SALTS_EBUSY;
  *out_profile = impl->profile;
  impl->profile_active = false;
  return SALTS_OK;
}
#endif

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
  free(session->receive_buffer);
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
      impl->session_work_count != 0u || impl->pending_event_count != 0u ||
      salts_deadline_queue_size(&impl->deadlines) != 0u)
    return SALTS_EBUSY;
  if (impl->writes.impl != NULL) {
    cnet_write_queue_stats write_stats = {0};
    if (!cnet_write_queue_get_stats(&impl->writes, &write_stats) || write_stats.live_writes != 0u)
      return SALTS_EBUSY;
    status = cnet_write_queue_close(&impl->writes);
    if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  }
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
  status = cnet_write_queue_destroy(&impl->writes);
  if (status != SALTS_OK) return status;
  free(impl->completions);
  free(impl->pending_events);
  free(impl->session_work);
  free(impl->free_requests);
  free(impl->request_records);
  free(impl->session_records);
  free(impl);
  owner->impl = NULL;
  return SALTS_OK;
}
