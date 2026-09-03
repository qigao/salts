#include "tinytest.h"
#include <chttp/chttp.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET chttp_server_test_socket;
  #define CHTTP_SERVER_TEST_INVALID_SOCKET INVALID_SOCKET
  #define chttp_server_test_close_socket closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int chttp_server_test_socket;
  #define CHTTP_SERVER_TEST_INVALID_SOCKET (-1)
  #define chttp_server_test_close_socket close
#endif

enum {
  CHTTP_SERVER_TEST_TIMEOUT_MS = 5000,
  CHTTP_SERVER_TEST_STOP_TIMEOUT_MS = 20,
  CHTTP_SERVER_TEST_RAW_BYTES = 8192,
  CHTTP_SERVER_TEST_LARGE_BODY_BYTES = 512,
  CHTTP_SERVER_TEST_PRESSURE_CLIENTS = 4,
  CHTTP_SERVER_TEST_ADMISSION_ATTEMPT_MS = 100
};

typedef struct chttp_server_test_probe {
  char order[32];
  size_t order_size;
} chttp_server_test_probe;

typedef struct chttp_server_test_blocking_probe {
  atomic_int entered;
  atomic_int release;
} chttp_server_test_blocking_probe;

typedef struct chttp_server_test_raw_client {
  uint16_t port;
  const char *request;
  char response[CHTTP_SERVER_TEST_RAW_BYTES];
  size_t response_size;
  int status;
} chttp_server_test_raw_client;

typedef struct chttp_server_test_large_probe {
  atomic_int calls;
} chttp_server_test_large_probe;

typedef struct chttp_server_test_deferred_probe {
  chttp_server_deferred handle;
  atomic_int acquired;
} chttp_server_test_deferred_probe;

typedef struct chttp_server_stream_probe {
  unsigned char data[64];
  size_t size;
  size_t writes;
  size_t opens;
  size_t closes;
  int close_status;
  int handler_called;
  int fail_write;
} chttp_server_stream_probe;

typedef struct chttp_server_response_source_probe {
  const unsigned char *data;
  size_t size;
  size_t offset;
  size_t chunk_size;
  size_t calls;
  int content_length_known;
  int fail_read;
} chttp_server_response_source_probe;

static int chttp_server_stream_write(void *user, const void *data, size_t size) {
  chttp_server_stream_probe *probe = (chttp_server_stream_probe *)user;
  if (probe == NULL || (data == NULL && size != 0u) || size > sizeof(probe->data) - probe->size)
    return SALTS_EMSGSIZE;
  ++probe->writes;
  if (probe->fail_write) return SALTS_EIO;
  memcpy(probe->data + probe->size, data, size);
  probe->size += size;
  return SALTS_OK;
}

static int chttp_server_stream_open(void *user, const chttp_server_request_view *request,
                                    chttp_body_sink *out_sink) {
  chttp_server_stream_probe *probe = (chttp_server_stream_probe *)user;
  if (probe == NULL || request == NULL || out_sink == NULL ||
      strcmp(request->path, "/stream-upload") != 0)
    return SALTS_EINVAL;
  ++probe->opens;
  *out_sink = (chttp_body_sink){.write = chttp_server_stream_write, .user = probe};
  return SALTS_OK;
}

static void chttp_server_stream_close(void *user, chttp_body_sink *sink, int status) {
  chttp_server_stream_probe *probe = (chttp_server_stream_probe *)user;
  if (probe == NULL || sink == NULL) return;
  ++probe->closes;
  probe->close_status = status;
}

static int chttp_server_stream_handler(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response) {
  chttp_server_stream_probe *probe = (chttp_server_stream_probe *)user;
  if (probe == NULL || request == NULL || !request->body_streamed || request->body != NULL ||
      request->body_size != probe->size || probe->closes != 1u || probe->close_status != SALTS_OK)
    return SALTS_EPROTO;
  probe->handler_called = 1;
  return chttp_server_reply(response, 200u, "text/plain", "ok", 2u);
}

static int chttp_server_response_source_read(void *user, void *buffer, size_t capacity,
                                             size_t *out_size) {
  chttp_server_response_source_probe *probe = (chttp_server_response_source_probe *)user;
  size_t size;
  if (probe == NULL || buffer == NULL || capacity == 0u || out_size == NULL) return SALTS_EINVAL;
  ++probe->calls;
  if (probe->fail_read) return SALTS_EIO;
  if (probe->offset == probe->size) {
    *out_size = 0u;
    return SALTS_OK;
  }
  size = probe->size - probe->offset;
  if (size > probe->chunk_size) size = probe->chunk_size;
  if (size > capacity) size = capacity;
  memcpy(buffer, probe->data + probe->offset, size);
  probe->offset += size;
  *out_size = size;
  return SALTS_OK;
}

static int chttp_server_response_source_handler(void *user,
                                                const chttp_server_request_view *request,
                                                chttp_server_response *response) {
  chttp_server_response_source_probe *probe = (chttp_server_response_source_probe *)user;
  const chttp_body_source source = {.read = chttp_server_response_source_read,
                                    .user = probe,
                                    .content_length = probe == NULL ? 0u : probe->size,
                                    .content_length_known =
                                        probe == NULL ? 0 : probe->content_length_known};
  (void)request;
  return chttp_server_response_source(response, 200u, "application/octet-stream", &source);
}

static int chttp_server_response_file_handler(void *user, const chttp_server_request_view *request,
                                              chttp_server_response *response) {
  (void)request;
  return chttp_server_response_file(response, 200u, "application/octet-stream", (const char *)user);
}

static int chttp_server_test_socket_timeout(chttp_server_test_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                    sizeof(timeout_ms)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#else
  const struct timeval timeout = {CHTTP_SERVER_TEST_TIMEOUT_MS / 1000,
                                  (CHTTP_SERVER_TEST_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#endif
}

static int chttp_server_test_raw_connect(uint16_t port, chttp_server_test_socket *out_socket) {
  struct sockaddr_in address;
  chttp_server_test_socket socket_value;
  if (out_socket == NULL || port == 0u) return SALTS_EINVAL;
  *out_socket = CHTTP_SERVER_TEST_INVALID_SOCKET;
  socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_value == CHTTP_SERVER_TEST_INVALID_SOCKET) return SALTS_EIO;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (chttp_server_test_socket_timeout(socket_value) != SALTS_OK ||
      connect(socket_value, (const struct sockaddr *)&address, sizeof(address)) != 0) {
    chttp_server_test_close_socket(socket_value);
    return SALTS_EIO;
  }
  *out_socket = socket_value;
  return SALTS_OK;
}

static int chttp_server_test_raw_send(chttp_server_test_socket socket_value, const void *data,
                                      size_t size) {
  const unsigned char *cursor = (const unsigned char *)data;
  size_t sent = 0u;
  if (data == NULL || size == 0u) return SALTS_EINVAL;
  while (sent < size) {
    const size_t remaining = size - sent;
    const int chunk = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
    const int result = send(socket_value, (const char *)cursor + sent, chunk, 0);
    if (result <= 0) return SALTS_EIO;
    sent += (size_t)result;
  }
  return SALTS_OK;
}

static int chttp_server_test_raw_receive(chttp_server_test_socket socket_value, char *response,
                                         size_t capacity, size_t *inout_size, bool until_close,
                                         const char *marker) {
  if (response == NULL || capacity == 0u || inout_size == NULL) return SALTS_EINVAL;
  while (*inout_size + 1u < capacity) {
    const int result =
        recv(socket_value, response + *inout_size, (int)(capacity - *inout_size - 1u), 0);
    if (result < 0) return SALTS_EIO;
    if (result == 0) {
      response[*inout_size] = '\0';
      return until_close ? SALTS_OK : SALTS_EOF;
    }
    *inout_size += (size_t)result;
    response[*inout_size] = '\0';
    if (!until_close && marker != NULL && strstr(response, marker) != NULL) return SALTS_OK;
  }
  return SALTS_EMSGSIZE;
}

static int chttp_server_test_raw_exchange(uint16_t port, const char *request, char *response,
                                          size_t capacity, size_t *out_size) {
  chttp_server_test_socket socket_value = CHTTP_SERVER_TEST_INVALID_SOCKET;
  int status;
  if (request == NULL || response == NULL || out_size == NULL) return SALTS_EINVAL;
  *out_size = 0u;
  status = chttp_server_test_raw_connect(port, &socket_value);
  if (status == SALTS_OK)
    status = chttp_server_test_raw_send(socket_value, request, strlen(request));
  if (status == SALTS_OK)
    status = chttp_server_test_raw_receive(socket_value, response, capacity, out_size, true, NULL);
  if (socket_value != CHTTP_SERVER_TEST_INVALID_SOCKET)
    chttp_server_test_close_socket(socket_value);
  return status;
}

static size_t chttp_server_test_count(const char *text, const char *needle) {
  size_t count = 0u;
  size_t needle_size;
  if (text == NULL || needle == NULL || needle[0] == '\0') return 0u;
  needle_size = strlen(needle);
  while ((text = strstr(text, needle)) != NULL) {
    ++count;
    text += needle_size;
  }
  return count;
}

static int chttp_server_test_wait_active(chttp_server *server, uint64_t expected,
                                         uint32_t timeout_ms) {
  const uint64_t deadline = salts_monotonic_ms() + timeout_ms;
  for (;;) {
    chttp_server_stats stats = {0};
    const int status = chttp_server_get_stats(server, &stats);
    if (status != SALTS_OK) return status;
    if (stats.active_connections == expected) return SALTS_OK;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
    salts_thread_yield();
  }
}

static native_io_backend_kind chttp_server_test_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config chttp_server_test_network(size_t connections) {
  const cnet_client_config config = {.backend = chttp_server_test_backend(),
                                     .connection_capacity = connections,
                                     .command_capacity = 16u,
                                     .request_capacity = 8u,
                                     .completion_batch_capacity = 8u,
                                     .event_capacity = 16u,
                                     .max_send_bytes = 4096u,
                                     .receive_buffer_bytes = 256u,
                                     .connect_timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS,
                                     .read_timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS,
                                     .write_timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS};
  return config;
}

static chttp_server_config chttp_server_test_config(void) {
  const chttp_server_config config = {.host = "127.0.0.1",
                                      .port = 0u,
                                      .backlog = 8u,
                                      .network = chttp_server_test_network(4u),
                                      .route_capacity = 8u,
                                      .middleware_capacity = 4u,
                                      .max_route_middleware_count = 4u,
                                      .max_route_param_count = 4u,
                                      .max_route_param_bytes = 128u,
                                      .max_target_bytes = 256u,
                                      .max_header_count = 16u,
                                      .max_header_bytes = 1024u,
                                      .max_request_body_bytes = 512u,
                                      .max_response_header_count = 16u,
                                      .max_response_header_bytes = 1024u,
                                      .max_response_body_bytes = 512u,
                                      .session_capacity = 4u,
                                      .session_entry_capacity = 4u,
                                      .max_session_key_bytes = 32u,
                                      .max_session_value_bytes = 64u,
                                      .session_idle_timeout_ms = 60000u,
                                      .session_cookie_name = "castle_sid",
                                      .session_cookie_secure = 0,
                                      .poll_slice_ms = 2u};
  return config;
}

static chttp_client_config chttp_server_test_client_config(void) {
  const chttp_client_config config = {.network = chttp_server_test_network(2u),
                                      .request_capacity = 1u,
                                      .max_start_line_bytes = 256u,
                                      .max_header_count = 16u,
                                      .max_header_bytes = 1024u,
                                      .max_request_body_bytes = 512u,
                                      .max_response_body_bytes = 512u,
                                      .max_informational_responses = 2u};
  return config;
}

static int chttp_server_test_mark(chttp_server_test_probe *probe, char mark) {
  if (probe == NULL || probe->order_size + 1u >= sizeof(probe->order)) return SALTS_ENOBUFS;
  probe->order[probe->order_size++] = mark;
  probe->order[probe->order_size] = '\0';
  return SALTS_OK;
}

static int chttp_server_test_global(void *user, const chttp_server_request_view *request,
                                    chttp_server_response *response, chttp_server_next *next) {
  int status;
  (void)request;
  status = chttp_server_test_mark((chttp_server_test_probe *)user, 'G');
  if (status != SALTS_OK) return status;
  status = chttp_server_response_set_header(response, "X-Global", "yes");
  return status == SALTS_OK ? chttp_server_next_call(next) : status;
}

static int chttp_server_test_route_middleware(void *user, const chttp_server_request_view *request,
                                              chttp_server_response *response,
                                              chttp_server_next *next) {
  int status;
  (void)request;
  status = chttp_server_test_mark((chttp_server_test_probe *)user, 'R');
  if (status != SALTS_OK) return status;
  status = chttp_server_response_set_header(response, "X-Route", "yes");
  return status == SALTS_OK ? chttp_server_next_call(next) : status;
}

static int chttp_server_test_user(void *user, const chttp_server_request_view *request,
                                  chttp_server_response *response) {
  chttp_server_test_probe *probe = (chttp_server_test_probe *)user;
  const char *name = chttp_server_request_param(request, "name");
  const char *visits = chttp_session_get(request->session, "visits");
  char body[64];
  int next_visits = visits == NULL ? 1 : visits[0] - '0' + 1;
  int body_size;
  int status = chttp_server_test_mark(probe, 'H');
  if (status != SALTS_OK) return status;
  if (name == NULL || next_visits < 1 || next_visits > 9) return SALTS_EPROTO;
  body_size = snprintf(body, sizeof(body), "%s:%d", name, next_visits);
  if (body_size < 0 || (size_t)body_size >= sizeof(body)) return SALTS_EMSGSIZE;
  body[body_size - 1] = (char)('0' + next_visits);
  status = chttp_session_set(request->session, "visits", body + body_size - 1);
  if (status != SALTS_OK) return status;
  return chttp_server_reply(response, 200u, "text/plain", body, (size_t)body_size);
}

static int chttp_server_test_static(void *user, const chttp_server_request_view *request,
                                    chttp_server_response *response) {
  (void)user;
  (void)request;
  return chttp_server_reply(response, 200u, "text/plain", "static", 6u);
}

static int chttp_server_test_deferred(void *user, const chttp_server_request_view *request,
                                      chttp_server_response *response) {
  chttp_server_test_deferred_probe *probe = (chttp_server_test_deferred_probe *)user;
  chttp_server_deferred handle = CHTTP_SERVER_DEFERRED_INIT;
  int status;
  (void)request;
  if (probe == NULL) return SALTS_EINVAL;
  status = chttp_server_response_defer(response, &handle);
  if (status != SALTS_OK) return status;
  probe->handle = handle;
  atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return SALTS_OK;
}

static int chttp_server_test_dynamic(void *user, const chttp_server_request_view *request,
                                     chttp_server_response *response) {
  (void)user;
  (void)request;
  return chttp_server_reply(response, 200u, "text/plain", "dynamic", 7u);
}

static int chttp_server_test_logout(void *user, const chttp_server_request_view *request,
                                    chttp_server_response *response) {
  int status;
  (void)user;
  status = chttp_session_invalidate(request->session);
  return status == SALTS_OK ? chttp_server_reply(response, 204u, NULL, NULL, 0u) : status;
}

static int chttp_server_test_echo_body(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response) {
  (void)user;
  return chttp_server_reply(response, 200u, "application/octet-stream", request->body,
                            request->body_size);
}

static int chttp_server_test_blocking(void *user, const chttp_server_request_view *request,
                                      chttp_server_response *response) {
  chttp_server_test_blocking_probe *probe = (chttp_server_test_blocking_probe *)user;
  (void)request;
  atomic_store_explicit(&probe->entered, 1, memory_order_release);
  while (atomic_load_explicit(&probe->release, memory_order_acquire) == 0)
    salts_thread_yield();
  return chttp_server_reply(response, 200u, "text/plain", "released", 8u);
}

static int chttp_server_test_large_session(void *user, const chttp_server_request_view *request,
                                           chttp_server_response *response) {
  static const unsigned char body[CHTTP_SERVER_TEST_LARGE_BODY_BYTES] = {1u};
  chttp_server_test_large_probe *probe = (chttp_server_test_large_probe *)user;
  int status;
  atomic_fetch_add_explicit(&probe->calls, 1, memory_order_relaxed);
  status = chttp_session_set(request->session, "committed", "yes");
  return status == SALTS_OK
             ? chttp_server_reply(response, 200u, "application/octet-stream", body, sizeof(body))
             : status;
}

static void chttp_server_test_raw_client_entry(void *user) {
  chttp_server_test_raw_client *client = (chttp_server_test_raw_client *)user;
  client->status = chttp_server_test_raw_exchange(client->port, client->request, client->response,
                                                  sizeof(client->response), &client->response_size);
}

static int chttp_server_test_next_once(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response, chttp_server_next *next) {
  int status;
  (void)user;
  (void)request;
  status = chttp_server_next_call(next);
  if (status != SALTS_OK) return status;
  if (chttp_server_next_call(next) != SALTS_EALREADY) return SALTS_EPROTO;
  return chttp_server_response_set_header(response, "X-Next-Once", "yes");
}

static int chttp_server_test_call(chttp_client *client, const char *uri, const char *target,
                                  const chttp_header *headers, size_t header_count,
                                  chttp_response *out_response) {
  const chttp_options options = {.connection_uri = uri,
                                 .authority = "127.0.0.1",
                                 .target = target,
                                 .headers = headers,
                                 .header_count = header_count,
                                 .timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS};
  chttp_error error = {0};
  return chttp_get(client, &options, out_response, &error);
}

static int chttp_server_test_method_call(chttp_client *client, const char *uri, const char *target,
                                         chttp_method method, chttp_response *out_response) {
  const chttp_options options = {.connection_uri = uri,
                                 .authority = "127.0.0.1",
                                 .target = target,
                                 .timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS};
  chttp_error error = {0};
  if (method == CHTTP_METHOD_HEAD) return chttp_head(client, &options, out_response, &error);
  if (method == CHTTP_METHOD_POST) return chttp_post(client, &options, out_response, &error);
  return chttp_get(client, &options, out_response, &error);
}

static int chttp_server_test_cookie_header(const chttp_response *response, char *cookie,
                                           size_t cookie_capacity) {
  const char *set_cookie = chttp_response_header(response, "Set-Cookie");
  const char *end;
  size_t size;
  if (set_cookie == NULL || cookie == NULL || cookie_capacity == 0u) return SALTS_EINVAL;
  end = strchr(set_cookie, ';');
  if (end == NULL) return SALTS_EPROTO;
  size = (size_t)(end - set_cookie);
  if (size >= cookie_capacity) return SALTS_EMSGSIZE;
  memcpy(cookie, set_cookie, size);
  cookie[size] = '\0';
  return SALTS_OK;
}

spec("CHTTP background HTTP/1.1 server") {
  it("does not reserve configured payload maxima during initialization") {
    enum { LARGE_PAYLOAD_BYTES = 256u * 1024u * 1024u };
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_stats stats = {0};

    config.network.connection_capacity = 128u;
    config.network.event_capacity = 256u;
    config.network.receive_buffer_bytes = LARGE_PAYLOAD_BYTES;
    config.network.event_buffer_bytes = LARGE_PAYLOAD_BYTES;
    config.network.max_send_bytes = LARGE_PAYLOAD_BYTES + config.max_response_header_bytes + 512u;
    config.max_response_body_bytes = LARGE_PAYLOAD_BYTES;
    config.max_buffered_response_body_bytes = LARGE_PAYLOAD_BYTES;
    config.buffer_capacity_bytes = (size_t)LARGE_PAYLOAD_BYTES * 2u + 4096u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get_stats(&server, &stats), SALTS_OK);
    check_equal(stats.buffer_bytes, (size_t)0u);
    check_equal(stats.peak_buffer_bytes, (size_t)0u);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("accounts live payload buffers and releases them after a closed request") {
    static const char request[] = "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_stats stats = {0};
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    config.buffer_capacity_bytes = 8192u;
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_equal(chttp_server_test_wait_active(&server, 0u, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_get_stats(&server, &stats), SALTS_OK);
    check_equal(stats.buffer_bytes, (size_t)0u);
    check_true(stats.peak_buffer_bytes != 0u);
    check_true(stats.peak_buffer_bytes <= config.buffer_capacity_bytes);
    check_equal(stats.rejected_buffer_allocations, (uint64_t)0u);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("copies CNet socket tuning before server start") {
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_socket_options options = CHTTP_SERVER_SOCKET_OPTIONS_INIT;

    options.stream.receive_buffer_bytes = 32768u;
    options.stream.send_buffer_bytes = 32768u;
    options.stream.keepalive = 1;
    options.stream.linger = 1;
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_set_socket_options(&server, &options), SALTS_OK);
    options.size = 0u;
    check_equal(chttp_server_set_socket_options(&server, &options), SALTS_EINVAL);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("keeps polling and preserves pipelined order while a response is deferred") {
    static const char requests[] =
        "GET /deferred HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
        "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    static const chttp_header response_headers[] = {{"X-Deferred", "yes"}};
    const chttp_server_deferred_response deferred_response = {
        .size = sizeof(chttp_server_deferred_response),
        .status_code = 200u,
        .content_type = "text/plain",
        .headers = response_headers,
        .header_count = sizeof(response_headers) / sizeof(response_headers[0]),
        .body = "delayed",
        .body_size = 7u};
    chttp_server server = {0};
    chttp_client independent_client = {0};
    chttp_server_config server_config = chttp_server_test_config();
    chttp_server_stats deferred_stats = {0};
    chttp_client_config client_config = chttp_server_test_client_config();
    chttp_server_test_deferred_probe probe;
    chttp_server_test_socket pipelined = CHTTP_SERVER_TEST_INVALID_SOCKET;
    chttp_response independent_response = {0};
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    char uri[64];
    size_t response_size = 0u;
    uint64_t deadline;
    uint16_t port = 0u;

    memset(&probe, 0, sizeof(probe));
    atomic_init(&probe.acquired, 0);
    server_config.session_capacity = 0u;
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/deferred", chttp_server_test_deferred, &probe),
                SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_server_test_raw_connect(port, &pipelined), SALTS_OK);
    check_equal(chttp_server_test_raw_send(pipelined, requests, sizeof(requests) - 1u), SALTS_OK);
    deadline = salts_monotonic_ms() + CHTTP_SERVER_TEST_TIMEOUT_MS;
    while (atomic_load_explicit(&probe.acquired, memory_order_acquire) == 0 &&
           salts_monotonic_ms() < deadline)
      salts_thread_yield();
    check_equal(atomic_load_explicit(&probe.acquired, memory_order_acquire), 1);
    check_equal(chttp_server_get_stats(&server, &deferred_stats), SALTS_OK);
    check_true(deferred_stats.buffer_bytes < server_config.network.max_send_bytes);

    check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port) > 0);
    check_equal(chttp_client_init(&independent_client, &client_config), SALTS_OK);
    check_equal(chttp_server_test_call(&independent_client, uri, "/static", NULL, 0u,
                                       &independent_response),
                SALTS_OK);
    check_equal(independent_response.status_code, 200u);
    check_equal(independent_response.body, "static", 6u);
    chttp_response_destroy(&independent_response);
    check_equal(chttp_client_destroy(&independent_client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);

    check_equal(chttp_server_deferred_reply(&probe.handle, &deferred_response), SALTS_OK);
    check_equal(chttp_server_test_raw_receive(pipelined, response, sizeof(response), &response_size,
                                              true, NULL),
                SALTS_OK);
    check_equal(chttp_server_test_count(response, "HTTP/1.1 200 OK"), (size_t)2u);
    check_not_null(strstr(response, "X-Deferred: yes"));
    check_not_null(strstr(response, "\r\n\r\ndelayedHTTP/1.1 200 OK"));
    check_not_null(strstr(response, "\r\n\r\nstatic"));

    chttp_server_test_close_socket(pipelined);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("runs Castle-style route middleware and bounded cookie sessions without caller polling") {
    chttp_server server = {0};
    chttp_client client = {0};
    chttp_server_test_probe probe = {0};
    chttp_server_config server_config = chttp_server_test_config();
    chttp_client_config client_config = chttp_server_test_client_config();
    const chttp_server_middleware route_middleware[] = {
        {chttp_server_test_route_middleware, &probe}};
    const chttp_server_route_options route = {.method = CHTTP_METHOD_GET,
                                              .path = "/users/:name",
                                              .middleware = route_middleware,
                                              .middleware_count = 1u,
                                              .handler = chttp_server_test_user,
                                              .user = &probe};
    chttp_response first = {0};
    chttp_response second = {0};
    chttp_response missing = {0};
    chttp_server_stats stats = {0};
    chttp_header cookie_header;
    char cookie[128];
    char uri[64];
    const char *set_cookie;
    const char *cookie_end;
    uint16_t port = 0u;
    int uri_size;

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_use(&server, chttp_server_test_global, &probe), SALTS_OK);
    check_equal(chttp_server_route_with(&server, &route), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_true(port != 0u);
    check_equal(chttp_server_start(&server), SALTS_EALREADY);
    check_equal(chttp_server_get(&server, "/late", chttp_server_test_user, &probe), SALTS_EBUSY);
    check_equal(chttp_server_use(&server, chttp_server_test_global, &probe), SALTS_EBUSY);
    check_equal(chttp_server_destroy(&server), SALTS_EBUSY);
    uri_size = snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
    check_true(uri_size > 0 && (size_t)uri_size < sizeof(uri));
    check_equal(chttp_client_init(&client, &client_config), SALTS_OK);

    check_equal(chttp_server_test_call(&client, uri, "/users/alice?from=castle", NULL, 0u, &first),
                SALTS_OK);
    check_equal(first.status_code, 200u);
    check_equal(first.body, "alice:1", 7u);
    check_equal(chttp_response_header(&first, "X-Global"), "yes");
    check_equal(chttp_response_header(&first, "X-Route"), "yes");
    set_cookie = chttp_response_header(&first, "Set-Cookie");
    check_not_null(set_cookie);
    cookie_end = set_cookie == NULL ? NULL : strchr(set_cookie, ';');
    check_not_null(cookie_end);
    if (cookie_end != NULL && (size_t)(cookie_end - set_cookie) < sizeof(cookie)) {
      memcpy(cookie, set_cookie, (size_t)(cookie_end - set_cookie));
      cookie[cookie_end - set_cookie] = '\0';
    } else cookie[0] = '\0';
    cookie_header = (chttp_header){"Cookie", cookie};

    check_equal(chttp_server_test_call(&client, uri, "/users/alice", &cookie_header, 1u, &second),
                SALTS_OK);
    check_equal(second.status_code, 200u);
    check_equal(second.body, "alice:2", 7u);
    check_equal(probe.order, "GRHGRH");

    check_equal(chttp_server_test_call(&client, uri, "/missing", NULL, 0u, &missing), SALTS_OK);
    check_equal(missing.status_code, 404u);
    check_equal(chttp_response_header(&missing, "X-Global"), "yes");
    check_equal(probe.order, "GRHGRHG");
    check_equal(chttp_server_get_stats(&server, &stats), SALTS_OK);
    check_equal(stats.requests, 3u);
    check_equal(stats.responses, 3u);
    check_equal(stats.running, 1);

    chttp_response_destroy(&missing);
    chttp_response_destroy(&second);
    chttp_response_destroy(&first);
    check_equal(chttp_client_destroy(&client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("prefers static routes, falls HEAD back to GET, and reports method mismatch") {
    chttp_server server = {0};
    chttp_client client = {0};
    chttp_server_config server_config = chttp_server_test_config();
    chttp_client_config client_config = chttp_server_test_client_config();
    const chttp_server_middleware next_once[] = {{chttp_server_test_next_once, NULL}};
    const chttp_server_route_options static_route = {.method = CHTTP_METHOD_GET,
                                                     .path = "/items/new",
                                                     .middleware = next_once,
                                                     .middleware_count = 1u,
                                                     .handler = chttp_server_test_static};
    chttp_response get_response = {0};
    chttp_response head_response = {0};
    chttp_response post_response = {0};
    char uri[64];
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/items/:id", chttp_server_test_dynamic, NULL), SALTS_OK);
    check_equal(chttp_server_route_with(&server, &static_route), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port) > 0);
    check_equal(chttp_client_init(&client, &client_config), SALTS_OK);

    check_equal(
        chttp_server_test_method_call(&client, uri, "/items/new", CHTTP_METHOD_GET, &get_response),
        SALTS_OK);
    check_equal(get_response.status_code, 200u);
    check_equal(get_response.body, "static", 6u);
    check_equal(chttp_response_header(&get_response, "X-Next-Once"), "yes");

    check_equal(chttp_server_test_method_call(&client, uri, "/items/new", CHTTP_METHOD_HEAD,
                                              &head_response),
                SALTS_OK);
    check_equal(head_response.status_code, 200u);
    check_equal(head_response.body_size, 0u);
    check_equal(chttp_response_header(&head_response, "Content-Length"), "6");

    check_equal(chttp_server_test_method_call(&client, uri, "/items/new", CHTTP_METHOD_POST,
                                              &post_response),
                SALTS_OK);
    check_equal(post_response.status_code, 405u);
    check_equal(chttp_response_header(&post_response, "Allow"), "GET, HEAD");

    chttp_response_destroy(&post_response);
    chttp_response_destroy(&head_response);
    chttp_response_destroy(&get_response);
    check_equal(chttp_client_destroy(&client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("fails fast at the live-session bound and reuses capacity only after invalidation") {
    chttp_server server = {0};
    chttp_client first_client = {0};
    chttp_client second_client = {0};
    chttp_server_test_probe probe = {0};
    chttp_server_config server_config = chttp_server_test_config();
    chttp_client_config client_config = chttp_server_test_client_config();
    chttp_response first = {0};
    chttp_response full = {0};
    chttp_response retained = {0};
    chttp_response logout = {0};
    chttp_response reused = {0};
    chttp_header cookie_header;
    char cookie[128];
    char uri[64];
    const char *expired_cookie;
    uint16_t port = 0u;

    server_config.session_capacity = 1u;
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/users/:name", chttp_server_test_user, &probe),
                SALTS_OK);
    check_equal(chttp_server_get(&server, "/logout", chttp_server_test_logout, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port) > 0);
    check_equal(chttp_client_init(&first_client, &client_config), SALTS_OK);
    check_equal(chttp_client_init(&second_client, &client_config), SALTS_OK);

    check_equal(chttp_server_test_call(&first_client, uri, "/users/alice", NULL, 0u, &first),
                SALTS_OK);
    check_equal(first.status_code, 200u);
    check_equal(chttp_server_test_cookie_header(&first, cookie, sizeof(cookie)), SALTS_OK);
    cookie_header = (chttp_header){"Cookie", cookie};

    check_equal(chttp_server_test_call(&second_client, uri, "/users/bob", NULL, 0u, &full),
                SALTS_OK);
    check_equal(full.status_code, 500u);

    check_equal(
        chttp_server_test_call(&first_client, uri, "/users/alice", &cookie_header, 1u, &retained),
        SALTS_OK);
    check_equal(retained.status_code, 200u);
    check_equal(retained.body, "alice:2", 7u);

    check_equal(chttp_server_test_call(&first_client, uri, "/logout", &cookie_header, 1u, &logout),
                SALTS_OK);
    check_equal(logout.status_code, 204u);
    check_null(chttp_response_header(&logout, "Content-Length"));
    expired_cookie = chttp_response_header(&logout, "Set-Cookie");
    check_not_null(expired_cookie);
    if (expired_cookie != NULL) check_not_null(strstr(expired_cookie, "Max-Age=0"));

    check_equal(chttp_server_test_call(&second_client, uri, "/users/bob", NULL, 0u, &reused),
                SALTS_OK);
    check_equal(reused.status_code, 200u);
    check_equal(reused.body, "bob:1", 5u);

    chttp_response_destroy(&reused);
    chttp_response_destroy(&logout);
    chttp_response_destroy(&retained);
    chttp_response_destroy(&full);
    chttp_response_destroy(&first);
    check_equal(chttp_client_destroy(&second_client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_client_destroy(&first_client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("sends 100 Continue before reading a request body from the network") {
    static const char headers[] = "POST /echo HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Expect: 100-continue\r\n"
                                  "Content-Length: 4\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_socket socket_value = CHTTP_SERVER_TEST_INVALID_SOCKET;
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_post(&server, "/echo", chttp_server_test_echo_body, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_server_test_raw_connect(port, &socket_value), SALTS_OK);
    check_equal(chttp_server_test_raw_send(socket_value, headers, sizeof(headers) - 1u), SALTS_OK);
    check_equal(chttp_server_test_raw_receive(socket_value, response, sizeof(response),
                                              &response_size, false, "\r\n\r\n"),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 100 Continue\r\n\r\n"));
    check_null(strstr(response, "HTTP/1.1 200 OK"));
    check_equal(chttp_server_test_raw_send(socket_value, "data", 4u), SALTS_OK);
    check_equal(chttp_server_test_raw_receive(socket_value, response, sizeof(response),
                                              &response_size, true, NULL),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_not_null(strstr(response, "\r\n\r\ndata"));

    chttp_server_test_close_socket(socket_value);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("decodes a chunked request body before invoking the handler") {
    static const char request[] = "POST /echo HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Transfer-Encoding: chunked\r\n"
                                  "Connection: close\r\n\r\n"
                                  "4\r\ndata\r\n0\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_post(&server, "/echo", chttp_server_test_echo_body, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_not_null(strstr(response, "Content-Length: 4"));
    check_not_null(strstr(response, "\r\n\r\ndata"));

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("streams a chunked request into a route sink before final dispatch") {
    static const char request[] = "POST /stream-upload HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Transfer-Encoding: chunked\r\n"
                                  "Connection: close\r\n\r\n"
                                  "4\r\ndata\r\n3\r\n123\r\n0\r\n\r\n";
    chttp_server_stream_probe probe = {0};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    const chttp_server_route_options route = {.method = CHTTP_METHOD_POST,
                                              .path = "/stream-upload",
                                              .handler = chttp_server_stream_handler,
                                              .user = &probe,
                                              .body_open = chttp_server_stream_open,
                                              .body_close = chttp_server_stream_close};
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_route_with(&server, &route), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_not_null(strstr(response, "\r\n\r\nok"));
    check_equal(probe.opens, (size_t)1u);
    check_equal(probe.writes, (size_t)2u);
    check_equal(probe.closes, (size_t)1u);
    check_equal(probe.close_status, SALTS_OK);
    check_equal(probe.handler_called, 1);
    check_equal(probe.size, (size_t)7u);
    check_equal(probe.data, "data123", 7u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("closes an HTTP/1.1 route sink once with its original write failure") {
    static const char request[] = "POST /stream-upload HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Content-Length: 4\r\n"
                                  "Connection: keep-alive\r\n\r\ndata";
    chttp_server_stream_probe probe = {.fail_write = 1};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    const chttp_server_route_options route = {.method = CHTTP_METHOD_POST,
                                              .path = "/stream-upload",
                                              .handler = chttp_server_stream_handler,
                                              .user = &probe,
                                              .body_open = chttp_server_stream_open,
                                              .body_close = chttp_server_stream_close};
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_route_with(&server, &route), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 500 Internal Server Error"));
    check_equal(probe.opens, (size_t)1u);
    check_equal(probe.writes, (size_t)1u);
    check_equal(probe.closes, (size_t)1u);
    check_equal(probe.close_status, SALTS_EIO);
    check_equal(probe.handler_called, 0);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("streams a known-length response source after the handler returns") {
    static const unsigned char payload[] = "streamed-response";
    static const char request[] = "GET /stream-response HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server_response_source_probe probe = {
        .data = payload, .size = sizeof(payload) - 1u, .chunk_size = 3u, .content_length_known = 1};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(
        chttp_server_get(&server, "/stream-response", chttp_server_response_source_handler, &probe),
        SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_not_null(strstr(response, "Content-Length: 17"));
    check_not_null(strstr(response, "\r\n\r\nstreamed-response"));
    check_equal(probe.offset, sizeof(payload) - 1u);
    check_equal(probe.calls, (size_t)7u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("closes HTTP/1.1 after a response source fails behind committed headers") {
    static const unsigned char payload[] = "data";
    static const char request[] = "GET /failed-response HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Connection: keep-alive\r\n\r\n";
    chttp_server_response_source_probe probe = {.data = payload,
                                                .size = sizeof(payload) - 1u,
                                                .chunk_size = sizeof(payload) - 1u,
                                                .content_length_known = 1,
                                                .fail_read = 1};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    const char *body;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(
        chttp_server_get(&server, "/failed-response", chttp_server_response_source_handler, &probe),
        SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_not_null(strstr(response, "Content-Length: 4"));
    body = strstr(response, "\r\n\r\n");
    check_not_null(body);
    check_equal(response_size, (size_t)(body + 4u - response));
    check_equal(probe.calls, (size_t)1u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("frames an unknown-length response source with HTTP/1.1 chunked coding") {
    static const unsigned char payload[] = "abcdefg";
    static const char request[] = "GET /chunked-response HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server_response_source_probe probe = {
        .data = payload, .size = sizeof(payload) - 1u, .chunk_size = 3u};
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/chunked-response", chttp_server_response_source_handler,
                                 &probe),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "Transfer-Encoding: chunked"));
    check_null(strstr(response, "Content-Length:"));
    check_not_null(strstr(response, "\r\n\r\n3\r\nabc\r\n3\r\ndef\r\n1\r\ng\r\n0\r\n\r\n"));
    check_equal(probe.offset, sizeof(payload) - 1u);
    check_equal(probe.calls, (size_t)4u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("streams a file response over HTTP/1.1 without loading it into the response body") {
    enum { FILE_RESPONSE_BYTES = 6000 };
    static const char request[] = "GET /file-response HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    unsigned char payload[FILE_RESPONSE_BYTES];
    char *path = tt_make_temp_file("chttp-h1-server-file", ".bin");
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    const char *body;
    uint16_t port = 0u;

    memset(payload, 'x', sizeof(payload));
    payload[sizeof(payload) - 1u] = 'y';
    check_not_null(path);
    check_equal(tt_write_file(path, payload, sizeof(payload)), 0);
    config.max_response_body_bytes = sizeof(payload);
    config.network.read_timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS * 2u;
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(
        chttp_server_get(&server, "/file-response", chttp_server_response_file_handler, path),
        SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "Content-Length: 6000"));
    body = strstr(response, "\r\n\r\n");
    check_not_null(body);
    body += 4u;
    check_true(response_size >= (size_t)(body - response) + sizeof(payload));
    check_equal(body, payload, sizeof(payload));

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(tt_remove_file(path), 0);
    free(path);
  }

  it("keeps a connection reusable while asynchronous file EOF is pending") {
    enum { FILE_RESPONSE_BYTES = 6000, FILE_RESPONSE_ROUNDS = 32 };
    unsigned char payload[FILE_RESPONSE_BYTES];
    char *path = tt_make_temp_file("chttp-h1-server-file-keepalive", ".bin");
    chttp_server server = {0};
    chttp_client client = {0};
    chttp_server_config server_config = chttp_server_test_config();
    chttp_client_config client_config = chttp_server_test_client_config();
    chttp_response response = {0};
    chttp_server_stats stats = {0};
    char uri[64];
    uint16_t port = 0u;
    size_t round;

    memset(payload, 'k', sizeof(payload));
    payload[sizeof(payload) - 1u] = 'z';
    check_not_null(path);
    check_equal(tt_write_file(path, payload, sizeof(payload)), 0);
    server_config.max_response_body_bytes = sizeof(payload);
    server_config.network.read_timeout_ms = CHTTP_SERVER_TEST_TIMEOUT_MS * 2u;
    client_config.max_response_body_bytes = sizeof(payload);
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(
        chttp_server_get(&server, "/file-keepalive", chttp_server_response_file_handler, path),
        SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port) > 0);
    check_equal(chttp_client_init(&client, &client_config), SALTS_OK);

    for (round = 0u; round < FILE_RESPONSE_ROUNDS; ++round) {
      check_equal(chttp_server_test_call(&client, uri, "/file-keepalive", NULL, 0u, &response),
                  SALTS_OK);
      check_equal(response.status_code, 200u);
      check_equal(response.body_size, sizeof(payload));
      check_equal(response.body, payload, sizeof(payload));
      chttp_response_destroy(&response);
      check_equal(chttp_server_test_call(&client, uri, "/static", NULL, 0u, &response), SALTS_OK);
      check_equal(response.status_code, 200u);
      check_equal(response.body, "static", 6u);
      chttp_response_destroy(&response);
    }
    check_equal(chttp_server_get_stats(&server, &stats), SALTS_OK);
    check_equal(stats.accepted_connections, (uint64_t)1u);

    check_equal(chttp_client_destroy(&client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
    check_equal(tt_remove_file(path), 0);
    free(path);
  }

  it("serves HTTP/1.0 without requiring a Host header") {
    static const char request[] = "GET /static HTTP/1.0\r\n"
                                  "Expect: 100-continue\r\n"
                                  "Connection: Upgrade\r\n"
                                  "Upgrade: websocket\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.0 200 OK"));
    check_not_null(strstr(response, "Connection: close"));
    check_not_null(strstr(response, "\r\n\r\nstatic"));
    check_null(strstr(response, "100 Continue"));
    check_null(strstr(response, "426 Upgrade Required"));

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("ignores an unsupported HTTP/1.1 Upgrade invitation and serves the route") {
    static const char request[] = "GET /static HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Connection: Upgrade\r\n"
                                  "Upgrade: websocket\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_socket socket_value = CHTTP_SERVER_TEST_INVALID_SOCKET;
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_server_test_raw_connect(port, &socket_value), SALTS_OK);
    check_equal(chttp_server_test_raw_send(socket_value, request, sizeof(request) - 1u), SALTS_OK);
    check_equal(chttp_server_test_raw_receive(socket_value, response, sizeof(response),
                                              &response_size, false, "\r\n\r\nstatic"),
                SALTS_OK);
    check_not_null(strstr(response, "HTTP/1.1 200 OK"));
    check_not_null(strstr(response, "\r\n\r\nstatic"));
    check_null(strstr(response, "426 Upgrade Required"));

    chttp_server_test_close_socket(socket_value);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("serializes two pipelined requests received in one TCP write") {
    static const char request[] = "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
                                  "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    uint16_t port = 0u;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_equal(chttp_server_test_count(response, "HTTP/1.1 200 OK"), (size_t)2u);
    check_equal(chttp_server_test_count(response, "\r\n\r\nstatic"), (size_t)2u);

    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("returns to network polling after accept admission backpressure") {
    static const char first_request[] = "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    static const char second_request[] = "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                         "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_socket primary = CHTTP_SERVER_TEST_INVALID_SOCKET;
    chttp_server_test_socket flood[12];
    chttp_server_stats stats = {0};
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    size_t response_size = 0u;
    size_t index;
    uint16_t port = 0u;

    config.backlog = 16u;
    config.network.connection_capacity = 4u;
    config.network.command_capacity = 2u;
    for (index = 0u; index < sizeof(flood) / sizeof(flood[0]); ++index)
      flood[index] = CHTTP_SERVER_TEST_INVALID_SOCKET;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_server_test_raw_connect(port, &primary), SALTS_OK);
    check_equal(chttp_server_test_raw_send(primary, first_request, sizeof(first_request) - 1u),
                SALTS_OK);
    check_equal(chttp_server_test_raw_receive(primary, response, sizeof(response), &response_size,
                                              false, "\r\n\r\nstatic"),
                SALTS_OK);

    for (index = 0u; index < sizeof(flood) / sizeof(flood[0]); ++index)
      check_equal(chttp_server_test_raw_connect(port, &flood[index]), SALTS_OK);
    check_equal(chttp_server_test_raw_send(primary, second_request, sizeof(second_request) - 1u),
                SALTS_OK);
    check_equal(chttp_server_test_raw_receive(primary, response, sizeof(response), &response_size,
                                              true, NULL),
                SALTS_OK);
    check_equal(chttp_server_test_count(response, "HTTP/1.1 200 OK"), (size_t)2u);
    check_equal(chttp_server_get_stats(&server, &stats), SALTS_OK);
    check_true(stats.rejected_connections != 0u);

    for (index = 0u; index < sizeof(flood) / sizeof(flood[0]); ++index)
      if (flood[index] != CHTTP_SERVER_TEST_INVALID_SOCKET)
        chttp_server_test_close_socket(flood[index]);
    chttp_server_test_close_socket(primary);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("retries callback actions when the command ring is full") {
    static const char request[] = "GET /static HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_socket clients[CHTTP_SERVER_TEST_PRESSURE_CLIENTS];
    char responses[CHTTP_SERVER_TEST_PRESSURE_CLIENTS][CHTTP_SERVER_TEST_RAW_BYTES] = {{0}};
    size_t response_sizes[CHTTP_SERVER_TEST_PRESSURE_CLIENTS] = {0};
    chttp_server_stats stats = {0};
    uint64_t admission_deadline;
    size_t index;
    uint16_t port = 0u;

    config.network.connection_capacity = CHTTP_SERVER_TEST_PRESSURE_CLIENTS;
    config.network.command_capacity = 1u;
    config.network.request_capacity = CHTTP_SERVER_TEST_PRESSURE_CLIENTS;
    config.network.completion_batch_capacity = CHTTP_SERVER_TEST_PRESSURE_CLIENTS;
    for (index = 0u; index < CHTTP_SERVER_TEST_PRESSURE_CLIENTS; ++index)
      clients[index] = CHTTP_SERVER_TEST_INVALID_SOCKET;

    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/static", chttp_server_test_static, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    admission_deadline = salts_monotonic_ms() + CHTTP_SERVER_TEST_TIMEOUT_MS;
    for (index = 0u; index < CHTTP_SERVER_TEST_PRESSURE_CLIENTS; ++index) {
      int status = SALTS_ETIMEDOUT;
      do {
        chttp_server_test_socket candidate = CHTTP_SERVER_TEST_INVALID_SOCKET;
        status = chttp_server_test_raw_connect(port, &candidate);
        if (status == SALTS_OK)
          status = chttp_server_test_wait_active(&server, (uint64_t)(index + 1u),
                                                 CHTTP_SERVER_TEST_ADMISSION_ATTEMPT_MS);
        if (status == SALTS_OK) {
          clients[index] = candidate;
          break;
        }
        chttp_server_test_close_socket(candidate);
      } while (status == SALTS_ETIMEDOUT && salts_monotonic_ms() < admission_deadline);
      check_equal(status, SALTS_OK);
    }

    for (index = 0u; index < CHTTP_SERVER_TEST_PRESSURE_CLIENTS; ++index)
      check_equal(chttp_server_test_raw_send(clients[index], request, sizeof(request) - 1u),
                  SALTS_OK);
    for (index = 0u; index < CHTTP_SERVER_TEST_PRESSURE_CLIENTS; ++index) {
      check_equal(chttp_server_test_raw_receive(clients[index], responses[index],
                                                sizeof(responses[index]), &response_sizes[index],
                                                true, NULL),
                  SALTS_OK);
      check_not_null(strstr(responses[index], "HTTP/1.1 200 OK"));
      check_not_null(strstr(responses[index], "\r\n\r\nstatic"));
      chttp_server_test_close_socket(clients[index]);
      clients[index] = CHTTP_SERVER_TEST_INVALID_SOCKET;
    }

    check_equal(chttp_server_test_wait_active(&server, 0u, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_get_stats(&server, &stats), SALTS_OK);
    check_equal(stats.responses, (uint64_t)CHTTP_SERVER_TEST_PRESSURE_CLIENTS);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("lets an external stop time out and retry while a handler is active") {
    static const char request[] = "GET /blocking HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_server_test_blocking_probe probe;
    chttp_server_test_raw_client client = {0};
    salts_thread_t client_thread = NULL;
    uint64_t deadline;

    atomic_init(&probe.entered, 0);
    atomic_init(&probe.release, 0);
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/blocking", chttp_server_test_blocking, &probe),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &client.port), SALTS_OK);
    client.request = request;
    client.status = SALTS_EIO;
    check_equal(salts_thread_create(&client_thread, chttp_server_test_raw_client_entry, &client),
                SALTS_OK);
    deadline = salts_monotonic_ms() + CHTTP_SERVER_TEST_TIMEOUT_MS;
    while (atomic_load_explicit(&probe.entered, memory_order_acquire) == 0 &&
           salts_monotonic_ms() < deadline)
      salts_thread_yield();
    check_equal(atomic_load_explicit(&probe.entered, memory_order_acquire), 1);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_STOP_TIMEOUT_MS), SALTS_ETIMEDOUT);
    atomic_store_explicit(&probe.release, 1, memory_order_release);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(salts_thread_join(&client_thread), SALTS_OK);
    salts_thread_destroy(&client_thread);
    check_equal(client.status, SALTS_OK);
    check_not_null(strstr(client.response, "HTTP/1.1 200 OK"));
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("rejects an unreserved pipelined response before handler and Session side effects") {
    static const char request[] = "GET /large HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
                                  "GET /large HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                  "Connection: close\r\n\r\n";
    chttp_server server = {0};
    chttp_client client = {0};
    chttp_server_config config = chttp_server_test_config();
    chttp_client_config client_config = chttp_server_test_client_config();
    chttp_server_test_large_probe probe;
    chttp_response response_value = {0};
    char response[CHTTP_SERVER_TEST_RAW_BYTES] = {0};
    char uri[64];
    size_t response_size = 0u;
    uint16_t port = 0u;

    atomic_init(&probe.calls, 0);
    config.max_response_header_bytes = 256u;
    config.network.max_send_bytes =
        config.max_response_body_bytes + config.max_response_header_bytes + 256u + 25u;
    config.session_capacity = 2u;
    check_equal(chttp_server_init(&server, &config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/large", chttp_server_test_large_session, &probe),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(
        chttp_server_test_raw_exchange(port, request, response, sizeof(response), &response_size),
        SALTS_OK);
    check_equal(chttp_server_test_count(response, "HTTP/1.1 200 OK"), (size_t)1u);
    check_equal(atomic_load_explicit(&probe.calls, memory_order_acquire), 1);

    check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port) > 0);
    check_equal(chttp_client_init(&client, &client_config), SALTS_OK);
    check_equal(chttp_server_test_call(&client, uri, "/large", NULL, 0u, &response_value),
                SALTS_OK);
    check_equal(response_value.status_code, 200u);
    check_equal(atomic_load_explicit(&probe.calls, memory_order_acquire), 2);

    chttp_response_destroy(&response_value);
    check_equal(chttp_client_destroy(&client, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_SERVER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }
}
