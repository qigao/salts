#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>

#include "cnet_io_benchmark_config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
  EXTERNAL_PARALLEL_LANES = 2,
  EXTERNAL_PARALLEL_WARMUPS = 8,
  EXTERNAL_PARALLEL_SAMPLES = 64,
  EXTERNAL_PARALLEL_REPEATS = 7,
  EXTERNAL_PARALLEL_PAYLOAD_COUNT = 2,
  EXTERNAL_PARALLEL_TIMEOUT_MS = 5000,
  EXTERNAL_PARALLEL_COMMAND_CAPACITY = 64,
  EXTERNAL_PARALLEL_EVENT_CAPACITY = 128,
  EXTERNAL_PARALLEL_REQUEST_CAPACITY = 64
};

static const size_t EXTERNAL_PARALLEL_PAYLOADS[] = {1024u, 65536u};

typedef enum external_parallel_mode {
  EXTERNAL_PARALLEL_SERIAL = 0,
  EXTERNAL_PARALLEL_PARALLEL = 1,
  EXTERNAL_PARALLEL_MODE_COUNT
} external_parallel_mode;

typedef struct external_parallel_peer {
  int listener;
  int accepted;
  struct sockaddr_in address;
  size_t payload_size;
  size_t cycles;
  unsigned char *scratch;
  pthread_t thread;
  int status;
  bool thread_started;
} external_parallel_peer;

typedef struct external_parallel_lane external_parallel_lane;

typedef struct external_parallel_connection {
  external_parallel_lane *lane;
  cnet_connection handle;
  size_t received_bytes;
  uint64_t started_ns;
  uint64_t *latency_out;
  bool connected;
  bool send_done;
  bool receive_done;
} external_parallel_connection;

struct external_parallel_lane {
  cnet_client client;
  native_io_backend backend;
  external_parallel_connection connection;
  mem_buffer_t *retained_buffer;
  unsigned char *payload;
  size_t payload_size;
  size_t advance_calls;
  size_t observe_calls;
  size_t routed_completions;
  size_t unrelated_completions;
  size_t measured_sends;
  size_t measured_receives;
  int status;
  bool measuring;
  bool terminal;
};

typedef struct external_parallel_gate {
  pthread_mutex_t mutex;
  pthread_cond_t changed;
  size_t ready;
  size_t done;
  bool start;
  int failure;
} external_parallel_gate;

typedef struct external_parallel_thread_arg {
  external_parallel_gate *gate;
  external_parallel_peer *peers[EXTERNAL_PARALLEL_LANES];
  external_parallel_lane lanes[EXTERNAL_PARALLEL_LANES];
  uint64_t latencies[EXTERNAL_PARALLEL_LANES][EXTERNAL_PARALLEL_SAMPLES];
  native_io_backend_kind backend_kind;
  size_t lane_count;
  size_t payload_size;
  int cpu;
  int observed_cpu;
  uint64_t cpu_ns;
  int status;
} external_parallel_thread_arg;

typedef struct external_parallel_sample {
  const char *mode;
  const char *topology;
  const char *backend;
  size_t payload_size;
  size_t repeat;
  size_t logical_operations;
  int cpu_a;
  int cpu_b;
  uint64_t wall_ns;
  uint64_t owner_cpu_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t p99_ns;
  double operations_per_second;
  double mib_per_second;
  double owner_cpu_us_per_op;
  size_t advance_calls;
  size_t observe_calls;
  size_t routed_completions;
  size_t unrelated_completions;
  size_t send_terminals;
  size_t receive_terminals;
} external_parallel_sample;

static int external_parallel_socket_error(void) {
  return errno == 0 ? SALTS_EIO : -errno;
}

static int external_parallel_set_nodelay(int fd) {
  const int enabled = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled,
                    sizeof(enabled)) == 0
             ? SALTS_OK
             : external_parallel_socket_error();
}

static int external_parallel_read_full(
    int fd, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = read(fd, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EOF : external_parallel_socket_error();
  }
  return SALTS_OK;
}

static int external_parallel_write_full(
    int fd, const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = write(fd, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EIO : external_parallel_socket_error();
  }
  return SALTS_OK;
}

static void *external_parallel_peer_entry(void *user) {
  external_parallel_peer *peer = (external_parallel_peer *)user;
  int status = SALTS_OK;

  do {
    peer->accepted = accept(peer->listener, NULL, NULL);
  } while (peer->accepted < 0 && errno == EINTR);
  if (peer->accepted < 0) status = external_parallel_socket_error();
  if (status == SALTS_OK)
    status = external_parallel_set_nodelay(peer->accepted);

  for (size_t cycle = 0u;
       cycle < peer->cycles && status == SALTS_OK; ++cycle) {
    status = external_parallel_read_full(
        peer->accepted, peer->scratch, peer->payload_size);
    if (status == SALTS_OK)
      status = external_parallel_write_full(
          peer->accepted, peer->scratch, peer->payload_size);
  }

  peer->status = status;
  return NULL;
}

static void external_parallel_peer_reset(external_parallel_peer *peer) {
  memset(peer, 0, sizeof(*peer));
  peer->listener = -1;
  peer->accepted = -1;
  peer->status = SALTS_OK;
}

static int external_parallel_peer_init(
    external_parallel_peer *peer, size_t payload_size, size_t cycles) {
  socklen_t address_size = sizeof(peer->address);
  const int reuse = 1;
  int status;

  external_parallel_peer_reset(peer);
  peer->payload_size = payload_size;
  peer->cycles = cycles;
  peer->scratch = (unsigned char *)malloc(payload_size);
  if (peer->scratch == NULL) return SALTS_ENOMEM;

  peer->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (peer->listener < 0) return external_parallel_socket_error();
  (void)setsockopt(peer->listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse));

  memset(&peer->address, 0, sizeof(peer->address));
  peer->address.sin_family = AF_INET;
  peer->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer->address.sin_port = 0u;
  if (bind(peer->listener, (const struct sockaddr *)&peer->address,
           sizeof(peer->address)) != 0)
    return external_parallel_socket_error();
  if (getsockname(peer->listener, (struct sockaddr *)&peer->address,
                  &address_size) != 0)
    return external_parallel_socket_error();
  if (listen(peer->listener, 1) != 0)
    return external_parallel_socket_error();

  status = pthread_create(
      &peer->thread, NULL, external_parallel_peer_entry, peer);
  if (status != 0) return -status;
  peer->thread_started = true;
  return SALTS_OK;
}

static int external_parallel_peer_destroy(
    external_parallel_peer *peer, bool abort_peer) {
  int status = SALTS_OK;

  if (abort_peer && peer->listener >= 0)
    (void)shutdown(peer->listener, SHUT_RDWR);

  if (peer->thread_started) {
    const int join_status = pthread_join(peer->thread, NULL);
    if (join_status != 0) status = -join_status;
    peer->thread_started = false;
    if (status == SALTS_OK && !abort_peer &&
        peer->status != SALTS_OK)
      status = peer->status;
  }

  if (peer->accepted >= 0) {
    (void)close(peer->accepted);
    peer->accepted = -1;
  }
  if (peer->listener >= 0) {
    (void)close(peer->listener);
    peer->listener = -1;
  }
  free(peer->scratch);
  peer->scratch = NULL;
  return status;
}

static void external_parallel_state(
    void *user, cnet_connection connection,
    cnet_connection_state state, const cnet_error *error) {
  external_parallel_connection *entry =
      (external_parallel_connection *)user;
  (void)connection;

  if (state == CNET_CONNECTION_CONNECTED) {
    entry->connected = true;
  } else if (state == CNET_CONNECTION_CLOSED ||
             state == CNET_CONNECTION_FAILED) {
    entry->lane->terminal = true;
    if (state == CNET_CONNECTION_FAILED)
      entry->lane->status =
          error != NULL ? error->status : SALTS_EIO;
  }
}

static void external_parallel_receive(
    void *user, cnet_connection connection,
    const cnet_receive_view *view) {
  external_parallel_connection *entry =
      (external_parallel_connection *)user;
  external_parallel_lane *lane = entry->lane;
  (void)connection;

  if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
      view->size == 0u ||
      view->size > lane->payload_size - entry->received_bytes) {
    lane->status = SALTS_EPROTO;
    return;
  }
  if (memcmp(lane->payload + entry->received_bytes,
             view->data, view->size) != 0) {
    lane->status = SALTS_EIO;
    return;
  }

  entry->received_bytes += view->size;
  if (entry->received_bytes == lane->payload_size) {
    if (entry->latency_out != NULL)
      *entry->latency_out = cmeta_hrtime() - entry->started_ns;
    entry->received_bytes = 0u;
    entry->receive_done = true;
    if (lane->measuring) ++lane->measured_receives;
  }
}

static void external_parallel_sent(
    void *user, cnet_connection connection, size_t size) {
  external_parallel_connection *entry =
      (external_parallel_connection *)user;
  external_parallel_lane *lane = entry->lane;
  (void)connection;

  if (size != lane->payload_size) {
    lane->status = SALTS_EIO;
    return;
  }
  entry->send_done = true;
  if (lane->measuring) ++lane->measured_sends;
}

static int external_parallel_lane_poll(
    external_parallel_lane *lane, uint32_t timeout_ms) {
  native_io_completion completions[EXTERNAL_PARALLEL_REQUEST_CAPACITY];
  size_t count = 0u;
  size_t events = 0u;
  uint32_t wait_ms = timeout_ms;
  int status;

  status = cnet_client_advance_external(&lane->client, &events);
  if (lane->measuring) ++lane->advance_calls;
  if (status != SALTS_OK) return status;

  status = cnet_client_external_timeout(
      &lane->client, timeout_ms, &wait_ms);
  if (status != SALTS_OK) return status;

  status = native_io_backend_observe(
      &lane->backend, completions,
      EXTERNAL_PARALLEL_REQUEST_CAPACITY,
      wait_ms, &count);
  if (lane->measuring) ++lane->observe_calls;
  if (status == SALTS_ETIMEDOUT) {
    status = cnet_client_advance_external(&lane->client, &events);
    if (lane->measuring) ++lane->advance_calls;
    return status;
  }
  if (status != SALTS_OK) return status;

  for (size_t index = 0u; index < count; ++index) {
    bool consumed = false;
    size_t routed_events = 0u;
    status = cnet_client_route_external_completion(
        &lane->client, &completions[index],
        &consumed, &routed_events);
    if (status != SALTS_OK) return status;
    if (lane->measuring) {
      if (consumed) ++lane->routed_completions;
      else ++lane->unrelated_completions;
    }
    if (!consumed) return SALTS_EPROTO;
  }

  /*
   * Do not immediately advance again after a successful observed batch.
   * Routing already settles callbacks inline. Any owner-local work queued by
   * those completions is advanced at the beginning of the next host-loop
   * iteration, before the next wait. Running another advance here double-
   * drives the same iteration and adds a control/deadline scan that ordinary
   * cnet_client_poll() does not perform.
   */
  return SALTS_OK;
}

static int external_parallel_lane_wait_connected(
    external_parallel_lane *lane) {
  const uint64_t deadline =
      cmeta_monotonic_ms() + EXTERNAL_PARALLEL_TIMEOUT_MS;

  while (!lane->connection.connected) {
    int status;
    if (lane->status != SALTS_OK) return lane->status;
    status = external_parallel_lane_poll(lane, 10u);
    if (status != SALTS_OK) return status;
    if (cmeta_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

static int external_parallel_lane_init(
    external_parallel_lane *lane,
    const external_parallel_peer *peer,
    size_t payload_size,
    native_io_backend_kind backend_kind) {
  const size_t cycles =
      EXTERNAL_PARALLEL_WARMUPS + EXTERNAL_PARALLEL_SAMPLES;
  const cnet_client_config config = {
      .backend = backend_kind,
      .connection_capacity = 1u,
      .command_capacity = EXTERNAL_PARALLEL_COMMAND_CAPACITY,
      .request_capacity = EXTERNAL_PARALLEL_REQUEST_CAPACITY,
      .completion_batch_capacity = EXTERNAL_PARALLEL_REQUEST_CAPACITY,
      .event_capacity = EXTERNAL_PARALLEL_EVENT_CAPACITY,
      .max_send_bytes = payload_size,
      .receive_buffer_bytes = payload_size,
      .connect_timeout_ms = EXTERNAL_PARALLEL_TIMEOUT_MS,
      .read_timeout_ms = 0u,
      .write_timeout_ms = 0u};
  cnet_stream_socket_options socket_options =
      (cnet_stream_socket_options)CNET_STREAM_SOCKET_OPTIONS_INIT;
  cnet_connect_options options;
  char uri[64];
  int status;

  memset(lane, 0, sizeof(*lane));
  lane->payload_size = payload_size;
  lane->status = SALTS_OK;
  lane->connection.lane = lane;

  lane->payload = (unsigned char *)malloc(payload_size);
  if (lane->payload == NULL) return SALTS_ENOMEM;
  memset(lane->payload, 0x5a, payload_size);
  lane->retained_buffer =
      mem_wrap_external(lane->payload, payload_size, NULL, NULL);
  if (lane->retained_buffer == NULL) return SALTS_ENOMEM;

  {
    const native_io_backend_config backend_config = {
        backend_kind,
        config.connection_capacity * 2u,
        config.request_capacity,
        config.completion_batch_capacity};
    status = native_io_backend_init(&lane->backend, &backend_config);
    if (status != SALTS_OK) return status;
  }

  status = cnet_client_init_external(
      &lane->client, &config, &lane->backend);
  if (status != SALTS_OK) return status;

  socket_options.nodelay = 1;
  status = cnet_client_set_stream_socket_options(
      &lane->client, &socket_options);
  if (status != SALTS_OK) return status;

  if (snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
               (unsigned)ntohs(peer->address.sin_port)) < 0)
    return SALTS_EIO;

  options = (cnet_connect_options){
      .uri = uri,
      .observer = {
          .on_state = external_parallel_state,
          .on_receive = external_parallel_receive,
          .user = &lane->connection,
          .on_send = external_parallel_sent}};

  status = cnet_connect(
      &lane->client, &options, &lane->connection.handle);
  if (status != SALTS_OK) return status;
  status = external_parallel_lane_wait_connected(lane);
  if (status != SALTS_OK) return status;

  if (payload_size > SIZE_MAX / cycles) return SALTS_ERANGE;
  return cnet_receive(
      &lane->client, lane->connection.handle,
      payload_size * cycles);
}

static int external_parallel_lane_cycle(
    external_parallel_lane *lane, uint64_t *latency_out) {
  const uint64_t deadline =
      cmeta_monotonic_ms() + EXTERNAL_PARALLEL_TIMEOUT_MS;
  external_parallel_connection *entry = &lane->connection;
  int status;

  entry->received_bytes = 0u;
  entry->send_done = false;
  entry->receive_done = false;
  entry->latency_out = latency_out;
  entry->started_ns = cmeta_hrtime();
  if (latency_out != NULL) *latency_out = 0u;

  status = cnet_send_buffer(
      &lane->client, entry->handle, lane->retained_buffer);
  if (status != SALTS_OK) return status;

  while (!entry->send_done || !entry->receive_done) {
    if (lane->status != SALTS_OK) return lane->status;
    status = external_parallel_lane_poll(lane, 10u);
    if (status != SALTS_OK) return status;
    if (cmeta_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }

  if (latency_out != NULL && *latency_out == 0u)
    return SALTS_EPROTO;
  entry->latency_out = NULL;
  return lane->status;
}

static int external_parallel_lane_warmup(external_parallel_lane *lane) {
  for (size_t sample = 0u;
       sample < EXTERNAL_PARALLEL_WARMUPS; ++sample) {
    const int status = external_parallel_lane_cycle(lane, NULL);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int external_parallel_lane_measure(
    external_parallel_lane *lane,
    uint64_t latencies[EXTERNAL_PARALLEL_SAMPLES]) {
  lane->measuring = true;
  lane->advance_calls = 0u;
  lane->observe_calls = 0u;
  lane->routed_completions = 0u;
  lane->unrelated_completions = 0u;
  lane->measured_sends = 0u;
  lane->measured_receives = 0u;

  for (size_t sample = 0u;
       sample < EXTERNAL_PARALLEL_SAMPLES; ++sample) {
    const int status =
        external_parallel_lane_cycle(lane, &latencies[sample]);
    if (status != SALTS_OK) {
      lane->measuring = false;
      return status;
    }
  }

  lane->measuring = false;
  if (lane->measured_sends != EXTERNAL_PARALLEL_SAMPLES ||
      lane->measured_receives != EXTERNAL_PARALLEL_SAMPLES)
    return SALTS_EPROTO;
  return SALTS_OK;
}

static int external_parallel_lane_destroy(
    external_parallel_lane *lane) {
  int status = SALTS_OK;

  if (lane->client.impl != NULL) {
    const uint64_t deadline =
        cmeta_monotonic_ms() + EXTERNAL_PARALLEL_TIMEOUT_MS;

    if (lane->connection.connected && !lane->terminal) {
      int close_status = cnet_close(
          &lane->client, lane->connection.handle);
      if (close_status != SALTS_OK &&
          close_status != SALTS_EALREADY)
        status = close_status;

      while (status == SALTS_OK && !lane->terminal) {
        status = external_parallel_lane_poll(lane, 10u);
        if (status == SALTS_OK &&
            cmeta_monotonic_ms() >= deadline)
          status = SALTS_ETIMEDOUT;
      }
    }

    if (status == SALTS_OK) {
      int stop_status = cnet_client_stop_external(&lane->client);
      while (stop_status == SALTS_EBUSY &&
             cmeta_monotonic_ms() < deadline) {
        const int progress_status =
            external_parallel_lane_poll(lane, 10u);
        if (progress_status != SALTS_OK) {
          status = progress_status;
          break;
        }
        stop_status = cnet_client_stop_external(&lane->client);
      }
      if (status == SALTS_OK && stop_status != SALTS_OK)
        status = stop_status;
    }

    {
      const int destroy_status =
          cnet_client_destroy(&lane->client);
      if (status == SALTS_OK &&
          destroy_status != SALTS_OK)
        status = destroy_status;
    }
  }

  if (lane->backend.impl != NULL) {
    const int close_status =
        native_io_backend_close(&lane->backend);
    if (status == SALTS_OK &&
        close_status != SALTS_OK &&
        close_status != SALTS_EALREADY)
      status = close_status;
    {
      const int destroy_status =
          native_io_backend_destroy(&lane->backend);
      if (status == SALTS_OK &&
          destroy_status != SALTS_OK)
        status = destroy_status;
    }
  }

  if (lane->retained_buffer != NULL) {
    if (status == SALTS_OK &&
        mem_buffer_ref_count(lane->retained_buffer) != 1u)
      status = SALTS_EPROTO;
    mem_buffer_release(lane->retained_buffer);
    lane->retained_buffer = NULL;
  }
  free(lane->payload);
  lane->payload = NULL;
  return status;
}

static int external_parallel_set_affinity(int cpu) {
  cpu_set_t set;
  int status;

  if (cpu < 0 || cpu >= CPU_SETSIZE) return SALTS_EINVAL;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  status = pthread_setaffinity_np(
      pthread_self(), sizeof(set), &set);
  return status == 0 ? SALTS_OK : -status;
}

static uint64_t external_parallel_thread_cpu_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
    return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
}

static int external_parallel_gate_init(external_parallel_gate *gate) {
  int status;
  memset(gate, 0, sizeof(*gate));
  gate->failure = SALTS_OK;
  status = pthread_mutex_init(&gate->mutex, NULL);
  if (status != 0) return -status;
  status = pthread_cond_init(&gate->changed, NULL);
  if (status != 0) {
    (void)pthread_mutex_destroy(&gate->mutex);
    return -status;
  }
  return SALTS_OK;
}

static void external_parallel_gate_destroy(external_parallel_gate *gate) {
  (void)pthread_cond_destroy(&gate->changed);
  (void)pthread_mutex_destroy(&gate->mutex);
}

static void external_parallel_gate_fail(
    external_parallel_gate *gate, int status) {
  (void)pthread_mutex_lock(&gate->mutex);
  if (gate->failure == SALTS_OK)
    gate->failure = status != SALTS_OK ? status : SALTS_EIO;
  (void)pthread_cond_broadcast(&gate->changed);
  (void)pthread_mutex_unlock(&gate->mutex);
}

static int external_parallel_gate_ready_and_wait(
    external_parallel_gate *gate) {
  int failure;

  (void)pthread_mutex_lock(&gate->mutex);
  ++gate->ready;
  (void)pthread_cond_broadcast(&gate->changed);
  while (!gate->start && gate->failure == SALTS_OK)
    (void)pthread_cond_wait(&gate->changed, &gate->mutex);
  failure = gate->failure;
  (void)pthread_mutex_unlock(&gate->mutex);
  return failure;
}

static void external_parallel_gate_done(
    external_parallel_gate *gate, int status) {
  (void)pthread_mutex_lock(&gate->mutex);
  if (status != SALTS_OK && gate->failure == SALTS_OK)
    gate->failure = status;
  ++gate->done;
  (void)pthread_cond_broadcast(&gate->changed);
  (void)pthread_mutex_unlock(&gate->mutex);
}

static void *external_parallel_owner_entry(void *user) {
  external_parallel_thread_arg *arg =
      (external_parallel_thread_arg *)user;
  size_t initialized = 0u;
  int status = external_parallel_set_affinity(arg->cpu);

  arg->status = status;
  arg->observed_cpu = -1;
  if (status != SALTS_OK) {
    external_parallel_gate_fail(arg->gate, status);
    return NULL;
  }

  arg->observed_cpu = sched_getcpu();
  if (arg->observed_cpu != arg->cpu) {
    arg->status = SALTS_EPROTO;
    external_parallel_gate_fail(arg->gate, arg->status);
    return NULL;
  }

  for (size_t lane = 0u;
       lane < arg->lane_count && status == SALTS_OK; ++lane) {
    status = external_parallel_lane_init(
        &arg->lanes[lane], arg->peers[lane],
        arg->payload_size, arg->backend_kind);
    ++initialized;
    if (status == SALTS_OK)
      status = external_parallel_lane_warmup(&arg->lanes[lane]);
  }

  if (status != SALTS_OK) {
    arg->status = status;
    external_parallel_gate_fail(arg->gate, status);
    goto cleanup;
  }

  status = external_parallel_gate_ready_and_wait(arg->gate);
  if (status != SALTS_OK) {
    arg->status = status;
    goto cleanup;
  }

  {
    const uint64_t cpu_started = external_parallel_thread_cpu_ns();
    for (size_t lane = 0u;
         lane < arg->lane_count && status == SALTS_OK; ++lane) {
      status = external_parallel_lane_measure(
          &arg->lanes[lane], arg->latencies[lane]);
    }
    arg->cpu_ns = external_parallel_thread_cpu_ns() - cpu_started;
  }

  arg->status = status;
  external_parallel_gate_done(arg->gate, status);

cleanup:
  for (size_t lane = 0u; lane < initialized; ++lane) {
    const int destroy_status =
        external_parallel_lane_destroy(&arg->lanes[lane]);
    if (arg->status == SALTS_OK &&
        destroy_status != SALTS_OK)
      arg->status = destroy_status;
  }
  return NULL;
}

static int external_parallel_u64_compare(
    const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t external_parallel_percentile(
    uint64_t *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), external_parallel_u64_compare);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int external_parallel_run_mode(
    external_parallel_mode mode,
    native_io_backend_kind backend_kind,
    const char *backend_name,
    const char *topology,
    size_t payload_size,
    size_t repeat,
    int cpu_a,
    int cpu_b,
    external_parallel_sample *out) {
  external_parallel_peer peers[EXTERNAL_PARALLEL_LANES];
  external_parallel_gate gate;
  external_parallel_thread_arg args[EXTERNAL_PARALLEL_LANES];
  pthread_t threads[EXTERNAL_PARALLEL_LANES];
  bool thread_started[EXTERNAL_PARALLEL_LANES] = {false, false};
  uint64_t latencies[EXTERNAL_PARALLEL_LANES * EXTERNAL_PARALLEL_SAMPLES];
  const size_t cycles =
      EXTERNAL_PARALLEL_WARMUPS + EXTERNAL_PARALLEL_SAMPLES;
  const size_t thread_count =
      mode == EXTERNAL_PARALLEL_SERIAL ? 1u : 2u;
  uint64_t wall_started = 0u;
  uint64_t wall_ns = 0u;
  uint64_t owner_cpu_ns = 0u;
  size_t latency_count = 0u;
  size_t advance_calls = 0u;
  size_t observe_calls = 0u;
  size_t routed_completions = 0u;
  size_t unrelated_completions = 0u;
  size_t send_terminals = 0u;
  size_t receive_terminals = 0u;
  int status = SALTS_OK;

  if (out == NULL) return SALTS_EINVAL;
  memset(out, 0, sizeof(*out));
  memset(args, 0, sizeof(args));
  for (size_t lane = 0u; lane < EXTERNAL_PARALLEL_LANES; ++lane)
    external_parallel_peer_reset(&peers[lane]);

  status = external_parallel_gate_init(&gate);
  if (status != SALTS_OK) return status;

  for (size_t lane = 0u;
       lane < EXTERNAL_PARALLEL_LANES && status == SALTS_OK; ++lane)
    status = external_parallel_peer_init(
        &peers[lane], payload_size, cycles);
  if (status != SALTS_OK) goto cleanup;

  if (mode == EXTERNAL_PARALLEL_SERIAL) {
    args[0].gate = &gate;
    args[0].peers[0] = &peers[0];
    args[0].peers[1] = &peers[1];
    args[0].backend_kind = backend_kind;
    args[0].lane_count = 2u;
    args[0].payload_size = payload_size;
    args[0].cpu = cpu_a;
  } else {
    for (size_t lane = 0u; lane < EXTERNAL_PARALLEL_LANES; ++lane) {
      args[lane].gate = &gate;
      args[lane].peers[0] = &peers[lane];
      args[lane].backend_kind = backend_kind;
      args[lane].lane_count = 1u;
      args[lane].payload_size = payload_size;
      args[lane].cpu = lane == 0u ? cpu_a : cpu_b;
    }
  }

  for (size_t index = 0u;
       index < thread_count && status == SALTS_OK; ++index) {
    const int create_status = pthread_create(
        &threads[index], NULL,
        external_parallel_owner_entry, &args[index]);
    if (create_status != 0) {
      status = -create_status;
      break;
    }
    thread_started[index] = true;
  }
  if (status != SALTS_OK) {
    external_parallel_gate_fail(&gate, status);
    goto join_threads;
  }

  (void)pthread_mutex_lock(&gate.mutex);
  while (gate.ready < thread_count &&
         gate.failure == SALTS_OK)
    (void)pthread_cond_wait(&gate.changed, &gate.mutex);
  if (gate.failure != SALTS_OK) {
    status = gate.failure;
    gate.start = true;
    (void)pthread_cond_broadcast(&gate.changed);
    (void)pthread_mutex_unlock(&gate.mutex);
    goto join_threads;
  }

  wall_started = cmeta_hrtime();
  gate.start = true;
  (void)pthread_cond_broadcast(&gate.changed);
  while (gate.done < thread_count &&
         gate.failure == SALTS_OK)
    (void)pthread_cond_wait(&gate.changed, &gate.mutex);
  while (gate.done < thread_count)
    (void)pthread_cond_wait(&gate.changed, &gate.mutex);
  wall_ns = cmeta_hrtime() - wall_started;
  if (gate.failure != SALTS_OK) status = gate.failure;
  (void)pthread_mutex_unlock(&gate.mutex);

join_threads:
  for (size_t index = 0u; index < thread_count; ++index) {
    if (thread_started[index]) {
      const int join_status = pthread_join(threads[index], NULL);
      if (status == SALTS_OK && join_status != 0)
        status = -join_status;
    }
    if (status == SALTS_OK &&
        args[index].status != SALTS_OK)
      status = args[index].status;
  }

  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u; index < thread_count; ++index) {
    owner_cpu_ns += args[index].cpu_ns;
    for (size_t lane = 0u; lane < args[index].lane_count; ++lane) {
      for (size_t sample = 0u;
           sample < EXTERNAL_PARALLEL_SAMPLES; ++sample)
        latencies[latency_count++] = args[index].latencies[lane][sample];
      advance_calls += args[index].lanes[lane].advance_calls;
      observe_calls += args[index].lanes[lane].observe_calls;
      routed_completions += args[index].lanes[lane].routed_completions;
      unrelated_completions += args[index].lanes[lane].unrelated_completions;
      send_terminals += args[index].lanes[lane].measured_sends;
      receive_terminals += args[index].lanes[lane].measured_receives;
    }
  }

  if (latency_count != EXTERNAL_PARALLEL_LANES * EXTERNAL_PARALLEL_SAMPLES ||
      send_terminals != latency_count ||
      receive_terminals != latency_count ||
      wall_ns == 0u || owner_cpu_ns == 0u ||
      unrelated_completions != 0u ||
      routed_completions == 0u) {
    status = SALTS_EPROTO;
    goto cleanup;
  }

  out->mode =
      mode == EXTERNAL_PARALLEL_SERIAL
          ? "one_owner_serial"
          : "two_external_parallel";
  out->topology = topology;
  out->backend = backend_name;
  out->payload_size = payload_size;
  out->repeat = repeat;
  out->logical_operations = latency_count;
  out->cpu_a = cpu_a;
  out->cpu_b = mode == EXTERNAL_PARALLEL_SERIAL ? cpu_a : cpu_b;
  out->wall_ns = wall_ns;
  out->owner_cpu_ns = owner_cpu_ns;
  out->p50_ns =
      external_parallel_percentile(latencies, latency_count, 50u);
  out->p95_ns =
      external_parallel_percentile(latencies, latency_count, 95u);
  out->p99_ns =
      external_parallel_percentile(latencies, latency_count, 99u);
  out->operations_per_second =
      (double)latency_count * 1.0e9 / (double)wall_ns;
  out->mib_per_second =
      ((double)latency_count * (double)payload_size /
       (1024.0 * 1024.0)) *
      1.0e9 / (double)wall_ns;
  out->owner_cpu_us_per_op =
      (double)owner_cpu_ns / 1000.0 / (double)latency_count;
  out->advance_calls = advance_calls;
  out->observe_calls = observe_calls;
  out->routed_completions = routed_completions;
  out->unrelated_completions = unrelated_completions;
  out->send_terminals = send_terminals;
  out->receive_terminals = receive_terminals;

cleanup:
  for (size_t lane = 0u; lane < EXTERNAL_PARALLEL_LANES; ++lane) {
    const int peer_status =
        external_parallel_peer_destroy(&peers[lane], status != SALTS_OK);
    if (status == SALTS_OK && peer_status != SALTS_OK)
      status = peer_status;
  }
  external_parallel_gate_destroy(&gate);
  return status;
}

static int external_parallel_parse_cpu(
    const char *name, int *out_cpu) {
  const char *value = getenv(name);
  char *end = NULL;
  long parsed;

  if (out_cpu == NULL || value == NULL || *value == '\0')
    return SALTS_EINVAL;
  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < 0 || parsed >= CPU_SETSIZE)
    return SALTS_EINVAL;
  *out_cpu = (int)parsed;
  return SALTS_OK;
}

static int external_parallel_double_compare(
    const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double external_parallel_double_median(
    double *values, size_t count) {
  qsort(values, count, sizeof(*values), external_parallel_double_compare);
  return values[count / 2u];
}

static FILE *external_parallel_open_csv(void) {
  const char *prefix = getenv("CNET_EXTERNAL_PARALLEL_OUTPUT");
  char path[1024];

  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0)
    return NULL;
  return fopen(path, "w");
}

static int external_parallel_write_csv(
    FILE *csv, const external_parallel_sample *sample) {
  if (csv == NULL || sample == NULL) return SALTS_OK;
  return fprintf(
             csv,
             "%s,%s,%s,%zu,%zu,%zu,%d,%d,%" PRIu64 ",%" PRIu64
             ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
             ",%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%zu,%zu\n",
             sample->backend, sample->topology, sample->mode,
             sample->payload_size, sample->repeat,
             sample->logical_operations, sample->cpu_a, sample->cpu_b,
             sample->wall_ns, sample->owner_cpu_ns,
             sample->p50_ns, sample->p95_ns, sample->p99_ns,
             sample->operations_per_second,
             sample->mib_per_second,
             sample->owner_cpu_us_per_op,
             sample->advance_calls,
             sample->observe_calls,
             sample->routed_completions,
             sample->unrelated_completions,
             sample->send_terminals,
             sample->receive_terminals) < 0
             ? SALTS_EIO
             : SALTS_OK;
}

int main(void) {
  cnet_io_benchmark_backend backend = {0};
  const char *requested_backend =
      getenv("CNET_IO_BENCHMARK_BACKEND");
  const char *topology =
      getenv("CNET_EXTERNAL_PARALLEL_TOPOLOGY");
  external_parallel_sample
      results[EXTERNAL_PARALLEL_PAYLOAD_COUNT]
             [EXTERNAL_PARALLEL_MODE_COUNT]
             [EXTERNAL_PARALLEL_REPEATS];
  FILE *csv = NULL;
  int cpu_a = -1;
  int cpu_b = -1;
  int status;

  if (topology == NULL || *topology == '\0')
    topology = "unspecified";

  status = cnet_io_benchmark_select_backend(
      requested_backend, &backend);
  if (status != SALTS_OK) {
    fprintf(stderr, "backend selection failed: status=%d\n", status);
    return 2;
  }
  if (backend.kind != NATIVE_IO_BACKEND_EPOLL &&
      backend.kind != NATIVE_IO_BACKEND_IO_URING) {
    fprintf(stderr,
            "CNet external external owner-parallel benchmark supports Linux epoll/io_uring only\n");
    return 2;
  }
  status = external_parallel_parse_cpu(
      "CNET_EXTERNAL_PARALLEL_CPU_A", &cpu_a);
  if (status == SALTS_OK)
    status = external_parallel_parse_cpu(
        "CNET_EXTERNAL_PARALLEL_CPU_B", &cpu_b);
  if (status != SALTS_OK) {
    fprintf(stderr, "invalid owner CPU affinity environment\n");
    return 2;
  }

  memset(results, 0, sizeof(results));
  for (size_t payload = 0u;
       payload < EXTERNAL_PARALLEL_PAYLOAD_COUNT; ++payload) {
    for (size_t repeat = 0u;
         repeat < EXTERNAL_PARALLEL_REPEATS; ++repeat) {
      for (size_t offset = 0u;
           offset < EXTERNAL_PARALLEL_MODE_COUNT; ++offset) {
        const external_parallel_mode mode =
            (external_parallel_mode)(
                (payload + repeat + offset) %
                EXTERNAL_PARALLEL_MODE_COUNT);
        status = external_parallel_run_mode(
            mode, backend.kind, backend.name, topology,
            EXTERNAL_PARALLEL_PAYLOADS[payload],
            repeat + 1u, cpu_a, cpu_b,
            &results[payload][mode][repeat]);
        if (status != SALTS_OK) {
          fprintf(stderr,
                  "CNet external external owner-parallel benchmark failed: backend=%s topology=%s payload=%zu repeat=%zu mode=%u status=%d\n",
                  backend.name, topology,
                  EXTERNAL_PARALLEL_PAYLOADS[payload],
                  repeat + 1u, (unsigned)mode, status);
          return 1;
        }
      }
    }
  }

  csv = external_parallel_open_csv();
  if (csv != NULL) {
    fprintf(
        csv,
        "backend,topology,mode,payload_bytes,repeat,logical_operations,"
        "cpu_a,cpu_b,wall_ns,owner_cpu_ns,p50_ns,p95_ns,p99_ns,"
        "operations_per_second,mib_per_second,owner_cpu_us_per_op,"
        "advance_calls,observe_calls,routed_completions,"
        "unrelated_completions,send_terminals,receive_terminals\n");
  }

  printf("# CNet external-progress owner-affine retained TCP scaling\n\n");
  printf("Backend: %s\n\n", backend.name);
  printf("Topology: %s; owner CPU A=%d; owner CPU B=%d.\n\n",
         topology, cpu_a, cpu_b);
  printf("Each repeat executes equal total work: two independent external-progress CNet clients, "
         "two loopback TCP echo lanes, retained cnet_send_buffer(), explicit "
         "receive demand, and %u measured logical round trips per lane. "
         "Each owner thread owns its NativeIO backend and drives advance -> observe -> route. Serial mode drives both clients on one pinned owner thread; parallel "
         "mode drives one client per explicitly pinned owner thread. Echo-peer "
         "threads are not affinity-pinned.\n\n",
         (unsigned)EXTERNAL_PARALLEL_SAMPLES);
  printf("| payload | mode | ops/s median | MiB/s median | p50 us median | "
         "p99 us median | owner CPU us/op median | advance calls median |\n");
  printf("| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n");

  for (size_t payload = 0u;
       payload < EXTERNAL_PARALLEL_PAYLOAD_COUNT; ++payload) {
    double rate[EXTERNAL_PARALLEL_MODE_COUNT][EXTERNAL_PARALLEL_REPEATS];
    double mib[EXTERNAL_PARALLEL_MODE_COUNT][EXTERNAL_PARALLEL_REPEATS];
    double p50[EXTERNAL_PARALLEL_MODE_COUNT][EXTERNAL_PARALLEL_REPEATS];
    double p99[EXTERNAL_PARALLEL_MODE_COUNT][EXTERNAL_PARALLEL_REPEATS];
    double cpu[EXTERNAL_PARALLEL_MODE_COUNT][EXTERNAL_PARALLEL_REPEATS];
    double advances[EXTERNAL_PARALLEL_MODE_COUNT][EXTERNAL_PARALLEL_REPEATS];
    double speedup[EXTERNAL_PARALLEL_REPEATS];

    for (size_t mode = 0u;
         mode < EXTERNAL_PARALLEL_MODE_COUNT; ++mode) {
      for (size_t repeat = 0u;
           repeat < EXTERNAL_PARALLEL_REPEATS; ++repeat) {
        const external_parallel_sample *sample =
            &results[payload][mode][repeat];
        rate[mode][repeat] = sample->operations_per_second;
        mib[mode][repeat] = sample->mib_per_second;
        p50[mode][repeat] = (double)sample->p50_ns / 1000.0;
        p99[mode][repeat] = (double)sample->p99_ns / 1000.0;
        cpu[mode][repeat] = sample->owner_cpu_us_per_op;
        advances[mode][repeat] = (double)sample->advance_calls;
        status = external_parallel_write_csv(csv, sample);
        if (status != SALTS_OK) goto cleanup;
      }
      printf("| %zu | %s | %.0f | %.3f | %.3f | %.3f | %.3f | %.0f |\n",
             EXTERNAL_PARALLEL_PAYLOADS[payload],
             mode == EXTERNAL_PARALLEL_SERIAL
                 ? "one_owner_serial"
                 : "two_external_parallel",
             external_parallel_double_median(
                 rate[mode], EXTERNAL_PARALLEL_REPEATS),
             external_parallel_double_median(
                 mib[mode], EXTERNAL_PARALLEL_REPEATS),
             external_parallel_double_median(
                 p50[mode], EXTERNAL_PARALLEL_REPEATS),
             external_parallel_double_median(
                 p99[mode], EXTERNAL_PARALLEL_REPEATS),
             external_parallel_double_median(
                 cpu[mode], EXTERNAL_PARALLEL_REPEATS),
             external_parallel_double_median(
                 advances[mode], EXTERNAL_PARALLEL_REPEATS));
    }

    for (size_t repeat = 0u;
         repeat < EXTERNAL_PARALLEL_REPEATS; ++repeat) {
      speedup[repeat] =
          results[payload][EXTERNAL_PARALLEL_PARALLEL][repeat]
              .operations_per_second /
          results[payload][EXTERNAL_PARALLEL_SERIAL][repeat]
              .operations_per_second;
    }
    printf("\nPayload %zu parallel speedup median: %.3fx\n\n",
           EXTERNAL_PARALLEL_PAYLOADS[payload],
           external_parallel_double_median(
               speedup, EXTERNAL_PARALLEL_REPEATS));
  }

cleanup:
  if (csv != NULL && fclose(csv) != 0 && status == SALTS_OK)
    status = SALTS_EIO;
  return status == SALTS_OK ? 0 : 1;
}
