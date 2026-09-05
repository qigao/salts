from pathlib import Path

p = Path("chttp/tests/chttp_server_test.c")
text = p.read_text()

helper_marker = '''static int chttp_server_test_jwt_observer(void *user, const chttp_server_request_view *request,
                                          chttp_server_response *response, chttp_server_next *next) {
  chttp_server_test_jwt_probe *probe = (chttp_server_test_jwt_probe *)user;
  (void)response;
  if (probe == NULL || request == NULL || request->jwt_claims == NULL ||
      request->jwt_claims->subject == NULL || strcmp(request->jwt_claims->subject, "alice") != 0)
    return SALTS_EPERM;
  ++probe->middleware_calls;
  return chttp_server_next_call(next);
}
'''
helper = helper_marker + '''
static int chttp_server_test_public_without_jwt(void *user,
                                                const chttp_server_request_view *request,
                                                chttp_server_response *response) {
  size_t *calls = (size_t *)user;
  if (calls == NULL || request == NULL || response == NULL || request->jwt_claims != NULL)
    return SALTS_EPROTO;
  ++*calls;
  return chttp_server_reply(response, 200u, "text/plain", "public", 6u);
}
'''
if text.count(helper_marker) != 1:
    raise SystemExit("JWT observer insertion marker mismatch")
text = text.replace(helper_marker, helper, 1)

insert_marker = '  it("does not reserve configured payload maxima during initialization") {'
if text.count(insert_marker) != 1:
    raise SystemExit("Task 3 checkpoint insertion marker mismatch")

checkpoint_tests = r'''  it("authenticates server-wide JWT before global middleware wraps built-in 404") {
    static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
    static const char anonymous_unknown[] = "GET /missing HTTP/1.1\r\n"
                                            "Host: 127.0.0.1\r\n"
                                            "Connection: close\r\n\r\n";
    const chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
    const chttp_jwt_bearer_validator_options validator_options = {
        .size = sizeof(validator_options), .key = key, .key_size = sizeof(key) - 1u};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_probe global_probe = {0};
    chttp_jwt_bearer_validator validator = {0};
    char *token = NULL;
    char authorization[512];
    chttp_header header = {0};
    char authorized_unknown[768];
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &token), SALTS_OK);
    check_equal(chttp_jwt_bearer_header(token, authorization, sizeof(authorization), &header),
                SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_init(&validator, &validator_options), SALTS_OK);
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_use(&server, chttp_server_test_global, &global_probe), SALTS_OK);
    check_equal(chttp_server_use_jwt_bearer(&server, &validator), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);

    check_equal(chttp_server_test_raw_exchange(port, anonymous_unknown, response, sizeof(response),
                                                &response_size),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 401 Unauthorized"));
    check_not_null(strstr(response, "WWW-Authenticate: Bearer"));
    check_equal(global_probe.order_size, (size_t)0u);

    check_true(snprintf(authorized_unknown, sizeof(authorized_unknown),
                        "GET /missing HTTP/1.1\r\nHost: 127.0.0.1\r\n%s: %s\r\n"
                        "Connection: close\r\n\r\n",
                        header.name, header.value) > 0);
    response[0] = '\0';
    response_size = 0u;
    check_equal(chttp_server_test_raw_exchange(port, authorized_unknown, response, sizeof(response),
                                                &response_size),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 404 Not Found"));
    check_not_null(strstr(response, "X-Global: yes"));
    check_equal(global_probe.order, "G");

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_destroy(&validator), SALTS_OK);
    chttp_jwt_token_destroy(token);
  }

  it("clears JWT identity before the next pipelined public request") {
    static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
    const chttp_jwt_claims claims = {.issuer = "issuer.example",
                                     .subject = "alice",
                                     .audience = "api.example",
                                     .expires_at = INT64_C(3000000000)};
    const chttp_jwt_bearer_validator_options validator_options = {
        .size = sizeof(validator_options),
        .key = key,
        .key_size = sizeof(key) - 1u,
        .expected_issuer = "issuer.example",
        .expected_audience = "api.example"};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_jwt_probe protected_probe = {0};
    chttp_jwt_bearer_validator validator = {0};
    const chttp_server_route_options protected_route = {
        .method = CHTTP_METHOD_GET,
        .path = "/protected",
        .handler = chttp_server_test_jwt_handler,
        .user = &protected_probe};
    size_t public_calls = 0u;
    char *token = NULL;
    char authorization[512];
    chttp_header header = {0};
    char requests[1024];
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &token), SALTS_OK);
    check_equal(chttp_jwt_bearer_header(token, authorization, sizeof(authorization), &header),
                SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_init(&validator, &validator_options), SALTS_OK);
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_route_with_jwt_bearer(&server, &protected_route, &validator), SALTS_OK);
    check_equal(chttp_server_get(&server, "/public", chttp_server_test_public_without_jwt,
                                 &public_calls),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);

    check_true(snprintf(requests, sizeof(requests),
                        "GET /protected HTTP/1.1\r\nHost: 127.0.0.1\r\n%s: %s\r\n\r\n"
                        "GET /public HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                        "Connection: close\r\n\r\n",
                        header.name, header.value) > 0);
    check_equal(chttp_server_test_raw_exchange(port, requests, response, sizeof(response),
                                                &response_size),
                SALTS_OK);
    check_equal(chttp_server_test_count(response, "HTTP/1.1 200 OK"), (size_t)2u);
    check_equal(protected_probe.calls, (size_t)1u);
    check_equal(protected_probe.subject, "alice");
    check_equal(public_calls, (size_t)1u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_destroy(&validator), SALTS_OK);
    chttp_jwt_token_destroy(token);
  }

'''
text = text.replace(insert_marker, checkpoint_tests + insert_marker, 1)
p.write_text(text)
