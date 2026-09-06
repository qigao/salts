from pathlib import Path

path = Path("chttp/tests/chttp_h2_deferred_test.c")
text = path.read_text()
marker = 'spec("CHTTP HTTP2 deferred responses")'
if marker not in text:
    raise SystemExit("Task 2 deferred test marker missing")
if 'spec("CHTTP HTTP2 deferred Task 3 bounds")' in text:
    raise SystemExit("Task 3 RED already appended")

append = r'''

typedef struct chttp_h2_task3_completion {
  size_t calls;
  int status;
  unsigned int response_status;
  char body[32];
  size_t body_size;
  char x_base[32];
  char x_replace[32];
  char x_deferred[32];
} chttp_h2_task3_completion;

typedef struct chttp_h2_task3_copy_probe {
  chttp_server_deferred deferred;
  atomic_int acquired;
  int first_reply_status;
  int second_reply_status;
  int handle_unchanged;
} chttp_h2_task3_copy_probe;

typedef struct chttp_h2_task3_reject_probe {
  atomic_int defer_status;
  atomic_int application_admitted;
  atomic_int immediate_reply_status;
  char copied_target[64];
} chttp_h2_task3_reject_probe;

typedef struct chttp_h2_task3_pool_probe {
  chttp_server_deferred deferred[3];
  int defer_status[3];
  size_t server_calls;
  atomic_size_t published_calls;
} chttp_h2_task3_pool_probe;

static void chttp_h2_task3_copy_text(char *dst, size_t capacity, const char *value) {
  if (dst == NULL || capacity == 0u) return;
  dst[0] = '\0';
  if (value != NULL) (void)snprintf(dst, capacity, "%s", value);
}

static void chttp_h2_task3_complete(void *user, chttp_request request,
                                    const chttp_response_view *response,
                                    const chttp_error *error) {
  chttp_h2_task3_completion *completion = (chttp_h2_task3_completion *)user;
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
  chttp_h2_task3_copy_text(completion->x_base, sizeof(completion->x_base),
                           chttp_response_view_header(response, "X-Base"));
  chttp_h2_task3_copy_text(completion->x_replace, sizeof(completion->x_replace),
                           chttp_response_view_header(response, "X-Replace"));
  chttp_h2_task3_copy_text(completion->x_deferred, sizeof(completion->x_deferred),
                           chttp_response_view_header(response, "X-Deferred"));
}

static int chttp_h2_task3_poll_completion(chttp_async_client *client,
                                          chttp_h2_task3_completion *completion,
                                          uint32_t timeout_ms) {
  const uint64_t deadline = salts_monotonic_ms() + timeout_ms;
  if (client == NULL || completion == NULL) return SALTS_EINVAL;
  while (completion->calls == 0u && salts_monotonic_ms() < deadline) {
    size_t completions = 0u;
    int status = chttp_async_client_poll(client, 20u, &completions);
    if (status != SALTS_OK) return status;
  }
  return completion->calls == 0u ? SALTS_ETIMEDOUT : SALTS_OK;
}

static int chttp_h2_task3_copy_handler(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response) {
  chttp_h2_task3_copy_probe *probe = (chttp_h2_task3_copy_probe *)user;
  int status;
  if (probe == NULL || request == NULL || response == NULL || request->http_major != 2u)
    return SALTS_EPROTO;
  status = chttp_server_response_set_header(response, "X-Base", "before");
  if (status == SALTS_OK)
    status = chttp_server_response_set_header(response, "X-Replace", "old");
  if (status == SALTS_OK) status = chttp_server_response_defer(response, &probe->deferred);
  if (status == SALTS_OK) atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return status;
}

static void chttp_h2_task3_copy_worker(void *user) {
  chttp_h2_task3_copy_probe *probe = (chttp_h2_task3_copy_probe *)user;
  static const unsigned char too_large[17] = {0};
  const chttp_header headers[] = {{"X-Replace", "new"}, {"X-Deferred", "yes"}};
  chttp_server_deferred before;
  chttp_server_deferred_response reply;
  const uint64_t deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
  if (probe == NULL) return;
  while (!atomic_load_explicit(&probe->acquired, memory_order_acquire) &&
         salts_monotonic_ms() < deadline)
    salts_thread_yield();
  if (!atomic_load_explicit(&probe->acquired, memory_order_acquire)) {
    probe->first_reply_status = SALTS_ETIMEDOUT;
    return;
  }
  before = probe->deferred;
  reply = (chttp_server_deferred_response){.size = sizeof(reply),
                                           .status_code = 200u,
                                           .content_type = "text/plain",
                                           .headers = headers,
                                           .header_count = 2u,
                                           .body = too_large,
                                           .body_size = sizeof(too_large)};
  probe->first_reply_status = chttp_server_deferred_reply(&probe->deferred, &reply);
  probe->handle_unchanged = probe->deferred.impl == before.impl &&
                            probe->deferred.generation == before.generation;
  reply.body = "bounded";
  reply.body_size = sizeof("bounded") - 1u;
  probe->second_reply_status = chttp_server_deferred_reply(&probe->deferred, &reply);
}

static int chttp_h2_task3_reject_handler(void *user, const chttp_server_request_view *request,
                                         chttp_server_response *response) {
  chttp_h2_task3_reject_probe *probe = (chttp_h2_task3_reject_probe *)user;
  chttp_server_deferred deferred = CHTTP_SERVER_DEFERRED_INIT;
  const chttp_server_deferred_response reply = {.size = sizeof(reply),
                                                .status_code = 503u,
                                                .content_type = "text/plain",
                                                .body = "rejected",
                                                .body_size = sizeof("rejected") - 1u};
  int status;
  if (probe == NULL || request == NULL || response == NULL) return SALTS_EINVAL;
  chttp_h2_task3_copy_text(probe->copied_target, sizeof(probe->copied_target), request->target);
  status = chttp_server_response_defer(response, &deferred);
  atomic_store_explicit(&probe->defer_status, status, memory_order_release);
  if (status != SALTS_OK) return status;
  atomic_store_explicit(&probe->application_admitted, 0, memory_order_release);
  status = chttp_server_deferred_reply(&deferred, &reply);
  atomic_store_explicit(&probe->immediate_reply_status, status, memory_order_release);
  return status;
}

static int chttp_h2_task3_sync_handler(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response) {
  (void)user;
  (void)request;
  return chttp_server_reply(response, 200u, "text/plain", "sibling", sizeof("sibling") - 1u);
}

static int chttp_h2_task3_pool_handler(void *user, const chttp_server_request_view *request,
                                       chttp_server_response *response) {
  chttp_h2_task3_pool_probe *probe = (chttp_h2_task3_pool_probe *)user;
  chttp_server_deferred deferred = CHTTP_SERVER_DEFERRED_INIT;
  size_t index;
  int status;
  (void)request;
  if (probe == NULL || response == NULL) return SALTS_EINVAL;
  index = probe->server_calls++;
  if (index >= 3u) return SALTS_EBUSY;
  status = chttp_server_response_defer(response, &deferred);
  probe->defer_status[index] = status;
  probe->deferred[index] = deferred;
  atomic_store_explicit(&probe->published_calls, index + 1u, memory_order_release);
  if (status == SALTS_ENOBUFS)
    return chttp_server_reply(response, 503u, "text/plain", "busy", sizeof("busy") - 1u);
  return status;
}

static int chttp_h2_task3_submit(chttp_async_client *client, const char *uri,
                                 const char *authority, const char *target,
                                 chttp_h2_task3_completion *completion,
                                 chttp_request *out_request) {
  const chttp_request_options options = {.connection_uri = uri,
                                         .authority = authority,
                                         .target = target,
                                         .method = CHTTP_METHOD_GET,
                                         .on_complete = chttp_h2_task3_complete,
                                         .user = completion,
                                         .protocol = CHTTP_HTTP_2};
  return chttp_async_client_submit(client, &options, out_request);
}

static void chttp_h2_task3_reply_pending(chttp_server_deferred *deferred, const char *body) {
  chttp_server_deferred_response reply;
  if (deferred == NULL || deferred->impl == NULL) return;
  reply = (chttp_server_deferred_response){.size = sizeof(reply),
                                           .status_code = 200u,
                                           .content_type = "text/plain",
                                           .body = body,
                                           .body_size = strlen(body)};
  (void)chttp_server_deferred_reply(deferred, &reply);
}

spec("CHTTP HTTP2 deferred Task 3 bounds") {
  it("retains base headers and retries a bounded deferred copy") {
    chttp_h2_task3_copy_probe probe = {.first_reply_status = SALTS_EIO,
                                       .second_reply_status = SALTS_EIO};
    chttp_h2_task3_completion completion = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request = {0};
    salts_thread_t worker = NULL;
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    probe.deferred = (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
    atomic_init(&probe.acquired, 0);
    server_config.max_buffered_response_body_bytes = 16u;
    client_config.request_capacity = 4u;
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/copy", chttp_h2_task3_copy_handler, &probe), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);
    check_equal(salts_thread_create(&worker, chttp_h2_task3_copy_worker, &probe), SALTS_OK);
    check_equal(chttp_h2_task3_submit(&client, uri, authority, "/copy", &completion, &request),
                SALTS_OK);
    check_equal(chttp_h2_task3_poll_completion(&client, &completion,
                                               CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(salts_thread_join(&worker), SALTS_OK);
    salts_thread_destroy(&worker);

    check_equal(probe.first_reply_status, SALTS_EMSGSIZE);
    check_equal(probe.handle_unchanged, 1);
    check_equal(probe.second_reply_status, SALTS_OK);
    check_equal(completion.calls, (size_t)1u);
    check_equal(completion.status, SALTS_OK);
    check_equal(completion.response_status, 200u);
    check_equal(completion.body, "bounded");
    check_equal(completion.x_base, "before");
    check_equal(completion.x_replace, "new");
    check_equal(completion.x_deferred, "yes");

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("acquires the CHTTP lease before rejecting application admission") {
    chttp_h2_task3_reject_probe probe;
    chttp_h2_task3_completion rejected = {0};
    chttp_h2_task3_completion sibling = {0};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request reject_request = {0};
    chttp_request sibling_request = {0};
    uint16_t port = 0u;
    char uri[64];
    char authority[64];

    memset(&probe, 0, sizeof(probe));
    atomic_init(&probe.defer_status, SALTS_EIO);
    atomic_init(&probe.application_admitted, 1);
    atomic_init(&probe.immediate_reply_status, SALTS_EIO);
    client_config.request_capacity = 4u;
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/reject", chttp_h2_task3_reject_handler, &probe),
                SALTS_OK);
    check_equal(chttp_server_get(&server, "/sibling", chttp_h2_task3_sync_handler, NULL), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);

    check_equal(chttp_h2_task3_submit(&client, uri, authority, "/reject", &rejected,
                                      &reject_request), SALTS_OK);
    check_equal(chttp_h2_task3_poll_completion(&client, &rejected,
                                               CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_h2_task3_submit(&client, uri, authority, "/sibling", &sibling,
                                      &sibling_request), SALTS_OK);
    check_equal(chttp_h2_task3_poll_completion(&client, &sibling,
                                               CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);

    check_equal(atomic_load_explicit(&probe.defer_status, memory_order_acquire), SALTS_OK);
    check_equal(atomic_load_explicit(&probe.application_admitted, memory_order_acquire), 0);
    check_equal(atomic_load_explicit(&probe.immediate_reply_status, memory_order_acquire), SALTS_OK);
    check_equal(probe.copied_target, "/reject");
    check_equal(rejected.calls, (size_t)1u);
    check_equal(rejected.response_status, 503u);
    check_equal(rejected.body, "rejected");
    check_equal(sibling.calls, (size_t)1u);
    check_equal(sibling.response_status, 200u);
    check_equal(sibling.body, "sibling");

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }

  it("reaches the handler for synchronous fallback when the deferred pool is full") {
    chttp_h2_task3_pool_probe probe;
    chttp_h2_task3_completion completion[3] = {{0}};
    chttp_server server = {0};
    chttp_async_client client = {0};
    chttp_server_config server_config = chttp_h2_deferred_server_config();
    chttp_client_config client_config = chttp_h2_deferred_client_config();
    chttp_request request[3] = {{0}};
    uint16_t port = 0u;
    char uri[64];
    char authority[64];
    uint64_t deadline;
    size_t calls;

    memset(&probe, 0, sizeof(probe));
    probe.defer_status[0] = probe.defer_status[1] = probe.defer_status[2] = SALTS_EIO;
    probe.deferred[0] = probe.deferred[1] = probe.deferred[2] =
        (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
    atomic_init(&probe.published_calls, 0u);
    server_config.h2_stream_capacity = 2u;
    client_config.request_capacity = 4u;
    check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
    check_equal(chttp_server_get(&server, "/hold", chttp_h2_task3_pool_handler, &probe), SALTS_OK);
    check_equal(chttp_server_start(&server), SALTS_OK);
    check_equal(chttp_server_port(&server, &port), SALTS_OK);
    check_equal(chttp_h2_deferred_endpoint(port, uri, sizeof(uri), authority, sizeof(authority)),
                SALTS_OK);
    check_equal(chttp_async_client_init(&client, &client_config), SALTS_OK);

    check_equal(chttp_h2_task3_submit(&client, uri, authority, "/hold", &completion[0], &request[0]),
                SALTS_OK);
    check_equal(chttp_h2_task3_submit(&client, uri, authority, "/hold", &completion[1], &request[1]),
                SALTS_OK);
    deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
    while (atomic_load_explicit(&probe.published_calls, memory_order_acquire) < 2u &&
           salts_monotonic_ms() < deadline) {
      size_t completions = 0u;
      check_equal(chttp_async_client_poll(&client, 20u, &completions), SALTS_OK);
    }
    check_equal(atomic_load_explicit(&probe.published_calls, memory_order_acquire), (size_t)2u);
    check_equal(probe.defer_status[0], SALTS_OK);
    check_equal(probe.defer_status[1], SALTS_OK);

    check_equal(chttp_h2_task3_submit(&client, uri, authority, "/hold", &completion[2], &request[2]),
                SALTS_OK);
    deadline = salts_monotonic_ms() + 1000u;
    while (atomic_load_explicit(&probe.published_calls, memory_order_acquire) < 3u &&
           completion[2].calls == 0u && salts_monotonic_ms() < deadline) {
      size_t completions = 0u;
      check_equal(chttp_async_client_poll(&client, 20u, &completions), SALTS_OK);
    }

    /* Normative Task 3 expectation: the third stream reaches the handler, defer fails
       atomically with ENOBUFS, and the same response remains available for sync 503. */
    check_equal(atomic_load_explicit(&probe.published_calls, memory_order_acquire), (size_t)3u);
    check_equal(probe.defer_status[2], SALTS_ENOBUFS);
    check_equal(completion[2].calls, (size_t)1u);
    check_equal(completion[2].response_status, 503u);
    check_equal(completion[2].body, "busy");

    /* Cleanup must be bounded even when the RED proves the third handler was unreachable. */
    chttp_h2_task3_reply_pending(&probe.deferred[0], "one");
    deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
    while (completion[0].calls == 0u && salts_monotonic_ms() < deadline) {
      size_t completions = 0u;
      check_equal(chttp_async_client_poll(&client, 20u, &completions), SALTS_OK);
    }
    deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
    while (atomic_load_explicit(&probe.published_calls, memory_order_acquire) < 3u &&
           salts_monotonic_ms() < deadline) {
      size_t completions = 0u;
      check_equal(chttp_async_client_poll(&client, 20u, &completions), SALTS_OK);
    }
    calls = atomic_load_explicit(&probe.published_calls, memory_order_acquire);
    if (calls >= 3u && probe.defer_status[2] == SALTS_OK)
      chttp_h2_task3_reply_pending(&probe.deferred[2], "three");
    chttp_h2_task3_reply_pending(&probe.deferred[1], "two");
    deadline = salts_monotonic_ms() + CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS;
    while ((completion[0].calls == 0u || completion[1].calls == 0u ||
            (calls >= 3u && probe.defer_status[2] == SALTS_OK && completion[2].calls == 0u)) &&
           salts_monotonic_ms() < deadline) {
      size_t completions = 0u;
      check_equal(chttp_async_client_poll(&client, 20u, &completions), SALTS_OK);
    }

    check_equal(chttp_async_client_stop(&client, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_async_client_destroy(&client), SALTS_OK);
    check_equal(chttp_server_stop(&server, CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(chttp_server_destroy(&server), SALTS_OK);
  }
}
'''

path.write_text(text + append)
