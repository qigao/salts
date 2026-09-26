#include <cflow/io_cnet_adapter.h>
#include <cflow/executor.h>
#include <cflow/graph.h>
#include <cflow/io_publisher.h>
#include <cflow/lower.h>
#include <cflow/scheduler.h>

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include "tinytest.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cflow_cnet_test_socket;
typedef int cflow_cnet_test_socklen;
  #define CFLOW_CNET_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cflow_cnet_test_socket;
typedef socklen_t cflow_cnet_test_socklen;
  #define CFLOW_CNET_TEST_INVALID_SOCKET (-1)
#endif

enum {
  CFLOW_CNET_TEST_TIMEOUT_MS = 5000,
  CFLOW_CNET_TEST_CAPACITY = 4,
  CFLOW_CNET_TEST_DRIVE_STEPS = 64
};

typedef struct cflow_cnet_test_token {
  cflow_io_cnet_receive_operation operation;
  unsigned char buffer[32];
  int released;
} cflow_cnet_test_token;

typedef struct cflow_cnet_test_completion {
  cflow_io_request_id request_id;
  cflow_io_completion completion;
  size_t count;
} cflow_cnet_test_completion;

typedef struct cflow_cnet_test_actor {
  cflow_executor executor;
  cflow_io_actor actor;
  cflow_cnet_test_completion completion;
} cflow_cnet_test_actor;

typedef struct cflow_cnet_test_forward {
  size_t states;
  size_t sends;
  cnet_connection_state last_state;
} cflow_cnet_test_forward;

typedef struct cflow_cnet_test_session {
  cnet_client client;
  cflow_io_cnet_session_adapter adapter;
  cnet_connection connection;
  cflow_cnet_test_socket listener;
  cflow_cnet_test_socket peer;
  cflow_cnet_test_forward forward;
} cflow_cnet_test_session;

typedef struct cflow_cnet_test_publisher_fixture {
  cflow_cnet_test_token token;
  size_t prepared;
  size_t encoded;
} cflow_cnet_test_publisher_fixture;

typedef struct cflow_cnet_test_sink {
  int value;
  size_t values;
  size_t errors;
  size_t done;
} cflow_cnet_test_sink;

static int cflow_cnet_test_network_start(void) {
#if defined(_WIN32)
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  return status == 0 ? SALTS_OK : -status;
#else
  return SALTS_OK;
#endif
}

static void cflow_cnet_test_network_stop(void) {
#if defined(_WIN32)
  (void)WSACleanup();
#endif
}

static int cflow_cnet_test_socket_error(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error == 0 ? SALTS_EIO : -error;
}

static bool cflow_cnet_test_would_block(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
  return error == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static void cflow_cnet_test_close_socket(
    cflow_cnet_test_socket socket_value) {
  if (socket_value == CFLOW_CNET_TEST_INVALID_SOCKET)
    return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cflow_cnet_test_set_nonblocking(
    cflow_cnet_test_socket socket_value) {
#if defined(_WIN32)
  u_long enabled = 1u;
  return ioctlsocket(socket_value, FIONBIO, &enabled) == 0
             ? SALTS_OK
             : cflow_cnet_test_socket_error();
#else
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags < 0)
    return cflow_cnet_test_socket_error();
  if ((flags & O_NONBLOCK) != 0)
    return SALTS_OK;
  return fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0
             ? SALTS_OK
             : cflow_cnet_test_socket_error();
#endif
}

static int cflow_cnet_test_listener(
    cflow_cnet_test_socket *out_listener,
    uint16_t *out_port) {
  struct sockaddr_in address;
  cflow_cnet_test_socklen length =
      (cflow_cnet_test_socklen)sizeof(address);

  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CFLOW_CNET_TEST_INVALID_SOCKET)
    return cflow_cnet_test_socket_error();
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0u;
  if (bind(*out_listener, (const struct sockaddr *)&address,
           (cflow_cnet_test_socklen)sizeof(address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)&address,
                  &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cflow_cnet_test_close_socket(*out_listener);
    *out_listener = CFLOW_CNET_TEST_INVALID_SOCKET;
    return cflow_cnet_test_socket_error();
  }
  if (cflow_cnet_test_set_nonblocking(*out_listener) != SALTS_OK) {
    cflow_cnet_test_close_socket(*out_listener);
    *out_listener = CFLOW_CNET_TEST_INVALID_SOCKET;
    return SALTS_EIO;
  }
  *out_port = ntohs(address.sin_port);
  return SALTS_OK;
}

static int cflow_cnet_test_send(
    cflow_cnet_test_socket socket_value,
    const void *data,
    size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t offset = 0u;
  const uint64_t deadline =
      salts_monotonic_ms() + CFLOW_CNET_TEST_TIMEOUT_MS;

  while (offset < size) {
#if defined(_WIN32)
    const int sent = send(
        socket_value, (const char *)bytes + offset,
        (int)(size - offset), 0);
    if (sent == SOCKET_ERROR) {
      if (cflow_cnet_test_would_block()) {
        if (salts_monotonic_ms() >= deadline)
          return SALTS_ETIMEDOUT;
        salts_thread_yield();
        continue;
      }
      return cflow_cnet_test_socket_error();
    }
#else
    const ssize_t sent = send(
        socket_value, bytes + offset, size - offset,
#if defined(MSG_NOSIGNAL)
        MSG_NOSIGNAL
#else
        0
#endif
    );
    if (sent < 0) {
      if (cflow_cnet_test_would_block()) {
        if (salts_monotonic_ms() >= deadline)
          return SALTS_ETIMEDOUT;
        salts_thread_yield();
        continue;
      }
      return cflow_cnet_test_socket_error();
    }
#endif
    if (sent <= 0)
      return SALTS_EIO;
    offset += (size_t)sent;
  }
  return SALTS_OK;
}

static cnet_client_config cflow_cnet_test_config(
    native_io_backend_kind backend) {
  const cnet_client_config config = {
      .backend = backend,
      .connection_capacity = 2u,
      .command_capacity = 8u,
      .request_capacity = 8u,
      .completion_batch_capacity = 4u,
      .event_capacity = 8u,
      .max_send_bytes = 1024u,
      .receive_buffer_bytes = 1024u};
  return config;
}

static size_t cflow_cnet_test_backends(
    native_io_backend_kind backends[2]) {
#if defined(_WIN32)
  backends[0] = NATIVE_IO_BACKEND_IOCP;
  return 1u;
#elif defined(__linux__)
  backends[0] = NATIVE_IO_BACKEND_EPOLL;
  backends[1] = NATIVE_IO_BACKEND_IO_URING;
  return 2u;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
  backends[0] = NATIVE_IO_BACKEND_KQUEUE;
  return 1u;
#else
  (void)backends;
  return 0u;
#endif
}

static void cflow_cnet_test_state_forward(
    void *user,
    cnet_connection connection,
    cnet_connection_state state,
    const cnet_error *error) {
  cflow_cnet_test_forward *forward =
      (cflow_cnet_test_forward *)user;
  (void)connection;
  (void)error;
  ++forward->states;
  forward->last_state = state;
}

static void cflow_cnet_test_send_forward(
    void *user,
    cnet_connection connection,
    size_t size) {
  cflow_cnet_test_forward *forward =
      (cflow_cnet_test_forward *)user;
  (void)connection;
  (void)size;
  ++forward->sends;
}

static bool cflow_cnet_test_adapter_state(
    cflow_io_cnet_session_adapter *adapter,
    cnet_connection_state state) {
  cflow_io_cnet_session_adapter_stats stats = {0};
  return cflow_io_cnet_session_adapter_get_stats(adapter, &stats) &&
         stats.state_known && stats.state == state;
}

static int cflow_cnet_test_poll_until_connected(
    cflow_cnet_test_session *session) {
  const uint64_t deadline =
      salts_monotonic_ms() + CFLOW_CNET_TEST_TIMEOUT_MS;

  while (!cflow_cnet_test_adapter_state(
      &session->adapter, CNET_CONNECTION_CONNECTED)) {
    if (session->peer == CFLOW_CNET_TEST_INVALID_SOCKET) {
      session->peer = accept(session->listener, NULL, NULL);
      if (session->peer == CFLOW_CNET_TEST_INVALID_SOCKET &&
          !cflow_cnet_test_would_block())
        return cflow_cnet_test_socket_error();
      if (session->peer != CFLOW_CNET_TEST_INVALID_SOCKET)
        (void)cflow_cnet_test_set_nonblocking(session->peer);
    }
    {
      size_t events = 0u;
      const int status =
          cnet_client_poll(&session->client, 1u, &events);
      if (status != SALTS_OK)
        return status;
    }
    if (salts_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }

  while (session->peer == CFLOW_CNET_TEST_INVALID_SOCKET) {
    session->peer = accept(session->listener, NULL, NULL);
    if (session->peer != CFLOW_CNET_TEST_INVALID_SOCKET) {
      (void)cflow_cnet_test_set_nonblocking(session->peer);
      break;
    }
    if (!cflow_cnet_test_would_block())
      return cflow_cnet_test_socket_error();
    if (salts_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
    salts_thread_yield();
  }
  return SALTS_OK;
}

static int cflow_cnet_test_poll_until_terminal(
    cflow_cnet_test_session *session) {
  const uint64_t deadline =
      salts_monotonic_ms() + CFLOW_CNET_TEST_TIMEOUT_MS;
  cflow_io_cnet_session_adapter_stats stats = {0};

  for (;;) {
    if (cflow_io_cnet_session_adapter_get_stats(
            &session->adapter, &stats) &&
        stats.terminal)
      return SALTS_OK;
    {
      size_t events = 0u;
      const int status =
          cnet_client_poll(&session->client, 1u, &events);
      if (status != SALTS_OK)
        return status;
    }
    if (salts_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }
}

static int cflow_cnet_test_poll_until_received(
    cflow_cnet_test_session *session,
    uint64_t expected) {
  const uint64_t deadline =
      salts_monotonic_ms() + CFLOW_CNET_TEST_TIMEOUT_MS;

  for (;;) {
    cflow_io_cnet_session_adapter_stats stats = {0};
    if (cflow_io_cnet_session_adapter_get_stats(
            &session->adapter, &stats) &&
        stats.received_values >= expected)
      return SALTS_OK;
    {
      size_t events = 0u;
      const int status =
          cnet_client_poll(&session->client, 1u, &events);
      if (status != SALTS_OK)
        return status;
    }
    if (salts_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }
}

static int cflow_cnet_test_session_init(
    cflow_cnet_test_session *session,
    native_io_backend_kind backend) {
  cflow_io_cnet_session_adapter_config adapter_config;
  cnet_connect_options options;
  cnet_client_config config;
  cnet_observer observer;
  uint16_t port = 0u;
  char uri[64];
  int status;

  memset(session, 0, sizeof(*session));
  session->listener = CFLOW_CNET_TEST_INVALID_SOCKET;
  session->peer = CFLOW_CNET_TEST_INVALID_SOCKET;

  status = cflow_cnet_test_listener(
      &session->listener, &port);
  if (status != SALTS_OK)
    return status;

  config = cflow_cnet_test_config(backend);
  status = cnet_client_init(&session->client, &config);
  if (status != SALTS_OK)
    return status;

  adapter_config =
      (cflow_io_cnet_session_adapter_config){
          .client = &session->client,
          .bridge_capacity = CFLOW_CNET_TEST_CAPACITY,
          .on_state = cflow_cnet_test_state_forward,
          .on_send = cflow_cnet_test_send_forward,
          .observer_user = &session->forward};
  status = cflow_io_cnet_session_adapter_init(
      &session->adapter, &adapter_config);
  if (status != SALTS_OK)
    return status;

  observer =
      cflow_io_cnet_session_adapter_observer(&session->adapter);
  (void)snprintf(
      uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned)port);
  options = (cnet_connect_options){
      .uri = uri, .observer = observer};
  status = cnet_connect(
      &session->client, &options, &session->connection);
  if (status != SALTS_OK)
    return status;
  status = cflow_io_cnet_session_adapter_bind(
      &session->adapter, session->connection);
  if (status != SALTS_OK)
    return status;

  status = cflow_cnet_test_poll_until_connected(session);
  if (status == SALTS_OK) {
    cflow_cnet_test_close_socket(session->listener);
    session->listener = CFLOW_CNET_TEST_INVALID_SOCKET;
  }
  return status;
}

static void cflow_cnet_test_session_finish(
    cflow_cnet_test_session *session) {
  cflow_io_cnet_session_adapter_stats stats = {0};
  int status;

  if (session->adapter.impl != NULL &&
      cflow_io_cnet_session_adapter_get_stats(
          &session->adapter, &stats) &&
      !stats.terminal &&
      session->connection.slot != 0u) {
    status = cnet_close(
        &session->client, session->connection);
    check_true(status == SALTS_OK ||
               status == SALTS_EALREADY ||
               status == SALTS_ENOENT);
    if (status == SALTS_OK || status == SALTS_EALREADY)
      check_equal(cflow_cnet_test_poll_until_terminal(session),
                  SALTS_OK);
  }

  if (session->adapter.impl != NULL) {
    status =
        cflow_io_cnet_session_adapter_close(&session->adapter);
    check_true(status == SALTS_OK || status == SALTS_EALREADY);
    check_equal(
        cflow_io_cnet_session_adapter_destroy(&session->adapter),
        SALTS_OK);
  }

  if (session->client.impl != NULL) {
    status = cnet_client_stop(
        &session->client, CFLOW_CNET_TEST_TIMEOUT_MS);
    check_true(status == SALTS_OK ||
               status == SALTS_ETIMEDOUT);
    if (status == SALTS_ETIMEDOUT)
      check_equal(
          cnet_client_stop(
              &session->client, CFLOW_CNET_TEST_TIMEOUT_MS),
          SALTS_OK);
    check_equal(cnet_client_destroy(&session->client), SALTS_OK);
  }

  cflow_cnet_test_close_socket(session->peer);
  cflow_cnet_test_close_socket(session->listener);
  session->peer = CFLOW_CNET_TEST_INVALID_SOCKET;
  session->listener = CFLOW_CNET_TEST_INVALID_SOCKET;
}

static void cflow_cnet_test_token_release(void *user) {
  cflow_io_cnet_receive_operation *operation =
      (cflow_io_cnet_receive_operation *)user;
  cflow_cnet_test_token *token =
      (cflow_cnet_test_token *)operation;
  ++token->released;
}

static void cflow_cnet_test_completion_record(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion) {
  cflow_cnet_test_completion *probe =
      (cflow_cnet_test_completion *)user;
  (void)lease_id;
  (void)operation_user;
  probe->request_id = request_id;
  probe->completion = *completion;
  ++probe->count;
}

static int cflow_cnet_test_actor_init(
    cflow_cnet_test_actor *fixture,
    cflow_io_cnet_session_adapter *adapter) {
  cflow_io_actor_config config;

  memset(fixture, 0, sizeof(*fixture));
  if (!cflow_executor_manual_init_with_capacity(
          &fixture->executor, CFLOW_CNET_TEST_CAPACITY))
    return SALTS_ENOMEM;
  memset(&config, 0, sizeof(config));
  config.request_capacity = CFLOW_CNET_TEST_CAPACITY;
  config.command_capacity = CFLOW_CNET_TEST_CAPACITY * 2u;
  config.executor = &fixture->executor;
  config.backend = cflow_io_cnet_session_adapter_actor_ops();
  config.backend_user = adapter;
  config.completion = cflow_cnet_test_completion_record;
  config.completion_user = &fixture->completion;
  const int status = cflow_io_actor_init(
      &fixture->actor, &config);
  if (status != SALTS_OK)
    cflow_executor_destroy(&fixture->executor);
  return status;
}

static void cflow_cnet_test_actor_finish(
    cflow_cnet_test_actor *fixture) {
  int status = cflow_io_actor_close(&fixture->actor);
  check_true(status == SALTS_OK || status == SALTS_EALREADY);
  for (size_t attempts = 0u;
       attempts < CFLOW_CNET_TEST_DRIVE_STEPS &&
       !cflow_io_actor_is_quiescent(&fixture->actor);
       ++attempts) {
    (void)cflow_io_actor_run_ready(
        &fixture->actor, CFLOW_CNET_TEST_DRIVE_STEPS);
    (void)cflow_executor_run_ready(&fixture->executor);
  }
  check_true(cflow_io_actor_is_quiescent(&fixture->actor));
  check_equal(cflow_io_actor_destroy(&fixture->actor), SALTS_OK);
  check_true(cflow_executor_shutdown(&fixture->executor));
  cflow_executor_destroy(&fixture->executor);
}

static cflow_io_submit_result cflow_cnet_test_submit(
    cflow_cnet_test_actor *fixture,
    cflow_cnet_test_token *token,
    cflow_io_lease_id lease) {
  cflow_io_operation moved;

  memset(token, 0, sizeof(*token));
  token->operation = (cflow_io_cnet_receive_operation){
      .buffer = token->buffer,
      .capacity = sizeof(token->buffer)};
  moved = (cflow_io_operation){
      &token->operation, cflow_cnet_test_token_release};
  return cflow_io_actor_try_submit(
      &fixture->actor, lease, &moved);
}

static void cflow_cnet_test_actor_drive(
    cflow_cnet_test_actor *fixture) {
  (void)cflow_io_actor_run_ready(
      &fixture->actor, CFLOW_CNET_TEST_DRIVE_STEPS);
  (void)cflow_executor_run_ready(&fixture->executor);
}

static void cflow_cnet_test_deliver_and_ack(
    cflow_cnet_test_actor *fixture,
    cflow_io_request_id request_id) {
  for (size_t attempts = 0u;
       attempts < CFLOW_CNET_TEST_DRIVE_STEPS &&
       fixture->completion.count == 0u;
       ++attempts)
    cflow_cnet_test_actor_drive(fixture);
  check_equal(fixture->completion.count, (size_t)1u);
  check_equal(fixture->completion.request_id, request_id);
  check_equal(
      cflow_io_actor_acknowledge(&fixture->actor, request_id),
      CFLOW_IO_ACK_RELEASED);
}

static void cflow_cnet_test_receive_backend(
    native_io_backend_kind backend) {
  cflow_cnet_test_session session;
  cflow_cnet_test_actor actor;
  cflow_cnet_test_token token;
  cflow_io_submit_result submitted;
  static const char payload[] = "cflow-cnet";
  int status = cflow_cnet_test_session_init(&session, backend);

  if (status == SALTS_ENOTSUP) {
    check_equal(status, SALTS_ENOTSUP);
    return;
  }
  check_equal(status, SALTS_OK);
  check_equal(cflow_cnet_test_actor_init(
                  &actor, &session.adapter),
              SALTS_OK);

  submitted = cflow_cnet_test_submit(&actor, &token, 1u);
  check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
  cflow_cnet_test_actor_drive(&actor);
  {
    cflow_io_cnet_session_adapter_stats stats = {0};
    check_true(cflow_io_cnet_session_adapter_get_stats(
        &session.adapter, &stats));
    check_equal(stats.pending_credits, (size_t)1u);
    check_equal(stats.admitted_credits, (uint64_t)1u);
  }

  check_equal(cflow_cnet_test_send(
                  session.peer, payload, sizeof(payload)),
              SALTS_OK);
  check_equal(cflow_cnet_test_poll_until_received(
                  &session, 1u),
              SALTS_OK);
  cflow_cnet_test_deliver_and_ack(
      &actor, submitted.request_id);
  check_equal(
      actor.completion.completion.kind,
      CFLOW_IO_COMPLETION_OK);
  check_equal(
      actor.completion.completion.bytes,
      sizeof(payload));
  check_equal(token.operation.size, sizeof(payload));
  check_equal(token.operation.kind, CNET_MESSAGE_BYTES);
  check_equal(
      memcmp(token.buffer, payload, sizeof(payload)), 0);
  check_equal(token.released, 1);

  cflow_cnet_test_actor_finish(&actor);
  cflow_cnet_test_session_finish(&session);
}

static void cflow_cnet_test_cancel_backend(
    native_io_backend_kind backend) {
  cflow_cnet_test_session session;
  cflow_cnet_test_actor actor;
  cflow_cnet_test_token first;
  cflow_cnet_test_token second;
  cflow_io_submit_result first_submit;
  cflow_io_submit_result second_submit;
  static const char discarded[] = "discarded";
  static const char delivered[] = "delivered";
  int status = cflow_cnet_test_session_init(&session, backend);

  if (status == SALTS_ENOTSUP) {
    check_equal(status, SALTS_ENOTSUP);
    return;
  }
  check_equal(status, SALTS_OK);
  check_equal(cflow_cnet_test_actor_init(
                  &actor, &session.adapter),
              SALTS_OK);

  first_submit = cflow_cnet_test_submit(&actor, &first, 11u);
  check_equal(first_submit.status, CFLOW_IO_SUBMIT_ACCEPTED);
  cflow_cnet_test_actor_drive(&actor);
  check_equal(
      cflow_io_actor_try_cancel(
          &actor.actor, first_submit.request_id),
      CFLOW_IO_CANCEL_ACCEPTED);
  cflow_cnet_test_actor_drive(&actor);
  cflow_cnet_test_deliver_and_ack(
      &actor, first_submit.request_id);
  check_equal(
      actor.completion.completion.kind,
      CFLOW_IO_COMPLETION_CANCELLED);
  check_equal(first.released, 1);

  {
    cflow_io_cnet_session_adapter_stats stats = {0};
    check_true(cflow_io_cnet_session_adapter_get_stats(
        &session.adapter, &stats));
    check_equal(stats.pending_credits, (size_t)1u);
    check_equal(stats.cancelled_tombstones, (size_t)1u);
    check_false(stats.terminal);
  }

  check_equal(cflow_cnet_test_send(
                  session.peer, discarded, sizeof(discarded)),
              SALTS_OK);
  check_equal(cflow_cnet_test_poll_until_received(
                  &session, 1u),
              SALTS_OK);
  {
    cflow_io_cnet_session_adapter_stats stats = {0};
    check_true(cflow_io_cnet_session_adapter_get_stats(
        &session.adapter, &stats));
    check_equal(stats.pending_credits, (size_t)0u);
    check_equal(stats.cancelled_tombstones, (size_t)0u);
    check_equal(stats.discarded_cancelled_values, (uint64_t)1u);
    check_false(stats.terminal);
  }

  memset(&actor.completion, 0, sizeof(actor.completion));
  second_submit =
      cflow_cnet_test_submit(&actor, &second, 12u);
  check_equal(second_submit.status, CFLOW_IO_SUBMIT_ACCEPTED);
  cflow_cnet_test_actor_drive(&actor);
  check_equal(cflow_cnet_test_send(
                  session.peer, delivered, sizeof(delivered)),
              SALTS_OK);
  check_equal(cflow_cnet_test_poll_until_received(
                  &session, 2u),
              SALTS_OK);
  cflow_cnet_test_deliver_and_ack(
      &actor, second_submit.request_id);
  check_equal(
      actor.completion.completion.kind,
      CFLOW_IO_COMPLETION_OK);
  check_equal(
      memcmp(second.buffer, delivered, sizeof(delivered)), 0);
  check_equal(second.released, 1);

  cflow_cnet_test_actor_finish(&actor);
  cflow_cnet_test_session_finish(&session);
}

static void cflow_cnet_test_terminal_backend(
    native_io_backend_kind backend) {
  cflow_cnet_test_session session;
  cflow_cnet_test_actor actor;
  cflow_cnet_test_token token;
  cflow_io_submit_result submitted;
  int status = cflow_cnet_test_session_init(&session, backend);

  if (status == SALTS_ENOTSUP) {
    check_equal(status, SALTS_ENOTSUP);
    return;
  }
  check_equal(status, SALTS_OK);
  check_equal(cflow_cnet_test_actor_init(
                  &actor, &session.adapter),
              SALTS_OK);

  submitted = cflow_cnet_test_submit(&actor, &token, 21u);
  check_equal(submitted.status, CFLOW_IO_SUBMIT_ACCEPTED);
  cflow_cnet_test_actor_drive(&actor);

  cflow_cnet_test_close_socket(session.peer);
  session.peer = CFLOW_CNET_TEST_INVALID_SOCKET;
  check_equal(cflow_cnet_test_poll_until_terminal(&session), SALTS_OK);
  cflow_cnet_test_deliver_and_ack(
      &actor, submitted.request_id);
  check_true(
      actor.completion.completion.kind == CFLOW_IO_COMPLETION_EOF ||
      actor.completion.completion.kind == CFLOW_IO_COMPLETION_FAILED);
  check_equal(token.released, 1);
  {
    cflow_io_cnet_session_adapter_stats stats = {0};
    check_true(cflow_io_cnet_session_adapter_get_stats(
        &session.adapter, &stats));
    check_equal(stats.pending_credits, (size_t)0u);
    check_equal(stats.active_bridges, (size_t)0u);
    check_equal(stats.terminal_drains, (uint64_t)1u);
  }

  cflow_cnet_test_actor_finish(&actor);
  cflow_cnet_test_session_finish(&session);
}

static cflow_io_publisher_prepare_status
cflow_cnet_test_publisher_prepare(
    void *user,
    cflow_io_operation *operation,
    const char **error) {
  cflow_cnet_test_publisher_fixture *fixture =
      (cflow_cnet_test_publisher_fixture *)user;
  (void)error;

  if (fixture->prepared != 0u)
    return CFLOW_IO_PUBLISHER_PREPARE_DONE;
  memset(&fixture->token, 0, sizeof(fixture->token));
  fixture->token.operation =
      (cflow_io_cnet_receive_operation){
          .buffer = fixture->token.buffer,
          .capacity = sizeof(fixture->token.buffer)};
  operation->user = &fixture->token.operation;
  operation->release = cflow_cnet_test_token_release;
  ++fixture->prepared;
  return CFLOW_IO_PUBLISHER_PREPARE_OPERATION;
}

static cflow_read_status cflow_cnet_test_publisher_encode(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion,
    void *out_value,
    const char **error) {
  static const char failure[] = "CNet receive failed";
  cflow_cnet_test_publisher_fixture *fixture =
      (cflow_cnet_test_publisher_fixture *)user;
  cflow_io_cnet_receive_operation *operation =
      (cflow_io_cnet_receive_operation *)operation_user;
  (void)request_id;
  (void)lease_id;

  if (completion->kind != CFLOW_IO_COMPLETION_OK) {
    *error = failure;
    return CFLOW_READ_ERROR;
  }
  *(int *)out_value = (int)operation->size;
  ++fixture->encoded;
  return CFLOW_READ_VALUE;
}

static bool cflow_cnet_test_sink_value(
    void *user,
    const cmeta_type_desc *type,
    const void *value) {
  cflow_cnet_test_sink *sink =
      (cflow_cnet_test_sink *)user;
  (void)type;
  sink->value = *(const int *)value;
  ++sink->values;
  return true;
}

static void cflow_cnet_test_sink_error(
    void *user, const char *message) {
  cflow_cnet_test_sink *sink =
      (cflow_cnet_test_sink *)user;
  (void)message;
  ++sink->errors;
}

static void cflow_cnet_test_sink_done(void *user) {
  cflow_cnet_test_sink *sink =
      (cflow_cnet_test_sink *)user;
  ++sink->done;
}

static void cflow_cnet_test_publisher_backend(
    native_io_backend_kind backend) {
  cflow_cnet_test_session session;
  cflow_publisher source = {0};
  cflow_io_publisher_owner owner = {0};
  cflow_graph surface = {0};
  cflow_graph normalized = {0};
  cflow_scheduler scheduler = {0};
  cflow_subscription run = {0};
  cflow_cnet_test_publisher_fixture fixture = {0};
  cflow_cnet_test_sink sink_state = {0};
  cflow_subscriber_callbacks callbacks = {
      cflow_cnet_test_sink_value,
      cflow_cnet_test_sink_error,
      cflow_cnet_test_sink_done,
      &sink_state};
  cflow_subscriber sink =
      cflow_subscriber_from_callbacks(&callbacks);
  cflow_io_publisher_config config = {0};
  static const char payload[] = "publisher";
  int status = cflow_cnet_test_session_init(&session, backend);

  if (status == SALTS_ENOTSUP) {
    check_equal(status, SALTS_ENOTSUP);
    return;
  }
  check_equal(status, SALTS_OK);

  config.name = "cflow-cnet-session";
  config.type = &cmeta_type_int;
  config.backend = cflow_io_cnet_session_adapter_actor_ops();
  config.backend_user = &session.adapter;
  config.prepare = cflow_cnet_test_publisher_prepare;
  config.encode = cflow_cnet_test_publisher_encode;
  config.user = &fixture;

  cflow_graph_init(&surface, &cmeta_type_int);
  normalized.root = CMETA_INVALID_ID;
  check_true(cflow_graph_normalize(&normalized, &surface));
  check_true(cflow_scheduler_test_init(&scheduler));
  check_equal(
      cflow_publisher_from_io_actor(
          &source, &owner, &config),
      SALTS_OK);
  check_true(cflow_subscribe(
      &run, &normalized, &source, &scheduler, &sink));

  (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
  {
    cflow_io_cnet_session_adapter_stats stats = {0};
    check_equal(fixture.prepared, (size_t)0u);
    check_true(cflow_io_cnet_session_adapter_get_stats(
        &session.adapter, &stats));
    check_equal(stats.admitted_credits, (uint64_t)0u);
  }

  check_true(cflow_subscription_request(&run, 1u));
  (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
  for (size_t attempts = 0u;
       attempts < CFLOW_CNET_TEST_DRIVE_STEPS;
       ++attempts) {
    size_t progressed = 0u;
    check_equal(
        cflow_io_publisher_owner_run_ready(
            &owner, CFLOW_CNET_TEST_DRIVE_STEPS,
            &progressed),
        SALTS_OK);
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
    if (fixture.prepared != 0u)
      break;
  }
  check_equal(fixture.prepared, (size_t)1u);
  {
    cflow_io_cnet_session_adapter_stats stats = {0};
    check_true(cflow_io_cnet_session_adapter_get_stats(
        &session.adapter, &stats));
    check_equal(stats.admitted_credits, (uint64_t)1u);
  }

  check_equal(cflow_cnet_test_send(
                  session.peer, payload, sizeof(payload)),
              SALTS_OK);
  check_equal(cflow_cnet_test_poll_until_received(
                  &session, 1u),
              SALTS_OK);

  for (size_t attempts = 0u;
       attempts < CFLOW_CNET_TEST_DRIVE_STEPS &&
       sink_state.values == 0u;
       ++attempts) {
    size_t progressed = 0u;
    check_equal(
        cflow_io_publisher_owner_run_ready(
            &owner, CFLOW_CNET_TEST_DRIVE_STEPS,
            &progressed),
        SALTS_OK);
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
  }
  check_equal(fixture.encoded, (size_t)1u);
  check_equal(sink_state.values, (size_t)1u);
  check_equal(sink_state.value, (int)sizeof(payload));
  check_equal(sink_state.errors, (size_t)0u);
  check_equal(fixture.token.released, 1);

  cflow_subscription_close(&run);
  for (size_t attempts = 0u;
       attempts < CFLOW_CNET_TEST_DRIVE_STEPS &&
       !cflow_io_publisher_owner_is_quiescent(&owner);
       ++attempts) {
    size_t progressed = 0u;
    check_equal(
        cflow_io_publisher_owner_run_ready(
            &owner, CFLOW_CNET_TEST_DRIVE_STEPS,
            &progressed),
        SALTS_OK);
    (void)cflow_scheduler_run_until_idle(&scheduler, 0u);
  }
  check_true(cflow_io_publisher_owner_is_quiescent(&owner));
  check_equal(cflow_io_publisher_owner_close(&owner), SALTS_OK);

  cflow_scheduler_destroy(&scheduler);
  cflow_graph_destroy(&normalized);
  cflow_graph_destroy(&surface);
  cflow_cnet_test_session_finish(&session);
}

spec("CFlow CNet session receive adapter") {
  it("maps one Actor receive credit without owning CNet poll") {
    native_io_backend_kind backends[2];
    const size_t count = cflow_cnet_test_backends(backends);

    check_equal(cflow_cnet_test_network_start(), SALTS_OK);
    for (size_t index = 0u; index < count; ++index)
      cflow_cnet_test_receive_backend(backends[index]);
    cflow_cnet_test_network_stop();
  }

  it("keeps cancelled CFlow credit as a bounded CNet tombstone") {
    native_io_backend_kind backends[2];
    const size_t count = cflow_cnet_test_backends(backends);

    check_equal(cflow_cnet_test_network_start(), SALTS_OK);
    for (size_t index = 0u; index < count; ++index)
      cflow_cnet_test_cancel_backend(backends[index]);
    cflow_cnet_test_network_stop();
  }

  it("maps CNet session terminal onto pending Actor receives") {
    native_io_backend_kind backends[2];
    const size_t count = cflow_cnet_test_backends(backends);

    check_equal(cflow_cnet_test_network_start(), SALTS_OK);
    for (size_t index = 0u; index < count; ++index)
      cflow_cnet_test_terminal_backend(backends[index]);
    cflow_cnet_test_network_stop();
  }

  it("maps Reactive demand onto CNet receive credit") {
    native_io_backend_kind backends[2];
    const size_t count = cflow_cnet_test_backends(backends);

    check_equal(cflow_cnet_test_network_start(), SALTS_OK);
    for (size_t index = 0u; index < count; ++index)
      cflow_cnet_test_publisher_backend(backends[index]);
    cflow_cnet_test_network_stop();
  }
}
