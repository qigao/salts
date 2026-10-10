#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#endif

enum { TEST_BYTES = 64, TEST_DEADLINE_MS = 5000, TEST_BURSTS = 64 };
typedef struct progress_fixture {
  cnet_client client;
  cnet_listener listener;
  cnet_connection sender, receiver;
  mem_buffer_t *payload;
  size_t connected, sent, received_bytes, target, callbacks;
  int error, closing, wake_from_callback, check_reentry;
} progress_fixture;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static cnet_client_config test_config(void) {
  const cnet_client_config config = {.backend = test_backend(), .connection_capacity = 2u,
      .command_capacity = 16u, .request_capacity = 16u, .completion_batch_capacity = 1u,
      .event_capacity = 16u, .max_send_bytes = TEST_BYTES, .receive_buffer_bytes = 4096u};
  return config;
}
static void fixture_state(void *user, cnet_connection connection,
                          cnet_connection_state state, const cnet_error *error) {
  progress_fixture *f = user;
  (void)connection;
  ++f->callbacks;
  if (state == CNET_CONNECTION_CONNECTED) ++f->connected;
  else if (!f->closing && (state == CNET_CONNECTION_FAILED || state == CNET_CONNECTION_CLOSED))
    f->error = error != NULL && error->status != SALTS_OK ? error->status : SALTS_ENOTCONN;
}
static void fixture_received(void *user, cnet_connection connection, const cnet_receive_view *view) {
  progress_fixture *f = user;
  (void)connection;
  ++f->callbacks;
  if (view->kind != CNET_MESSAGE_BYTES || view->size > f->target * TEST_BYTES - f->received_bytes) {
    f->error = SALTS_EPROTO; return;
  }
  const unsigned char *data = view->data;
  for (size_t i = 0u; i < view->size; ++i)
    if (data[i] != (unsigned char)((f->received_bytes + i) % TEST_BYTES)) f->error = SALTS_EPROTO;
  f->received_bytes += view->size;
}
static void fixture_sent(void *user, cnet_connection connection, size_t size) {
  progress_fixture *f = user;
  ++f->callbacks;
  if (size != TEST_BYTES || ++f->sent > f->target) { f->error = SALTS_EPROTO; return; }
  if (f->check_reentry) {
    size_t events = SIZE_MAX;
    if (cnet_client_poll_strategy(&f->client, NULL, 0u, &events) != SALTS_EBUSY || events != 0u ||
        cnet_client_poll(&f->client, 0u, &events) != SALTS_EBUSY ||
        cnet_client_stop(&f->client, 0u) != SALTS_EBUSY) f->error = SALTS_EPROTO;
  }
  /* The same immutable backing may be retained again; no mutation or borrowed
     receive view crosses a callback. Only one new write is admitted each time. */
  if (!f->closing && f->sent < f->target && f->error == SALTS_OK)
    f->error = cnet_send_buffer(&f->client, connection, f->payload);
  if (!f->closing && f->wake_from_callback && f->error == SALTS_OK)
    f->error = cnet_client_wake(&f->client);
}
static int fixture_open(progress_fixture *f) {
  const cnet_client_config config = test_config();
  const cnet_listener_config listener = {test_backend(), "127.0.0.1", 0u, 1u};
  cnet_stream_socket_options options = CNET_STREAM_SOCKET_OPTIONS_INIT;
  cnet_stream_peer peer;
  options.nodelay = 1;
  int status = cnet_client_init(&f->client, &config);
  if (status == SALTS_OK) status = cnet_client_set_stream_socket_options(&f->client, &options);
  if (status == SALTS_OK) status = cnet_listener_init(&f->listener, &listener);
  if (status == SALTS_OK) status = cnet_listener_tcp_option_set(&f->listener, CNET_TCP_SOCKET_NODELAY, 1u);
  if (status == SALTS_OK) status = cnet_listener_local(&f->listener, &peer);
  const cnet_observer sender = {.on_state = fixture_state, .on_send = fixture_sent, .user = f};
  const cnet_observer receiver = {.on_state = fixture_state, .on_receive = fixture_received, .user = f};
  if (status == SALTS_OK) status = cnet_connect_peer(&f->client, &peer, NULL, &sender, &f->sender);
  const uint64_t deadline = cmeta_monotonic_ms() + TEST_DEADLINE_MS;
  int accepted = 0;
  while (status == SALTS_OK && f->connected != 2u) {
    size_t events;
    if (cmeta_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
    if (!accepted) {
      status = cnet_listener_accept(&f->listener, &f->client, &receiver, &f->receiver);
      if (status == SALTS_OK) accepted = 1;
      else if (status == SALTS_ETIMEDOUT) status = SALTS_OK;
    }
    if (status == SALTS_OK) status = cnet_client_poll(&f->client, 0u, &events);
    if (status == SALTS_OK) status = f->error;
  }
  if (status == SALTS_OK) status = cnet_listener_close(&f->listener);
  if (status == SALTS_OK) status = cnet_listener_destroy(&f->listener);
  if (status != SALTS_OK) return status;
  f->payload = mem_get_buffer(mem_global(), TEST_BYTES);
  if (f->payload == NULL) return SALTS_ENOMEM;
  for (size_t i = 0u; i < TEST_BYTES; ++i) ((unsigned char *)mem_buffer_data(f->payload))[i] = (unsigned char)i;
  mem_set_used(f->payload, TEST_BYTES);
  return SALTS_OK;
}
static void fixture_close(progress_fixture *f) {
  f->closing = 1;
  if (f->client.impl != NULL) {
    if (cnet_client_stop(&f->client, TEST_DEADLINE_MS) != SALTS_OK ||
        cnet_client_destroy(&f->client) != SALTS_OK) abort();
  }
  if (f->listener.impl != NULL) {
    if (cnet_listener_close(&f->listener) != SALTS_OK || cnet_listener_destroy(&f->listener) != SALTS_OK) abort();
  }
  if (f->payload != NULL && mem_buffer_ref_count(f->payload) != 1u) abort();
  mem_buffer_release(f->payload);
  memset(f, 0, sizeof(*f));
}
static int fixture_begin(progress_fixture *f, size_t count) {
  f->sent = f->received_bytes = 0u; f->target = count;
  int status = cnet_receive(&f->client, f->receiver, count * TEST_BYTES);
  if (status == SALTS_OK) status = cnet_send_buffer(&f->client, f->sender, f->payload);
  return status;
}
static int fixture_drive(progress_fixture *f, size_t passes, int wake) {
  cnet_progress_strategy strategy = CNET_PROGRESS_STRATEGY_INIT;
  strategy.max_poll_passes = passes;
  strategy.idle_wait_ms = 1000u;
  const uint64_t deadline = cmeta_monotonic_ms() + TEST_DEADLINE_MS;
  while (f->sent != f->target || f->received_bytes != f->target * TEST_BYTES) {
    const size_t before = f->callbacks, sent = f->sent;
    size_t events = 0u;
    int status = cnet_client_poll_strategy(&f->client, passes == 0u ? NULL : &strategy, 0u, &events);
    if (status != SALTS_OK) return status;
    if (events != f->callbacks - before ||
        f->sent - sent > (passes == 0u ? CNET_PROGRESS_DEFAULT_PASSES : passes)) return SALTS_EPROTO;
    if (f->error != SALTS_OK) return f->error;
    if (wake && f->sent != sent) {
      const uint64_t started = cmeta_monotonic_ms();
      status = cnet_client_poll_strategy(&f->client, &strategy, 1000u, &events);
      if (status != SALTS_OK || events != 0u || cmeta_monotonic_ms() - started >= 900u)
        return SALTS_EPROTO;
    }
    if (cmeta_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

typedef struct wake_probe { cnet_client *client; atomic_int start; int wake_status, busy_status; } wake_probe;
static void wake_worker(void *user) {
  wake_probe *p = user;
  while (!atomic_load(&p->start)) cmeta_thread_yield();
  cmeta_sleep_ms(20u);
  size_t events;
  p->busy_status = cnet_client_poll_strategy(p->client, NULL, 0u, &events);
  p->wake_status = cnet_client_wake(p->client);
}
static uint64_t process_cpu_ns(void) {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  ULARGE_INTEGER k, u;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) abort();
  k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
  return (k.QuadPart + u.QuadPart) * 100u;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) abort();
  return ((uint64_t)usage.ru_utime.tv_sec + (uint64_t)usage.ru_stime.tv_sec) * 1000000000u +
         ((uint64_t)usage.ru_utime.tv_usec + (uint64_t)usage.ru_stime.tv_usec) * 1000u;
#endif
}
static int compare_ns(const void *a, const void *b) {
  const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}
static int measured_poll(progress_fixture *f, int mode, uint32_t remaining) {
  const cnet_progress_strategy spin = CNET_PROGRESS_STRATEGY_SPIN_INIT;
  size_t events;
  int status = mode == 0 ? cnet_client_poll(&f->client, 0u, &events)
      : cnet_client_poll_strategy(&f->client, mode == 1 ? &spin : NULL, remaining, &events);
  return status == SALTS_OK ? f->error : status;
}
static int measure(progress_fixture *f, int mode, int load, size_t repeat) {
  size_t polls = 0u, messages = 0u;
  uint64_t scheduled[TEST_BURSTS] = {0}, duration[TEST_BURSTS] = {0};
  int status = fixture_begin(f, 32u);
  if (status == SALTS_OK) status = fixture_drive(f, CNET_PROGRESS_DEFAULT_PASSES, 0);
  if (status != SALTS_OK) return status;
  /* Keep an outstanding receive during idle periods; callback delivery must
     remain empty until the next scheduled application burst. */
  status = cnet_receive(&f->client, f->receiver, TEST_BYTES);
  if (status != SALTS_OK) return status;
  const uint64_t cpu_before = process_cpu_ns(), start = cmeta_hrtime();
  const uint64_t deadline = cmeta_monotonic_ms() + TEST_DEADLINE_MS;
  const size_t rounds = load == 1 ? TEST_BURSTS : 1u;
  for (size_t round = 0u; status == SALTS_OK && round < rounds; ++round) {
    const uint64_t due = start + (load == 0 ? 400000000u : load == 1 ? (round + 1u) * 5000000u : 0u);
    uint64_t now;
    while ((now = cmeta_hrtime()) < due && status == SALTS_OK) {
      ++polls;
      status = measured_poll(f, mode, (uint32_t)((due - now + 999999u) / 1000000u));
    }
    if (status != SALTS_OK || load == 0) break;
    const uint64_t began = cmeta_hrtime();
    const size_t count = load == 1 ? 32u : 16384u;
    status = fixture_begin(f, count);
    while (status == SALTS_OK && (f->sent != count || f->received_bytes != count * TEST_BYTES)) {
      ++polls;
      status = measured_poll(f, mode, CNET_PROGRESS_DEFAULT_IDLE_WAIT_MS);
      if (cmeta_monotonic_ms() >= deadline) status = SALTS_ETIMEDOUT;
    }
    messages += f->sent;
    duration[round] = cmeta_hrtime() - began;
    scheduled[round] = cmeta_hrtime() - due;
  }
  const uint64_t elapsed = cmeta_hrtime() - start, cpu = process_cpu_ns() - cpu_before;
  qsort(scheduled, rounds, sizeof(*scheduled), compare_ns);
  qsort(duration, rounds, sizeof(*duration), compare_ns);
  if (status == SALTS_OK)
    printf("PROGRESS_RESULT,%s,%s,%zu,%zu,%llu,%llu,%.6f,%zu,%llu,%llu\n",
        mode == 0 ? "legacy_spin" : mode == 1 ? "drain_spin" : "balanced",
        load == 0 ? "idle" : load == 1 ? "paced" : "saturated", repeat, messages,
        (unsigned long long)elapsed, (unsigned long long)cpu, (double)cpu / elapsed, polls,
        (unsigned long long)duration[rounds - 1u], (unsigned long long)scheduled[rounds - 1u]);
  return status;
}

spec("CNet progress strategy") {
  static progress_fixture fixture;
  before_each() { memset(&fixture, 0, sizeof(fixture)); }
  after_each() { fixture_close(&fixture); }

  it("strategy correctness: validates configuration before progressing and caps idle waiting") {
    cnet_client_config config = test_config();
    cnet_progress_strategy strategy = CNET_PROGRESS_STRATEGY_INIT;
    size_t events = SIZE_MAX;
    check_equal(cnet_client_init(&fixture.client, &config), SALTS_OK);
    strategy.size = 0u;
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 0u, &events), SALTS_EINVAL);
    check_equal(events, 0u);
    strategy.size = sizeof(strategy); strategy.max_poll_passes = 0u;
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 0u, &events), SALTS_EINVAL);
    strategy.max_poll_passes = CNET_PROGRESS_MAX_PASSES + 1u;
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 0u, &events), SALTS_EINVAL);
    check_equal(cnet_client_poll_strategy(NULL, NULL, 0u, &events), SALTS_EINVAL);
    check_equal(cnet_client_poll_strategy(&fixture.client, NULL, 0u, NULL), SALTS_EINVAL);
    const uint64_t start = cmeta_monotonic_ms();
    check_equal(cnet_client_poll_strategy(&fixture.client, NULL, 1000u, &events), SALTS_OK);
    check_equal(events, 0u);
    check_greater_equal(cmeta_monotonic_ms() - start, (uint64_t)CNET_PROGRESS_DEFAULT_IDLE_WAIT_MS);
    check_less(cmeta_monotonic_ms() - start, (uint64_t)900u);
    strategy.max_poll_passes = 1u; strategy.idle_wait_ms = 1000u;
    const uint64_t capped = cmeta_monotonic_ms();
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 1u, &events), SALTS_OK);
    check_greater_equal(cmeta_monotonic_ms() - capped, (uint64_t)1u);
    check_less(cmeta_monotonic_ms() - capped, (uint64_t)900u);
    strategy.idle_wait_ms = 0u;
    const uint64_t spinning = cmeta_monotonic_ms();
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 1000u, &events), SALTS_OK);
    check_less(cmeta_monotonic_ms() - spinning, (uint64_t)900u);
    check_equal(cnet_client_stop(&fixture.client, TEST_DEADLINE_MS), SALTS_OK);
    check_equal(cnet_client_poll_strategy(&fixture.client, NULL, 0u, &events), SALTS_ESHUTDOWN);
    check_equal(cnet_client_destroy(&fixture.client), SALTS_OK);
  }
  it("strategy correctness: keeps callback-driven sends bounded and rejects recursive progress") {
    const size_t passes[] = {0u, 1u, CNET_PROGRESS_DEFAULT_PASSES, CNET_PROGRESS_MAX_PASSES};
    for (size_t i = 0u; i < 4u; ++i) {
      check_equal(fixture_open(&fixture), SALTS_OK);
      fixture.check_reentry = 1;
      check_equal(fixture_begin(&fixture, 257u), SALTS_OK);
      check_equal(fixture_drive(&fixture, passes[i], 0), SALTS_OK);
      fixture_close(&fixture);
    }
  }
  it("strategy correctness: callback wake survives drain and the following idle transition") {
    check_equal(fixture_open(&fixture), SALTS_OK);
    fixture.wake_from_callback = 1;
    check_equal(fixture_begin(&fixture, 33u), SALTS_OK);
    check_equal(fixture_drive(&fixture, CNET_PROGRESS_DEFAULT_PASSES, 1), SALTS_OK);
  }
  it("strategy correctness: shutdown drains an unfinished retained write chain") {
    check_equal(fixture_open(&fixture), SALTS_OK);
    check_equal(fixture_begin(&fixture, 257u), SALTS_OK);
    size_t events;
    cnet_progress_strategy strategy = CNET_PROGRESS_STRATEGY_INIT;
    strategy.max_poll_passes = 1u;
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 0u, &events), SALTS_OK);
    check_less(fixture.sent, fixture.target);
    fixture_close(&fixture);
  }
  it("strategy correctness: an external wake interrupts waiting and concurrent polls are rejected") {
    check_equal(fixture_open(&fixture), SALTS_OK);
    check_equal(cnet_receive(&fixture.client, fixture.receiver, 1u), SALTS_OK);
    cnet_progress_strategy strategy = CNET_PROGRESS_STRATEGY_INIT;
    strategy.idle_wait_ms = 1000u;
    wake_probe probe = {.client = &fixture.client};
    atomic_init(&probe.start, 0);
    cmeta_thread_t thread;
    check_equal(cmeta_thread_create(&thread, wake_worker, &probe), SALTS_OK);
    size_t events;
    atomic_store(&probe.start, 1);
    const uint64_t start = cmeta_monotonic_ms();
    const int status = cnet_client_poll_strategy(&fixture.client, &strategy, 1000u, &events);
    const uint64_t elapsed = cmeta_monotonic_ms() - start;
    check_equal(cmeta_thread_join(&thread), SALTS_OK);
    check_equal(status, SALTS_OK); check_equal(events, 0u);
    check_equal(probe.busy_status, SALTS_EBUSY); check_equal(probe.wake_status, SALTS_OK);
    check_less(elapsed, (uint64_t)900u);
    check_equal(cnet_client_wake(&fixture.client), SALTS_OK);
    check_equal(cnet_client_poll_strategy(&fixture.client, &strategy, 1000u, &events), SALTS_OK);
    check_equal(events, 0u);
  }
  bench("strategy measurements: idle, scheduled bursts and saturated callback chains") {
    printf("PROGRESS_CONFIG,payload=64,burst=32,period_ms=5,paced_rounds=64,idle_ms=400,repeats=5,owners=1,affinity=unbound,cpu=whole_process,completion_capacity=1\n");
    printf("PROGRESS_HEADER,mode,load,repeat,messages,wall_ns,cpu_ns,cpu_equivalents,poll_calls,max_burst_ns,max_scheduled_completion_ns\n");
    for (size_t repeat = 0u; repeat < 5u; ++repeat)
      for (int load = 0; load < 3; ++load)
        for (int order = 0; order < 3; ++order) {
          check_equal(fixture_open(&fixture), SALTS_OK);
          check_equal(measure(&fixture, (order + (int)repeat) % 3, load, repeat + 1u), SALTS_OK);
          fixture_close(&fixture);
        }
  }
}
