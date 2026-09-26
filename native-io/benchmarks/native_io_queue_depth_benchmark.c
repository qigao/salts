#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>
#include <salts/thread.h>

#include <errno.h>
#include <inttypes.h>
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
  QD_BENCH_MAX_QD = 64,
  QD_BENCH_WARMUPS = 8,
  QD_BENCH_SAMPLES = 32,
  QD_BENCH_TIMEOUT_MS = 5000
};

static const size_t QD_BENCH_DEPTHS[] = {1u, 2u, 4u, 8u, 16u, 32u, 64u};
static const size_t QD_BENCH_PAYLOADS[] = {1024u, 8192u, 32768u, 65536u};

typedef struct qd_peer {
  int descriptors[QD_BENCH_MAX_QD];
  size_t qd;
  size_t payload_size;
  size_t cycles;
  unsigned char *scratch;
  atomic_int status;
} qd_peer;

typedef struct qd_fixture {
  native_io_backend backend;
  native_io_endpoint endpoints[QD_BENCH_MAX_QD];
  int local[QD_BENCH_MAX_QD];
  qd_peer peer;
  salts_thread_t peer_thread;
  unsigned char *sent;
  unsigned char *received;
  size_t qd;
  size_t payload_size;
  size_t peak_active;
  size_t observe_calls;
} qd_fixture;

typedef struct qd_result {
  size_t qd;
  size_t payload_size;
  size_t logical_operations;
  size_t peak_active;
  size_t observe_calls;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t p99_ns;
  double operations_per_second;
  double mib_per_second;
} qd_result;

static int qd_socket_error(void) {
  return errno == 0 ? SALTS_EIO : -errno;
}

static int qd_read_full(int descriptor, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    ssize_t count = read(descriptor, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EOF : qd_socket_error();
  }
  return SALTS_OK;
}

static int qd_write_full(int descriptor, const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    ssize_t count = write(descriptor, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EIO : qd_socket_error();
  }
  return SALTS_OK;
}

static void qd_peer_run(void *user) {
  qd_peer *peer = (qd_peer *)user;
  int status = SALTS_OK;
  for (size_t cycle = 0u; cycle < peer->cycles && status == SALTS_OK; ++cycle) {
    for (size_t index = 0u; index < peer->qd; ++index) {
      status = qd_read_full(peer->descriptors[index], peer->scratch, peer->payload_size);
      if (status != SALTS_OK) break;
      status = qd_write_full(peer->descriptors[index], peer->scratch, peer->payload_size);
      if (status != SALTS_OK) break;
    }
  }
  atomic_store_explicit(&peer->status, status, memory_order_release);
}

static uint64_t qd_thread_cpu_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static int qd_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t qd_percentile(const uint64_t *sorted, size_t count, unsigned percentile) {
  size_t index;
  if (count == 0u) return 0u;
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return sorted[index];
}

static uintptr_t qd_tag(size_t endpoint_index, bool send) {
  return (uintptr_t)((endpoint_index << 1u) | (send ? 1u : 0u));
}

static int qd_prepare(qd_fixture *fixture, size_t index, bool send, size_t offset,
                      native_io_request *out_request) {
  native_io_operation operation = {
      .kind = send ? NATIVE_IO_OPERATION_STREAM_SEND : NATIVE_IO_OPERATION_STREAM_RECV,
      .endpoint = fixture->endpoints[index],
      .buffer = send ? (void *)(fixture->sent + offset)
                     : (void *)(fixture->received + index * fixture->payload_size + offset),
      .length = fixture->payload_size - offset,
      .user_data = qd_tag(index, send)};
  return native_io_backend_prepare(&fixture->backend, &operation, out_request);
}

static int qd_cycle(qd_fixture *fixture, uint64_t *latencies, size_t latency_base) {
  size_t send_offsets[QD_BENCH_MAX_QD] = {0};
  size_t recv_offsets[QD_BENCH_MAX_QD] = {0};
  native_io_completion completions[QD_BENCH_MAX_QD * 2u];
  const size_t completion_capacity = fixture->qd * 2u;
  const uint64_t started = salts_hrtime();
  size_t sends_done = 0u;
  size_t recvs_done = 0u;
  native_io_backend_stats stats;

  memset(fixture->received, 0, fixture->qd * fixture->payload_size);

  for (size_t index = 0u; index < fixture->qd; ++index) {
    native_io_request request = {0};
    int status = qd_prepare(fixture, index, false, 0u, &request);
    if (status != SALTS_OK) return status;
  }
  for (size_t index = 0u; index < fixture->qd; ++index) {
    native_io_request request = {0};
    int status = qd_prepare(fixture, index, true, 0u, &request);
    if (status != SALTS_OK) return status;
  }

  if (native_io_backend_get_stats(&fixture->backend, &stats)) {
    if (stats.active_requests > fixture->peak_active) fixture->peak_active = stats.active_requests;
  }

  while (sends_done != fixture->qd || recvs_done != fixture->qd) {
    size_t count = 0u;
    int status = native_io_backend_observe(&fixture->backend, completions, completion_capacity,
                                           QD_BENCH_TIMEOUT_MS, &count);
    ++fixture->observe_calls;
    if (status != SALTS_OK) return status;
    if (count == 0u) return SALTS_EIO;

    for (size_t cursor = 0u; cursor < count; ++cursor) {
      const native_io_completion *completion = &completions[cursor];
      const bool send = (completion->user_data & 1u) != 0u;
      const size_t index = (size_t)(completion->user_data >> 1u);
      size_t *offset;
      native_io_request request = {0};

      if (index >= fixture->qd || completion->kind != NATIVE_IO_COMPLETION_OK ||
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
        status = qd_prepare(fixture, index, send, *offset, &request);
        if (status != SALTS_OK) return status;
      }
    }
  }

  for (size_t index = 0u; index < fixture->qd; ++index) {
    const unsigned char *received = fixture->received + index * fixture->payload_size;
    if (memcmp(received, fixture->sent, fixture->payload_size) != 0) return SALTS_EIO;
  }
  return SALTS_OK;
}

static void qd_fixture_reset(qd_fixture *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  for (size_t i = 0u; i < QD_BENCH_MAX_QD; ++i) {
    fixture->local[i] = -1;
    fixture->peer.descriptors[i] = -1;
  }
}

static int qd_fixture_init(qd_fixture *fixture, size_t qd, size_t payload_size, size_t cycles) {
  native_io_backend_config config = {
      NATIVE_IO_BACKEND_IO_URING, qd, qd * 2u, qd * 2u};
  int status;

  qd_fixture_reset(fixture);
  fixture->qd = qd;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(qd * payload_size);
  fixture->peer.scratch = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL || fixture->peer.scratch == NULL)
    return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, qd * payload_size);

  status = native_io_backend_init(&fixture->backend, &config);
  if (status != SALTS_OK) return status;

  for (size_t index = 0u; index < qd; ++index) {
    int pair[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return qd_socket_error();
    fixture->local[index] = pair[0];
    fixture->peer.descriptors[index] = pair[1];
    status = native_io_backend_attach_socket(&fixture->backend, (uintptr_t)pair[0],
                                             &fixture->endpoints[index]);
    if (status != SALTS_OK) return status;
  }

  fixture->peer.qd = qd;
  fixture->peer.payload_size = payload_size;
  fixture->peer.cycles = cycles;
  atomic_init(&fixture->peer.status, SALTS_OK);
  status = salts_thread_create(&fixture->peer_thread, qd_peer_run, &fixture->peer);
  return status;
}

static int qd_fixture_destroy(qd_fixture *fixture, bool abort_peer) {
  int status = SALTS_OK;
  int peer_status = SALTS_OK;

  if (abort_peer) {
    /* Unblock a peer that may be waiting in read() after a failed sample. */
    for (size_t index = 0u; index < fixture->qd; ++index) {
      if (fixture->local[index] >= 0) (void)shutdown(fixture->local[index], SHUT_RDWR);
      if (fixture->peer.descriptors[index] >= 0)
        (void)shutdown(fixture->peer.descriptors[index], SHUT_RDWR);
    }
  }

  if (fixture->peer_thread != NULL) {
    int join_status = salts_thread_join(&fixture->peer_thread);
    if (join_status != SALTS_OK && status == SALTS_OK) status = join_status;
    peer_status = atomic_load_explicit(&fixture->peer.status, memory_order_acquire);
    if (peer_status != SALTS_OK && status == SALTS_OK) status = peer_status;
  }

  if (fixture->backend.impl != NULL) {
    int close_status = native_io_backend_close(&fixture->backend);
    if (close_status != SALTS_OK && close_status != SALTS_EALREADY && status == SALTS_OK)
      status = close_status;
  }

  for (size_t index = 0u; index < fixture->qd; ++index) {
    if (fixture->local[index] >= 0) {
      (void)close(fixture->local[index]);
      fixture->local[index] = -1;
      if (fixture->backend.impl != NULL && native_io_endpoint_valid(fixture->endpoints[index])) {
        int release_status = native_io_backend_release_socket(&fixture->backend,
                                                              fixture->endpoints[index]);
        if (release_status != SALTS_OK && status == SALTS_OK) status = release_status;
      }
    }
    if (fixture->peer.descriptors[index] >= 0) {
      (void)close(fixture->peer.descriptors[index]);
      fixture->peer.descriptors[index] = -1;
    }
  }

  if (fixture->backend.impl != NULL) {
    int destroy_status = native_io_backend_destroy(&fixture->backend);
    if (destroy_status != SALTS_OK && status == SALTS_OK) status = destroy_status;
  }

  free(fixture->peer.scratch);
  free(fixture->received);
  free(fixture->sent);
  qd_fixture_reset(fixture);
  return status;
}

static int qd_run_cell(size_t qd, size_t payload_size, qd_result *out) {
  qd_fixture fixture;
  const size_t latency_count = (size_t)QD_BENCH_SAMPLES * qd;
  uint64_t *latencies = (uint64_t *)calloc(latency_count, sizeof(uint64_t));
  uint64_t wall_started;
  uint64_t cpu_started;
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = qd_fixture_init(&fixture, qd, payload_size,
                           QD_BENCH_WARMUPS + QD_BENCH_SAMPLES);
  if (status != SALTS_OK) {
    free(latencies);
    (void)qd_fixture_destroy(&fixture, true);
    return status;
  }

  for (size_t sample = 0u; sample < QD_BENCH_WARMUPS; ++sample) {
    status = qd_cycle(&fixture, NULL, 0u);
    if (status != SALTS_OK) goto cleanup;
  }

  /* Measurement counters exclude warmup. Reset peak too, so the CSV proves
   * that the requested 2*QD active request depth was reached while timed. */
  fixture.peak_active = 0u;
  fixture.observe_calls = 0u;

  fprintf(stderr, "NATIVE_IO_QD_MEASURE_BEGIN qd=%zu payload=%zu samples=%u\n",
          qd, payload_size, (unsigned)QD_BENCH_SAMPLES);
  fflush(stderr);
  wall_started = salts_hrtime();
  cpu_started = qd_thread_cpu_ns();

  for (size_t sample = 0u; sample < QD_BENCH_SAMPLES; ++sample) {
    status = qd_cycle(&fixture, latencies, sample * qd);
    if (status != SALTS_OK) goto cleanup;
  }

  out->cpu_ns = qd_thread_cpu_ns() - cpu_started;
  out->wall_ns = salts_hrtime() - wall_started;
  fprintf(stderr, "NATIVE_IO_QD_MEASURE_END qd=%zu payload=%zu\n", qd, payload_size);
  fflush(stderr);

  qsort(latencies, latency_count, sizeof(*latencies), qd_compare_u64);
  out->qd = qd;
  out->payload_size = payload_size;
  out->logical_operations = latency_count;
  out->peak_active = fixture.peak_active;
  out->observe_calls = fixture.observe_calls;
  out->p50_ns = qd_percentile(latencies, latency_count, 50u);
  out->p95_ns = qd_percentile(latencies, latency_count, 95u);
  out->p99_ns = qd_percentile(latencies, latency_count, 99u);
  out->operations_per_second =
      out->wall_ns == 0u ? 0.0 : (double)latency_count * 1.0e9 / (double)out->wall_ns;
  out->mib_per_second =
      out->wall_ns == 0u
          ? 0.0
          : ((double)latency_count * (double)payload_size / (1024.0 * 1024.0)) *
                1.0e9 / (double)out->wall_ns;

  if (out->peak_active < qd * 2u) {
    status = SALTS_EIO;
    goto cleanup;
  }

cleanup:
  {
    int destroy_status = qd_fixture_destroy(&fixture, status != SALTS_OK);
    if (status == SALTS_OK && destroy_status != SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static bool qd_trace_filter(size_t qd, size_t payload_size) {
  const char *value = getenv("NATIVE_IO_QD_TRACE");
  size_t requested_qd = 0u;
  size_t requested_payload = 0u;
  if (value == NULL || *value == '\0') return true;
  if (sscanf(value, "%zu:%zu", &requested_qd, &requested_payload) != 2) return false;
  return qd == requested_qd && payload_size == requested_payload;
}

static FILE *qd_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_QD_BENCHMARK_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

int main(void) {
  FILE *csv = qd_open_csv();
  size_t cells = 0u;
  int status = SALTS_OK;

  if (!native_io_backend_kind_supported(NATIVE_IO_BACKEND_IO_URING)) {
    fprintf(stderr, "io_uring backend is not supported\n");
    return 2;
  }

  if (csv != NULL) {
    fprintf(csv,
            "qd,payload_bytes,samples,logical_operations,peak_active,observe_calls,"
            "wall_ns,cpu_ns,p50_ns,p95_ns,p99_ns,operations_per_second,mib_per_second\n");
  }

  printf("# NativeIO io_uring queue-depth benchmark\n\n");
  printf("One owner, independent persistent stream socketpairs, one read and one write lane head per endpoint.\n\n");
  printf("| QD | payload | ops/s | MiB/s | p50 us | p95 us | p99 us | CPU us/op | peak active | observe/op |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");

  for (size_t p = 0u; p < sizeof(QD_BENCH_PAYLOADS) / sizeof(QD_BENCH_PAYLOADS[0]); ++p) {
    for (size_t d = 0u; d < sizeof(QD_BENCH_DEPTHS) / sizeof(QD_BENCH_DEPTHS[0]); ++d) {
      const size_t qd = QD_BENCH_DEPTHS[d];
      const size_t payload = QD_BENCH_PAYLOADS[p];
      qd_result result = {0};
      if (!qd_trace_filter(qd, payload)) continue;
      status = qd_run_cell(qd, payload, &result);
      if (status != SALTS_OK) {
        fprintf(stderr, "queue-depth benchmark failed qd=%zu payload=%zu status=%d\n",
                qd, payload, status);
        break;
      }
      ++cells;
      printf("| %zu | %zu | %.0f | %.2f | %.3f | %.3f | %.3f | %.3f | %zu | %.3f |\n",
             result.qd, result.payload_size, result.operations_per_second,
             result.mib_per_second, (double)result.p50_ns / 1000.0,
             (double)result.p95_ns / 1000.0, (double)result.p99_ns / 1000.0,
             result.logical_operations == 0u
                 ? 0.0
                 : (double)result.cpu_ns / 1000.0 / (double)result.logical_operations,
             result.peak_active,
             result.logical_operations == 0u
                 ? 0.0
                 : (double)result.observe_calls / (double)result.logical_operations);
      if (csv != NULL) {
        fprintf(csv,
                "%zu,%zu,%u,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%.6f,%.6f\n",
                result.qd, result.payload_size, (unsigned)QD_BENCH_SAMPLES,
                result.logical_operations, result.peak_active, result.observe_calls,
                result.wall_ns, result.cpu_ns, result.p50_ns, result.p95_ns, result.p99_ns,
                result.operations_per_second, result.mib_per_second);
        fflush(csv);
      }
    }
    if (status != SALTS_OK) break;
  }

  if (csv != NULL) fclose(csv);
  if (status != SALTS_OK) return 1;
  if (cells == 0u) {
    fprintf(stderr, "NATIVE_IO_QD_TRACE did not match a benchmark cell\n");
    return 2;
  }
  return 0;
}
