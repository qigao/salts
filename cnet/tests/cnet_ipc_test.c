#include <cnet/ipc.h>
#include <fmt.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <stdatomic.h>
#include <string.h>
#include <tinytest.h>
#if defined(_WIN32)
  #include <windows.h>
#else
  #include <poll.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

enum { TIMEOUT_MS = 5000, BYTE_COUNT = 4 };
typedef struct ipc_probe {
  bool connected;
  bool failed;
  size_t sends;
  size_t bytes;
  unsigned char data[BYTE_COUNT];
  const void *owner;
} ipc_probe;
static cnet_ipc_listener listener;
static cnet_ipc_accepted accepted;
static cnet_client clients[2];
static cnet_connection connections[2];
static ipc_probe probes[2];
static tstr uri;
static mem_buffer_t *buffer;
static bool pathname_owned;
typedef struct ipc_worker {
  cnet_ipc_accepted child;
  cmeta_thread_t thread;
  atomic_bool stop;
  ipc_probe probe;
  size_t udp_sends;
  size_t udp_receives;
  int status;
} ipc_worker;
static ipc_worker workers[2];

static native_io_backend_kind backend_kind(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static void on_state(void *user, cnet_connection connection, cnet_connection_state state,
                     const cnet_error *error) {
  ipc_probe *probe = user;
  if (probe->owner != NULL && probe->owner != cmeta_thread_current_token()) probe->failed = true;
  (void)connection;
  (void)error;
  if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
  if (state == CNET_CONNECTION_FAILED) probe->failed = true;
}
static void on_receive(void *user, cnet_connection connection, const cnet_receive_view *view) {
  ipc_probe *probe = user;
  if (probe->owner != NULL && probe->owner != cmeta_thread_current_token()) probe->failed = true;
  (void)connection;
  check_warn(view->kind == CNET_MESSAGE_BYTES);
  check_warn(view->size <= sizeof(probe->data) - probe->bytes);
  if (view->size <= sizeof(probe->data) - probe->bytes) {
    memcpy(probe->data + probe->bytes, view->data, view->size);
    probe->bytes += view->size;
  }
}
static void on_send(void *user, cnet_connection connection, size_t size) {
  ipc_probe *probe = user;
  (void)connection;
  if (probe->owner != NULL && probe->owner != cmeta_thread_current_token()) probe->failed = true;
  check_warn(size == BYTE_COUNT);
  ++probe->sends;
}
static cnet_observer observer_for(size_t index) {
  return (cnet_observer){on_state, on_receive, &probes[index], on_send};
}
static cnet_ipc_listener_config listener_config(void) {
  return (cnet_ipc_listener_config){sizeof(cnet_ipc_listener_config),
                                    CNET_IPC_VERSION,
                                    backend_kind(),
                                    uri,
                                    2u,
                                    2u,
                                    4096u,
                                    4096u};
}
static cnet_client_config client_config(void) {
  cnet_client_config config = {0};
  config.backend = backend_kind();
  config.connection_capacity = 2u;
  config.command_capacity = 8u;
  config.request_capacity = 8u;
  config.completion_batch_capacity = 8u;
  config.event_capacity = 8u;
  config.max_send_bytes = 4096u;
  config.receive_buffer_bytes = 4096u;
  return config;
}
static void worker_udp_receive(void *user, cnet_datagram *datagram, const cnet_datagram_peer *peer,
                               const cnet_receive_view *view) {
  ipc_worker *worker = user;
  (void)datagram;
  (void)peer;
  if (worker->probe.owner != cmeta_thread_current_token() || view->size != 4u ||
      memcmp(view->data, "udp!", 4u) != 0)
    worker->status = SALTS_EPROTO;
  ++worker->udp_receives;
}
static void worker_udp_send(void *user, cnet_datagram *datagram, const cnet_datagram_peer *peer,
                            size_t size, int status, uint64_t tag) {
  ipc_worker *worker = user;
  (void)datagram;
  (void)peer;
  (void)tag;
  if (worker->probe.owner != cmeta_thread_current_token() || size != 4u || status != SALTS_OK)
    worker->status = SALTS_EPROTO;
  ++worker->udp_sends;
}
static int worker_progress(ipc_worker *worker, native_io_backend *backend, cnet_client *client,
                           cnet_datagram *datagrams) {
  native_io_completion batch[16];
  size_t count = 0u, events;
  int first = cnet_client_advance_external(client, &events);
  int status = native_io_backend_observe(backend, batch, 16u, 1u, &count);
  if (status != SALTS_OK && status != SALTS_ETIMEDOUT) return status;
  for (size_t i = 0u; i < count; ++i) {
    bool consumed = false;
    for (size_t j = 0u; j < 2u && !consumed; ++j) {
      if (datagrams[j].impl == NULL) continue;
      status =
          cnet_datagram_route_external_completion(&datagrams[j], &batch[i], &consumed, &events);
      if (first == SALTS_OK) first = status;
    }
    if (!consumed) {
      status = cnet_client_route_external_completion(client, &batch[i], &consumed, &events);
      if (first == SALTS_OK) first = status;
    }
    if (!consumed && first == SALTS_OK) first = SALTS_EPROTO;
  }
  if (worker->probe.failed && first == SALTS_OK) first = SALTS_EPROTO;
  return first;
}
static void worker_run(void *user) {
  ipc_worker *worker = user;
  native_io_backend backend = {0};
  native_io_backend_config native_config = {backend_kind(), 4u, 16u, 16u};
  cnet_client client = {0};
  cnet_client_config config = client_config();
  cnet_datagram datagrams[2] = {{0}};
  cnet_connection connection = {0};
  cnet_observer observer = {on_state, on_receive, &worker->probe, on_send};
  uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  bool receiving = false, sent = false;
  worker->probe.owner = cmeta_thread_current_token();
#define WORK_TRY(expression)                                                                       \
  do {                                                                                             \
    int result = (expression);                                                                     \
    if (result != SALTS_OK) {                                                                      \
      worker->status = result;                                                                     \
      goto cleanup;                                                                                \
    }                                                                                              \
  } while (0)
  WORK_TRY(native_io_backend_init(&backend, &native_config));
  WORK_TRY(cnet_client_init_external(&client, &config, &backend));
  WORK_TRY(cnet_client_adopt_ipc(&client, &worker->child, &observer, &connection));
  for (size_t i = 0; i < 2u; ++i) {
    cnet_datagram_config udp = CNET_DATAGRAM_CONFIG_INIT;
    cnet_datagram_peer peer = {0};
    udp.backend = backend_kind();
    udp.host = "127.0.0.1";
    udp.send_capacity = 1u;
    udp.request_capacity = 2u;
    udp.completion_batch_capacity = 2u;
    udp.max_datagram_bytes = 4u;
    udp.receive_buffer_bytes = 4u;
    udp.observer = (cnet_datagram_observer){worker_udp_receive, worker_udp_send, worker};
    WORK_TRY(cnet_datagram_init_external(&datagrams[i], &udp, &backend));
    peer.family = CNET_DATAGRAM_ADDRESS_IPV4;
    peer.address[0] = 127u;
    peer.address[3] = 1u;
    WORK_TRY(cnet_datagram_port(&datagrams[i], &peer.port));
    WORK_TRY(cnet_datagram_receive(&datagrams[i], 1u));
    WORK_TRY(cnet_datagram_send(&datagrams[i], &peer, "udp!", 4u, 1u));
  }
  while (!atomic_load_explicit(&worker->stop, memory_order_acquire)) {
    WORK_TRY(worker_progress(worker, &backend, &client, datagrams));
    if (worker->probe.connected && !receiving) {
      WORK_TRY(cnet_receive(&client, connection, BYTE_COUNT));
      receiving = true;
    }
    if (worker->probe.bytes == BYTE_COUNT && !sent) {
      mem_buffer_t *reply = mem_get_buffer(mem_global(), BYTE_COUNT);
      int status;
      if (reply == NULL) {
        worker->status = SALTS_ENOMEM;
        goto cleanup;
      }
      memcpy(mem_buffer_data(reply), worker->probe.data, BYTE_COUNT);
      mem_set_used(reply, BYTE_COUNT);
      status = cnet_send_buffer(&client, connection, reply);
      mem_buffer_release(reply);
      WORK_TRY(status);
      sent = true;
    }
    if (cmeta_monotonic_ms() >= deadline) {
      worker->status = SALTS_ETIMEDOUT;
      goto cleanup;
    }
  }
cleanup:
  if (worker->child.kind != CNET_IPC_RESOURCE_NONE) (void)cnet_ipc_accepted_close(&worker->child);
  if (connection.slot != 0u) (void)cnet_close(&client, connection);
  deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  while (client.impl != NULL || datagrams[0].impl != NULL || datagrams[1].impl != NULL) {
    bool udp_stopped = true;
    for (size_t i = 0; i < 2u; ++i) {
      bool stopped = false;
      if (datagrams[i].impl == NULL) continue;
      (void)cnet_datagram_stop_external(&datagrams[i], &stopped);
      if (stopped) (void)cnet_datagram_destroy(&datagrams[i]);
      else udp_stopped = false;
    }
    if (client.impl != NULL && udp_stopped && cnet_client_stop_external(&client) == SALTS_OK)
      (void)cnet_client_destroy(&client);
    if (client.impl == NULL && udp_stopped) break;
    if (client.impl != NULL) (void)worker_progress(worker, &backend, &client, datagrams);
    if (cmeta_monotonic_ms() >= deadline) {
      worker->status = SALTS_ETIMEDOUT;
      return;
    }
  }
  if (backend.impl != NULL) {
    int status = native_io_backend_close(&backend);
    if (status == SALTS_OK) status = native_io_backend_destroy(&backend);
    if (worker->status == SALTS_OK) worker->status = status;
  }
#undef WORK_TRY
}
static void poll_clients(void) {
  for (size_t i = 0; i < 2u; ++i) {
    size_t events;
    check_equal(cnet_client_poll(&clients[i], 1u, &events), SALTS_OK);
    check_false(probes[i].failed);
  }
}
static void stop_listener(void) {
  bool stopped = false;
  uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  while (!stopped && cmeta_monotonic_ms() < deadline) {
    size_t events;
    int status = cnet_ipc_listener_stop(&listener, &stopped);
    check_warn(status == SALTS_OK || status == SALTS_EBUSY);
    if (!stopped) check_warn(cnet_ipc_listener_advance(&listener, 2u, &events) == SALTS_OK);
  }
  check_warn(stopped);
}
static void connect_and_detach(void) {
  size_t events;
  cnet_connect_options options = {0};
  uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  options.uri = uri;
  options.observer = observer_for(0u);
  check_equal(cnet_ipc_listener_advance(&listener, 2u, &events), SALTS_OK);
  check_equal(cnet_connect(&clients[0], &options, &connections[0]), SALTS_OK);
  while (!probes[0].connected) {
    poll_clients();
    check(cmeta_monotonic_ms() < deadline);
  }
  /* The host waits on the actual control-lane source, without a polling thread. */
  {
    cnet_ipc_wait_source sources[2];
    size_t count;
    bool ready;
    check_equal(cnet_ipc_listener_wait_sources(&listener, sources, 2u, &count, &ready), SALTS_OK);
    if (!ready) {
      check(count > 0u);
#if defined(_WIN32)
      HANDLE handles[2];
      for (size_t i = 0; i < count; ++i)
        handles[i] = (HANDLE)sources[i].native_handle;
      check(WaitForMultipleObjects((DWORD)count, handles, FALSE, TIMEOUT_MS) <
            WAIT_OBJECT_0 + count);
#else
      struct pollfd item = {(int)sources[0].native_handle, POLLIN, 0};
      check_equal(poll(&item, 1u, TIMEOUT_MS), 1);
#endif
    }
  }
  while (accepted.kind == CNET_IPC_RESOURCE_NONE) {
    int status;
    check_equal(cnet_ipc_listener_advance(&listener, 2u, &events), SALTS_OK);
    status = cnet_ipc_listener_accept_detached(&listener, &accepted);
    check(status == SALTS_OK || status == SALTS_ENOENT);
    check(cmeta_monotonic_ms() < deadline);
  }
}

spec("CNet local IPC") {
  before_each() {
    cnet_client_config config = client_config();
    cnet_ipc_listener_config listen_config;
    memset(probes, 0, sizeof(probes));
    pathname_owned = false;
    memset(connections, 0, sizeof(connections));
    memset(workers, 0, sizeof(workers));
    for (size_t i = 0u; i < 2u; ++i)
      atomic_init(&workers[i].stop, false);
#if defined(_WIN32)
    uri = tstr_format("ipc://salts-cnet-ipc-{}", (uint64_t)GetCurrentProcessId());
#else
    uri = tstr_format("ipc:///tmp/salts-cnet-ipc-{}", (uint64_t)getpid());
#endif
    check_not_null(uri);
    listen_config = listener_config();
    check_equal(cnet_ipc_listener_init(&listener, &listen_config), SALTS_OK);
    pathname_owned = true;
    for (size_t i = 0; i < 2u; ++i)
      check_equal(cnet_client_init(&clients[i], &config), SALTS_OK);
  }
  after_each() {
    for (size_t i = 0u; i < 2u; ++i)
      atomic_store_explicit(&workers[i].stop, true, memory_order_release);
    for (size_t i = 0u; i < 2u; ++i) {
      if (workers[i].thread != NULL) {
        check_warn(cmeta_thread_join(&workers[i].thread) == SALTS_OK);
        check_warn(workers[i].status == SALTS_OK, "worker status: %d", workers[i].status);
      }
      if (workers[i].child.kind != CNET_IPC_RESOURCE_NONE)
        (void)cnet_ipc_accepted_close(&workers[i].child);
    }
    if (buffer != NULL) {
      mem_buffer_release(buffer);
      buffer = NULL;
    }
    if (accepted.kind != CNET_IPC_RESOURCE_NONE)
      check_warn(cnet_ipc_accepted_close(&accepted) == SALTS_OK);
    for (size_t i = 0; i < 2u; ++i) {
      if (clients[i].impl == NULL) continue;
      check_warn(cnet_client_stop(&clients[i], TIMEOUT_MS) == SALTS_OK);
      check_warn(cnet_client_destroy(&clients[i]) == SALTS_OK);
    }
    if (listener.impl != NULL) {
      stop_listener();
      check_warn(cnet_ipc_listener_destroy(&listener) == SALTS_OK);
    }
#if !defined(_WIN32)
    /* This fixture created this exact pathname; the library must preserve it. */
    if (uri != NULL && pathname_owned) check_warn(unlink(uri + sizeof("ipc://") - 1u) == 0);
#endif
    tstr_free(uri);
    uri = NULL;
  }

  it("hands off before backend association and preserves duplex I/O after listener stop") {
    cnet_observer observer = observer_for(1u);
    uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
    connect_and_detach();
    stop_listener();
    check_equal(cnet_client_adopt_ipc(&clients[1], &accepted, &observer, &connections[1]),
                SALTS_OK);
    check_equal(accepted.kind, CNET_IPC_RESOURCE_NONE);
    while (!probes[1].connected) {
      poll_clients();
      check(cmeta_monotonic_ms() < deadline);
    }
    for (size_t i = 0; i < 2u; ++i) {
      check_equal(cnet_receive(&clients[i], connections[i], BYTE_COUNT), SALTS_OK);
      buffer = mem_get_buffer(mem_global(), BYTE_COUNT);
      check_not_null(buffer);
      memcpy(mem_buffer_data(buffer), i == 0u ? "ping" : "pong", BYTE_COUNT);
      mem_set_used(buffer, BYTE_COUNT);
      check_equal(cnet_send_buffer(&clients[i], connections[i], buffer), SALTS_OK);
      mem_buffer_release(buffer);
      buffer = NULL;
    }
    while (probes[0].bytes != BYTE_COUNT || probes[1].bytes != BYTE_COUNT ||
           probes[0].sends != 1u || probes[1].sends != 1u) {
      poll_clients();
      check(cmeta_monotonic_ms() < deadline);
    }
    check_equal(probes[0].data, "pong", BYTE_COUNT);
    check_equal(probes[1].data, "ping", BYTE_COUNT);
  }
  it("consumes a detached descriptor even when target admission fails") {
    connect_and_detach();
    check_equal(cnet_client_adopt_ipc(NULL, &accepted, NULL, NULL), SALTS_EINVAL);
    check_equal(accepted.kind, CNET_IPC_RESOURCE_NONE);
    check_equal(cnet_ipc_accepted_close(&accepted), SALTS_EALREADY);
  }
  it("moves IPC children to two final owners sharing each backend with independent UDP instances") {
    uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
    for (size_t i = 0u; i < 2u; ++i) {
      cnet_connect_options options = {0};
      size_t events;
      options.uri = uri;
      options.observer = observer_for(i);
      check_equal(cnet_ipc_listener_advance(&listener, 2u, &events), SALTS_OK);
      check_equal(cnet_connect(&clients[i], &options, &connections[i]), SALTS_OK);
      while (!probes[i].connected || workers[i].child.kind == CNET_IPC_RESOURCE_NONE) {
        int status;
        poll_clients();
        check_equal(cnet_ipc_listener_advance(&listener, 2u, &events), SALTS_OK);
        if (workers[i].child.kind == CNET_IPC_RESOURCE_NONE) {
          status = cnet_ipc_listener_accept_detached(&listener, &workers[i].child);
          check(status == SALTS_OK || status == SALTS_ENOENT);
        }
        check(cmeta_monotonic_ms() < deadline);
      }
      check_equal(cmeta_thread_create(&workers[i].thread, worker_run, &workers[i]), SALTS_OK);
      check_equal(cnet_receive(&clients[i], connections[i], BYTE_COUNT), SALTS_OK);
      buffer = mem_get_buffer(mem_global(), BYTE_COUNT);
      check_not_null(buffer);
      memcpy(mem_buffer_data(buffer), "echo", BYTE_COUNT);
      mem_set_used(buffer, BYTE_COUNT);
      check_equal(cnet_send_buffer(&clients[i], connections[i], buffer), SALTS_OK);
      mem_buffer_release(buffer);
      buffer = NULL;
    }
    stop_listener();
    while (probes[0].bytes != BYTE_COUNT || probes[1].bytes != BYTE_COUNT) {
      poll_clients();
      check(cmeta_monotonic_ms() < deadline);
    }
    for (size_t i = 0u; i < 2u; ++i)
      atomic_store_explicit(&workers[i].stop, true, memory_order_release);
    for (size_t i = 0u; i < 2u; ++i) {
      check_equal(cmeta_thread_join(&workers[i].thread), SALTS_OK);
      check_equal(workers[i].status, SALTS_OK);
      check_false(workers[i].probe.failed);
      check_equal(workers[i].udp_sends, 2u);
      check_equal(workers[i].udp_receives, 2u);
      check_equal(probes[i].data, "echo", BYTE_COUNT);
    }
  }
  it("exposes bounded wait sources and drains pending cancellation before destroy") {
    size_t events, count;
    bool ready, stopped;
    check_equal(cnet_ipc_listener_advance(&listener, 2u, &events), SALTS_OK);
    check_equal(cnet_ipc_listener_wait_sources(&listener, NULL, 0u, &count, &ready), SALTS_ENOBUFS);
    check(count > 0u && count <= 2u);
    check_false(ready);
    check_equal(cnet_ipc_listener_destroy(&listener), SALTS_EBUSY);
    {
      int status = cnet_ipc_listener_stop(&listener, &stopped);
      check(status == SALTS_OK || status == SALTS_EBUSY);
    }
    check_equal(cnet_ipc_listener_accept_detached(&listener, &accepted), SALTS_ESHUTDOWN);
    stop_listener();
    check_equal(cnet_ipc_listener_wait_sources(&listener, NULL, 0u, &count, &ready), SALTS_OK);
    check_equal(count, 0u);
#if !defined(_WIN32)
    struct stat value;
    check_equal(lstat(uri + sizeof("ipc://") - 1u, &value), 0);
    check(S_ISSOCK(value.st_mode));
#endif
  }
  it("rejects remote, malformed and incompatible names before admission") {
    cnet_ipc_listener rejected = {0};
    cnet_ipc_listener_config config = listener_config();
    config.uri = "ipc://remote/share?query";
    check_equal(cnet_ipc_listener_init(&rejected, &config), SALTS_EINVAL);
    check_null(rejected.impl);
    config = listener_config();
    config.child_capacity = 1u;
    check_equal(cnet_ipc_listener_init(&rejected, &config), SALTS_EINVAL);
    check_null(rejected.impl);
    {
      cnet_stream_socket_options policy = CNET_STREAM_SOCKET_OPTIONS_INIT;
      cnet_connect_options options = {0};
      policy.nodelay = 1;
      check_equal(cnet_client_set_stream_socket_options(&clients[0], &policy), SALTS_OK);
      options.uri = uri;
      options.observer = observer_for(0u);
      check_equal(cnet_connect(&clients[0], &options, &connections[0]), SALTS_ENOTSUP);
      check_equal(connections[0].slot, 0u);
    }
  }
}
