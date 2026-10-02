#include <gmssl/tls.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct cnet_gmssl_probe_io {
  size_t send_calls;
  size_t recv_calls;
} cnet_gmssl_probe_io;

static tls_ret_t cnet_gmssl_probe_send(void *user, const void *buf, size_t len, int flags) {
  cnet_gmssl_probe_io *io = (cnet_gmssl_probe_io *)user;
  (void)buf;
  (void)len;
  (void)flags;
  if (io == NULL) return TLS_ERROR_SYSCALL;
  ++io->send_calls;
  return TLS_ERROR_SEND_AGAIN;
}

static tls_ret_t cnet_gmssl_probe_recv(void *user, void *buf, size_t len, int flags) {
  cnet_gmssl_probe_io *io = (cnet_gmssl_probe_io *)user;
  (void)buf;
  (void)len;
  (void)flags;
  if (io == NULL) return TLS_ERROR_SYSCALL;
  ++io->recv_calls;
  return TLS_ERROR_RECV_AGAIN;
}

int main(void) {
  TLS_CONNECT conn;
  TLS_IO callbacks;
  cnet_gmssl_probe_io io = {0};
  unsigned char byte = 0u;

  if (sizeof(TLS_CTX) != tls_ctx_sizeof()) return 5;
  if (sizeof(TLS_CONNECT) != tls_connect_sizeof()) return 6;

  memset(&conn, 0, sizeof(conn));
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.user = &io;
  callbacks.send = cnet_gmssl_probe_send;
  callbacks.recv = cnet_gmssl_probe_recv;

  if (tls_set_io(&conn, &callbacks) != 1) return 1;
  if (tls_io_send(&conn, &byte, 1u, 0) >= 0) return 2;
  if (tls_io_recv(&conn, &byte, 1u, 0) >= 0) return 3;
  if (io.send_calls != 1u || io.recv_calls != 1u) return 4;

  return 0;
}
