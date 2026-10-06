#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "cnet_module.h"
#include "cnet_shards.h"
#include "cnet_transport.h"

#include "cnet_io_benchmark_config.h"

#include <salts/clock.h>
#include <salts/error_codes.h>

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
  SHARDS_PARALLEL_LANES = 2,
  SHARDS_PARALLEL_WARMUPS = 8,
  SHARDS_PARALLEL_SAMPLES = 64,
  SHARDS_PARALLEL_REPEATS = 7,
  SHARDS_PARALLEL_PAYLOAD_COUNT = 2,
  SHARDS_PARALLEL_TIMEOUT_MS = 5000,
  SHARDS_PARALLEL_COMMAND_CAPACITY = 32,
  SHARDS_PARALLEL_EVENT_CAPACITY = 128,
  SHARDS_PARALLEL_REQUEST_CAPACITY = 32
};

static const size_t SHARDS_PARALLEL_PAYLOADS[] = {1024u, 65536u};

typedef struct shards_parallel_peer {
  int listener;
  int accepted;
  struct sockaddr_in address;
  size_t payload_size;
  size_t cycles;
  unsigned char *scratch;
  pthread_t thread;
  int status;
  bool thread_started;
} shards_parallel_peer;

typedef struct shards_parallel_gate {
  pthread_mutex_t mutex;
  pthread_cond_t changed;
  size_t ready;
  size_t done;
  bool start;
  int failure;
} shards_parallel_gate;

typedef struct shards_parallel_lane {
  cnet_shards *shards;
  cnet_shard_connection connection;
  mem_buffer_t *buffer;
  unsigned char *payload;
  size_t payload_size;
  size_t receive_offset;
  size_t poll_calls;
  size_t measured_sends;
  size_t measured_receives;
  uint64_t started_ns;
  uint64_t *latency_out;
  int status;
  bool connected;
  bool send_done;
  bool receive_done;
  bool terminal;
} shards_parallel_lane;

typedef struct shards_parallel_thread_arg {
  shards_parallel_gate *gate;
  shards_parallel_lane *lane;
  uint64_t latencies[SHARDS_PARALLEL_SAMPLES];
  int cpu;
  int observed_cpu;
  uint64_t cpu_ns;
  int status;
} shards_parallel_thread_arg;

typedef struct shards_parallel_sample {
  const char *backend;
  const char *topology;
  const char *mode;
  size_t payload_size;
  size_t repeat;
  size_t logical_operations;
  int cpu_a;
  int cpu_b;
  uint32_t shard_a;
  uint32_t shard_b;
  uint64_t wall_ns;
  uint64_t owner_cpu_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t p99_ns;
  double operations_per_second;
  double mib_per_second;
  double owner_cpu_us_per_op;
  size_t poll_calls;
  size_t send_terminals;
  size_t receive_terminals;
} shards_parallel_sample;

static int shards_parallel_socket_error(void) {
  return errno == 0 ? SALTS_EIO : -errno;
}

static int shards_parallel_set_nodelay(int fd) {
  const int enabled = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled,
                    sizeof(enabled)) == 0
             ? SALTS_OK
             : shards_parallel_socket_error();
}

static int shards_parallel_read_full(
    int fd, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = read(fd, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EOF : shards_parallel_socket_error();
  }
  return SALTS_OK;
}

static int shards_parallel_write_full(
    int fd, const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = write(fd, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EIO : shards_parallel_socket_error();
  }
  return SALTS_OK;
}

static void *shards_parallel_peer_entry(void *user) {
  shards_parallel_peer *peer = (shards_parallel_peer *)user;
  int status = SALTS_OK;

  do {
    peer->accepted = accept(peer->listener, NULL, NULL);
  } while (peer->accepted < 0 && errno == EINTR);
  if (peer->accepted < 0) status = shards_parallel_socket_error();
  if (status == SALTS_OK)
    status = shards_parallel_set_nodelay(peer->accepted);

  for (size_t cycle = 0u;
       cycle < peer->cycles && status == SALTS_OK; ++cycle) {
    status = shards_parallel_read_full(
        peer->accepted, peer->scratch, peer->payload_size);
    if (status == SALTS_OK)
      status = shards_parallel_write_full(
          peer->accepted, peer->scratch, peer->payload_size);
  }

  peer->status = status;
  return NULL;
}

static void shards_parallel_peer_reset(shards_parallel_peer *peer) {
  memset(peer, 0, sizeof(*peer));
  peer->listener = -1;
  peer->accepted = -1;
  peer->status = SALTS_OK;
}

static int shards_parallel_peer_init(
    shards_parallel_peer *peer, size_t payload_size, size_t cycles) {
  socklen_t address_size = sizeof(peer->address);
  const int reuse = 1;
  int status;

  shards_parallel_peer_reset(peer);
  peer->payload_size = payload_size;
  peer->cycles = cycles;
  peer->scratch = (unsigned char *)malloc(payload_size);
  if (peer->scratch == NULL) return SALTS_ENOMEM;

  peer->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (peer->listener < 0) return shards_parallel_socket_error();
  (void)setsockopt(peer->listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse));

  memset(&peer->address, 0, sizeof(peer->address));
  peer->address.sin_family = AF_INET;
  peer->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer->address.sin_port = 0u;
  if (bind(peer->listener, (const struct sockaddr *)&peer->address,
           sizeof(peer->address)) != 0)
    return shards_parallel_socket_error();
  if (getsockname(peer->listener, (struct sockaddr *)&peer->address,
                  &address_size) != 0)
    return shards_parallel_socket_error();
  if (listen(peer->listener, 1) != 0)
    return shards_parallel_socket_error();

  status = pthread_create(
      &peer->thread, NULL, shards_parallel_peer_entry, peer);
  if (status != 0) return -status;
  peer->thread_started = true;
  return SALTS_OK;
}

static int shards_parallel_peer_destroy(
    shards_parallel_peer *peer, bool abort_peer) {
  int status = SALTS_OK;

  if (abort_peer && peer->listener >= 0)
    (void)shutdown(peer->listener, SHUT_RDWR);
  if (abort_peer && peer->accepted >= 0)
    (void)shutdown(peer->accepted, SHUT_RDWR);

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

static int shards_parallel_set_affinity(int cpu) {
  cpu_set_t set;
  int status;

  if (cpu < 0 || cpu >= CPU_SETSIZE) return SALTS_EINVAL;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  status = pthread_setaffinity_np(
      pthread_self(), sizeof(set), &set);
  return status == 0 ? SALTS_OK : -status;
}

static uint64_t shards_parallel_thread_cpu_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
    return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
}

static int shards_parallel_gate_init(shards_parallel_gate *gate) {
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

static void shards_parallel_gate_destroy(shards_parallel_gate *gate) {
  (void)pthread_cond_destroy(&gate->changed);
  (void)pthread_mutex_destroy(&gate->mutex);
}

static void shards_parallel_gate_fail(
    shards_parallel_gate *gate, int status) {
  (void)pthread_mutex_lock(&gate->mutex);
  if (gate->failure == SALTS_OK)
    gate->failure = status != SALTS_OK ? status : SALTS_EIO;
  (void)pthread_cond_broadcast(&gate->changed);
  (void)pthread_mutex_unlock(&gate->mutex);
}

static int shards_parallel_gate_ready_and_wait(
    shards_parallel_gate *gate) {
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

static void shards_parallel_gate_done(
    shards_parallel_gate *gate, int status) {
  (void)pthread_mutex_lock(&gate->mutex);
  if (status != SALTS_OK && gate->failure == SALTS_OK)
    gate->failure = status;
  ++gate->done;
  (void)pthread_cond_broadcast(&gate->changed);
  (void)pthread_mutex_unlock(&gate->mutex);
}

static int shards_parallel_drain_events(shards_parallel_lane *lane) {
  for (;;) {
    cnet_event_view event = {0};
    int status = cnet_shards_take_event(
        lane->shards, lane->connection.shard, &event);

    if (status == SALTS_ETIMEDOUT) return SALTS_OK;
    if (status != SALTS_OK) return status;
    if (event.session.slot != lane->connection.session.slot ||
        event.session.generation != lane->connection.session.generation) {
      (void)cnet_shards_release_event(
          lane->shards, lane->connection.shard, &event);
      return SALTS_EPROTO;
    }

    if (event.kind == CNET_EVENT_STATE) {
      if (event.state == CNET_EVENT_STATE_CONNECTED) {
        lane->connected = true;
      } else if (event.state == CNET_EVENT_STATE_CLOSED) {
        lane->terminal = true;
      } else if (event.state == CNET_EVENT_STATE_FAILED) {
        lane->status =
            event.status != SALTS_OK ? event.status : SALTS_EIO;
        lane->terminal = true;
      }
    } else if (event.kind == CNET_EVENT_SEND) {
      if (event.argument != lane->payload_size) {
        lane->status = SALTS_EPROTO;
      } else {
        lane->send_done = true;
        ++lane->measured_sends;
      }
    } else if (event.kind == CNET_EVENT_RECEIVE) {
      if (event.data == NULL || event.size == 0u ||
          event.size > lane->payload_size - lane->receive_offset ||
          memcmp(lane->payload + lane->receive_offset,
                 event.data, event.size) != 0) {
        lane->status = SALTS_EIO;
      } else {
        lane->receive_offset += event.size;
        if (lane->receive_offset == lane->payload_size) {
          if (lane->latency_out != NULL)
            *lane->latency_out =
                cmeta_hrtime() - lane->started_ns;
          lane->receive_offset = 0u;
          lane->receive_done = true;
          ++lane->measured_receives;
        }
      }
    }

    status = cnet_shards_release_event(
        lane->shards, lane->connection.shard, &event);
    if (status != SALTS_OK) return status;
    if (lane->status != SALTS_OK) return lane->status;
  }
}

static int shards_parallel_poll_lane(
    shards_parallel_lane *lane, uint32_t timeout_ms) {
  int status = cnet_shards_poll_owner(
      lane->shards, lane->connection.shard, timeout_ms);
  ++lane->poll_calls;
  if (status != SALTS_OK) return status;
  return shards_parallel_drain_events(lane);
}

static int shards_parallel_wait_connected(
    shards_parallel_lane *lane) {
  const uint64_t deadline =
      cmeta_monotonic_ms() + SHARDS_PARALLEL_TIMEOUT_MS;

  while (!lane->connected) {
    int status = shards_parallel_poll_lane(lane, 10u);
    if (status != SALTS_OK) return status;
    if (cmeta_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

static int shards_parallel_cycle(
    shards_parallel_lane *lane, uint64_t *latency_out,
    bool measuring) {
  const uint64_t deadline =
      cmeta_monotonic_ms() + SHARDS_PARALLEL_TIMEOUT_MS;
  size_t sends_before = lane->measured_sends;
  size_t receives_before = lane->measured_receives;
  int status;

  lane->send_done = false;
  lane->receive_done = false;
  lane->receive_offset = 0u;
  lane->latency_out = latency_out;
  lane->started_ns = cmeta_hrtime();
  if (latency_out != NULL) *latency_out = 0u;

  status = cnet_shards_send_buffer_direct(
      lane->shards, lane->connection, lane->buffer);
  if (status != SALTS_OK) return status;

  while (!lane->send_done || !lane->receive_done) {
    status = shards_parallel_poll_lane(lane, 10u);
    if (status != SALTS_OK) return status;
    if (cmeta_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }

  if (!measuring) {
    lane->measured_sends = sends_before;
    lane->measured_receives = receives_before;
  }
  if (latency_out != NULL && *latency_out == 0u)
    return SALTS_EPROTO;
  lane->latency_out = NULL;
  return SALTS_OK;
}

static int shards_parallel_warmup(shards_parallel_lane *lane) {
  for (size_t index = 0u;
       index < SHARDS_PARALLEL_WARMUPS; ++index) {
    const int status = shards_parallel_cycle(lane, NULL, false);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int shards_parallel_measure(
    shards_parallel_lane *lane,
    uint64_t latencies[SHARDS_PARALLEL_SAMPLES]) {
  for (size_t index = 0u;
       index < SHARDS_PARALLEL_SAMPLES; ++index) {
    const int status =
        shards_parallel_cycle(lane, &latencies[index], true);
    if (status != SALTS_OK) return status;
  }
  if (lane->measured_sends != SHARDS_PARALLEL_SAMPLES ||
      lane->measured_receives != SHARDS_PARALLEL_SAMPLES)
    return SALTS_EPROTO;
  return SALTS_OK;
}

static int shards_parallel_close_and_recycle(
    shards_parallel_lane *lane) {
  const uint64_t deadline =
      cmeta_monotonic_ms() + SHARDS_PARALLEL_TIMEOUT_MS;
  cnet_session_terminal terminal = {0};
  int status = cnet_shards_close(
      lane->shards, lane->connection);
  if (status != SALTS_OK && status != SALTS_EALREADY)
    return status;

  while (!lane->terminal) {
    status = shards_parallel_poll_lane(lane, 10u);
    if (status != SALTS_OK) return status;
    if (cmeta_monotonic_ms() >= deadline)
      return SALTS_ETIMEDOUT;
  }
  status = cnet_shards_recycle(
      lane->shards, lane->connection, &terminal);
  if (status != SALTS_OK) return status;
  return terminal.status;
}

static void *shards_parallel_owner_entry(void *user) {
  shards_parallel_thread_arg *arg =
      (shards_parallel_thread_arg *)user;
  shards_parallel_lane *lane = arg->lane;
  const size_t demand =
      lane->payload_size *
      (SHARDS_PARALLEL_WARMUPS + SHARDS_PARALLEL_SAMPLES);
  int status = shards_parallel_set_affinity(arg->cpu);

  arg->status = status;
  arg->observed_cpu = -1;
  if (status != SALTS_OK) {
    shards_parallel_gate_fail(arg->gate, status);
    return NULL;
  }
  arg->observed_cpu = sched_getcpu();
  if (arg->observed_cpu != arg->cpu) {
    arg->status = SALTS_EPROTO;
    shards_parallel_gate_fail(arg->gate, arg->status);
    return NULL;
  }

  status = cnet_shards_init_owner_experimental(
      lane->shards, lane->connection.shard);
  if (status != SALTS_OK) {
    arg->status = status;
    shards_parallel_gate_fail(arg->gate, status);
    return NULL;
  }

  status = shards_parallel_wait_connected(lane);
  if (status == SALTS_OK)
    status = cnet_shards_receive_direct(
        lane->shards, lane->connection, demand);
  if (status == SALTS_OK)
    status = shards_parallel_warmup(lane);
  if (status != SALTS_OK) {
    arg->status = status;
    shards_parallel_gate_fail(arg->gate, status);
    goto cleanup;
  }

  lane->poll_calls = 0u;
  lane->measured_sends = 0u;
  lane->measured_receives = 0u;

  status = shards_parallel_gate_ready_and_wait(arg->gate);
  if (status != SALTS_OK) {
    arg->status = status;
    goto cleanup;
  }

  {
    const uint64_t cpu_started = shards_parallel_thread_cpu_ns();
    status = shards_parallel_measure(lane, arg->latencies);
    arg->cpu_ns = shards_parallel_thread_cpu_ns() - cpu_started;
  }

  arg->status = status;
  shards_parallel_gate_done(arg->gate, status);

cleanup:
  {
    const int close_status =
        shards_parallel_close_and_recycle(lane);
    if (arg->status == SALTS_OK &&
        close_status != SALTS_OK)
      arg->status = close_status;
  }
  return NULL;
}

static int shards_parallel_u64_compare(
    const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t shards_parallel_percentile(
    uint64_t *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), shards_parallel_u64_compare);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int shards_parallel_parse_cpu(
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

static int shards_parallel_double_compare(
    const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double shards_parallel_double_median(
    double *values, size_t count) {
  qsort(values, count, sizeof(*values), shards_parallel_double_compare);
  return values[count / 2u];
}

static int shards_parallel_run_repeat(
    native_io_backend_kind backend_kind,
    const char *backend_name,
    const char *topology,
    size_t payload_size,
    size_t repeat,
    int cpu_a,
    int cpu_b,
    shards_parallel_sample *out) {
  cnet_shards shards = {0};
  shards_parallel_peer peers[SHARDS_PARALLEL_LANES];
  shards_parallel_lane lanes[SHARDS_PARALLEL_LANES];
  shards_parallel_gate gate;
  shards_parallel_thread_arg args[SHARDS_PARALLEL_LANES];
  pthread_t threads[SHARDS_PARALLEL_LANES];
  bool thread_started[SHARDS_PARALLEL_LANES] = {false, false};
  uint64_t combined_latencies[
      SHARDS_PARALLEL_LANES * SHARDS_PARALLEL_SAMPLES];
  const size_t cycles =
      SHARDS_PARALLEL_WARMUPS + SHARDS_PARALLEL_SAMPLES;
  const cnet_shards_config config = {
      .backend_kind = backend_kind,
      .shard_count = SHARDS_PARALLEL_LANES,
      .connection_capacity_per_shard = 1u,
      .command_capacity_per_shard = SHARDS_PARALLEL_COMMAND_CAPACITY,
      .request_capacity_per_shard = SHARDS_PARALLEL_REQUEST_CAPACITY,
      .completion_batch_capacity = SHARDS_PARALLEL_REQUEST_CAPACITY,
      .event_capacity_per_shard = SHARDS_PARALLEL_EVENT_CAPACITY,
      .receive_buffer_bytes = payload_size,
      .max_command_payload_bytes = sizeof(cnet_owner_connect_payload),
      .write_capacity_per_shard = 8u,
      .max_write_payload_bytes = payload_size};
  uint64_t wall_started = 0u;
  uint64_t wall_ns = 0u;
  uint64_t owner_cpu_ns = 0u;
  size_t latency_count = 0u;
  size_t poll_calls = 0u;
  size_t send_terminals = 0u;
  size_t receive_terminals = 0u;
  bool module_initialized = false;
  bool gate_initialized = false;
  int status = SALTS_OK;

  if (out == NULL) return SALTS_EINVAL;
  memset(out, 0, sizeof(*out));
  memset(lanes, 0, sizeof(lanes));
  memset(args, 0, sizeof(args));
  for (size_t index = 0u; index < SHARDS_PARALLEL_LANES; ++index)
    shards_parallel_peer_reset(&peers[index]);

  status = cnet_module_init();
  if (status != SALTS_OK) goto cleanup;
  module_initialized = true;

  status = shards_parallel_gate_init(&gate);
  if (status != SALTS_OK) goto cleanup;
  gate_initialized = true;

  status = cnet_shards_init_multi_owner_experimental(
      &shards, &config);
  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u;
       index < SHARDS_PARALLEL_LANES && status == SALTS_OK; ++index) {
    cnet_owner_connect_payload payload = {0};

    status = shards_parallel_peer_init(
        &peers[index], payload_size, cycles);
    if (status != SALTS_OK) break;

    payload.scheme = CNET_URI_TCP;
    payload.socket_options =
        (cnet_stream_socket_options)CNET_STREAM_SOCKET_OPTIONS_INIT;
    payload.socket_options.nodelay = 1;
    status = cnet_transport_parse_numeric_address(
        "127.0.0.1",
        ntohs(peers[index].address.sin_port),
        payload.address, sizeof(payload.address),
        &payload.address_length);
    if (status != SALTS_OK) break;

    status = cnet_shards_connect(
        &shards, &payload, &lanes[index].connection);
    if (status != SALTS_OK) break;

    if (lanes[index].connection.shard != (uint32_t)index) {
      status = SALTS_EPROTO;
      break;
    }

    lanes[index].shards = &shards;
    lanes[index].payload_size = payload_size;
    lanes[index].status = SALTS_OK;
    lanes[index].payload = (unsigned char *)malloc(payload_size);
    if (lanes[index].payload == NULL) {
      status = SALTS_ENOMEM;
      break;
    }
    memset(lanes[index].payload, 0x5a, payload_size);
    lanes[index].buffer = mem_wrap_external(
        lanes[index].payload, payload_size, NULL, NULL);
    if (lanes[index].buffer == NULL) {
      status = SALTS_ENOMEM;
      break;
    }
  }
  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u;
       index < SHARDS_PARALLEL_LANES && status == SALTS_OK; ++index) {
    args[index].gate = &gate;
    args[index].lane = &lanes[index];
    args[index].cpu = index == 0u ? cpu_a : cpu_b;
    const int create_status = pthread_create(
        &threads[index], NULL,
        shards_parallel_owner_entry, &args[index]);
    if (create_status != 0) {
      status = -create_status;
      break;
    }
    thread_started[index] = true;
  }

  if (status != SALTS_OK) {
    shards_parallel_gate_fail(&gate, status);
    goto join_threads;
  }

  (void)pthread_mutex_lock(&gate.mutex);
  while (gate.ready < SHARDS_PARALLEL_LANES &&
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
  while (gate.done < SHARDS_PARALLEL_LANES &&
         gate.failure == SALTS_OK)
    (void)pthread_cond_wait(&gate.changed, &gate.mutex);
  while (gate.done < SHARDS_PARALLEL_LANES)
    (void)pthread_cond_wait(&gate.changed, &gate.mutex);
  wall_ns = cmeta_hrtime() - wall_started;
  if (gate.failure != SALTS_OK) status = gate.failure;
  (void)pthread_mutex_unlock(&gate.mutex);

join_threads:
  for (size_t index = 0u;
       index < SHARDS_PARALLEL_LANES; ++index) {
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

  for (size_t index = 0u; index < SHARDS_PARALLEL_LANES; ++index) {
    if (args[index].observed_cpu != args[index].cpu) {
      status = SALTS_EPROTO;
      goto cleanup;
    }
    owner_cpu_ns += args[index].cpu_ns;
    for (size_t sample = 0u;
         sample < SHARDS_PARALLEL_SAMPLES; ++sample)
      combined_latencies[latency_count++] =
          args[index].latencies[sample];
    poll_calls += lanes[index].poll_calls;
    send_terminals += lanes[index].measured_sends;
    receive_terminals += lanes[index].measured_receives;
  }

  if (latency_count !=
          SHARDS_PARALLEL_LANES * SHARDS_PARALLEL_SAMPLES ||
      send_terminals != latency_count ||
      receive_terminals != latency_count ||
      wall_ns == 0u || owner_cpu_ns == 0u) {
    status = SALTS_EPROTO;
    goto cleanup;
  }

  out->backend = backend_name;
  out->topology = topology;
  out->mode = "shared_engine_parallel";
  out->payload_size = payload_size;
  out->repeat = repeat;
  out->logical_operations = latency_count;
  out->cpu_a = cpu_a;
  out->cpu_b = cpu_b;
  out->shard_a = lanes[0].connection.shard;
  out->shard_b = lanes[1].connection.shard;
  out->wall_ns = wall_ns;
  out->owner_cpu_ns = owner_cpu_ns;
  out->p50_ns = shards_parallel_percentile(
      combined_latencies, latency_count, 50u);
  out->p95_ns = shards_parallel_percentile(
      combined_latencies, latency_count, 95u);
  out->p99_ns = shards_parallel_percentile(
      combined_latencies, latency_count, 99u);
  out->operations_per_second =
      (double)latency_count * 1.0e9 / (double)wall_ns;
  out->mib_per_second =
      ((double)latency_count * (double)payload_size /
       (1024.0 * 1024.0)) *
      1.0e9 / (double)wall_ns;
  out->owner_cpu_us_per_op =
      (double)owner_cpu_ns / 1000.0 / (double)latency_count;
  out->poll_calls = poll_calls;
  out->send_terminals = send_terminals;
  out->receive_terminals = receive_terminals;

cleanup:
  if (shards.impl != NULL) {
    const int stop_status =
        cnet_shards_stop(&shards, SHARDS_PARALLEL_TIMEOUT_MS);
    if (status == SALTS_OK &&
        stop_status != SALTS_OK &&
        stop_status != SALTS_EALREADY)
      status = stop_status;
    if (cnet_shards_stopped(&shards)) {
      const int destroy_status = cnet_shards_destroy(&shards);
      if (status == SALTS_OK && destroy_status != SALTS_OK)
        status = destroy_status;
    }
  }

  for (size_t index = 0u; index < SHARDS_PARALLEL_LANES; ++index) {
    if (lanes[index].buffer != NULL) {
      if (status == SALTS_OK &&
          mem_buffer_ref_count(lanes[index].buffer) != 1u)
        status = SALTS_EPROTO;
      mem_buffer_release(lanes[index].buffer);
      lanes[index].buffer = NULL;
    }
    free(lanes[index].payload);
    lanes[index].payload = NULL;

    {
      const int peer_status =
          shards_parallel_peer_destroy(
              &peers[index], status != SALTS_OK);
      if (status == SALTS_OK && peer_status != SALTS_OK)
        status = peer_status;
    }
  }

  if (gate_initialized)
    shards_parallel_gate_destroy(&gate);
  if (module_initialized) {
    const int module_status = cnet_module_shutdown();
    if (status == SALTS_OK && module_status != SALTS_OK)
      status = module_status;
  }
  return status;
}

static FILE *shards_parallel_open_csv(void) {
  const char *prefix = getenv("CNET_SHARDS_PARALLEL_OUTPUT");
  char path[1024];

  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0)
    return NULL;
  return fopen(path, "w");
}

static int shards_parallel_write_csv(
    FILE *csv, const shards_parallel_sample *sample) {
  if (csv == NULL || sample == NULL) return SALTS_OK;
  return fprintf(
             csv,
             "%s,%s,%s,%zu,%zu,%zu,%d,%d,%u,%u,%" PRIu64
             ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
             ",%.6f,%.6f,%.6f,%zu,%zu,%zu\n",
             sample->backend, sample->topology, sample->mode,
             sample->payload_size, sample->repeat,
             sample->logical_operations,
             sample->cpu_a, sample->cpu_b,
             sample->shard_a, sample->shard_b,
             sample->wall_ns, sample->owner_cpu_ns,
             sample->p50_ns, sample->p95_ns, sample->p99_ns,
             sample->operations_per_second,
             sample->mib_per_second,
             sample->owner_cpu_us_per_op,
             sample->poll_calls,
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
      getenv("CNET_OWNER_PARALLEL_TOPOLOGY");
  shards_parallel_sample
      results[SHARDS_PARALLEL_PAYLOAD_COUNT][SHARDS_PARALLEL_REPEATS];
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
            "CNet shared-engine benchmark supports Linux epoll/io_uring only\n");
    return 2;
  }

  status = shards_parallel_parse_cpu(
      "CNET_OWNER_PARALLEL_CPU_A", &cpu_a);
  if (status == SALTS_OK)
    status = shards_parallel_parse_cpu(
        "CNET_OWNER_PARALLEL_CPU_B", &cpu_b);
  if (status != SALTS_OK) {
    fprintf(stderr, "invalid owner CPU affinity environment\n");
    return 2;
  }

  memset(results, 0, sizeof(results));
  for (size_t payload = 0u;
       payload < SHARDS_PARALLEL_PAYLOAD_COUNT; ++payload) {
    for (size_t repeat = 0u;
         repeat < SHARDS_PARALLEL_REPEATS; ++repeat) {
      status = shards_parallel_run_repeat(
          backend.kind, backend.name, topology,
          SHARDS_PARALLEL_PAYLOADS[payload],
          repeat + 1u, cpu_a, cpu_b,
          &results[payload][repeat]);
      if (status != SALTS_OK) {
        fprintf(stderr,
                "CNet shared-engine benchmark failed: backend=%s topology=%s payload=%zu repeat=%zu status=%d\n",
                backend.name, topology,
                SHARDS_PARALLEL_PAYLOADS[payload],
                repeat + 1u, status);
        return 1;
      }
    }
  }

  csv = shards_parallel_open_csv();
  if (csv != NULL) {
    fprintf(
        csv,
        "backend,topology,mode,payload_bytes,repeat,logical_operations,"
        "cpu_a,cpu_b,shard_a,shard_b,wall_ns,owner_cpu_ns,"
        "p50_ns,p95_ns,p99_ns,operations_per_second,mib_per_second,"
        "owner_cpu_us_per_op,poll_calls,send_terminals,receive_terminals\n");
  }

  printf("# CNet shared multi-owner engine retained TCP control\n\n");
  printf("Backend: %s\n\n", backend.name);
  printf("Topology: %s; owner CPU A=%d; owner CPU B=%d.\n\n",
         topology, cpu_a, cpu_b);
  printf("One private cnet_shards engine owns two fixed owners. Two adopted "
         "loopback TCP connections are assigned round-robin to shard 0 and "
         "shard 1. Measured send/receive progress is owner-local: retained "
         "send_buffer_direct + receive_direct + poll_owner + per-shard event "
         "queue. No public CNet facade or dispatcher participates.\n\n");
  printf("| payload | ops/s median | MiB/s median | p50 us median | "
         "p99 us median | owner CPU us/op median | poll calls median |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");

  for (size_t payload = 0u;
       payload < SHARDS_PARALLEL_PAYLOAD_COUNT; ++payload) {
    double rate[SHARDS_PARALLEL_REPEATS];
    double mib[SHARDS_PARALLEL_REPEATS];
    double p50[SHARDS_PARALLEL_REPEATS];
    double p99[SHARDS_PARALLEL_REPEATS];
    double cpu[SHARDS_PARALLEL_REPEATS];
    double polls[SHARDS_PARALLEL_REPEATS];

    for (size_t repeat = 0u;
         repeat < SHARDS_PARALLEL_REPEATS; ++repeat) {
      const shards_parallel_sample *sample =
          &results[payload][repeat];
      rate[repeat] = sample->operations_per_second;
      mib[repeat] = sample->mib_per_second;
      p50[repeat] = (double)sample->p50_ns / 1000.0;
      p99[repeat] = (double)sample->p99_ns / 1000.0;
      cpu[repeat] = sample->owner_cpu_us_per_op;
      polls[repeat] = (double)sample->poll_calls;
      status = shards_parallel_write_csv(csv, sample);
      if (status != SALTS_OK) goto cleanup;
    }

    printf("| %zu | %.0f | %.3f | %.3f | %.3f | %.3f | %.0f |\n",
           SHARDS_PARALLEL_PAYLOADS[payload],
           shards_parallel_double_median(
               rate, SHARDS_PARALLEL_REPEATS),
           shards_parallel_double_median(
               mib, SHARDS_PARALLEL_REPEATS),
           shards_parallel_double_median(
               p50, SHARDS_PARALLEL_REPEATS),
           shards_parallel_double_median(
               p99, SHARDS_PARALLEL_REPEATS),
           shards_parallel_double_median(
               cpu, SHARDS_PARALLEL_REPEATS),
           shards_parallel_double_median(
               polls, SHARDS_PARALLEL_REPEATS));
  }

cleanup:
  if (csv != NULL && fclose(csv) != 0 && status == SALTS_OK)
    status = SALTS_EIO;
  return status == SALTS_OK ? 0 : 1;
}
