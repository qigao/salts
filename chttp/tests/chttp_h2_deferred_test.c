#include "tinytest.h"

#include <chttp/chttp.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS = 5000 };

typedef struct chttp_h2_deferred_probe {
  chttp_server_deferred deferred;
  atomic_int acquired;
  int reply_status;
} chttp_h2_deferred_probe;

typedef struct chttp_h2_deferred_completion {
  size_t calls;
  int status;
  unsigned int response_status;
  char body[32];
  size_t body_size;
} chttp_h2_deferred_completion;

static native_io_backend_kind chttp_h2_deferred_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config chttp_h2_deferred_network(size_t connections) {
  const cnet_client_config config = {
      .backend = chttp_h2_deferred_backend(),
      .connection_capacity = connections,
      .command_capacity = 16u,
      .request_capacity = 16u,
      .completion_batch_capacity = 8u,
      .event_capacity = 32u,
      .max_send_bytes = 64u * 1024u,
      .receive_buffer_bytes = 4096u,
      .connect_timeout_ms = CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS,
      .read_timeout_ms = CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS,
      .write_timeout_ms = CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS};
  return config;
}

static chttp_server_config chttp_h2_deferred_server_config(void) {
  const chttp_server_config config = {
      .host = "127.0.0.1",
      .port = 0u,
      .backlog = 8u,
      .network = chttp_h2_deferred_network(2u),
      .route_capacity = 4u,
      .middleware_capacity = 2u,
      .max_route_middleware_count = 2u,
      .max_route_param_count = 2u,
      .max_route_param_bytes = 64u,
      .max_target_bytes = 128u,
      .max_header_count = 16u,
      .max_header_bytes = 4096u,
      .max_request_body_bytes = 4096u,
      .max_response_header_count = 16u,
      .max_response_header_bytes = 4096u,
      .max_response_body_bytes = 4096u,
      .poll_slice_ms = 2u,
      .enable_http2 = 1,
      .h2_stream_capacity = 4u,
      .h2_input_buffer_bytes = 64u * 1024u,
      .h2_output_buffer_bytes = 64u * 1024u,
      .h2_hpack_dynamic_table_bytes = 4096u,
      .h2_max_settings_count = 16u};
  return config;
}

static chttp_client_config chttp_h2_deferred_client_config(void) {
  const chttp_client_config config = {
      .network = chttp_h2_deferred_network(1u),
      .request_capacity = 4u,
      .max_start_line_bytes = 256u,
      .max_header_count = 16u,
      .max_header_bytes = 4096u,
      .max_request_body_bytes = 4096u,
      .max_response_body_bytes = 4096u,
      .max_informational_responses = 2u,
      .h2_input_buffer_bytes = 64u * 1024u,
      .h2_hpack_dynamic_table_bytes = 4096u,
      .h2_max_settings_count = 16u};
  return config;
}

static int chttp_h2_deferred_handler(void *user, const chttp_server_request_view *request,
                                     chttp_server_response *response) {
  chttp_h2_deferred_probe *probe = (chttp_h2_deferred_probe *)user;
  chttp_server_deferred deferred = CHTTP_SERVER_DEFERRED_INIT;
  int status;
  if (probe == NULL || request == NULL || response == NULL || request->http_major != 2u)
    return SALTS_EPROTO;
  status = chttp_server_response_defer(response, &deferred);
  if (status != SALTS_OK) return status;
  probe->deferred = deferred;
  atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return SALTS_OK;
}

static void chttp_h2_deferred_worker(void *user) {
  chttp_h2_deferred_probe *probe = (chttp_h2_deferred_probe *)user;
  const uint64_t deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
  const chttp_server_deferred_response reply = {
      .size = sizeof(reply),
      .status_code = 200u,
      .content_type = "text/plain",
      .body = "deferred-h2",
      .body_size = sizeof("deferred-h2") - 1u};
  if (probe == NULL) return;
  while (!atomic_load_explicit(&probe->acquired, memory_order_acquire)) {
    if (salts_monotonic_ms() >= deadline) {
      probe->reply_status = SALTS_ETIMEDOUT;
      return;
    }
    salts_thread_yield();
  }
  probe->reply_status = chttp_server_deferred_reply(&probe->deferred, &reply);
}

static void chttp_h2_deferred_complete(void *user, chttp_request request,
                                       const chttp_response_view *response,
                                       const chttp_error *error) {
  chttp_h2_deferred_completion *completion = (chttp_h2_deferred_completion *)user;
  size_t body_size;
  (void)request;
  if (completion == NULL) return;
  ++completion->calls;
  if (error != NULL) {
    completion->status = error->status;
    return;
  }
  if (response == NULL) {
    completion->status = SALTS_EPROTO;
    return;
  }
  completion->status = SALTS_OK;
  completion->response_status = response->status_code;
  body_size = response->body_size;
  if (body_size >= sizeof(completion->body)) body_size = sizeof(completion->body) - 1u;
  if (body_size != 0u && response->body != NULL)
    memcpy(completion->body, response->body, body_size);
  completion->body[body_size] = '\0';
  completion->body_size = body_size;
}

spec("CHTTP HTTP/2 deferred server response") {
  it("publishes a deferred response from a worker on one HTTP/2 stream") {
    chttp_h2_deferred_probe probe;
    chttp_h2_deferred_completion completion = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    chttp_request_options options;
    salts_thread_t worker = NULL;
    char uri[64];
    char authority[64];
    uint16_t port = 0u;
    size_t completions = 0u;
    size_t polls = 0u;

    memset(&probe, 0, sizeof(probe));
    probe.deferred = (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
    probe.reply_status = SALTS_EBUSY;
    atomic_init(&probe.acquired, 0);

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/deferred", chttp_h2_deferred_handler, &probe), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port) > 0);
    check_true(snprintf(authority, sizeof(authority), "127.0.0.1:%u", (unsigned int)port) > 0);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    check_equal(salts_thread_create(&worker, chttp_h2_deferred_worker, &probe), SALTS_OK);

    options = (chttp_request_options){.connection_uri = uri,
                                      .authority = authority,
                                      .target = "/deferred",
                                      .method = CHTTP_METHOD_GET,
                                      .on_complete = chttp_h2_deferred_complete,
                                      .user = &completion,
                                      .protocol = CHTTP_HTTP_2};
    check_equal(chttp_async_client_submit(&client, &options, &request), SALTS_OK);

    while (completion.calls == 0u && polls++ < 40u)
      check_equal(chttp_async_client_poll(&client, 250u, &completions), SALTS_OK);

    check_equal(salts_thread_join(&worker), SALTS_OK);
    salts_thread_destroy(&worker);
    check_equal(atomic_load_explicit(&probe.acquired, memory_order_acquire), 1);
    check_equal(probe.reply_status, SALTS_OK);
    check_equal(completion.calls, (size_t)1u);
    check_equal(completion.status, SALTS_OK);
    check_equal(completion.response_status, 200u);
    check_equal(completion.body_size, sizeof("deferred-h2") - 1u);
    check_equal(completion.body, "deferred-h2");

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }
}
