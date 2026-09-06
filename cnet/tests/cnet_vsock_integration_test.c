#include <cnet/cnet.h>

#include <salts/error_codes.h>

#include <errno.h>
#include <linux/vm_sockets.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { CNET_VSOCK_TEST_ATTEMPTS = 200, CNET_VSOCK_TEST_POLL_MS = 10 };

typedef struct cnet_vsock_test_probe {
  int connected;
  int received;
  int terminal;
  int failed;
  unsigned char value;
} cnet_vsock_test_probe;

static void cnet_vsock_test_state(void *user, cnet_connection connection,
                                  cnet_connection_state state, const cnet_error *error) {
  cnet_vsock_test_probe *probe = (cnet_vsock_test_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) probe->connected = 1;
  if (state == CNET_CONNECTION_FAILED) probe->failed = 1;
  if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) probe->terminal = 1;
  if (error != NULL) probe->failed = 1;
}

static void cnet_vsock_test_receive(void *user, cnet_connection connection,
                                    const cnet_receive_view *view) {
  cnet_vsock_test_probe *probe = (cnet_vsock_test_probe *)user;
  (void)connection;
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES || view->size != 1u)
    probe->failed = 1;
  else probe->value = *(const unsigned char *)view->data;
  probe->received = 1;
}

static int cnet_vsock_test_runtime_unavailable(int status) {
  return status == SALTS_EAFNOSUPPORT || status == SALTS_EPROTONOSUPPORT ||
         status == SALTS_ENOTSUP || status == SALTS_EADDRNOTAVAIL || status == SALTS_EPERM;
}

static int cnet_vsock_test_poll_until(cnet_client *client, const int *condition) {
  int attempt;
  for (attempt = 0; attempt < CNET_VSOCK_TEST_ATTEMPTS && !*condition; ++attempt) {
    size_t events = 0u;
    const int status = cnet_client_poll(client, CNET_VSOCK_TEST_POLL_MS, &events);
    if (status != SALTS_OK) return status;
  }
  return *condition ? SALTS_OK : SALTS_ETIMEDOUT;
}

int main(void) {
  const cnet_client_config client_config = {.backend = NATIVE_IO_BACKEND_EPOLL,
                                            .connection_capacity = 1u,
                                            .command_capacity = 8u,
                                            .request_capacity = 4u,
                                            .completion_batch_capacity = 4u,
                                            .event_capacity = 8u,
                                            .max_send_bytes = 64u,
                                            .receive_buffer_bytes = 64u};
  cnet_vsock_listener_config listener_config = CNET_VSOCK_LISTENER_CONFIG_INIT;
  cnet_listener listener = {0};
  cnet_client client = {0};
  cnet_vsock_test_probe probe = {0};
  cnet_observer observer = {.on_state = cnet_vsock_test_state,
                            .on_receive = cnet_vsock_test_receive,
                            .user = &probe};
  cnet_vsock_peer local = {0};
  cnet_vsock_peer remote = {0};
  cnet_stream_peer tcp_peer = {0};
  cnet_connection connection = {0};
  struct sockaddr_vm address;
  int peer_socket = -1;
  int listener_initialized = 0;
  int client_initialized = 0;
  int status;
  int ready = 0;
  int attempt;
  uint16_t tcp_port = UINT16_MAX;
  const unsigned char inbound = 37u;
  const unsigned char outbound = 73u;
  unsigned char received = 0u;

  peer_socket = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (peer_socket < 0) {
    if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT || errno == ESOCKTNOSUPPORT ||
        errno == ENODEV || errno == EPERM)
      return 77;
    perror("socket(AF_VSOCK)");
    return 1;
  }
  (void)close(peer_socket);
  peer_socket = -1;

  listener_config.backend = NATIVE_IO_BACKEND_EPOLL;
  listener_config.cid = CNET_VSOCK_CID_ANY;
  listener_config.port = CNET_VSOCK_PORT_ANY;
  listener_config.backlog = 2u;
  status = cnet_listener_init_vsock(&listener, &listener_config);
  if (cnet_vsock_test_runtime_unavailable(status)) return 77;
  if (status != SALTS_OK) return 1;
  listener_initialized = 1;
  if (cnet_listener_vsock_local(&listener, &local) != SALTS_OK ||
      local.port == CNET_VSOCK_PORT_ANY)
    goto fail;
  if (cnet_client_init(&client, &client_config) != SALTS_OK) goto fail;
  client_initialized = 1;
  if (cnet_listener_port(&listener, &tcp_port) != SALTS_ENOTSUP || tcp_port != 0u) goto fail;
  connection = (cnet_connection){17u, 19u};
  if (cnet_listener_accept_peer(&listener, &client, &observer, &connection, &tcp_peer) !=
          SALTS_ENOTSUP ||
      connection.slot != 0u || connection.generation != 0u)
    goto fail;

  peer_socket = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (peer_socket < 0) goto fail;
  memset(&address, 0, sizeof(address));
  address.svm_family = AF_VSOCK;
  address.svm_cid = CNET_VSOCK_CID_LOCAL;
  address.svm_port = local.port;
  if (connect(peer_socket, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) != 0) {
    if (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT || errno == ENODEV) {
      (void)close(peer_socket);
      (void)cnet_client_stop(&client, 1000u);
      (void)cnet_client_destroy(&client);
      (void)cnet_listener_close(&listener);
      (void)cnet_listener_destroy(&listener);
      return 77;
    }
    goto fail;
  }
  if (cnet_listener_wait(&listener, 1000u, &ready) != SALTS_OK || !ready) goto fail;
  if (cnet_listener_accept_vsock_peer(&listener, &client, &observer, &connection, &remote) !=
      SALTS_OK)
    goto fail;
  if (remote.cid == CNET_VSOCK_CID_ANY || remote.port == CNET_VSOCK_PORT_ANY) goto fail;
  if (cnet_vsock_test_poll_until(&client, &probe.connected) != SALTS_OK || probe.failed) goto fail;

  if (cnet_receive(&client, connection, 1u) != SALTS_OK) goto fail;
  if (send(peer_socket, &inbound, sizeof(inbound), 0) != (ssize_t)sizeof(inbound)) goto fail;
  if (cnet_vsock_test_poll_until(&client, &probe.received) != SALTS_OK || probe.failed ||
      probe.value != inbound)
    goto fail;

  if (cnet_send(&client, connection, &outbound, sizeof(outbound)) != SALTS_OK) goto fail;
  for (attempt = 0; attempt < CNET_VSOCK_TEST_ATTEMPTS; ++attempt) {
    size_t events = 0u;
    ssize_t received_size;
    if (cnet_client_poll(&client, CNET_VSOCK_TEST_POLL_MS, &events) != SALTS_OK) goto fail;
    received_size = recv(peer_socket, &received, sizeof(received), MSG_DONTWAIT);
    if (received_size == (ssize_t)sizeof(received)) break;
    if (received_size == 0 || (received_size < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                               errno != EINTR))
      goto fail;
  }
  if (attempt == CNET_VSOCK_TEST_ATTEMPTS || received != outbound) goto fail;
  if (cnet_close(&client, connection) != SALTS_OK) goto fail;
  if (cnet_vsock_test_poll_until(&client, &probe.terminal) != SALTS_OK || probe.failed) goto fail;

  (void)close(peer_socket);
  peer_socket = -1;
  if (cnet_client_stop(&client, 1000u) != SALTS_OK || cnet_client_destroy(&client) != SALTS_OK)
    goto fail;
  client_initialized = 0;
  if (cnet_listener_close(&listener) != SALTS_OK || cnet_listener_destroy(&listener) != SALTS_OK)
    goto fail;
  listener_initialized = 0;
  return 0;

fail:
  if (peer_socket >= 0) (void)close(peer_socket);
  if (client_initialized) {
    (void)cnet_client_stop(&client, 1000u);
    (void)cnet_client_destroy(&client);
  }
  if (listener_initialized) {
    (void)cnet_listener_close(&listener);
    (void)cnet_listener_destroy(&listener);
  }
  return 1;
}
