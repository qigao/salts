from pathlib import Path

path = Path("cnet/tests/cnet_api_test.c")
text = path.read_text()
marker = '''    check_equal(cnet_client_profile_take(&client, &profile), SALTS_EBUSY);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
  }
#endif
'''
insert = '''    check_equal(cnet_client_profile_take(&client, &profile), SALTS_EBUSY);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
  }

  it("direct dispatch preserves TCP callbacks and terminal recycle without dispatcher work") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_listener_probe probe = {0};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket peer = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    cnet_client_poll_profile profile = {0};
    char uri[64];
    uint16_t port = 0u;
    const unsigned char value = 53u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.sent, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.failed, 0);
    probe.received_value = 0u;

    check_equal(cnet_client_init(&client, &config), SALTS_OK);
    check_equal(cnet_client_set_diagnostic_direct_dispatch(&client, true), SALTS_OK);
    check_equal(cnet_client_profile_begin(&client), SALTS_OK);
    check_equal(cnet_api_test_listener(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_listener_state,
                                                  .on_receive = cnet_api_test_listener_receive,
                                                  .on_send = cnet_api_test_listener_send,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), SALTS_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), SALTS_OK);
    peer = accept(listener, NULL, NULL);
    check_true(peer != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_receive(&client, connection, 1u), SALTS_OK);
    check_equal(send(peer, (const char *)&value, (int)sizeof(value), 0), (int)sizeof(value));
    check_equal(cnet_api_test_poll_until(&client, &probe.received, 1), SALTS_OK);
    check_equal(probe.received_value, value);
    check_equal(cnet_close(&client, connection), SALTS_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), SALTS_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_profile_take(&client, &profile), SALTS_OK);
    check_true(profile.owner.event_publish_calls >= (uint64_t)3u);
    check_equal(profile.dispatcher_prepare_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_invoke_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_observer_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_release_calls, (uint64_t)0u);
    check_equal(cnet_client_set_diagnostic_direct_dispatch(&client, false), SALTS_OK);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
    cnet_api_test_close_socket(peer);
    cnet_api_test_close_socket(listener);
  }
#endif
'''
if text.count(marker) != 1:
    raise SystemExit(f"expected one profile-test marker, got {text.count(marker)}")
path.write_text(text.replace(marker, insert, 1))
