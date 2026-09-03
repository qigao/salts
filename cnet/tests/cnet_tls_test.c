#include "cnet_tls.h"
#include "tinytest.h"

#include <salts/clock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char CNET_TLS_TEST_CERTIFICATE[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC7TCCAdWgAwIBAgIUT4pOT+qAkLpsC1bUF3bYRrTHssQwDQYJKoZIhvcNAQEL\n"
    "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDMyMzA4MDcwMloXDTM2MDMy\n"
    "MDA4MDcwMlowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF\n"
    "AAOCAQ8AMIIBCgKCAQEAtNuutQlZVrXBW97HX5HfMXbMkES9n2eglXQRzU7Qg4Mm\n"
    "KtAprpkBVSFHeAti0NyPgasoaoJTBi1xBhDsGTWTto0TJVHhW5QcYSPRc8x/acWQ\n"
    "NxBSMdWf8Rp9QxbaECyQbWr+QDb/c1a9QU0fGFntQBnLfk9lLJG7MRTwg38ufnSk\n"
    "OqqyAtbT4V5ZwImkOo9MdECcZMvRDYnvH1atIUvGRI7O3M466jGe+5WN4E42h8VN\n"
    "PSJw2IBvbFxePZ3yMWpiVRkbsWlq1hJIHGvnGD+4IPGr2nB/FmR+P969KFm/gSvG\n"
    "L9tYFRw36Cfa+cnwWAYNpLspwOaaAQcpeMN8tAGIPwIDAQABozcwNTAUBgNVHREE\n"
    "DTALgglsb2NhbGhvc3QwHQYDVR0OBBYEFHSKGrYW6d59EU5htbnpgVhPLaQiMA0G\n"
    "CSqGSIb3DQEBCwUAA4IBAQBhIzu8IJ7Pm30nKOfvwgQRKbJDWIBKZz/NYoIP5Ljm\n"
    "fZG+ZZT0BnuCObKTvPwAWERwbIn5cIDNCkVKhQoJc4+KqR9fXptxML+Q3e4lCVo3\n"
    "5jjQpG/r18aZxhfroinp6iCfGcECw/JAXPxC8jOhEgVOPQd/LybM9vO8vraH/dIR\n"
    "YRmIoBvGw+wQMt/PcV0GxYLo6LsYJFs0FuJyiufJ2auNtmW5h8qOdtnagmeo0ehp\n"
    "g5VqPlB3EMa/01r9WmfNQJcBbEF8ONhhPXZCV4uplsXGtN8+Xxrzb3SAYQR9xFry\n"
    "x9YTzT8UMLc26vY1RiF6uwODUJzmSaqmefmapVsWrgi3\n"
    "-----END CERTIFICATE-----\n";

static const char CNET_TLS_TEST_KEY[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQC02661CVlWtcFb\n"
    "3sdfkd8xdsyQRL2fZ6CVdBHNTtCDgyYq0CmumQFVIUd4C2LQ3I+BqyhqglMGLXEG\n"
    "EOwZNZO2jRMlUeFblBxhI9FzzH9pxZA3EFIx1Z/xGn1DFtoQLJBtav5ANv9zVr1B\n"
    "TR8YWe1AGct+T2UskbsxFPCDfy5+dKQ6qrIC1tPhXlnAiaQ6j0x0QJxky9ENie8f\n"
    "Vq0hS8ZEjs7czjrqMZ77lY3gTjaHxU09InDYgG9sXF49nfIxamJVGRuxaWrWEkgc\n"
    "a+cYP7gg8avacH8WZH4/3r0oWb+BK8Yv21gVHDfoJ9r5yfBYBg2kuynA5poBByl4\n"
    "w3y0AYg/AgMBAAECggEAEJkoy4yexQp2mHaLAwZhiX9G/uaQJepeHoPsg6nRZoB0\n"
    "JvG7zD5WlPgyQEjV5NKZM7lVmDt7Cydt0V9e4QwTERSZcToL3gUV0FnNMJIlZLuw\n"
    "+fIRg76rUyFZ5aevPlTDXIdj64N1+6E2SqFH/UrOL1fZXoTthXhKdGgLkBtCqnA6\n"
    "DlHQX3lehrnV+MG5fTxPc8lro/s4UVAoBMhc4dP5U1W5Xt5c6RsdcWYytidRYj8t\n"
    "XMkyjST/F2NV80+8WGp/YFE0dHyxGWvLGNmkOUuI4EMwzzSadsIM+PQO/YP1KwHA\n"
    "0DYHuEFvPCLjPsD+7IUnZgifQe45/FJoJMp5hSmzgQKBgQD7XEl2mLR3iqup2dF+\n"
    "PD3zA2J48jdiJdbK7vRLXpdV5WP2/s90GZFKLadg7UWmx9zWkC4B92atNJV0/+8o\n"
    "wE4Zd8PG62QZ3o1T4QpYMem9PAq5OxqwYBxMZ2Y5Mf+54Gp0SXB+AbXPlYI/LIwP\n"
    "i/2Iq+bAjGmuGuloJNWD3Wl3DwKBgQC4MkMYvf5aSqbL8GE5ndKY06HzbxwcMoh3\n"
    "Hia5LRMw5dG3J2JwdruiE4V3gQyqz0NzYrrqqkyYxh3aJW934qj6JVMVw/xWx2n5\n"
    "xB4X4hcCKrO2piROmOuXBEt1T36C+fShNb8g+RNY0edoiw+OKTa3rzlQhggTkoGs\n"
    "Iy7oyxtb0QKBgGKkgfP304LCOcHrSCppC8qtflyGebObs+Jpyhc15OABqKxKrTEb\n"
    "w4e/yNrh4p6j+od9h4CgDXxVkX2b3sg4R6348SzEPcFlNENBomSgGeF4iaDNkBi9\n"
    "bv2Q6m3xsDDK4BwIogvhMe9n9fhCzChhwLp8846GzAZWa1jCc8RPBM+DAoGAQxRy\n"
    "4QDYL5O+OMka7zutpWB1O008hHxWvGKroYZr1cPsYvIh5GkpHfZUBdhmf5Ips0zC\n"
    "W5GXgY+s8XPuq09NUIPlRSjxrbzDuGUWvIXm8TAR8LOCx2jja0TyIg/IN/TFhSwo\n"
    "pd5vkEopJyZ1jMUvmydiDRQyvsX9GW5auAa3uPECgYBxuBJ6Vji7pxlqjG3aB0je\n"
    "+JexLyzdckU7EKTxpTSU1o/p17QpT26KF+DPMc2kg+PBK+Sjm0m4Uxdzq/OXNMMA\n"
    "zhR6Vjo1nPWsKgzK03hGzaJVMkHekgCidY9R+MZEeDAhHDIia9XyAS1qCoGAJ6WC\n"
    "oYB4EuDLFhurWiLO+diuMg==\n"
    "-----END PRIVATE KEY-----\n";

typedef struct cnet_tls_test_pair {
  cnet_tls_server server_context;
  cnet_tls_state client;
  cnet_tls_state server;
  char *cert_path;
  char *key_path;
} cnet_tls_test_pair;

static int cnet_tls_test_transfer(cnet_tls_state *source, cnet_tls_state *target) {
  unsigned char buffer[1024];
  for (;;) {
    size_t size = 0u;
    int status = cnet_tls_take_cipher(source, buffer, sizeof(buffer), &size);
    if (status == SALTS_ENOENT) return SALTS_OK;
    if (status != SALTS_OK) return status;
    if (size == 0u) return SALTS_EPROTO;
    status = cnet_tls_feed_cipher(target, buffer, size);
    if (status != SALTS_OK) return status;
  }
}

static int cnet_tls_test_pair_init(cnet_tls_test_pair *pair) {
  static const char *server_alpn[] = {"h2", "http/1.1"};
  static const char *client_alpn[] = {"http/1.1", "h2"};
  cnet_tls_server_config server_config;
  cnet_tls_client_config client_config;
  cnet_tls_context *client_context = NULL;
  cnet_tls_context *server_context;
  int status;

  memset(pair, 0, sizeof(*pair));
  pair->cert_path = tt_make_temp_file("cnet-cert", ".pem");
  pair->key_path = tt_make_temp_file("cnet-key", ".pem");
  if (pair->cert_path == NULL || pair->key_path == NULL) return SALTS_ENOMEM;
  if (tt_write_file(pair->cert_path, CNET_TLS_TEST_CERTIFICATE,
                    sizeof(CNET_TLS_TEST_CERTIFICATE) - 1u) != 0 ||
      tt_write_file(pair->key_path, CNET_TLS_TEST_KEY, sizeof(CNET_TLS_TEST_KEY) - 1u) != 0)
    return SALTS_EIO;

  server_config = (cnet_tls_server_config){.size = sizeof(server_config),
                                           .cert_file = pair->cert_path,
                                           .key_file = pair->key_path,
                                           .client_auth = CNET_TLS_CLIENT_AUTH_NONE,
                                           .alpn_protocols = server_alpn,
                                           .alpn_protocol_count = 2u};
  status = cnet_tls_server_init(&pair->server_context, &server_config);
  if (status != SALTS_OK) return status;
  client_config = (cnet_tls_client_config){.size = sizeof(client_config),
                                           .ca_file = pair->cert_path,
                                           .alpn_protocols = client_alpn,
                                           .alpn_protocol_count = 2u};
  status = cnet_tls_client_context_create(&client_config, &client_context);
  if (status != SALTS_OK) return status;
  status = cnet_tls_state_init(&pair->client, client_context, false, "localhost",
                               CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) {
    cnet_tls_context_release(client_context);
    return status;
  }
  server_context = cnet_tls_server_context(&pair->server_context);
  cnet_tls_context_retain(server_context);
  status =
      cnet_tls_state_init(&pair->server, server_context, true, NULL, CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) cnet_tls_context_release(server_context);
  return status;
}

static void cnet_tls_test_pair_destroy(cnet_tls_test_pair *pair) {
  cnet_tls_state_destroy(&pair->server);
  cnet_tls_state_destroy(&pair->client);
  (void)cnet_tls_server_destroy(&pair->server_context);
  if (pair->cert_path != NULL) {
    (void)tt_remove_file(pair->cert_path);
    free(pair->cert_path);
  }
  if (pair->key_path != NULL) {
    (void)tt_remove_file(pair->key_path);
    free(pair->key_path);
  }
}

static int cnet_tls_test_handshake(cnet_tls_test_pair *pair) {
  size_t iteration;
  for (iteration = 0u; iteration < 256u; ++iteration) {
    bool client_complete = false;
    bool server_complete = false;
    int status = cnet_tls_handshake(&pair->client, &client_complete);
    if (status != SALTS_OK) return status;
    status = cnet_tls_test_transfer(&pair->client, &pair->server);
    if (status != SALTS_OK) return status;
    status = cnet_tls_handshake(&pair->server, &server_complete);
    if (status != SALTS_OK) return status;
    status = cnet_tls_test_transfer(&pair->server, &pair->client);
    if (status != SALTS_OK) return status;
    if (client_complete && server_complete) return SALTS_OK;
  }
  return SALTS_ETIMEDOUT;
}

static int cnet_tls_test_reset_client(cnet_tls_test_pair *pair,
                                      const cnet_tls_client_config *config,
                                      const char *server_name) {
  cnet_tls_context *context = NULL;
  int status;
  cnet_tls_state_destroy(&pair->client);
  status = cnet_tls_client_context_create(config, &context);
  if (status != SALTS_OK) return status;
  status =
      cnet_tls_state_init(&pair->client, context, false, server_name, CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) cnet_tls_context_release(context);
  return status;
}

typedef struct cnet_tls_network_probe {
  cnet_client *client;
  cnet_connection connection;
  char received[16];
  char alpn[16];
  size_t received_size;
  size_t alpn_size;
  int alpn_status;
  int connected;
  int sent;
  int terminal;
  int failed;
  int failure_status;
  const char *failure_stage;
} cnet_tls_network_probe;

static void cnet_tls_network_state(void *user, cnet_connection connection,
                                   cnet_connection_state state, const cnet_error *error) {
  cnet_tls_network_probe *probe = (cnet_tls_network_probe *)user;
  probe->connection = connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    probe->connected = 1;
    probe->alpn_status = cnet_tls_negotiated_alpn(probe->client, connection, probe->alpn,
                                                  sizeof(probe->alpn), &probe->alpn_size);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    probe->terminal = 1;
    if (state == CNET_CONNECTION_FAILED || error != NULL) {
      probe->failed = 1;
      if (error != NULL) {
        probe->failure_status = error->status;
        probe->failure_stage = error->stage;
      }
    }
  }
}

static void cnet_tls_network_receive(void *user, cnet_connection connection,
                                     const cnet_receive_view *view) {
  cnet_tls_network_probe *probe = (cnet_tls_network_probe *)user;
  (void)connection;
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES || view->size > sizeof(probe->received)) {
    probe->failed = 1;
    return;
  }
  memcpy(probe->received, view->data, view->size);
  probe->received_size = view->size;
}

static void cnet_tls_network_send(void *user, cnet_connection connection, size_t size) {
  cnet_tls_network_probe *probe = (cnet_tls_network_probe *)user;
  (void)connection;
  if (size == 0u) probe->failed = 1;
  ++probe->sent;
}

static cnet_client_config cnet_tls_network_config(void) {
  const cnet_client_config config = {.backend =
#if defined(_WIN32)
                                         NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                         NATIVE_IO_BACKEND_EPOLL,
#else
                                         NATIVE_IO_BACKEND_KQUEUE,
#endif
                                     .connection_capacity = 2u,
                                     .command_capacity = 16u,
                                     .request_capacity = 8u,
                                     .completion_batch_capacity = 8u,
                                     .event_capacity = 16u,
                                     .max_send_bytes = 1024u,
                                     .receive_buffer_bytes = 1024u,
                                     .connect_timeout_ms = 2000u,
                                     .read_timeout_ms = 2000u,
                                     .write_timeout_ms = 2000u,
                                     .tls_io_buffer_bytes = CNET_TLS_MIN_IO_BUFFER_BYTES,
                                     .tls_handshake_timeout_ms = 2000u};
  return config;
}

static int cnet_tls_network_drive(cnet_client *client, cnet_client *server, cnet_listener *listener,
                                  cnet_tls_server *tls_server, cnet_tls_network_probe *server_probe,
                                  bool *accepted) {
  size_t events = 0u;
  int ready = 0;
  int status = cnet_client_poll(client, 1u, &events);
  if (status != SALTS_OK) return status;
  if (!*accepted) {
    status = cnet_listener_wait(listener, 0u, &ready);
    if (status != SALTS_OK) return status;
    if (ready != 0) {
      const cnet_observer observer = {.on_state = cnet_tls_network_state,
                                      .on_receive = cnet_tls_network_receive,
                                      .user = server_probe,
                                      .on_send = cnet_tls_network_send};
      status = cnet_listener_accept_tls(listener, server, tls_server, &observer,
                                        &server_probe->connection);
      if (status != SALTS_OK) return status;
      *accepted = true;
    }
  }
  return cnet_client_poll(server, 1u, &events);
}

spec("CNet bounded TLS engine") {
  it("verifies localhost negotiates server-preferred ALPN and carries bytes") {
    static const char request[] = "ping";
    cnet_tls_test_pair pair;
    const unsigned char *alpn = NULL;
    unsigned char plaintext[16];
    size_t alpn_size = 0u;
    size_t plaintext_size = 0u;
    bool complete = false;
    bool peer_closed = false;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    check_equal(cnet_tls_get_negotiated_alpn(&pair.client, &alpn, &alpn_size), SALTS_OK);
    check_equal(alpn_size, (size_t)2u);
    check_equal(memcmp(alpn, "h2", 2u), 0);
    check_equal(cnet_tls_get_negotiated_alpn(&pair.server, &alpn, &alpn_size), SALTS_OK);
    check_equal(alpn_size, (size_t)2u);
    check_equal(memcmp(alpn, "h2", 2u), 0);

    check_equal(cnet_tls_write(&pair.client, request, sizeof(request) - 1u, &complete), SALTS_OK);
    check_true(complete);
    check_equal(cnet_tls_test_transfer(&pair.client, &pair.server), SALTS_OK);
    check_equal(
        cnet_tls_read(&pair.server, plaintext, sizeof(plaintext), &plaintext_size, &peer_closed),
        SALTS_OK);
    check_false(peer_closed);
    check_equal(plaintext_size, sizeof(request) - 1u);
    check_equal(memcmp(plaintext, request, sizeof(request) - 1u), 0);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("exchanges close-notify without treating clean EOF as plaintext") {
    cnet_tls_test_pair pair;
    unsigned char plaintext[16];
    size_t plaintext_size = 0u;
    bool notify_generated = false;
    bool peer_closed = false;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    check_equal(cnet_tls_shutdown(&pair.client, &notify_generated), SALTS_OK);
    check_true(notify_generated);
    check_equal(cnet_tls_test_transfer(&pair.client, &pair.server), SALTS_OK);
    check_equal(
        cnet_tls_read(&pair.server, plaintext, sizeof(plaintext), &plaintext_size, &peer_closed),
        SALTS_OK);
    check_true(peer_closed);
    check_equal(plaintext_size, (size_t)0u);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("rejects a trusted certificate whose identity does not match") {
    cnet_tls_test_pair pair;
    cnet_tls_client_config config;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    config = (cnet_tls_client_config){.size = sizeof(config), .ca_file = pair.cert_path};
    check_equal(cnet_tls_test_reset_client(&pair, &config, "example.com"), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_ECONNABORTED);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("rejects a self-signed certificate outside the configured trust store") {
    cnet_tls_test_pair pair;
    cnet_tls_client_config config = {.size = sizeof(config)};

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_test_reset_client(&pair, &config, "localhost"), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_ECONNABORTED);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("requires and verifies a configured client certificate") {
    cnet_tls_test_pair pair;
    cnet_tls_server_config server_config;
    cnet_tls_client_config client_config;
    cnet_tls_context *server_context;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    cnet_tls_state_destroy(&pair.client);
    cnet_tls_state_destroy(&pair.server);
    check_equal(cnet_tls_server_destroy(&pair.server_context), SALTS_OK);
    server_config = (cnet_tls_server_config){.size = sizeof(server_config),
                                             .cert_file = pair.cert_path,
                                             .key_file = pair.key_path,
                                             .ca_file = pair.cert_path,
                                             .client_auth = CNET_TLS_CLIENT_AUTH_REQUIRED};
    check_equal(cnet_tls_server_init(&pair.server_context, &server_config), SALTS_OK);
    client_config = (cnet_tls_client_config){.size = sizeof(client_config),
                                             .ca_file = pair.cert_path,
                                             .cert_file = pair.cert_path,
                                             .key_file = pair.key_path};
    check_equal(cnet_tls_test_reset_client(&pair, &client_config, "localhost"), SALTS_OK);
    server_context = cnet_tls_server_context(&pair.server_context);
    cnet_tls_context_retain(server_context);
    check_equal(
        cnet_tls_state_init(&pair.server, server_context, true, NULL, CNET_TLS_MIN_IO_BUFFER_BYTES),
        SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("drives verified TLS and ALPN through the public listener and client APIs") {
    static const char request[] = "ping";
    static const char response[] = "pong";
    static const char *server_alpn[] = {"h2", "http/1.1"};
    static const char *client_alpn[] = {"http/1.1", "h2"};
    char request_first[] = "pi";
    char request_second[] = "ng";
    cnet_const_buffer request_segments[] = {
        {request_first, sizeof(request_first) - 1u},
        {request_second, sizeof(request_second) - 1u}};
    cnet_client client = {0};
    cnet_client server = {0};
    cnet_listener listener = {0};
    cnet_tls_client tls_client = {0};
    cnet_tls_server tls_server = {0};
    cnet_client_config client_config = cnet_tls_network_config();
    cnet_client_config server_client_config = cnet_tls_network_config();
    cnet_listener_config listener_config = {
        .backend = client_config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    cnet_tls_server_config tls_server_config;
    cnet_tls_client_config tls_client_config;
    cnet_tls_network_probe client_probe = {.client = &client};
    cnet_tls_network_probe server_probe = {.client = &server};
    cnet_connect_options connect_options;
    cnet_connection client_connection = {0};
    char peer_certificate_sha256[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY] = {0};
    char server_name[] = "localhost";
    char *cert_path = tt_make_temp_file("cnet-cert", ".pem");
    char *key_path = tt_make_temp_file("cnet-key", ".pem");
    char uri[64];
    uint16_t port = 0u;
    uint64_t deadline;
    bool accepted = false;

    check_not_null(cert_path);
    check_not_null(key_path);
    check_equal(
        tt_write_file(cert_path, CNET_TLS_TEST_CERTIFICATE, sizeof(CNET_TLS_TEST_CERTIFICATE) - 1u),
        0);
    check_equal(tt_write_file(key_path, CNET_TLS_TEST_KEY, sizeof(CNET_TLS_TEST_KEY) - 1u), 0);
    tls_server_config = (cnet_tls_server_config){.size = sizeof(tls_server_config),
                                                 .cert_file = cert_path,
                                                 .key_file = key_path,
                                                 .client_auth = CNET_TLS_CLIENT_AUTH_NONE,
                                                 .alpn_protocols = server_alpn,
                                                 .alpn_protocol_count = 2u};
    tls_client_config = (cnet_tls_client_config){.size = sizeof(tls_client_config),
                                                 .ca_file = cert_path,
                                                 .server_name = server_name,
                                                 .alpn_protocols = client_alpn,
                                                 .alpn_protocol_count = 2u};

    check_equal(cnet_tls_server_init(&tls_server, &tls_server_config), SALTS_OK);
    check_equal(cnet_tls_client_init(&tls_client, &tls_client_config), SALTS_OK);
    server_name[0] = 'x';
    check_equal(cnet_client_init(&client, &client_config), SALTS_OK);
    check_equal(cnet_client_init(&server, &server_client_config), SALTS_OK);
    check_equal(cnet_listener_init(&listener, &listener_config), SALTS_OK);
    check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tls://127.0.0.1:%u", (unsigned int)port), 0);
    connect_options = (cnet_connect_options){.uri = uri,
                                             .observer = {.on_state = cnet_tls_network_state,
                                                          .on_receive = cnet_tls_network_receive,
                                                          .user = &client_probe,
                                                          .on_send = cnet_tls_network_send},
                                             .tls_client = &tls_client};
    check_equal(cnet_connect(&client, &connect_options, &client_connection), SALTS_OK);
    check_equal(cnet_tls_client_destroy(&tls_client), SALTS_OK);
    check_null(tls_client.impl);
    client_probe.connection = client_connection;

    deadline = salts_monotonic_ms() + 5000u;
    while ((!client_probe.connected || !server_probe.connected) && salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_true(accepted);
    check_true(client_probe.connected);
    check_true(server_probe.connected);
    check_equal(client_probe.alpn_status, SALTS_OK);
    check_equal(server_probe.alpn_status, SALTS_OK);
    check_equal(client_probe.alpn_size, (size_t)2u);
    check_equal(server_probe.alpn_size, (size_t)2u);
    check_equal(memcmp(client_probe.alpn, "h2", 2u), 0);
    check_equal(memcmp(server_probe.alpn, "h2", 2u), 0);
    check_equal(cnet_tls_peer_certificate_sha256(&client, client_connection,
                                                 peer_certificate_sha256),
                SALTS_OK);
    check_equal(strcmp(peer_certificate_sha256,
                       "ebd76f304bc43bc2be697fca2f054206978c0558931529a7c1b2bb7d82a7a3c4"),
                0);
    check_equal(cnet_tls_peer_certificate_sha256(&server, server_probe.connection,
                                                 peer_certificate_sha256),
                SALTS_ENOENT);

    check_equal(cnet_receive(&server, server_probe.connection, 1u), SALTS_OK);
    check_equal(cnet_sendv(&client, client_connection, request_segments, 2u), SALTS_OK);
    memset(request_first, 'x', sizeof(request_first) - 1u);
    memset(request_second, 'x', sizeof(request_second) - 1u);
    deadline = salts_monotonic_ms() + 5000u;
    while ((server_probe.received_size == 0u || client_probe.sent == 0) &&
           salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_equal(server_probe.received_size, sizeof(request) - 1u);
    check_equal(memcmp(server_probe.received, request, sizeof(request) - 1u), 0);
    check_equal(client_probe.sent, 1);

    check_equal(cnet_receive(&client, client_connection, 1u), SALTS_OK);
    check_equal(cnet_send(&server, server_probe.connection, response, sizeof(response) - 1u),
                SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((client_probe.received_size == 0u || server_probe.sent == 0) &&
           salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_equal(client_probe.received_size, sizeof(response) - 1u);
    check_equal(memcmp(client_probe.received, response, sizeof(response) - 1u), 0);
    check_equal(server_probe.sent, 1);

    check_equal(cnet_receive(&server, server_probe.connection, 1u), SALTS_OK);
    check_equal(cnet_close(&client, client_connection), SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((!client_probe.terminal || !server_probe.terminal) && salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_true(client_probe.terminal);
    check_true(server_probe.terminal);
    check_false(client_probe.failed);
    check_false(server_probe.failed);
    check_equal(cnet_tls_peer_certificate_sha256(&client, client_connection,
                                                 peer_certificate_sha256),
                SALTS_ENOENT);

    check_equal(cnet_listener_close(&listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    check_equal(cnet_client_stop(&client, 5000u), SALTS_OK);
    check_equal(cnet_client_stop(&server, 5000u), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
    check_equal(cnet_client_destroy(&server), SALTS_OK);
    check_equal(cnet_tls_server_destroy(&tls_server), SALTS_OK);
    check_equal(tt_remove_file(cert_path), 0);
    check_equal(tt_remove_file(key_path), 0);
    free(cert_path);
    free(key_path);
  }

  it("times out a silent peer in the handshake stage without downgrading") {
    cnet_client client = {0};
    cnet_client raw_server = {0};
    cnet_listener listener = {0};
    cnet_client_config client_config = cnet_tls_network_config();
    cnet_client_config server_config = cnet_tls_network_config();
    cnet_listener_config listener_config = {
        .backend = client_config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    cnet_tls_client_config tls_config;
    cnet_tls_network_probe client_probe = {.client = &client};
    cnet_tls_network_probe server_probe = {.client = &raw_server};
    cnet_observer server_observer = {.on_state = cnet_tls_network_state,
                                     .on_receive = cnet_tls_network_receive,
                                     .user = &server_probe,
                                     .on_send = cnet_tls_network_send};
    cnet_connect_options options;
    cnet_connection client_connection = {0};
    cnet_connection server_connection = {0};
    char *cert_path = tt_make_temp_file("cnet-cert", ".pem");
    char uri[64];
    uint16_t port = 0u;
    uint64_t deadline;
    bool accepted = false;

    check_not_null(cert_path);
    check_equal(
        tt_write_file(cert_path, CNET_TLS_TEST_CERTIFICATE, sizeof(CNET_TLS_TEST_CERTIFICATE) - 1u),
        0);
    client_config.tls_handshake_timeout_ms = 20u;
    server_config.tls_io_buffer_bytes = 0u;
    server_config.tls_handshake_timeout_ms = 0u;
    tls_config = (cnet_tls_client_config){
        .size = sizeof(tls_config), .ca_file = cert_path, .server_name = "localhost"};
    check_equal(cnet_client_init(&client, &client_config), SALTS_OK);
    check_equal(cnet_client_init(&raw_server, &server_config), SALTS_OK);
    check_equal(cnet_listener_init(&listener, &listener_config), SALTS_OK);
    check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tls://127.0.0.1:%u", (unsigned int)port), 0);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_tls_network_state,
                                                  .on_receive = cnet_tls_network_receive,
                                                  .user = &client_probe,
                                                  .on_send = cnet_tls_network_send},
                                     .tls = &tls_config};
    check_equal(cnet_connect(&client, &options, &client_connection), SALTS_OK);
    deadline = salts_monotonic_ms() + 2000u;
    while (!client_probe.terminal && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      int ready = 0;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      if (!accepted) {
        check_equal(cnet_listener_wait(&listener, 0u, &ready), SALTS_OK);
        if (ready != 0) {
          check_equal(
              cnet_listener_accept(&listener, &raw_server, &server_observer, &server_connection),
              SALTS_OK);
          server_probe.connection = server_connection;
          accepted = true;
        }
      }
      check_equal(cnet_client_poll(&raw_server, 1u, &events), SALTS_OK);
    }
    check_true(accepted);
    check_true(client_probe.terminal);
    check_true(client_probe.failed);
    check_equal(client_probe.failure_status, SALTS_ETIMEDOUT);
    check_equal(strcmp(client_probe.failure_stage, "handshake"), 0);
    check_false(client_probe.connected);

    if (!server_probe.terminal) check_equal(cnet_close(&raw_server, server_connection), SALTS_OK);
    check_equal(cnet_listener_close(&listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    check_equal(cnet_client_stop(&client, 5000u), SALTS_OK);
    check_equal(cnet_client_stop(&raw_server, 5000u), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
    check_equal(cnet_client_destroy(&raw_server), SALTS_OK);
    check_equal(tt_remove_file(cert_path), 0);
    free(cert_path);
  }

  it("cancels a handshake without publishing a connection or failure") {
    cnet_client client = {0};
    cnet_client raw_server = {0};
    cnet_listener listener = {0};
    cnet_client_config client_config = cnet_tls_network_config();
    cnet_client_config server_config = cnet_tls_network_config();
    cnet_listener_config listener_config = {
        .backend = client_config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    cnet_tls_client_config tls_config = {.size = sizeof(tls_config), .server_name = "localhost"};
    cnet_tls_network_probe client_probe = {.client = &client};
    cnet_tls_network_probe server_probe = {.client = &raw_server};
    cnet_observer server_observer = {.on_state = cnet_tls_network_state,
                                     .on_receive = cnet_tls_network_receive,
                                     .user = &server_probe,
                                     .on_send = cnet_tls_network_send};
    cnet_connect_options options;
    cnet_connection client_connection = {0};
    cnet_connection server_connection = {0};
    char uri[64];
    uint16_t port = 0u;
    uint64_t deadline;
    bool accepted = false;

    server_config.tls_io_buffer_bytes = 0u;
    server_config.tls_handshake_timeout_ms = 0u;
    check_equal(cnet_client_init(&client, &client_config), SALTS_OK);
    check_equal(cnet_client_init(&raw_server, &server_config), SALTS_OK);
    check_equal(cnet_listener_init(&listener, &listener_config), SALTS_OK);
    check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tls://127.0.0.1:%u", (unsigned int)port), 0);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_tls_network_state,
                                                  .on_receive = cnet_tls_network_receive,
                                                  .user = &client_probe,
                                                  .on_send = cnet_tls_network_send},
                                     .tls = &tls_config};
    check_equal(cnet_connect(&client, &options, &client_connection), SALTS_OK);

    deadline = salts_monotonic_ms() + 2000u;
    while (!accepted && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      int ready = 0;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_listener_wait(&listener, 0u, &ready), SALTS_OK);
      if (ready != 0) {
        check_equal(
            cnet_listener_accept(&listener, &raw_server, &server_observer, &server_connection),
            SALTS_OK);
        server_probe.connection = server_connection;
        accepted = true;
      }
      check_equal(cnet_client_poll(&raw_server, 1u, &events), SALTS_OK);
    }
    check_true(accepted);
    check_false(client_probe.connected);
    check_equal(cnet_close(&client, client_connection), SALTS_OK);

    deadline = salts_monotonic_ms() + 2000u;
    while (!client_probe.terminal && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&raw_server, 1u, &events), SALTS_OK);
    }
    check_true(client_probe.terminal);
    check_false(client_probe.connected);
    check_false(client_probe.failed);

    if (!server_probe.terminal) check_equal(cnet_close(&raw_server, server_connection), SALTS_OK);
    check_equal(cnet_listener_close(&listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    check_equal(cnet_client_stop(&client, 5000u), SALTS_OK);
    check_equal(cnet_client_stop(&raw_server, 5000u), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
    check_equal(cnet_client_destroy(&raw_server), SALTS_OK);
  }
}
