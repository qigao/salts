#include <cnet/handoff.h>
#include <fmt.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <stdatomic.h>
#include <string.h>
#include <tinytest.h>

enum { PRODUCERS = 4, CREDIT_ROUNDS = 1000, TIMEOUT_MS = 5000 };
static cnet_handoff inbox, other;
static cnet_client peer;
static cnet_listener listener;
static tstr uri;
static cnet_accepted_stream streams[PRODUCERS];
static cnet_handoff_ticket tickets[PRODUCERS];
static cmeta_thread_t threads[PRODUCERS];
static atomic_bool stop_workers, published, wake_allowed;
static int worker_status[PRODUCERS];

static native_io_backend_kind backend(void) {
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
  (void)user; (void)connection; (void)state; (void)error;
}
static void init_inbox(size_t credits, size_t queue) {
  const cnet_handoff_config config = {sizeof(config), CNET_HANDOFF_VERSION, credits, queue};
  check_equal(cnet_handoff_init(&inbox, &config), SALTS_OK);
}
static void accept_stream(size_t index) {
  const cnet_connect_options options = {.uri = uri, .observer = {.on_state = on_state}};
  cnet_connection connection;
  const uint64_t end = cmeta_monotonic_ms() + TIMEOUT_MS;
  int status;
  check_equal(cnet_connect(&peer, &options, &connection), SALTS_OK);
  do {
    size_t events;
    check_equal(cnet_client_poll(&peer, 0, &events), SALTS_OK);
    status = cnet_listener_accept_detached(&listener, &streams[index]);
  } while (status == SALTS_ETIMEDOUT && cmeta_monotonic_ms() < end);
  check_equal(status, SALTS_OK);
}
static void credit_producer(void *user) {
  const size_t index = (size_t)(uintptr_t)user;
  int status = SALTS_OK;
  for (size_t i = 0; i < CREDIT_ROUNDS; ++i) {
    if (atomic_load(&stop_workers)) return;
    status = cnet_handoff_reserve(&inbox, &tickets[index]);
    if (status == SALTS_ENOBUFS) { cmeta_thread_yield(); --i; continue; }
    if (status != SALTS_OK) break;
    status = cnet_handoff_release(&inbox, tickets[index]);
    if (status != SALTS_OK) break;
  }
  if (status == SALTS_OK) status = cnet_handoff_reserve(&inbox, &tickets[index]);
  if (status == SALTS_OK) {
    do {
      status = cnet_handoff_publish(&inbox, tickets[index], &streams[index]);
      if (status == SALTS_ENOBUFS) cmeta_thread_yield();
    } while (status == SALTS_ENOBUFS && !atomic_load(&stop_workers));
  }
  worker_status[index] = status;
}
static void wake_tail_producer(void *user) {
  (void)user;
  worker_status[0] = cnet_handoff_reserve(&inbox, &tickets[0]);
  if (worker_status[0] == SALTS_OK)
    worker_status[0] = cnet_handoff_publish(&inbox, tickets[0], &streams[0]);
  atomic_store(&published, true);
  while (!atomic_load(&wake_allowed) && !atomic_load(&stop_workers)) cmeta_thread_yield();
  if (!atomic_load(&stop_workers) && worker_status[0] == SALTS_OK)
    worker_status[0] = cnet_client_wake(&peer);
}
static void join_workers(void) {
  for (size_t i = 0; i < PRODUCERS; ++i) {
    if (threads[i] == NULL) continue;
    check_equal(cmeta_thread_join(&threads[i]), SALTS_OK);
    cmeta_thread_destroy(&threads[i]);
  }
}
static void clean_inbox(cnet_handoff *handoff) {
  if (handoff->impl == NULL) return;
  cnet_handoff_ticket ticket;
  cnet_accepted_stream stream;
  check_equal(cnet_handoff_seal(handoff), SALTS_OK);
  while (cnet_handoff_take(handoff, &ticket, &stream) == SALTS_OK) {
    check_equal(cnet_accepted_stream_close(&stream), SALTS_OK);
    check_equal(cnet_handoff_release(handoff, ticket), SALTS_OK);
  }
  for (size_t i = 0; i < PRODUCERS; ++i) (void)cnet_handoff_release(handoff, tickets[i]);
  check_equal(cnet_handoff_destroy(handoff), SALTS_OK);
}

spec("CNet bounded final-owner admission handoff") {
  before_each() {
    memset(streams, 0, sizeof(streams));
    memset(tickets, 0, sizeof(tickets));
    memset(worker_status, 0, sizeof(worker_status));
    atomic_store(&stop_workers, false);
    atomic_store(&published, false);
    atomic_store(&wake_allowed, false);
    const cnet_client_config config = {.backend = backend(), .connection_capacity = PRODUCERS,
      .command_capacity = 16, .request_capacity = 16, .completion_batch_capacity = 8,
      .event_capacity = 16, .max_send_bytes = 1024, .receive_buffer_bytes = 1024,
      .connect_timeout_ms = TIMEOUT_MS, .write_timeout_ms = TIMEOUT_MS};
    const cnet_listener_config listen_config = {backend(), "127.0.0.1", 0, 8};
    uint16_t port;
    check_equal(cnet_client_init(&peer, &config), SALTS_OK);
    check_equal(cnet_listener_init(&listener, &listen_config), SALTS_OK);
    check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
    uri = tstr_format("tcp://127.0.0.1:{}", port);
    check_not_null(uri);
  }
  after_each() {
    atomic_store(&stop_workers, true);
    join_workers();
    for (size_t i = 0; i < PRODUCERS; ++i)
      if (streams[i].internal_active) check_equal(cnet_accepted_stream_close(&streams[i]), SALTS_OK);
    clean_inbox(&inbox);
    clean_inbox(&other);
    check_equal(cnet_client_stop(&peer, TIMEOUT_MS), SALTS_OK);
    check_equal(cnet_client_destroy(&peer), SALTS_OK);
    check_equal(cnet_listener_close(&listener), SALTS_OK);
    check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    tstr_free(uri);
    uri = NULL;
  }
  it("rejects zero, incompatible and overflowing capacities without publishing storage") {
    cnet_handoff_config config = {sizeof(config), CNET_HANDOFF_VERSION, 0, 1};
    check_equal(cnet_handoff_init(&inbox, &config), SALTS_EINVAL);
    config.connection_capacity = 1;
    config.queue_capacity = 2;
    check_equal(cnet_handoff_init(&inbox, &config), SALTS_EINVAL);
    config.connection_capacity = SIZE_MAX;
    check_equal(cnet_handoff_init(&inbox, &config), SALTS_ERANGE);
    check_null(inbox.impl);
    config.connection_capacity = config.queue_capacity = 1;
    ++config.version;
    check_equal(cnet_handoff_init(&inbox, &config), SALTS_EINVAL);
  }
  it("returns credit exactly once and rejects stale identities across slot and inbox reuse") {
    init_inbox(1, 1);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[0]), SALTS_OK);
    const cnet_handoff_ticket old = tickets[0];
    check_equal(cnet_handoff_reserve(&inbox, &tickets[1]), SALTS_ENOBUFS);
    check_equal(tickets[1].slot, 0u);
    check_equal(cnet_handoff_destroy(&inbox), SALTS_EBUSY);
    check_equal(cnet_handoff_release(&inbox, old), SALTS_OK);
    check_equal(cnet_handoff_release(&inbox, old), SALTS_ENOENT);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[0]), SALTS_OK);
    check_equal(cnet_handoff_release(&inbox, old), SALTS_ENOENT);
    check_equal(cnet_handoff_release(&inbox, tickets[0]), SALTS_OK);
    check_equal(cnet_handoff_destroy(&inbox), SALTS_OK);
    init_inbox(1, 1);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[0]), SALTS_OK);
    check_equal(cnet_handoff_release(&inbox, old), SALTS_ENOENT);
  }
  it("does not accept another final owner's ticket") {
    init_inbox(1, 1);
    const cnet_handoff_config config = {sizeof(config), CNET_HANDOFF_VERSION, 1, 1};
    check_equal(cnet_handoff_init(&other, &config), SALTS_OK);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[0]), SALTS_OK);
    check_equal(cnet_handoff_reserve(&other, &tickets[1]), SALTS_OK);
    check_equal(cnet_handoff_release(&other, tickets[0]), SALTS_ENOENT);
    accept_stream(0);
    check_equal(cnet_handoff_publish(&other, tickets[0], &streams[0]), SALTS_ENOENT);
    check_equal(streams[0].internal_active, 1u);
  }
  it("keeps rejected descriptors and credits with the producer when the queue is full") {
    init_inbox(2, 1);
    for (size_t i = 0; i < 2; ++i) {
      accept_stream(i);
      check_equal(cnet_handoff_reserve(&inbox, &tickets[i]), SALTS_OK);
    }
    check_equal(cnet_handoff_publish(&inbox, tickets[0], &streams[0]), SALTS_OK);
    check_equal(streams[0].internal_active, 0u);
    check_equal(cnet_handoff_release(&inbox, tickets[0]), SALTS_EBUSY);
    const uintptr_t socket = streams[1].internal_socket;
    check_equal(cnet_handoff_publish(&inbox, tickets[1], &streams[1]), SALTS_ENOBUFS);
    check_equal(streams[1].internal_socket, socket);
    check_equal(streams[1].internal_active, 1u);
    check_equal(cnet_handoff_take(&inbox, &tickets[0], &streams[0]), SALTS_OK);
    check_equal(cnet_handoff_publish(&inbox, tickets[1], &streams[1]), SALTS_OK);
    cnet_handoff_snapshot snapshot;
    check_equal(cnet_handoff_get_snapshot(&inbox, &snapshot), SALTS_OK);
    check_equal(snapshot.taken, 1u);
    check_equal(snapshot.queued, 1u);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[2]), SALTS_ENOBUFS);
  }
  it("preserves FIFO and fixed-capacity reuse through repeated queue wrapping") {
    init_inbox(2, 2);
    accept_stream(0);
    accept_stream(1);
    const uintptr_t first = streams[0].internal_socket, second = streams[1].internal_socket;
    for (size_t round = 0; round < 128; ++round) {
      for (size_t i = 0; i < 2; ++i) {
        check_equal(cnet_handoff_reserve(&inbox, &tickets[i]), SALTS_OK);
        check_equal(cnet_handoff_publish(&inbox, tickets[i], &streams[i]), SALTS_OK);
      }
      for (size_t i = 0; i < 2; ++i) {
        check_equal(cnet_handoff_take(&inbox, &tickets[i], &streams[i]), SALTS_OK);
        check_equal(streams[i].internal_socket, i == 0 ? first : second);
        check_equal(cnet_handoff_release(&inbox, tickets[i]), SALTS_OK);
      }
    }
  }
  it("seals publication without losing reserved, queued or taken obligations") {
    init_inbox(3, 2);
    for (size_t i = 0; i < 3; ++i) {
      accept_stream(i);
      check_equal(cnet_handoff_reserve(&inbox, &tickets[i]), SALTS_OK);
    }
    check_equal(cnet_handoff_publish(&inbox, tickets[0], &streams[0]), SALTS_OK);
    check_equal(cnet_handoff_take(&inbox, &tickets[0], &streams[0]), SALTS_OK);
    check_equal(cnet_handoff_publish(&inbox, tickets[1], &streams[1]), SALTS_OK);
    check_equal(cnet_handoff_seal(&inbox), SALTS_OK);
    check_equal(cnet_handoff_seal(&inbox), SALTS_OK);
    check_equal(cnet_handoff_publish(&inbox, tickets[2], &streams[2]), SALTS_ESHUTDOWN);
    check_equal(streams[2].internal_active, 1u);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[3]), SALTS_ESHUTDOWN);
    check_equal(cnet_handoff_destroy(&inbox), SALTS_EBUSY);
    check_equal(cnet_handoff_take(&inbox, &tickets[1], &streams[1]), SALTS_OK);
  }
  it("keeps successful publication owned by the inbox after a host wake fails") {
    init_inbox(1, 1);
    accept_stream(0);
    check_equal(cnet_handoff_reserve(&inbox, &tickets[0]), SALTS_OK);
    check_equal(cnet_handoff_publish(&inbox, tickets[0], &streams[0]), SALTS_OK);
    cnet_client unavailable = {0};
    check_equal(cnet_client_wake(&unavailable), SALTS_EINVAL);
    check_equal(streams[0].internal_active, 0u);
    check_equal(cnet_handoff_release(&inbox, tickets[0]), SALTS_EBUSY);
    check_equal(cnet_handoff_seal(&inbox), SALTS_OK);
    check_equal(cnet_handoff_take(&inbox, &tickets[0], &streams[0]), SALTS_OK);
    check_equal(streams[0].internal_active, 1u);
  }
  it("serializes multiple producers while the final owner drains a smaller queue") {
    init_inbox(PRODUCERS, 2);
    uintptr_t sockets[PRODUCERS];
    bool seen[PRODUCERS] = {false};
    for (size_t i = 0; i < PRODUCERS; ++i) {
      accept_stream(i);
      sockets[i] = streams[i].internal_socket;
    }
    for (size_t i = 0; i < PRODUCERS; ++i)
      check_equal(cmeta_thread_create(&threads[i], credit_producer, (void *)(uintptr_t)i), SALTS_OK);
    const uint64_t end = cmeta_monotonic_ms() + TIMEOUT_MS;
    size_t received = 0;
    while (received < PRODUCERS && cmeta_monotonic_ms() < end) {
      cnet_accepted_stream stream;
      cnet_handoff_ticket ticket;
      const int status = cnet_handoff_take(&inbox, &ticket, &stream);
      if (status == SALTS_ENOENT) { cmeta_thread_yield(); continue; }
      check_equal(status, SALTS_OK);
      size_t index = 0;
      while (index < PRODUCERS && sockets[index] != stream.internal_socket) ++index;
      check_less(index, (size_t)PRODUCERS);
      check_false(seen[index]);
      seen[index] = true;
      check_equal(cnet_accepted_stream_close(&stream), SALTS_OK);
      check_equal(cnet_handoff_release(&inbox, ticket), SALTS_OK);
      ++received;
    }
    check_equal(received, (size_t)PRODUCERS);
    join_workers();
    for (size_t i = 0; i < PRODUCERS; ++i) check_equal(worker_status[i], SALTS_OK);
  }
  it("keeps the wake target alive until a producer's post-publication tail has joined") {
    init_inbox(1, 1);
    accept_stream(0);
    check_equal(cmeta_thread_create(&threads[0], wake_tail_producer, NULL), SALTS_OK);
    const uint64_t end = cmeta_monotonic_ms() + TIMEOUT_MS;
    while (!atomic_load(&published) && cmeta_monotonic_ms() < end) cmeta_thread_yield();
    check_true(atomic_load(&published));
    check_equal(cnet_handoff_seal(&inbox), SALTS_OK);
    cnet_accepted_stream stream;
    cnet_handoff_ticket ticket;
    check_equal(cnet_handoff_take(&inbox, &ticket, &stream), SALTS_OK);
    check_equal(cnet_accepted_stream_close(&stream), SALTS_OK);
    check_equal(cnet_handoff_release(&inbox, ticket), SALTS_OK);
    cnet_handoff_snapshot snapshot;
    check_equal(cnet_handoff_get_snapshot(&inbox, &snapshot), SALTS_OK);
    check_true(snapshot.drained);
    /* Empty is not quiescent: the host joins the tail before inbox/backend teardown. */
    atomic_store(&wake_allowed, true);
    join_workers();
    check_equal(worker_status[0], SALTS_OK);
    check_equal(cnet_handoff_destroy(&inbox), SALTS_OK);
  }
  it("returns every descriptor and reservation when seal races with producers") {
    init_inbox(PRODUCERS, 1);
    for (size_t i = 0; i < PRODUCERS; ++i) accept_stream(i);
    for (size_t i = 0; i < PRODUCERS; ++i)
      check_equal(cmeta_thread_create(&threads[i], credit_producer, (void *)(uintptr_t)i), SALTS_OK);
    check_equal(cnet_handoff_seal(&inbox), SALTS_OK);
    join_workers();
    for (size_t i = 0; i < PRODUCERS; ++i)
      check(worker_status[i] == SALTS_OK || worker_status[i] == SALTS_ESHUTDOWN);
    /* Producer-owned streams and reservations plus queued streams account for
     * all four descriptors; cleanup returns each through its actual owner. */
    cnet_handoff_snapshot snapshot;
    check_equal(cnet_handoff_get_snapshot(&inbox, &snapshot), SALTS_OK);
    size_t producer_owned = 0;
    for (size_t i = 0; i < PRODUCERS; ++i) producer_owned += streams[i].internal_active;
    check_equal(producer_owned + snapshot.queued, (size_t)PRODUCERS);
    check_equal(snapshot.taken, 0u);
    clean_inbox(&inbox);
  }
}
