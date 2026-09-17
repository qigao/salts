from pathlib import Path

owner_path = Path('cnet/tests/cnet_owner_test.c')
owner = owner_path.read_text()
old = '#include <salts/clock.h>\n#include <salts/thread.h>\n\n#include <limits.h>\n#include <stdint.h>\n#include <string.h>\n'
new = '#include <salts/clock.h>\n#include <salts/thread.h>\n#include <salts_buffer.h>\n\n#include <limits.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n'
if owner.count(old) != 1:
    raise SystemExit(f'owner include block count={owner.count(old)}')
owner = owner.replace(old, new, 1)

marker = '\nspec("CNet owner shard") {'
if owner.count(marker) != 1:
    raise SystemExit(f'owner spec marker count={owner.count(marker)}')
insert = r'''

typedef struct cnet_owner_test_free_probe {
  int freed;
} cnet_owner_test_free_probe;

static void cnet_owner_test_external_free(void *data, void *user_data) {
  cnet_owner_test_free_probe *probe = (cnet_owner_test_free_probe *)user_data;
  free(data);
  ++probe->freed;
}

static mem_buffer_t *cnet_owner_test_retained_backing(const unsigned char payload[4],
                                                      cnet_owner_test_free_probe *probe) {
  enum { PREFIX = 8, PAYLOAD = 4, SUFFIX = 8, TOTAL = PREFIX + PAYLOAD + SUFFIX };
  unsigned char *storage = (unsigned char *)malloc(TOTAL);
  mem_buffer_t *buffer;
  if (storage == NULL) return NULL;
  memset(storage, 0xa5, PREFIX);
  memcpy(storage + PREFIX, payload, PAYLOAD);
  memset(storage + PREFIX + PAYLOAD, 0x5a, SUFFIX);
  buffer = mem_wrap_external(storage, TOTAL, cnet_owner_test_external_free, probe);
  if (buffer == NULL) free(storage);
  return buffer;
}

#if defined(CNET_INTERNAL_PROFILING) && defined(CNET_INTERNAL_TESTING)
static void cnet_owner_test_retained_slice_partial(native_io_backend_kind backend_kind) {
  static const unsigned char payload[] = {1u, 3u, 5u, 7u};
  cnet_session_table sessions = {0};
  cnet_command_queue commands = {0};
  cnet_event_queue events = {0};
  cnet_owner owner = {0};
  const cnet_command_queue_config command_config = {8u, sizeof(cnet_owner_connect_payload)};
  const cnet_event_queue_config event_config = {8u, 2u, 64u};
  const cnet_owner_config owner_config = {.backend_kind = backend_kind,
                                          .connection_capacity = 1u,
                                          .request_capacity = 4u,
                                          .completion_batch_capacity = 4u,
                                          .receive_buffer_bytes = 64u,
                                          .receive_buffer_count = 1u,
                                          .sessions = &sessions,
                                          .commands = &commands,
                                          .events = &events};
  cnet_owner_test_socket listener = CNET_OWNER_TEST_INVALID_SOCKET;
  cnet_owner_test_socket accepted = CNET_OWNER_TEST_INVALID_SOCKET;
  struct sockaddr_in address;
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};
  cnet_owner_profile profile = {0};
  cnet_owner_test_free_probe free_probe = {0};
  mem_buffer_t *buffer;
  const unsigned char *view_data;
  unsigned char received[sizeof(payload)] = {0};

  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_equal(cnet_owner_test_listener(&listener, &address), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);
  connect_payload.scheme = CNET_URI_TCP;
  connect_payload.address_length = sizeof(address);
  memcpy(connect_payload.address, &address, sizeof(address));
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_OPEN),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_CONNECTED);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  accepted = accept(listener, NULL, NULL);
  check_true(accepted != CNET_OWNER_TEST_INVALID_SOCKET);

  buffer = cnet_owner_test_retained_backing(payload, &free_probe);
  check_true(buffer != NULL);
  view_data = (const unsigned char *)mem_buffer_const_data(buffer) + 8u;
  command = (cnet_command){.kind = CNET_COMMAND_SEND,
                           .connection = session,
                           .size = sizeof(payload),
                           .retained_buffer = buffer,
                           .retained_data = view_data};
  check_equal(cnet_owner_test_set_send_chunk_bytes(&owner, 1u), SALTS_OK);
  check_equal(cnet_owner_profile_begin(&owner), SALTS_OK);
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
  mem_buffer_release(buffer);
  check_equal(free_probe.freed, 0);

  check_equal(cnet_owner_drive(&owner, CNET_OWNER_TEST_TIMEOUT_MS), SALTS_OK);
  check_equal(cnet_owner_test_receive_all(accepted, received, sizeof(received)), SALTS_OK);
  check_equal(received, payload, sizeof(payload));
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.kind, CNET_EVENT_SEND);
  check_equal(event.argument, sizeof(payload));
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_owner_profile_take(&owner, &profile), SALTS_OK);
  check_equal(profile.request_start_calls, UINT64_C(1));
  check_equal(profile.request_resubmit_calls, UINT64_C(3));
  check_equal(profile.request_completion_calls, UINT64_C(1));
  check_equal(free_probe.freed, 1);
  check_equal(cnet_owner_test_set_send_chunk_bytes(&owner, 0u), SALTS_OK);

  command = (cnet_command){CNET_COMMAND_CLOSE, session, NULL, 0u, 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  while (cnet_event_queue_take(&events, &event) == SALTS_OK)
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, CNET_SESSION_TERMINAL_CLOSED);
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

  cnet_owner_test_close_socket(accepted);
  cnet_owner_test_close_socket(listener);
  check_equal(cnet_command_queue_close(&commands), SALTS_OK);
  check_equal(cnet_owner_close(&owner), SALTS_OK);
  check_equal(cnet_owner_destroy(&owner), SALTS_OK);
  check_equal(cnet_event_queue_close(&events), SALTS_OK);
  check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&commands), SALTS_OK);
  check_equal(cnet_session_table_destroy(&sessions), SALTS_OK);
}
#endif

static void cnet_owner_test_retained_slice_write_timeout(native_io_backend_kind backend_kind) {
  static const unsigned char payload[] = {1u, 3u, 5u, 7u};
  cnet_session_table sessions = {0};
  cnet_command_queue commands = {0};
  cnet_event_queue events = {0};
  cnet_owner owner = {0};
  const cnet_command_queue_config command_config = {8u, sizeof(cnet_owner_connect_payload)};
  const cnet_event_queue_config event_config = {8u, 2u, 64u};
  cnet_owner_test_clock clock = {.now_ms = 100u, .next_ms = 100u, .calls = 0u};
  const cnet_owner_config owner_config = {.backend_kind = backend_kind,
                                          .connection_capacity = 1u,
                                          .request_capacity = 4u,
                                          .completion_batch_capacity = 4u,
                                          .receive_buffer_bytes = 64u,
                                          .receive_buffer_count = 1u,
                                          .sessions = &sessions,
                                          .commands = &commands,
                                          .events = &events,
                                          .now_ms = cnet_owner_test_now,
                                          .clock_context = &clock};
  cnet_owner_test_socket listener = CNET_OWNER_TEST_INVALID_SOCKET;
  cnet_owner_test_socket accepted = CNET_OWNER_TEST_INVALID_SOCKET;
  struct sockaddr_in address;
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};
  cnet_owner_test_free_probe free_probe = {0};
  mem_buffer_t *buffer;
  const unsigned char *view_data;
  bool saw_failed = false;

  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_equal(cnet_owner_test_listener(&listener, &address), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);
  connect_payload.scheme = CNET_URI_TCP;
  connect_payload.address_length = sizeof(address);
  memcpy(connect_payload.address, &address, sizeof(address));
  connect_payload.write_timeout_ms = 10u;
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_OPEN),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_CONNECTED);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  accepted = accept(listener, NULL, NULL);
  check_true(accepted != CNET_OWNER_TEST_INVALID_SOCKET);

  buffer = cnet_owner_test_retained_backing(payload, &free_probe);
  check_true(buffer != NULL);
  view_data = (const unsigned char *)mem_buffer_const_data(buffer) + 8u;
  command = (cnet_command){.kind = CNET_COMMAND_SEND,
                           .connection = session,
                           .size = sizeof(payload),
                           .retained_buffer = buffer,
                           .retained_data = view_data};
  clock.now_ms = 100u;
  clock.next_ms = 111u;
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
  mem_buffer_release(buffer);
  check_equal(free_probe.freed, 0);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  check_equal(free_probe.freed, 1);

  while (cnet_event_queue_take(&events, &event) == SALTS_OK) {
    if (event.kind == CNET_EVENT_STATE && event.state == CNET_EVENT_STATE_FAILED) {
      check_equal(event.status, SALTS_ETIMEDOUT);
      check_equal(event.stage, CNET_SESSION_STAGE_WRITE);
      saw_failed = true;
    }
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  }
  check_true(saw_failed);
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, CNET_SESSION_TERMINAL_FAILED);
  check_equal(terminal.status, SALTS_ETIMEDOUT);
  check_equal(terminal.stage, CNET_SESSION_STAGE_WRITE);
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

  cnet_owner_test_close_socket(accepted);
  cnet_owner_test_close_socket(listener);
  check_equal(cnet_command_queue_close(&commands), SALTS_OK);
  check_equal(cnet_owner_close(&owner), SALTS_OK);
  check_equal(cnet_owner_destroy(&owner), SALTS_OK);
  check_equal(cnet_event_queue_close(&events), SALTS_OK);
  check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&commands), SALTS_OK);
  check_equal(cnet_session_table_destroy(&sessions), SALTS_OK);
}
'''
owner = owner.replace(marker, insert + marker, 1)

spec_marker = '  it("owns the resolve to TCP connect state transition") {'
if owner.count(spec_marker) != 1:
    raise SystemExit(f'owner spec insertion count={owner.count(spec_marker)}')
spec_tests = r'''#if defined(CNET_INTERNAL_PROFILING) && defined(CNET_INTERNAL_TESTING)
  it("keeps one retained interior owner across deterministic partial resubmits") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_retained_slice_partial(backends[index]);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }
#endif

  it("releases one retained interior owner after an authoritative write timeout") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_retained_slice_write_timeout(backends[index]);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

'''
owner = owner.replace(spec_marker, spec_tests + spec_marker, 1)
owner_path.write_text(owner)

tls_path = Path('cnet/tests/cnet_tls_test.c')
tls = tls_path.read_text()
old_vars = '''    uint64_t deadline;
    bool accepted = false;

    check_not_null(cert_path);'''
new_vars = '''    uint64_t deadline;
    bool accepted = false;
    mem_pool_t secure_pool = {0};
    mem_buffer_t *secure_buffer = NULL;
    mem_slice_t secure_slice = {0};

    check_not_null(cert_path);'''
if tls.count(old_vars) < 1:
    raise SystemExit('TLS vars marker absent')
# The STARTTLS fixture is the first exact occurrence after its own declaration block? There are
# multiple fixtures with these tail variables, so replace the last occurrence before the STARTTLS
# secure-send block by locating from the test title.
title = '  it("upgrades one negotiated plaintext connection in place and carries TLS bytes") {'
pos = tls.find(title)
if pos < 0:
    raise SystemExit('STARTTLS test title absent')
head, tail = tls[:pos], tls[pos:]
if tail.count(old_vars) < 1:
    raise SystemExit('STARTTLS vars marker absent in tail')
tail = tail.replace(old_vars, new_vars, 1)
old_send = '''    check_equal(cnet_receive(&server, server_connection, 1u), SALTS_OK);
    check_equal(cnet_send(&client, client_connection, secure_request, sizeof(secure_request) - 1u),
                SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while (server_probe.received_size == 0u && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_equal(server_probe.received_size, sizeof(secure_request) - 1u);
    check_equal(memcmp(server_probe.received, secure_request, sizeof(secure_request) - 1u), 0);
'''
new_send = '''    check_equal(mem_init(&secure_pool, 0u), 0);
    secure_buffer = mem_get_buffer(&secure_pool, sizeof(secure_request) - 1u + 16u);
    check_not_null(secure_buffer);
    memset(mem_buffer_data(secure_buffer), 0xa5, 8u);
    memcpy(mem_buffer_data(secure_buffer) + 8u, secure_request, sizeof(secure_request) - 1u);
    memset(mem_buffer_data(secure_buffer) + 8u + sizeof(secure_request) - 1u, 0x5a, 8u);
    mem_set_used(secure_buffer, sizeof(secure_request) - 1u + 16u);
    secure_slice = mem_slice(secure_buffer, 8u, sizeof(secure_request) - 1u);
    check_true(secure_slice.buffer == secure_buffer);
    check_equal(mem_buffer_ref_count(secure_buffer), UINT32_C(2));

    check_equal(cnet_receive(&server, server_connection, 1u), SALTS_OK);
    check_equal(cnet_send_slice(&client, client_connection, &secure_slice), SALTS_OK);
    check_equal(mem_buffer_ref_count(secure_buffer), UINT32_C(3));
    mem_slice_release(&secure_slice);
    check_equal(mem_buffer_ref_count(secure_buffer), UINT32_C(2));
    mem_buffer_release(secure_buffer);
    check_equal(mem_buffer_ref_count(secure_buffer), UINT32_C(1));
    deadline = salts_monotonic_ms() + 5000u;
    while ((server_probe.received_size == 0u || client_probe.sent < 2) &&
           salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_equal(server_probe.received_size, sizeof(secure_request) - 1u);
    check_equal(memcmp(server_probe.received, secure_request, sizeof(secure_request) - 1u), 0);
    check_equal(client_probe.sent, 2);
    mem_destroy(&secure_pool);
'''
if tail.count(old_send) != 1:
    raise SystemExit(f'STARTTLS secure send block count={tail.count(old_send)}')
tail = tail.replace(old_send, new_send, 1)
tls_path.write_text(head + tail)
