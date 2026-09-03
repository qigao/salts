#include <s3/s3_bucket_config.h>

#include "s3_test_support.h"
#include "tinytest.h"

#include <string.h>

typedef struct s3_bucket_config_probe {
  size_t gets;
  size_t puts;
  size_t deletes;
} s3_bucket_config_probe;

static int64_t s3_bucket_config_clock(void *user) {
  (void)user;
  return INT64_C(1369353600);
}

static int s3_bucket_config_get(void *user, const chttp_server_request_view *request,
                                chttp_server_response *response) {
  s3_bucket_config_probe *probe = (s3_bucket_config_probe *)user;
  const char *body;
  if (probe == NULL || request == NULL) return TURBO_EPROTO;
  if (strcmp(request->target, "/bucket?lifecycle=") == 0) body = "<LifecycleConfiguration/>";
  else if (strcmp(request->target, "/bucket?notification=") == 0)
    body = "<NotificationConfiguration/>";
  else if (strcmp(request->target, "/bucket?replication=") == 0)
    body = "<ReplicationConfiguration/>";
  else return TURBO_EPROTO;
  ++probe->gets;
  return chttp_server_reply(response, 200u, "application/xml", body, strlen(body));
}

static int s3_bucket_config_put(void *user, const chttp_server_request_view *request,
                                chttp_server_response *response) {
  s3_bucket_config_probe *probe = (s3_bucket_config_probe *)user;
  const char *root;
  if (probe == NULL || request == NULL) return TURBO_EPROTO;
  if (strcmp(request->target, "/bucket?lifecycle=") == 0) root = "<LifecycleConfiguration";
  else if (strcmp(request->target, "/bucket?notification=") == 0)
    root = "<NotificationConfiguration";
  else if (strcmp(request->target, "/bucket?replication=") == 0) root = "<ReplicationConfiguration";
  else return TURBO_EPROTO;
  if (request->body == NULL || request->body_size < strlen(root) ||
      memcmp(request->body, root, strlen(root)) != 0 ||
      strcmp(chttp_server_request_header(request, "content-type"), "application/xml") != 0)
    return TURBO_EPROTO;
  ++probe->puts;
  return chttp_server_reply(response, 200u, NULL, NULL, 0u);
}

static int s3_bucket_config_delete(void *user, const chttp_server_request_view *request,
                                   chttp_server_response *response) {
  s3_bucket_config_probe *probe = (s3_bucket_config_probe *)user;
  if (probe == NULL || request == NULL ||
      (strcmp(request->target, "/bucket?lifecycle=") != 0 &&
       strcmp(request->target, "/bucket?replication=") != 0))
    return TURBO_EPROTO;
  ++probe->deletes;
  return chttp_server_reply(response, 204u, NULL, NULL, 0u);
}

static void s3_bucket_config_run(chttp_protocol protocol) {
  static const s3_static_credentials credentials = {"access", "secret", NULL};
  static const char lifecycle[] =
      "<LifecycleConfiguration><Rule><Status>Enabled</Status></Rule></LifecycleConfiguration>";
  static const char notification[] = "<NotificationConfiguration/>";
  static const char replication[] =
      "<ReplicationConfiguration><Role>arn:test</Role></ReplicationConfiguration>";
  chttp_server server = {0};
  chttp_client http_client = {0};
  s3_client client = {0};
  s3_bucket_config_probe probe = {0};
  chttp_server_config server_config = s3_test_server_config();
  chttp_client_config http_config = s3_test_client_config();
  s3_client_config config;
  s3_response response = {0};
  s3_error error = {0};
  char connection_uri[64];
  char authority[64];
  uint16_t port = 0u;

  check_equal(chttp_server_init(&server, &server_config), TURBO_OK);
  check_equal(chttp_server_get(&server, "/bucket", s3_bucket_config_get, &probe), TURBO_OK);
  check_equal(chttp_server_put(&server, "/bucket", s3_bucket_config_put, &probe), TURBO_OK);
  check_equal(chttp_server_delete(&server, "/bucket", s3_bucket_config_delete, &probe), TURBO_OK);
  check_equal(chttp_server_start(&server), TURBO_OK);
  check_equal(chttp_server_port(&server, &port), TURBO_OK);
  check_equal(
      s3_test_endpoint(port, connection_uri, sizeof(connection_uri), authority, sizeof(authority)),
      TURBO_OK);
  check_equal(chttp_client_init(&http_client, &http_config), TURBO_OK);
  config = (s3_client_config){.size = sizeof(config),
                              .connection_uri = connection_uri,
                              .authority = authority,
                              .region = "us-east-1",
                              .addressing_style = S3_ADDRESSING_PATH,
                              .protocol = protocol,
                              .credentials = s3_credentials_provider_static(&credentials),
                              .clock = s3_bucket_config_clock,
                              .timeout_ms = S3_TEST_TIMEOUT_MS};
  check_equal(s3_client_init(&client, &http_client, &config), TURBO_OK);

  check_equal(s3_put_bucket_lifecycle(&client, "bucket", lifecycle, sizeof(lifecycle) - 1u,
                                      &response, &error),
              TURBO_OK);
  s3_response_destroy(&response);
  check_equal(s3_get_bucket_lifecycle(&client, "bucket", &response, &error), TURBO_OK);
  check_equal(response.http.body, "<LifecycleConfiguration/>",
              sizeof("<LifecycleConfiguration/>") - 1u);
  s3_response_destroy(&response);
  check_equal(s3_delete_bucket_lifecycle(&client, "bucket", &response, &error), TURBO_OK);
  s3_response_destroy(&response);

  check_equal(s3_put_bucket_notification(&client, "bucket", notification, sizeof(notification) - 1u,
                                         &response, &error),
              TURBO_OK);
  s3_response_destroy(&response);
  check_equal(s3_get_bucket_notification(&client, "bucket", &response, &error), TURBO_OK);
  s3_response_destroy(&response);

  check_equal(s3_put_bucket_replication(&client, "bucket", replication, sizeof(replication) - 1u,
                                        &response, &error),
              TURBO_OK);
  s3_response_destroy(&response);
  check_equal(s3_get_bucket_replication(&client, "bucket", &response, &error), TURBO_OK);
  s3_response_destroy(&response);
  check_equal(s3_delete_bucket_replication(&client, "bucket", &response, &error), TURBO_OK);
  s3_response_destroy(&response);

  check_equal(s3_put_bucket_lifecycle(&client, "bucket", notification, sizeof(notification) - 1u,
                                      &response, &error),
              TURBO_EPROTO);
  check_equal(probe.puts, (size_t)3u);
  check_equal(probe.gets, (size_t)3u);
  check_equal(probe.deletes, (size_t)2u);
  check_equal(s3_client_destroy(&client), TURBO_OK);
  check_equal(chttp_client_destroy(&http_client, S3_TEST_TIMEOUT_MS), TURBO_OK);
  check_equal(chttp_server_stop(&server, S3_TEST_TIMEOUT_MS), TURBO_OK);
  check_equal(chttp_server_destroy(&server), TURBO_OK);
}

spec("S3 bucket management subresources") {
  it("validates and transports XML over HTTP/1.1") { s3_bucket_config_run(CHTTP_HTTP_1_1); }
  it("validates and transports XML over HTTP/2") { s3_bucket_config_run(CHTTP_HTTP_2); }
}
