#include "cnet_test_named_pipe.h"
#include "cnet_transport.h"
#include "tinytest.h"
#include <cnet/cnet.h>

#include <turbo/clock.h>
#include <turbo/thread.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_api_test_socket;
  #define CNET_API_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <pthread.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int cnet_api_test_socket;
  #define CNET_API_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_API_TEST_TIMEOUT_MS = 5000, CNET_API_TEST_BATCH_DATAGRAMS = 256 };

typedef struct cnet_api_test_probe {
  cnet_client *client;
  atomic_int connected;
  atomic_int received;
  atomic_int terminal;
  atomic_int callback_stop_status;
  atomic_int callback_operation_status;
  atomic_int failed;
  atomic_int block_terminal;
  atomic_int release_terminal;
  cnet_message_kind expected_kind;
  unsigned char received_value;
} cnet_api_test_probe;

typedef struct cnet_api_test_batch_probe {
  atomic_int connected;
  atomic_int received;
  atomic_int terminal;
  atomic_int failed;
  unsigned char received_value;
} cnet_api_test_batch_probe;

typedef struct cnet_api_test_poll_probe {
  cnet_client *client;
  atomic_int connected;
  atomic_int terminal;
  atomic_int callback_thread_marker;
  atomic_int failed;
} cnet_api_test_poll_probe;

typedef struct cnet_api_test_listener_probe {
  atomic_int connected;
  atomic_int received;
  atomic_int sent;
  atomic_int terminal;
  atomic_int failed;
  unsigned char received_value;
} cnet_api_test_listener_probe;

#if !defined(_WIN32)
typedef struct cnet_api_test_interrupt_probe {
  pthread_t target;
  atomic_int armed;
  atomic_int signal_status;
} cnet_api_test_interrupt_probe;
#endif

static TURBO_THREAD_LOCAL int cnet_api_test_thread_marker;

#if !defined(_WIN32)
static void cnet_api_test_signal_handler(int signal_number) { (void)signal_number; }

static void cnet_api_test_interrupt_wait(void *user) {
  cnet_api_test_interrupt_probe *probe = (cnet_api_test_interrupt_probe *)user;
  while (atomic_load_explicit(&probe->armed, memory_order_acquire) == 0)
    turbo_thread_yield();
  turbo_sleep_ms(10u);
  atomic_store_explicit(&probe->signal_status, pthread_kill(probe->target, SIGUSR1),
                        memory_order_release);
}
#endif

static void cnet_api_test_close_socket(cnet_api_test_socket socket_value) {
  if (socket_value == CNET_API_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_api_test_set_receive_timeout(cnet_api_test_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = CNET_API_TEST_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                    (int)sizeof(timeout_ms)) == 0
             ? TURBO_OK
             : TURBO_EIO;
#else
  const struct timeval timeout = {CNET_API_TEST_TIMEOUT_MS / 1000,
                                  (CNET_API_TEST_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout)) ==
                 0
             ? TURBO_OK
             : TURBO_EIO;
#endif
}

static int cnet_api_test_listener(cnet_api_test_socket *out_listener, uint16_t *out_port) {
  struct sockaddr_in address;
#if defined(_WIN32)
  int length = (int)sizeof(address);
#else
  socklen_t length = (socklen_t)sizeof(address);
#endif
  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_API_TEST_INVALID_SOCKET) return TURBO_EIO;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)&address, (int)sizeof(address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)&address, &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cnet_api_test_close_socket(*out_listener);
    *out_listener = CNET_API_TEST_INVALID_SOCKET;
    return TURBO_EIO;
  }
  *out_port = ntohs(address.sin_port);
  return TURBO_OK;
}

static int cnet_api_test_wait_pipe_read(cnet_shared_test_named_pipe *pipe, void *data,
                                        size_t size) {
#if !defined(_WIN32)
  const uint64_t deadline = turbo_monotonic_ms() + CNET_API_TEST_TIMEOUT_MS;
#endif
  for (;;) {
    const int status = cnet_shared_test_named_pipe_peer_read(pipe, data, size);
    if (status == TURBO_OK) return TURBO_OK;
#if defined(_WIN32)
    return status;
#else
    if (status != -EAGAIN && status != -EWOULDBLOCK && status != -EINTR) return status;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
    turbo_thread_yield();
#endif
  }
}

static void cnet_api_test_state(void *user, cnet_connection connection, cnet_connection_state state,
                                const cnet_error *error) {
  cnet_api_test_probe *probe = (cnet_api_test_probe *)user;
  if (state == CNET_CONNECTION_CONNECTED) {
    atomic_store_explicit(&probe->callback_stop_status,
                          cnet_client_stop(probe->client, CNET_API_TEST_TIMEOUT_MS),
                          memory_order_release);
    atomic_store_explicit(&probe->callback_operation_status,
                          cnet_receive(probe->client, connection, 1u), memory_order_release);
    atomic_fetch_add_explicit(&probe->connected, 1, memory_order_release);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_fetch_add_explicit(&probe->terminal, 1, memory_order_release);
    while (atomic_load_explicit(&probe->block_terminal, memory_order_acquire) != 0 &&
           atomic_load_explicit(&probe->release_terminal, memory_order_acquire) == 0)
      turbo_thread_yield();
  }
}

static void cnet_api_test_receive(void *user, cnet_connection connection,
                                  const cnet_receive_view *view) {
  cnet_api_test_probe *probe = (cnet_api_test_probe *)user;
  if (view->kind != probe->expected_kind || view->size != 1u)
    atomic_store_explicit(&probe->failed, 1, memory_order_release);
  else probe->received_value = *(const unsigned char *)view->data;
  atomic_store_explicit(&probe->callback_operation_status, cnet_close(probe->client, connection),
                        memory_order_release);
  atomic_fetch_add_explicit(&probe->received, 1, memory_order_release);
}

static void cnet_api_test_ignore_state(void *user, cnet_connection connection,
                                       cnet_connection_state state, const cnet_error *error) {
  (void)user;
  (void)connection;
  (void)state;
  (void)error;
}

static void cnet_api_test_poll_state(void *user, cnet_connection connection,
                                     cnet_connection_state state, const cnet_error *error) {
  cnet_api_test_poll_probe *probe = (cnet_api_test_poll_probe *)user;
  if (state == CNET_CONNECTION_CONNECTED) {
    atomic_store_explicit(&probe->callback_thread_marker, cnet_api_test_thread_marker,
                          memory_order_release);
    if (cnet_close(probe->client, connection) != TURBO_OK)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
}

static int cnet_api_test_poll_until(cnet_client *client, atomic_int *value, int expected) {
  const uint64_t deadline = turbo_monotonic_ms() + CNET_API_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(value, memory_order_acquire) < expected) {
    size_t events = 0u;
    const int status = cnet_client_poll(client, 1u, &events);
    if (status != TURBO_OK) return status;
    if (turbo_monotonic_ms() >= deadline) return TURBO_ETIMEDOUT;
  }
  return TURBO_OK;
}

static void cnet_api_test_batch_state(void *user, cnet_connection connection,
                                      cnet_connection_state state, const cnet_error *error) {
  cnet_api_test_batch_probe *probe = (cnet_api_test_batch_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED)
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
}

static void cnet_api_test_batch_receive(void *user, cnet_connection connection,
                                        const cnet_receive_view *view) {
  cnet_api_test_batch_probe *probe = (cnet_api_test_batch_probe *)user;
  (void)connection;
  if (view->kind != CNET_MESSAGE_DATAGRAM || view->size != 1u)
    atomic_store_explicit(&probe->failed, 1, memory_order_release);
  else probe->received_value = *(const unsigned char *)view->data;
  atomic_fetch_add_explicit(&probe->received, 1, memory_order_release);
}

static void cnet_api_test_listener_state(void *user, cnet_connection connection,
                                         cnet_connection_state state, const cnet_error *error) {
  cnet_api_test_listener_probe *probe = (cnet_api_test_listener_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED)
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
}

static void cnet_api_test_listener_receive(void *user, cnet_connection connection,
                                           const cnet_receive_view *view) {
  cnet_api_test_listener_probe *probe = (cnet_api_test_listener_probe *)user;
  (void)connection;
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES || view->size != 1u)
    atomic_store_explicit(&probe->failed, 1, memory_order_release);
  else probe->received_value = *(const unsigned char *)view->data;
  atomic_store_explicit(&probe->received, 1, memory_order_release);
}

static void cnet_api_test_listener_send(void *user, cnet_connection connection, size_t size) {
  cnet_api_test_listener_probe *probe = (cnet_api_test_listener_probe *)user;
  (void)connection;
  if (size != 1u) atomic_store_explicit(&probe->failed, 1, memory_order_release);
  atomic_fetch_add_explicit(&probe->sent, 1, memory_order_release);
}

static cnet_client_config cnet_api_test_config(void) {
  const cnet_client_config config = {.backend =
#if defined(_WIN32)
                                         NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                         NATIVE_IO_BACKEND_EPOLL,
#else
                                         NATIVE_IO_BACKEND_KQUEUE,
#endif
                                     .connection_capacity = 2u,
                                     .command_capacity = 8u,
                                     .request_capacity = 4u,
                                     .completion_batch_capacity = 4u,
                                     .event_capacity = 8u,
                                     .max_send_bytes = 256u,
                                     .receive_buffer_bytes = 256u};
  return config;
}

spec("CNet public client API") {
  it("rejects malformed TLS server configuration without publishing a context") {
    cnet_tls_server server = {0};
    cnet_tls_server_config config = {.size = sizeof(config)};

    check_equal(cnet_tls_server_init(NULL, &config), TURBO_EINVAL);
    check_equal(cnet_tls_server_init(&server, NULL), TURBO_EINVAL);
    check_equal(cnet_tls_server_init(&server, &config), TURBO_EINVAL);
    check_null(server.impl);
    check_equal(cnet_tls_server_destroy(&server), TURBO_OK);
  }

  it("keeps TLS disabled when bounded TLS storage is not configured") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_connection connection = {17u, 19u};
    cnet_connect_options options = {.uri = "tls://localhost:443",
                                    .observer = {.on_state = cnet_api_test_ignore_state}};

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_connect(&client, &options, &connection), TURBO_ENOTSUP);
    check_equal(connection.slot, 0u);
    check_equal(connection.generation, 0u);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
  }

  it("requires a complete bounded TLS client allocation policy") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();

    config.tls_io_buffer_bytes = CNET_TLS_MIN_IO_BUFFER_BYTES;
    check_equal(cnet_client_init(&client, &config), TURBO_EINVAL);
    check_null(client.impl);

    config.tls_handshake_timeout_ms = 1000u;
    config.tls_io_buffer_bytes = CNET_TLS_MIN_IO_BUFFER_BYTES - 1u;
    check_equal(cnet_client_init(&client, &config), TURBO_EINVAL);
    check_null(client.impl);

    config.tls_io_buffer_bytes = CNET_TLS_MIN_IO_BUFFER_BYTES;
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
  }

  it("returns a portable address-in-use error for listener bind conflicts") {
    cnet_listener first = {0};
    cnet_listener second = {0};
    cnet_client_config client_config = cnet_api_test_config();
    cnet_listener_config config = {
        .backend = client_config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    uint16_t port = 0u;

    check_equal(cnet_listener_init(&first, &config), TURBO_OK);
    check_equal(cnet_listener_port(&first, &port), TURBO_OK);
    config.port = port;
    check_equal(cnet_listener_init(&second, &config), TURBO_EADDRINUSE);
    check_null(second.impl);
    check_equal(cnet_listener_close(&first), TURBO_OK);
    check_equal(cnet_listener_destroy(&first), TURBO_OK);
  }

#if !defined(_WIN32)
  it("retries an interrupted listener wait against the original deadline") {
    cnet_listener listener = {0};
    cnet_client_config client_config = cnet_api_test_config();
    cnet_listener_config config = {
        .backend = client_config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    cnet_api_test_interrupt_probe probe;
    turbo_thread_t interrupter = {0};
    void (*previous_handler)(int);
    int ready = -1;

    atomic_init(&probe.armed, 0);
    atomic_init(&probe.signal_status, -1);
    probe.target = pthread_self();
    previous_handler = signal(SIGUSR1, cnet_api_test_signal_handler);
    check_true(previous_handler != SIG_ERR);
    check_equal(cnet_listener_init(&listener, &config), TURBO_OK);
    check_equal(turbo_thread_create(&interrupter, cnet_api_test_interrupt_wait, &probe), TURBO_OK);
    atomic_store_explicit(&probe.armed, 1, memory_order_release);
    check_equal(cnet_listener_wait(&listener, 100u, &ready), TURBO_OK);
    check_equal(ready, 0);
    check_equal(turbo_thread_join(&interrupter), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.signal_status, memory_order_acquire), 0);
    check_true(signal(SIGUSR1, previous_handler) != SIG_ERR);
    check_equal(cnet_listener_close(&listener), TURBO_OK);
    check_equal(cnet_listener_destroy(&listener), TURBO_OK);
  }
#endif

  it("accepts a TCP connection and closes only after the final copied send") {
    cnet_client client = {0};
    cnet_listener listener = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_listener_config listener_config = {
        .backend = config.backend, .host = "127.0.0.1", .port = 0u, .backlog = 2u};
    cnet_api_test_listener_probe probe;
    cnet_api_test_socket peer = CNET_API_TEST_INVALID_SOCKET;
    struct sockaddr_in address;
    cnet_connection connection = {0};
    cnet_observer observer = {.on_state = cnet_api_test_listener_state,
                              .on_receive = cnet_api_test_listener_receive,
                              .on_send = cnet_api_test_listener_send,
                              .user = &probe};
    uint16_t port = 0u;
    unsigned char parsed_address[128];
    size_t parsed_address_length = 0u;
    int ready = 0;
    unsigned char received = 0u;
    const unsigned char request_value = 31u;
    const unsigned char response_value = 47u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.sent, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.failed, 0);
    probe.received_value = 0u;

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_true(native_io_backend_kind_supported(listener_config.backend));
    check_equal(cnet_transport_parse_bind_address(listener_config.host, listener_config.port,
                                                  parsed_address, sizeof(parsed_address),
                                                  &parsed_address_length),
                TURBO_OK);
    check_true(parsed_address_length != 0u);
    check_equal(cnet_listener_init(&listener, &listener_config), TURBO_OK);
    check_equal(cnet_listener_port(&listener, &port), TURBO_OK);
    check_true(port != 0u);

    peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check_true(peer != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_api_test_set_receive_timeout(peer), TURBO_OK);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    check_equal(connect(peer, (const struct sockaddr *)&address, (int)sizeof(address)), 0);

    check_equal(cnet_listener_wait(&listener, CNET_API_TEST_TIMEOUT_MS, &ready), TURBO_OK);
    check_equal(ready, 1);
    check_equal(cnet_listener_accept(&listener, &client, &observer, &connection), TURBO_OK);
    {
      cnet_connection no_peer = {0};
      check_equal(cnet_listener_accept(&listener, &client, &observer, &no_peer), TURBO_ETIMEDOUT);
      check_equal(no_peer.slot, 0u);
      check_equal(no_peer.generation, 0u);
    }
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    check_equal(cnet_receive(&client, connection, 1u), TURBO_OK);
    check_equal(send(peer, (const char *)&request_value, (int)sizeof(request_value), 0),
                (int)sizeof(request_value));
    check_equal(cnet_api_test_poll_until(&client, &probe.received, 1), TURBO_OK);
    check_equal(probe.received_value, request_value);

    check_equal(cnet_send_and_close(&client, connection, &response_value, sizeof(response_value)),
                TURBO_OK);
    check_equal(cnet_send(&client, connection, &response_value, sizeof(response_value)),
                TURBO_EBUSY);
    check_equal(cnet_receive(&client, connection, 1u), TURBO_EBUSY);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(recv(peer, (char *)&received, (int)sizeof(received), 0), (int)sizeof(received));
    check_equal(received, response_value);
    check_equal(recv(peer, (char *)&received, (int)sizeof(received), 0), 0);
    check_equal(atomic_load_explicit(&probe.sent, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);

    check_equal(cnet_listener_close(&listener), TURBO_OK);
    check_equal(cnet_listener_destroy(&listener), TURBO_OK);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(peer);
  }

  it("rejects new work as soon as close is admitted") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_listener_probe probe;
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket peer = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    uint16_t port = 0u;
    const unsigned char value = 37u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.sent, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.failed, 0);
    probe.received_value = 0u;
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_listener_state,
                                                  .on_receive = cnet_api_test_listener_receive,
                                                  .on_send = cnet_api_test_listener_send,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    peer = accept(listener, NULL, NULL);
    check_true(peer != CNET_API_TEST_INVALID_SOCKET);

    check_equal(cnet_close(&client, connection), TURBO_OK);
    check_equal(cnet_close(&client, connection), TURBO_EALREADY);
    check_equal(cnet_send(&client, connection, &value, sizeof(value)), TURBO_EBUSY);
    check_equal(cnet_receive(&client, connection, 1u), TURBO_EBUSY);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);

    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(peer);
    cnet_api_test_close_socket(listener);
  }

  it("returns zero progress when the caller polls an idle client") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    size_t events = SIZE_MAX;

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_client_poll(&client, 0u, &events), TURBO_OK);
    check_equal(events, (size_t)0u);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
  }

  it("invokes connection callbacks on the polling thread") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_poll_probe probe = {.client = &client};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    uint16_t port = 0u;

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned)port), 0);
    cnet_api_test_thread_marker = 73;
    options = (cnet_connect_options){
        .uri = uri, .observer = {.on_state = cnet_api_test_poll_state, .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.callback_thread_marker, memory_order_acquire), 73);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(listener);
  }

  it("initializes without a callback executor") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
  }

  it("rejects invalid configuration without publishing a client") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();

    check_equal(cnet_client_init(NULL, &config), TURBO_EINVAL);
    check_equal(cnet_client_init(&client, NULL), TURBO_EINVAL);
    config.command_capacity = 0u;
    check_equal(cnet_client_init(&client, &config), TURBO_EINVAL);
    check_null(client.impl);
  }

  it("clears immediate connect failures and stops a quiescent client") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_connection connection = {17u, 19u};
    cnet_connect_options options = {.uri = "unknown://endpoint",
                                    .observer = {.on_state = cnet_api_test_ignore_state}};

    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_not_null(client.impl);
    check_equal(cnet_connect(&client, &options, &connection), TURBO_ENOTSUP);
    check_equal(connection.slot, 0u);
    check_equal(connection.generation, 0u);
    check_equal(cnet_client_destroy(&client), TURBO_EBUSY);
    check_equal(cnet_client_stop(&client, 5000u), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    check_null(client.impl);
  }

  it("copies options and supports reentrant TCP operations on the I/O owner") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_BYTES};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket accepted = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    char uri[64];
    uint16_t port = 0u;
    unsigned char outbound = 0u;
    unsigned char send_value = 41u;
    const unsigned char expected_outbound = 41u;
    const unsigned char expected_inbound = 73u;
    cnet_connect_options options;
    int stale_status = TURBO_OK;
    uint64_t stale_deadline;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    memset(uri, 'x', strlen(uri));
    options.observer = (cnet_observer){0};

    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_API_TEST_INVALID_SOCKET);
    check_equal(atomic_load_explicit(&probe.callback_stop_status, memory_order_acquire),
                TURBO_EBUSY);
    check_equal(atomic_load_explicit(&probe.callback_operation_status, memory_order_acquire),
                TURBO_OK);

    check_equal(cnet_send(&client, connection, &send_value, sizeof(send_value)), TURBO_OK);
    check_equal(cnet_send(&client, connection, &send_value, sizeof(send_value)), TURBO_EBUSY);
    send_value = 99u;
    {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 0u, &events), TURBO_OK);
    }
    check_equal(recv(accepted, (char *)&outbound, (int)sizeof(outbound), 0), (int)sizeof(outbound));
    check_equal(outbound, expected_outbound);
    check_equal(send(accepted, (const char *)&expected_inbound, (int)sizeof(expected_inbound), 0),
                (int)sizeof(expected_inbound));
    check_equal(cnet_api_test_poll_until(&client, &probe.received, 1), TURBO_OK);
    check_equal(probe.received_value, expected_inbound);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);

    stale_deadline = turbo_monotonic_ms() + CNET_API_TEST_TIMEOUT_MS;
    do {
      stale_status = cnet_send(&client, connection, &expected_outbound, sizeof(expected_outbound));
      if (stale_status == TURBO_ENOENT) break;
      turbo_thread_yield();
    } while (turbo_monotonic_ms() < stale_deadline);
    check_equal(stale_status, TURBO_ENOENT);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(accepted);
    cnet_api_test_close_socket(listener);
  }

  it("drains one live TCP connection before stopping") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_BYTES};
    cnet_api_test_socket listener = CNET_API_TEST_INVALID_SOCKET;
    cnet_api_test_socket accepted = CNET_API_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    uint16_t port = 0u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    check_equal(cnet_api_test_listener(&listener, &port), TURBO_OK);
    (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.terminal, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(accepted);
    cnet_api_test_close_socket(listener);
  }

  it("preserves one connected UDP datagram as one public receive value") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_DATAGRAM};
    cnet_api_test_socket server = CNET_API_TEST_INVALID_SOCKET;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
#if defined(_WIN32)
    int server_length = (int)sizeof(server_address);
    int client_length = (int)sizeof(client_address);
#else
    socklen_t server_length = (socklen_t)sizeof(server_address);
    socklen_t client_length = (socklen_t)sizeof(client_address);
#endif
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    unsigned char outbound = 0u;
    const unsigned char expected_outbound = 29u;
    const unsigned char expected_inbound = 97u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    check_true(server != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_api_test_set_receive_timeout(server), TURBO_OK);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check_equal(bind(server, (const struct sockaddr *)&server_address, (int)sizeof(server_address)),
                0);
    check_equal(getsockname(server, (struct sockaddr *)&server_address, &server_length), 0);
    (void)snprintf(uri, sizeof(uri), "udp://127.0.0.1:%u",
                   (unsigned int)ntohs(server_address.sin_port));
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    check_equal(cnet_send(&client, connection, &expected_outbound, sizeof(expected_outbound)),
                TURBO_OK);
    {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 0u, &events), TURBO_OK);
    }
    check_equal(recvfrom(server, (char *)&outbound, (int)sizeof(outbound), 0,
                         (struct sockaddr *)&client_address, &client_length),
                (int)sizeof(outbound));
    check_equal(outbound, expected_outbound);
    check_equal(sendto(server, (const char *)&expected_inbound, (int)sizeof(expected_inbound), 0,
                       (const struct sockaddr *)&client_address, client_length),
                (int)sizeof(expected_inbound));
    check_equal(cnet_api_test_poll_until(&client, &probe.received, 1), TURBO_OK);
    check_equal(probe.received_value, expected_inbound);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(server);
  }

  it("delivers every UDP datagram from one bounded receive demand") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_batch_probe probe;
    cnet_api_test_socket server = CNET_API_TEST_INVALID_SOCKET;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
#if defined(_WIN32)
    int server_length = (int)sizeof(server_address);
    int client_length = (int)sizeof(client_address);
#else
    socklen_t server_length = (socklen_t)sizeof(server_address);
    socklen_t client_length = (socklen_t)sizeof(client_address);
#endif
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[64];
    int exchange_status = TURBO_OK;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.failed, 0);
    probe.received_value = 0u;
    config.connection_capacity = 64u;
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    check_true(server != CNET_API_TEST_INVALID_SOCKET);
    check_equal(cnet_api_test_set_receive_timeout(server), TURBO_OK);
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check_equal(bind(server, (const struct sockaddr *)&server_address, (int)sizeof(server_address)),
                0);
    check_equal(getsockname(server, (struct sockaddr *)&server_address, &server_length), 0);
    (void)snprintf(uri, sizeof(uri), "udp://127.0.0.1:%u",
                   (unsigned int)ntohs(server_address.sin_port));
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_batch_state,
                                                  .on_receive = cnet_api_test_batch_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    check_equal(cnet_receive(&client, connection, CNET_API_TEST_BATCH_DATAGRAMS), TURBO_OK);

    for (int index = 0; exchange_status == TURBO_OK && index < CNET_API_TEST_BATCH_DATAGRAMS;
         ++index) {
      unsigned char outbound = 0u;
      const unsigned char expected = (unsigned char)(index + 1);
      exchange_status = cnet_send(&client, connection, &expected, sizeof(expected));
      if (exchange_status == TURBO_OK) {
        size_t events = 0u;
        exchange_status = cnet_client_poll(&client, 0u, &events);
      }
      if (exchange_status == TURBO_OK) {
        const int received = recvfrom(server, (char *)&outbound, (int)sizeof(outbound), 0,
                                      (struct sockaddr *)&client_address, &client_length);
        if (received != (int)sizeof(outbound) || outbound != expected) exchange_status = TURBO_EIO;
      }
      if (exchange_status == TURBO_OK) {
        const int sent = sendto(server, (const char *)&expected, (int)sizeof(expected), 0,
                                (const struct sockaddr *)&client_address, client_length);
        if (sent != (int)sizeof(expected)) exchange_status = TURBO_EIO;
      }
      if (exchange_status == TURBO_OK)
        exchange_status = cnet_api_test_poll_until(&client, &probe.received, index + 1);
      if (exchange_status == TURBO_OK &&
          (probe.received_value != expected ||
           atomic_load_explicit(&probe.failed, memory_order_acquire) != 0))
        exchange_status = TURBO_EIO;
    }
    check_equal(exchange_status, TURBO_OK);

    check_equal(cnet_close(&client, connection), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_api_test_close_socket(server);
  }

  it("uses the same public byte API for a platform Pipe endpoint") {
    cnet_client client = {0};
    cnet_client_config config = cnet_api_test_config();
    cnet_api_test_probe probe = {.client = &client, .expected_kind = CNET_MESSAGE_BYTES};
    cnet_shared_test_named_pipe pipe;
    cnet_connection connection = {0};
    cnet_connect_options options;
    char uri[CNET_URI_PATH_CAPACITY + 8u];
    unsigned char outbound = 0u;
    const unsigned char expected_outbound = 53u;
    const unsigned char expected_inbound = 89u;
    int uri_length;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.received, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.callback_stop_status, TURBO_OK);
    atomic_init(&probe.callback_operation_status, TURBO_EIO);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.block_terminal, 0);
    atomic_init(&probe.release_terminal, 0);
    check_equal(cnet_shared_test_named_pipe_start(&pipe), TURBO_OK);
    check_equal(cnet_client_init(&client, &config), TURBO_OK);
    uri_length = snprintf(uri, sizeof(uri), "pipe://%s", pipe.name);
    check_true(uri_length > 0 && (size_t)uri_length < sizeof(uri));
    options = (cnet_connect_options){.uri = uri,
                                     .observer = {.on_state = cnet_api_test_state,
                                                  .on_receive = cnet_api_test_receive,
                                                  .user = &probe}};
    check_equal(cnet_connect(&client, &options, &connection), TURBO_OK);
    {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 0u, &events), TURBO_OK);
    }
    check_equal(cnet_shared_test_named_pipe_finish(&pipe), TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.connected, 1), TURBO_OK);
    check_equal(cnet_send(&client, connection, &expected_outbound, sizeof(expected_outbound)),
                TURBO_OK);
    {
      size_t events = 0u;
      check_equal(cnet_client_poll(&client, 0u, &events), TURBO_OK);
    }
    check_equal(cnet_api_test_wait_pipe_read(&pipe, &outbound, sizeof(outbound)), TURBO_OK);
    check_equal(outbound, expected_outbound);
    check_equal(
        cnet_shared_test_named_pipe_peer_write(&pipe, &expected_inbound, sizeof(expected_inbound)),
        TURBO_OK);
    check_equal(cnet_api_test_poll_until(&client, &probe.received, 1), TURBO_OK);
    check_equal(probe.received_value, expected_inbound);
    check_equal(cnet_api_test_poll_until(&client, &probe.terminal, 1), TURBO_OK);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);
    check_equal(cnet_client_stop(&client, CNET_API_TEST_TIMEOUT_MS), TURBO_OK);
    check_equal(cnet_client_destroy(&client), TURBO_OK);
    cnet_shared_test_named_pipe_close(&pipe);
  }
}
