from pathlib import Path

p = Path("chttp/tests/chttp_websocket_test.c")
text = p.read_text()

probe_marker = '''typedef struct chttp_websocket_test_server_session_probe {
  chttp_server_websocket_session session;
  atomic_int captured;
  atomic_int peer_present;
} chttp_websocket_test_server_session_probe;
'''
probe_insert = probe_marker + '''
typedef struct chttp_websocket_test_jwt_probe {
  atomic_int opened;
  char subject[32];
} chttp_websocket_test_jwt_probe;
'''
if text.count(probe_marker) != 1:
    raise SystemExit("Task 4 probe marker mismatch")
text = text.replace(probe_marker, probe_insert, 1)

callback_marker = '''static int chttp_websocket_test_capture_open(void *user, chttp_websocket *websocket,
                                              const chttp_server_request_view *request,
                                              chttp_server_response *response) {
'''
callback = '''static int chttp_websocket_test_jwt_open(void *user, chttp_websocket *websocket,
                                          const chttp_server_request_view *request,
                                          chttp_server_response *response) {
  chttp_websocket_test_jwt_probe *probe = (chttp_websocket_test_jwt_probe *)user;
  size_t subject_size;
  (void)websocket;
  (void)response;
  if (probe == NULL || request == NULL || request->jwt_claims == NULL ||
      request->jwt_claims->subject == NULL)
    return SALTS_EPERM;
  subject_size = strlen(request->jwt_claims->subject);
  if (subject_size >= sizeof(probe->subject)) return SALTS_ENOBUFS;
  memcpy(probe->subject, request->jwt_claims->subject, subject_size + 1u);
  atomic_fetch_add_explicit(&probe->opened, 1, memory_order_relaxed);
  return SALTS_OK;
}

'''
if text.count(callback_marker) != 1:
    raise SystemExit("Task 4 callback marker mismatch")
text = text.replace(callback_marker, callback + callback_marker, 1)

spec_marker = 'spec("CHTTP WebSocket client/server") {\n'
test = r'''  it("authenticates a protected HTTP1 WebSocket before the opening callback") {
    static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
    const chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
    const chttp_jwt_bearer_validator_options validator_options = {
        .size = sizeof(validator_options), .key = key, .key_size = sizeof(key) - 1u};
    chttp_websocket_test_jwt_probe probe;
    chttp_server server = {0};
    chttp_server_config server_config = chttp_websocket_test_server_config();
    chttp_server_websocket_options route = {.size = sizeof(route),
                                            .path = "/jwt",
                                            .on_open = chttp_websocket_test_jwt_open,
                                            .on_event = chttp_websocket_test_capture_event,
                                            .user = &probe};
    chttp_jwt_bearer_validator validator = {0};
    chttp_websocket_client_config client_config = chttp_websocket_test_client_config();
    chttp_websocket_client client = {0};
    chttp_websocket_connect_options connect_options = {.size = sizeof(connect_options),
                                                       .timeout_ms =
                                                           CHTTP_WEBSOCKET_TEST_TIMEOUT_MS};
    char *token = NULL;
    char authorization[512];
    chttp_header header = {0};
    unsigned int http_status = 0u;
    uint16_t port = 0u;
    char uri[128];

    memset(&probe, 0, sizeof(probe));
    atomic_init(&probe.opened, 0);
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &token), SALTS_OK);
    check_equal(chttp_jwt_bearer_header(token, authorization, sizeof(authorization), &header),
                SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_init(&validator, &validator_options), SALTS_OK);
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_websocket_with_jwt_bearer(&server, &route, &validator), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_true(snprintf(uri, sizeof(uri), "ws://127.0.0.1:%u/jwt", (unsigned int)port) > 0);

    connect_options.uri = uri;
    connect_options.headers = &header;
    connect_options.header_count = 1u;
    check_equal(chttp_websocket_client_init(&client, &client_config), SALTS_OK);
    check_equal(chttp_websocket_client_connect(&client, &connect_options, &http_status), SALTS_OK);
    check_equal(http_status, 101u);
    check_equal(atomic_load_explicit(&probe.opened, memory_order_relaxed), 1);
    check_equal(probe.subject, "alice");
    check_equal(chttp_websocket_client_destroy(&client, CHTTP_WEBSOCKET_TEST_TIMEOUT_MS), SALTS_OK);

    client = (chttp_websocket_client){0};
    connect_options.headers = NULL;
    connect_options.header_count = 0u;
    http_status = 0u;
    check_equal(chttp_websocket_client_init(&client, &client_config), SALTS_OK);
    check_equal(chttp_websocket_client_connect(&client, &connect_options, &http_status), SALTS_EPROTO);
    check_equal(http_status, 401u);
    check_equal(atomic_load_explicit(&probe.opened, memory_order_relaxed), 1);
    check_equal(chttp_websocket_client_destroy(&client, CHTTP_WEBSOCKET_TEST_TIMEOUT_MS), SALTS_OK);

    check_equal(chttp_server_stop(&server, CHTTP_WEBSOCKET_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_destroy(&validator), SALTS_OK);
    chttp_jwt_token_destroy(token);
  }

'''
if text.count(spec_marker) != 1:
    raise SystemExit("Task 4 spec marker mismatch")
text = text.replace(spec_marker, spec_marker + test, 1)
p.write_text(text)
