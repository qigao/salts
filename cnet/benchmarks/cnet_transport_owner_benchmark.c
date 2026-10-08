#include "cnet_io_benchmark_config.h"
#include <cnet/ipc.h>
#include <fmt.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>
#include <uv.h>

enum {
  PAIRS = 4,
  CHANNELS = 2 * PAIRS,
  PAYLOAD = 1024,
  WARMUP = 32,
  SAMPLES = 512,
  TIMEOUT_MS = 10000,
  REQUESTS = 32,
  COMMANDS = 32
};

typedef struct transport_lane transport_lane;
typedef struct transport_channel {
  transport_lane *owner;
  cnet_connection connection;
  cnet_datagram datagram;
  cnet_datagram_peer peer;
  size_t received;
  size_t sends;
  bool connected;
} transport_channel;

struct transport_lane {
  cmeta_thread_t thread;
  native_io_backend backend;
  cnet_client client;
  transport_channel channels[CHANNELS];
  mem_buffer_t *payload;
  const void *token;
  atomic_bool ready;
  atomic_bool *start;
  atomic_bool *abort;
  size_t pairs;
  size_t index;
  size_t rejected;
  bool ipc;
  const char *phase;
  int status;
  uint64_t latency[PAIRS * SAMPLES];
  uint64_t setup_ns;
  uint64_t cpu_ns;
  uint64_t cpu_cycles;
  uint64_t wall_ns;
  uint64_t began_ns;
  uint64_t ended_ns;
  uint64_t drain_ns;
};

static cnet_io_benchmark_backend selected;

static void remember(transport_lane *lane, int status) {
  if (status != SALTS_OK && lane->status == SALTS_OK) lane->status = status;
}
static void receive_bytes(transport_channel *channel, const cnet_receive_view *view) {
  transport_lane *lane = channel->owner;
  if (lane->token != cmeta_thread_current_token() || view->kind != CNET_MESSAGE_BYTES ||
      channel->received > PAYLOAD || view->size > PAYLOAD - channel->received) {
    remember(lane, SALTS_EPROTO);
    return;
  }
  for (size_t i = 0; i < view->size; ++i)
    if (((const unsigned char *)view->data)[i] != 0x5a) remember(lane, SALTS_EPROTO);
  channel->received += view->size;
}
static void received(void *user, cnet_connection connection, const cnet_receive_view *view) {
  transport_channel *channel = user;
  receive_bytes(user, view);
  if (channel->owner->status == SALTS_OK && channel->received < PAYLOAD)
    remember(channel->owner, cnet_receive(&channel->owner->client, connection, 1));
}
static void sent(void *user, cnet_connection connection, size_t size) {
  transport_channel *channel = user;
  (void)connection;
  if (size != PAYLOAD || channel->owner->token != cmeta_thread_current_token())
    remember(channel->owner, SALTS_EPROTO);
  ++channel->sends;
}
static void state(void *user, cnet_connection connection, cnet_connection_state value,
                  const cnet_error *error) {
  transport_channel *channel = user;
  (void)connection;
  if (value == CNET_CONNECTION_CONNECTED) channel->connected = true;
  if (error != NULL || value == CNET_CONNECTION_FAILED)
    remember(channel->owner, error != NULL ? error->status : SALTS_EIO);
}
static void udp_received(void *user, cnet_datagram *datagram, const cnet_datagram_peer *peer,
                         const cnet_receive_view *view) {
  (void)datagram;
  (void)peer;
  /* Datagram views use the datagram message kind; byte validation is shared. */
  cnet_receive_view bytes = *view;
  bytes.kind = CNET_MESSAGE_BYTES;
  receive_bytes(user, &bytes);
}
static void udp_sent(void *user, cnet_datagram *datagram, const cnet_datagram_peer *peer,
                     size_t size, int status, uint64_t tag) {
  transport_channel *channel = user;
  (void)datagram;
  (void)peer;
  (void)tag;
  remember(channel->owner, status);
  sent(user, (cnet_connection){0}, size);
}
static int progress(transport_lane *lane) {
  native_io_completion batch[REQUESTS];
  size_t events, count = 0;
  int first = SALTS_OK, status;
  if (lane->client.impl != NULL) first = cnet_client_advance_external(&lane->client, &events);
  for (size_t i = 0; i < lane->pairs * 2; ++i) {
    if (lane->channels[i].datagram.impl == NULL) continue;
    status = cnet_datagram_advance_external(&lane->channels[i].datagram, &events);
    if (first == SALTS_OK) first = status;
  }
  status = native_io_backend_observe(&lane->backend, batch, REQUESTS, 1, &count);
  if (status != SALTS_OK && status != SALTS_ETIMEDOUT && first == SALTS_OK) first = status;
  for (size_t i = 0; i < count; ++i) {
    bool consumed = false;
    if (lane->client.impl != NULL) {
      status = cnet_client_route_external_completion(&lane->client, &batch[i], &consumed, &events);
      if (first == SALTS_OK) first = status;
    }
    for (size_t j = 0; j < lane->pairs * 2 && !consumed; ++j) {
      if (lane->channels[j].datagram.impl == NULL) continue;
      status = cnet_datagram_route_external_completion(&lane->channels[j].datagram, &batch[i],
                                                       &consumed, &events);
      if (first == SALTS_OK) first = status;
    }
    if (!consumed && first == SALTS_OK) first = SALTS_EPROTO;
  }
  return first;
}
static int setup_ipc_pair(transport_lane *lane, size_t pair) {
  cnet_ipc_listener listener = {0};
  cnet_ipc_accepted child = {0};
  transport_channel *a = &lane->channels[pair * 2], *b = a + 1;
  cnet_observer first = {state, received, a, sent}, second = {state, received, b, sent};
  tstr uri;
  size_t events;
  bool stopped = false, path_owned = false;
  uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
#if defined(_WIN32)
  uri =
      tstr_format("ipc://salts-owner-bench-{}-{}-{}", (unsigned)uv_os_getpid(), lane->index, pair);
#else
  uri = tstr_format("ipc:///tmp/salts-owner-bench-{}-{}-{}", (unsigned)uv_os_getpid(), lane->index,
                    pair);
#endif
  int status = uri == NULL ? SALTS_ENOMEM : SALTS_OK;
  if (status == SALTS_OK) {
    cnet_ipc_listener_config config = {sizeof(config), CNET_IPC_VERSION, selected.kind, uri, 1, 1,
                                       PAYLOAD,        PAYLOAD};
    status = cnet_ipc_listener_init(&listener, &config);
    path_owned = status == SALTS_OK;
  }
  if (status == SALTS_OK) status = cnet_ipc_listener_advance(&listener, 1, &events);
  if (status == SALTS_OK) {
    cnet_connect_options options = {.uri = uri, .observer = first};
    status = cnet_connect(&lane->client, &options, &a->connection);
  }
  while (status == SALTS_OK && (!a->connected || !b->connected)) {
    status = cnet_ipc_listener_advance(&listener, 1, &events);
    if (status == SALTS_OK && b->connection.slot == 0) {
      status = cnet_ipc_listener_accept_detached(&listener, &child);
      if (status == SALTS_OK)
        status = cnet_client_adopt_ipc(&lane->client, &child, &second, &b->connection);
      else if (status == SALTS_ENOENT) status = SALTS_OK;
    }
    if (status == SALTS_OK) status = progress(lane);
    if (status == SALTS_OK) status = lane->status;
    if (cmeta_monotonic_ms() >= deadline) status = SALTS_ETIMEDOUT;
  }
  if (child.kind != CNET_IPC_RESOURCE_NONE) remember(lane, cnet_ipc_accepted_close(&child));
  while (listener.impl != NULL && !stopped) {
    int result = cnet_ipc_listener_stop(&listener, &stopped);
    if (result != SALTS_EBUSY) remember(lane, result);
    if (!stopped) remember(lane, cnet_ipc_listener_advance(&listener, 1, &events));
  }
  remember(lane, cnet_ipc_listener_destroy(&listener));
#if !defined(_WIN32)
  if (path_owned) {
    uv_fs_t request;
    int result = uv_fs_unlink(NULL, &request, uri + strlen("ipc://"), NULL);
    uv_fs_req_cleanup(&request);
    if (result != 0) remember(lane, SALTS_EIO);
  }
#else
  (void)path_owned;
#endif
  tstr_free(uri);
  return status;
}
static int exchange(transport_lane *lane, uint64_t *latencies) {
  uint64_t starts[PAIRS];
  bool done[PAIRS] = {false};
  size_t remaining = lane->pairs;
  uint64_t deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
  for (size_t pair = 0; pair < lane->pairs; ++pair) {
    starts[pair] = cmeta_hrtime();
    for (size_t side = 0; side < 2; ++side) {
      transport_channel *channel = &lane->channels[pair * 2 + side];
      channel->received = channel->sends = 0;
      int status = lane->ipc ? cnet_receive(&lane->client, channel->connection, 1)
                             : cnet_datagram_receive(&channel->datagram, 1);
      if (status == SALTS_OK)
        status = lane->ipc ? cnet_send_buffer(&lane->client, channel->connection, lane->payload)
                           : cnet_datagram_send(&channel->datagram, &channel->peer,
                                                mem_buffer_data(lane->payload), PAYLOAD, 1);
      if (status != SALTS_OK) {
        ++lane->rejected;
        return status;
      }
    }
  }
  while (remaining != 0) {
    int status = progress(lane);
    if (status != SALTS_OK) return status;
    if (lane->status != SALTS_OK) return lane->status;
    for (size_t pair = 0; pair < lane->pairs; ++pair) {
      transport_channel *a = &lane->channels[pair * 2], *b = a + 1;
      if (!done[pair] && a->received == PAYLOAD && b->received == PAYLOAD && a->sends == 1 &&
          b->sends == 1) {
        done[pair] = true;
        --remaining;
        if (latencies != NULL) latencies[pair] = cmeta_hrtime() - starts[pair];
      }
    }
    if (cmeta_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}
static uint64_t cpu_time(const uv_rusage_t *usage) {
  return ((uint64_t)usage->ru_utime.tv_sec + (uint64_t)usage->ru_stime.tv_sec) *
             UINT64_C(1000000000) +
         ((uint64_t)usage->ru_utime.tv_usec + (uint64_t)usage->ru_stime.tv_usec) * 1000;
}
static void run_lane(void *arg) {
  transport_lane *lane = arg;
  uint64_t began = cmeta_hrtime();
  uv_rusage_t before, after;
#if defined(_WIN32)
  ULONG64 cycles_before = 0, cycles_after = 0;
#endif
  native_io_backend_config native = {selected.kind, lane->pairs * 4, REQUESTS, REQUESTS};
  lane->token = cmeta_thread_current_token();
  lane->phase = "setup";
#define REQUIRE(call)                                                                              \
  do {                                                                                             \
    int result = (call);                                                                           \
    if (result != SALTS_OK) {                                                                      \
      remember(lane, result);                                                                      \
      goto drain;                                                                                  \
    }                                                                                              \
  } while (0)
  REQUIRE(native_io_backend_init(&lane->backend, &native));
  lane->payload = mem_get_buffer(mem_global(), PAYLOAD);
  if (lane->payload == NULL) {
    lane->status = SALTS_ENOMEM;
    goto drain;
  }
  memset(mem_buffer_data(lane->payload), 0x5a, PAYLOAD);
  mem_set_used(lane->payload, PAYLOAD);
  for (size_t i = 0; i < lane->pairs * 2; ++i)
    lane->channels[i].owner = lane;
  if (lane->ipc) {
    cnet_client_config config = {.backend = selected.kind,
                                 .connection_capacity = lane->pairs * 2,
                                 .command_capacity = COMMANDS,
                                 .request_capacity = REQUESTS,
                                 .completion_batch_capacity = REQUESTS,
                                 .event_capacity = COMMANDS,
                                 .max_send_bytes = PAYLOAD,
                                 .receive_buffer_bytes = PAYLOAD,
                                 .connect_timeout_ms = TIMEOUT_MS,
                                 .read_timeout_ms = TIMEOUT_MS,
                                 .write_timeout_ms = TIMEOUT_MS};
    REQUIRE(cnet_client_init_external(&lane->client, &config, &lane->backend));
    for (size_t i = 0; i < lane->pairs; ++i)
      REQUIRE(setup_ipc_pair(lane, i));
  } else {
    for (size_t i = 0; i < lane->pairs * 2; ++i) {
      cnet_datagram_config config = CNET_DATAGRAM_CONFIG_INIT;
      config.backend = selected.kind;
      config.host = "127.0.0.1";
      config.send_capacity = 1;
      config.request_capacity = config.completion_batch_capacity = 2;
      config.max_datagram_bytes = config.receive_buffer_bytes = PAYLOAD;
      config.observer = (cnet_datagram_observer){udp_received, udp_sent, &lane->channels[i]};
      REQUIRE(cnet_datagram_init_external(&lane->channels[i].datagram, &config, &lane->backend));
    }
    for (size_t i = 0; i < lane->pairs * 2; ++i) {
      cnet_datagram_peer *peer = &lane->channels[i].peer;
      peer->family = CNET_DATAGRAM_ADDRESS_IPV4;
      peer->address[0] = 127;
      peer->address[3] = 1;
      REQUIRE(cnet_datagram_port(&lane->channels[i ^ 1].datagram, &peer->port));
    }
  }
  lane->setup_ns = cmeta_hrtime() - began;
  lane->phase = "warmup";
  for (size_t i = 0; i < WARMUP; ++i)
    REQUIRE(exchange(lane, NULL));
  atomic_store_explicit(&lane->ready, true, memory_order_release);
  while (!atomic_load_explicit(lane->start, memory_order_acquire)) {
    if (atomic_load_explicit(lane->abort, memory_order_acquire)) goto drain;
    cmeta_thread_yield();
  }
  REQUIRE(uv_getrusage_thread(&before));
#if defined(_WIN32)
  REQUIRE(QueryThreadCycleTime(GetCurrentThread(), &cycles_before) ? SALTS_OK : SALTS_EIO);
#endif
  lane->phase = "steady";
  began = cmeta_hrtime();
  lane->began_ns = began;
  for (size_t i = 0; i < SAMPLES; ++i)
    REQUIRE(exchange(lane, lane->latency + i * lane->pairs));
  lane->ended_ns = cmeta_hrtime();
  lane->wall_ns = lane->ended_ns - began;
  REQUIRE(uv_getrusage_thread(&after));
#if defined(_WIN32)
  REQUIRE(QueryThreadCycleTime(GetCurrentThread(), &cycles_after) ? SALTS_OK : SALTS_EIO);
  lane->cpu_cycles = cycles_after - cycles_before;
#endif
  lane->cpu_ns = cpu_time(&after) - cpu_time(&before);
drain:
  if (lane->status != SALTS_OK) {
    tstr diagnostic = tstr_format("transport={} lane={} phase={} error={} received={} sends={}",
                                  lane->ipc ? "ipc" : "udp", lane->index, lane->phase, lane->status,
                                  lane->channels[0].received, lane->channels[0].sends);
    if (diagnostic != NULL) {
      puts(diagnostic);
      tstr_free(diagnostic);
    }
  }
  if (lane->status != SALTS_OK) atomic_store_explicit(lane->abort, true, memory_order_release);
  began = cmeta_hrtime();
  if (lane->client.impl != NULL)
    for (size_t i = 0; i < lane->pairs * 2; ++i) {
      if (lane->channels[i].connection.slot == 0) continue;
      int status = cnet_close(&lane->client, lane->channels[i].connection);
      if (status != SALTS_ENOENT) remember(lane, status);
    }
  /* Keep callback storage alive until real native terminals drain. CTest owns
   * the outer timeout; it never authorizes freeing borrowed in-flight data. */
  while (lane->backend.impl != NULL) {
    bool stopped = true;
    if (lane->client.impl != NULL) {
      int result = cnet_client_stop_external(&lane->client);
      if (result == SALTS_OK) remember(lane, cnet_client_destroy(&lane->client));
      else {
        stopped = false;
        if (result != SALTS_EBUSY) remember(lane, result);
      }
    }
    for (size_t i = 0; i < lane->pairs * 2; ++i) {
      cnet_datagram *datagram = &lane->channels[i].datagram;
      bool quiet = false;
      if (datagram->impl == NULL) continue;
      int result = cnet_datagram_stop_external(datagram, &quiet);
      if (result != SALTS_EBUSY) remember(lane, result);
      if (quiet) remember(lane, cnet_datagram_destroy(datagram));
      else stopped = false;
    }
    if (stopped) break;
    remember(lane, progress(lane));
  }
  if (lane->payload != NULL) mem_buffer_release(lane->payload);
  if (lane->backend.impl != NULL) {
    remember(lane, native_io_backend_close(&lane->backend));
    remember(lane, native_io_backend_destroy(&lane->backend));
  }
  lane->drain_ns = cmeta_hrtime() - began;
  atomic_store_explicit(&lane->ready, true, memory_order_release);
#undef REQUIRE
}
static int compare_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}
static int measure(bool ipc, size_t owners) {
  transport_lane lanes[PAIRS] = {0};
  uint64_t latency[PAIRS * SAMPLES];
  atomic_bool start = false, abort = false;
  uint64_t wall = 0, cpu = 0, setup = 0, drain = 0;
  uint64_t first_start = UINT64_MAX, last_end = 0;
  uint64_t cycles = 0;
  size_t started = 0, rejected = 0, resident = 0;
  int result = SALTS_OK;
  for (size_t i = 0; i < owners; ++i) {
    lanes[i].pairs = PAIRS / owners;
    lanes[i].ipc = ipc;
    lanes[i].index = i;
    lanes[i].start = &start;
    lanes[i].abort = &abort;
    atomic_init(&lanes[i].ready, false);
    result = cmeta_thread_create(&lanes[i].thread, run_lane, &lanes[i]);
    if (result != SALTS_OK) {
      atomic_store(&abort, true);
      break;
    }
    ++started;
  }
  for (size_t i = 0; i < started && !atomic_load(&abort); ++i)
    while (!atomic_load_explicit(&lanes[i].ready, memory_order_acquire) && !atomic_load(&abort))
      cmeta_thread_yield();
  if (uv_resident_set_memory(&resident) != 0) {
    result = SALTS_EIO;
    atomic_store(&abort, true);
  }
  atomic_store_explicit(&start, true, memory_order_release);
  size_t offset = 0;
  for (size_t i = 0; i < started; ++i) {
    int status = cmeta_thread_join(&lanes[i].thread);
    if (result == SALTS_OK) result = status;
    if (result == SALTS_OK) result = lanes[i].status;
    if (lanes[i].began_ns < first_start) first_start = lanes[i].began_ns;
    if (lanes[i].ended_ns > last_end) last_end = lanes[i].ended_ns;
    if (lanes[i].setup_ns > setup) setup = lanes[i].setup_ns;
    if (lanes[i].drain_ns > drain) drain = lanes[i].drain_ns;
    cpu += lanes[i].cpu_ns;
    cycles += lanes[i].cpu_cycles;
    rejected += lanes[i].rejected;
    memcpy(latency + offset, lanes[i].latency, SAMPLES * lanes[i].pairs * sizeof(uint64_t));
    offset += SAMPLES * lanes[i].pairs;
  }
  if (result != SALTS_OK) return result;
  wall = last_end > first_start ? last_end - first_start : 0;
  if (wall == 0 || offset != PAIRS * SAMPLES) return SALTS_EPROTO;
  qsort(latency, offset, sizeof(*latency), compare_u64);
  /* Report data, not business logging. RSS includes runtime/allocator metadata;
   * the payload budget is a configured bound, not an RSS upper bound. */
  tstr report = tstr_format(
      "transport={} backend={} owners={} pairs={} payload={} messages={} wall_ns={} cpu_ns={} "
      "p95_ns={} p99_ns={} rejected={} lost=0 outstanding_message_bound={} "
      "configured_payload_bytes={} rss_at_ready={} setup_max_ns={} drain_max_ns={}",
      ipc ? "ipc" : "udp", selected.name, owners, PAIRS, PAYLOAD, PAIRS * SAMPLES * 2, wall, cpu,
      latency[(offset * 95 + 99) / 100 - 1], latency[(offset * 99 + 99) / 100 - 1], rejected,
      PAIRS * 2,
      ipc ? (size_t)(owners * COMMANDS * PAYLOAD * 2 + CHANNELS * PAYLOAD)
          : (size_t)(CHANNELS * PAYLOAD * 2),
      resident, setup, drain);
  if (report == NULL) return SALTS_ENOMEM;
  tstr complete = tstr_append_format(report, " cpu_cycles={}", cycles);
  if (complete == NULL) {
    tstr_free(report);
    return SALTS_ENOMEM;
  }
  report = complete;
  puts(report);
  tstr_free(report);
  return SALTS_OK;
}

spec("CNet fixed-budget transport owner benchmark") {
  it("measures independent duplex pairs on 1/2/4 owners with fixed total work") {
    check_equal(cnet_io_benchmark_select_backend(getenv("CNET_IO_BENCHMARK_BACKEND"), &selected),
                SALTS_OK);
    for (size_t mode = 0; mode < 2; ++mode)
      for (size_t owners = 1; owners <= PAIRS; owners *= 2) {
        benchmark_batch("duplex lifecycle (setup, warmup, steady, drain)", 3) {
          check_equal(measure(mode != 0, owners), SALTS_OK);
        }
      }
  }
}
