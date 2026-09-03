#include "cnet_module.h"
#include "cnet_owner.h"
#include "cnet_test_named_pipe.h"
#include "cnet_test_pipe.h"
#include "tinytest.h"
#include <salts/clock.h>
#include <salts/thread.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_owner_test_socket;
  #define CNET_OWNER_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_owner_test_socket;
  #define CNET_OWNER_TEST_INVALID_SOCKET (-1)
#endif

enum {
  CNET_OWNER_TEST_TIMEOUT_MS = 5000,
  CNET_OWNER_TEST_MAX_BACKENDS = 2,
  CNET_OWNER_TEST_UDP_ECHO_CAPACITY = 64
};

typedef struct cnet_owner_test_clock {
  uint64_t now_ms;
  uint64_t next_ms;
  size_t calls;
} cnet_owner_test_clock;

static uint64_t cnet_owner_test_now(void *context) {
  cnet_owner_test_clock *clock = (cnet_owner_test_clock *)context;
  const uint64_t now_ms = clock->now_ms;
  ++clock->calls;
  clock->now_ms = clock->next_ms;
  return now_ms;
}

typedef enum cnet_owner_test_timeout {
  CNET_OWNER_TEST_NO_TIMEOUT = 0,
  CNET_OWNER_TEST_CONNECT_TIMEOUT,
  CNET_OWNER_TEST_READ_TIMEOUT,
  CNET_OWNER_TEST_WRITE_TIMEOUT
} cnet_owner_test_timeout;

typedef struct cnet_owner_test_udp_echo {
  cnet_owner_test_socket socket_value;
  const unsigned char *expected;
  size_t expected_size;
  const unsigned char *reply;
  size_t reply_size;
  int status;
} cnet_owner_test_udp_echo;

static void cnet_owner_test_udp_echo_entry(void *argument) {
  cnet_owner_test_udp_echo *echo = (cnet_owner_test_udp_echo *)argument;
  struct sockaddr_storage peer;
#if defined(_WIN32)
  int peer_length = (int)sizeof(peer);
#else
  socklen_t peer_length = (socklen_t)sizeof(peer);
#endif
  unsigned char buffer[CNET_OWNER_TEST_UDP_ECHO_CAPACITY];
  int received;
  int sent;

  echo->status = SALTS_EIO;
  if (echo->expected_size > sizeof(buffer) || echo->reply_size > INT_MAX) return;
#if defined(_WIN32)
  received = recvfrom(echo->socket_value, (char *)buffer, (int)sizeof(buffer), 0,
                      (struct sockaddr *)&peer, &peer_length);
  if (received == (int)echo->expected_size &&
      memcmp(buffer, echo->expected, echo->expected_size) == 0)
    sent = sendto(echo->socket_value, (const char *)echo->reply, (int)echo->reply_size, 0,
                  (const struct sockaddr *)&peer, peer_length);
  else return;
#else
  received = (int)recvfrom(echo->socket_value, buffer, sizeof(buffer), 0, (struct sockaddr *)&peer,
                           &peer_length);
  if (received == (int)echo->expected_size &&
      memcmp(buffer, echo->expected, echo->expected_size) == 0)
    sent = (int)sendto(echo->socket_value, echo->reply, echo->reply_size, 0,
                       (const struct sockaddr *)&peer, peer_length);
  else return;
#endif
  if (sent == (int)echo->reply_size) echo->status = SALTS_OK;
}

static size_t
cnet_owner_test_backends(native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS]) {
#if defined(_WIN32)
  backends[0] = NATIVE_IO_BACKEND_IOCP;
  return 1u;
#elif defined(__linux__)
  backends[0] = NATIVE_IO_BACKEND_EPOLL;
  backends[1] = NATIVE_IO_BACKEND_IO_URING;
  return 2u;
#elif defined(__APPLE__) && UINTPTR_MAX > UINT32_MAX
  backends[0] = NATIVE_IO_BACKEND_KQUEUE;
  return 1u;
#else
  (void)backends;
  return 0u;
#endif
}

static void cnet_owner_test_close_socket(cnet_owner_test_socket socket_value) {
  if (socket_value == CNET_OWNER_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_owner_test_listener(cnet_owner_test_socket *out_listener,
                                    struct sockaddr_in *out_address) {
#if defined(_WIN32)
  int length = (int)sizeof(*out_address);
#else
  socklen_t length = (socklen_t)sizeof(*out_address);
#endif
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_OWNER_TEST_INVALID_SOCKET) return SALTS_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)out_address, &length) != 0 ||
      listen(*out_listener, 1) != 0) {
    cnet_owner_test_close_socket(*out_listener);
    *out_listener = CNET_OWNER_TEST_INVALID_SOCKET;
    return SALTS_EIO;
  }
  return SALTS_OK;
}

static int cnet_owner_test_udp_peer(cnet_owner_test_socket *out_socket,
                                    struct sockaddr_in *out_address) {
#if defined(_WIN32)
  int length = (int)sizeof(*out_address);
#else
  socklen_t length = (socklen_t)sizeof(*out_address);
#endif
  *out_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (*out_socket == CNET_OWNER_TEST_INVALID_SOCKET) return SALTS_EIO;
  memset(out_address, 0, sizeof(*out_address));
  out_address->sin_family = AF_INET;
  out_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_socket, (const struct sockaddr *)out_address, (int)sizeof(*out_address)) != 0 ||
      getsockname(*out_socket, (struct sockaddr *)out_address, &length) != 0) {
    cnet_owner_test_close_socket(*out_socket);
    *out_socket = CNET_OWNER_TEST_INVALID_SOCKET;
    return SALTS_EIO;
  }
  return SALTS_OK;
}

static int cnet_owner_test_drive_to_state(cnet_owner *owner, cnet_session_table *sessions,
                                          cnet_session_handle handle, cnet_session_state expected) {
  const uint64_t deadline = salts_monotonic_ms() + CNET_OWNER_TEST_TIMEOUT_MS;
  for (;;) {
    cnet_session_state state = CNET_SESSION_FREE;
    int status = cnet_session_table_state(sessions, handle, &state);
    if (status != SALTS_OK) return status;
    if (state == expected) return SALTS_OK;
    if (state == CNET_SESSION_TERMINAL) return SALTS_EIO;
    status = cnet_owner_drive(owner, 10u);
    if (status != SALTS_OK) return status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
}

static int cnet_owner_test_drive_to_event(cnet_owner *owner, cnet_event_queue *events,
                                          cnet_event_view *out_event) {
  const uint64_t deadline = salts_monotonic_ms() + CNET_OWNER_TEST_TIMEOUT_MS;
  for (;;) {
    int status = cnet_event_queue_take(events, out_event);
    if (status == SALTS_OK) return SALTS_OK;
    if (status != SALTS_ETIMEDOUT) return status;
    status = cnet_owner_drive(owner, 10u);
    if (status != SALTS_OK) return status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
}

static void cnet_owner_test_tcp(native_io_backend_kind backend_kind, bool resolve_host,
                                cnet_owner_test_timeout timeout) {
  static const unsigned char payload[] = {1u, 3u, 5u, 7u};
  cnet_session_table sessions = {0};
  cnet_command_queue commands = {0};
  cnet_event_queue events = {0};
  cnet_owner owner = {0};
  const cnet_command_queue_config command_config = {8u, sizeof(cnet_owner_connect_payload)};
  const cnet_event_queue_config event_config = {8u, 2u, 64u};
  cnet_owner_test_clock clock = {.now_ms = 100u,
                                 .next_ms = timeout == CNET_OWNER_TEST_CONNECT_TIMEOUT ||
                                                    timeout == CNET_OWNER_TEST_WRITE_TIMEOUT
                                                ? 111u
                                                : 100u,
                                 .calls = 0u};
  const cnet_owner_config owner_config = {
      .backend_kind = backend_kind,
      .connection_capacity = 1u,
      .request_capacity = 4u,
      .completion_batch_capacity = 4u,
      .receive_buffer_bytes = 64u,
      .receive_buffer_count = 1u,
      .sessions = &sessions,
      .commands = &commands,
      .events = &events,
      .now_ms = timeout != CNET_OWNER_TEST_NO_TIMEOUT ? cnet_owner_test_now : NULL,
      .clock_context = timeout != CNET_OWNER_TEST_NO_TIMEOUT ? &clock : NULL};
  cnet_owner_test_socket listener = CNET_OWNER_TEST_INVALID_SOCKET;
  cnet_owner_test_socket accepted = CNET_OWNER_TEST_INVALID_SOCKET;
  struct sockaddr_in address;
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event queued_state = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};
  native_io_coroutine_stats coroutine_stats = NATIVE_IO_COROUTINE_STATS_V1_INITIALIZER;
  unsigned char received[sizeof(payload)] = {0};
  size_t event_index;

  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_true(cnet_owner_get_coroutine_stats(&owner, &coroutine_stats));
  check_equal(coroutine_stats.capacity, owner_config.request_capacity);
  check_equal(coroutine_stats.active, 0u);
  check_equal(coroutine_stats.retained_frames, 0u);
  check_equal(cnet_owner_test_listener(&listener, &address), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);
  queued_state = (cnet_event){CNET_EVENT_STATE,
                              {UINT32_MAX, UINT32_MAX},
                              CNET_EVENT_STATE_CLOSING,
                              SALTS_OK,
                              CNET_SESSION_STAGE_NONE,
                              NULL,
                              0u};
  for (event_index = 0u; event_index < event_config.capacity; ++event_index)
    check_equal(cnet_event_queue_publish(&events, &queued_state), SALTS_OK);
  connect_payload.scheme = CNET_URI_TCP;
  connect_payload.connect_timeout_ms = timeout == CNET_OWNER_TEST_CONNECT_TIMEOUT ? 10u : 0u;
  connect_payload.read_timeout_ms = timeout == CNET_OWNER_TEST_READ_TIMEOUT ? 10u : 0u;
  connect_payload.write_timeout_ms = timeout == CNET_OWNER_TEST_WRITE_TIMEOUT ? 10u : 0u;
  if (resolve_host) {
    memcpy(connect_payload.host, "127.0.0.1", sizeof("127.0.0.1"));
    connect_payload.port = ntohs(address.sin_port);
  } else {
    connect_payload.address_length = sizeof(address);
    memcpy(connect_payload.address, &address, sizeof(address));
  }
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  if (timeout == CNET_OWNER_TEST_CONNECT_TIMEOUT) {
    check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
                SALTS_OK);
    for (event_index = 0u; event_index < event_config.capacity; ++event_index) {
      check_equal(cnet_owner_test_drive_to_event(&owner, &events, &event), SALTS_OK);
      check_equal(event.state, CNET_EVENT_STATE_CLOSING);
      check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
    }
    check_equal(cnet_owner_drive(&owner, 0u), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
    check_equal(event.kind, CNET_EVENT_STATE);
    check_equal(event.state, CNET_EVENT_STATE_FAILED);
    check_equal(event.status, SALTS_ETIMEDOUT);
    check_equal(event.stage,
                resolve_host ? CNET_SESSION_STAGE_RESOLVE : CNET_SESSION_STAGE_CONNECT);
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
    check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
    check_equal(terminal.kind, CNET_SESSION_TERMINAL_FAILED);
    check_equal(terminal.status, SALTS_ETIMEDOUT);
    check_equal(terminal.stage,
                resolve_host ? CNET_SESSION_STAGE_RESOLVE : CNET_SESSION_STAGE_CONNECT);
    check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
    check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);
    goto cleanup;
  }
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_OPEN),
              SALTS_OK);
  coroutine_stats = (native_io_coroutine_stats)NATIVE_IO_COROUTINE_STATS_V1_INITIALIZER;
  check_true(cnet_owner_get_coroutine_stats(&owner, &coroutine_stats));
  check_equal(coroutine_stats.active, 0u);
  check_true(coroutine_stats.retained_frames >= 1u);
  for (event_index = 0u; event_index < event_config.capacity; ++event_index) {
    check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
    check_equal(event.state, CNET_EVENT_STATE_CLOSING);
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  }
  check_equal(cnet_owner_drive(&owner, 0u), SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.kind, CNET_EVENT_STATE);
  check_equal(event.state, CNET_EVENT_STATE_CONNECTED);
  check_equal(event.session.slot, session.slot);
  check_equal(event.session.generation, session.generation);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  accepted = accept(listener, NULL, NULL);
  check_true(accepted != CNET_OWNER_TEST_INVALID_SOCKET);

  command = (cnet_command){CNET_COMMAND_RECEIVE, session, NULL, 0u, 1u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_drive(&owner, 0u), SALTS_OK);
  coroutine_stats = (native_io_coroutine_stats)NATIVE_IO_COROUTINE_STATS_V1_INITIALIZER;
  check_true(cnet_owner_get_coroutine_stats(&owner, &coroutine_stats));
  check_equal(coroutine_stats.active, 1u);
  if (timeout == CNET_OWNER_TEST_READ_TIMEOUT) {
    clock.now_ms = 111u;
    clock.next_ms = 111u;
  } else {
    check_equal(send(accepted, (const char *)payload, (int)sizeof(payload), 0),
                (int)sizeof(payload));
    check_equal(cnet_owner_test_drive_to_event(&owner, &events, &event), SALTS_OK);
    check_equal(event.kind, CNET_EVENT_RECEIVE);
    check_equal(event.data, payload, sizeof(payload));
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);

    if (timeout == CNET_OWNER_TEST_WRITE_TIMEOUT) {
      check_equal(clock.calls, 0u);
      clock.now_ms = 100u;
      clock.next_ms = 111u;
    }
    command = (cnet_command){timeout == CNET_OWNER_TEST_WRITE_TIMEOUT ? CNET_COMMAND_SEND_CLOSE
                                                                      : CNET_COMMAND_SEND,
                             session, payload, sizeof(payload), 0u};
    check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
    if (timeout == CNET_OWNER_TEST_WRITE_TIMEOUT) {
      check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
                  SALTS_OK);
    } else {
      check_equal(cnet_owner_drive(&owner, CNET_OWNER_TEST_TIMEOUT_MS), SALTS_OK);
      check_equal(recv(accepted, (char *)received, (int)sizeof(received), 0),
                  (int)sizeof(received));
      check_equal(received, payload, sizeof(payload));
      check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
      check_equal(event.kind, CNET_EVENT_SEND);
      check_equal(event.argument, sizeof(payload));
      check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);

      command = (cnet_command){CNET_COMMAND_CLOSE, session, NULL, 0u, 0u};
      check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
    }
  }
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  if (timeout == CNET_OWNER_TEST_WRITE_TIMEOUT) {
    check_equal(event.state, CNET_EVENT_STATE_CLOSING);
    check_equal(event.status, SALTS_OK);
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  }
  check_equal(event.state, timeout != CNET_OWNER_TEST_NO_TIMEOUT ? CNET_EVENT_STATE_FAILED
                                                                 : CNET_EVENT_STATE_CLOSING);
  if (timeout == CNET_OWNER_TEST_READ_TIMEOUT || timeout == CNET_OWNER_TEST_WRITE_TIMEOUT) {
    check_equal(event.status, SALTS_ETIMEDOUT);
    check_equal(event.stage, timeout == CNET_OWNER_TEST_READ_TIMEOUT ? CNET_SESSION_STAGE_READ
                                                                     : CNET_SESSION_STAGE_WRITE);
  }
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  if (timeout == CNET_OWNER_TEST_NO_TIMEOUT) {
    check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
    check_equal(event.state, CNET_EVENT_STATE_CLOSED);
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  }
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, timeout != CNET_OWNER_TEST_NO_TIMEOUT ? CNET_SESSION_TERMINAL_FAILED
                                                                   : CNET_SESSION_TERMINAL_CLOSED);
  if (timeout == CNET_OWNER_TEST_READ_TIMEOUT || timeout == CNET_OWNER_TEST_WRITE_TIMEOUT) {
    check_equal(terminal.status, SALTS_ETIMEDOUT);
    check_equal(terminal.stage, timeout == CNET_OWNER_TEST_READ_TIMEOUT ? CNET_SESSION_STAGE_READ
                                                                        : CNET_SESSION_STAGE_WRITE);
  }
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

cleanup:
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

static void cnet_owner_test_udp(native_io_backend_kind backend_kind) {
  static const unsigned char outbound[] = {2u, 4u, 6u, 8u};
  static const unsigned char inbound[] = {1u, 9u, 3u, 7u};
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
  cnet_owner_test_socket peer = CNET_OWNER_TEST_INVALID_SOCKET;
  struct sockaddr_in peer_address;
  cnet_owner_test_udp_echo echo = {0};
  salts_thread_t echo_thread = {0};
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};
  int echo_start_status;

  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_equal(cnet_owner_test_udp_peer(&peer, &peer_address), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);

  connect_payload.scheme = CNET_URI_UDP;
  connect_payload.address_length = sizeof(peer_address);
  memcpy(connect_payload.address, &peer_address, sizeof(peer_address));
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_OPEN),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.kind, CNET_EVENT_STATE);
  check_equal(event.state, CNET_EVENT_STATE_CONNECTED);
  check_equal(event.session.slot, session.slot);
  check_equal(event.session.generation, session.generation);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);

  echo = (cnet_owner_test_udp_echo){peer,    outbound,        sizeof(outbound),
                                    inbound, sizeof(inbound), SALTS_EIO};
  echo_start_status = salts_thread_create(&echo_thread, cnet_owner_test_udp_echo_entry, &echo);
  check_equal(echo_start_status, SALTS_OK);
  if (echo_start_status == SALTS_OK) {
    int send_event_seen = 0;
    command = (cnet_command){CNET_COMMAND_RECEIVE, session, NULL, 0u, 1u};
    check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
    command = (cnet_command){CNET_COMMAND_SEND, session, outbound, sizeof(outbound), 0u};
    check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
    check_equal(cnet_owner_drive(&owner, CNET_OWNER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
    if (event.kind == CNET_EVENT_SEND) {
      check_equal(event.argument, sizeof(outbound));
      check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
      send_event_seen = 1;
      check_equal(cnet_owner_test_drive_to_event(&owner, &events, &event), SALTS_OK);
    }
    check_equal(event.kind, CNET_EVENT_RECEIVE);
    check_equal(event.data, inbound, sizeof(inbound));
    check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
    if (!send_event_seen) {
      check_equal(cnet_owner_test_drive_to_event(&owner, &events, &event), SALTS_OK);
      check_equal(event.kind, CNET_EVENT_SEND);
      check_equal(event.argument, sizeof(outbound));
      check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
    }
    check_equal(salts_thread_join(&echo_thread), SALTS_OK);
    salts_thread_destroy(&echo_thread);
    check_equal(echo.status, SALTS_OK);
  }

  command = (cnet_command){CNET_COMMAND_CLOSE, session, NULL, 0u, 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_CLOSING);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_CLOSED);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, CNET_SESSION_TERMINAL_CLOSED);
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

  cnet_owner_test_close_socket(peer);
  check_equal(cnet_command_queue_close(&commands), SALTS_OK);
  check_equal(cnet_owner_close(&owner), SALTS_OK);
  check_equal(cnet_owner_destroy(&owner), SALTS_OK);
  check_equal(cnet_event_queue_close(&events), SALTS_OK);
  check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&commands), SALTS_OK);
  check_equal(cnet_session_table_destroy(&sessions), SALTS_OK);
}

static void cnet_owner_test_resolve_failure(native_io_backend_kind backend_kind) {
  cnet_session_table sessions = {0};
  cnet_command_queue commands = {0};
  cnet_event_queue events = {0};
  cnet_owner owner = {0};
  const cnet_command_queue_config command_config = {4u, sizeof(cnet_owner_connect_payload)};
  const cnet_event_queue_config event_config = {4u, 1u, 64u};
  const cnet_owner_config owner_config = {.backend_kind = backend_kind,
                                          .connection_capacity = 1u,
                                          .request_capacity = 2u,
                                          .completion_batch_capacity = 2u,
                                          .receive_buffer_bytes = 64u,
                                          .receive_buffer_count = 1u,
                                          .sessions = &sessions,
                                          .commands = &commands,
                                          .events = &events};
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};

  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);

  connect_payload.scheme = CNET_URI_TCP;
  memcpy(connect_payload.host, "bad host", sizeof("bad host"));
  connect_payload.port = 443u;
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_FAILED);
  check_equal(event.stage, CNET_SESSION_STAGE_RESOLVE);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, CNET_SESSION_TERMINAL_FAILED);
  check_equal(terminal.stage, CNET_SESSION_STAGE_RESOLVE);
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

  check_equal(cnet_command_queue_close(&commands), SALTS_OK);
  check_equal(cnet_owner_close(&owner), SALTS_OK);
  check_equal(cnet_owner_destroy(&owner), SALTS_OK);
  check_equal(cnet_event_queue_close(&events), SALTS_OK);
  check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&commands), SALTS_OK);
  check_equal(cnet_session_table_destroy(&sessions), SALTS_OK);
}

static void cnet_owner_test_pipe_open_failure(native_io_backend_kind backend_kind) {
  cnet_session_table sessions = {0};
  cnet_command_queue commands = {0};
  cnet_event_queue events = {0};
  cnet_owner owner = {0};
  const cnet_command_queue_config command_config = {4u, sizeof(cnet_owner_connect_payload)};
  const cnet_event_queue_config event_config = {4u, 1u, 64u};
  const cnet_owner_config owner_config = {.backend_kind = backend_kind,
                                          .connection_capacity = 1u,
                                          .request_capacity = 2u,
                                          .completion_batch_capacity = 2u,
                                          .receive_buffer_bytes = 64u,
                                          .receive_buffer_count = 1u,
                                          .sessions = &sessions,
                                          .commands = &commands,
                                          .events = &events};
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};

  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);

  connect_payload.scheme = CNET_URI_PIPE;
  memcpy(connect_payload.pipe_name, "cnet-owner-missing-platform-pipe",
         sizeof("cnet-owner-missing-platform-pipe"));
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_FAILED);
  check(event.status != SALTS_OK);
  check_equal(event.stage, CNET_SESSION_STAGE_CONNECT);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, CNET_SESSION_TERMINAL_FAILED);
  check_equal(terminal.stage, CNET_SESSION_STAGE_CONNECT);
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

  check_equal(cnet_command_queue_close(&commands), SALTS_OK);
  check_equal(cnet_owner_close(&owner), SALTS_OK);
  check_equal(cnet_owner_destroy(&owner), SALTS_OK);
  check_equal(cnet_event_queue_close(&events), SALTS_OK);
  check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&commands), SALTS_OK);
  check_equal(cnet_session_table_destroy(&sessions), SALTS_OK);
}

static void cnet_owner_test_pipe(native_io_backend_kind backend_kind) {
  static const unsigned char outbound[] = {5u, 6u, 7u, 8u};
  static const unsigned char inbound[] = {8u, 3u, 2u, 1u};
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
  cnet_shared_test_named_pipe pipe;
  cnet_session_handle session = {0};
  cnet_owner_connect_payload connect_payload = {0};
  cnet_command command = {0};
  cnet_event_view event = {0};
  cnet_session_terminal terminal = {0};
  unsigned char received[sizeof(outbound)] = {0};

  check_equal(cnet_shared_test_named_pipe_start(&pipe), SALTS_OK);
  check_equal(cnet_session_table_init(&sessions, 1u), SALTS_OK);
  check_equal(cnet_command_queue_init(&commands, &command_config), SALTS_OK);
  check_equal(cnet_event_queue_init(&events, &event_config), SALTS_OK);
  check_equal(cnet_owner_init(&owner, &owner_config), SALTS_OK);
  check_equal(cnet_session_table_reserve(&sessions, &session), SALTS_OK);

  connect_payload.scheme = CNET_URI_PIPE;
  memcpy(connect_payload.pipe_name, pipe.name, strlen(pipe.name) + 1u);
  command =
      (cnet_command){CNET_COMMAND_CONNECT, session, &connect_payload, sizeof(connect_payload), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_OPEN),
              SALTS_OK);
  check_equal(cnet_shared_test_named_pipe_finish(&pipe), SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.kind, CNET_EVENT_STATE);
  check_equal(event.state, CNET_EVENT_STATE_CONNECTED);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);

  command = (cnet_command){CNET_COMMAND_SEND, session, outbound, sizeof(outbound), 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_drive(&owner, CNET_OWNER_TEST_TIMEOUT_MS), SALTS_OK);
  check_equal(cnet_shared_test_named_pipe_peer_read(&pipe, received, sizeof(received)), SALTS_OK);
  check_equal(received, outbound, sizeof(outbound));
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.kind, CNET_EVENT_SEND);
  check_equal(event.argument, sizeof(outbound));
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);

  command = (cnet_command){CNET_COMMAND_RECEIVE, session, NULL, 0u, 1u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_drive(&owner, 0u), SALTS_OK);
  check_equal(cnet_shared_test_named_pipe_peer_write(&pipe, inbound, sizeof(inbound)), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_event(&owner, &events, &event), SALTS_OK);
  check_equal(event.kind, CNET_EVENT_RECEIVE);
  check_equal(event.data, inbound, sizeof(inbound));
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);

  command = (cnet_command){CNET_COMMAND_CLOSE, session, NULL, 0u, 0u};
  check_equal(cnet_command_queue_publish(&commands, &command), SALTS_OK);
  check_equal(cnet_owner_test_drive_to_state(&owner, &sessions, session, CNET_SESSION_TERMINAL),
              SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_CLOSING);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_event_queue_take(&events, &event), SALTS_OK);
  check_equal(event.state, CNET_EVENT_STATE_CLOSED);
  check_equal(cnet_event_queue_release(&events, &event), SALTS_OK);
  check_equal(cnet_session_table_take_terminal(&sessions, session, &terminal), SALTS_OK);
  check_equal(terminal.kind, CNET_SESSION_TERMINAL_CLOSED);
  check_equal(cnet_session_table_recycle(&sessions, session), SALTS_OK);
  check_equal(cnet_owner_release_session(&owner, session), SALTS_OK);

  cnet_shared_test_named_pipe_close(&pipe);
  check_equal(cnet_command_queue_close(&commands), SALTS_OK);
  check_equal(cnet_owner_close(&owner), SALTS_OK);
  check_equal(cnet_owner_destroy(&owner), SALTS_OK);
  check_equal(cnet_event_queue_close(&events), SALTS_OK);
  check_equal(cnet_event_queue_destroy(&events), SALTS_OK);
  check_equal(cnet_command_queue_destroy(&commands), SALTS_OK);
  check_equal(cnet_session_table_destroy(&sessions), SALTS_OK);
}

spec("CNet owner shard") {
  it("owns a TCP session from command admission through terminal recycle") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_tcp(backends[index], false, CNET_OWNER_TEST_NO_TIMEOUT);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("owns the resolve to TCP connect state transition") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_tcp(backends[index], true, CNET_OWNER_TEST_NO_TIMEOUT);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("times out one pending read through the owner deadline queue") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_tcp(backends[index], false, CNET_OWNER_TEST_READ_TIMEOUT);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("keeps an expired connect terminal when a native success is already pending") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_tcp(backends[index], false, CNET_OWNER_TEST_CONNECT_TIMEOUT);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("expires resolution before accepting a late resolver result") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_tcp(backends[index], true, CNET_OWNER_TEST_CONNECT_TIMEOUT);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("keeps an expired write terminal when a native success is already pending") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_tcp(backends[index], false, CNET_OWNER_TEST_WRITE_TIMEOUT);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("contains resolver failure in one session and reports its stage") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_resolve_failure(backends[index]);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("owns a connected UDP session through bidirectional datagrams and terminal recycle") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      cnet_owner_test_udp(backends[index]);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("owns a platform byte-pipe session through bidirectional bytes and terminal recycle") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      if (native_io_backend_kind_supports_pipe(backends[index]))
        cnet_owner_test_pipe(backends[index]);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }

  it("reports a named platform pipe open failure at the connect stage") {
    native_io_backend_kind backends[CNET_OWNER_TEST_MAX_BACKENDS];
    const size_t count = cnet_owner_test_backends(backends);
    size_t index;
    check_equal(cnet_module_init(), SALTS_OK);
    for (index = 0u; index < count; ++index)
      if (native_io_backend_kind_supports_pipe(backends[index]))
        cnet_owner_test_pipe_open_failure(backends[index]);
    check_equal(cnet_module_shutdown(), SALTS_OK);
  }
}
