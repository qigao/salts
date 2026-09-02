#include "cnet_tls.h"

#include <turbo/error_codes.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <wincrypt.h>
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif

enum {
  CNET_TLS_PATH_MAX_BYTES = 4095,
  CNET_TLS_PASSWORD_MAX_BYTES = 1023,
  CNET_TLS_ALPN_WIRE_MAX_BYTES = 65535
};

struct cnet_tls_context {
  SSL_CTX *ssl;
  unsigned char *alpn_wire;
  size_t alpn_wire_size;
  char client_server_name[CNET_TLS_SERVER_NAME_CAPACITY];
  atomic_size_t references;
  bool server;
};

static bool cnet_tls_bounded_string(const char *value, size_t max_bytes, size_t *out_size) {
  size_t size;
  if (value == NULL) {
    if (out_size != NULL) *out_size = 0u;
    return true;
  }
  for (size = 0u; size <= max_bytes; ++size) {
    if (value[size] == '\0') {
      if (out_size != NULL) *out_size = size;
      return true;
    }
  }
  return false;
}

static bool cnet_tls_optional_path_valid(const char *value) {
  size_t size = 0u;
  return cnet_tls_bounded_string(value, CNET_TLS_PATH_MAX_BYTES, &size) &&
         (value == NULL || size != 0u);
}

static int cnet_tls_build_alpn(const char *const *protocols, size_t count, unsigned char **out_wire,
                               size_t *out_size) {
  unsigned char *wire;
  size_t total = 0u;
  size_t index;
  size_t offset = 0u;

  if (out_wire == NULL || out_size == NULL) return TURBO_EINVAL;
  *out_wire = NULL;
  *out_size = 0u;
  if (count == 0u) return protocols == NULL ? TURBO_OK : TURBO_EINVAL;
  if (protocols == NULL) return TURBO_EINVAL;

  for (index = 0u; index < count; ++index) {
    size_t length = 0u;
    if (protocols[index] == NULL ||
        !cnet_tls_bounded_string(protocols[index], CNET_TLS_ALPN_NAME_MAX_BYTES, &length) ||
        length == 0u)
      return TURBO_EINVAL;
    if (total > CNET_TLS_ALPN_WIRE_MAX_BYTES - length - 1u) return TURBO_ERANGE;
    total += length + 1u;
  }

  wire = (unsigned char *)malloc(total);
  if (wire == NULL) return TURBO_ENOMEM;
  for (index = 0u; index < count; ++index) {
    size_t length = 0u;
    (void)cnet_tls_bounded_string(protocols[index], CNET_TLS_ALPN_NAME_MAX_BYTES, &length);
    wire[offset++] = (unsigned char)length;
    memcpy(wire + offset, protocols[index], length);
    offset += length;
  }
  *out_wire = wire;
  *out_size = total;
  return TURBO_OK;
}

static int cnet_tls_password(char *buffer, int capacity, int writing, void *user) {
  const char *password = (const char *)user;
  size_t size = 0u;
  (void)writing;
  if (buffer == NULL || capacity <= 0 || password == NULL ||
      !cnet_tls_bounded_string(password, CNET_TLS_PASSWORD_MAX_BYTES, &size))
    return 0;
  if (size >= (size_t)capacity) size = (size_t)capacity - 1u;
  memcpy(buffer, password, size);
  buffer[size] = '\0';
  return (int)size;
}

#if defined(_WIN32)
static bool cnet_tls_load_windows_store(SSL_CTX *ssl, const char *name) {
  HCERTSTORE store;
  PCCERT_CONTEXT certificate = NULL;
  X509_STORE *target;
  bool loaded = false;

  store = CertOpenSystemStoreA(0u, name);
  if (store == NULL) return false;
  target = SSL_CTX_get_cert_store(ssl);
  if (target == NULL) {
    (void)CertCloseStore(store, 0u);
    return false;
  }
  while ((certificate = CertEnumCertificatesInStore(store, certificate)) != NULL) {
    const unsigned char *encoded = certificate->pbCertEncoded;
    X509 *x509 = d2i_X509(NULL, &encoded, (long)certificate->cbCertEncoded);
    if (x509 == NULL) {
      ERR_clear_error();
      continue;
    }
    if (X509_STORE_add_cert(target, x509) == 1) loaded = true;
    else {
      const unsigned long error = ERR_peek_last_error();
      if (ERR_GET_LIB(error) == ERR_LIB_X509 &&
          ERR_GET_REASON(error) == X509_R_CERT_ALREADY_IN_HASH_TABLE)
        loaded = true;
      ERR_clear_error();
    }
    X509_free(x509);
  }
  (void)CertCloseStore(store, 0u);
  return loaded;
}
#endif

static int cnet_tls_load_system_trust(SSL_CTX *ssl) {
  bool loaded = SSL_CTX_set_default_verify_paths(ssl) == 1;
  if (!loaded) ERR_clear_error();
#if defined(_WIN32)
  if (cnet_tls_load_windows_store(ssl, "ROOT")) loaded = true;
  if (cnet_tls_load_windows_store(ssl, "CA")) loaded = true;
#endif
  return loaded ? TURBO_OK : TURBO_EIO;
}

static int cnet_tls_configure_common(SSL_CTX *ssl) {
  long options;
  if (SSL_CTX_set_min_proto_version(ssl, TLS1_2_VERSION) != 1) return TURBO_EIO;
  options = SSL_OP_NO_COMPRESSION;
#if defined(SSL_OP_NO_RENEGOTIATION)
  options |= SSL_OP_NO_RENEGOTIATION;
#endif
  (void)SSL_CTX_set_options(ssl, options);
  (void)SSL_CTX_set_mode(ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  return TURBO_OK;
}

static int cnet_tls_load_identity(SSL_CTX *ssl, const char *cert_file, const char *key_file,
                                  const char *key_password) {
  if ((cert_file == NULL) != (key_file == NULL)) return TURBO_EINVAL;
  if (cert_file == NULL) return key_password == NULL ? TURBO_OK : TURBO_EINVAL;
  if (key_password != NULL) {
    SSL_CTX_set_default_passwd_cb(ssl, cnet_tls_password);
    SSL_CTX_set_default_passwd_cb_userdata(ssl, (void *)key_password);
  }
  if (SSL_CTX_use_certificate_chain_file(ssl, cert_file) != 1 ||
      SSL_CTX_use_PrivateKey_file(ssl, key_file, SSL_FILETYPE_PEM) != 1 ||
      SSL_CTX_check_private_key(ssl) != 1) {
    SSL_CTX_set_default_passwd_cb(ssl, NULL);
    SSL_CTX_set_default_passwd_cb_userdata(ssl, NULL);
    return TURBO_EIO;
  }
  SSL_CTX_set_default_passwd_cb(ssl, NULL);
  SSL_CTX_set_default_passwd_cb_userdata(ssl, NULL);
  return TURBO_OK;
}

static int cnet_tls_server_select_alpn(SSL *ssl, const unsigned char **out,
                                       unsigned char *out_length, const unsigned char *input,
                                       unsigned int input_length, void *user) {
  const cnet_tls_context *context = (const cnet_tls_context *)user;
  (void)ssl;
  if (context == NULL || context->alpn_wire == NULL || context->alpn_wire_size == 0u)
    return SSL_TLSEXT_ERR_NOACK;
  if (SSL_select_next_proto((unsigned char **)out, out_length, context->alpn_wire,
                            (unsigned int)context->alpn_wire_size, input,
                            input_length) == OPENSSL_NPN_NEGOTIATED)
    return SSL_TLSEXT_ERR_OK;
  return SSL_TLSEXT_ERR_NOACK;
}

static void cnet_tls_context_dispose(cnet_tls_context *context) {
  if (context == NULL) return;
  SSL_CTX_free(context->ssl);
  free(context->alpn_wire);
  free(context);
}

void cnet_tls_context_retain(cnet_tls_context *context) {
  if (context != NULL)
    (void)atomic_fetch_add_explicit(&context->references, 1u, memory_order_relaxed);
}

void cnet_tls_context_release(cnet_tls_context *context) {
  if (context != NULL &&
      atomic_fetch_sub_explicit(&context->references, 1u, memory_order_acq_rel) == 1u)
    cnet_tls_context_dispose(context);
}

static cnet_tls_context *cnet_tls_context_allocate(SSL_CTX *ssl, bool server) {
  cnet_tls_context *context = (cnet_tls_context *)calloc(1u, sizeof(*context));
  if (context == NULL) return NULL;
  context->ssl = ssl;
  context->server = server;
  atomic_init(&context->references, 1u);
  return context;
}

int cnet_tls_client_context_create(const cnet_tls_client_config *config,
                                   cnet_tls_context **out_context) {
  cnet_tls_client_config defaults = {sizeof(defaults)};
  cnet_tls_context *context;
  SSL_CTX *ssl;
  int status;

  if (out_context == NULL) return TURBO_EINVAL;
  *out_context = NULL;
  if (config == NULL) config = &defaults;
  if (config->size != sizeof(*config) || !cnet_tls_optional_path_valid(config->ca_file) ||
      !cnet_tls_optional_path_valid(config->ca_path) ||
      !cnet_tls_optional_path_valid(config->cert_file) ||
      !cnet_tls_optional_path_valid(config->key_file) ||
      !cnet_tls_bounded_string(config->key_password, CNET_TLS_PASSWORD_MAX_BYTES, NULL) ||
      !cnet_tls_bounded_string(config->server_name, CNET_TLS_SERVER_NAME_CAPACITY - 1u, NULL) ||
      (config->server_name != NULL && config->server_name[0] == '\0'))
    return TURBO_EINVAL;

  ssl = SSL_CTX_new(TLS_client_method());
  if (ssl == NULL) return TURBO_ENOMEM;
  status = cnet_tls_configure_common(ssl);
  if (status == TURBO_OK) {
    SSL_CTX_set_verify(ssl, SSL_VERIFY_PEER, NULL);
    status = config->ca_file != NULL || config->ca_path != NULL
                 ? (SSL_CTX_load_verify_locations(ssl, config->ca_file, config->ca_path) == 1
                        ? TURBO_OK
                        : TURBO_EIO)
                 : cnet_tls_load_system_trust(ssl);
  }
  if (status == TURBO_OK)
    status = cnet_tls_load_identity(ssl, config->cert_file, config->key_file, config->key_password);
  if (status != TURBO_OK) {
    SSL_CTX_free(ssl);
    return status;
  }

  context = cnet_tls_context_allocate(ssl, false);
  if (context == NULL) {
    SSL_CTX_free(ssl);
    return TURBO_ENOMEM;
  }
  status = cnet_tls_build_alpn(config->alpn_protocols, config->alpn_protocol_count,
                               &context->alpn_wire, &context->alpn_wire_size);
  if (status != TURBO_OK) {
    cnet_tls_context_release(context);
    return status;
  }
  if (config->server_name != NULL)
    memcpy(context->client_server_name, config->server_name, strlen(config->server_name) + 1u);
  *out_context = context;
  return TURBO_OK;
}

int cnet_tls_client_init(cnet_tls_client *client, const cnet_tls_client_config *config) {
  cnet_tls_context *context = NULL;
  int status;
  if (client == NULL || config == NULL) return TURBO_EINVAL;
  if (client->impl != NULL) return TURBO_EALREADY;
  status = cnet_tls_client_context_create(config, &context);
  if (status != TURBO_OK) return status;
  client->impl = context;
  return TURBO_OK;
}

int cnet_tls_client_destroy(cnet_tls_client *client) {
  cnet_tls_context *context;
  if (client == NULL) return TURBO_EINVAL;
  context = (cnet_tls_context *)client->impl;
  if (context == NULL) return TURBO_OK;
  client->impl = NULL;
  cnet_tls_context_release(context);
  return TURBO_OK;
}

cnet_tls_context *cnet_tls_client_context(const cnet_tls_client *client) {
  cnet_tls_context *context = client != NULL ? (cnet_tls_context *)client->impl : NULL;
  return context != NULL && !context->server ? context : NULL;
}

const char *cnet_tls_client_server_name(const cnet_tls_client *client) {
  const cnet_tls_context *context = cnet_tls_client_context(client);
  return context != NULL && context->client_server_name[0] != '\0' ? context->client_server_name
                                                                   : NULL;
}

int cnet_tls_server_init(cnet_tls_server *server, const cnet_tls_server_config *config) {
  cnet_tls_context *context;
  SSL_CTX *ssl;
  int status;

  if (server == NULL || config == NULL) return TURBO_EINVAL;
  if (server->impl != NULL) return TURBO_EALREADY;
  if (config->size != sizeof(*config) || !cnet_tls_optional_path_valid(config->cert_file) ||
      !cnet_tls_optional_path_valid(config->key_file) || config->cert_file == NULL ||
      config->key_file == NULL || !cnet_tls_optional_path_valid(config->ca_file) ||
      !cnet_tls_optional_path_valid(config->ca_path) ||
      !cnet_tls_bounded_string(config->key_password, CNET_TLS_PASSWORD_MAX_BYTES, NULL) ||
      (config->client_auth != CNET_TLS_CLIENT_AUTH_NONE &&
       config->client_auth != CNET_TLS_CLIENT_AUTH_REQUIRED) ||
      (config->client_auth == CNET_TLS_CLIENT_AUTH_REQUIRED && config->ca_file == NULL &&
       config->ca_path == NULL) ||
      (config->client_auth == CNET_TLS_CLIENT_AUTH_NONE &&
       (config->ca_file != NULL || config->ca_path != NULL)))
    return TURBO_EINVAL;

  ssl = SSL_CTX_new(TLS_server_method());
  if (ssl == NULL) return TURBO_ENOMEM;
  status = cnet_tls_configure_common(ssl);
  if (status == TURBO_OK)
    status = cnet_tls_load_identity(ssl, config->cert_file, config->key_file, config->key_password);
  if (status == TURBO_OK && config->client_auth == CNET_TLS_CLIENT_AUTH_REQUIRED) {
    if (SSL_CTX_load_verify_locations(ssl, config->ca_file, config->ca_path) != 1)
      status = TURBO_EIO;
    else SSL_CTX_set_verify(ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
  } else if (status == TURBO_OK) {
    SSL_CTX_set_verify(ssl, SSL_VERIFY_NONE, NULL);
  }
  if (status != TURBO_OK) {
    SSL_CTX_free(ssl);
    return status;
  }

  context = cnet_tls_context_allocate(ssl, true);
  if (context == NULL) {
    SSL_CTX_free(ssl);
    return TURBO_ENOMEM;
  }
  status = cnet_tls_build_alpn(config->alpn_protocols, config->alpn_protocol_count,
                               &context->alpn_wire, &context->alpn_wire_size);
  if (status != TURBO_OK) {
    cnet_tls_context_release(context);
    return status;
  }
  if (context->alpn_wire != NULL)
    SSL_CTX_set_alpn_select_cb(ssl, cnet_tls_server_select_alpn, context);
  server->impl = context;
  return TURBO_OK;
}

int cnet_tls_server_destroy(cnet_tls_server *server) {
  cnet_tls_context *context;
  if (server == NULL) return TURBO_EINVAL;
  context = (cnet_tls_context *)server->impl;
  if (context == NULL) return TURBO_OK;
  server->impl = NULL;
  cnet_tls_context_release(context);
  return TURBO_OK;
}

cnet_tls_context *cnet_tls_server_context(const cnet_tls_server *server) {
  cnet_tls_context *context = server != NULL ? (cnet_tls_context *)server->impl : NULL;
  return context != NULL && context->server ? context : NULL;
}

static int cnet_tls_configure_server_name(SSL *ssl, const char *server_name) {
  unsigned char ipv4[4];
  unsigned char ipv6[16];
  X509_VERIFY_PARAM *verify;
  if (ssl == NULL || server_name == NULL || server_name[0] == '\0') return TURBO_EINVAL;

  verify = SSL_get0_param(ssl);
  if (verify == NULL) return TURBO_EIO;
  if (inet_pton(AF_INET, server_name, ipv4) == 1 || inet_pton(AF_INET6, server_name, ipv6) == 1)
    return X509_VERIFY_PARAM_set1_ip_asc(verify, server_name) == 1 ? TURBO_OK : TURBO_EIO;
  if (SSL_set_tlsext_host_name(ssl, server_name) != 1 || SSL_set1_host(ssl, server_name) != 1)
    return TURBO_EIO;
  return TURBO_OK;
}

int cnet_tls_state_init(cnet_tls_state *state, cnet_tls_context *context, bool server,
                        const char *server_name, size_t io_buffer_bytes) {
  BIO *ssl_bio = NULL;
  BIO *network_bio = NULL;
  SSL *ssl = NULL;
  unsigned char *read_buffer = NULL;
  unsigned char *write_buffer = NULL;
  int status = TURBO_OK;

  if (state == NULL || context == NULL || context->ssl == NULL || context->server != server ||
      io_buffer_bytes < CNET_TLS_MIN_IO_BUFFER_BYTES || io_buffer_bytes > INT_MAX ||
      (!server && (server_name == NULL || server_name[0] == '\0')))
    return TURBO_EINVAL;
  if (state->ssl != NULL || state->network_bio != NULL || state->context != NULL)
    return TURBO_EALREADY;

  read_buffer = (unsigned char *)malloc(io_buffer_bytes);
  write_buffer = (unsigned char *)malloc(io_buffer_bytes);
  ssl = SSL_new(context->ssl);
  if (read_buffer == NULL || write_buffer == NULL || ssl == NULL) {
    status = TURBO_ENOMEM;
    goto fail;
  }
  if (BIO_new_bio_pair(&ssl_bio, io_buffer_bytes, &network_bio, io_buffer_bytes) != 1) {
    status = TURBO_ENOMEM;
    goto fail;
  }
  if (BIO_up_ref(ssl_bio) != 1) {
    status = TURBO_EIO;
    goto fail;
  }
  SSL_set0_rbio(ssl, ssl_bio);
  SSL_set0_wbio(ssl, ssl_bio);
  ssl_bio = NULL;

  if (server) {
    SSL_set_accept_state(ssl);
  } else {
    status = cnet_tls_configure_server_name(ssl, server_name);
    if (status != TURBO_OK) goto fail;
    if (context->alpn_wire_size != 0u &&
        SSL_set_alpn_protos(ssl, context->alpn_wire, (unsigned int)context->alpn_wire_size) != 0) {
      status = TURBO_EIO;
      goto fail;
    }
    SSL_set_connect_state(ssl);
  }

  state->context = context;
  state->ssl = ssl;
  state->network_bio = network_bio;
  state->read_buffer = read_buffer;
  state->write_buffer = write_buffer;
  state->io_buffer_bytes = io_buffer_bytes;
  state->server = server;
  return TURBO_OK;

fail:
  BIO_free(ssl_bio);
  BIO_free(network_bio);
  SSL_free(ssl);
  free(write_buffer);
  free(read_buffer);
  return status;
}

void cnet_tls_state_destroy(cnet_tls_state *state) {
  cnet_tls_context *context;
  if (state == NULL) return;
  context = state->context;
  SSL_free(state->ssl);
  BIO_free(state->network_bio);
  free(state->write_buffer);
  free(state->read_buffer);
  memset(state, 0, sizeof(*state));
  cnet_tls_context_release(context);
}

static int cnet_tls_retry_or_error(SSL *ssl, int result, int fatal_status) {
  const int error = SSL_get_error(ssl, result);
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return TURBO_OK;
  return fatal_status;
}

int cnet_tls_handshake(cnet_tls_state *state, bool *out_complete) {
  const unsigned char *alpn = NULL;
  unsigned int alpn_size = 0u;
  int result;
  if (state == NULL || state->ssl == NULL || out_complete == NULL) return TURBO_EINVAL;
  *out_complete = state->handshake_complete;
  if (state->handshake_complete) return TURBO_OK;

  ERR_clear_error();
  result = SSL_do_handshake(state->ssl);
  if (result != 1) return cnet_tls_retry_or_error(state->ssl, result, TURBO_ECONNABORTED);
  if (!state->server && SSL_get_verify_result(state->ssl) != X509_V_OK) return TURBO_ECONNABORTED;
  SSL_get0_alpn_selected(state->ssl, &alpn, &alpn_size);
  if (alpn_size > CNET_TLS_ALPN_NAME_MAX_BYTES) return TURBO_EPROTO;
  if (alpn_size != 0u) memcpy(state->negotiated_alpn, alpn, alpn_size);
  state->negotiated_alpn_size = alpn_size;
  state->handshake_complete = true;
  *out_complete = true;
  return TURBO_OK;
}

size_t cnet_tls_cipher_input_capacity(const cnet_tls_state *state) {
  if (state == NULL || state->network_bio == NULL) return 0u;
  return BIO_get_write_guarantee(state->network_bio);
}

int cnet_tls_feed_cipher(cnet_tls_state *state, const void *data, size_t size) {
  size_t written = 0u;
  if (state == NULL || state->network_bio == NULL || data == NULL || size == 0u)
    return TURBO_EINVAL;
  if (size > cnet_tls_cipher_input_capacity(state)) return TURBO_ENOBUFS;
  if (BIO_write_ex(state->network_bio, data, size, &written) != 1)
    return BIO_should_retry(state->network_bio) ? TURBO_ENOBUFS : TURBO_EIO;
  return written == size ? TURBO_OK : TURBO_EIO;
}

int cnet_tls_take_cipher(cnet_tls_state *state, void *buffer, size_t capacity, size_t *out_size) {
  size_t read_size = 0u;
  if (out_size == NULL) return TURBO_EINVAL;
  *out_size = 0u;
  if (state == NULL || state->network_bio == NULL || buffer == NULL || capacity == 0u)
    return TURBO_EINVAL;
  if (BIO_ctrl_pending(state->network_bio) == 0u) return TURBO_ENOENT;
  if (BIO_read_ex(state->network_bio, buffer, capacity, &read_size) != 1)
    return BIO_should_retry(state->network_bio) ? TURBO_ENOENT : TURBO_EIO;
  if (read_size == 0u) return TURBO_EIO;
  *out_size = read_size;
  return TURBO_OK;
}

int cnet_tls_write(cnet_tls_state *state, const void *data, size_t size, bool *out_complete) {
  size_t written = 0u;
  int result;
  if (state == NULL || state->ssl == NULL || data == NULL || size == 0u || out_complete == NULL)
    return TURBO_EINVAL;
  *out_complete = false;
  if (!state->handshake_complete || state->close_notify_started) return TURBO_ENOTCONN;
  ERR_clear_error();
  result = SSL_write_ex(state->ssl, data, size, &written);
  if (result == 1) {
    if (written != size) return TURBO_EIO;
    *out_complete = true;
    return TURBO_OK;
  }
  return cnet_tls_retry_or_error(state->ssl, result, TURBO_EPROTO);
}

int cnet_tls_read(cnet_tls_state *state, void *buffer, size_t capacity, size_t *out_size,
                  bool *out_peer_closed) {
  size_t read_size = 0u;
  int result;
  int error;
  if (out_size == NULL || out_peer_closed == NULL) return TURBO_EINVAL;
  *out_size = 0u;
  *out_peer_closed = false;
  if (state == NULL || state->ssl == NULL || buffer == NULL || capacity == 0u) return TURBO_EINVAL;
  if (!state->handshake_complete) return TURBO_ENOTCONN;
  if (state->peer_close_notify) {
    *out_peer_closed = true;
    return TURBO_OK;
  }

  ERR_clear_error();
  result = SSL_read_ex(state->ssl, buffer, capacity, &read_size);
  if (result == 1) {
    if (read_size == 0u) return TURBO_EIO;
    *out_size = read_size;
    return TURBO_OK;
  }
  error = SSL_get_error(state->ssl, result);
  if (error == SSL_ERROR_ZERO_RETURN) {
    state->peer_close_notify = true;
    *out_peer_closed = true;
    return TURBO_OK;
  }
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return TURBO_OK;
  return TURBO_EPROTO;
}

int cnet_tls_shutdown(cnet_tls_state *state, bool *out_notify_generated) {
  int result;
  int error;
  if (state == NULL || state->ssl == NULL || out_notify_generated == NULL) return TURBO_EINVAL;
  *out_notify_generated = false;
  if (!state->handshake_complete) return TURBO_ENOTCONN;

  ERR_clear_error();
  result = SSL_shutdown(state->ssl);
  state->close_notify_started = (SSL_get_shutdown(state->ssl) & SSL_SENT_SHUTDOWN) != 0;
  *out_notify_generated = state->close_notify_started;
  if (result >= 0) return TURBO_OK;
  error = SSL_get_error(state->ssl, result);
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return TURBO_OK;
  return TURBO_EPROTO;
}

int cnet_tls_get_negotiated_alpn(const cnet_tls_state *state, const unsigned char **out_data,
                                 size_t *out_size) {
  if (out_data == NULL || out_size == NULL) return TURBO_EINVAL;
  *out_data = NULL;
  *out_size = 0u;
  if (state == NULL || state->ssl == NULL) return TURBO_EINVAL;
  if (!state->handshake_complete) return TURBO_ENOTCONN;
  if (state->negotiated_alpn_size == 0u) return TURBO_ENOENT;
  *out_data = state->negotiated_alpn;
  *out_size = state->negotiated_alpn_size;
  return TURBO_OK;
}
