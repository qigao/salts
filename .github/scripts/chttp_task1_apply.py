from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:96]!r}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{path}: regex expected one match, got {count}: {pattern[:96]!r}")
    p.write_text(updated)


runtime = "chttp/src/chttp_server_runtime.h"
replace_once(
    runtime,
    """typedef enum chttp_server_wire_protocol {
  CHTTP_SERVER_WIRE_UNKNOWN = 0,
  CHTTP_SERVER_WIRE_HTTP_1_1,
  CHTTP_SERVER_WIRE_HTTP_2
} chttp_server_wire_protocol;

""",
    """typedef enum chttp_server_wire_protocol {
  CHTTP_SERVER_WIRE_UNKNOWN = 0,
  CHTTP_SERVER_WIRE_HTTP_1_1,
  CHTTP_SERVER_WIRE_HTTP_2
} chttp_server_wire_protocol;

typedef struct chttp_server_response_builder chttp_server_response_builder;
typedef struct chttp_server_deferred_control chttp_server_deferred_control;

typedef enum chttp_server_deferred_transport {
  CHTTP_SERVER_DEFERRED_TRANSPORT_NONE = 0,
  CHTTP_SERVER_DEFERRED_TRANSPORT_H1,
  CHTTP_SERVER_DEFERRED_TRANSPORT_H2
} chttp_server_deferred_transport;

typedef int (*chttp_server_deferred_acquire_fn)(
    void *user, chttp_server_response_builder *base_builder,
    chttp_server_deferred_control **out_control);

typedef struct chttp_server_defer_target {
  chttp_server_deferred_acquire_fn acquire;
  void *user;
} chttp_server_defer_target;

""",
)
replace_once(
    runtime,
    """typedef struct chttp_server_response_builder {
  chttp_server_impl *server;
  chttp_server_connection *connection;
""",
    """struct chttp_server_response_builder {
  chttp_server_impl *server;
  chttp_server_defer_target defer_target;
""",
)
replace_once(
    runtime,
    """  bool replied;
  bool source_enabled;
  bool deferred;
} chttp_server_response_builder;
""",
    """  bool replied;
  bool source_enabled;
  bool deferred;
};
""",
)
replace_once(
    runtime,
    """typedef enum chttp_server_deferred_state {
  CHTTP_SERVER_DEFERRED_IDLE = 0,
  CHTTP_SERVER_DEFERRED_PENDING,
  CHTTP_SERVER_DEFERRED_WRITING,
  CHTTP_SERVER_DEFERRED_READY
} chttp_server_deferred_state;

""",
    """typedef enum chttp_server_deferred_state {
  CHTTP_SERVER_DEFERRED_IDLE = 0,
  CHTTP_SERVER_DEFERRED_PENDING,
  CHTTP_SERVER_DEFERRED_WRITING,
  CHTTP_SERVER_DEFERRED_READY,
  CHTTP_SERVER_DEFERRED_SUBMITTED,
  CHTTP_SERVER_DEFERRED_CANCELED
} chttp_server_deferred_state;

struct chttp_server_deferred_control {
  chttp_server_impl *server;
  chttp_server_response_builder *base_builder;
  chttp_server_response_builder reply_builder;
  chttp_server_response reply_response;
  void *transport;
  atomic_int state;
  atomic_int cancel_requested;
  uint32_t generation;
  uint32_t transport_generation;
  chttp_server_deferred_transport transport_kind;
  bool reply_builder_initialized;
};

""",
)
replace_once(
    runtime,
    """  chttp_server_parser parser;
  chttp_server_request_state request_state;
  chttp_server_response_builder deferred_builder;
  chttp_server_response deferred_response;
  chttp_server_request_view deferred_request;
""",
    """  chttp_server_parser parser;
  chttp_server_request_state request_state;
  chttp_server_deferred_control deferred_control;
  chttp_server_request_view deferred_request;
""",
)
replace_once(
    runtime,
    """  char peer_certificate_sha256[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY];
  atomic_int deferred_state;
  uint32_t deferred_generation;
  bool active;
""",
    """  char peer_certificate_sha256[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY];
  bool active;
""",
)
replace_once(
    runtime,
    """int chttp_server_response_builder_init(chttp_server_response_builder *builder,
                                       const chttp_server_config *config);
""",
    """chttp_server_deferred_state chttp_server_deferred_control_state(
    const chttp_server_deferred_control *control);

int chttp_server_response_builder_init(chttp_server_response_builder *builder,
                                       const chttp_server_config *config);
""",
)

response = "chttp/src/chttp_server_response.c"
replace_once(
    response,
    """static bool chttp_server_response_value(const char *value) {
  const unsigned char *cursor = (const unsigned char *)value;
  if (cursor == NULL) return false;
  for (; *cursor != 0u; ++cursor) {
    if ((*cursor < 32u && *cursor != (unsigned char)'\\t') || *cursor == 127u) return false;
  }
  return true;
}

""",
    """static bool chttp_server_response_value(const char *value) {
  const unsigned char *cursor = (const unsigned char *)value;
  if (cursor == NULL) return false;
  for (; *cursor != 0u; ++cursor) {
    if ((*cursor < 32u && *cursor != (unsigned char)'\\t') || *cursor == 127u) return false;
  }
  return true;
}

chttp_server_deferred_state chttp_server_deferred_control_state(
    const chttp_server_deferred_control *control) {
  if (control == NULL) return CHTTP_SERVER_DEFERRED_IDLE;
  return (chttp_server_deferred_state)atomic_load_explicit(&control->state, memory_order_acquire);
}

""",
)
regex_once(
    response,
    r"int chttp_server_response_defer\(chttp_server_response \*response,.*?\n}\n\nint chttp_server_response_source_owned",
    """int chttp_server_response_defer(chttp_server_response *response,
                                chttp_server_deferred *out_deferred) {
  chttp_server_response_builder *builder;
  chttp_server_deferred_control *control = NULL;
  chttp_server_connection *connection;
  int status;
  if (out_deferred != NULL)
    *out_deferred = (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
  if (response == NULL || response->impl == NULL || out_deferred == NULL) return SALTS_EINVAL;
  builder = (chttp_server_response_builder *)response->impl;
  if (builder->request == NULL || builder->server == NULL || builder->defer_target.acquire == NULL)
    return SALTS_ENOTSUP;
  if (chttp_active_callback_server != builder->server) return SALTS_EBUSY;
  if (builder->request->session != NULL) return SALTS_ENOTSUP;
  if (builder->replied || builder->deferred) return SALTS_EALREADY;

  status = builder->defer_target.acquire(builder->defer_target.user, builder, &control);
  if (status != SALTS_OK) return status;
  if (control == NULL) return SALTS_EPROTO;

  if (control->transport_kind == CHTTP_SERVER_DEFERRED_TRANSPORT_H1) {
    connection = (chttp_server_connection *)control->transport;
    if (connection == NULL) {
      atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE, memory_order_release);
      control->base_builder = NULL;
      return SALTS_EPROTO;
    }
    connection->deferred_request = *builder->request;
    connection->deferred_request.target = NULL;
    connection->deferred_request.path = NULL;
    connection->deferred_request.headers = NULL;
    connection->deferred_request.header_count = 0u;
    connection->deferred_request.params = NULL;
    connection->deferred_request.param_count = 0u;
    connection->deferred_request.body = NULL;
    connection->deferred_request.body_size = 0u;
    connection->deferred_request.peer = NULL;
    connection->deferred_request.peer_certificate_sha256 = NULL;
    connection->deferred_request.session = NULL;
    connection->deferred_disconnected = false;
  }
  builder->deferred = true;
  out_deferred->impl = control;
  out_deferred->generation = control->generation;
  out_deferred->reserved = 0u;
  atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_PENDING, memory_order_release);
  return SALTS_OK;
}

int chttp_server_deferred_reply(chttp_server_deferred *deferred,
                                const chttp_server_deferred_response *response) {
  chttp_server_deferred_control *control;
  chttp_server_response_builder *base;
  size_t index;
  int expected = CHTTP_SERVER_DEFERRED_PENDING;
  int status = SALTS_OK;
  if (deferred == NULL || deferred->impl == NULL || deferred->generation == 0u ||
      response == NULL || response->size < sizeof(*response) ||
      (response->header_count != 0u && response->headers == NULL) ||
      (response->body_size != 0u && response->body == NULL))
    return SALTS_EINVAL;
  control = (chttp_server_deferred_control *)deferred->impl;
  if (control->generation != deferred->generation) return SALTS_ENOENT;
  if (!atomic_compare_exchange_strong_explicit(
          &control->state, &expected, CHTTP_SERVER_DEFERRED_WRITING, memory_order_acq_rel,
          memory_order_acquire))
    return expected == CHTTP_SERVER_DEFERRED_IDLE ? SALTS_ENOENT : SALTS_EALREADY;

  base = control->base_builder;
  if (base == NULL || !control->reply_builder_initialized) {
    atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_PENDING, memory_order_release);
    return SALTS_EPROTO;
  }
  chttp_server_response_builder_reset(&control->reply_builder);
  for (index = 0u; status == SALTS_OK && index < base->header_count; ++index)
    status = chttp_server_response_set_header(&control->reply_response, base->headers[index].name,
                                              base->headers[index].value);
  for (index = 0u; status == SALTS_OK && index < response->header_count; ++index) {
    if (response->headers[index].name == NULL || response->headers[index].value == NULL)
      status = SALTS_EINVAL;
    else
      status = chttp_server_response_set_header(&control->reply_response,
                                                response->headers[index].name,
                                                response->headers[index].value);
  }
  if (status == SALTS_OK)
    status = chttp_server_reply(&control->reply_response, response->status_code,
                                response->content_type, response->body, response->body_size);
  if (status != SALTS_OK) {
    chttp_server_response_builder_reset(&control->reply_builder);
    atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_PENDING, memory_order_release);
    return status;
  }
  atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_READY, memory_order_release);
  *deferred = (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
  (void)cnet_client_wake(&control->server->network);
  return SALTS_OK;
}

int chttp_server_response_source_owned""",
)

server = "chttp/src/chttp_server.c"
replace_once(
    server,
    """static int chttp_server_connection_init(chttp_server_impl *server,
                                        chttp_server_connection *connection) {
""",
    """static int chttp_server_h1_deferred_acquire(
    void *user, chttp_server_response_builder *base_builder,
    chttp_server_deferred_control **out_control) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  chttp_server_deferred_control *control;
  int expected = CHTTP_SERVER_DEFERRED_IDLE;
  if (connection == NULL || base_builder == NULL || out_control == NULL) return SALTS_EINVAL;
  *out_control = NULL;
  if (connection->wire_protocol != CHTTP_SERVER_WIRE_HTTP_1_1) return SALTS_ENOTSUP;
  control = &connection->deferred_control;
  if (!atomic_compare_exchange_strong_explicit(
          &control->state, &expected, CHTTP_SERVER_DEFERRED_WRITING, memory_order_acq_rel,
          memory_order_acquire))
    return SALTS_EALREADY;
  ++control->generation;
  if (control->generation == 0u) ++control->generation;
  control->server = connection->server;
  control->transport_kind = CHTTP_SERVER_DEFERRED_TRANSPORT_H1;
  control->transport = connection;
  control->transport_generation = 0u;
  control->base_builder = base_builder;
  atomic_store_explicit(&control->cancel_requested, 0, memory_order_release);
  *out_control = control;
  return SALTS_OK;
}

static int chttp_server_connection_init(chttp_server_impl *server,
                                        chttp_server_connection *connection) {
""",
)
replace_once(
    server,
    """  int status;
  connection->server = server;
  status = chttp_server_request_state_init(&connection->request_state, server);
  if (status != SALTS_OK) return status;
  connection->request_state.response_builder.connection = connection;
  connection->deferred_builder.server = server;
  status = chttp_server_response_builder_init(&connection->deferred_builder, &server->config);
  if (status != SALTS_OK) return status;
  connection->deferred_response.impl = &connection->deferred_builder;
  atomic_init(&connection->deferred_state, CHTTP_SERVER_DEFERRED_IDLE);
""",
    """  chttp_server_deferred_control *control = &connection->deferred_control;
  int status;
  connection->server = server;
  status = chttp_server_request_state_init(&connection->request_state, server);
  if (status != SALTS_OK) return status;
  connection->request_state.response_builder.defer_target =
      (chttp_server_defer_target){chttp_server_h1_deferred_acquire, connection};
  control->server = server;
  control->transport_kind = CHTTP_SERVER_DEFERRED_TRANSPORT_H1;
  atomic_init(&control->state, CHTTP_SERVER_DEFERRED_IDLE);
  atomic_init(&control->cancel_requested, 0);
  status = chttp_server_response_builder_init(&control->reply_builder, &server->config);
  if (status != SALTS_OK) return status;
  control->reply_builder.server = server;
  control->reply_response.impl = &control->reply_builder;
  control->reply_builder_initialized = true;
""",
)
replace_once(
    server,
    "  chttp_server_response_builder_destroy(&connection->deferred_builder);\n",
    """  if (connection->deferred_control.reply_builder_initialized)
    chttp_server_response_builder_destroy(&connection->deferred_control.reply_builder);
""",
)

p = Path(server)
text = p.read_text()
if "deferred_state" not in text:
    raise SystemExit(f"{server}: expected old deferred_state references")
text = text.replace("&connection->deferred_state", "&connection->deferred_control.state")
text = text.replace(
    "&server->connections[index].deferred_state",
    "&server->connections[index].deferred_control.state",
)
text = text.replace(
    "connection->deferred_builder", "connection->deferred_control.reply_builder"
)
p.write_text(text)

replace_once(
    server,
    """static int chttp_server_deferred_progress(chttp_server_impl *server) {
  size_t index;
  for (index = 0u; index < server->config.network.connection_capacity; ++index) {
    chttp_server_connection *connection = &server->connections[index];
    chttp_server_response_builder *builder = &connection->deferred_control.reply_builder;
    int status;
""",
    """static int chttp_server_deferred_progress(chttp_server_impl *server) {
  size_t index;
  for (index = 0u; index < server->config.network.connection_capacity; ++index) {
    chttp_server_connection *connection = &server->connections[index];
    chttp_server_deferred_control *control = &connection->deferred_control;
    chttp_server_response_builder *builder = &control->reply_builder;
    int status;
""",
)
replace_once(
    server,
    """      chttp_server_response_builder_reset(builder);
      (void)chttp_server_parser_reset(&connection->parser);
      connection->deferred_disconnected = false;
      atomic_store_explicit(&connection->deferred_control.state, CHTTP_SERVER_DEFERRED_IDLE,
                            memory_order_release);
""",
    """      chttp_server_response_builder_reset(builder);
      control->base_builder = NULL;
      control->transport = NULL;
      (void)chttp_server_parser_reset(&connection->parser);
      connection->deferred_disconnected = false;
      atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE, memory_order_release);
""",
)
replace_once(
    server,
    """      chttp_server_request_state_reset(&connection->request_state);
      chttp_server_response_builder_reset(builder);
      atomic_store_explicit(&connection->deferred_control.state, CHTTP_SERVER_DEFERRED_IDLE,
                            memory_order_release);
      chttp_server_connection_close(connection);
""",
    """      chttp_server_request_state_reset(&connection->request_state);
      chttp_server_response_builder_reset(builder);
      control->base_builder = NULL;
      control->transport = NULL;
      atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE, memory_order_release);
      chttp_server_connection_close(connection);
""",
)
replace_once(
    server,
    """    connection->deferred_response_writing = true;
    chttp_server_stats_response(server);
    atomic_store_explicit(&connection->deferred_control.state, CHTTP_SERVER_DEFERRED_IDLE,
                          memory_order_release);
""",
    """    connection->deferred_response_writing = true;
    chttp_server_stats_response(server);
    chttp_server_response_builder_reset(builder);
    control->base_builder = NULL;
    control->transport = NULL;
    atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE, memory_order_release);
""",
)

# Task 1 must eliminate connection identity from the public deferred handle path.
for path in (runtime, response, server):
    text = Path(path).read_text()
    if path == runtime and "chttp_server_connection *connection;" in text:
        raise SystemExit("runtime: response builder still stores connection identity")
if "connection = builder->connection" in Path(response).read_text():
    raise SystemExit("response: defer still recovers a connection from the public builder")
