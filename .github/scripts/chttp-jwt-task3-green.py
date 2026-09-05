from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


def replace_region(path, start, end, new):
    text = read(path)
    if text.count(start) != 1:
        raise SystemExit(f"{path}: start marker count != 1: {start!r}")
    i = text.index(start)
    j = text.find(end, i + len(start))
    if j < 0:
        raise SystemExit(f"{path}: end marker not found: {end!r}")
    write(path, text[:i] + new + text[j:])


replace_once(
    "chttp/include/chttp/chttp.h",
    "/** Owns the key and expected claim values used by Bearer middleware. */",
    "/** Owns the key and expected claim values used by Bearer admission. */",
)
replace_once(
    "chttp/include/chttp/chttp.h",
    "  /** NULL unless JWT Bearer middleware authenticated this callback. */",
    "  /** NULL unless JWT Bearer admission authenticated this callback. */",
)
replace_once(
    "chttp/include/chttp/chttp.h",
    """/** Validates one HS256 Authorization: Bearer header and exposes claims to subsequent handlers. */
int chttp_jwt_bearer_middleware(void *user, const chttp_server_request_view *request,
                                 chttp_server_response *response, chttp_server_next *next);

""",
    "",
)
replace_once(
    "chttp/include/chttp/chttp.h",
    """int chttp_server_route_with_jwt_bearer(chttp_server *server,
                                       const chttp_server_route_options *options,
                                       chttp_jwt_bearer_validator *validator);
""",
    """int chttp_server_route_with_jwt_bearer(chttp_server *server,
                                       const chttp_server_route_options *options,
                                       chttp_jwt_bearer_validator *validator);

/** Installs one server-wide JWT Bearer admission policy before server start. */
int chttp_server_use_jwt_bearer(chttp_server *server,
                                chttp_jwt_bearer_validator *validator);
""",
)

replace_once(
    "chttp/src/chttp_server_runtime.h",
    """  void *jwt_owner;
  chttp_jwt_claims_view jwt_claims;
  bool jwt_body_rejected;
  chttp_server_route_record *body_route;
""",
    """  void *jwt_owner;
  chttp_jwt_claims_view jwt_claims;
  chttp_server_route_record *admitted_route;
  unsigned int admitted_allowed_methods;
  unsigned int admitted_fallback_status;
  bool admission_complete;
  bool admission_rejected;
  chttp_server_route_record *body_route;
""",
)
replace_once(
    "chttp/src/chttp_server_runtime.h",
    """  chttp_server_middleware *route_middleware;
  chttp_server_middleware *middleware;
  size_t route_count;
""",
    """  chttp_server_middleware *route_middleware;
  chttp_server_middleware *middleware;
  chttp_jwt_bearer_validator *jwt_bearer_validator;
  size_t route_count;
""",
)
replace_once(
    "chttp/src/chttp_server_runtime.h",
    """void chttp_server_request_state_destroy(chttp_server_request_state *state);
int chttp_server_dispatch_request(chttp_server_request_state *state,
""",
    """void chttp_server_request_state_destroy(chttp_server_request_state *state);
int chttp_server_request_admit(chttp_server_request_state *state,
                               const chttp_server_request_view *request,
                               chttp_method route_method);
int chttp_server_dispatch_request(chttp_server_request_state *state,
""",
)

replace_region(
    "chttp/src/chttp_server_route.c",
    "int chttp_server_route_with_jwt_bearer(",
    "static int chttp_server_websocket_upgrade_required",
    """int chttp_server_route_with_jwt_bearer(chttp_server *server,
                                       const chttp_server_route_options *options,
                                       chttp_jwt_bearer_validator *validator) {
  chttp_server_impl *impl;
  chttp_server_route_record *route;
  int status;
  if (server == NULL || server->impl == NULL || options == NULL || validator == NULL ||
      validator->impl == NULL)
    return SALTS_EINVAL;
  impl = (chttp_server_impl *)server->impl;
  status = chttp_server_route_register(impl, options);
  if (status != SALTS_OK) return status;
  route = &impl->routes[impl->route_count - 1u];
  route->jwt_bearer_validator = validator;
  return SALTS_OK;
}

""",
)
replace_region(
    "chttp/src/chttp_server_route.c",
    "int chttp_server_use(chttp_server *server, chttp_server_middleware_fn middleware, void *user) {",
    "static int chttp_server_route_param_copy",
    """int chttp_server_use(chttp_server *server, chttp_server_middleware_fn middleware, void *user) {
  chttp_server_impl *impl;
  if (server == NULL || server->impl == NULL || middleware == NULL) return SALTS_EINVAL;
  impl = (chttp_server_impl *)server->impl;
  if (impl->start_called) return SALTS_EBUSY;
  if (impl->middleware_count >= impl->config.middleware_capacity) return SALTS_ENOBUFS;
  impl->middleware[impl->middleware_count++] = (chttp_server_middleware){middleware, user};
  return SALTS_OK;
}

int chttp_server_use_jwt_bearer(chttp_server *server,
                                chttp_jwt_bearer_validator *validator) {
  chttp_server_impl *impl;
  if (server == NULL || server->impl == NULL || validator == NULL || validator->impl == NULL)
    return SALTS_EINVAL;
  impl = (chttp_server_impl *)server->impl;
  if (impl->start_called) return SALTS_EBUSY;
  if (impl->jwt_bearer_validator != NULL) return SALTS_EALREADY;
  impl->jwt_bearer_validator = validator;
  return SALTS_OK;
}

""",
)

replace_once(
    "chttp/src/chttp_jwt.c",
    """  state->jwt_owner = NULL;
  state->jwt_claims = (chttp_jwt_claims_view){0};
  state->jwt_body_rejected = false;
""",
    """  state->jwt_owner = NULL;
  state->jwt_claims = (chttp_jwt_claims_view){0};
""",
)
jwt = read("chttp/src/chttp_jwt.c")
middleware_start = "\nint chttp_jwt_bearer_middleware("
if jwt.count(middleware_start) != 1:
    raise SystemExit("chttp/src/chttp_jwt.c: middleware tail marker mismatch")
jwt = jwt[:jwt.index(middleware_start)].rstrip() + "\n"
write("chttp/src/chttp_jwt.c", jwt)

replace_once(
    "chttp/src/chttp_server.c",
    """static int chttp_server_on_request(void *user, const chttp_server_request_view *request);
static int chttp_server_on_continue(void *user);
""",
    """static int chttp_server_on_request(void *user, const chttp_server_request_view *request);
static int chttp_server_on_headers(void *user, const chttp_server_request_view *request,
                                   chttp_server_parser_headers_action *out_action);
static int chttp_server_on_continue(void *user);
""",
)
replace_once(
    "chttp/src/chttp_server.c",
    """void chttp_server_request_state_reset(chttp_server_request_state *state) {
  if (state == NULL) return;
  chttp_server_request_body_close(state, SALTS_ECANCELED);
  chttp_jwt_request_state_reset(state);
  chttp_server_response_builder_reset(&state->response_builder);
""",
    """void chttp_server_request_state_reset(chttp_server_request_state *state) {
  if (state == NULL) return;
  chttp_server_request_body_close(state, SALTS_ECANCELED);
  chttp_server_request_admission_clear(state);
  chttp_server_response_builder_reset(&state->response_builder);
""",
)

admission_impl = """static void chttp_server_request_admission_clear(chttp_server_request_state *state) {
  if (state == NULL) return;
  chttp_jwt_request_state_reset(state);
  state->admitted_route = NULL;
  state->admitted_allowed_methods = 0u;
  state->admitted_fallback_status = 0u;
  state->admission_complete = false;
  state->admission_rejected = false;
  state->param_storage_used = 0u;
  state->param_count = 0u;
}

int chttp_server_request_admit(chttp_server_request_state *state,
                               const chttp_server_request_view *request,
                               chttp_method route_method) {
  chttp_server_route_record *route;
  chttp_jwt_bearer_validator *validator;
  unsigned int allowed_methods = 0u;
  int route_status = SALTS_OK;
  int status;

  if (state == NULL || state->server == NULL || request == NULL || state->admission_complete)
    return SALTS_EINVAL;

  chttp_server_stats_request(state->server);
  route = chttp_server_route_find(state, route_method, request->path, &allowed_methods,
                                  &route_status);
  state->admitted_route = route;
  state->admitted_allowed_methods = allowed_methods;
  state->admitted_fallback_status = allowed_methods != 0u ? 405u : 404u;
  if (route_status == SALTS_ENOBUFS)
    state->admitted_fallback_status = 414u;
  else if (route_status != SALTS_OK)
    return route_status;

  state->admission_complete = true;
  validator = route != NULL && route->jwt_bearer_validator != NULL
                  ? route->jwt_bearer_validator
                  : state->server->jwt_bearer_validator;
  if (validator == NULL) return SALTS_OK;

  status = chttp_jwt_bearer_request_validate(state, request, validator);
  if (status != SALTS_OK) state->admission_rejected = true;
  return status;
}

"""
server = read("chttp/src/chttp_server.c")
marker = "void chttp_server_request_state_reset(chttp_server_request_state *state) {"
if server.count(marker) != 1:
    raise SystemExit("server reset insertion marker mismatch")
server = server.replace(marker, admission_impl + marker, 1)
write("chttp/src/chttp_server.c", server)

replace_once(
    "chttp/src/chttp_server.c",
    """      .max_body_bytes = server->config.max_request_body_bytes,
      .on_request = chttp_server_on_request,
      .on_continue = chttp_server_on_continue,
""",
    """      .max_body_bytes = server->config.max_request_body_bytes,
      .on_request = chttp_server_on_request,
      .on_headers = chttp_server_on_headers,
      .on_continue = chttp_server_on_continue,
""",
)

on_headers_impl = """static int chttp_server_on_headers(void *user, const chttp_server_request_view *request,
                                   chttp_server_parser_headers_action *out_action) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  chttp_server_request_state *state;
  chttp_server_request_view routed_request;
  chttp_server_response_builder *builder;
  int status;

  if (connection == NULL || request == NULL || out_action == NULL) return SALTS_EINVAL;
  *out_action = CHTTP_SERVER_HEADERS_CONTINUE;
  state = &connection->request_state;
  chttp_server_request_admission_clear(state);
  chttp_server_response_builder_reset(&state->response_builder);

  routed_request = *request;
  chttp_server_request_enrich(connection, &routed_request);
  status = chttp_server_request_admit(state, &routed_request, routed_request.method);
  if (status == SALTS_OK) return SALTS_OK;
  if (status != SALTS_EPERM) return status;

  routed_request.protocol_keep_alive = 0;
  builder = &state->response_builder;
  builder->request = &routed_request;
  status = chttp_jwt_bearer_unauthorized_response(&state->response);
  if (status == SALTS_OK)
    status = chttp_server_connection_reserve_outbound(
        connection, connection->outbound_size + connection->server->max_response_wire_bytes);
  if (status == SALTS_OK)
    status = chttp_server_response_serialize(builder, &routed_request, connection->outbound,
                                             connection->outbound_capacity,
                                             &connection->outbound_size);
  builder->request = NULL;
  if (status != SALTS_OK) return status;

  chttp_server_stats_response(connection->server);
  connection->close_after_write = true;
  *out_action = CHTTP_SERVER_HEADERS_STOP;
  return SALTS_OK;
}

"""
server = read("chttp/src/chttp_server.c")
marker = "static int chttp_server_on_continue(void *user) {"
if server.count(marker) != 1:
    raise SystemExit("on_continue insertion marker mismatch")
server = server.replace(marker, on_headers_impl + marker, 1)
write("chttp/src/chttp_server.c", server)

replace_once(
    "chttp/src/chttp_server.c",
    """  if (status != SALTS_OK) return status;
  if (consumed < size) {
    const size_t remaining = size - consumed;
""",
    """  if (status != SALTS_OK) return status;
  if (consumed < size) {
    const size_t remaining = size - consumed;
    if (connection->close_after_write) return SALTS_OK;
""",
)

replace_region(
    "chttp/src/chttp_server.c",
    "static int chttp_server_discard_body(void *user, const void *data, size_t size) {",
    "int chttp_server_request_body_write(chttp_server_request_state *state, const void *data,",
    """int chttp_server_request_body_open(chttp_server_request_state *state,
                                   const chttp_server_request_view *request,
                                   chttp_body_sink *out_sink) {
  chttp_server_request_view routed_request;
  chttp_server_route_record *route;
  chttp_server_impl *previous_callback_server;
  int status;

  if (state == NULL || state->server == NULL || request == NULL || out_sink == NULL)
    return SALTS_EINVAL;
  *out_sink = (chttp_body_sink){0};
  if (state->body_sink_open) return SALTS_EBUSY;
  if (!state->admission_complete || state->admission_rejected) return SALTS_EPERM;

  state->body_route = NULL;
  state->body_sink = (chttp_body_sink){0};
  state->body_was_streamed = false;
  route = state->admitted_route;
  if (route == NULL || route->body_open == NULL) return SALTS_OK;

  routed_request = *request;
  routed_request.params = state->params;
  routed_request.param_count = state->param_count;
  routed_request.session = NULL;
  routed_request.jwt_claims = state->jwt_owner != NULL ? &state->jwt_claims : NULL;
  previous_callback_server = chttp_active_callback_server;
  chttp_active_callback_server = state->server;
  status = route->body_open(route->user, &routed_request, out_sink);
  chttp_active_callback_server = previous_callback_server;
  if (status != SALTS_OK) {
    *out_sink = (chttp_body_sink){0};
    return status;
  }
  if (out_sink->write == NULL) return SALTS_EINVAL;

  state->body_route = route;
  state->body_sink = *out_sink;
  state->body_sink_open = true;
  state->body_was_streamed = true;
  return SALTS_OK;
}

""",
)

replace_region(
    "chttp/src/chttp_server.c",
    "int chttp_server_dispatch_request(chttp_server_request_state *state,",
    "static int chttp_server_on_request(void *user, const chttp_server_request_view *request) {",
    """int chttp_server_dispatch_request(chttp_server_request_state *state,
                                  const chttp_server_request_view *request) {
  chttp_server_impl *server;
  chttp_server_request_view routed_request;
  chttp_server_route_record *route;
  chttp_server_chain chain;
  chttp_server_impl *previous_callback_server;
  int status;

  if (state == NULL || state->server == NULL || request == NULL) return SALTS_EINVAL;
  if (!state->admission_complete || state->admission_rejected) return SALTS_EPERM;

  server = state->server;
  route = state->admitted_route;
  routed_request = *request;
  routed_request.params = state->params;
  routed_request.param_count = state->param_count;
  routed_request.session = server->config.session_capacity == 0u ? NULL : &state->session;
  routed_request.jwt_claims = state->jwt_owner != NULL ? &state->jwt_claims : NULL;
  chttp_server_response_builder_reset(&state->response_builder);
  chttp_session_request_begin(state, &routed_request);
  chain = (chttp_server_chain){.server = server,
                               .request_state = state,
                               .request = &routed_request,
                               .response = &state->response,
                               .route = route,
                               .fallback_status = state->admitted_fallback_status,
                               .allowed_methods = state->admitted_allowed_methods};
  previous_callback_server = chttp_active_callback_server;
  chttp_active_callback_server = server;
  status = chttp_server_chain_run(&chain);
  chttp_active_callback_server = previous_callback_server;
  if (status == SALTS_OK && state->response_builder.deferred) return SALTS_OK;
  if (status == SALTS_OK && !state->response_builder.replied)
    status = chttp_server_reply(&state->response, 204u, NULL, NULL, 0u);
  if (status == SALTS_OK) status = chttp_session_request_finish(state);
  if (status != SALTS_OK) {
    chttp_session_request_abort(state);
    chttp_server_stats_handler_error(server);
    chttp_server_response_builder_reset(&state->response_builder);
    status = chttp_server_reply(&state->response, 500u, "text/plain", "Internal Server Error", 21u);
  }
  return status;
}

""",
)
