#ifndef CNET_TLS_H
#define CNET_TLS_H

#include <cnet/cnet.h>

#include <stdbool.h>
#include <stddef.h>

enum { CNET_TLS_SERVER_NAME_CAPACITY = 254 };

typedef struct cnet_tls_context cnet_tls_context;

typedef struct cnet_tls_state {
  cnet_tls_context *context;
  void *ssl;
  void *network_bio;
  unsigned char *read_buffer;
  unsigned char *write_buffer;
  size_t io_buffer_bytes;
  size_t negotiated_alpn_size;
  unsigned char negotiated_alpn[CNET_TLS_ALPN_NAME_MAX_BYTES];
  bool server;
  bool handshake_complete;
  bool peer_close_notify;
  bool close_notify_started;
} cnet_tls_state;

int cnet_tls_client_context_create(const cnet_tls_client_config *config,
                                   cnet_tls_context **out_context);
cnet_tls_context *cnet_tls_client_context(const cnet_tls_client *client);
const char *cnet_tls_client_server_name(const cnet_tls_client *client);
cnet_tls_context *cnet_tls_server_context(const cnet_tls_server *server);
void cnet_tls_context_retain(cnet_tls_context *context);
void cnet_tls_context_release(cnet_tls_context *context);

/** Takes ownership of `context` only on success. */
int cnet_tls_state_init(cnet_tls_state *state, cnet_tls_context *context, bool server,
                        const char *server_name, size_t io_buffer_bytes);
void cnet_tls_state_destroy(cnet_tls_state *state);

int cnet_tls_handshake(cnet_tls_state *state, bool *out_complete);
size_t cnet_tls_cipher_input_capacity(const cnet_tls_state *state);
int cnet_tls_feed_cipher(cnet_tls_state *state, const void *data, size_t size);
int cnet_tls_take_cipher(cnet_tls_state *state, void *buffer, size_t capacity, size_t *out_size);
int cnet_tls_write(cnet_tls_state *state, const void *data, size_t size, bool *out_complete);
int cnet_tls_read(cnet_tls_state *state, void *buffer, size_t capacity, size_t *out_size,
                  bool *out_peer_closed);
int cnet_tls_shutdown(cnet_tls_state *state, bool *out_notify_generated);
int cnet_tls_get_negotiated_alpn(const cnet_tls_state *state, const unsigned char **out_data,
                                 size_t *out_size);

#endif /* CNET_TLS_H */
