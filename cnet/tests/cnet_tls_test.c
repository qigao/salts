#include "cnet_tls.h"
#include "tinytest.h"

#include <salts/clock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cnet_tls_test_send_bytes(cnet_client *client,
                                    cnet_connection connection,
                                    const void *data, size_t size) {
  mem_buffer_t *buffer;
  int status;
  if (client == NULL || data == NULL || size == 0u) return SALTS_EINVAL;
  buffer = mem_get_buffer(mem_global(), size);
  if (buffer == NULL) return SALTS_ENOMEM;
  memcpy(mem_buffer_data(buffer), data, size);
  mem_set_used(buffer, size);
  status = cnet_send_buffer(client, connection, buffer);
  mem_buffer_release(buffer);
  return status;
}

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

static int cnet_tls_test_ip_pair_init(cnet_tls_test_pair *pair,
                                      const char *server_name) {
  cnet_tls_server_config server_config;
  cnet_tls_client_config client_config;
  cnet_tls_context *client_context = NULL;
  cnet_tls_context *server_context;
  int status;

  if (pair == NULL || server_name == NULL) return SALTS_EINVAL;
  memset(pair, 0, sizeof(*pair));

  server_config = (cnet_tls_server_config){
      .size = sizeof(server_config),
      .cert_file = CNET_TLS_TEST_IP_CERT,
      .key_file = CNET_TLS_TEST_IP_KEY,
      .client_auth = CNET_TLS_CLIENT_AUTH_NONE};
  status = cnet_tls_server_init(&pair->server_context, &server_config);
  if (status != SALTS_OK) return status;

  client_config = (cnet_tls_client_config){
      .size = sizeof(client_config),
      .ca_file = CNET_TLS_TEST_IP_CA};
  status = cnet_tls_client_context_create(&client_config, &client_context);
  if (status != SALTS_OK) {
    (void)cnet_tls_server_destroy(&pair->server_context);
    return status;
  }
  status = cnet_tls_state_init(&pair->client, client_context, false,
                               server_name, CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) {
    cnet_tls_context_release(client_context);
    (void)cnet_tls_server_destroy(&pair->server_context);
    return status;
  }

  server_context = cnet_tls_server_context(&pair->server_context);
  cnet_tls_context_retain(server_context);
  status = cnet_tls_state_init(&pair->server, server_context, true, NULL,
                               CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) {
    cnet_tls_context_release(server_context);
    cnet_tls_state_destroy(&pair->client);
    (void)cnet_tls_server_destroy(&pair->server_context);
  }
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
  char tls_version[16];
  char tls_cipher[128];
  size_t received_size;
  size_t alpn_size;
  size_t tls_version_size;
  size_t tls_cipher_size;
  int alpn_status;
  int tls_version_status;
  int tls_cipher_status;
  int connected;
  int connected_count;
  int handshaking;
  int sent;
  int terminal;
  int failed;
  int failure_status;
  const char *failure_stage;
  mem_slice_t owned_slice;
  cnet_message_kind owned_kind;
  size_t owned_received_size;
} cnet_tls_network_probe;

static void cnet_tls_network_state(void *user, cnet_connection connection,
                                   cnet_connection_state state, const cnet_error *error) {
  cnet_tls_network_probe *probe = (cnet_tls_network_probe *)user;
  probe->connection = connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    probe->connected = 1;
    ++probe->connected_count;
    probe->alpn_status = cnet_tls_negotiated_alpn(probe->client, connection, probe->alpn,
                                                  sizeof(probe->alpn), &probe->alpn_size);
    probe->tls_version_status =
        cnet_tls_negotiated_version(probe->client, connection, probe->tls_version,
                                    sizeof(probe->tls_version), &probe->tls_version_size);
    probe->tls_cipher_status =
        cnet_tls_negotiated_cipher(probe->client, connection, probe->tls_cipher,
                                   sizeof(probe->tls_cipher), &probe->tls_cipher_size);
  } else if (state == CNET_CONNECTION_TLS_HANDSHAKING) {
    ++probe->handshaking;
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
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
      view->size > sizeof(probe->received) - probe->received_size) {
    probe->failed = 1;
    return;
  }
  memcpy(probe->received + probe->received_size, view->data, view->size);
  probe->received_size += view->size;
}

static void cnet_tls_network_receive_owned(void *user,
                                           cnet_connection connection,
                                           mem_slice_t slice,
                                           cnet_message_kind kind) {
  cnet_tls_network_probe *probe = (cnet_tls_network_probe *)user;
  (void)connection;
  if (kind != CNET_MESSAGE_BYTES || slice.buffer == NULL ||
      slice.data == NULL || slice.length == 0u ||
      probe->owned_slice.buffer != NULL) {
    probe->failed = 1;
    mem_slice_release(&slice);
    return;
  }
  probe->owned_slice = slice;
  probe->owned_kind = kind;
  probe->owned_received_size = slice.length;
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

  it("preserves the full TLS ALPN wire bound instead of a provider count cap") {
    static const char *five_protocols[] = {"p1", "p2", "p3", "p4", "p5"};
    char max_name[256];
    const char *overflow_protocols[256];
    cnet_tls_client_config config = {
        .size = sizeof(config),
        .ca_file = CNET_TLS_TEST_IP_CA,
        .alpn_protocols = five_protocols,
        .alpn_protocol_count = sizeof(five_protocols) / sizeof(five_protocols[0])};
    cnet_tls_context *context = NULL;
    size_t index;

    check_equal(cnet_tls_client_context_create(&config, &context), SALTS_OK);
    check_not_null(context);
    cnet_tls_context_release(context);

    memset(max_name, 'a', sizeof(max_name) - 1u);
    max_name[sizeof(max_name) - 1u] = '\0';
    for (index = 0u; index < sizeof(overflow_protocols) / sizeof(overflow_protocols[0]);
         ++index)
      overflow_protocols[index] = max_name;
    config.alpn_protocols = overflow_protocols;
    config.alpn_protocol_count =
        sizeof(overflow_protocols) / sizeof(overflow_protocols[0]);
    context = NULL;
    check_equal(cnet_tls_client_context_create(&config, &context), SALTS_ERANGE);
    check_null(context);
  }

  it("verifies an IP literal against subjectAltName iPAddress without SNI") {
    cnet_tls_test_pair pair;
    check_equal(cnet_tls_test_ip_pair_init(&pair, "127.0.0.1"), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("rejects a mismatched IP literal without falling back to DNS identity") {
    cnet_tls_test_pair pair;
    check_equal(cnet_tls_test_ip_pair_init(&pair, "127.0.0.2"), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_ECONNABORTED);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("reports negotiated TLS protocol and cipher only after handshake") {
    cnet_tls_test_pair pair;
    char client_version[16] = {0};
    char server_version[16] = {0};
    char client_cipher[128] = {0};
    char server_cipher[128] = {0};
    size_t client_version_size = 0u;
    size_t server_version_size = 0u;
    size_t client_cipher_size = 0u;
    size_t server_cipher_size = 0u;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_state_negotiated_version(&pair.client, client_version,
                                                  sizeof(client_version), &client_version_size),
                SALTS_ENOTCONN);
    check_equal(client_version_size, (size_t)0u);
    check_equal(cnet_tls_state_negotiated_cipher(&pair.client, client_cipher,
                                                 sizeof(client_cipher), &client_cipher_size),
                SALTS_ENOTCONN);
    check_equal(client_cipher_size, (size_t)0u);

    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    check_equal(cnet_tls_state_negotiated_version(&pair.client, client_version,
                                                  sizeof(client_version), &client_version_size),
                SALTS_OK);
    check_equal(cnet_tls_state_negotiated_version(&pair.server, server_version,
                                                  sizeof(server_version), &server_version_size),
                SALTS_OK);
    check_equal(strcmp(client_version, server_version), 0);
    check_true(strcmp(client_version, "TLSv1.2") == 0 ||
               strcmp(client_version, "TLSv1.3") == 0);
    check_equal(client_version_size, strlen(client_version));
    check_equal(server_version_size, strlen(server_version));

    check_equal(cnet_tls_state_negotiated_cipher(&pair.client, client_cipher,
                                                 sizeof(client_cipher), &client_cipher_size),
                SALTS_OK);
    check_equal(cnet_tls_state_negotiated_cipher(&pair.server, server_cipher,
                                                 sizeof(server_cipher), &server_cipher_size),
                SALTS_OK);
    check_equal(strcmp(client_cipher, server_cipher), 0);
    check_greater(client_cipher_size, (size_t)0u);
    check_equal(client_cipher_size, strlen(client_cipher));
    check_equal(server_cipher_size, strlen(server_cipher));

    check_equal(cnet_tls_state_negotiated_version(&pair.client, client_version, 1u,
                                                  &client_version_size),
                SALTS_EMSGSIZE);
    check_equal(client_version_size, (size_t)0u);
    cnet_tls_test_pair_destroy(&pair);
  }

  it("continues to negotiate TLS 1.2 when both peers cap at TLS 1.2") {
    cnet_tls_test_pair pair;
    char version[16] = {0};
    size_t version_size = 0u;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_state_set_protocol_range(&pair.client,
                                                   CNET_TLS_PROTOCOL_VERSION_DEFAULT,
                                                   CNET_TLS_PROTOCOL_VERSION_1_2),
                SALTS_OK);
    check_equal(cnet_tls_state_set_protocol_range(&pair.server,
                                                   CNET_TLS_PROTOCOL_VERSION_DEFAULT,
                                                   CNET_TLS_PROTOCOL_VERSION_1_2),
                SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    check_equal(cnet_tls_state_negotiated_version(&pair.client, version, sizeof(version),
                                                  &version_size),
                SALTS_OK);
    check_equal(strcmp(version, "TLSv1.2"), 0);
    check_equal(version_size, strlen(version));
    cnet_tls_test_pair_destroy(&pair);
  }

  it("negotiates TLS 1.3 when both peers require TLS 1.3") {
    cnet_tls_test_pair pair;
    char version[16] = {0};
    size_t version_size = 0u;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_state_set_protocol_range(&pair.client,
                                                   CNET_TLS_PROTOCOL_VERSION_1_3,
                                                   CNET_TLS_PROTOCOL_VERSION_DEFAULT),
                SALTS_OK);
    check_equal(cnet_tls_state_set_protocol_range(&pair.server,
                                                   CNET_TLS_PROTOCOL_VERSION_1_3,
                                                   CNET_TLS_PROTOCOL_VERSION_DEFAULT),
                SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);
    check_equal(cnet_tls_state_negotiated_version(&pair.client, version, sizeof(version),
                                                  &version_size),
                SALTS_OK);
    check_equal(strcmp(version, "TLSv1.3"), 0);
    check_equal(version_size, strlen(version));
    cnet_tls_test_pair_destroy(&pair);
  }

  it("probes peer close without consuming pending application plaintext") {
    cnet_tls_test_pair pair;
    static const unsigned char payload = 0x5au;
    unsigned char received = 0u;
    size_t received_size = 0u;
    bool complete = false;
    bool peer_closed = false;
    bool plaintext_pending = false;
    bool notify_generated = false;

    check_equal(cnet_tls_test_pair_init(&pair), SALTS_OK);
    check_equal(cnet_tls_test_handshake(&pair), SALTS_OK);

    check_equal(cnet_tls_write(&pair.client, &payload, sizeof(payload), &complete), SALTS_OK);
    check_true(complete);
    check_equal(cnet_tls_test_transfer(&pair.client, &pair.server), SALTS_OK);
    check_equal(cnet_tls_probe_peer_close(&pair.server, &peer_closed, &plaintext_pending), SALTS_OK);
    check_false(peer_closed);
    check_true(plaintext_pending);

    check_equal(cnet_tls_read(&pair.server, &received, sizeof(received), &received_size,
                              &peer_closed),
                SALTS_OK);
    check_equal(received_size, sizeof(received));
    check_equal(received, payload);
    check_false(peer_closed);

    check_equal(cnet_tls_shutdown(&pair.client, &notify_generated), SALTS_OK);
    check_true(notify_generated);
    check_equal(cnet_tls_test_transfer(&pair.client, &pair.server), SALTS_OK);
    plaintext_pending = false;
    check_equal(cnet_tls_probe_peer_close(&pair.server, &peer_closed, &plaintext_pending), SALTS_OK);
    check_true(peer_closed);
    check_false(plaintext_pending);

    cnet_tls_test_pair_destroy(&pair);
  }

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
    static const char second_request[] = "more";
    static const char final_request[] = "done";
    static const char response[] = "pong";
    static const char combined_requests[] = "pingmore";
    static const char *server_alpn[] = {"h2", "http/1.1"};
    static const char *client_alpn[] = {"http/1.1", "h2"};
    mem_buffer_t *request_first = NULL;
    mem_buffer_t *request_second = NULL;
    mem_slice_t request_segments[2] = {{0}};
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
    cnet_start_tls_options upgrade = CNET_START_TLS_OPTIONS_INIT;
    cnet_connection client_connection = {0};
    mem_buffer_t *final_buffer = NULL;
    char peer_certificate_sha256[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY] = {0};
    uint8_t client_channel_binding[CNET_TLS_CHANNEL_BINDING_BYTES] = {0};
    uint8_t server_channel_binding[CNET_TLS_CHANNEL_BINDING_BYTES] = {0};
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
    upgrade.server_name = "localhost";
    check_equal(cnet_start_tls(&client, client_connection, &upgrade), SALTS_ENOTSUP);
    check_equal(cnet_start_tls_server(&server, server_probe.connection, &tls_server),
                SALTS_ENOTSUP);
    check_equal(client_probe.alpn_status, SALTS_OK);
    check_equal(server_probe.alpn_status, SALTS_OK);
    check_equal(client_probe.alpn_size, (size_t)2u);
    check_equal(server_probe.alpn_size, (size_t)2u);
    check_equal(memcmp(client_probe.alpn, "h2", 2u), 0);
    check_equal(memcmp(server_probe.alpn, "h2", 2u), 0);
    check_equal(client_probe.tls_version_status, SALTS_OK);
    check_equal(server_probe.tls_version_status, SALTS_OK);
    check_equal(strcmp(client_probe.tls_version, server_probe.tls_version), 0);
    check_true(strcmp(client_probe.tls_version, "TLSv1.2") == 0 ||
               strcmp(client_probe.tls_version, "TLSv1.3") == 0);
    check_equal(client_probe.tls_version_size, strlen(client_probe.tls_version));
    check_equal(server_probe.tls_version_size, strlen(server_probe.tls_version));
    check_equal(client_probe.tls_cipher_status, SALTS_OK);
    check_equal(server_probe.tls_cipher_status, SALTS_OK);
    check_greater(client_probe.tls_cipher_size, (size_t)0u);
    check_equal(strcmp(client_probe.tls_cipher, server_probe.tls_cipher), 0);
    check_equal(client_probe.tls_cipher_size, strlen(client_probe.tls_cipher));
    check_equal(server_probe.tls_cipher_size, strlen(server_probe.tls_cipher));
    check_equal(cnet_tls_peer_certificate_sha256(&client, client_connection,
                                                 peer_certificate_sha256),
                SALTS_OK);
    check_equal(strcmp(peer_certificate_sha256,
                       "ebd76f304bc43bc2be697fca2f054206978c0558931529a7c1b2bb7d82a7a3c4"),
                0);
    check_equal(cnet_tls_peer_certificate_sha256(&server, server_probe.connection,
                                                 peer_certificate_sha256),
                SALTS_ENOENT);
    check_equal(cnet_tls_export_channel_binding(&client, client_connection,
                                                client_channel_binding),
                SALTS_OK);
    check_equal(cnet_tls_export_channel_binding(&server, server_probe.connection,
                                                server_channel_binding),
                SALTS_OK);
    check_equal(memcmp(client_channel_binding, server_channel_binding,
                       CNET_TLS_CHANNEL_BINDING_BYTES),
                0);

    /* TLS records and receive callbacks are not aligned with one logical slicev send.
     * Retained vectors feed plaintext spans independently, so request enough bounded
     * receive demand for the complete byte stream without depending on record grouping. */
    check_equal(cnet_receive(&server, server_probe.connection, 4u), SALTS_OK);
    request_first = mem_get_buffer(mem_global(), 2u);
    request_second = mem_get_buffer(mem_global(), 2u);
    check_not_null(request_first);
    check_not_null(request_second);
    memcpy(mem_buffer_data(request_first), "pi", 2u);
    memcpy(mem_buffer_data(request_second), "ng", 2u);
    mem_set_used(request_first, 2u);
    mem_set_used(request_second, 2u);
    request_segments[0] = mem_slice(request_first, 0u, 2u);
    request_segments[1] = mem_slice(request_second, 0u, 2u);
    check_not_null(request_segments[0].buffer);
    check_not_null(request_segments[1].buffer);
    check_equal(mem_buffer_ref_count(request_first), UINT32_C(2));
    check_equal(mem_buffer_ref_count(request_second), UINT32_C(2));
    check_equal(cnet_send_slicev(&client, client_connection, request_segments, 2u), SALTS_OK);
    check_equal(mem_buffer_ref_count(request_first), UINT32_C(3));
    check_equal(mem_buffer_ref_count(request_second), UINT32_C(3));
    mem_slice_release(&request_segments[0]);
    mem_slice_release(&request_segments[1]);
    check_equal(mem_buffer_ref_count(request_first), UINT32_C(2));
    check_equal(mem_buffer_ref_count(request_second), UINT32_C(2));
    mem_buffer_release(request_first);
    mem_buffer_release(request_second);
    request_first = NULL;
    request_second = NULL;
    check_equal(cnet_tls_test_send_bytes(&client, client_connection, second_request, sizeof(second_request) - 1u),
                SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((server_probe.received_size < sizeof(combined_requests) - 1u || client_probe.sent < 2) &&
           salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_equal(server_probe.received_size, sizeof(combined_requests) - 1u);
    check_equal(memcmp(server_probe.received, combined_requests, sizeof(combined_requests) - 1u), 0);
    check_equal(client_probe.sent, 2);

    /*
     * Switch one verified TLS plaintext receive to the explicit owned surface.
     * The old borrowed callback must remain silent for this admitted demand.
     */
    check_equal(cnet_set_receive_slice_handler(
                    &client, client_connection,
                    cnet_tls_network_receive_owned, &client_probe),
                SALTS_OK);
    check_equal(cnet_receive(&client, client_connection, 1u), SALTS_OK);
    check_equal(cnet_tls_test_send_bytes(&server, server_probe.connection, response, sizeof(response) - 1u),
                SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((client_probe.owned_received_size == 0u || server_probe.sent == 0) &&
           salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_equal(client_probe.received_size, (size_t)0u);
    check_equal(client_probe.owned_received_size, sizeof(response) - 1u);
    check_equal(client_probe.owned_kind, CNET_MESSAGE_BYTES);
    check_not_null(client_probe.owned_slice.buffer);
    check_true(mem_buffer_pool(client_probe.owned_slice.buffer) == mem_global());
    check_equal(memcmp(client_probe.owned_slice.data, response, sizeof(response) - 1u), 0);
    check_equal(server_probe.sent, 1);

    server_probe.received_size = 0u;
    memset(server_probe.received, 0, sizeof(server_probe.received));
    check_equal(cnet_receive(&server, server_probe.connection, 1u), SALTS_OK);
    final_buffer = mem_get_buffer(mem_global(), sizeof(final_request) - 1u);
    check_not_null(final_buffer);
    memcpy(mem_buffer_data(final_buffer), final_request, sizeof(final_request) - 1u);
    mem_set_used(final_buffer, sizeof(final_request) - 1u);
    check_equal(mem_buffer_ref_count(final_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer_and_close(&client, client_connection, final_buffer), SALTS_OK);
    check_equal(mem_buffer_ref_count(final_buffer), UINT32_C(2));
    mem_buffer_release(final_buffer);
    final_buffer = NULL;
    check_equal(cnet_tls_test_send_bytes(&client, client_connection, request, sizeof(request) - 1u), SALTS_EBUSY);
    check_equal(cnet_receive(&client, client_connection, 1u), SALTS_EBUSY);
    deadline = salts_monotonic_ms() + 5000u;
    while ((!client_probe.terminal || !server_probe.terminal ||
            server_probe.received_size < sizeof(final_request) - 1u) &&
           salts_monotonic_ms() < deadline)
      check_equal(cnet_tls_network_drive(&client, &server, &listener, &tls_server, &server_probe,
                                         &accepted),
                  SALTS_OK);
    check_equal(server_probe.received_size, sizeof(final_request) - 1u);
    check_equal(memcmp(server_probe.received, final_request, sizeof(final_request) - 1u), 0);
    check_equal(client_probe.sent, 3);
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

    /* Owned decrypted plaintext remains valid after both CNet clients are gone. */
    check_not_null(client_probe.owned_slice.buffer);
    check_equal(client_probe.owned_received_size, sizeof(response) - 1u);
    check_equal(memcmp(client_probe.owned_slice.data, response, sizeof(response) - 1u), 0);
    mem_slice_release(&client_probe.owned_slice);
    check_null(client_probe.owned_slice.buffer);

    check_equal(cnet_tls_server_destroy(&tls_server), SALTS_OK);
    check_equal(tt_remove_file(cert_path), 0);
    check_equal(tt_remove_file(key_path), 0);
    free(cert_path);
    free(key_path);
  }

  it("upgrades one negotiated plaintext connection in place and carries TLS bytes") {
    static const char plaintext_request[] = "STARTTLS";
    static const char plaintext_response[] = "READY";
    static const char secure_request[] = "secret";
    cnet_client client = {0};
    cnet_client server = {0};
    cnet_listener listener = {0};
    cnet_tls_server tls_server = {0};
    cnet_client_config client_config = cnet_tls_network_config();
    cnet_client_config server_config = cnet_tls_network_config();
    cnet_listener_config listener_config = {
        .backend = client_config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    cnet_tls_network_probe client_probe = {.client = &client};
    cnet_tls_network_probe server_probe = {.client = &server};
    cnet_observer client_observer = {.on_state = cnet_tls_network_state,
                                     .on_receive = cnet_tls_network_receive,
                                     .user = &client_probe,
                                     .on_send = cnet_tls_network_send};
    cnet_observer server_observer = {.on_state = cnet_tls_network_state,
                                     .on_receive = cnet_tls_network_receive,
                                     .user = &server_probe,
                                     .on_send = cnet_tls_network_send};
    cnet_tls_server_config tls_server_config;
    cnet_tls_client_config tls_client_config;
    cnet_start_tls_options tls_options = CNET_START_TLS_OPTIONS_INIT;
    cnet_connect_options connect_options;
    cnet_connection client_connection = {0};
    cnet_connection server_connection = {0};
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
                                                 .client_auth = CNET_TLS_CLIENT_AUTH_NONE};
    tls_client_config = (cnet_tls_client_config){
        .size = sizeof(tls_client_config), .ca_file = cert_path, .server_name = "localhost"};
    tls_options.tls = &tls_client_config;

    check_equal(cnet_tls_server_init(&tls_server, &tls_server_config), SALTS_OK);
    check_equal(cnet_client_init(&client, &client_config), SALTS_OK);
    check_equal(cnet_client_init(&server, &server_config), SALTS_OK);
    check_equal(cnet_listener_init(&listener, &listener_config), SALTS_OK);
    check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    connect_options = (cnet_connect_options){.uri = uri, .observer = client_observer};
    check_equal(cnet_connect(&client, &connect_options, &client_connection), SALTS_OK);

    deadline = salts_monotonic_ms() + 5000u;
    while ((!client_probe.connected || !server_probe.connected) &&
           salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      int ready = 0;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      if (!accepted) {
        check_equal(cnet_listener_wait(&listener, 0u, &ready), SALTS_OK);
        if (ready != 0) {
          check_equal(
              cnet_listener_accept(&listener, &server, &server_observer, &server_connection),
              SALTS_OK);
          server_probe.connection = server_connection;
          accepted = true;
        }
      }
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_true(accepted);
    check_equal(client_probe.connected_count, 1);
    check_equal(server_probe.connected_count, 1);

    check_equal(cnet_receive(&server, server_connection, 1u), SALTS_OK);
    check_equal(
        cnet_tls_test_send_bytes(&client, client_connection, plaintext_request, sizeof(plaintext_request) - 1u),
        SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((server_probe.received_size == 0u || client_probe.sent == 0) &&
           salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_equal(server_probe.received_size, sizeof(plaintext_request) - 1u);
    check_equal(memcmp(server_probe.received, plaintext_request, sizeof(plaintext_request) - 1u),
                0);

    client_probe.received_size = 0u;
    check_equal(cnet_receive(&client, client_connection, 1u), SALTS_OK);
    check_equal(
        cnet_tls_test_send_bytes(&server, server_connection, plaintext_response, sizeof(plaintext_response) - 1u),
        SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((client_probe.received_size == 0u || server_probe.sent == 0) &&
           salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_equal(client_probe.received_size, sizeof(plaintext_response) - 1u);
    check_equal(memcmp(client_probe.received, plaintext_response, sizeof(plaintext_response) - 1u),
                0);

    client_probe.received_size = 0u;
    server_probe.received_size = 0u;
    check_equal(cnet_start_tls_server(&server, server_connection, &tls_server), SALTS_OK);
    check_equal(cnet_start_tls(&client, client_connection, &tls_options), SALTS_OK);
    check_equal(cnet_tls_server_destroy(&tls_server), SALTS_OK);

    deadline = salts_monotonic_ms() + 5000u;
    while ((client_probe.connected_count != 2 || server_probe.connected_count != 2) &&
           !client_probe.terminal && !server_probe.terminal && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_equal(client_probe.handshaking, 1);
    check_equal(server_probe.handshaking, 1);
    check_equal(client_probe.connected_count, 2);
    check_equal(server_probe.connected_count, 2);
    check_false(client_probe.failed);
    check_false(server_probe.failed);

    check_equal(cnet_receive(&server, server_connection, 1u), SALTS_OK);
    check_equal(cnet_tls_test_send_bytes(&client, client_connection, secure_request, sizeof(secure_request) - 1u),
                SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while (server_probe.received_size == 0u && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_equal(server_probe.received_size, sizeof(secure_request) - 1u);
    check_equal(memcmp(server_probe.received, secure_request, sizeof(secure_request) - 1u), 0);

    check_equal(cnet_receive(&server, server_connection, 1u), SALTS_OK);
    check_equal(cnet_close(&client, client_connection), SALTS_OK);
    deadline = salts_monotonic_ms() + 5000u;
    while ((!client_probe.terminal || !server_probe.terminal) && salts_monotonic_ms() < deadline) {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 1u, &events), SALTS_OK);
      check_equal(cnet_client_poll(&server, 1u, &events), SALTS_OK);
    }
    check_true(client_probe.terminal);
    check_true(server_probe.terminal);

    check_equal(cnet_listener_close(&listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    check_equal(cnet_client_stop(&client, 5000u), SALTS_OK);
    check_equal(cnet_client_stop(&server, 5000u), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
    check_equal(cnet_client_destroy(&server), SALTS_OK);
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
