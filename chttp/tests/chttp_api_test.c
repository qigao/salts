#include "tinytest.h"
#include <chttp/chttp.h>

#include <turbo/clock.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET chttp_test_socket;
  #define CHTTP_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int chttp_test_socket;
  #define CHTTP_TEST_INVALID_SOCKET (-1)
#endif

enum { CHTTP_TEST_TIMEOUT_MS = 5000 };

typedef struct chttp_test_probe {
  int called;
  int status;
  unsigned int response_status;
  char reason[32];
  char content_type[64];
  unsigned char body[128];
  size_t body_size;
} chttp_test_probe;

static void chttp_test_close_socket(chttp_test_socket socket_value) {
  if (socket_value == CHTTP_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int chttp_test_set_timeout(chttp_test_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = CHTTP_TEST_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                    (int)sizeof(timeout_ms)) == 0
             ? TURBO_OK
             : TURBO_EIO;
#else
  const struct timeval timeout = {CHTTP_TEST_TIMEOUT_MS / 1000,
                                  (CHTTP_TEST_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout)) ==
                 0
             ? TURBO_OK
             : TURBO_EIO;
#endif
}

static int chttp_test_listener(chttp_test_socket *out_listener, uint16_t *out_port) {
  struct sockaddr_in address;
#if defined(_WIN32)
  int length = (int)sizeof(address);
#else
  socklen_t length = (socklen_t)sizeof(address);
#endif
  if (out_listener == NULL || out_port == NULL) return TURBO_EINVAL;
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CHTTP_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)&address, (int)sizeof(address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)&address, &length) != 0 ||
      listen(*out_listener, 1) != 0) {
    chttp_test_close_socket(*out_listener);
    *out_listener = CHTTP_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  *out_port = ntohs(address.sin_port);
  return TURBO_OK;
}

static int chttp_test_recv_all(chttp_test_socket socket_value, void *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const int received = recv(socket_value, (char *)data + offset, (int)(size - offset), 0);
    if (received <= 0) return TURBO_EIO;
    offset += (size_t)received;
  }
  return TURBO_OK;
}

static int chttp_test_send_all(chttp_test_socket socket_value, const void *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const int sent = send(socket_value, (const char *)data + offset, (int)(size - offset), 0);
    if (sent <= 0) return TURBO_EIO;
    offset += (size_t)sent;
  }
  return TURBO_OK;
}

static void chttp_test_complete(void *user, chttp_request request,
                                const chttp_response_view *response, const chttp_error *error) {
  chttp_test_probe *probe = (chttp_test_probe *)user;
  const char *content_type;
  (void)request;
  ++probe->called;
  if (error != NULL) {
    probe->status = error->status;
    return;
  }
  probe->status = TURBO_OK;
  probe->response_status = response->status_code;
  (void)snprintf(probe->reason, sizeof(probe->reason), "%s", response->reason);
  content_type = chttp_response_view_header(response, "Content-Type");
  if (content_type != NULL)
    (void)snprintf(probe->content_type, sizeof(probe->content_type), "%s", content_type);
  probe->body_size = response->body_size;
  if (probe->body_size <= sizeof(probe->body))
    memcpy(probe->body, response->body, probe->body_size);
  else probe->status = TURBO_EMSGSIZE;
}

static chttp_client_config chttp_test_config(void) {
  const chttp_client_config config = {.network = {.backend =
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
                                                  .max_send_bytes = 1024u,
                                                  .receive_buffer_bytes = 32u,
                                                  .connect_timeout_ms = CHTTP_TEST_TIMEOUT_MS,
                                                  .read_timeout_ms = CHTTP_TEST_TIMEOUT_MS,
                                                  .write_timeout_ms = CHTTP_TEST_TIMEOUT_MS},
                                      .request_capacity = 2u,
                                      .max_start_line_bytes = 256u,
                                      .max_header_count = 16u,
                                      .max_header_bytes = 512u,
                                      .max_request_body_bytes = 128u,
                                      .max_response_body_bytes = 128u,
                                      .max_informational_responses = 4u};
  return config;
}

static int chttp_test_poll_until(chttp_async_client *client, chttp_test_probe *probe) {
  const uint64_t deadline = turbo_monotonic_ms() + CHTTP_TEST_TIMEOUT_MS;
  while (probe->called == 0) {
    size_t completions = 0u;
    const int status = chttp_async_client_poll(client, 5u, &completions);
    if (status != TURBO_OK) return status;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
  }
  return TURBO_OK;
}

spec("CHTTP advanced async client API") {
  it("round-trips one bounded HTTP response over CNet") {
    static const char response[] = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 11\r\n"
                                   "Connection: close\r\n"
                                   "\r\n"
                                   "hello world";
    const chttp_header headers[] = {{"Accept", "text/plain"}};
    chttp_async_client client = {0};
    chttp_client_config config = chttp_test_config();
    chttp_test_probe probe = {0};
    chttp_test_socket listener = CHTTP_TEST_INVALID_SOCKET;
    chttp_test_socket accepted = CHTTP_TEST_INVALID_SOCKET;
    chttp_request request = {0};
    chttp_request_options options;
    char uri[64];
    char authority[64];
    char expected[512];
    unsigned char received[512];
    uint16_t port = 0u;
    int expected_size;
    size_t completions = 0u;

    check_equal(chttp_async_client_init(&client, &config), TURBO_OK);
    check_equal(chttp_test_listener(&listener, &port), TURBO_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    check_greater(snprintf(authority, sizeof(authority), "127.0.0.1:%u", (unsigned int)port), 0);
    options = (chttp_request_options){.connection_uri = uri,
                                      .authority = authority,
                                      .target = "/hello?x=1",
                                      .method = CHTTP_METHOD_GET,
                                      .headers = headers,
                                      .header_count = 1u,
                                      .on_complete = chttp_test_complete,
                                      .user = &probe};
    check_equal(chttp_async_client_submit(&client, &options, &request), TURBO_OK);
    check_true(request.slot != 0u && request.generation != 0u);
    check_equal(chttp_async_client_poll(&client, CHTTP_TEST_TIMEOUT_MS, &completions), TURBO_OK);
    check_equal(completions, (size_t)0u);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CHTTP_TEST_INVALID_SOCKET);
    check_equal(chttp_test_set_timeout(accepted), TURBO_OK);
    check_equal(chttp_async_client_poll(&client, 10u, &completions), TURBO_OK);

    expected_size = snprintf(expected, sizeof(expected),
                             "GET /hello?x=1 HTTP/1.1\r\n"
                             "Host: %s\r\n"
                             "Content-Length: 0\r\n"
                             "Connection: keep-alive\r\n"
                             "Accept: text/plain\r\n"
                             "\r\n",
                             authority);
    check_true(expected_size > 0 && (size_t)expected_size < sizeof(expected));
    check_equal(chttp_test_recv_all(accepted, received, (size_t)expected_size), TURBO_OK);
    check_equal(received, expected, (size_t)expected_size);

    check_equal(chttp_test_send_all(accepted, response, 19u), TURBO_OK);
    check_equal(chttp_async_client_poll(&client, 5u, &completions), TURBO_OK);
    check_equal(chttp_test_send_all(accepted, response + 19u, sizeof(response) - 1u - 19u),
                TURBO_OK);
    check_equal(chttp_test_poll_until(&client, &probe), TURBO_OK);
    check_equal(probe.called, 1);
    check_equal(probe.status, TURBO_OK);
    check_equal(probe.response_status, 200u);
    check_equal(probe.reason, "OK");
    check_equal(probe.content_type, "text/plain");
    check_equal(probe.body_size, (size_t)11u);
    check_equal(probe.body, "hello world", 11u);

    check_equal(chttp_async_client_stop(&client, CHTTP_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(chttp_async_client_destroy(&client), TURBO_OK);
    chttp_test_close_socket(accepted);
    chttp_test_close_socket(listener);
  }

  it("rejects datagram transport before admission") {
    chttp_async_client client = {0};
    chttp_client_config config = chttp_test_config();
    chttp_test_probe probe = {0};
    chttp_request request = {7u, 9u};
    const chttp_request_options options = {.connection_uri = "udp://127.0.0.1:9000",
                                           .authority = "127.0.0.1:9000",
                                           .target = "/",
                                           .method = CHTTP_METHOD_GET,
                                           .on_complete = chttp_test_complete,
                                           .user = &probe};

    check_equal(chttp_async_client_init(&client, &config), TURBO_OK);
    check_equal(chttp_async_client_submit(&client, &options, &request), TURBO_ENOTSUP);
    check_equal(request.slot, 0u);
    check_equal(request.generation, 0u);
    check_equal(probe.called, 0);
    check_equal(chttp_async_client_stop(&client, CHTTP_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(chttp_async_client_destroy(&client), TURBO_OK);
  }

  it("keeps a canceled request until its terminal callback") {
    chttp_async_client client = {0};
    chttp_client_config config = chttp_test_config();
    chttp_test_probe probe = {0};
    chttp_test_socket listener = CHTTP_TEST_INVALID_SOCKET;
    chttp_request request = {0};
    chttp_request_options options;
    char uri[64];
    char authority[64];
    uint16_t port = 0u;

    check_equal(chttp_async_client_init(&client, &config), TURBO_OK);
    check_equal(chttp_test_listener(&listener, &port), TURBO_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    check_greater(snprintf(authority, sizeof(authority), "127.0.0.1:%u", (unsigned int)port), 0);
    options = (chttp_request_options){.connection_uri = uri,
                                      .authority = authority,
                                      .target = "/cancel",
                                      .method = CHTTP_METHOD_GET,
                                      .on_complete = chttp_test_complete,
                                      .user = &probe};
    check_equal(chttp_async_client_submit(&client, &options, &request), TURBO_OK);
    check_equal(chttp_async_request_cancel(&client, request), TURBO_OK);
    check_equal(chttp_async_request_cancel(&client, request), TURBO_EALREADY);
    check_equal(chttp_test_poll_until(&client, &probe), TURBO_OK);
    check_equal(probe.called, 1);
    check_equal(probe.status, TURBO_ECANCELED);
    check_equal(chttp_async_request_cancel(&client, request), TURBO_ENOENT);
    check_equal(chttp_async_client_stop(&client, CHTTP_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(chttp_async_client_destroy(&client), TURBO_OK);
    chttp_test_close_socket(listener);
  }
}
