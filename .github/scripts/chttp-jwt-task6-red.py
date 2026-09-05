from pathlib import Path


def patch(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:140]!r}")
    p.write_text(text.replace(old, new, 1))


path = "chttp/tests/chttp_h2_server_test.c"

patch(
    path,
    """static void chttp_h2_websocket_event(void *user, chttp_websocket *websocket,
                                     const chttp_websocket_event *event) {
""",
    """static int chttp_h2_websocket_jwt_open(void *user, chttp_websocket *websocket,
                                       const chttp_server_request_view *request,
                                       chttp_server_response *response) {
  if (request == NULL || request->jwt_claims == NULL || request->jwt_claims->subject == NULL ||
      strcmp(request->jwt_claims->subject, "alice") != 0)
    return SALTS_EPROTO;
  return chttp_h2_websocket_open(user, websocket, request, response);
}

static void chttp_h2_websocket_event(void *user, chttp_websocket *websocket,
                                     const chttp_websocket_event *event) {
""",
)

patch(
    path,
    """  it("resets a malformed RFC 8441 stream without failing an HTTP sibling") {
""",
    """  it("authenticates RFC 8441 websocket before opening") {
    static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
    static const chttp_h2_hpack_header base_headers[] = {
        {":method", sizeof(":method") - 1u, "CONNECT", sizeof("CONNECT") - 1u},
        {":protocol", sizeof(":protocol") - 1u, "websocket", sizeof("websocket") - 1u},
        {":scheme", sizeof(":scheme") - 1u, "http", sizeof("http") - 1u},
        {":path", sizeof(":path") - 1u, "/jwt-ws/42", sizeof("/jwt-ws/42") - 1u},
        {":authority", sizeof(":authority") - 1u, "localhost", sizeof("localhost") - 1u},
        {"sec-websocket-version", sizeof("sec-websocket-version") - 1u, "13", 2u}};
    static const uint8_t masked_close[] = {0x88u, 0x82u, 0x01u, 0x02u, 0x03u, 0x04u, 0x02u, 0xeau};
    const chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
    const chttp_jwt_bearer_validator_options validator_options = {
        .size = sizeof(validator_options), .key = key, .key_size = sizeof(key) - 1u};
    chttp_h2_hpack_header authorized[sizeof(base_headers) / sizeof(base_headers[0]) + 1u];
    chttp_h2_websocket_probe probe;
    chttp_jwt_bearer_validator validator = {0};
    chttp_server server = {0};
    chttp_server_config config = chttp_h2_server_test_config();
    chttp_server_websocket_options websocket_options = {
        .size = sizeof(websocket_options),
        .path = "/jwt-ws/:id",
        .on_open = chttp_h2_websocket_jwt_open,
        .on_event = chttp_h2_websocket_event,
        .user = &probe};
    chttp_h2_server_test_peer peer = {0};
    chttp_h2_server_test_socket socket_value = CHTTP_H2_SERVER_TEST_INVALID_SOCKET;
    chttp_header authorization = {0};
    char authorization_storage[512];
    char *token = NULL;
    int32_t anonymous_stream = 0;
    int32_t authorized_stream = 0;
    uint16_t port = 0u;

    atomic_init(&probe.opens, 0);
    atomic_init(&probe.messages, 0);
    check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &token), SALTS_OK);
    check_equal(chttp_jwt_bearer_header(token, authorization_storage, sizeof(authorization_storage),
                                        &authorization),
                SALTS_OK);
    memcpy(authorized, base_headers, sizeof(base_headers));
    authorized[sizeof(base_headers) / sizeof(base_headers[0])] =
        (chttp_h2_hpack_header){"authorization", sizeof("authorization") - 1u,
                                authorization.value, strlen(authorization.value)};

    check_equal(chttp_jwt_bearer_validator_init(&validator, &validator_options), SALTS_OK);
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_websocket_with_jwt_bearer(&server, &websocket_options, &validator),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_server_test_socket_connect(port, &socket_value), SALTS_OK);
    check_equal(chttp_h2_server_test_peer_init(&peer), SALTS_OK);
    check_equal(chttp_h2_server_test_peer_send_once(&peer, socket_value), SALTS_OK);
    check_equal(chttp_h2_server_test_peer_receive_once(&peer, socket_value), SALTS_OK);
    check(chttp_h2_proto_peer_settings_received(peer.protocol));
    check_equal(chttp_h2_proto_peer_enable_connect_protocol(peer.protocol), 1u);

    check_equal(chttp_h2_server_test_peer_submit_tunnel(
                    &peer, base_headers, sizeof(base_headers) / sizeof(base_headers[0]),
                    &anonymous_stream),
                SALTS_OK);
    check_equal(chttp_h2_server_test_peer_send(&peer, socket_value), SALTS_OK);
    check_equal(chttp_h2_server_test_peer_receive(&peer, socket_value, 1u), SALTS_OK);
    check_equal(peer.results[0].stream_id, anonymous_stream);
    check_equal(peer.results[0].status, 401u);
    check_equal(atomic_load_explicit(&probe.opens, memory_order_acquire), 0);

    check_equal(chttp_h2_server_test_peer_submit_tunnel(
                    &peer, authorized, sizeof(authorized) / sizeof(authorized[0]),
                    &authorized_stream),
                SALTS_OK);
    check_equal(chttp_h2_server_test_peer_send(&peer, socket_value), SALTS_OK);
    check_equal(chttp_h2_server_test_peer_receive(&peer, socket_value, 2u), SALTS_OK);
    check_equal(peer.results[1].stream_id, authorized_stream);
    check_equal(peer.results[1].status, 200u);
    check(!peer.results[1].closed);
    check_equal(atomic_load_explicit(&probe.opens, memory_order_acquire), 1);

    check_equal(chttp_h2_proto_submit_data(peer.protocol, authorized_stream, masked_close,
                                           sizeof(masked_close), 1),
                0);
    check_equal(chttp_h2_server_test_peer_send(&peer, socket_value), SALTS_OK);
    check_equal(chttp_h2_server_test_peer_pump(&peer, socket_value, 2u), SALTS_OK);

    chttp_h2_server_test_peer_destroy(&peer);
    chttp_h2_server_test_socket_close(socket_value);
    check_equal(chttp_server_stop(&server, CHTTP_H2_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(chttp_jwt_bearer_validator_destroy(&validator), SALTS_OK);
    chttp_jwt_token_destroy(token);
  }

  it("resets a malformed RFC 8441 stream without failing an HTTP sibling") {
""",
)
