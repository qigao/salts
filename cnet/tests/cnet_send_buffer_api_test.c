#define TINYTEST_NO_MAIN
#include "tinytest.h"
#include <cnet/cnet.h>

#include <salts/clock.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET cnet_send_buffer_test_socket;
  #define CNET_SEND_BUFFER_TEST_INVALID_SOCKET INVALID_SOCKET
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int cnet_send_buffer_test_socket;
  #define CNET_SEND_BUFFER_TEST_INVALID_SOCKET (-1)
#endif

enum { CNET_SEND_BUFFER_TEST_TIMEOUT_MS = 5000, CNET_SEND_BUFFER_TEST_BYTES = 64 };

typedef struct cnet_send_buffer_test_probe {
  cnet_client *client;
  atomic_int connected;
  atomic_int sent;
  atomic_int terminal;
  atomic_int failed;
  atomic_int receive_admit_status;
  size_t expected_send_size;
  bool queue_receive_on_connect;
} cnet_send_buffer_test_probe;

typedef struct cnet_send_buffer_free_probe {
  atomic_int freed;
} cnet_send_buffer_free_probe;

static void cnet_send_buffer_test_close_socket(cnet_send_buffer_test_socket socket_value) {
  if (socket_value == CNET_SEND_BUFFER_TEST_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int cnet_send_buffer_test_set_receive_timeout(cnet_send_buffer_test_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = CNET_SEND_BUFFER_TEST_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                    (int)sizeof(timeout_ms)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#else
  const struct timeval timeout = {CNET_SEND_BUFFER_TEST_TIMEOUT_MS / 1000,
                                  (CNET_SEND_BUFFER_TEST_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout)) ==
                 0
             ? SALTS_OK
             : SALTS_EIO;
#endif
}

static int cnet_send_buffer_test_listener(cnet_send_buffer_test_socket *out_listener,
                                          uint16_t *out_port) {
  struct sockaddr_in address;
#if defined(_WIN32)
  int length = (int)sizeof(address);
#else
  socklen_t length = (socklen_t)sizeof(address);
#endif

  *out_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (*out_listener == CNET_SEND_BUFFER_TEST_INVALID_SOCKET) return SALTS_EIO;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(*out_listener, (const struct sockaddr *)&address, (int)sizeof(address)) != 0 ||
      getsockname(*out_listener, (struct sockaddr *)&address, &length) != 0 ||
      listen(*out_listener, 2) != 0) {
    cnet_send_buffer_test_close_socket(*out_listener);
    *out_listener = CNET_SEND_BUFFER_TEST_INVALID_SOCKET;
    return SALTS_EIO;
  }
  *out_port = ntohs(address.sin_port);
  return SALTS_OK;
}

static void cnet_send_buffer_test_state(void *user, cnet_connection connection,
                                        cnet_connection_state state, const cnet_error *error) {
  cnet_send_buffer_test_probe *probe = (cnet_send_buffer_test_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    if (probe->queue_receive_on_connect) {
      atomic_store_explicit(&probe->receive_admit_status,
                            cnet_receive(probe->client, connection, 1u),
                            memory_order_release);
    }
    atomic_store_explicit(&probe->connected, 1, memory_order_release);
  } else if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    if (state == CNET_CONNECTION_FAILED || error != NULL)
      atomic_store_explicit(&probe->failed, 1, memory_order_release);
    atomic_store_explicit(&probe->terminal, 1, memory_order_release);
  }
}

static void cnet_send_buffer_test_receive(void *user, cnet_connection connection,
                                          const cnet_receive_view *view) {
  cnet_send_buffer_test_probe *probe = (cnet_send_buffer_test_probe *)user;
  (void)connection;
  (void)view;
  atomic_store_explicit(&probe->failed, 1, memory_order_release);
}

static void cnet_send_buffer_test_sent(void *user, cnet_connection connection, size_t size) {
  cnet_send_buffer_test_probe *probe = (cnet_send_buffer_test_probe *)user;
  (void)connection;
  if (size != probe->expected_send_size)
    atomic_store_explicit(&probe->failed, 1, memory_order_release);
  atomic_fetch_add_explicit(&probe->sent, 1, memory_order_release);
}

static int cnet_send_buffer_test_poll_until(cnet_client *client, atomic_int *value, int expected) {
  const uint64_t deadline = salts_monotonic_ms() + CNET_SEND_BUFFER_TEST_TIMEOUT_MS;
  while (atomic_load_explicit(value, memory_order_acquire) < expected) {
    size_t events = 0u;
    const int status = cnet_client_poll(client, 1u, &events);
    if (status != SALTS_OK) return status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

static void cnet_send_buffer_test_free(void *data, void *user_data) {
  cnet_send_buffer_free_probe *probe = (cnet_send_buffer_free_probe *)user_data;
  free(data);
  atomic_fetch_add_explicit(&probe->freed, 1, memory_order_release);
}

static mem_buffer_t *cnet_send_buffer_test_external(size_t size, unsigned char value,
                                                    cnet_send_buffer_free_probe *probe) {
  unsigned char *data = (unsigned char *)malloc(size);
  mem_buffer_t *buffer;
  if (data == NULL) return NULL;
  memset(data, value, size);
  buffer = mem_wrap_external(data, size, cnet_send_buffer_test_free, probe);
  if (buffer == NULL) free(data);
  return buffer;
}

static cnet_client_config cnet_send_buffer_test_config(void) {
  const cnet_client_config config = {.backend =
#if defined(_WIN32)
                                         NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                         NATIVE_IO_BACKEND_EPOLL,
#else
                                         NATIVE_IO_BACKEND_KQUEUE,
#endif
                                     .connection_capacity = 2u,
                                     .command_capacity = 1u,
                                     .request_capacity = 4u,
                                     .completion_batch_capacity = 4u,
                                     .event_capacity = 8u,
                                     .max_send_bytes = 256u,
                                     .receive_buffer_bytes = 256u};
  return config;
}

spec("CNet retained buffer public send API") {
  it("owns one reference only after admission and releases it at terminal send") {
    cnet_client client = {0};
    cnet_client_config config = cnet_send_buffer_test_config();
    cnet_send_buffer_test_probe probe = {.client = &client,
                                         .expected_send_size = CNET_SEND_BUFFER_TEST_BYTES,
                                         .queue_receive_on_connect = true};
    cnet_send_buffer_test_socket listener = CNET_SEND_BUFFER_TEST_INVALID_SOCKET;
    cnet_send_buffer_test_socket accepted = CNET_SEND_BUFFER_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    cnet_observer observer = {.on_state = cnet_send_buffer_test_state,
                              .on_receive = cnet_send_buffer_test_receive,
                              .on_send = cnet_send_buffer_test_sent,
                              .user = &probe};
    cnet_send_buffer_free_probe success_free;
    cnet_send_buffer_free_probe zero_free;
    cnet_send_buffer_free_probe oversize_free;
    cnet_send_buffer_free_probe full_free;
    cnet_send_buffer_free_probe busy_free;
    cnet_send_buffer_free_probe stale_free;
    cnet_send_buffer_free_probe shutdown_free;
    mem_buffer_t *success_buffer;
    mem_buffer_t *zero_buffer;
    mem_buffer_t *oversize_buffer;
    mem_buffer_t *full_buffer;
    mem_buffer_t *busy_buffer;
    mem_buffer_t *stale_buffer;
    mem_buffer_t *shutdown_buffer;
    unsigned char received[CNET_SEND_BUFFER_TEST_BYTES] = {0};
    unsigned char expected[CNET_SEND_BUFFER_TEST_BYTES];
    char uri[64];
    uint16_t port = 0u;
    size_t events = 0u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.sent, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.receive_admit_status, SALTS_EIO);
    atomic_init(&success_free.freed, 0);
    atomic_init(&zero_free.freed, 0);
    atomic_init(&oversize_free.freed, 0);
    atomic_init(&full_free.freed, 0);
    atomic_init(&busy_free.freed, 0);
    atomic_init(&stale_free.freed, 0);
    atomic_init(&shutdown_free.freed, 0);
    memset(expected, 0x5a, sizeof(expected));

    check_equal(cnet_client_init(&client, &config), SALTS_OK);
    check_equal(cnet_send_buffer(NULL, connection, NULL), SALTS_EINVAL);
    check_equal(cnet_send_buffer(&client, connection, NULL), SALTS_EINVAL);
    check_equal(cnet_send_buffer_test_listener(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    options = (cnet_connect_options){.uri = uri, .observer = observer};
    check_equal(cnet_connect(&client, &options, &connection), SALTS_OK);
    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.connected, 1), SALTS_OK);
    check_equal(atomic_load_explicit(&probe.receive_admit_status, memory_order_acquire), SALTS_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_SEND_BUFFER_TEST_INVALID_SOCKET);
    check_equal(cnet_send_buffer_test_set_receive_timeout(accepted), SALTS_OK);

    zero_buffer = cnet_send_buffer_test_external(1u, 0x11u, &zero_free);
    check_true(zero_buffer != NULL);
    mem_set_used(zero_buffer, 0u);
    check_equal(mem_buffer_ref_count(zero_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer(&client, connection, zero_buffer), SALTS_EINVAL);
    check_equal(mem_buffer_ref_count(zero_buffer), UINT32_C(1));
    mem_buffer_release(zero_buffer);
    check_equal(atomic_load_explicit(&zero_free.freed, memory_order_acquire), 1);

    oversize_buffer = cnet_send_buffer_test_external(257u, 0x22u, &oversize_free);
    check_true(oversize_buffer != NULL);
    check_equal(mem_buffer_ref_count(oversize_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer(&client, connection, oversize_buffer), SALTS_EMSGSIZE);
    check_equal(mem_buffer_ref_count(oversize_buffer), UINT32_C(1));
    mem_buffer_release(oversize_buffer);
    check_equal(atomic_load_explicit(&oversize_free.freed, memory_order_acquire), 1);

    /* A callback-issued receive occupies the single generic command slot, but ordinary
       retained send ownership is independent after #479 W2. */
    full_buffer = cnet_send_buffer_test_external(1u, 0x33u, &full_free);
    check_true(full_buffer != NULL);
    check_equal(mem_buffer_ref_count(full_buffer), UINT32_C(1));
    probe.expected_send_size = 1u;
    check_equal(cnet_send_buffer(&client, connection, full_buffer), SALTS_OK);
    check_equal(mem_buffer_ref_count(full_buffer), UINT32_C(2));
    mem_buffer_release(full_buffer);
    check_equal(atomic_load_explicit(&full_free.freed, memory_order_acquire), 0);
    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.sent, 1), SALTS_OK);
    check_equal(atomic_load_explicit(&full_free.freed, memory_order_acquire), 1);
    check_equal(recv(accepted, (char *)received, 1, 0), 1);
    check_equal(received[0], (unsigned char)0x33u);

    probe.expected_send_size = CNET_SEND_BUFFER_TEST_BYTES;
    memset(received, 0, sizeof(received));
    success_buffer = cnet_send_buffer_test_external(CNET_SEND_BUFFER_TEST_BYTES, 0x5au, &success_free);
    check_true(success_buffer != NULL);
    check_equal(mem_buffer_ref_count(success_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer(&client, connection, success_buffer), SALTS_OK);
    check_equal(mem_buffer_ref_count(success_buffer), UINT32_C(2));

    busy_buffer = cnet_send_buffer_test_external(1u, 0x44u, &busy_free);
    check_true(busy_buffer != NULL);
    check_equal(mem_buffer_ref_count(busy_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer(&client, connection, busy_buffer), SALTS_ENOBUFS);
    check_equal(mem_buffer_ref_count(busy_buffer), UINT32_C(1));
    mem_buffer_release(busy_buffer);
    check_equal(atomic_load_explicit(&busy_free.freed, memory_order_acquire), 1);

    mem_buffer_release(success_buffer);
    check_equal(atomic_load_explicit(&success_free.freed, memory_order_acquire), 0);
    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.sent, 2), SALTS_OK);
    check_equal(atomic_load_explicit(&success_free.freed, memory_order_acquire), 1);
    check_equal(recv(accepted, (char *)received, (int)sizeof(received), 0), (int)sizeof(received));
    check_equal(received, expected, sizeof(expected));
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);

    check_equal(cnet_close(&client, connection), SALTS_OK);
    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.terminal, 1), SALTS_OK);
    stale_buffer = cnet_send_buffer_test_external(1u, 0x55u, &stale_free);
    check_true(stale_buffer != NULL);
    check_equal(mem_buffer_ref_count(stale_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer(&client, connection, stale_buffer), SALTS_ENOENT);
    check_equal(mem_buffer_ref_count(stale_buffer), UINT32_C(1));
    mem_buffer_release(stale_buffer);
    check_equal(atomic_load_explicit(&stale_free.freed, memory_order_acquire), 1);

    check_equal(cnet_client_stop(&client, CNET_SEND_BUFFER_TEST_TIMEOUT_MS), SALTS_OK);
    shutdown_buffer = cnet_send_buffer_test_external(1u, 0x66u, &shutdown_free);
    check_true(shutdown_buffer != NULL);
    check_equal(mem_buffer_ref_count(shutdown_buffer), UINT32_C(1));
    check_equal(cnet_send_buffer(&client, connection, shutdown_buffer), SALTS_ESHUTDOWN);
    check_equal(mem_buffer_ref_count(shutdown_buffer), UINT32_C(1));
    mem_buffer_release(shutdown_buffer);
    check_equal(atomic_load_explicit(&shutdown_free.freed, memory_order_acquire), 1);

    check_equal(cnet_client_destroy(&client), SALTS_OK);
    cnet_send_buffer_test_close_socket(accepted);
    cnet_send_buffer_test_close_socket(listener);


  it("sends one retained middle slice and releases backing only after terminal") {
    cnet_client client = {0};
    cnet_client_config config = cnet_send_buffer_test_config();
    cnet_send_buffer_test_probe probe = {.client = &client, .expected_send_size = 4u};
    cnet_send_buffer_test_socket listener = CNET_SEND_BUFFER_TEST_INVALID_SOCKET;
    cnet_send_buffer_test_socket accepted = CNET_SEND_BUFFER_TEST_INVALID_SOCKET;
    cnet_connection connection = {0};
    cnet_connect_options options;
    cnet_observer observer = {.on_state = cnet_send_buffer_test_state,
                              .on_receive = cnet_send_buffer_test_receive,
                              .on_send = cnet_send_buffer_test_sent,
                              .user = &probe};
    cnet_send_buffer_free_probe free_probe;
    mem_buffer_t *buffer;
    mem_slice_t slice;
    mem_slice_t forged;
    unsigned char received[4] = {0};
    const unsigned char expected[4] = {0x12u, 0x13u, 0x14u, 0x15u};
    char uri[64];
    uint16_t port = 0u;
    size_t received_size = 0u;

    atomic_init(&probe.connected, 0);
    atomic_init(&probe.sent, 0);
    atomic_init(&probe.terminal, 0);
    atomic_init(&probe.failed, 0);
    atomic_init(&probe.receive_admit_status, SALTS_OK);
    atomic_init(&free_probe.freed, 0);

    check_equal(cnet_client_init(&client, &config), SALTS_OK);
    check_equal(cnet_send_slice(NULL, connection, NULL), SALTS_EINVAL);
    check_equal(cnet_send_slice(&client, connection, NULL), SALTS_EINVAL);
    check_equal(cnet_send_buffer_test_listener(&listener, &port), SALTS_OK);
    check_greater(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port), 0);
    options = (cnet_connect_options){.uri = uri, .observer = observer};
    check_equal(cnet_connect(&client, &options, &connection), SALTS_OK);
    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.connected, 1), SALTS_OK);
    accepted = accept(listener, NULL, NULL);
    check_true(accepted != CNET_SEND_BUFFER_TEST_INVALID_SOCKET);
    check_equal(cnet_send_buffer_test_set_receive_timeout(accepted), SALTS_OK);

    buffer = cnet_send_buffer_test_external(8u, 0u, &free_probe);
    check_true(buffer != NULL);
    for (size_t index = 0u; index < 8u; ++index)
      mem_buffer_data(buffer)[index] = (char)(0x10u + index);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    forged = (mem_slice_t){mem_buffer_data(buffer) + 8u, 1u, buffer};
    check_equal(cnet_send_slice(&client, connection, &forged), SALTS_EINVAL);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(1));

    slice = mem_slice(buffer, 2u, 4u);
    check_equal(slice.length, (size_t)4u);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
    check_equal(cnet_send_slice(&client, connection, &slice), SALTS_OK);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(3));

    mem_slice_release(&slice);
    check_equal(mem_buffer_ref_count(buffer), UINT32_C(2));
    mem_buffer_release(buffer);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 0);

    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.sent, 1), SALTS_OK);
    check_equal(atomic_load_explicit(&free_probe.freed, memory_order_acquire), 1);
    while (received_size < sizeof(received)) {
      const int got = recv(accepted, (char *)received + received_size,
                           (int)(sizeof(received) - received_size), 0);
      check_greater(got, 0);
      if (got <= 0) break;
      received_size += (size_t)got;
    }
    check_equal(received_size, sizeof(received));
    check_equal(memcmp(received, expected, sizeof(expected)), 0);
    check_equal(atomic_load_explicit(&probe.failed, memory_order_acquire), 0);

    check_equal(cnet_close(&client, connection), SALTS_OK);
    check_equal(cnet_send_buffer_test_poll_until(&client, &probe.terminal, 1), SALTS_OK);
    check_equal(cnet_client_stop(&client, CNET_SEND_BUFFER_TEST_TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
    cnet_send_buffer_test_close_socket(accepted);
    cnet_send_buffer_test_close_socket(listener);
  }
  }
}
