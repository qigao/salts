from pathlib import Path


def patch(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


path = "chttp/tests/chttp_h2_server_test.c"

patch(
    path,
    """  size_t writes;
  size_t opens;
  size_t closes;
  size_t response_offset;
""",
    """  size_t writes;
  size_t opens;
  size_t closes;
  size_t handler_calls;
  size_t response_offset;
""",
)

patch(
    path,
    """static int chttp_h2_server_stream_open(void *user, const chttp_server_request_view *request,
                                       chttp_body_sink *out_sink) {
  chttp_h2_server_stream_probe *probe = (chttp_h2_server_stream_probe *)user;
  if (probe == NULL || request == NULL || out_sink == NULL || request->http_major != 2u ||
      strcmp(request->path, "/echo") != 0)
    return SALTS_EINVAL;
  ++probe->opens;
  *out_sink = (chttp_body_sink){.write = chttp_h2_server_stream_write, .user = probe};
  return SALTS_OK;
}

static int chttp_h2_server_failing_stream_open""",
    """static int chttp_h2_server_stream_open(void *user, const chttp_server_request_view *request,
                                       chttp_body_sink *out_sink) {
  chttp_h2_server_stream_probe *probe = (chttp_h2_server_stream_probe *)user;
  if (probe == NULL || request == NULL || out_sink == NULL || request->http_major != 2u ||
      strcmp(request->path, "/echo") != 0)
    return SALTS_EINVAL;
  ++probe->opens;
  *out_sink = (chttp_body_sink){.write = chttp_h2_server_stream_write, .user = probe};
  return SALTS_OK;
}

static int chttp_h2_server_jwt_stream_open(void *user,
                                           const chttp_server_request_view *request,
                                           chttp_body_sink *out_sink) {
  chttp_h2_server_stream_probe *probe = (chttp_h2_server_stream_probe *)user;
  if (probe == NULL || request == NULL || out_sink == NULL || request->http_major != 2u ||
      strcmp(request->path, "/jwt-echo") != 0 || request->jwt_claims == NULL ||
      request->jwt_claims->subject == NULL || strcmp(request->jwt_claims->subject, "alice") != 0)
    return SALTS_EPROTO;
  ++probe->opens;
  *out_sink = (chttp_body_sink){.write = chttp_h2_server_stream_write, .user = probe};
  return SALTS_OK;
}

static int chttp_h2_server_failing_stream_open""",
)

patch(
    path,
    """static int chttp_h2_server_stream_handler(void *user, const chttp_server_request_view *request,
                                          chttp_server_response *response) {
  chttp_h2_server_stream_probe *probe = (chttp_h2_server_stream_probe *)user;
  chttp_body_source source;
  if (probe == NULL || request == NULL || !request->body_streamed || request->body != NULL ||
      request->body_size != probe->size || probe->closes != 1u || probe->close_status != SALTS_OK)
    return SALTS_EPROTO;
  source = (chttp_body_source){.read = chttp_h2_server_response_read,
                               .user = probe,
                               .content_length = probe->size,
                               .content_length_known = 1};
  return chttp_server_response_source(response, 200u, "application/octet-stream", &source);
}

static void chttp_h2_stream_complete""",
    """static int chttp_h2_server_stream_handler(void *user, const chttp_server_request_view *request,
                                          chttp_server_response *response) {
  chttp_h2_server_stream_probe *probe = (chttp_h2_server_stream_probe *)user;
  chttp_body_source source;
  if (probe == NULL || request == NULL || !request->body_streamed || request->body != NULL ||
      request->body_size != probe->size || probe->closes != 1u || probe->close_status != SALTS_OK)
    return SALTS_EPROTO;
  source = (chttp_body_source){.read = chttp_h2_server_response_read,
                               .user = probe,
                               .content_length = probe->size,
                               .content_length_known = 1};
  return chttp_server_response_source(response, 200u, "application/octet-stream", &source);
}

static int chttp_h2_server_jwt_stream_handler(void *user,
                                              const chttp_server_request_view *request,
                                              chttp_server_response *response) {
  chttp_h2_server_stream_probe *probe = (chttp_h2_server_stream_probe *)user;
  if (probe == NULL || request == NULL) return SALTS_EPROTO;
  ++probe->handler_calls;
  if (request->jwt_claims == NULL || request->jwt_claims->subject == NULL ||
      strcmp(request->jwt_claims->subject, "alice") != 0 || !request->body_streamed ||
      request->body != NULL || request->body_size != probe->size || probe->closes != 1u ||
      probe->close_status != SALTS_OK)
    return SALTS_EPROTO;
  return chttp_server_reply(response, 200u, "text/plain", "ok", 2u);
}

static void chttp_h2_stream_complete""",
)

patch(
    path,
    """  it("resets a failing response sink without failing an HTTP/2 sibling") {
""",
    """  it("authenticates regular HTTP2 before streaming body admission") {
    static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
    static const unsigned char payload[] = "jwt-h2-body";
    const chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
    const chttp_jwt_bearer_validator_options validator_options = {
        .size = sizeof(validator_options), .key = key, .key_size = sizeof(key) - 1u};
    chttp_h2_stream_probe client_probe = {
        .source_data = payload, .source_size = sizeof(payload) - 1u, .source_chunk = 3u};
    const chttp_body_source source = {.read = chttp_h2_stream_read,
                                      .user = &client_probe,
                                      .content_length = sizeof(payload) - 1u,
                                      .content_length_known = 1};
    chttp_h2_server_stream_probe server_probe = {0};
    chttp_jwt_bearer_validator validator = {0};
    chttp_server server = {0};
    chttp_client client = {0};
    chttp_server_config server_config = chttp_h2_server_test_config();
    chttp_client_config client_config = chttp_h2_server_test_client_config();
    chttp_response response = {0};
    chttp_error error = {0};
    chttp_options options;
    chttp_header authorization = {0};
    char authorization_storage[512];
    char *token = NULL;
    char uri[64];
    char authority[64];
    uint16_t port = 0u;

    client_config.stream_chunk_bytes = 3u;
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &token), SALTS_OK);
    check_equal(chttp_jwt_bearer_header(token, authorization_storage, sizeof(authorization_storage),
                                        &authorization),
                SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_init(&validator, &validator_options), SALTS_OK);
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    {
      const chttp_server_route_options route = {.method = CHTTP_METHOD_POST,
                                                .path = "/jwt-echo",
                                                .handler = chttp_h2_server_jwt_stream_handler,
                                                .user = &server_probe,
                                                .body_open = chttp_h2_server_jwt_stream_open,
                                                .body_close = chttp_h2_server_stream_close};
      check_equal(chttp_server_route_with_jwt_bearer(&server, &route, &validator), SALTS_OK);
    }
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_server_test_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_client_init(&client, &client_config), SALTS_OK);
    options = (chttp_options){.connection_uri = uri,
                              .authority = authority,
                              .target = "/jwt-echo",
                              .body_source = &source,
                              .timeout_ms = CHTTP_H2_SERVER_TEST_TIMEOUT_MS,
                              .protocol = CHTTP_HTTP_2};

    check_equal(chttp_post(&client, &options, &response, &error), SALTS_OK);
    check_equal(response.status_code, 401u);
    check_equal(server_probe.opens, (size_t)0u);
    check_equal(server_probe.writes, (size_t)0u);
    check_equal(server_probe.handler_calls, (size_t)0u);
    chttp_response_destroy(&response);

    client_probe.source_offset = 0u;
    client_probe.source_calls = 0u;
    response = (chttp_response){0};
    error = (chttp_error){0};
    options.headers = &authorization;
    options.header_count = 1u;
    check_equal(chttp_post(&client, &options, &response, &error), SALTS_OK);
    check_equal(response.status_code, 200u);
    check_equal(response.body, "ok", 2u);
    check_equal(server_probe.opens, (size_t)1u);
    check_greater(server_probe.writes, (size_t)0u);
    check_equal(server_probe.closes, (size_t)1u);
    check_equal(server_probe.close_status, SALTS_OK);
    check_equal(server_probe.handler_calls, (size_t)1u);
    check_equal(server_probe.size, sizeof(payload) - 1u);
    check_equal(server_probe.data, payload, sizeof(payload) - 1u);

    chttp_response_destroy(&response);
    check_equal(chttp_client_destroy(&client, CHTTP_H2_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_destroy(&validator), SALTS_OK);
    chttp_jwt_token_destroy(token);
  }

  it("resets a failing response sink without failing an HTTP/2 sibling") {
""",
)
