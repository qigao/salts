#include "cnet_tls.h"

#include <salts/error_codes.h>

#include <gmssl/tls.h>
#include <gmssl/x509_cer.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <windows.h>
  #include <wincrypt.h>
  #include <ws2tcpip.h>
#elif defined(__APPLE__)
  #include <arpa/inet.h>
  #include <CoreFoundation/CoreFoundation.h>
  #include <Security/Security.h>
#else
  #include <arpa/inet.h>
#endif

enum {
  CNET_TLS_PATH_MAX_BYTES = 4095,
  CNET_TLS_PASSWORD_MAX_BYTES = 1023,
  CNET_TLS_GMSSL_ALPN_MAX_COUNT = 4,
  CNET_TLS_TRUST_MAX_BYTES = 16 * 1024 * 1024
};

typedef enum cnet_tls_variant {
  CNET_TLS_VARIANT_RANGE = 0,
  CNET_TLS_VARIANT_1_2 = 1,
  CNET_TLS_VARIANT_1_3 = 2,
  CNET_TLS_VARIANT_COUNT = 3
} cnet_tls_variant;

struct cnet_tls_context {
  TLS_CTX tls[CNET_TLS_VARIANT_COUNT];
  bool tls_ready[CNET_TLS_VARIANT_COUNT];
  char **alpn_protocols;
  size_t alpn_protocol_count;
  char client_server_name[CNET_TLS_SERVER_NAME_CAPACITY];
  atomic_size_t references;
  bool server;
  bool verify_peer;
};

typedef struct cnet_tls_gmssl_state {
  TLS_CONNECT connection;
  TLS_IO io;
  unsigned char *cipher_input;
  unsigned char *cipher_output;
  size_t capacity;
  size_t input_head;
  size_t input_size;
  size_t output_head;
  size_t output_size;
  size_t plaintext_pending_offset;
  size_t plaintext_pending_size;
  cnet_tls_variant variant;
  bool connection_ready;
  char server_name[CNET_TLS_SERVER_NAME_CAPACITY];
} cnet_tls_gmssl_state;

typedef struct cnet_tls_der_bundle {
  unsigned char *data;
  size_t size;
  size_t capacity;
} cnet_tls_der_bundle;

#define CNET_TLS_ENGINE(state) ((cnet_tls_gmssl_state *)((state)->engine))
#define CNET_TLS_CONNECTION(state)   (CNET_TLS_ENGINE(state) != NULL ? &CNET_TLS_ENGINE(state)->connection : NULL)

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

static bool cnet_tls_file_readable(const char *path) {
  FILE *file;
  if (path == NULL || path[0] == '\0') return false;
  file = fopen(path, "rb");
  if (file == NULL) return false;
  (void)fclose(file);
  return true;
}

static int cnet_tls_der_bundle_append(cnet_tls_der_bundle *bundle,
                                      const unsigned char *data, size_t size) {
  unsigned char *next;
  size_t capacity;
  if (bundle == NULL || data == NULL || size == 0u) return SALTS_EINVAL;
  if (bundle->size > CNET_TLS_TRUST_MAX_BYTES - size) return SALTS_ERANGE;
  if (bundle->size + size <= bundle->capacity) {
    memcpy(bundle->data + bundle->size, data, size);
    bundle->size += size;
    return SALTS_OK;
  }

  capacity = bundle->capacity != 0u ? bundle->capacity : 4096u;
  while (capacity < bundle->size + size) {
    if (capacity > CNET_TLS_TRUST_MAX_BYTES / 2u) {
      capacity = CNET_TLS_TRUST_MAX_BYTES;
      break;
    }
    capacity *= 2u;
  }
  if (capacity < bundle->size + size) return SALTS_ERANGE;
  next = (unsigned char *)realloc(bundle->data, capacity);
  if (next == NULL) return SALTS_ENOMEM;
  bundle->data = next;
  bundle->capacity = capacity;
  memcpy(bundle->data + bundle->size, data, size);
  bundle->size += size;
  return SALTS_OK;
}

static void cnet_tls_der_bundle_dispose(cnet_tls_der_bundle *bundle) {
  if (bundle == NULL) return;
  free(bundle->data);
  memset(bundle, 0, sizeof(*bundle));
}

#if defined(_WIN32)
static int cnet_tls_append_windows_store(cnet_tls_der_bundle *bundle, const char *name) {
  HCERTSTORE store;
  PCCERT_CONTEXT certificate = NULL;
  int status = SALTS_OK;

  if (bundle == NULL || name == NULL) return SALTS_EINVAL;
  store = CertOpenSystemStoreA(0u, name);
  if (store == NULL) return SALTS_EIO;
  while ((certificate = CertEnumCertificatesInStore(store, certificate)) != NULL) {
    status = cnet_tls_der_bundle_append(bundle, certificate->pbCertEncoded,
                                        (size_t)certificate->cbCertEncoded);
    if (status != SALTS_OK) break;
  }
  (void)CertCloseStore(store, 0u);
  return status;
}
#endif

#if defined(__APPLE__)
static int cnet_tls_load_apple_anchors(cnet_tls_der_bundle *bundle) {
  CFArrayRef anchors = NULL;
  CFIndex index;
  CFIndex count;
  OSStatus result;
  int status = SALTS_OK;

  if (bundle == NULL) return SALTS_EINVAL;
  result = SecTrustCopyAnchorCertificates(&anchors);
  if (result != errSecSuccess || anchors == NULL) return SALTS_EIO;
  count = CFArrayGetCount(anchors);
  for (index = 0; index < count; ++index) {
    SecCertificateRef certificate =
        (SecCertificateRef)CFArrayGetValueAtIndex(anchors, index);
    CFDataRef der;
    if (certificate == NULL) continue;
    der = SecCertificateCopyData(certificate);
    if (der == NULL) {
      status = SALTS_EIO;
      break;
    }
    status = cnet_tls_der_bundle_append(
        bundle, (const unsigned char *)CFDataGetBytePtr(der),
        (size_t)CFDataGetLength(der));
    CFRelease(der);
    if (status != SALTS_OK) break;
  }
  CFRelease(anchors);
  return status;
}
#endif

static int cnet_tls_load_system_trust(TLS_CTX *tls) {
#if defined(_WIN32)
  cnet_tls_der_bundle bundle = {0};
  int status = cnet_tls_append_windows_store(&bundle, "ROOT");
  if (status == SALTS_OK) status = cnet_tls_append_windows_store(&bundle, "CA");
  if (status == SALTS_OK && bundle.size == 0u) status = SALTS_EIO;
  if (status == SALTS_OK &&
      tls_ctx_set_ca_certificates_der(tls, bundle.data, bundle.size,
                                      TLS_DEFAULT_VERIFY_DEPTH) != 1)
    status = SALTS_EIO;
  cnet_tls_der_bundle_dispose(&bundle);
  return status;
#elif defined(__APPLE__)
  cnet_tls_der_bundle bundle = {0};
  int status = cnet_tls_load_apple_anchors(&bundle);
  if (status == SALTS_OK && bundle.size == 0u) status = SALTS_EIO;
  if (status == SALTS_OK &&
      tls_ctx_set_ca_certificates_der(tls, bundle.data, bundle.size,
                                      TLS_DEFAULT_VERIFY_DEPTH) != 1)
    status = SALTS_EIO;
  cnet_tls_der_bundle_dispose(&bundle);
  return status;
#else
  static const char *const paths[] = {
      "/etc/ssl/certs/ca-certificates.crt",
      "/etc/pki/tls/certs/ca-bundle.crt",
      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
      "/etc/ssl/ca-bundle.pem"};
  const char *environment = getenv("SSL_CERT_FILE");
  size_t index;

  if (environment != NULL && environment[0] != '\0' &&
      cnet_tls_file_readable(environment))
    return tls_ctx_set_ca_certificates(tls, environment,
                                       TLS_DEFAULT_VERIFY_DEPTH) == 1
               ? SALTS_OK
               : SALTS_EIO;
  for (index = 0u; index < sizeof(paths) / sizeof(paths[0]); ++index) {
    if (cnet_tls_file_readable(paths[index]))
      return tls_ctx_set_ca_certificates(tls, paths[index],
                                         TLS_DEFAULT_VERIFY_DEPTH) == 1
                 ? SALTS_OK
                 : SALTS_EIO;
  }
  return SALTS_EIO;
#endif
}

static void cnet_tls_alpn_dispose(cnet_tls_context *context) {
  size_t index;
  if (context == NULL || context->alpn_protocols == NULL) return;
  for (index = 0u; index < context->alpn_protocol_count; ++index)
    free(context->alpn_protocols[index]);
  free(context->alpn_protocols);
  context->alpn_protocols = NULL;
  context->alpn_protocol_count = 0u;
}

static int cnet_tls_alpn_copy(cnet_tls_context *context,
                              const char *const *protocols, size_t count) {
  size_t index;
  if (context == NULL) return SALTS_EINVAL;
  if (count == 0u) return protocols == NULL ? SALTS_OK : SALTS_EINVAL;
  if (protocols == NULL) return SALTS_EINVAL;
  if (count > CNET_TLS_GMSSL_ALPN_MAX_COUNT) return SALTS_ERANGE;

  context->alpn_protocols = (char **)calloc(count, sizeof(char *));
  if (context->alpn_protocols == NULL) return SALTS_ENOMEM;
  context->alpn_protocol_count = count;
  for (index = 0u; index < count; ++index) {
    size_t length = 0u;
    if (protocols[index] == NULL ||
        !cnet_tls_bounded_string(protocols[index],
                                 CNET_TLS_ALPN_NAME_MAX_BYTES, &length) ||
        length == 0u) {
      cnet_tls_alpn_dispose(context);
      return SALTS_EINVAL;
    }
    context->alpn_protocols[index] = (char *)malloc(length + 1u);
    if (context->alpn_protocols[index] == NULL) {
      cnet_tls_alpn_dispose(context);
      return SALTS_ENOMEM;
    }
    memcpy(context->alpn_protocols[index], protocols[index], length + 1u);
  }
  return SALTS_OK;
}

static int cnet_tls_variant_protocol(cnet_tls_variant variant) {
  return variant == CNET_TLS_VARIANT_1_2 ? TLS_protocol_tls12
                                         : TLS_protocol_tls13;
}

static int cnet_tls_configure_variant(cnet_tls_context *context,
                                      cnet_tls_variant variant,
                                      const char *ca_file,
                                      const char *cert_file,
                                      const char *key_file,
                                      const char *key_password,
                                      cnet_tls_client_auth client_auth) {
  static const int range_ciphers[] = {
      TLS_cipher_aes_128_gcm_sha256,
      TLS_cipher_ecdhe_rsa_with_aes_128_gcm_sha256,
      TLS_cipher_ecdhe_ecdsa_with_aes_128_gcm_sha256};
  static const int tls13_ciphers[] = {TLS_cipher_aes_128_gcm_sha256};
  static const int tls12_ciphers[] = {
      TLS_cipher_ecdhe_rsa_with_aes_128_gcm_sha256,
      TLS_cipher_ecdhe_ecdsa_with_aes_128_gcm_sha256};
  static const int range_signatures[] = {
      TLS_sig_rsa_pss_rsae_sha256,
      TLS_sig_rsa_pkcs1_sha256,
      TLS_sig_ecdsa_secp256r1_sha256};
  static const int tls13_signatures[] = {
      TLS_sig_rsa_pss_rsae_sha256,
      TLS_sig_ecdsa_secp256r1_sha256};
  static const int tls12_signatures[] = {
      TLS_sig_rsa_pkcs1_sha256,
      TLS_sig_ecdsa_secp256r1_sha256};
  static const int groups[] = {TLS_curve_secp256r1};
  TLS_CTX *tls;
  const int *ciphers;
  const int *signatures;
  size_t cipher_count;
  size_t signature_count;
  int mode;
  int status = SALTS_OK;

  if (context == NULL || variant < 0 || variant >= CNET_TLS_VARIANT_COUNT)
    return SALTS_EINVAL;
  tls = &context->tls[variant];
  mode = context->server ? TLS_server_mode : TLS_client_mode;
  if (tls_ctx_init(tls, cnet_tls_variant_protocol(variant), mode) != 1)
    return SALTS_EIO;
  context->tls_ready[variant] = true;

  if (variant == CNET_TLS_VARIANT_RANGE) {
    if (tls_ctx_set_protocol_range(tls, TLS_protocol_tls12,
                                   TLS_protocol_tls13) != 1)
      return SALTS_EIO;
    ciphers = range_ciphers;
    cipher_count = sizeof(range_ciphers) / sizeof(range_ciphers[0]);
    signatures = range_signatures;
    signature_count = sizeof(range_signatures) / sizeof(range_signatures[0]);
  } else if (variant == CNET_TLS_VARIANT_1_2) {
    ciphers = tls12_ciphers;
    cipher_count = sizeof(tls12_ciphers) / sizeof(tls12_ciphers[0]);
    signatures = tls12_signatures;
    signature_count = sizeof(tls12_signatures) / sizeof(tls12_signatures[0]);
  } else {
    ciphers = tls13_ciphers;
    cipher_count = sizeof(tls13_ciphers) / sizeof(tls13_ciphers[0]);
    signatures = tls13_signatures;
    signature_count = sizeof(tls13_signatures) / sizeof(tls13_signatures[0]);
  }

  if (tls_ctx_set_cipher_suites(tls, ciphers, cipher_count) != 1 ||
      tls_ctx_set_supported_groups(tls, groups,
                                   sizeof(groups) / sizeof(groups[0])) != 1 ||
      tls_ctx_set_signature_algorithms(tls, signatures, signature_count) != 1)
    return SALTS_EIO;

  if (variant == CNET_TLS_VARIANT_1_2) {
    if (tls12_ctx_set_renegotiation_info(tls, 1) != 1)
      return SALTS_EIO;
    if (!context->server &&
        tls12_ctx_set_empty_renegotiation_info_scsv(tls, 1) != 1)
      return SALTS_EIO;
  }

  if (context->alpn_protocol_count != 0u &&
      tls_ctx_set_application_layer_protocol_negotiation(
          tls, context->alpn_protocols, context->alpn_protocol_count) != 1)
    return SALTS_EIO;

  if (context->verify_peer) {
    status = ca_file != NULL
                 ? (tls_ctx_set_ca_certificates(tls, ca_file,
                                                TLS_DEFAULT_VERIFY_DEPTH) == 1
                        ? SALTS_OK
                        : SALTS_EIO)
                 : cnet_tls_load_system_trust(tls);
    if (status != SALTS_OK) return status;
  }

  if (cert_file != NULL) {
    if (tls_ctx_set_certificate_and_key(
            tls, cert_file, key_file,
            key_password != NULL ? key_password : "") != 1)
      return SALTS_EIO;
  }

  if (context->server && client_auth == CNET_TLS_CLIENT_AUTH_REQUIRED &&
      tls_ctx_enable_certificate_request(tls, 1) != 1)
    return SALTS_EIO;

  return SALTS_OK;
}

static void cnet_tls_context_dispose(cnet_tls_context *context) {
  size_t index;
  if (context == NULL) return;
  for (index = 0u; index < CNET_TLS_VARIANT_COUNT; ++index) {
    if (context->tls_ready[index]) tls_ctx_cleanup(&context->tls[index]);
  }
  cnet_tls_alpn_dispose(context);
  free(context);
}

void cnet_tls_context_retain(cnet_tls_context *context) {
  if (context != NULL)
    (void)atomic_fetch_add_explicit(&context->references, 1u,
                                    memory_order_relaxed);
}

void cnet_tls_context_release(cnet_tls_context *context) {
  if (context != NULL &&
      atomic_fetch_sub_explicit(&context->references, 1u,
                                memory_order_acq_rel) == 1u)
    cnet_tls_context_dispose(context);
}

static int cnet_tls_context_create_common(
    bool server, bool verify_peer, const char *ca_file, const char *ca_path,
    const char *cert_file, const char *key_file, const char *key_password,
    cnet_tls_client_auth client_auth, const char *const *alpn_protocols,
    size_t alpn_protocol_count, cnet_tls_context **out_context) {
  cnet_tls_context *context;
  size_t index;
  int status;

  if (out_context == NULL) return SALTS_EINVAL;
  *out_context = NULL;
  if (ca_path != NULL) return SALTS_ENOTSUP;

  context = (cnet_tls_context *)calloc(1u, sizeof(*context));
  if (context == NULL) return SALTS_ENOMEM;
  context->server = server;
  context->verify_peer = verify_peer;
  atomic_init(&context->references, 1u);

  status = cnet_tls_alpn_copy(context, alpn_protocols, alpn_protocol_count);
  if (status != SALTS_OK) {
    cnet_tls_context_dispose(context);
    return status;
  }

  for (index = 0u; index < CNET_TLS_VARIANT_COUNT; ++index) {
    status = cnet_tls_configure_variant(
        context, (cnet_tls_variant)index, ca_file, cert_file, key_file,
        key_password, client_auth);
    if (status != SALTS_OK) {
      cnet_tls_context_dispose(context);
      return status;
    }
  }

  *out_context = context;
  return SALTS_OK;
}

int cnet_tls_client_context_create(const cnet_tls_client_config *config,
                                   cnet_tls_context **out_context) {
  cnet_tls_client_config defaults = {sizeof(defaults)};
  cnet_tls_context *context = NULL;
  int status;

  if (out_context == NULL) return SALTS_EINVAL;
  *out_context = NULL;
  if (config == NULL) config = &defaults;
  if (config->size != sizeof(*config) ||
      !cnet_tls_optional_path_valid(config->ca_file) ||
      !cnet_tls_optional_path_valid(config->ca_path) ||
      !cnet_tls_optional_path_valid(config->cert_file) ||
      !cnet_tls_optional_path_valid(config->key_file) ||
      !cnet_tls_bounded_string(config->key_password,
                               CNET_TLS_PASSWORD_MAX_BYTES, NULL) ||
      !cnet_tls_bounded_string(config->server_name,
                               CNET_TLS_SERVER_NAME_CAPACITY - 1u, NULL) ||
      (config->server_name != NULL && config->server_name[0] == '\0') ||
      ((config->cert_file == NULL) != (config->key_file == NULL)) ||
      (config->cert_file == NULL && config->key_password != NULL))
    return SALTS_EINVAL;

  status = cnet_tls_context_create_common(
      false, true, config->ca_file, config->ca_path, config->cert_file,
      config->key_file, config->key_password, CNET_TLS_CLIENT_AUTH_NONE,
      config->alpn_protocols, config->alpn_protocol_count, &context);
  if (status != SALTS_OK) return status;
  if (config->server_name != NULL)
    memcpy(context->client_server_name, config->server_name,
           strlen(config->server_name) + 1u);
  *out_context = context;
  return SALTS_OK;
}

int cnet_tls_client_init(cnet_tls_client *client,
                         const cnet_tls_client_config *config) {
  cnet_tls_context *context = NULL;
  int status;
  if (client == NULL || config == NULL) return SALTS_EINVAL;
  if (client->impl != NULL) return SALTS_EALREADY;
  status = cnet_tls_client_context_create(config, &context);
  if (status != SALTS_OK) return status;
  client->impl = context;
  return SALTS_OK;
}

int cnet_tls_client_destroy(cnet_tls_client *client) {
  cnet_tls_context *context;
  if (client == NULL) return SALTS_EINVAL;
  context = (cnet_tls_context *)client->impl;
  if (context == NULL) return SALTS_OK;
  client->impl = NULL;
  cnet_tls_context_release(context);
  return SALTS_OK;
}

cnet_tls_context *cnet_tls_client_context(const cnet_tls_client *client) {
  cnet_tls_context *context =
      client != NULL ? (cnet_tls_context *)client->impl : NULL;
  return context != NULL && !context->server ? context : NULL;
}

const char *cnet_tls_client_server_name(const cnet_tls_client *client) {
  const cnet_tls_context *context = cnet_tls_client_context(client);
  return context != NULL && context->client_server_name[0] != '\0'
             ? context->client_server_name
             : NULL;
}

int cnet_tls_server_init(cnet_tls_server *server,
                         const cnet_tls_server_config *config) {
  cnet_tls_context *context = NULL;
  int status;

  if (server == NULL || config == NULL) return SALTS_EINVAL;
  if (server->impl != NULL) return SALTS_EALREADY;
  if (config->size != sizeof(*config) ||
      !cnet_tls_optional_path_valid(config->cert_file) ||
      !cnet_tls_optional_path_valid(config->key_file) ||
      config->cert_file == NULL || config->key_file == NULL ||
      !cnet_tls_optional_path_valid(config->ca_file) ||
      !cnet_tls_optional_path_valid(config->ca_path) ||
      !cnet_tls_bounded_string(config->key_password,
                               CNET_TLS_PASSWORD_MAX_BYTES, NULL) ||
      (config->client_auth != CNET_TLS_CLIENT_AUTH_NONE &&
       config->client_auth != CNET_TLS_CLIENT_AUTH_REQUIRED) ||
      (config->client_auth == CNET_TLS_CLIENT_AUTH_REQUIRED &&
       config->ca_file == NULL && config->ca_path == NULL) ||
      (config->client_auth == CNET_TLS_CLIENT_AUTH_NONE &&
       (config->ca_file != NULL || config->ca_path != NULL)))
    return SALTS_EINVAL;

  status = cnet_tls_context_create_common(
      true, config->client_auth == CNET_TLS_CLIENT_AUTH_REQUIRED,
      config->ca_file, config->ca_path, config->cert_file, config->key_file,
      config->key_password, config->client_auth, config->alpn_protocols,
      config->alpn_protocol_count, &context);
  if (status != SALTS_OK) return status;
  server->impl = context;
  return SALTS_OK;
}

int cnet_tls_server_destroy(cnet_tls_server *server) {
  cnet_tls_context *context;
  if (server == NULL) return SALTS_EINVAL;
  context = (cnet_tls_context *)server->impl;
  if (context == NULL) return SALTS_OK;
  server->impl = NULL;
  cnet_tls_context_release(context);
  return SALTS_OK;
}

cnet_tls_context *cnet_tls_server_context(const cnet_tls_server *server) {
  cnet_tls_context *context =
      server != NULL ? (cnet_tls_context *)server->impl : NULL;
  return context != NULL && context->server ? context : NULL;
}

static size_t cnet_tls_ring_write(unsigned char *buffer, size_t capacity,
                                  size_t head, size_t size,
                                  const unsigned char *data, size_t length) {
  size_t tail;
  size_t first;
  if (buffer == NULL || data == NULL || capacity == 0u ||
      size >= capacity || length == 0u)
    return 0u;
  if (length > capacity - size) length = capacity - size;
  tail = (head + size) % capacity;
  first = capacity - tail;
  if (first > length) first = length;
  memcpy(buffer + tail, data, first);
  if (length > first) memcpy(buffer, data + first, length - first);
  return length;
}

static size_t cnet_tls_ring_read(unsigned char *buffer, size_t capacity,
                                 size_t *head, size_t *size,
                                 unsigned char *data, size_t length) {
  size_t first;
  if (buffer == NULL || head == NULL || size == NULL || data == NULL ||
      capacity == 0u || *size == 0u || length == 0u)
    return 0u;
  if (length > *size) length = *size;
  first = capacity - *head;
  if (first > length) first = length;
  memcpy(data, buffer + *head, first);
  if (length > first) memcpy(data + first, buffer, length - first);
  *head = (*head + length) % capacity;
  *size -= length;
  return length;
}

static tls_ret_t cnet_tls_io_send(void *user, const void *buffer, size_t length,
                                  int flags) {
  cnet_tls_gmssl_state *engine = (cnet_tls_gmssl_state *)user;
  size_t written;
  (void)flags;
  if (engine == NULL || buffer == NULL || length == 0u)
    return TLS_ERROR_SYSCALL;
  written = cnet_tls_ring_write(
      engine->cipher_output, engine->capacity, engine->output_head,
      engine->output_size, (const unsigned char *)buffer, length);
  if (written == 0u) return TLS_ERROR_SEND_AGAIN;
  engine->output_size += written;
  return (tls_ret_t)written;
}

static tls_ret_t cnet_tls_io_recv(void *user, void *buffer, size_t length,
                                  int flags) {
  cnet_tls_gmssl_state *engine = (cnet_tls_gmssl_state *)user;
  size_t read_size;
  (void)flags;
  if (engine == NULL || buffer == NULL || length == 0u)
    return TLS_ERROR_SYSCALL;
  read_size = cnet_tls_ring_read(
      engine->cipher_input, engine->capacity, &engine->input_head,
      &engine->input_size, (unsigned char *)buffer, length);
  if (read_size == 0u) return TLS_ERROR_RECV_AGAIN;
  return (tls_ret_t)read_size;
}

static bool cnet_tls_ip_literal(const char *name) {
  unsigned char ipv4[4];
  unsigned char ipv6[16];
  if (name == NULL) return false;
  return inet_pton(AF_INET, name, ipv4) == 1 ||
         inet_pton(AF_INET6, name, ipv6) == 1;
}

static int cnet_tls_engine_init_connection(cnet_tls_state *state,
                                           cnet_tls_variant variant) {
  cnet_tls_gmssl_state *engine;
  TLS_CTX *tls;
  if (state == NULL || state->context == NULL ||
      (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  if (variant < 0 || variant >= CNET_TLS_VARIANT_COUNT ||
      !state->context->tls_ready[variant])
    return SALTS_EINVAL;
  if (engine->input_size != 0u || engine->output_size != 0u ||
      engine->plaintext_pending_size != 0u)
    return SALTS_EBUSY;

  if (engine->connection_ready) {
    tls_cleanup(&engine->connection);
    memset(&engine->connection, 0, sizeof(engine->connection));
    engine->connection_ready = false;
  }

  tls = &state->context->tls[variant];
  if (tls_init(&engine->connection, tls) != 1) return SALTS_EIO;
  engine->connection_ready = true;
  engine->variant = variant;
  engine->io.user = engine;
  engine->io.send = cnet_tls_io_send;
  engine->io.recv = cnet_tls_io_recv;

  if (!state->server) {
    if (engine->server_name[0] == '\0') return SALTS_EINVAL;
    if (cnet_tls_ip_literal(engine->server_name)) return SALTS_ENOTSUP;
    if (tls_set_hostname(&engine->connection, engine->server_name) != 1 ||
        tls_set_server_name(&engine->connection) != 1)
      return SALTS_EIO;
  }
  if (tls_set_io(&engine->connection, &engine->io) != 1)
    return SALTS_EIO;
  return SALTS_OK;
}

int cnet_tls_state_init(cnet_tls_state *state, cnet_tls_context *context,
                        bool server, const char *server_name,
                        size_t io_buffer_bytes) {
  cnet_tls_gmssl_state *engine = NULL;
  unsigned char *read_buffer = NULL;
  unsigned char *write_buffer = NULL;
  size_t allocation_size;
  int status;

  if (state == NULL || context == NULL || context->server != server ||
      io_buffer_bytes < CNET_TLS_MIN_IO_BUFFER_BYTES ||
      (!server && (server_name == NULL || server_name[0] == '\0')))
    return SALTS_EINVAL;
  if (state->engine != NULL || state->context != NULL) return SALTS_EALREADY;
  if (io_buffer_bytes >
      (SIZE_MAX - sizeof(cnet_tls_gmssl_state)) / 2u)
    return SALTS_ERANGE;

  allocation_size = sizeof(cnet_tls_gmssl_state) + io_buffer_bytes * 2u;
  engine = (cnet_tls_gmssl_state *)calloc(1u, allocation_size);
  read_buffer = (unsigned char *)malloc(io_buffer_bytes);
  write_buffer = (unsigned char *)malloc(io_buffer_bytes);
  if (engine == NULL || read_buffer == NULL || write_buffer == NULL) {
    free(write_buffer);
    free(read_buffer);
    free(engine);
    return SALTS_ENOMEM;
  }

  engine->capacity = io_buffer_bytes;
  engine->cipher_input = (unsigned char *)(engine + 1);
  engine->cipher_output = engine->cipher_input + io_buffer_bytes;
  if (!server)
    memcpy(engine->server_name, server_name, strlen(server_name) + 1u);

  state->context = context;
  state->engine = engine;
  state->read_buffer = read_buffer;
  state->write_buffer = write_buffer;
  state->io_buffer_bytes = io_buffer_bytes;
  state->server = server;

  status = cnet_tls_engine_init_connection(state, CNET_TLS_VARIANT_RANGE);
  if (status != SALTS_OK) {
    state->context = NULL;
    state->engine = NULL;
    state->read_buffer = NULL;
    state->write_buffer = NULL;
    state->io_buffer_bytes = 0u;
    state->server = false;
    if (engine->connection_ready) tls_cleanup(&engine->connection);
    free(write_buffer);
    free(read_buffer);
    free(engine);
    return status;
  }
  return SALTS_OK;
}

void cnet_tls_state_destroy(cnet_tls_state *state) {
  cnet_tls_context *context;
  cnet_tls_gmssl_state *engine;
  if (state == NULL) return;
  context = state->context;
  engine = CNET_TLS_ENGINE(state);
  if (engine != NULL && engine->connection_ready)
    tls_cleanup(&engine->connection);
  free(engine);
  free(state->write_buffer);
  free(state->read_buffer);
  memset(state, 0, sizeof(*state));
  cnet_tls_context_release(context);
}

static bool cnet_tls_retryable(int result) {
  return result == TLS_ERROR_RECV_AGAIN || result == TLS_ERROR_SEND_AGAIN;
}

static int cnet_tls_finish_handshake(cnet_tls_state *state,
                                     bool *out_complete) {
  cnet_tls_gmssl_state *engine = CNET_TLS_ENGINE(state);
  int complete = 0;
  int verify_result = X509_verify_ok;
  const char *alpn = NULL;
  size_t alpn_size = 0u;
  int result;

  if (tls_get_handshake_complete(&engine->connection, &complete) != 1)
    return SALTS_EIO;
  if (!complete) return SALTS_OK;

  if (state->context->verify_peer) {
    result = tls_get_verify_result(&engine->connection, &verify_result);
    if (result != 1 || verify_result != X509_verify_ok)
      return SALTS_ECONNABORTED;
  }

  result = tls_get_selected_alpn(&engine->connection, &alpn, &alpn_size);
  if (result < 0) return SALTS_EPROTO;
  if (result == 1) {
    if (alpn_size == 0u || alpn_size > CNET_TLS_ALPN_NAME_MAX_BYTES)
      return SALTS_EPROTO;
    memcpy(state->negotiated_alpn, alpn, alpn_size);
    state->negotiated_alpn_size = alpn_size;
  }

  state->handshake_complete = true;
  *out_complete = true;
  return SALTS_OK;
}

int cnet_tls_handshake(cnet_tls_state *state, bool *out_complete) {
  cnet_tls_gmssl_state *engine;
  int result;
  int status;
  if (state == NULL || out_complete == NULL ||
      (engine = CNET_TLS_ENGINE(state)) == NULL ||
      !engine->connection_ready)
    return SALTS_EINVAL;
  *out_complete = state->handshake_complete;
  if (state->handshake_complete) return SALTS_OK;

  result = tls_do_handshake(&engine->connection);
  if (result != 1 && !cnet_tls_retryable(result))
    return SALTS_ECONNABORTED;
  status = cnet_tls_finish_handshake(state, out_complete);
  return status;
}

bool cnet_tls_state_handshake_complete(const cnet_tls_state *state) {
  return state != NULL && state->handshake_complete;
}

static int cnet_tls_select_variant(cnet_tls_protocol_version minimum,
                                   cnet_tls_protocol_version maximum,
                                   cnet_tls_variant *out_variant) {
  cnet_tls_protocol_version effective_min = minimum;
  cnet_tls_protocol_version effective_max = maximum;
  if (out_variant == NULL) return SALTS_EINVAL;
  if (minimum != CNET_TLS_PROTOCOL_VERSION_DEFAULT &&
      minimum != CNET_TLS_PROTOCOL_VERSION_1_2 &&
      minimum != CNET_TLS_PROTOCOL_VERSION_1_3)
    return SALTS_EINVAL;
  if (maximum != CNET_TLS_PROTOCOL_VERSION_DEFAULT &&
      maximum != CNET_TLS_PROTOCOL_VERSION_1_2 &&
      maximum != CNET_TLS_PROTOCOL_VERSION_1_3)
    return SALTS_EINVAL;

  if (effective_min == CNET_TLS_PROTOCOL_VERSION_DEFAULT)
    effective_min = CNET_TLS_PROTOCOL_VERSION_1_2;
  if (effective_max == CNET_TLS_PROTOCOL_VERSION_DEFAULT)
    effective_max = CNET_TLS_PROTOCOL_VERSION_1_3;
  if (effective_min > effective_max) return SALTS_EINVAL;

  if (effective_min == CNET_TLS_PROTOCOL_VERSION_1_2 &&
      effective_max == CNET_TLS_PROTOCOL_VERSION_1_2)
    *out_variant = CNET_TLS_VARIANT_1_2;
  else if (effective_min == CNET_TLS_PROTOCOL_VERSION_1_3 &&
           effective_max == CNET_TLS_PROTOCOL_VERSION_1_3)
    *out_variant = CNET_TLS_VARIANT_1_3;
  else
    *out_variant = CNET_TLS_VARIANT_RANGE;
  return SALTS_OK;
}

int cnet_tls_state_set_protocol_range(cnet_tls_state *state,
                                      cnet_tls_protocol_version minimum,
                                      cnet_tls_protocol_version maximum) {
  cnet_tls_variant variant;
  int status;
  if (state == NULL || CNET_TLS_ENGINE(state) == NULL ||
      state->handshake_complete)
    return SALTS_EINVAL;
  status = cnet_tls_select_variant(minimum, maximum, &variant);
  if (status != SALTS_OK) return status;
  if (CNET_TLS_ENGINE(state)->variant == variant) return SALTS_OK;
  state->negotiated_alpn_size = 0u;
  memset(state->negotiated_alpn, 0, sizeof(state->negotiated_alpn));
  return cnet_tls_engine_init_connection(state, variant);
}

void *cnet_tls_state_read_buffer(cnet_tls_state *state) {
  return state != NULL ? state->read_buffer : NULL;
}

void *cnet_tls_state_write_buffer(cnet_tls_state *state) {
  return state != NULL ? state->write_buffer : NULL;
}

size_t cnet_tls_state_io_buffer_bytes(const cnet_tls_state *state) {
  return state != NULL ? state->io_buffer_bytes : 0u;
}

size_t cnet_tls_cipher_input_capacity(const cnet_tls_state *state) {
  const cnet_tls_gmssl_state *engine =
      state != NULL ? CNET_TLS_ENGINE(state) : NULL;
  return engine != NULL && engine->input_size <= engine->capacity
             ? engine->capacity - engine->input_size
             : 0u;
}

int cnet_tls_feed_cipher(cnet_tls_state *state, const void *data, size_t size) {
  cnet_tls_gmssl_state *engine;
  size_t written;
  if (state == NULL || data == NULL || size == 0u ||
      (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  if (size > engine->capacity - engine->input_size) return SALTS_ENOBUFS;
  written = cnet_tls_ring_write(
      engine->cipher_input, engine->capacity, engine->input_head,
      engine->input_size, (const unsigned char *)data, size);
  if (written != size) return SALTS_EIO;
  engine->input_size += written;
  return SALTS_OK;
}

int cnet_tls_take_cipher(cnet_tls_state *state, void *buffer,
                         size_t capacity, size_t *out_size) {
  cnet_tls_gmssl_state *engine;
  size_t read_size;
  if (out_size == NULL) return SALTS_EINVAL;
  *out_size = 0u;
  if (state == NULL || buffer == NULL || capacity == 0u ||
      (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  if (engine->output_size == 0u) return SALTS_ENOENT;
  read_size = cnet_tls_ring_read(
      engine->cipher_output, engine->capacity, &engine->output_head,
      &engine->output_size, (unsigned char *)buffer, capacity);
  if (read_size == 0u) return SALTS_EIO;
  *out_size = read_size;
  return SALTS_OK;
}

int cnet_tls_write(cnet_tls_state *state, const void *data, size_t size,
                   bool *out_complete) {
  cnet_tls_gmssl_state *engine;
  size_t sent_size = 0u;
  int result;
  if (state == NULL || data == NULL || size == 0u ||
      out_complete == NULL ||
      (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  *out_complete = false;
  if (!state->handshake_complete || state->close_notify_started)
    return SALTS_ENOTCONN;

  result = tls_send(&engine->connection, (const uint8_t *)data, size,
                    &sent_size);
  if (result == 1) {
    if (sent_size != size) return SALTS_EPROTO;
    *out_complete = true;
    return SALTS_OK;
  }
  if (cnet_tls_retryable(result)) return SALTS_OK;
  return SALTS_EPROTO;
}

static int cnet_tls_read_pending(cnet_tls_state *state, void *buffer,
                                 size_t capacity, size_t *out_size) {
  cnet_tls_gmssl_state *engine = CNET_TLS_ENGINE(state);
  size_t available;
  size_t size;
  if (engine == NULL || engine->plaintext_pending_size == 0u)
    return SALTS_ENOENT;
  available =
      engine->plaintext_pending_size - engine->plaintext_pending_offset;
  size = capacity < available ? capacity : available;
  memcpy(buffer, state->read_buffer + engine->plaintext_pending_offset, size);
  engine->plaintext_pending_offset += size;
  if (engine->plaintext_pending_offset == engine->plaintext_pending_size) {
    engine->plaintext_pending_offset = 0u;
    engine->plaintext_pending_size = 0u;
  }
  *out_size = size;
  return SALTS_OK;
}

int cnet_tls_read(cnet_tls_state *state, void *buffer, size_t capacity,
                  size_t *out_size, bool *out_peer_closed) {
  cnet_tls_gmssl_state *engine;
  size_t received_size = 0u;
  int result;
  int status;
  if (out_size == NULL || out_peer_closed == NULL) return SALTS_EINVAL;
  *out_size = 0u;
  *out_peer_closed = false;
  if (state == NULL || buffer == NULL || capacity == 0u ||
      (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  if (!state->handshake_complete) return SALTS_ENOTCONN;
  if (state->peer_close_notify) {
    *out_peer_closed = true;
    return SALTS_OK;
  }

  status = cnet_tls_read_pending(state, buffer, capacity, out_size);
  if (status == SALTS_OK) return SALTS_OK;
  if (status != SALTS_ENOENT) return status;

  result = tls_recv(&engine->connection, (uint8_t *)buffer, capacity,
                    &received_size);
  if (result == 1) {
    *out_size = received_size;
    return SALTS_OK;
  }
  if (result == 0 || result == TLS_ERROR_TCP_CLOSED) {
    state->peer_close_notify = true;
    *out_peer_closed = true;
    return SALTS_OK;
  }
  if (cnet_tls_retryable(result)) return SALTS_OK;
  return SALTS_EPROTO;
}

int cnet_tls_probe_peer_close(cnet_tls_state *state, bool *out_peer_closed,
                              bool *out_plaintext_pending) {
  cnet_tls_gmssl_state *engine;
  size_t received_size = 0u;
  int result;
  int received = 0;

  if (out_peer_closed == NULL || out_plaintext_pending == NULL)
    return SALTS_EINVAL;
  *out_peer_closed = false;
  *out_plaintext_pending = false;
  if (state == NULL || (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  if (!state->handshake_complete) return SALTS_ENOTCONN;
  if (state->peer_close_notify) {
    *out_peer_closed = true;
    return SALTS_OK;
  }
  if (engine->plaintext_pending_size != 0u) {
    *out_plaintext_pending = true;
    return SALTS_OK;
  }

  result = tls_recv(&engine->connection, state->read_buffer,
                    state->io_buffer_bytes, &received_size);
  if (result == 1 && received_size != 0u) {
    engine->plaintext_pending_offset = 0u;
    engine->plaintext_pending_size = received_size;
    *out_plaintext_pending = true;
    return SALTS_OK;
  }
  if (result == 0 || result == TLS_ERROR_TCP_CLOSED) {
    state->peer_close_notify = true;
    *out_peer_closed = true;
    return SALTS_OK;
  }
  if (!cnet_tls_retryable(result) && result != 1) return SALTS_EPROTO;

  if (tls_get_peer_close_notify(&engine->connection, &received) != 1)
    return SALTS_EIO;
  if (received) {
    state->peer_close_notify = true;
    *out_peer_closed = true;
  }
  return SALTS_OK;
}

int cnet_tls_shutdown(cnet_tls_state *state, bool *out_notify_generated) {
  cnet_tls_gmssl_state *engine;
  size_t output_before;
  int result;
  if (state == NULL || out_notify_generated == NULL ||
      (engine = CNET_TLS_ENGINE(state)) == NULL)
    return SALTS_EINVAL;
  *out_notify_generated = state->close_notify_started;
  if (!state->handshake_complete) return SALTS_ENOTCONN;

  output_before = engine->output_size;
  result = tls_shutdown(&engine->connection);
  if (engine->output_size > output_before) state->close_notify_started = true;
  *out_notify_generated = state->close_notify_started;

  if (result == 1 || result == TLS_ERROR_TCP_CLOSED ||
      cnet_tls_retryable(result))
    return SALTS_OK;
  return SALTS_EPROTO;
}

int cnet_tls_get_negotiated_alpn(const cnet_tls_state *state,
                                 const unsigned char **out_data,
                                 size_t *out_size) {
  if (out_data == NULL || out_size == NULL) return SALTS_EINVAL;
  *out_data = NULL;
  *out_size = 0u;
  if (state == NULL || CNET_TLS_ENGINE(state) == NULL) return SALTS_EINVAL;
  if (!state->handshake_complete) return SALTS_ENOTCONN;
  if (state->negotiated_alpn_size == 0u) return SALTS_ENOENT;
  *out_data = state->negotiated_alpn;
  *out_size = state->negotiated_alpn_size;
  return SALTS_OK;
}

static int cnet_tls_state_copy_session_string(const cnet_tls_state *state,
                                              const char *value,
                                              char *buffer, size_t capacity,
                                              size_t *out_size) {
  size_t size;
  if (out_size == NULL) return SALTS_EINVAL;
  *out_size = 0u;
  if (state == NULL || buffer == NULL || capacity == 0u)
    return SALTS_EINVAL;
  buffer[0] = '\0';
  if (CNET_TLS_ENGINE(state) == NULL || !state->handshake_complete)
    return SALTS_ENOTCONN;
  if (value == NULL || value[0] == '\0') return SALTS_ENOENT;
  size = strlen(value);
  if (capacity <= size) return SALTS_EMSGSIZE;
  memcpy(buffer, value, size + 1u);
  *out_size = size;
  return SALTS_OK;
}

int cnet_tls_state_negotiated_version(const cnet_tls_state *state,
                                      char *buffer, size_t capacity,
                                      size_t *out_size) {
  int protocol = 0;
  int result;
  const char *name = NULL;
  if (state == NULL || CNET_TLS_ENGINE(state) == NULL)
    return cnet_tls_state_copy_session_string(state, NULL, buffer, capacity,
                                              out_size);
  if (!state->handshake_complete)
    return cnet_tls_state_copy_session_string(state, NULL, buffer, capacity,
                                              out_size);
  result = tls_get_negotiated_protocol(
      &CNET_TLS_ENGINE(state)->connection, &protocol);
  if (result < 0) return SALTS_EIO;
  if (result == 0) return SALTS_ENOENT;
  if (protocol == TLS_protocol_tls12)
    name = "TLSv1.2";
  else if (protocol == TLS_protocol_tls13)
    name = "TLSv1.3";
  else
    return SALTS_EPROTO;
  return cnet_tls_state_copy_session_string(state, name, buffer, capacity,
                                            out_size);
}

int cnet_tls_state_negotiated_cipher(const cnet_tls_state *state,
                                     char *buffer, size_t capacity,
                                     size_t *out_size) {
  const char *name = NULL;
  int result;
  if (state == NULL || CNET_TLS_ENGINE(state) == NULL)
    return cnet_tls_state_copy_session_string(state, NULL, buffer, capacity,
                                              out_size);
  if (!state->handshake_complete)
    return cnet_tls_state_copy_session_string(state, NULL, buffer, capacity,
                                              out_size);
  result = tls_get_negotiated_cipher_name(
      &CNET_TLS_ENGINE(state)->connection, &name);
  if (result < 0) return SALTS_EIO;
  if (result == 0) return SALTS_ENOENT;
  return cnet_tls_state_copy_session_string(state, name, buffer, capacity,
                                            out_size);
}

int cnet_tls_state_peer_certificate_sha256(
    const cnet_tls_state *state,
    char buffer[CNET_TLS_PEER_CERTIFICATE_SHA256_CAPACITY]) {
  static const char hex[] = "0123456789abcdef";
  uint8_t digest[32];
  size_t index;
  int result;

  if (state == NULL || buffer == NULL) return SALTS_EINVAL;
  buffer[0] = '\0';
  if (CNET_TLS_ENGINE(state) == NULL || !state->handshake_complete)
    return SALTS_ENOTCONN;
  result = tls_get_peer_certificate_sha256(
      &CNET_TLS_ENGINE(state)->connection, digest);
  if (result < 0) return SALTS_EIO;
  if (result == 0) return SALTS_ENOENT;
  for (index = 0u; index < sizeof(digest); ++index) {
    buffer[index * 2u] = hex[digest[index] >> 4u];
    buffer[index * 2u + 1u] = hex[digest[index] & 0x0fu];
  }
  buffer[sizeof(digest) * 2u] = '\0';
  return SALTS_OK;
}

int cnet_tls_state_export_channel_binding(
    const cnet_tls_state *state,
    uint8_t output[CNET_TLS_CHANNEL_BINDING_BYTES]) {
  static const char exporter_label[] = "EXPORTER-Channel-Binding";
  if (output == NULL) return SALTS_EINVAL;
  memset(output, 0, CNET_TLS_CHANNEL_BINDING_BYTES);
  if (state == NULL || CNET_TLS_ENGINE(state) == NULL ||
      !state->handshake_complete)
    return SALTS_ENOTCONN;
  return tls_export_keying_material(
             &CNET_TLS_ENGINE(state)->connection, output,
             CNET_TLS_CHANNEL_BINDING_BYTES, exporter_label, NULL, 0u, 1) == 1
             ? SALTS_OK
             : SALTS_EIO;
}
