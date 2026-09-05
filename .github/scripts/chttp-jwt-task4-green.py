from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


replace_once(
    "chttp/include/chttp/chttp.h",
    """/** Registers one explicit H1 Upgrade/H2 Extended CONNECT WebSocket route before server start. */
int chttp_server_websocket_with(chttp_server *server,
                                const chttp_server_websocket_options *options);

/** Convenience WebSocket route using bounded defaults and no route middleware. */
""",
    """/** Registers one explicit H1 Upgrade/H2 Extended CONNECT WebSocket route before server start. */
int chttp_server_websocket_with(chttp_server *server,
                                const chttp_server_websocket_options *options);

/** Registers one WebSocket route protected by the supplied JWT Bearer validator. */
int chttp_server_websocket_with_jwt_bearer(
    chttp_server *server, const chttp_server_websocket_options *options,
    chttp_jwt_bearer_validator *validator);

/** Convenience WebSocket route using bounded defaults and no route middleware. */
""",
)

replace_once(
    "chttp/src/chttp_server_route.c",
    """int chttp_server_websocket_with(chttp_server *server,
                                const chttp_server_websocket_options *options) {
  if (server == NULL || server->impl == NULL) return SALTS_EINVAL;
  return chttp_server_websocket_route_register((chttp_server_impl *)server->impl, options);
}

int chttp_server_websocket(chttp_server *server, const char *path, chttp_websocket_open_fn on_open,
""",
    """int chttp_server_websocket_with(chttp_server *server,
                                const chttp_server_websocket_options *options) {
  if (server == NULL || server->impl == NULL) return SALTS_EINVAL;
  return chttp_server_websocket_route_register((chttp_server_impl *)server->impl, options);
}

int chttp_server_websocket_with_jwt_bearer(
    chttp_server *server, const chttp_server_websocket_options *options,
    chttp_jwt_bearer_validator *validator) {
  chttp_server_impl *impl;
  chttp_server_route_record *route;
  int status;
  if (server == NULL || server->impl == NULL || options == NULL || validator == NULL ||
      validator->impl == NULL)
    return SALTS_EINVAL;
  impl = (chttp_server_impl *)server->impl;
  status = chttp_server_websocket_route_register(impl, options);
  if (status != SALTS_OK) return status;
  route = &impl->routes[impl->route_count - 1u];
  route->jwt_bearer_validator = validator;
  return SALTS_OK;
}

int chttp_server_websocket(chttp_server *server, const char *path, chttp_websocket_open_fn on_open,
""",
)

replace_once(
    "chttp/src/chttp_websocket_server.c",
    """  if (peer == NULL || state == NULL || route == NULL || request == NULL || peer->server == NULL ||
      peer->phase == CHTTP_SERVER_WEBSOCKET_NONE)
    return SALTS_EINVAL;
  chttp_server_stats_request(peer->server);
  routed_request = *request;
""",
    """  if (peer == NULL || state == NULL || route == NULL || request == NULL || peer->server == NULL ||
      peer->phase == CHTTP_SERVER_WEBSOCKET_NONE)
    return SALTS_EINVAL;
  if (!state->admission_complete) chttp_server_stats_request(peer->server);
  routed_request = *request;
""",
)
replace_once(
    "chttp/src/chttp_websocket_server.c",
    "  routed_request.jwt_claims = NULL;",
    "  routed_request.jwt_claims = state->jwt_owner != NULL ? &state->jwt_claims : NULL;",
)
replace_once(
    "chttp/src/chttp_websocket_server.c",
    """  chttp_server_request_state *state;
  chttp_server_route_record *route;
  unsigned int allowed_methods = 0u;
  int route_status = SALTS_OK;
  char accept[CHTTP_WEBSOCKET_ACCEPT_CAPACITY];
""",
    """  chttp_server_request_state *state;
  chttp_server_route_record *route;
  char accept[CHTTP_WEBSOCKET_ACCEPT_CAPACITY];
""",
)
replace_once(
    "chttp/src/chttp_websocket_server.c",
    """  state = &connection->request_state;
  route = chttp_server_route_find(state, CHTTP_METHOD_GET, request->path, &allowed_methods,
                                  &route_status);
  (void)allowed_methods;
  if (route_status != SALTS_OK) return route_status;
  if (route == NULL || !route->websocket) return SALTS_OK;
""",
    """  state = &connection->request_state;
  if (!state->admission_complete || state->admission_rejected) return SALTS_EPERM;
  route = state->admitted_route;
  if (route == NULL || !route->websocket) return SALTS_OK;
""",
)
