from pathlib import Path


def patch(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


path = "chttp/tests/chttp_server_test.c"

patch(
    path,
    """static int chttp_server_test_public_without_jwt(void *user,
                                                const chttp_server_request_view *request,
                                                chttp_server_response *response) {
  size_t *calls = (size_t *)user;
  if (calls == NULL || request == NULL || response == NULL || request->jwt_claims != NULL)
    return SALTS_EPROTO;
  ++*calls;
  return chttp_server_reply(response, 200u, "text/plain", "public", 6u);
}

spec("CHTTP background HTTP/1.1 server") {
""",
    """static int chttp_server_test_public_without_jwt(void *user,
                                                const chttp_server_request_view *request,
                                                chttp_server_response *response) {
  size_t *calls = (size_t *)user;
  if (calls == NULL || request == NULL || response == NULL || request->jwt_claims != NULL)
    return SALTS_EPROTO;
  ++*calls;
  return chttp_server_reply(response, 200u, "text/plain", "public", 6u);
}

static int chttp_server_test_jwt_matrix_exchange(uint16_t port, const char *authorization_headers,
                                                 char *response, size_t response_capacity,
                                                 size_t *out_response_size) {
  char request[2048];
  const char *headers = authorization_headers == NULL ? "" : authorization_headers;
  const int written = snprintf(request, sizeof(request),
                               "GET /jwt-matrix HTTP/1.1\\r\\n"
                               "Host: 127.0.0.1\\r\\n%s"
                               "Connection: close\\r\\n\\r\\n",
                               headers);
  if (written < 0 || (size_t)written >= sizeof(request)) return SALTS_ENOBUFS;
  return chttp_server_test_raw_exchange(port, request, response, response_capacity,
                                        out_response_size);
}

spec("CHTTP background HTTP/1.1 server") {
""",
)

marker = """  it("authenticates server-wide JWT before streaming admission and 100 continue") {
"""
insert = r'''  it("uses one uniform JWT rejection boundary and RFC6750 Bearer spacing") {
    static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
    static const unsigned char wrong_key[] = "fedcba9876543210fedcba9876543210";
    static const char hs384_token[] =
        "eyJhbGciOiJIUzM4NCIsInR5cCI6IkpXVCJ9."
        "eyJpc3MiOiJpc3N1ZXIuZXhhbXBsZSIsInN1YiI6ImFsaWNlIiwiYXVkIjoiYXBpLmV4YW1wbGUiLCJpYXQiOjE3MDAwMDAwMDAsIm5iZiI6MTcwMDAwMDAwMCwiZXhwIjozMDAwMDAwMDAwfQ."
        "8J-7L1cH7qM0uSf_ZX6TTWfhaC8DVxAnWrPQRUN8_h_0Wy6HyW_R42W1kPfZ_5hO";
    const chttp_jwt_claims valid_claims = {.issuer = "issuer.example",
                                           .subject = "alice",
                                           .audience = "api.example",
                                           .issued_at = INT64_C(1700000000),
                                           .not_before = INT64_C(1700000000),
                                           .expires_at = INT64_C(3000000000)};
    const chttp_jwt_bearer_validator_options validator_options = {
        .size = sizeof(validator_options),
        .key = key,
        .key_size = sizeof(key) - 1u,
        .expected_issuer = "issuer.example",
        .expected_audience = "api.example"};
    chttp_jwt_claims claims;
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_jwt_probe probe = {0};
    chttp_jwt_bearer_validator validator = {0};
    const chttp_server_route_options route = {.method = CHTTP_METHOD_GET,
                                              .path = "/jwt-matrix",
                                              .handler = chttp_server_test_jwt_handler,
                                              .user = &probe};
    char *valid_token = NULL;
    char *wrong_key_token = NULL;
    char *wrong_issuer_token = NULL;
    char *wrong_audience_token = NULL;
    char *expired_token = NULL;
    char *future_nbf_token = NULL;
    char tampered_token[1024];
    char lowercase_header[1200];
    char multisp_header[1200];
    char tab_header[1200];
    char malformed_header[256];
    char wrong_key_header[1200];
    char tampered_header[1200];
    char wrong_issuer_header[1200];
    char wrong_audience_header[1200];
    char duplicate_header[2400];
    char hs384_header[1600];
    char expired_header[1200];
    char future_nbf_header[1200];
    const char *invalid_headers[10];
    char response[CHTTP_SERVER_TEST_RAW_BYTES];
    size_t response_size = 0u;
    size_t invalid_count = 0u;
    size_t index;
    size_t token_size;
    uint16_t port = 0u;

    check_equal(chttp_jwt_hs256_token_create(&valid_claims, key, sizeof(key) - 1u, &valid_token),
                SALTS_OK);
    check_equal(chttp_jwt_hs256_token_create(&valid_claims, wrong_key, sizeof(wrong_key) - 1u,
                                             &wrong_key_token),
                SALTS_OK);

    claims = valid_claims;
    claims.issuer = "wrong-issuer.example";
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &wrong_issuer_token),
                SALTS_OK);
    claims = valid_claims;
    claims.audience = "wrong-audience.example";
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &wrong_audience_token),
                SALTS_OK);
    claims = valid_claims;
    claims.issued_at = 0;
    claims.not_before = 0;
    claims.expires_at = INT64_C(1);
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &expired_token),
                SALTS_OK);
    claims = valid_claims;
    claims.issued_at = 0;
    claims.not_before = INT64_C(3000000000);
    claims.expires_at = INT64_C(3000001000);
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &future_nbf_token),
                SALTS_OK);

    token_size = strlen(valid_token);
    check(token_size + 1u <= sizeof(tampered_token));
    memcpy(tampered_token, valid_token, token_size + 1u);
    tampered_token[token_size - 1u] = tampered_token[token_size - 1u] == 'A' ? 'B' : 'A';

    check_true(snprintf(lowercase_header, sizeof(lowercase_header),
                        "Authorization: bearer %s\\r\\n", valid_token) > 0);
    check_true(snprintf(multisp_header, sizeof(multisp_header),
                        "Authorization: BEARER   %s\\r\\n", valid_token) > 0);
    check_true(snprintf(tab_header, sizeof(tab_header),
                        "Authorization: Bearer\\t%s\\r\\n", valid_token) > 0);
    check_true(snprintf(malformed_header, sizeof(malformed_header),
                        "Authorization: Bearer abc.def\\r\\n") > 0);
    check_true(snprintf(wrong_key_header, sizeof(wrong_key_header),
                        "Authorization: Bearer %s\\r\\n", wrong_key_token) > 0);
    check_true(snprintf(tampered_header, sizeof(tampered_header),
                        "Authorization: Bearer %s\\r\\n", tampered_token) > 0);
    check_true(snprintf(wrong_issuer_header, sizeof(wrong_issuer_header),
                        "Authorization: Bearer %s\\r\\n", wrong_issuer_token) > 0);
    check_true(snprintf(wrong_audience_header, sizeof(wrong_audience_header),
                        "Authorization: Bearer %s\\r\\n", wrong_audience_token) > 0);
    check_true(snprintf(duplicate_header, sizeof(duplicate_header),
                        "Authorization: Bearer %s\\r\\nAuthorization: Bearer %s\\r\\n",
                        valid_token, valid_token) > 0);
    check_true(snprintf(hs384_header, sizeof(hs384_header),
                        "Authorization: Bearer %s\\r\\n", hs384_token) > 0);
    check_true(snprintf(expired_header, sizeof(expired_header),
                        "Authorization: Bearer %s\\r\\n", expired_token) > 0);
    check_true(snprintf(future_nbf_header, sizeof(future_nbf_header),
                        "Authorization: Bearer %s\\r\\n", future_nbf_token) > 0);

    invalid_headers[invalid_count++] = NULL;
    invalid_headers[invalid_count++] = malformed_header;
    invalid_headers[invalid_count++] = wrong_key_header;
    invalid_headers[invalid_count++] = tampered_header;
    invalid_headers[invalid_count++] = wrong_issuer_header;
    invalid_headers[invalid_count++] = wrong_audience_header;
    invalid_headers[invalid_count++] = duplicate_header;
    invalid_headers[invalid_count++] = hs384_header;
    invalid_headers[invalid_count++] = expired_header;
    invalid_headers[invalid_count++] = future_nbf_header;

    check_equal(chttp_jwt_bearer_validator_init(&validator, &validator_options), SALTS_OK);
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_route_with_jwt_bearer(&server, &route, &validator), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);

    for (index = 0u; index < invalid_count; ++index) {
      memset(response, 0, sizeof(response));
      response_size = 0u;
      check_equal(chttp_server_test_jwt_matrix_exchange(port, invalid_headers[index], response,
                                                        sizeof(response), &response_size),
                  SALTS_OK);
      check_not_null(strstr(response, "HTTP/1.1 401 Unauthorized"));
      check_not_null(strstr(response, "WWW-Authenticate: Bearer"));
      check_not_null(strstr(response, "Unauthorized"));
    }
    check_equal(probe.calls, (size_t)0u);

    memset(response, 0, sizeof(response));
    response_size = 0u;
    check_equal(chttp_server_test_jwt_matrix_exchange(port, lowercase_header, response,
                                                      sizeof(response), &response_size),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_equal(probe.calls, (size_t)1u);

    memset(response, 0, sizeof(response));
    response_size = 0u;
    check_equal(chttp_server_test_jwt_matrix_exchange(port, multisp_header, response,
                                                      sizeof(response), &response_size),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_equal(probe.calls, (size_t)2u);

    memset(response, 0, sizeof(response));
    response_size = 0u;
    check_equal(chttp_server_test_jwt_matrix_exchange(port, tab_header, response,
                                                      sizeof(response), &response_size),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 401 Unauthorized"));
    check_not_null(strstr(response, "WWW-Authenticate: Bearer"));
    check_equal(probe.calls, (size_t)2u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_destroy(&validator), SALTS_OK);
    chttp_jwt_token_destroy(valid_token);
    chttp_jwt_token_destroy(wrong_key_token);
    chttp_jwt_token_destroy(wrong_issuer_token);
    chttp_jwt_token_destroy(wrong_audience_token);
    chttp_jwt_token_destroy(expired_token);
    chttp_jwt_token_destroy(future_nbf_token);
  }

'''
patch(path, marker, insert + marker)
