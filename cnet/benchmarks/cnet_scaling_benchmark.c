#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>
#include <salts/thread.h>

#include "cnet_client_internal.h"
#include "cnet_io_benchmark_config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
  SCALE_MAX_CONNECTIONS = 64,
  SCALE_WARMUPS = 8,
  SCALE_SAMPLES = 32,
  SCALE_TIMEOUT_MS = 5000,
  SCALE_COMMAND_CAPACITY = 128,
  SCALE_EVENT_CAPACITY = 256,
  SCALE_REQUEST_CAPACITY = 256
};

static const size_t SCALE_CONNECTIONS[] = {1u, 4u, 16u, 64u};
static const size_t SCALE_PAYLOADS[] = {1024u, 8192u, 32768u, 65536u};

typedef struct scale_peer {
  int listener;
  int accepted[SCALE_MAX_CONNECTIONS];
  struct sockaddr_in address;
  size_t connections;
  size_t payload_size;
  size_t cycles;
  unsigned char *scratch;
  salts_thread_t thread;
  atomic_int status;
  bool thread_started;
} scale_peer;

typedef struct scale_result {
  const char *driver;
  size_t connections;
  size_t payload_size;
  size_t logical_operations;
  size_t peak_active;
  size_t progress_calls;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t p99_ns;
  double operations_per_second;
  double mib_per_second;
  uint64_t owner_drive_ns;
  uint64_t owner_observe_ns;
  uint64_t client_poll_ns;
  uint64_t owner_drive_calls;
  uint64_t owner_observe_calls;
  uint64_t client_poll_calls;
} scale_result;

typedef struct scale_native {
  native_io_backend backend;
  native_io_endpoint endpoints[SCALE_MAX_CONNECTIONS];
  int sockets[SCALE_MAX_CONNECTIONS];
  unsigned char *sent;
  unsigned char *received;
  size_t connections;
  size_t payload_size;
  size_t peak_active;
  size_t observe_calls;
} scale_native;

struct scale_cnet;

typedef struct scale_cnet_connection {
  struct scale_cnet *owner;
  cnet_connection handle;
  size_t index;
  size_t received;
  uint64_t started_ns;
  uint64_t *latency_out;
  bool connected;
} scale_cnet_connection;

typedef struct scale_cnet {
  cnet_client client;
  scale_cnet_connection connections[SCALE_MAX_CONNECTIONS];
  unsigned char *sent;
  size_t connection_count;
  size_t payload_size;
  size_t connected_count;
  size_t cycle_received;
  size_t cycle_sent;
  size_t poll_calls;
  int status;
} scale_cnet;

static int scale_socket_error(void) {
  return errno == 0 ? SALTS_EIO : -errno;
}

static int scale_set_nonblocking(int descriptor) {
  const int flags = fcntl(descriptor, F_GETFL, 0);
  if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0)
    return scale_socket_error();
  return SALTS_OK;
}

static int scale_set_nodelay(int descriptor) {
  const int enabled = 1;
  return setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0
             ? SALTS_OK
             : scale_socket_error();
}

static int scale_read_full(int descriptor, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = read(descriptor, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EOF : scale_socket_error();
  }
  return SALTS_OK;
}

static int scale_write_full(int descriptor, const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = write(descriptor, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EIO : scale_socket_error();
  }
  return SALTS_OK;
}

static void scale_peer_reset(scale_peer *peer) {
  memset(peer, 0, sizeof(*peer));
  peer->listener = -1;
  for (size_t index = 0u; index < SCALE_MAX_CONNECTIONS; ++index)
    peer->accepted[index] = -1;
}

static void scale_peer_entry(void *user) {
  scale_peer *peer = (scale_peer *)user;
  int status = SALTS_OK;

  for (size_t index = 0u; index < peer->connections && status == SALTS_OK; ++index) {
    int descriptor;
    do {
      descriptor = accept(peer->listener, NULL, NULL);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
      status = scale_socket_error();
      break;
    }
    peer->accepted[index] = descriptor;
    status = scale_set_nodelay(descriptor);
  }

  for (size_t cycle = 0u; cycle < peer->cycles && status == SALTS_OK; ++cycle) {
    for (size_t index = 0u; index < peer->connections; ++index) {
      status = scale_read_full(peer->accepted[index], peer->scratch, peer->payload_size);
      if (status != SALTS_OK) break;
      status = scale_write_full(peer->accepted[index], peer->scratch, peer->payload_size);
      if (status != SALTS_OK) break;
    }
  }

  atomic_store_explicit(&peer->status, status, memory_order_release);
}

static int scale_peer_init(scale_peer *peer, size_t connections, size_t payload_size,
                           size_t cycles) {
  socklen_t address_size = sizeof(peer->address);
  const int reuse = 1;
  int status;

  scale_peer_reset(peer);
  peer->connections = connections;
  peer->payload_size = payload_size;
  peer->cycles = cycles;
  peer->scratch = (unsigned char *)malloc(payload_size);
  if (peer->scratch == NULL) return SALTS_ENOMEM;

  peer->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (peer->listener < 0) return scale_socket_error();
  (void)setsockopt(peer->listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  memset(&peer->address, 0, sizeof(peer->address));
  peer->address.sin_family = AF_INET;
  peer->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer->address.sin_port = 0u;
  if (bind(peer->listener, (const struct sockaddr *)&peer->address, sizeof(peer->address)) != 0)
    return scale_socket_error();
  if (getsockname(peer->listener, (struct sockaddr *)&peer->address, &address_size) != 0)
    return scale_socket_error();
  if (listen(peer->listener, (int)connections) != 0) return scale_socket_error();

  atomic_init(&peer->status, SALTS_OK);
  status = salts_thread_create(&peer->thread, scale_peer_entry, peer);
  if (status == SALTS_OK) peer->thread_started = true;
  return status;
}

static int scale_peer_destroy(scale_peer *peer, bool abort_peer) {
  int status = SALTS_OK;

  if (abort_peer) {
    if (peer->listener >= 0) (void)shutdown(peer->listener, SHUT_RDWR);
    for (size_t index = 0u; index < peer->connections; ++index) {
      if (peer->accepted[index] >= 0) (void)shutdown(peer->accepted[index], SHUT_RDWR);
    }
  }

  if (peer->thread_started) {
    const int join_status = salts_thread_join(&peer->thread);
    if (join_status != SALTS_OK) status = join_status;
    salts_thread_destroy(&peer->thread);
    peer->thread_started = false;
    if (status == SALTS_OK) {
      const int peer_status = atomic_load_explicit(&peer->status, memory_order_acquire);
      if (!abort_peer && peer_status != SALTS_OK) status = peer_status;
    }
  }

  for (size_t index = 0u; index < peer->connections; ++index) {
    if (peer->accepted[index] >= 0) {
      (void)close(peer->accepted[index]);
      peer->accepted[index] = -1;
    }
  }
  if (peer->listener >= 0) {
    (void)close(peer->listener);
    peer->listener = -1;
  }
  free(peer->scratch);
  peer->scratch = NULL;
  return status;
}

static uint64_t scale_thread_cpu_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static int scale_u64_compare(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t scale_percentile(const uint64_t *sorted, size_t count, unsigned percentile) {
  size_t index;
  if (count == 0u) return 0u;
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return sorted[index];
}

static void scale_result_finish(scale_result *result, uint64_t *latencies, size_t latency_count) {
  qsort(latencies, latency_count, sizeof(*latencies), scale_u64_compare);
  result->logical_operations = latency_count;
  result->p50_ns = scale_percentile(latencies, latency_count, 50u);
  result->p95_ns = scale_percentile(latencies, latency_count, 95u);
  result->p99_ns = scale_percentile(latencies, latency_count, 99u);
  result->operations_per_second =
      result->wall_ns == 0u ? 0.0 : (double)latency_count * 1.0e9 / (double)result->wall_ns;
  result->mib_per_second =
      result->wall_ns == 0u
          ? 0.0
          : ((double)latency_count * (double)result->payload_size / (1024.0 * 1024.0)) *
                1.0e9 / (double)result->wall_ns;
}

static uintptr_t scale_native_tag(size_t index, bool send) {
  return (uintptr_t)((index << 1u) | (send ? 1u : 0u));
}

static int scale_native_prepare(scale_native *fixture, size_t index, bool send, size_t offset,
                                native_io_request *out_request) {
  native_io_operation operation = {
      .kind = send ? NATIVE_IO_OPERATION_TCP_SEND : NATIVE_IO_OPERATION_TCP_RECV,
      .endpoint = fixture->endpoints[index],
      .buffer = send ? (void *)(fixture->sent + offset)
                     : (void *)(fixture->received + index * fixture->payload_size + offset),
      .length = fixture->payload_size - offset,
      .user_data = scale_native_tag(index, send)};
  return native_io_backend_prepare(&fixture->backend, &operation, out_request);
}

static int scale_native_cycle(scale_native *fixture, uint64_t *latencies, size_t latency_base) {
  size_t send_offsets[SCALE_MAX_CONNECTIONS] = {0};
  size_t recv_offsets[SCALE_MAX_CONNECTIONS] = {0};
  native_io_completion completions[SCALE_MAX_CONNECTIONS * 2u];
  const size_t completion_capacity = fixture->connections * 2u;
  const uint64_t started = salts_hrtime();
  size_t sends_done = 0u;
  size_t recvs_done = 0u;
  native_io_backend_stats stats;

  memset(fixture->received, 0, fixture->connections * fixture->payload_size);

  for (size_t index = 0u; index < fixture->connections; ++index) {
    native_io_request request = {0};
    int status = scale_native_prepare(fixture, index, false, 0u, &request);
    if (status != SALTS_OK) return status;
  }
  for (size_t index = 0u; index < fixture->connections; ++index) {
    native_io_request request = {0};
    int status = scale_native_prepare(fixture, index, true, 0u, &request);
    if (status != SALTS_OK) return status;
  }

  if (native_io_backend_get_stats(&fixture->backend, &stats) &&
      stats.active_requests > fixture->peak_active)
    fixture->peak_active = stats.active_requests;

  while (sends_done != fixture->connections || recvs_done != fixture->connections) {
    size_t count = 0u;
    int status = native_io_backend_observe(&fixture->backend, completions, completion_capacity,
                                           SCALE_TIMEOUT_MS, &count);
    ++fixture->observe_calls;
    if (status != SALTS_OK) return status;
    if (count == 0u) return SALTS_ETIMEDOUT;

    for (size_t cursor = 0u; cursor < count; ++cursor) {
      const native_io_completion *completion = &completions[cursor];
      const bool send = (completion->user_data & 1u) != 0u;
      const size_t index = (size_t)(completion->user_data >> 1u);
      size_t *offset;
      native_io_request request = {0};

      if (index >= fixture->connections || completion->kind != NATIVE_IO_COMPLETION_OK ||
          completion->bytes == 0u)
        return completion->status != SALTS_OK ? completion->status : SALTS_EIO;

      offset = send ? &send_offsets[index] : &recv_offsets[index];
      if (completion->bytes > fixture->payload_size - *offset) return SALTS_EIO;
      *offset += completion->bytes;
      if (*offset == fixture->payload_size) {
        if (send) {
          ++sends_done;
        } else {
          ++recvs_done;
          if (latencies != NULL) latencies[latency_base + index] = salts_hrtime() - started;
        }
      } else {
        status = scale_native_prepare(fixture, index, send, *offset, &request);
        if (status != SALTS_OK) return status;
      }
    }
  }

  for (size_t index = 0u; index < fixture->connections; ++index) {
    if (memcmp(fixture->received + index * fixture->payload_size, fixture->sent,
               fixture->payload_size) != 0)
      return SALTS_EIO;
  }
  return SALTS_OK;
}

static void scale_native_reset(scale_native *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  for (size_t index = 0u; index < SCALE_MAX_CONNECTIONS; ++index)
    fixture->sockets[index] = -1;
}

static int scale_native_init(scale_native *fixture, const struct sockaddr_in *address,
                             size_t connections, size_t payload_size,
                             native_io_backend_kind backend_kind) {
  const native_io_backend_config config = {
      backend_kind, connections, connections * 2u, connections * 2u};
  int status;

  scale_native_reset(fixture);
  fixture->connections = connections;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(connections * payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5a, payload_size);

  status = native_io_backend_init(&fixture->backend, &config);
  if (status != SALTS_OK) return status;

  for (size_t index = 0u; index < connections; ++index) {
    int descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (descriptor < 0) return scale_socket_error();
    fixture->sockets[index] = descriptor;
    status = scale_set_nodelay(descriptor);
    if (status == SALTS_OK &&
        connect(descriptor, (const struct sockaddr *)address, sizeof(*address)) != 0)
      status = scale_socket_error();
    if (status == SALTS_OK) status = scale_set_nonblocking(descriptor);
    if (status == SALTS_OK)
      status = native_io_backend_attach_socket(&fixture->backend, (uintptr_t)descriptor,
                                               &fixture->endpoints[index]);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int scale_native_destroy(scale_native *fixture) {
  int status = SALTS_OK;

  for (size_t index = 0u; index < fixture->connections; ++index) {
    if (fixture->sockets[index] >= 0) (void)shutdown(fixture->sockets[index], SHUT_RDWR);
  }
  if (fixture->backend.impl != NULL) {
    const int close_status = native_io_backend_close(&fixture->backend);
    if (close_status != SALTS_OK && close_status != SALTS_EALREADY) status = close_status;
  }
  for (size_t index = 0u; index < fixture->connections; ++index) {
    if (fixture->backend.impl != NULL && native_io_endpoint_valid(fixture->endpoints[index])) {
      const int release_status =
          native_io_backend_release_socket(&fixture->backend, fixture->endpoints[index]);
      if (status == SALTS_OK && release_status != SALTS_OK) status = release_status;
    }
    if (fixture->sockets[index] >= 0) {
      (void)close(fixture->sockets[index]);
      fixture->sockets[index] = -1;
    }
  }
  if (fixture->backend.impl != NULL) {
    const int destroy_status = native_io_backend_destroy(&fixture->backend);
    if (status == SALTS_OK && destroy_status != SALTS_OK) status = destroy_status;
  }
  free(fixture->received);
  free(fixture->sent);
  scale_native_reset(fixture);
  return status;
}

static int scale_run_native(size_t connections, size_t payload_size,
                            native_io_backend_kind backend_kind, scale_result *out) {
  scale_peer peer;
  scale_native fixture;
  const size_t cycles = SCALE_WARMUPS + SCALE_SAMPLES;
  const size_t latency_count = SCALE_SAMPLES * connections;
  uint64_t *latencies = (uint64_t *)calloc(latency_count, sizeof(*latencies));
  uint64_t wall_started = 0u;
  uint64_t cpu_started = 0u;
  int status;

  memset(out, 0, sizeof(*out));
  out->driver = "NativeIO direct";
  out->connections = connections;
  out->payload_size = payload_size;
  scale_peer_reset(&peer);
  scale_native_reset(&fixture);
  if (latencies == NULL) return SALTS_ENOMEM;

  status = scale_peer_init(&peer, connections, payload_size, cycles);
  if (status == SALTS_OK)
    status = scale_native_init(&fixture, &peer.address, connections, payload_size, backend_kind);
  if (status != SALTS_OK) goto cleanup;

  for (size_t cycle = 0u; cycle < SCALE_WARMUPS; ++cycle) {
    status = scale_native_cycle(&fixture, NULL, 0u);
    if (status != SALTS_OK) goto cleanup;
  }

  fixture.peak_active = 0u;
  fixture.observe_calls = 0u;
  wall_started = salts_hrtime();
  cpu_started = scale_thread_cpu_ns();
  for (size_t sample = 0u; sample < SCALE_SAMPLES; ++sample) {
    status = scale_native_cycle(&fixture, latencies, sample * connections);
    if (status != SALTS_OK) goto cleanup;
  }
  out->cpu_ns = scale_thread_cpu_ns() - cpu_started;
  out->wall_ns = salts_hrtime() - wall_started;
  out->peak_active = fixture.peak_active;
  out->progress_calls = fixture.observe_calls;
  scale_result_finish(out, latencies, latency_count);

cleanup:
  {
    const int fixture_status = scale_native_destroy(&fixture);
    const int peer_status = scale_peer_destroy(&peer, status != SALTS_OK);
    if (status == SALTS_OK) status = fixture_status;
    if (status == SALTS_OK) status = peer_status;
  }
  free(latencies);
  return status;
}

static void scale_cnet_state(void *user, cnet_connection connection, cnet_connection_state state,
                             const cnet_error *error) {
  scale_cnet_connection *entry = (scale_cnet_connection *)user;
  scale_cnet *fixture = entry->owner;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    if (!entry->connected) {
      entry->connected = true;
      ++fixture->connected_count;
    }
  } else if (state == CNET_CONNECTION_FAILED) {
    fixture->status = error == NULL ? SALTS_EIO : error->status;
  }
}

static void scale_cnet_receive(void *user, cnet_connection connection,
                               const cnet_receive_view *view) {
  scale_cnet_connection *entry = (scale_cnet_connection *)user;
  scale_cnet *fixture = entry->owner;
  const unsigned char *data = (const unsigned char *)view->data;
  (void)connection;

  if (view->kind != CNET_MESSAGE_BYTES || view->size == 0u ||
      view->size > fixture->payload_size - entry->received) {
    fixture->status = SALTS_EIO;
    return;
  }
  for (size_t index = 0u; index < view->size; ++index) {
    if (data[index] != 0x5au) {
      fixture->status = SALTS_EIO;
      return;
    }
  }
  entry->received += view->size;
  if (entry->received == fixture->payload_size) {
    if (entry->latency_out != NULL) *entry->latency_out = salts_hrtime() - entry->started_ns;
    entry->received = 0u;
    ++fixture->cycle_received;
  }
}

static void scale_cnet_sent(void *user, cnet_connection connection, size_t size) {
  scale_cnet_connection *entry = (scale_cnet_connection *)user;
  scale_cnet *fixture = entry->owner;
  (void)connection;
  if (size != fixture->payload_size) {
    fixture->status = SALTS_EIO;
    return;
  }
  ++fixture->cycle_sent;
}

static int scale_cnet_wait_connected(scale_cnet *fixture) {
  const uint64_t deadline = salts_monotonic_ms() + SCALE_TIMEOUT_MS;
  while (fixture->connected_count < fixture->connection_count) {
    size_t events = 0u;
    int status;
    if (fixture->status != SALTS_OK) return fixture->status;
    status = cnet_client_poll(&fixture->client, 10u, &events);
    if (status != SALTS_OK) return status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

static int scale_cnet_cycle(scale_cnet *fixture, uint64_t *latencies, size_t latency_base) {
  const uint64_t deadline = salts_monotonic_ms() + SCALE_TIMEOUT_MS;
  fixture->cycle_received = 0u;
  fixture->cycle_sent = 0u;

  for (size_t index = 0u; index < fixture->connection_count; ++index) {
    scale_cnet_connection *entry = &fixture->connections[index];
    entry->started_ns = salts_hrtime();
    entry->latency_out = latencies == NULL ? NULL : &latencies[latency_base + index];
    {
      const int status = cnet_send(&fixture->client, entry->handle, fixture->sent,
                                   fixture->payload_size);
      if (status != SALTS_OK) return status;
    }
  }

  while (fixture->cycle_received < fixture->connection_count ||
         fixture->cycle_sent < fixture->connection_count) {
    size_t events = 0u;
    int status;
    if (fixture->status != SALTS_OK) return fixture->status;
    status = cnet_client_poll(&fixture->client, 10u, &events);
    ++fixture->poll_calls;
    if (status != SALTS_OK) return status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return fixture->status;
}

static int scale_cnet_init(scale_cnet *fixture, const struct sockaddr_in *address,
                           size_t connections, size_t payload_size,
                           native_io_backend_kind backend_kind, size_t cycles) {
  const cnet_client_config config = {
      .backend = backend_kind,
      .connection_capacity = connections,
      .command_capacity = SCALE_COMMAND_CAPACITY,
      .request_capacity = SCALE_REQUEST_CAPACITY,
      .completion_batch_capacity = SCALE_REQUEST_CAPACITY,
      .event_capacity = SCALE_EVENT_CAPACITY,
      .max_send_bytes = SCALE_PAYLOADS[sizeof(SCALE_PAYLOADS) / sizeof(SCALE_PAYLOADS[0]) - 1u],
      .receive_buffer_bytes =
          SCALE_PAYLOADS[sizeof(SCALE_PAYLOADS) / sizeof(SCALE_PAYLOADS[0]) - 1u],
      .connect_timeout_ms = SCALE_TIMEOUT_MS,
      .read_timeout_ms = 0u,
      .write_timeout_ms = 0u};
  char uri[64];
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->connection_count = connections;
  fixture->payload_size = payload_size;
  fixture->status = SALTS_OK;
  fixture->sent = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5a, payload_size);

  status = cnet_client_init(&fixture->client, &config);
  if (status != SALTS_OK) return status;
  if (snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned)ntohs(address->sin_port)) < 0)
    return SALTS_EIO;

  for (size_t index = 0u; index < connections; ++index) {
    scale_cnet_connection *entry = &fixture->connections[index];
    cnet_connect_options options;
    entry->owner = fixture;
    entry->index = index;
    options = (cnet_connect_options){
        .uri = uri,
        .observer = {.on_state = scale_cnet_state,
                     .on_receive = scale_cnet_receive,
                     .user = entry,
                     .on_send = scale_cnet_sent}};
    status = cnet_connect(&fixture->client, &options, &entry->handle);
    if (status != SALTS_OK) return status;
  }

  status = scale_cnet_wait_connected(fixture);
  if (status != SALTS_OK) return status;
  if (payload_size > SIZE_MAX / cycles) return SALTS_ERANGE;
  for (size_t index = 0u; index < connections; ++index) {
    status = cnet_receive(&fixture->client, fixture->connections[index].handle,
                          payload_size * cycles);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int scale_cnet_destroy(scale_cnet *fixture) {
  int status = SALTS_OK;
  if (fixture->client.impl != NULL) {
    const int stop_status = cnet_client_stop(&fixture->client, SCALE_TIMEOUT_MS);
    if (stop_status != SALTS_OK) status = stop_status;
    {
      const int destroy_status = cnet_client_destroy(&fixture->client);
      if (status == SALTS_OK && destroy_status != SALTS_OK) status = destroy_status;
    }
  }
  free(fixture->sent);
  memset(fixture, 0, sizeof(*fixture));
  return status;
}

static int scale_run_cnet(size_t connections, size_t payload_size,
                          native_io_backend_kind backend_kind, scale_result *out) {
  scale_peer peer;
  scale_cnet fixture;
  cnet_client_poll_profile profile = {0};
  const size_t cycles = SCALE_WARMUPS + SCALE_SAMPLES;
  const size_t latency_count = SCALE_SAMPLES * connections;
  uint64_t *latencies = (uint64_t *)calloc(latency_count, sizeof(*latencies));
  uint64_t wall_started = 0u;
  uint64_t cpu_started = 0u;
  int status;

  memset(out, 0, sizeof(*out));
  out->driver = "CNet";
  out->connections = connections;
  out->payload_size = payload_size;
  scale_peer_reset(&peer);
  memset(&fixture, 0, sizeof(fixture));
  if (latencies == NULL) return SALTS_ENOMEM;

  status = scale_peer_init(&peer, connections, payload_size, cycles);
  if (status == SALTS_OK)
    status = scale_cnet_init(&fixture, &peer.address, connections, payload_size, backend_kind,
                             cycles);
  if (status != SALTS_OK) goto cleanup;

  for (size_t cycle = 0u; cycle < SCALE_WARMUPS; ++cycle) {
    status = scale_cnet_cycle(&fixture, NULL, 0u);
    if (status != SALTS_OK) goto cleanup;
  }

  fixture.poll_calls = 0u;
  status = cnet_client_profile_begin(&fixture.client);
  if (status != SALTS_OK) goto cleanup;
  wall_started = salts_hrtime();
  cpu_started = scale_thread_cpu_ns();
  for (size_t sample = 0u; sample < SCALE_SAMPLES; ++sample) {
    status = scale_cnet_cycle(&fixture, latencies, sample * connections);
    if (status != SALTS_OK) goto cleanup;
  }
  out->cpu_ns = scale_thread_cpu_ns() - cpu_started;
  out->wall_ns = salts_hrtime() - wall_started;
  status = cnet_client_profile_take(&fixture.client, &profile);
  if (status != SALTS_OK) goto cleanup;

  out->peak_active = connections;
  out->progress_calls = fixture.poll_calls;
  out->owner_drive_ns = profile.owner.owner_drive_ns;
  out->owner_observe_ns = profile.owner.observe_ns;
  out->client_poll_ns = profile.client_poll_ns;
  out->owner_drive_calls = profile.owner.owner_drive_calls;
  out->owner_observe_calls = profile.owner.observe_calls;
  out->client_poll_calls = profile.client_poll_calls;
  scale_result_finish(out, latencies, latency_count);

cleanup:
  {
    const int fixture_status = scale_cnet_destroy(&fixture);
    const int peer_status = scale_peer_destroy(&peer, status != SALTS_OK);
    if (status == SALTS_OK) status = fixture_status;
    if (status == SALTS_OK) status = peer_status;
  }
  free(latencies);
  return status;
}

static FILE *scale_open_csv(void) {
  const char *prefix = getenv("CNET_SCALING_BENCHMARK_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void scale_print_result(const scale_result *result) {
  const double cpu_us =
      result->logical_operations == 0u
          ? 0.0
          : (double)result->cpu_ns / 1000.0 / (double)result->logical_operations;
  const double owner_drive_us =
      result->logical_operations == 0u
          ? 0.0
          : (double)result->owner_drive_ns / 1000.0 / (double)result->logical_operations;
  const double observe_us =
      result->logical_operations == 0u
          ? 0.0
          : (double)result->owner_observe_ns / 1000.0 / (double)result->logical_operations;

  printf("| %s | %zu | %zu | %.0f | %.2f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f |\n",
         result->driver, result->connections, result->payload_size,
         result->operations_per_second, result->mib_per_second,
         (double)result->p50_ns / 1000.0, (double)result->p95_ns / 1000.0,
         (double)result->p99_ns / 1000.0, cpu_us, owner_drive_us, observe_us);
}

static int scale_write_csv(FILE *csv, const scale_result *result,
                           const char *backend_name) {
  if (csv == NULL) return SALTS_OK;
  if (fprintf(csv,
              "%s,%s,%zu,%zu,%u,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64
              ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.6f,%.6f,%" PRIu64
              ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
              backend_name, result->driver, result->connections, result->payload_size,
              (unsigned)SCALE_SAMPLES, result->logical_operations, result->peak_active,
              result->progress_calls, result->wall_ns, result->cpu_ns, result->p50_ns,
              result->p95_ns, result->p99_ns, result->operations_per_second,
              result->mib_per_second, result->owner_drive_ns, result->owner_observe_ns,
              result->client_poll_ns, result->owner_drive_calls, result->owner_observe_calls,
              result->client_poll_calls) < 0)
    return SALTS_EIO;
  return fflush(csv) == 0 ? SALTS_OK : SALTS_EIO;
}

int main(void) {
  cnet_io_benchmark_backend backend = {0};
  const char *requested_backend = getenv("CNET_IO_BENCHMARK_BACKEND");
  FILE *csv = scale_open_csv();
  int status = cnet_io_benchmark_select_backend(requested_backend, &backend);

  if (status != SALTS_OK) {
    fprintf(stderr, "backend selection failed: %s status=%d\n",
            requested_backend == NULL ? "<unset>" : requested_backend, status);
    return 2;
  }
  if (backend.kind != NATIVE_IO_BACKEND_EPOLL && backend.kind != NATIVE_IO_BACKEND_IO_URING) {
    fprintf(stderr, "scaling benchmark currently supports Linux epoll/io_uring only\n");
    return 2;
  }

  if (csv != NULL) {
    fprintf(csv,
            "backend,driver,connections,payload_bytes,samples,logical_operations,peak_active,"
            "progress_calls,wall_ns,cpu_ns,p50_ns,p95_ns,p99_ns,operations_per_second,"
            "mib_per_second,owner_drive_ns,owner_observe_ns,client_poll_ns,owner_drive_calls,"
            "owner_observe_calls,client_poll_calls\n");
  }

  printf("# NativeIO direct versus CNet TCP scaling benchmark\n\n");
  printf("Backend: %s. One owner, independent persistent TCP loopback connections, "
         "one logical round trip outstanding per connection.\n", backend.name);
  printf("The peer processes connections in stable index order for both drivers. "
         "Logical concurrency is connection count; stream lane heads remain serialized per endpoint.\n\n");
  printf("| driver | connections | payload | ops/s | MiB/s | p50 us | p95 us | p99 us | "
         "CPU us/op | CNet owner drive us/op | CNet observe us/op |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");

  for (size_t p = 0u; p < sizeof(SCALE_PAYLOADS) / sizeof(SCALE_PAYLOADS[0]); ++p) {
    for (size_t d = 0u; d < sizeof(SCALE_CONNECTIONS) / sizeof(SCALE_CONNECTIONS[0]); ++d) {
      scale_result native_result;
      scale_result cnet_result;
      const size_t connections = SCALE_CONNECTIONS[d];
      const size_t payload = SCALE_PAYLOADS[p];

      if (((p + d) & 1u) == 0u) {
        status = scale_run_native(connections, payload, backend.kind, &native_result);
        if (status == SALTS_OK)
          status = scale_run_cnet(connections, payload, backend.kind, &cnet_result);
      } else {
        status = scale_run_cnet(connections, payload, backend.kind, &cnet_result);
        if (status == SALTS_OK)
          status = scale_run_native(connections, payload, backend.kind, &native_result);
      }
      if (status != SALTS_OK) {
        fprintf(stderr, "scaling cell failed backend=%s connections=%zu payload=%zu status=%d\n",
                backend.name, connections, payload, status);
        break;
      }

      scale_print_result(&native_result);
      scale_print_result(&cnet_result);
      status = scale_write_csv(csv, &native_result, backend.name);
      if (status == SALTS_OK) status = scale_write_csv(csv, &cnet_result, backend.name);
      if (status != SALTS_OK) break;
    }
    if (status != SALTS_OK) break;
  }

  if (csv != NULL) fclose(csv);
  return status == SALTS_OK ? 0 : 1;
}
