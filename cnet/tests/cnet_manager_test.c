#include <cnet/manager.h>
#include <cnet/handoff.h>
#include <fmt.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

enum { CAPACITY = 3, TIMEOUT_MS = 3000 };
typedef struct probe {
  cnet_managed_connection managed;
  size_t connected, terminal, recycled, sends, bytes;
  bool reenter;
} probe;
static cnet_client client, peer;
static cnet_listener listener;
static cnet_manager manager;
static cnet_handoff handoff;
static cnet_handoff_ticket ticket;
static cnet_accepted_stream accepted;
static tstr uri;
static probe probes[4];
static cnet_connection connections[4];
static mem_buffer_t *buffer;
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
  probe *p = user;
  (void)connection;
  (void)error;
  if (state == CNET_CONNECTION_CONNECTED) ++p->connected;
  if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
    ++p->terminal;
    if (p->reenter) {
      size_t work = 99;
      cnet_manager_entry entry;
      check_equal(cnet_manager_lookup(&manager, p->managed, &entry), SALTS_OK);
      check_equal(entry.state, CNET_MANAGER_RETIRED);
      check_equal(cnet_manager_release_context(&manager, p->managed), SALTS_OK);
      check_equal(cnet_manager_advance(&manager, CAPACITY, &work), SALTS_EBUSY);
      check_equal(work, 0u);
      check_equal(cnet_manager_destroy(&manager), SALTS_EBUSY);
      check_equal(p->recycled, 0u);
    }
  }
}
static void on_send(void *user, cnet_connection connection, size_t bytes) {
  probe *p = user;
  (void)connection;
  (void)bytes;
  ++p->sends;
}
static void on_slice(void *user, cnet_connection connection, mem_slice_t slice,
                     cnet_message_kind kind) {
  probe *p = user;
  size_t work;
  (void)connection;
  (void)kind;
  p->bytes += slice.length;
  check_equal(cnet_manager_advance(&manager, CAPACITY, &work), SALTS_EBUSY);
  mem_slice_release(&slice);
}
static void on_recycle(void *user) {
  probe *p = user;
  size_t work;
  ++p->recycled;
  check_equal(cnet_manager_destroy(&manager), SALTS_EBUSY);
  check_equal(cnet_manager_advance(&manager, 1u, &work), SALTS_EBUSY);
}
static cnet_observer observer(size_t index) {
  return (cnet_observer){.on_state = on_state, .on_send = on_send, .user = &probes[index]};
}
static cnet_client_config client_config(void) {
  return (cnet_client_config){.backend = backend_kind(),
                              .connection_capacity = CAPACITY,
                              .command_capacity = 16,
                              .request_capacity = 16,
                              .completion_batch_capacity = 8,
                              .event_capacity = 16,
                              .max_send_bytes = 1024,
                              .receive_buffer_bytes = 1024,
                              .connect_timeout_ms = TIMEOUT_MS,
                              .write_timeout_ms = TIMEOUT_MS};
}
static void init_manager(size_t records, size_t credits) {
  const cnet_manager_config config = {sizeof(config), CNET_MANAGER_VERSION, &client, records,
                                      credits};
  check_equal(cnet_manager_init(&manager, &config), SALTS_OK);
}
static void reserve(size_t index, bool held) {
  const cnet_manager_attachment attachment = {observer(index), on_recycle, held};
  check_equal(cnet_manager_reserve(&manager, &attachment, &probes[index].managed), SALTS_OK);
}
static void progress(void) {
  size_t events;
  check_equal(cnet_client_poll(&client, 0u, &events), SALTS_OK);
  check_equal(cnet_client_poll(&peer, 0u, &events), SALTS_OK);
}
static void wait_count(size_t *count, size_t expected) {
  const uint64_t end = cmeta_monotonic_ms() + TIMEOUT_MS;
  while (*count < expected && cmeta_monotonic_ms() < end)
    progress();
  check_equal(*count, expected);
}
static void connect_managed(size_t index) {
  const cnet_connect_options options = {.uri = uri};
  check_equal(cnet_manager_connect(&manager, probes[index].managed, &options, &connections[index]),
              SALTS_OK);
}
static void connect_raw(cnet_client *target, size_t index) {
  const cnet_connect_options options = {.uri = uri, .observer = observer(index)};
  check_equal(cnet_connect(target, &options, &connections[index]), SALTS_OK);
}
static void detach(void) {
  int status;
  const uint64_t end = cmeta_monotonic_ms() + TIMEOUT_MS;
  do {
    progress();
    status = cnet_listener_accept_detached(&listener, &accepted);
  } while (status == SALTS_ETIMEDOUT && cmeta_monotonic_ms() < end);
  check_equal(status, SALTS_OK);
}
static void foreign_owner(void *user) {
  int *results = user;
  cnet_manager_snapshot snapshot;
  results[0] = cnet_manager_get_snapshot(&manager, &snapshot);
  results[1] = cnet_manager_cancel(&manager, probes[0].managed);
  results[2] = cnet_manager_destroy(&manager);
}
spec("CNet owner-local connection manager") {
  before_each() {
    cnet_client_config config = client_config();
    cnet_listener_config listen_config = {backend_kind(), "127.0.0.1", 0, 8};
    uint16_t port;
    memset(probes, 0, sizeof(probes));
    memset(connections, 0, sizeof(connections));
    accepted = (cnet_accepted_stream)CNET_ACCEPTED_STREAM_INIT;
    check_equal(cnet_client_init(&client, &config), SALTS_OK);
    check_equal(cnet_client_init(&peer, &config), SALTS_OK);
    check_equal(cnet_listener_init(&listener, &listen_config), SALTS_OK);
    check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
    uri = tstr_format("tcp://127.0.0.1:{}", port);
    check_not_null(uri);
  }
  after_each() {
    if (accepted.internal_active) (void)cnet_accepted_stream_close(&accepted);
    if (client.impl) check_equal(cnet_client_stop(&client, TIMEOUT_MS), SALTS_OK);
    if (peer.impl) check_equal(cnet_client_stop(&peer, TIMEOUT_MS), SALTS_OK);
    if (manager.impl) {
      cnet_manager_snapshot snapshot;
      size_t work;
      check_equal(cnet_manager_get_snapshot(&manager, &snapshot), SALTS_OK);
      for (size_t i = 0; i < snapshot.record_capacity; ++i) {
        cnet_manager_entry entry;
        if (cnet_manager_inspect(&manager, i, &entry) != SALTS_OK) continue;
        if (entry.state == CNET_MANAGER_RESERVED)
          check_equal(cnet_manager_cancel(&manager, entry.managed), SALTS_OK);
        if (entry.context_held)
          check_equal(cnet_manager_release_context(&manager, entry.managed), SALTS_OK);
      }
      check_equal(cnet_manager_advance(&manager, snapshot.record_capacity, &work), SALTS_OK);
      check_equal(cnet_manager_destroy(&manager), SALTS_OK);
    }
    if (client.impl) check_equal(cnet_client_destroy(&client), SALTS_OK);
    if (peer.impl) check_equal(cnet_client_destroy(&peer), SALTS_OK);
    if (listener.impl) {
      check_equal(cnet_listener_close(&listener), SALTS_OK);
      check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    }
    if (buffer) {
      mem_buffer_release(buffer);
      buffer = NULL;
    }
    tstr_free(uri);
    uri = NULL;
    if (handoff.impl) {
      (void)cnet_handoff_release(&handoff, ticket);
      check_equal(cnet_handoff_destroy(&handoff), SALTS_OK);
    }
  }
  it("rejects invalid and overflow capacities before publishing storage") {
    cnet_manager_config config = {sizeof(config), CNET_MANAGER_VERSION, &client, 0, 1};
    check_equal(cnet_manager_init(&manager, &config), SALTS_EINVAL);
    config.record_capacity = 1;
    config.connection_capacity = 2;
    check_equal(cnet_manager_init(&manager, &config), SALTS_EINVAL);
    config.record_capacity = SIZE_MAX;
    config.connection_capacity = 1;
    check_equal(cnet_manager_init(&manager, &config), SALTS_ERANGE);
    check_null(manager.impl);
  }
  it("bounds reservations and rejects stale identities across recycle and reinitialization") {
    cnet_manager_entry entry;
    cnet_managed_connection old;
    cnet_manager_attachment attachment = {observer(1), on_recycle, false};
    size_t work;
    init_manager(1, 1);
    reserve(0, false);
    old = probes[0].managed;
    check_equal(cnet_manager_reserve(&manager, &attachment, &probes[1].managed), SALTS_ENOBUFS);
    check_equal(cnet_manager_cancel(&manager, old), SALTS_OK);
    check_equal(cnet_manager_cancel(&manager, old), SALTS_EALREADY);
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    check_equal(probes[0].recycled, 1u);
    reserve(1, false);
    check_equal(cnet_manager_lookup(&manager, old, &entry), SALTS_ENOENT);
    check_equal(cnet_manager_cancel(&manager, probes[1].managed), SALTS_OK);
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    check_equal(cnet_manager_destroy(&manager), SALTS_OK);
    init_manager(1, 1);
    reserve(2, false);
    check_equal(cnet_manager_cancel(&manager, old), SALTS_ENOENT);
  }
  it("returns credit at terminal while retaining a separate context record") {
    cnet_manager_snapshot snapshot;
    size_t work;
    init_manager(2, 1);
    reserve(0, true);
    connect_managed(0);
    wait_count(&probes[0].connected, 1);
    check_equal(cnet_close(&client, connections[0]), SALTS_OK);
    wait_count(&probes[0].terminal, 1);
    check_equal(cnet_manager_get_snapshot(&manager, &snapshot), SALTS_OK);
    check_equal(snapshot.bound, 0u);
    check_equal(snapshot.retired, 1u);
    check_false(snapshot.runnable);
    check_false(snapshot.drained);
    reserve(1, false);
    check_equal(cnet_manager_destroy(&manager), SALTS_EBUSY);
    check_equal(cnet_manager_release_context(&manager, probes[0].managed), SALTS_OK);
    check_equal(cnet_manager_advance(&manager, 2, &work), SALTS_OK);
    check_equal(probes[0].recycled, 1u);
  }
  it("rejects foreign threads without changing owner obligations") {
    cmeta_thread_t thread = NULL;
    int results[3] = {0};
    cnet_manager_snapshot snapshot;
    init_manager(1, 1);
    reserve(0, true);
    check_equal(cmeta_thread_create(&thread, foreign_owner, results), SALTS_OK);
    check_equal(cmeta_thread_join(&thread), SALTS_OK);
    cmeta_thread_destroy(&thread);
    for (size_t i = 0; i < 3; ++i)
      check_equal(results[i], SALTS_EPERM);
    check_equal(cnet_manager_get_snapshot(&manager, &snapshot), SALTS_OK);
    check_equal(snapshot.reserved, 1u);
    check_equal(snapshot.context_holds, 1u);
  }
  it("keeps full retired storage unavailable until its context hold ends") {
    cnet_manager_attachment attachment = {observer(1), on_recycle, false};
    size_t work;
    init_manager(1, 1);
    reserve(0, true);
    check_equal(cnet_manager_cancel(&manager, probes[0].managed), SALTS_OK);
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    check_equal(work, 0u);
    check_equal(cnet_manager_reserve(&manager, &attachment, &probes[1].managed), SALTS_ENOBUFS);
    check_equal(cnet_manager_release_context(&manager, probes[0].managed), SALTS_OK);
    check_equal(cnet_manager_release_context(&manager, probes[0].managed), SALTS_EALREADY);
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    reserve(1, false);
  }
  it("preserves detached ownership on stale input and consumes a sealed valid attempt") {
    cnet_managed_connection foreign;
    init_manager(1, 1);
    reserve(0, false);
    connect_raw(&peer, 1);
    detach();
    foreign = probes[0].managed;
    ++foreign.incarnation;
    check_equal(cnet_manager_adopt(&manager, foreign, &accepted, NULL, &connections[0]),
                SALTS_ENOENT);
    check_true(accepted.internal_active != 0u);
    check_equal(cnet_manager_seal(&manager), SALTS_OK);
    check_equal(cnet_manager_adopt(&manager, probes[0].managed, &accepted, NULL, &connections[0]),
                SALTS_ESHUTDOWN);
    check_equal(accepted.internal_active, 0u);
    check_equal(probes[0].terminal, 0u);
  }
  it("never recycles inside the real terminal callback even when it releases the hold") {
    size_t work;
    init_manager(1, 1);
    reserve(0, true);
    probes[0].reenter = true;
    connect_managed(0);
    wait_count(&probes[0].connected, 1);
    check_equal(cnet_close(&client, connections[0]), SALTS_OK);
    wait_count(&probes[0].terminal, 1);
    check_equal(probes[0].recycled, 0u);
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    check_equal(probes[0].recycled, 1u);
  }
  it("rolls failed connects back without fabricating callbacks or dropping context") {
    const cnet_connect_options bad = {.uri = "udp://127.0.0.1:1"};
    size_t work;
    init_manager(1, 1);
    reserve(0, false);
    check_equal(cnet_manager_connect(&manager, probes[0].managed, &bad, &connections[0]),
                SALTS_ENOTSUP);
    check_equal(connections[0].slot, 0u);
    check_equal(probes[0].terminal, 0u);
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    check_equal(probes[0].recycled, 1u);
  }
  it("consumes detached adoption on shared-client exhaustion after reservation") {
    init_manager(1, 1);
    reserve(0, false);
    connect_raw(&client, 1);
    connect_raw(&client, 2);
    connect_raw(&client, 3);
    wait_count(&probes[1].connected, 1);
    detach();
    check_equal(cnet_manager_adopt(&manager, probes[0].managed, &accepted, NULL, &connections[0]),
                SALTS_ENOBUFS);
    check_equal(accepted.internal_active, 0u);
    check_equal(probes[0].terminal, 0u);
  }
  it("seals admission and closes only its subset with bounded management work") {
    cnet_manager_snapshot snapshot;
    size_t work;
    init_manager(2, 2);
    reserve(0, false);
    connect_managed(0);
    reserve(2, false);
    connect_raw(&client, 1);
    wait_count(&probes[0].connected, 1);
    wait_count(&probes[1].connected, 1);
    check_equal(cnet_manager_request_close(&manager), SALTS_OK);
    for (size_t i = 0; i < 8; ++i) {
      check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
      check_less_equal(work, 1u);
      progress();
    }
    wait_count(&probes[0].terminal, 1);
    check_equal(probes[1].terminal, 0u);
    check_equal(cnet_manager_advance(&manager, 2, &work), SALTS_OK);
    check_equal(cnet_manager_get_snapshot(&manager, &snapshot), SALTS_OK);
    check_true(snapshot.drained);
    check_true(snapshot.sealed);
    check_equal(probes[2].recycled, 1u);
  }
  it("bridges owned receive delivery without allowing callback-time recycle") {
    size_t bytes = 1;
    init_manager(1, 1);
    reserve(0, false);
    connect_raw(&peer, 1);
    detach();
    check_equal(cnet_manager_adopt(&manager, probes[0].managed, &accepted, NULL, &connections[0]),
                SALTS_OK);
    wait_count(&probes[0].connected, 1);
    wait_count(&probes[1].connected, 1);
    check_equal(
        cnet_manager_set_receive_slice_handler(&manager, probes[0].managed, on_slice, &probes[0]),
        SALTS_OK);
    check_equal(cnet_receive(&client, connections[0], 1), SALTS_OK);
    buffer = mem_get_buffer(mem_global(), 1);
    check_not_null(buffer);
    memcpy(mem_buffer_data(buffer), "x", bytes);
    mem_set_used(buffer, bytes);
    check_equal(cnet_send_buffer(&peer, connections[1], buffer), SALTS_OK);
    wait_count(&probes[0].bytes, 1);
  }
  it("adopts a handed-off stream and retains owner credit through real terminal") {
    const cnet_handoff_config config = {sizeof(config), CNET_HANDOFF_VERSION, 1, 1};
    check_equal(cnet_handoff_init(&handoff, &config), SALTS_OK);
    init_manager(1, 1);
    connect_raw(&peer, 1);
    detach();
    check_equal(cnet_handoff_reserve(&handoff, &ticket), SALTS_OK);
    check_equal(cnet_handoff_publish(&handoff, ticket, &accepted), SALTS_OK);
    check_equal(cnet_handoff_take(&handoff, &ticket, &accepted), SALTS_OK);
    reserve(0, false);
    check_equal(cnet_manager_adopt(&manager, probes[0].managed, &accepted, NULL, &connections[0]),
                SALTS_OK);
    wait_count(&probes[0].connected, 1);
    cnet_handoff_ticket rejected;
    check_equal(cnet_handoff_reserve(&handoff, &rejected), SALTS_ENOBUFS);
    check_equal(cnet_close(&client, connections[0]), SALTS_OK);
    wait_count(&probes[0].terminal, 1);
    check_equal(cnet_handoff_release(&handoff, ticket), SALTS_OK);
    cnet_handoff_snapshot snapshot;
    check_equal(cnet_handoff_get_snapshot(&handoff, &snapshot), SALTS_OK);
    check_true(snapshot.drained);
  }
  it("returns handoff credit and retires context after invalid TLS adoption consumes the stream") {
    const cnet_handoff_config config = {sizeof(config), CNET_HANDOFF_VERSION, 1, 1};
    const cnet_tls_server unavailable_tls = {0};
    check_equal(cnet_handoff_init(&handoff, &config), SALTS_OK);
    init_manager(1, 1);
    connect_raw(&peer, 1);
    detach();
    check_equal(cnet_handoff_reserve(&handoff, &ticket), SALTS_OK);
    check_equal(cnet_handoff_publish(&handoff, ticket, &accepted), SALTS_OK);
    check_equal(cnet_handoff_take(&handoff, &ticket, &accepted), SALTS_OK);
    reserve(0, false);
    check_equal(cnet_manager_adopt(&manager, probes[0].managed, &accepted, &unavailable_tls,
                                  &connections[0]), SALTS_EINVAL);
    check_equal(accepted.internal_active, 0u);
    check_equal(cnet_handoff_release(&handoff, ticket), SALTS_OK);
    size_t work;
    check_equal(cnet_manager_advance(&manager, 1, &work), SALTS_OK);
    check_equal(probes[0].terminal, 0u);
    check_equal(probes[0].recycled, 1u);
  }
}
