#include "tinytest.h"

#include "chttp_server_runtime.h"

#include <chttp/chttp.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS = 5000,
  CHTTP_H2_DEFERRED_TEST_STOP_TIMEOUT_MS = 20
};

typedef struct chttp_h2_deferred_probe {
  chttp_server_deferred deferred;
  atomic_int acquired;
  int reply_status;
  int defer_status;
  int immediate_reply_status;
  int application_admitted;
} chttp_h2_deferred_probe;

typedef struct chttp_h2_deferred_completion {
  size_t calls;
  int status;
  unsigned int response_status;
  char body[64];
  size_t body_size;
  char x_base[32];
  char x_replace[32];
  char x_deferred[32];
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
  const cnet_client_config config = {.backend = chttp_h2_deferred_backend(),
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
  const chttp_server_config config = {.host = "127.0.0.1",
                                      .port = 0u,
                                      .backlog = 8u,
                                      .network = chttp_h2_deferred_network(2u),
                                      .route_capacity = 8u,
                                      .middleware_capacity = 2u,
                                      .max_route_middleware_count = 2u,
                                      .max_route_param_count = 2u,
                                      .max_route_param_bytes = 64u,
                                      .max_target_bytes = 256u,
                                      .max_header_count = 16u,
                                      .max_header_bytes = 4096u,
                                      .max_request_body_bytes = 4096u,
                                      .max_response_header_count = 16u,
                                      .max_response_header_bytes = 4096u,
                                      .max_response_body_bytes = 4096u,
                                      .max_buffered_response_body_bytes = 4096u,
                                      .session_capacity = 0u,
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
  const chttp_client_config config = {.network = chttp_h2_deferred_network(1u),
                                      .request_capacity = 8u,
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

static int chttp_h2_deferred_endpoint(uint16_t port, char *uri, size_t uri_capacity,
                                      char *authority, size_t authority_capacity) {
  const int uri_size = snprintf(uri, uri_capacity, "tcp://127.0.0.1:%u", (unsigned int)port);
  const int authority_size =
      snprintf(authority, authority_capacity, "127.0.0.1:%u", (unsigned int)port);
  return uri_size > 0 && (size_t)uri_size < uri_capacity && authority_size > 0 &&
                 (size_t)authority_size < authority_capacity
             ? SALTS_OK
             : SALTS_EMSGSIZE;
}

static int chttp_h2_deferred_handler(void *user, const chttp_server_request_view *request,
                                     chttp_server_response *response) {
  chttp_h2_deferred_probe *probe = (chttp_h2_deferred_probe *)user;
  chttp_server_deferred deferred = CHTTP_SERVER_DEFERRED_INIT;
  int status;
  if (probe == NULL || request == NULL || request->http_major != 2u) return SALTS_EPROTO;
  status = chttp_server_response_defer(response, &deferred);
  probe->defer_status = status;
  if (status != SALTS_OK) return status;
  probe->deferred = deferred;
  atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return SALTS_OK;
}

static int chttp_h2_deferred_header_handler(void *user,
                                            const chttp_server_request_view *request,
                                            chttp_server_response *response) {
  int status;
  if (request == NULL || request->http_major != 2u) return SALTS_EPROTO;
  status = chttp_server_response_set_header(response, "X-Base", "before");
  if (status == SALTS_OK)
    status = chttp_server_response_set_header(response, "X-Replace", "old");
  if (status != SALTS_OK) return status;
  return chttp_h2_deferred_handler(user, request, response);
}

static int chttp_h2_deferred_lease_first_handler(void *user,
                                                 const chttp_server_request_view *request,
                                                 chttp_server_response *response) {
  chttp_h2_deferred_probe *probe = (chttp_h2_deferred_probe *)user;
  chttp_server_deferred deferred = CHTTP_SERVER_DEFERRED_INIT;
  const chttp_server_deferred_response rejected = {.size = sizeof(rejected),
                                                   .status_code = 503u,
                                                   .content_type = "text/plain",
                                                   .body = "rejected",
                                                   .body_size = sizeof("rejected") - 1u};
  if (probe == NULL || request == NULL || request->http_major != 2u) return SALTS_EPROTO;
  probe->defer_status = chttp_server_response_defer(response, &deferred);
  if (probe->defer_status != SALTS_OK) return probe->defer_status;
  probe->application_admitted = 0;
  probe->immediate_reply_status = chttp_server_deferred_reply(&deferred, &rejected);
  probe->deferred = deferred;
  atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return probe->immediate_reply_status;
}

static void chttp_h2_deferred_reply_worker(void *user) {
  chttp_h2_deferred_probe *probe = (chttp_h2_deferred_probe *)user;
  const chttp_server_deferred_response reply = {.size = sizeof(reply),
                                                .status_code = 200u,
                                                .content_type = "text/plain",
                                                .body = "deferred-h2",
                                                .body_size = sizeof("deferred-h2") - 1u};
  const uint64_t deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
  if (probe == NULL) return;
  while (atomic_load_explicit(&probe->acquired, memory_order_acquire) == 0 &&
         salts_monotonic_ms() < deadline)
    salts_thread_yield();
  if (atomic_load_explicit(&probe->acquired, memory_order_acquire) == 0) {
    probe->reply_status = SALTS_ETIMEDOUT;
    return;
  }
  probe->reply_status = chttp_server_deferred_reply(&probe->deferred, &reply);
}

static void chttp_h2_deferred_copy_header(const chttp_response_view *response, const char *name,
                                          char *output, size_t capacity) {
  const char *value = chttp_response_view_header(response, name);
  if (value != NULL && capacity != 0u) (void)snprintf(output, capacity, "%s", value);
}

static void chttp_h2_deferred_complete(void *user, chttp_request request,
                                       const chttp_response_view *response,
                                       const chttp_error *error) {
  chttp_h2_deferred_completion *completion = (chttp_h2_deferred_completion *)user;
  (void)request;
  if (completion == NULL) return;
  ++completion->calls;
  if (error != NULL) {
    completion->status = error->status;
    return;
  }
  completion->status = SALTS_OK;
  completion->response_status = response->status_code;
  completion->body_size = response->body_size;
  if (response->body != NULL && response->body_size <= sizeof(completion->body))
    memcpy(completion->body, response->body, response->body_size);
  chttp_h2_deferred_copy_header(response, "X-Base", completion->x_base,
                                sizeof(completion->x_base));
  chttp_h2_deferred_copy_header(response, "X-Replace", completion->x_replace,
                                sizeof(completion->x_replace));
  chttp_h2_deferred_copy_header(response, "X-Deferred", completion->x_deferred,
                                sizeof(completion->x_deferred));
}

static int chttp_h2_deferred_poll_until(chttp_async_client *client, size_t *value, size_t expected) {
  const uint64_t deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
  while (*value < expected && salts_monotonic_ms() < deadline) {
    size_t completions = 0u;
    int status = chttp_async_client_poll(client, 20u, &completions);
    if (status != SALTS_OK) return status;
  }
  return *value >= expected ? SALTS_OK : SALTS_ETIMEDOUT;
}

static int chttp_h2_deferred_poll_until_acquired(chttp_async_client *client,
                                                 chttp_h2_deferred_probe *probe) {
  const uint64_t deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(&probe->acquired, memory_order_acquire) == 0 &&
         salts_monotonic_ms() < deadline) {
    size_t completions = 0u;
    int status = chttp_async_client_poll(client, 20u, &completions);
    if (status != SALTS_OK) return status;
  }
  return atomic_load_explicit(&probe->acquired, memory_order_acquire) != 0 ? SALTS_OK
                                                                          : SALTS_ETIMEDOUT;
}

static chttp_request_options chttp_h2_deferred_options(const char *uri, const char *authority,
                                                       const char *target,
                                                       chttp_h2_deferred_completion *completion) {
  return (chttp_request_options){.connection_uri = uri,
                                 .authority = authority,
                                 .target = target,
                                 .method = CHTTP_METHOD_GET,
                                 .on_complete = chttp_h2_deferred_complete,
                                 .user = completion,
                                 .protocol = CHTTP_HTTP_2};
}

spec("CHTTP HTTP2 deferred responses") {
  it("completes a deferred HTTP2 response from another thread") {
    chttp_h2_deferred_probe probe = {.reply_status = SALTS_EIO};
    chttp_h2_deferred_completion completion = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    chttp_request_options options;
    salts_thread_t worker = NULL;
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/deferred", chttp_h2_deferred_handler, &probe), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    check_equal(salts_thread_create(&worker, chttp_h2_deferred_reply_worker, &probe), SALTS_OK);

    options = chttp_h2_deferred_options(uri, authority, "/deferred", &completion);
    check_equal(chttp_async_client_submit(&client, &options, &request), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until(&client, &completion.calls, 1u), SALTS_OK);

    check_equal(salts_thread_join(&worker), SALTS_OK);
    salts_thread_destroy(&worker);
    check_equal(atomic_load_explicit(&probe.acquired, memory_order_acquire), 1);
    check_equal(probe.reply_status, SALTS_OK);
    check_equal(completion.calls, (size_t)1u);
    check_equal(completion.status, SALTS_OK);
    check_equal(completion.response_status, 200u);
    check_equal(completion.body_size, sizeof("deferred-h2") - 1u);
    check_equal(completion.body, "deferred-h2", sizeof("deferred-h2") - 1u);

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("keeps H2 copied reply storage lazy until first worker copy") {
    chttp_h2_deferred_probe probe = {0};
    chttp_h2_deferred_completion completion = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    chttp_request_options options;
    chttp_server_deferred_control *control;
    const chttp_server_deferred_response reply = {.size = sizeof(reply),
                                                  .status_code = 200u,
                                                  .content_type = "text/plain",
                                                  .body = "lazy",
                                                  .body_size = 4u};
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/lazy", chttp_h2_deferred_handler, &probe), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    options = chttp_h2_deferred_options(uri, authority, "/lazy", &completion);
    check_equal(chttp_async_client_submit(&client, &options, &request), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until_acquired(&client, &probe), SALTS_OK);

    control = (chttp_server_deferred_control *)probe.deferred.impl;
    check_not_null(control);
    check_equal(chttp_server_deferred_control_state(control), CHTTP_SERVER_DEFERRED_PENDING);
    check_true(!control->reply_builder_initialized);
    check_equal(chttp_server_deferred_reply(&probe.deferred, &reply), SALTS_OK);
    check_true(control->reply_builder_initialized);
    check_equal(chttp_h2_deferred_poll_until(&client, &completion.calls, 1u), SALTS_OK);
    check_equal(completion.response_status, 200u);
    check_equal(completion.body, "lazy", 4u);

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("retains base headers and lets deferred headers replace them") {
    const chttp_header reply_headers[] = {{"X-Replace", "new"}, {"X-Deferred", "yes"}};
    const chttp_server_deferred_response reply = {.size = sizeof(reply),
                                                  .status_code = 200u,
                                                  .content_type = "text/plain",
                                                  .headers = reply_headers,
                                                  .header_count = 2u,
                                                  .body = "headers",
                                                  .body_size = 7u};
    chttp_h2_deferred_probe probe = {0};
    chttp_h2_deferred_completion completion = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    chttp_request_options options;
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/headers", chttp_h2_deferred_header_handler, &probe),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    options = chttp_h2_deferred_options(uri, authority, "/headers", &completion);
    check_equal(chttp_async_client_submit(&client, &options, &request), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until_acquired(&client, &probe), SALTS_OK);
    check_equal(chttp_server_deferred_reply(&probe.deferred, &reply), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until(&client, &completion.calls, 1u), SALTS_OK);
    check_equal(completion.x_base, "before");
    check_equal(completion.x_replace, "new");
    check_equal(completion.x_deferred, "yes");

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("keeps the lease retryable when a copied body exceeds capacity") {
    static const unsigned char oversized[4097] = {0};
    const chttp_server_deferred_response too_large = {.size = sizeof(too_large),
                                                      .status_code = 200u,
                                                      .content_type = "text/plain",
                                                      .body = oversized,
                                                      .body_size = sizeof(oversized)};
    const chttp_server_deferred_response retry = {.size = sizeof(retry),
                                                  .status_code = 200u,
                                                  .content_type = "text/plain",
                                                  .body = "ok",
                                                  .body_size = 2u};
    chttp_h2_deferred_probe probe = {0};
    chttp_h2_deferred_completion completion = {0};
    chttp_server_deferred before;
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    chttp_request_options options;
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/retry", chttp_h2_deferred_handler, &probe), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    options = chttp_h2_deferred_options(uri, authority, "/retry", &completion);
    check_equal(chttp_async_client_submit(&client, &options, &request), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until_acquired(&client, &probe), SALTS_OK);

    before = probe.deferred;
    check_equal(chttp_server_deferred_reply(&probe.deferred, &too_large), SALTS_EMSGSIZE);
    check_equal(probe.deferred.impl, before.impl);
    check_equal(probe.deferred.generation, before.generation);
    check_equal(chttp_server_deferred_reply(&probe.deferred, &retry), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until(&client, &completion.calls, 1u), SALTS_OK);
    check_equal(completion.response_status, 200u);
    check_equal(completion.body, "ok", 2u);

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("reserves the CHTTP lease before application admission rejection") {
    chttp_h2_deferred_probe probe = {.application_admitted = 1,
                                     .defer_status = SALTS_EIO,
                                     .immediate_reply_status = SALTS_EIO};
    chttp_h2_deferred_completion completion = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    chttp_request_options options;
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/lease-first", chttp_h2_deferred_lease_first_handler,
                                 &probe),
                SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    options = chttp_h2_deferred_options(uri, authority, "/lease-first", &completion);
    check_equal(chttp_async_client_submit(&client, &options, &request), SALTS_OK);
    check_equal(chttp_h2_deferred_poll_until(&client, &completion.calls, 1u), SALTS_OK);

    check_equal(probe.defer_status, SALTS_OK);
    check_equal(probe.application_admitted, 0);
    check_equal(probe.immediate_reply_status, SALTS_OK);
    check_null(probe.deferred.impl);
    check_equal(completion.response_status, 503u);
    check_equal(completion.body, "rejected", 8u);

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }
}
