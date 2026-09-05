from pathlib import Path


def patch(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


path = "chttp/src/chttp_h2_server.c"

patch(
    path,
    """static int chttp_h2_server_websocket_dispatch(chttp_h2_server_stream *stream) {
  chttp_server_request_view request;
  chttp_server_route_record *route;
  unsigned int allowed_methods = 0u;
  int route_status = SALTS_OK;
  int status;
  if (!stream->extended_connect || !stream->protocol_seen || !stream->websocket_version_seen ||
      stream->content_length_seen || stream->body_size != 0u)
    return SALTS_EPROTO;
  request = chttp_h2_server_request_view(stream);
  route = chttp_server_route_find(&stream->request_state, CHTTP_METHOD_GET, request.path,
                                  &allowed_methods, &route_status);
  (void)allowed_methods;
  if (route_status != SALTS_OK) return route_status;
  if (route == NULL || !route->websocket) {
    chttp_server_stats_request(stream->owner->connection->server);
    return chttp_h2_server_websocket_status(stream, 404u);
  }
""",
    """static int chttp_h2_server_websocket_dispatch(chttp_h2_server_stream *stream) {
  chttp_server_request_view request;
  chttp_server_route_record *route;
  int status;
  if (!stream->extended_connect || !stream->protocol_seen || !stream->websocket_version_seen ||
      stream->content_length_seen || stream->body_size != 0u)
    return SALTS_EPROTO;
  request = chttp_h2_server_request_view(stream);
  status = chttp_server_request_admit(&stream->request_state, &request, CHTTP_METHOD_GET);
  if (status == SALTS_EPERM) {
    chttp_server_response_builder_reset(&stream->request_state.response_builder);
    status = chttp_jwt_bearer_unauthorized_response(&stream->request_state.response);
    if (status == SALTS_OK) status = chttp_h2_server_submit_response(stream);
    return status;
  }
  if (status != SALTS_OK) return status;
  route = stream->request_state.admitted_route;
  if (route == NULL || !route->websocket)
    return chttp_h2_server_websocket_status(stream, 404u);
""",
)
