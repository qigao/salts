from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{path}: regex expected one match, got {count}: {pattern[:120]!r}")
    p.write_text(updated)


header = "chttp/src/chttp_h2_server.h"
replace_once(
    header,
    """int chttp_h2_server_connection_flush(chttp_h2_server_connection *h2);
int chttp_h2_server_connection_begin_stop(chttp_h2_server_connection *h2);
""",
    """int chttp_h2_server_connection_flush(chttp_h2_server_connection *h2);
int chttp_h2_server_connection_deferred_progress(chttp_h2_server_connection *h2);
bool chttp_h2_server_connection_deferred_active(const chttp_h2_server_connection *h2);
int chttp_h2_server_connection_begin_stop(chttp_h2_server_connection *h2);
""",
)

h2_file = "chttp/src/chttp_h2_server.c"
replace_once(
    h2_file,
    """  size_t websocket_output_size;
  int32_t stream_id;
  chttp_method method;
""",
    """  size_t websocket_output_size;
  int32_t stream_id;
  uint32_t generation;
  chttp_server_deferred_control *deferred_control;
  uint32_t deferred_generation;
  chttp_method method;
""",
)
replace_once(
    h2_file,
    """  chttp_h2_server_stream *streams;
  size_t stream_capacity;
  size_t active_streams;
""",
    """  chttp_h2_server_stream *streams;
  size_t stream_capacity;
  size_t active_streams;
  chttp_server_deferred_control *deferred_controls;
  size_t deferred_control_capacity;
""",
)
replace_once(
    h2_file,
    """  bool drain_ping_acked;
};

static bool chttp_h2_server_add""",
    """  bool drain_ping_acked;
};

static int chttp_h2_server_deferred_acquire(
    void *user, chttp_server_response_builder *base_builder,
    chttp_server_deferred_control **out_control);
static int chttp_h2_server_submit_response_from(
    chttp_h2_server_stream *stream, chttp_server_response_builder *builder);

static bool chttp_h2_server_add""",
)
replace_once(
    h2_file,
    """  stream->websocket_output_size = 0u;
  stream->stream_id = 0;
  stream->method = (chttp_method)0;
""",
    """  stream->websocket_output_size = 0u;
  stream->stream_id = 0;
  stream->deferred_control = NULL;
  stream->deferred_generation = 0u;
  stream->method = (chttp_method)0;
""",
)

marker = """chttp_server_websocket_peer *chttp_h2_server_websocket_peer_find(
    chttp_h2_server_connection *h2, int32_t stream_id) {
  chttp_h2_server_stream *stream = chttp_h2_server_stream_find(h2, stream_id);
  if (stream == NULL || stream->websocket_peer.phase == CHTTP_SERVER_WEBSOCKET_NONE) return NULL;
  return &stream->websocket_peer;
}

"""
helpers = marker + """static bool chttp_h2_server_deferred_exact(
    const chttp_server_deferred_control *control, const chttp_h2_server_stream *stream) {
  return control != NULL && stream != NULL && stream->active &&
         control->transport_kind == CHTTP_SERVER_DEFERRED_TRANSPORT_H2 &&
         control->transport == stream && control->transport_generation == stream->generation &&
         stream->deferred_control == control &&
         stream->deferred_generation == control->generation;
}

static void chttp_h2_server_deferred_idle(chttp_server_deferred_control *control) {
  if (control == NULL) return;
  if (control->reply_builder_initialized)
    chttp_server_response_builder_reset(&control->reply_builder);
  control->base_builder = NULL;
  control->transport = NULL;
  control->transport_generation = 0u;
  atomic_store_explicit(&control->cancel_requested, 0, memory_order_release);
  atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE, memory_order_release);
}

static int chttp_h2_server_deferred_acquire(
    void *user, chttp_server_response_builder *base_builder,
    chttp_server_deferred_control **out_control) {
  chttp_h2_server_stream *stream = (chttp_h2_server_stream *)user;
  chttp_h2_server_connection *h2;
  size_t index;
  if (stream == NULL || base_builder == NULL || out_control == NULL || !stream->active ||
      stream->owner == NULL)
    return SALTS_EINVAL;
  *out_control = NULL;
  if (stream->deferred_control != NULL) return SALTS_EALREADY;
  h2 = stream->owner;
  for (index = 0u; index < h2->deferred_control_capacity; ++index) {
    chttp_server_deferred_control *control = &h2->deferred_controls[index];
    int expected = CHTTP_SERVER_DEFERRED_IDLE;
    int status;
    if (!atomic_compare_exchange_strong_explicit(
            &control->state, &expected, CHTTP_SERVER_DEFERRED_WRITING, memory_order_acq_rel,
            memory_order_acquire))
      continue;
    ++control->generation;
    if (control->generation == 0u) ++control->generation;
    control->server = h2->connection->server;
    control->transport_kind = CHTTP_SERVER_DEFERRED_TRANSPORT_H2;
    control->transport = stream;
    control->transport_generation = stream->generation;
    control->base_builder = base_builder;
    atomic_store_explicit(&control->cancel_requested, 0, memory_order_release);
    if (!control->reply_builder_initialized) {
      status = chttp_server_response_builder_init(&control->reply_builder,
                                                  &h2->connection->server->config);
      if (status != SALTS_OK) {
        control->base_builder = NULL;
        control->transport = NULL;
        control->transport_generation = 0u;
        atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE,
                              memory_order_release);
        return status;
      }
      control->reply_builder.server = h2->connection->server;
      control->reply_response.impl = &control->reply_builder;
      control->reply_builder_initialized = true;
    }
    stream->deferred_control = control;
    stream->deferred_generation = control->generation;
    *out_control = control;
    return SALTS_OK;
  }
  return SALTS_ENOBUFS;
}

"""
replace_once(h2_file, marker, helpers)

replace_once(
    h2_file,
    """    chttp_h2_server_stream_reset(stream);
    stream->owner = h2;
    stream->active = true;
    stream->stream_id = stream_id;
    ++h2->active_streams;
""",
    """    chttp_h2_server_stream_reset(stream);
    stream->owner = h2;
    stream->active = true;
    stream->stream_id = stream_id;
    ++stream->generation;
    if (stream->generation == 0u) ++stream->generation;
    stream->request_state.response_builder.defer_target =
        (chttp_server_defer_target){chttp_h2_server_deferred_acquire, stream};
    ++h2->active_streams;
""",
)

replace_once(
    h2_file,
    """static int chttp_h2_server_submit_response(chttp_h2_server_stream *stream) {
  chttp_server_response_builder *builder = &stream->request_state.response_builder;
  chttp_server_impl *server = stream->owner->connection->server;
""",
    """static int chttp_h2_server_submit_response_from(
    chttp_h2_server_stream *stream, chttp_server_response_builder *builder) {
  chttp_server_impl *server;
  if (stream == NULL || builder == NULL || stream->owner == NULL) return SALTS_EINVAL;
  server = stream->owner->connection->server;
""",
)
replace_once(
    h2_file,
    """  if (submit_status != 0) {
    stream->response_submitted = false;
    chttp_server_response_builder_close_source(builder, SALTS_ENOBUFS);
    chttp_session_request_abort(&stream->request_state);
    return SALTS_ENOBUFS;
  }
""",
    """  if (submit_status != 0) {
    stream->response_submitted = false;
    chttp_server_response_builder_close_source(builder, SALTS_ENOBUFS);
    return SALTS_ENOBUFS;
  }
""",
)

p = Path(h2_file)
text = p.read_text()
text = text.replace(
    "chttp_h2_server_submit_response(stream)",
    "chttp_h2_server_submit_response_from(stream, &stream->request_state.response_builder)",
)
p.write_text(text)

replace_once(
    h2_file,
    """static int chttp_h2_server_dispatch(chttp_h2_server_stream *stream) {
  chttp_server_request_view request;
  int status;
""",
    """static int chttp_h2_server_dispatch(chttp_h2_server_stream *stream) {
  chttp_server_request_view request;
  chttp_server_response_builder *builder;
  int status;
""",
)
replace_once(
    h2_file,
    """  chttp_server_request_body_close(&stream->request_state, SALTS_OK);
  request = chttp_h2_server_request_view(stream);
  status = chttp_server_dispatch_request(&stream->request_state, &request);
  if (status != SALTS_OK) return status;
  return chttp_h2_server_submit_response_from(stream, &stream->request_state.response_builder);
}
""",
    """  chttp_server_request_body_close(&stream->request_state, SALTS_OK);
  request = chttp_h2_server_request_view(stream);
  builder = &stream->request_state.response_builder;
  builder->request = &request;
  status = chttp_server_dispatch_request(&stream->request_state, &request);
  builder->request = NULL;
  if (status != SALTS_OK) return status;
  if (builder->deferred) return SALTS_OK;
  return chttp_h2_server_submit_response_from(stream, builder);
}
""",
)

submit_tail = """  chttp_server_stats_response(server);
  return SALTS_OK;
}

static chttp_server_request_view
chttp_h2_server_request_view"""
progress = """  chttp_server_stats_response(server);
  return SALTS_OK;
}

int chttp_h2_server_connection_deferred_progress(chttp_h2_server_connection *h2) {
  size_t index;
  if (h2 == NULL || h2->connection == NULL) return SALTS_EINVAL;
  for (index = 0u; index < h2->deferred_control_capacity; ++index) {
    chttp_server_deferred_control *control = &h2->deferred_controls[index];
    chttp_h2_server_stream *stream;
    int status;
    if (chttp_server_deferred_control_state(control) != CHTTP_SERVER_DEFERRED_READY) continue;
    stream = (chttp_h2_server_stream *)control->transport;
    if (!chttp_h2_server_deferred_exact(control, stream)) {
      chttp_h2_server_deferred_idle(control);
      continue;
    }
    status = chttp_session_request_finish(&stream->request_state);
    if (status != SALTS_OK) return status;
    status = chttp_h2_server_submit_response_from(stream, &control->reply_builder);
    if (status == SALTS_ENOBUFS) continue;
    if (status != SALTS_OK) return status;
    if (!chttp_h2_server_deferred_exact(control, stream)) {
      chttp_h2_server_deferred_idle(control);
      continue;
    }
    atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_SUBMITTED,
                          memory_order_release);
    status = chttp_server_send_pending(h2->connection);
    if (status != SALTS_OK && status != SALTS_EBUSY && status != SALTS_ENOBUFS)
      return status;
  }
  return SALTS_OK;
}

bool chttp_h2_server_connection_deferred_active(const chttp_h2_server_connection *h2) {
  size_t index;
  if (h2 == NULL) return false;
  for (index = 0u; index < h2->deferred_control_capacity; ++index)
    if (chttp_server_deferred_control_state(&h2->deferred_controls[index]) !=
        CHTTP_SERVER_DEFERRED_IDLE)
      return true;
  return false;
}

static chttp_server_request_view
chttp_h2_server_request_view"""
replace_once(h2_file, submit_tail, progress)

replace_once(
    h2_file,
    """static int chttp_h2_server_stream_close(void *user, int32_t stream_id, uint32_t error_code) {
  chttp_h2_server_connection *h2 = (chttp_h2_server_connection *)user;
  chttp_h2_server_stream *stream = chttp_h2_server_stream_find(h2, stream_id);
  (void)error_code;
  if (stream == NULL) return 0;
  (void)chttp_h2_proto_set_stream_user_data(h2->protocol, stream_id, NULL);
""",
    """static int chttp_h2_server_stream_close(void *user, int32_t stream_id, uint32_t error_code) {
  chttp_h2_server_connection *h2 = (chttp_h2_server_connection *)user;
  chttp_h2_server_stream *stream = chttp_h2_server_stream_find(h2, stream_id);
  chttp_server_deferred_control *control;
  (void)error_code;
  if (stream == NULL) return 0;
  control = stream->deferred_control;
  if (chttp_h2_server_deferred_exact(control, stream) &&
      chttp_server_deferred_control_state(control) == CHTTP_SERVER_DEFERRED_SUBMITTED)
    chttp_h2_server_deferred_idle(control);
  (void)chttp_h2_proto_set_stream_user_data(h2->protocol, stream_id, NULL);
""",
)

replace_once(
    h2_file,
    """  h2->streams = (chttp_h2_server_stream *)calloc(h2->stream_capacity, sizeof(*h2->streams));
  if (h2->streams == NULL) {
    free(h2);
    return SALTS_ENOMEM;
  }
  for (index = 0u; index < h2->stream_capacity; ++index) {
""",
    """  h2->streams = (chttp_h2_server_stream *)calloc(h2->stream_capacity, sizeof(*h2->streams));
  h2->deferred_control_capacity = h2->stream_capacity;
  h2->deferred_controls = (chttp_server_deferred_control *)calloc(
      h2->deferred_control_capacity, sizeof(*h2->deferred_controls));
  if (h2->streams == NULL || h2->deferred_controls == NULL) {
    free(h2->deferred_controls);
    free(h2->streams);
    free(h2);
    return SALTS_ENOMEM;
  }
  for (index = 0u; index < h2->deferred_control_capacity; ++index) {
    chttp_server_deferred_control *control = &h2->deferred_controls[index];
    control->server = connection->server;
    control->transport_kind = CHTTP_SERVER_DEFERRED_TRANSPORT_H2;
    atomic_init(&control->state, CHTTP_SERVER_DEFERRED_IDLE);
    atomic_init(&control->cancel_requested, 0);
  }
  for (index = 0u; index < h2->stream_capacity; ++index) {
""",
)

replace_once(
    h2_file,
    """int chttp_h2_server_connection_prepare(chttp_h2_server_connection *h2) {
  chttp_h2_proto_callbacks callbacks = {0};
  size_t index;
  if (h2 == NULL || h2->connection == NULL) return SALTS_EINVAL;
  chttp_h2_proto_destroy(h2->protocol);
""",
    """int chttp_h2_server_connection_prepare(chttp_h2_server_connection *h2) {
  chttp_h2_proto_callbacks callbacks = {0};
  size_t index;
  if (h2 == NULL || h2->connection == NULL) return SALTS_EINVAL;
  if (chttp_h2_server_connection_deferred_active(h2)) return SALTS_EBUSY;
  chttp_h2_proto_destroy(h2->protocol);
""",
)

replace_once(
    h2_file,
    """void chttp_h2_server_connection_destroy(chttp_h2_server_connection *h2) {
  size_t index;
  if (h2 == NULL) return;
  chttp_h2_proto_destroy(h2->protocol);
  if (h2->streams != NULL)
    for (index = 0u; index < h2->stream_capacity; ++index)
      chttp_h2_server_stream_destroy(&h2->streams[index]);
  free(h2->streams);
  free(h2);
}
""",
    """void chttp_h2_server_connection_destroy(chttp_h2_server_connection *h2) {
  size_t index;
  if (h2 == NULL) return;
  chttp_h2_proto_destroy(h2->protocol);
  if (h2->streams != NULL)
    for (index = 0u; index < h2->stream_capacity; ++index)
      chttp_h2_server_stream_destroy(&h2->streams[index]);
  if (h2->deferred_controls != NULL)
    for (index = 0u; index < h2->deferred_control_capacity; ++index)
      if (h2->deferred_controls[index].reply_builder_initialized)
        chttp_server_response_builder_destroy(&h2->deferred_controls[index].reply_builder);
  free(h2->deferred_controls);
  free(h2->streams);
  free(h2);
}
""",
)

replace_once(
    h2_file,
    """bool chttp_h2_server_connection_stop_ready(const chttp_h2_server_connection *h2) {
  return h2 != NULL && h2->protocol != NULL && h2->draining && h2->drain_ping_acked &&
         h2->active_streams == 0u && !chttp_h2_proto_want_write(h2->protocol) &&
""",
    """bool chttp_h2_server_connection_stop_ready(const chttp_h2_server_connection *h2) {
  return h2 != NULL && h2->protocol != NULL && h2->draining && h2->drain_ping_acked &&
         h2->active_streams == 0u && !chttp_h2_server_connection_deferred_active(h2) &&
         !chttp_h2_proto_want_write(h2->protocol) &&
""",
)
replace_once(
    h2_file,
    """bool chttp_h2_server_connection_stop_waiting(const chttp_h2_server_connection *h2) {
  return h2 != NULL && h2->protocol != NULL && h2->draining && h2->drain_ping_sent &&
         !h2->drain_ping_acked && h2->active_streams == 0u &&
         !chttp_h2_proto_want_write(h2->protocol) && h2->connection->outbound_size == 0u &&
""",
    """bool chttp_h2_server_connection_stop_waiting(const chttp_h2_server_connection *h2) {
  return h2 != NULL && h2->protocol != NULL && h2->draining && h2->drain_ping_sent &&
         !h2->drain_ping_acked && h2->active_streams == 0u &&
         !chttp_h2_server_connection_deferred_active(h2) &&
         !chttp_h2_proto_want_write(h2->protocol) && h2->connection->outbound_size == 0u &&
""",
)

server = "chttp/src/chttp_server.c"
replace_once(
    server,
    """  for (index = 0u; index < server->config.network.connection_capacity; ++index)
    if (!server->connections[index].active &&
        atomic_load_explicit(&server->connections[index].deferred_control.state, memory_order_acquire) ==
            CHTTP_SERVER_DEFERRED_IDLE)
      return &server->connections[index];
""",
    """  for (index = 0u; index < server->config.network.connection_capacity; ++index)
    if (!server->connections[index].active &&
        atomic_load_explicit(&server->connections[index].deferred_control.state,
                             memory_order_acquire) == CHTTP_SERVER_DEFERRED_IDLE &&
        (server->connections[index].h2 == NULL ||
         !chttp_h2_server_connection_deferred_active(server->connections[index].h2)))
      return &server->connections[index];
""",
)
replace_once(
    server,
    """    chttp_server_deferred_control *control = &connection->deferred_control;
    chttp_server_response_builder *builder = &control->reply_builder;
    int status;
    if (atomic_load_explicit(&connection->deferred_control.state, memory_order_acquire) !=
        CHTTP_SERVER_DEFERRED_READY)
      continue;
""",
    """    chttp_server_deferred_control *control = &connection->deferred_control;
    chttp_server_response_builder *builder = &control->reply_builder;
    int status;
    if (connection->wire_protocol == CHTTP_SERVER_WIRE_HTTP_2 && connection->h2 != NULL) {
      status = chttp_h2_server_connection_deferred_progress(connection->h2);
      if (status != SALTS_OK && !chttp_server_action_pressure(status)) return status;
      continue;
    }
    if (atomic_load_explicit(&connection->deferred_control.state, memory_order_acquire) !=
        CHTTP_SERVER_DEFERRED_READY)
      continue;
""",
)
replace_once(
    server,
    """  for (index = 0u; index < server->config.network.connection_capacity; ++index)
    if (server->connections[index].active ||
        atomic_load_explicit(&server->connections[index].deferred_control.state, memory_order_acquire) !=
            CHTTP_SERVER_DEFERRED_IDLE)
      return true;
""",
    """  for (index = 0u; index < server->config.network.connection_capacity; ++index)
    if (server->connections[index].active ||
        atomic_load_explicit(&server->connections[index].deferred_control.state,
                             memory_order_acquire) != CHTTP_SERVER_DEFERRED_IDLE ||
        (server->connections[index].h2 != NULL &&
         chttp_h2_server_connection_deferred_active(server->connections[index].h2)))
      return true;
""",
)

# Sanity checks: Task 2 must stay in the H2 server adapter and common owner loop.
h2_text = Path(h2_file).read_text()
if "chttp_h2_server_submit_response(" in h2_text:
    raise SystemExit("h2 adapter still contains old submit_response helper")
if "chttp_h2_proto.c" in h2_text:
    raise SystemExit("unexpected protocol-core reference in adapter patch")
if "deferred_control_capacity" not in h2_text or "CHTTP_SERVER_DEFERRED_SUBMITTED" not in h2_text:
    raise SystemExit("Task 2 control pool/progress patch incomplete")
