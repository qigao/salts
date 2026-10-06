#include <gmssl/tls.h>
#include <tinytest.h>

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

suite("CNet GmSSL contract") {
  group("linked provider ABI") {
    it("matches the public context and connection sizes") {
      check_equal(sizeof(TLS_CTX), tls_ctx_sizeof());
      check_equal(sizeof(TLS_CONNECT), tls_connect_sizeof());
    }
  }
  group("custom IO callbacks") {
    it("dispatches send and receive through the configured provider") {
      TLS_CONNECT conn = {0};
      TLS_IO callbacks = {0};
      cnet_gmssl_probe_io io = {0};
      unsigned char byte = 0u;
      callbacks.user = &io;
      callbacks.send = cnet_gmssl_probe_send;
      callbacks.recv = cnet_gmssl_probe_recv;

      check_equal(tls_set_io(&conn, &callbacks), 1);
      check_less(tls_io_send(&conn, &byte, 1u, 0), 0);
      check_less(tls_io_recv(&conn, &byte, 1u, 0), 0);
      check_equal(io.send_calls, 1u);
      check_equal(io.recv_calls, 1u);
    }
  }
}
