#include "tinytest.h"
#include <chttp/chttp.h>

#include <turbo/thread.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET chttp_requests_test_socket;
  #define CHTTP_REQUESTS_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int chttp_requests_test_socket;
  #define CHTTP_REQUESTS_TEST_INVALID_SOCKET (-1)
#endif

enum {
  CHTTP_REQUESTS_TEST_TIMEOUT_MS = 5000,
  CHTTP_REQUESTS_TEST_DEADLINE_MS = 20,
  CHTTP_REQUESTS_TEST_SERVER_HOLD_MS = 100
};

typedef struct chttp_requests_test_server {
  chttp_requests_test_socket listener;
  const void *expected;
  size_t expected_size;
  const void *response;
  size_t response_size;
  uint32_t hold_after_receive_ms;
  int status;
} chttp_requests_test_server;

typedef struct chttp_requests_test_keep_alive_server {
  chttp_requests_test_socket listener;
  const void *first_expected;
  size_t first_expected_size;
  const void *second_expected;
  size_t second_expected_size;
  const void *first_response;
  size_t first_response_size;
  const void *second_response;
  size_t second_response_size;
  int status;
} chttp_requests_test_keep_alive_server;

typedef int (*chttp_requests_test_call_fn)(chttp_client *, const chttp_options *, chttp_response *,
                                           chttp_error *);

typedef struct chttp_requests_test_case {
  const char *method;
  const char *target;
  const char *request_body;
  chttp_requests_test_call_fn call;
  const char *wire_response;
  size_t wire_response_size;
  unsigned int status_code;
  const char *reason;
  const char *response_body;
  size_t response_body_size;
} chttp_requests_test_case;

static const char chttp_requests_test_response[] = "HTTP/1.1 201 Created\r\n"
                                                   "X-Mode: requests\r\n"
                                                   "Content-Length: 2\r\n"
                                                   "Connection: close\r\n"
                                                   "\r\n"
                                                   "ok";

static const char chttp_requests_test_head_response[] = "HTTP/1.1 200 OK\r\n"
                                                        "X-Mode: requests\r\n"
                                                        "Content-Length: 2\r\n"
                                                        "Connection: close\r\n"
                                                        "\r\n";

static const char chttp_requests_test_keep_alive_response[] = "HTTP/1.1 200 OK\r\n"
                                                              "Content-Length: 3\r\n"
                                                              "Connection: keep-alive\r\n"
                                                              "\r\n"
                                                              "one";

static const char chttp_requests_test_close_response[] = "HTTP/1.1 200 OK\r\n"
                                                         "Content-Length: 3\r\n"
                                                         "Connection: close\r\n"
                                                         "\r\n"
                                                         "two";

static void chttp_requests_test_close_socket(chttp_requests_test_socket socket_value) {
  if (socket_value == CHTTP_REQUESTS_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int chttp_requests_test_set_timeout(chttp_requests_test_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                    (int)sizeof(timeout_ms)) == 0
             ? TURBO_OK
             : TURBO_EIO;
#else
  const struct timeval timeout = {CHTTP_REQUESTS_TEST_TIMEOUT_MS / 1000,
                                  (CHTTP_REQUESTS_TEST_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout)) ==
                 0
             ? TURBO_OK
             : TURBO_EIO;
#endif
}

static int chttp_requests_test_listener(chttp_requests_test_socket *out_listener,
                                        uint16_t *out_port) {
  struct sockaddr_in address;
#if defined(_WIN32)
  int length = (int)sizeof(address);
#else
  socklen_t length = (socklen_t)sizeof(address);
#endif
  if (out_listener == NULL || out_port == NULL) return TURBO_EINVAL;
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CHTTP_REQUESTS_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)&address, (int)sizeof(address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)&address, &length) != 0 ||
      listen(*out_listener, 1) != 0) {
    chttp_requests_test_close_socket(*out_listener);
    *out_listener = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  *out_port = ntohs(address.sin_port);
  return TURBO_OK;
}

static int chttp_requests_test_recv_all(chttp_requests_test_socket socket_value, void *data,
                                        size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const int received = recv(socket_value, (char *)data + offset, (int)(size - offset), 0);
    if (received <= 0) return TURBO_EIO;
    offset += (size_t)received;
  }
  return TURBO_OK;
}

static int chttp_requests_test_send_all(chttp_requests_test_socket socket_value, const void *data,
                                        size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const int sent = send(socket_value, (const char *)data + offset, (int)(size - offset), 0);
    if (sent <= 0) return TURBO_EIO;
    offset += (size_t)sent;
  }
  return TURBO_OK;
}

static chttp_requests_test_socket chttp_requests_test_accept(chttp_requests_test_socket listener) {
  fd_set readable;
  struct timeval timeout = {CHTTP_REQUESTS_TEST_TIMEOUT_MS / 1000,
                            (CHTTP_REQUESTS_TEST_TIMEOUT_MS % 1000) * 1000};
  int status;
  FD_ZERO(&readable);
  FD_SET(listener, &readable);
#if defined(_WIN32)
  status = select(0, &readable, NULL, NULL, &timeout);
#else
  status = select(listener + 1, &readable, NULL, NULL, &timeout);
#endif
  return status == 1 ? accept(listener, NULL, NULL) : CHTTP_REQUESTS_TEST_INVALID_SOCKET;
}

static void chttp_requests_test_serve(void *user) {
  chttp_requests_test_server *server = (chttp_requests_test_server *)user;
  chttp_requests_test_socket accepted = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
  unsigned char received[1024];
  if (server == NULL || server->expected_size > sizeof(received)) return;
  accepted = accept(server->listener, NULL, NULL);
  if (accepted == CHTTP_REQUESTS_TEST_INVALID_SOCKET) {
    server->status = TURBO_EIO;
    return;
  }
  server->status = chttp_requests_test_set_timeout(accepted);
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_recv_all(accepted, received, server->expected_size);
  if (server->status == TURBO_OK && memcmp(received, server->expected, server->expected_size) != 0)
    server->status = TURBO_EPROTO;
  if (server->status == TURBO_OK && server->hold_after_receive_ms != 0u)
    turbo_sleep_ms(server->hold_after_receive_ms);
  if (server->status == TURBO_OK)
    server->status =
        chttp_requests_test_send_all(accepted, server->response, server->response_size);
  chttp_requests_test_close_socket(accepted);
}

static void chttp_requests_test_serve_keep_alive(void *user) {
  chttp_requests_test_keep_alive_server *server = (chttp_requests_test_keep_alive_server *)user;
  chttp_requests_test_socket accepted = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
  unsigned char received[1024];
  if (server == NULL || server->first_expected_size > sizeof(received) ||
      server->second_expected_size > sizeof(received))
    return;
  accepted = accept(server->listener, NULL, NULL);
  if (accepted == CHTTP_REQUESTS_TEST_INVALID_SOCKET) {
    server->status = TURBO_EIO;
    return;
  }
  server->status = chttp_requests_test_set_timeout(accepted);
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_recv_all(accepted, received, server->first_expected_size);
  if (server->status == TURBO_OK &&
      memcmp(received, server->first_expected, server->first_expected_size) != 0)
    server->status = TURBO_EPROTO;
  if (server->status == TURBO_OK)
    server->status =
        chttp_requests_test_send_all(accepted, server->first_response, server->first_response_size);
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_recv_all(accepted, received, server->second_expected_size);
  if (server->status == TURBO_OK &&
      memcmp(received, server->second_expected, server->second_expected_size) != 0)
    server->status = TURBO_EPROTO;
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_send_all(accepted, server->second_response,
                                                  server->second_response_size);
  chttp_requests_test_close_socket(accepted);
}

static void chttp_requests_test_serve_origin_switch(void *user) {
  chttp_requests_test_keep_alive_server *server = (chttp_requests_test_keep_alive_server *)user;
  chttp_requests_test_socket accepted = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
  unsigned char received[1024];
  if (server == NULL || server->first_expected_size > sizeof(received) ||
      server->second_expected_size > sizeof(received))
    return;

  accepted = chttp_requests_test_accept(server->listener);
  if (accepted == CHTTP_REQUESTS_TEST_INVALID_SOCKET) {
    server->status = TURBO_EIO;
    return;
  }
  server->status = chttp_requests_test_set_timeout(accepted);
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_recv_all(accepted, received, server->first_expected_size);
  if (server->status == TURBO_OK &&
      memcmp(received, server->first_expected, server->first_expected_size) != 0)
    server->status = TURBO_EPROTO;
  if (server->status == TURBO_OK)
    server->status =
        chttp_requests_test_send_all(accepted, server->first_response, server->first_response_size);
  if (server->status == TURBO_OK && recv(accepted, (char *)received, 1, 0) != 0)
    server->status = TURBO_EPROTO;
  chttp_requests_test_close_socket(accepted);

  if (server->status != TURBO_OK) return;
  accepted = chttp_requests_test_accept(server->listener);
  if (accepted == CHTTP_REQUESTS_TEST_INVALID_SOCKET) {
    server->status = TURBO_EIO;
    return;
  }
  server->status = chttp_requests_test_set_timeout(accepted);
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_recv_all(accepted, received, server->second_expected_size);
  if (server->status == TURBO_OK &&
      memcmp(received, server->second_expected, server->second_expected_size) != 0)
    server->status = TURBO_EPROTO;
  if (server->status == TURBO_OK)
    server->status = chttp_requests_test_send_all(accepted, server->second_response,
                                                  server->second_response_size);
  chttp_requests_test_close_socket(accepted);
}

static chttp_client_config chttp_requests_test_config(void) {
  const chttp_client_config config = {
      .network = {.backend =
#if defined(_WIN32)
                      NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                      NATIVE_IO_BACKEND_EPOLL,
#else
                      NATIVE_IO_BACKEND_KQUEUE,
#endif
                  .connection_capacity = 2u,
                  .command_capacity = 8u,
                  .request_capacity = 4u,
                  .completion_batch_capacity = 4u,
                  .event_capacity = 8u,
                  .max_send_bytes = 2048u,
                  .receive_buffer_bytes = 32u,
                  .connect_timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS,
                  .read_timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS,
                  .write_timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS},
      .request_capacity = 1u,
      .max_start_line_bytes = 256u,
      .max_header_count = 16u,
      .max_header_bytes = 1024u,
      .max_request_body_bytes = 512u,
      .max_response_body_bytes = 512u,
      .max_informational_responses = 2u};
  return config;
}

static void chttp_requests_test_round_trip(chttp_client *client,
                                           const chttp_requests_test_case *test_case) {
  chttp_requests_test_socket listener = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
  chttp_requests_test_server server = {0};
  turbo_thread_t thread = NULL;
  chttp_response response = {0};
  chttp_error error = {0};
  chttp_options options;
  char uri[64];
  char authority[64];
  char expected[1024];
  uint16_t port = 0u;
  int expected_size;

  check_not_null(client);
  check_not_null(test_case);
  check_equal(chttp_requests_test_listener(&listener, &port), TURBO_OK);
  check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
  check_greater(snprintf(authority, sizeof(authority), "127.0.0.1:%u", (unsigned int)port), 0);
  expected_size = snprintf(expected, sizeof(expected),
                           "%s %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n"
                           "%s",
                           test_case->method, test_case->target, authority,
                           strlen(test_case->request_body), test_case->request_body);
  check_true(expected_size > 0 && (size_t)expected_size < sizeof(expected));
  server = (chttp_requests_test_server){.listener = listener,
                                        .expected = expected,
                                        .expected_size = (size_t)expected_size,
                                        .response = test_case->wire_response,
                                        .response_size = test_case->wire_response_size};
  check_equal(turbo_thread_create(&thread, chttp_requests_test_serve, &server), TURBO_OK);
  options = (chttp_options){.connection_uri = uri,
                            .authority = authority,
                            .target = test_case->target,
                            .body = test_case->request_body,
                            .body_size = strlen(test_case->request_body),
                            .timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS};
  check_equal(test_case->call(client, &options, &response, &error), TURBO_OK);
  check_equal(turbo_thread_join(&thread), TURBO_OK);
  turbo_thread_destroy(&thread);
  check_equal(server.status, TURBO_OK);
  check_equal(response.status_code, test_case->status_code);
  check_equal(response.reason, test_case->reason);
  check_equal(chttp_response_header(&response, "x-mode"), "requests");
  check_equal(response.body_size, test_case->response_body_size);
  if (test_case->response_body_size == 0u) {
    check_null(response.body);
  } else {
    check_equal(response.body, test_case->response_body, test_case->response_body_size);
  }
  chttp_response_destroy(&response);
  chttp_requests_test_close_socket(listener);
}

spec("CHTTP requests-style client") {
  it("reuses one same-origin connection for sequential targets") {
    chttp_client client = {0};
    chttp_client_config config = chttp_requests_test_config();
    chttp_requests_test_socket listener = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
    chttp_requests_test_keep_alive_server server = {0};
    turbo_thread_t thread = NULL;
    chttp_response response = {0};
    chttp_error error = {0};
    chttp_options options;
    char uri[64];
    char authority[64];
    char first_expected[512];
    char second_expected[512];
    uint16_t port = 0u;
    int first_expected_size;
    int second_expected_size;

    check_equal(chttp_client_init(&client, &config), TURBO_OK);
    check_equal(chttp_requests_test_listener(&listener, &port), TURBO_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    check_greater(snprintf(authority, sizeof(authority), "127.0.0.1:%u", (unsigned int)port), 0);
    first_expected_size = snprintf(first_expected, sizeof(first_expected),
                                   "GET /first HTTP/1.1\r\n"
                                   "Host: %s\r\n"
                                   "Content-Length: 0\r\n"
                                   "Connection: keep-alive\r\n"
                                   "\r\n",
                                   authority);
    second_expected_size = snprintf(second_expected, sizeof(second_expected),
                                    "GET /second HTTP/1.1\r\n"
                                    "Host: %s\r\n"
                                    "Content-Length: 0\r\n"
                                    "Connection: keep-alive\r\n"
                                    "\r\n",
                                    authority);
    check_true(first_expected_size > 0 && (size_t)first_expected_size < sizeof(first_expected));
    check_true(second_expected_size > 0 && (size_t)second_expected_size < sizeof(second_expected));
    server = (chttp_requests_test_keep_alive_server){
        .listener = listener,
        .first_expected = first_expected,
        .first_expected_size = (size_t)first_expected_size,
        .second_expected = second_expected,
        .second_expected_size = (size_t)second_expected_size,
        .first_response = chttp_requests_test_keep_alive_response,
        .first_response_size = sizeof(chttp_requests_test_keep_alive_response) - 1u,
        .second_response = chttp_requests_test_close_response,
        .second_response_size = sizeof(chttp_requests_test_close_response) - 1u};
    check_equal(turbo_thread_create(&thread, chttp_requests_test_serve_keep_alive, &server),
                TURBO_OK);
    options = (chttp_options){.connection_uri = uri,
                              .authority = authority,
                              .target = "/first",
                              .timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS};
    check_equal(chttp_get(&client, &options, &response, &error), TURBO_OK);
    check_equal(response.body_size, (size_t)3u);
    check_equal(response.body, "one", 3u);
    check_equal(response.protocol_keep_alive, 1);
    chttp_response_destroy(&response);

    options.target = "/second";
    check_equal(chttp_get(&client, &options, &response, &error), TURBO_OK);
    check_equal(response.body_size, (size_t)3u);
    check_equal(response.body, "two", 3u);
    check_equal(response.protocol_keep_alive, 0);
    chttp_response_destroy(&response);

    check_equal(chttp_client_destroy(&client, CHTTP_REQUESTS_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(turbo_thread_join(&thread), TURBO_OK);
    turbo_thread_destroy(&thread);
    check_equal(server.status, TURBO_OK);
    chttp_requests_test_close_socket(listener);
  }

  it("evicts an idle origin without exposing poll to a blocking caller") {
    chttp_client client = {0};
    chttp_client_config config = chttp_requests_test_config();
    chttp_requests_test_socket listener = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
    chttp_requests_test_keep_alive_server server = {0};
    turbo_thread_t thread = NULL;
    chttp_response response = {0};
    chttp_error error = {0};
    chttp_options options;
    char uri[64];
    char first_expected[512];
    char second_expected[512];
    uint16_t port = 0u;
    int first_expected_size;
    int second_expected_size;

    check_equal(chttp_client_init(&client, &config), TURBO_OK);
    check_equal(chttp_requests_test_listener(&listener, &port), TURBO_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    first_expected_size = snprintf(first_expected, sizeof(first_expected),
                                   "GET /first HTTP/1.1\r\n"
                                   "Host: first.test\r\n"
                                   "Content-Length: 0\r\n"
                                   "Connection: keep-alive\r\n"
                                   "\r\n");
    second_expected_size = snprintf(second_expected, sizeof(second_expected),
                                    "GET /second HTTP/1.1\r\n"
                                    "Host: second.test\r\n"
                                    "Content-Length: 0\r\n"
                                    "Connection: keep-alive\r\n"
                                    "\r\n");
    check_true(first_expected_size > 0 && (size_t)first_expected_size < sizeof(first_expected));
    check_true(second_expected_size > 0 && (size_t)second_expected_size < sizeof(second_expected));
    server = (chttp_requests_test_keep_alive_server){
        .listener = listener,
        .first_expected = first_expected,
        .first_expected_size = (size_t)first_expected_size,
        .second_expected = second_expected,
        .second_expected_size = (size_t)second_expected_size,
        .first_response = chttp_requests_test_keep_alive_response,
        .first_response_size = sizeof(chttp_requests_test_keep_alive_response) - 1u,
        .second_response = chttp_requests_test_close_response,
        .second_response_size = sizeof(chttp_requests_test_close_response) - 1u};
    check_equal(turbo_thread_create(&thread, chttp_requests_test_serve_origin_switch, &server),
                TURBO_OK);

    options = (chttp_options){.connection_uri = uri,
                              .authority = "first.test",
                              .target = "/first",
                              .timeout_ms = CHTTP_REQUESTS_TEST_TIMEOUT_MS};
    check_equal(chttp_get(&client, &options, &response, &error), TURBO_OK);
    chttp_response_destroy(&response);

    options.authority = "second.test";
    options.target = "/second";
    check_equal(chttp_get(&client, &options, &response, &error), TURBO_OK);
    check_equal(response.body_size, (size_t)3u);
    check_equal(response.body, "two", 3u);
    chttp_response_destroy(&response);

    check_equal(chttp_client_destroy(&client, CHTTP_REQUESTS_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(turbo_thread_join(&thread), TURBO_OK);
    turbo_thread_destroy(&thread);
    check_equal(server.status, TURBO_OK);
    chttp_requests_test_close_socket(listener);
  }

  it("recovers the client after a blocking deadline") {
    chttp_client client = {0};
    chttp_client_config config = chttp_requests_test_config();
    static const chttp_requests_test_case recovery_case = {
        .method = "GET",
        .target = "/after-timeout",
        .request_body = "",
        .call = chttp_get,
        .wire_response = chttp_requests_test_response,
        .wire_response_size = sizeof(chttp_requests_test_response) - 1u,
        .status_code = 201u,
        .reason = "Created",
        .response_body = "ok",
        .response_body_size = 2u};

    check_equal(chttp_client_init(&client, &config), TURBO_OK);
    {
      chttp_requests_test_socket listener = CHTTP_REQUESTS_TEST_INVALID_SOCKET;
      chttp_requests_test_server server = {0};
      turbo_thread_t thread = NULL;
      chttp_response response_value = {0};
      chttp_error error = {0};
      chttp_options options;
      char uri[64];
      char authority[64];
      char expected[512];
      uint16_t port = 0u;
      int expected_size;

      check_equal(chttp_requests_test_listener(&listener, &port), TURBO_OK);
      check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
      check_greater(snprintf(authority, sizeof(authority), "127.0.0.1:%u", (unsigned int)port), 0);
      expected_size = snprintf(expected, sizeof(expected),
                               "GET /timeout HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: keep-alive\r\n"
                               "\r\n",
                               authority);
      check_true(expected_size > 0 && (size_t)expected_size < sizeof(expected));
      server =
          (chttp_requests_test_server){.listener = listener,
                                       .expected = expected,
                                       .expected_size = (size_t)expected_size,
                                       .hold_after_receive_ms = CHTTP_REQUESTS_TEST_SERVER_HOLD_MS};
      check_equal(turbo_thread_create(&thread, chttp_requests_test_serve, &server), TURBO_OK);
      options = (chttp_options){.connection_uri = uri,
                                .authority = authority,
                                .target = "/timeout",
                                .timeout_ms = CHTTP_REQUESTS_TEST_DEADLINE_MS};
      check_equal(chttp_get(&client, &options, &response_value, &error), TURBO_ETIMEDOUT);
      check_equal(error.status, TURBO_ETIMEDOUT);
      check_null(response_value.body);
      check_equal(turbo_thread_join(&thread), TURBO_OK);
      turbo_thread_destroy(&thread);
      check_equal(server.status, TURBO_OK);
      chttp_requests_test_close_socket(listener);
    }
    chttp_requests_test_round_trip(&client, &recovery_case);
    check_equal(chttp_client_destroy(&client, CHTTP_REQUESTS_TEST_TIMEOUT_MS), TURBO_OK);
    check_null(client.impl);
  }

  it("performs GET POST PUT DELETE and PATCH without caller polling") {
    static const chttp_requests_test_case cases[] = {
        {.method = "GET",
         .target = "/get",
         .request_body = "",
         .call = chttp_get,
         .wire_response = chttp_requests_test_response,
         .wire_response_size = sizeof(chttp_requests_test_response) - 1u,
         .status_code = 201u,
         .reason = "Created",
         .response_body = "ok",
         .response_body_size = 2u},
        {.method = "POST",
         .target = "/post",
         .request_body = "alpha",
         .call = chttp_post,
         .wire_response = chttp_requests_test_response,
         .wire_response_size = sizeof(chttp_requests_test_response) - 1u,
         .status_code = 201u,
         .reason = "Created",
         .response_body = "ok",
         .response_body_size = 2u},
        {.method = "PUT",
         .target = "/put",
         .request_body = "beta",
         .call = chttp_put,
         .wire_response = chttp_requests_test_response,
         .wire_response_size = sizeof(chttp_requests_test_response) - 1u,
         .status_code = 201u,
         .reason = "Created",
         .response_body = "ok",
         .response_body_size = 2u},
        {.method = "DELETE",
         .target = "/delete",
         .request_body = "",
         .call = chttp_delete,
         .wire_response = chttp_requests_test_response,
         .wire_response_size = sizeof(chttp_requests_test_response) - 1u,
         .status_code = 201u,
         .reason = "Created",
         .response_body = "ok",
         .response_body_size = 2u},
        {.method = "PATCH",
         .target = "/patch",
         .request_body = "gamma",
         .call = chttp_patch,
         .wire_response = chttp_requests_test_response,
         .wire_response_size = sizeof(chttp_requests_test_response) - 1u,
         .status_code = 201u,
         .reason = "Created",
         .response_body = "ok",
         .response_body_size = 2u}};
    chttp_client client = {0};
    chttp_client_config config = chttp_requests_test_config();
    size_t index;

    check_equal(chttp_client_init(&client, &config), TURBO_OK);
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      chttp_requests_test_round_trip(&client, &cases[index]);
    }
    check_equal(chttp_client_destroy(&client, CHTTP_REQUESTS_TEST_TIMEOUT_MS), TURBO_OK);
    check_null(client.impl);
  }

  it("performs HEAD without returning a response body") {
    static const chttp_requests_test_case head_case = {
        .method = "HEAD",
        .target = "/head",
        .request_body = "",
        .call = chttp_head,
        .wire_response = chttp_requests_test_head_response,
        .wire_response_size = sizeof(chttp_requests_test_head_response) - 1u,
        .status_code = 200u,
        .reason = "OK",
        .response_body = NULL,
        .response_body_size = 0u};
    chttp_client client = {0};
    chttp_client_config config = chttp_requests_test_config();

    check_equal(chttp_client_init(&client, &config), TURBO_OK);
    chttp_requests_test_round_trip(&client, &head_case);
    check_equal(chttp_client_destroy(&client, CHTTP_REQUESTS_TEST_TIMEOUT_MS), TURBO_OK);
    check_null(client.impl);
  }
}
