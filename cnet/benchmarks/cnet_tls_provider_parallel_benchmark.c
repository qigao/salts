#include "cnet_tls.h"

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
  TLS_PARALLEL_PAIRS = 8,
  TLS_PARALLEL_MAX_OWNERS = 4,
  TLS_PARALLEL_RECORD_HEADER_BYTES = 5,
  TLS_PARALLEL_TRANSFER_BYTES = 4096,
  TLS_PARALLEL_DEFAULT_OPS = 64,
  TLS_PARALLEL_WARMUP_OPS = 8
};

#ifndef CNET_TLS_BENCH_CA
#error CNET_TLS_BENCH_CA is required
#endif
#ifndef CNET_TLS_BENCH_CERT
#error CNET_TLS_BENCH_CERT is required
#endif
#ifndef CNET_TLS_BENCH_KEY
#error CNET_TLS_BENCH_KEY is required
#endif

typedef enum tls_parallel_mode {
  TLS_PARALLEL_PUSH = 0,
  TLS_PARALLEL_ECHO = 1
} tls_parallel_mode;

typedef struct tls_parallel_record_counter {
  unsigned char header[TLS_PARALLEL_RECORD_HEADER_BYTES];
  size_t header_size;
  size_t payload_remaining;
  uint64_t records;
} tls_parallel_record_counter;

typedef struct tls_parallel_pair {
  cnet_tls_server server_context;
  cnet_tls_state client;
  cnet_tls_state server;
  unsigned char *received;
  size_t received_capacity;
  bool initialized;
} tls_parallel_pair;

typedef struct tls_parallel_shared {
  const unsigned char *payload;
  size_t payload_size;
  size_t owner_count;
  size_t ops_per_owner;
  size_t warmup_ops;
  tls_parallel_mode mode;
  atomic_size_t ready;
  atomic_size_t measured_done;
  atomic_bool start;
  atomic_bool cleanup;
  atomic_int first_error;
} tls_parallel_shared;

typedef struct tls_parallel_worker {
  tls_parallel_shared *shared;
  tls_parallel_pair pairs[TLS_PARALLEL_PAIRS];
  uint64_t *latencies_ns;
  size_t first_pair;
  size_t pair_count;
  uint64_t tls_records;
  uint64_t cipher_bytes;
} tls_parallel_worker;

static int tls_parallel_u64_compare(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t tls_parallel_percentile(
    const uint64_t *values, size_t count, unsigned percent) {
  uint64_t *ordered;
  size_t rank;
  uint64_t result;
  if (values == NULL || count == 0u) return 0u;
  ordered = (uint64_t *)malloc(count * sizeof(*ordered));
  if (ordered == NULL) return 0u;
  memcpy(ordered, values, count * sizeof(*ordered));
  qsort(ordered, count, sizeof(*ordered), tls_parallel_u64_compare);
  rank = ((size_t)percent * count + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  if (rank > count) rank = count;
  result = ordered[rank - 1u];
  free(ordered);
  return result;
}

static size_t tls_parallel_env_count(const char *name, size_t fallback) {
  const char *text = getenv(name);
  char *end = NULL;
  unsigned long long value;
  if (text == NULL || text[0] == '\0') return fallback;
  value = strtoull(text, &end, 10);
  if (end == text || end == NULL || *end != '\0' ||
      value == 0u || value > 1000000ull)
    return fallback;
  return (size_t)value;
}

static int tls_parallel_parse_size(
    const char *text, size_t min_value, size_t max_value,
    size_t *out_value) {
  char *end = NULL;
  unsigned long long value;
  if (text == NULL || out_value == NULL) return SALTS_EINVAL;
  value = strtoull(text, &end, 10);
  if (end == text || end == NULL || *end != '\0' ||
      value < min_value || value > max_value)
    return SALTS_EINVAL;
  *out_value = (size_t)value;
  return SALTS_OK;
}

static void tls_parallel_set_error(
    tls_parallel_shared *shared, int status) {
  int expected = SALTS_OK;
  if (shared == NULL || status == SALTS_OK) return;
  (void)atomic_compare_exchange_strong_explicit(
      &shared->first_error, &expected, status,
      memory_order_acq_rel, memory_order_acquire);
}

static int tls_parallel_count_records(
    tls_parallel_record_counter *counter,
    const unsigned char *data, size_t size) {
  size_t offset = 0u;
  if (counter == NULL || data == NULL || size == 0u)
    return SALTS_EINVAL;
  while (offset < size) {
    if (counter->payload_remaining != 0u) {
      const size_t available = size - offset;
      const size_t take =
          available < counter->payload_remaining
              ? available
              : counter->payload_remaining;
      offset += take;
      counter->payload_remaining -= take;
      continue;
    }
    while (counter->header_size < TLS_PARALLEL_RECORD_HEADER_BYTES &&
           offset < size)
      counter->header[counter->header_size++] = data[offset++];
    if (counter->header_size != TLS_PARALLEL_RECORD_HEADER_BYTES)
      continue;
    counter->payload_remaining =
        ((size_t)counter->header[3] << 8u) |
        (size_t)counter->header[4];
    counter->header_size = 0u;
    if (counter->records == UINT64_MAX) return SALTS_ERANGE;
    ++counter->records;
  }
  return SALTS_OK;
}

static int tls_parallel_transfer(
    cnet_tls_state *source, cnet_tls_state *target,
    tls_parallel_record_counter *counter,
    uint64_t *io_cipher_bytes, bool *out_progress) {
  unsigned char buffer[TLS_PARALLEL_TRANSFER_BYTES];
  bool progress = false;
  for (;;) {
    size_t capacity = cnet_tls_cipher_input_capacity(target);
    size_t size = 0u;
    int status;
    if (capacity == 0u) break;
    if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
    status = cnet_tls_take_cipher(source, buffer, capacity, &size);
    if (status == SALTS_ENOENT) break;
    if (status != SALTS_OK) return status;
    if (size == 0u || size > capacity) return SALTS_EPROTO;
    if (counter != NULL) {
      status = tls_parallel_count_records(counter, buffer, size);
      if (status != SALTS_OK) return status;
    }
    status = cnet_tls_feed_cipher(target, buffer, size);
    if (status != SALTS_OK) return status;
    if (io_cipher_bytes != NULL) {
      if (UINT64_MAX - *io_cipher_bytes < size) return SALTS_ERANGE;
      *io_cipher_bytes += size;
    }
    progress = true;
  }
  if (out_progress != NULL) *out_progress = progress;
  return SALTS_OK;
}

static int tls_parallel_handshake(tls_parallel_pair *pair) {
  size_t iteration;
  for (iteration = 0u; iteration < 512u; ++iteration) {
    bool client_complete = false;
    bool server_complete = false;
    bool progress = false;
    int status = cnet_tls_handshake(
        &pair->client, &client_complete);
    if (status != SALTS_OK) return status;
    status = tls_parallel_transfer(
        &pair->client, &pair->server, NULL, NULL, &progress);
    if (status != SALTS_OK) return status;
    status = cnet_tls_handshake(
        &pair->server, &server_complete);
    if (status != SALTS_OK) return status;
    status = tls_parallel_transfer(
        &pair->server, &pair->client, NULL, NULL, &progress);
    if (status != SALTS_OK) return status;
    if (client_complete && server_complete) return SALTS_OK;
  }
  return SALTS_ETIMEDOUT;
}

static int tls_parallel_pair_init(
    tls_parallel_pair *pair, size_t payload_size) {
  cnet_tls_server_config server_config;
  cnet_tls_client_config client_config;
  cnet_tls_context *client_context = NULL;
  cnet_tls_context *server_context;
  int status;

  memset(pair, 0, sizeof(*pair));
  pair->received = (unsigned char *)malloc(payload_size);
  if (pair->received == NULL) return SALTS_ENOMEM;
  pair->received_capacity = payload_size;

  server_config = (cnet_tls_server_config){
      .size = sizeof(server_config),
      .cert_file = CNET_TLS_BENCH_CERT,
      .key_file = CNET_TLS_BENCH_KEY,
      .client_auth = CNET_TLS_CLIENT_AUTH_NONE};
  status = cnet_tls_server_init(
      &pair->server_context, &server_config);
  if (status != SALTS_OK) return status;

  client_config = (cnet_tls_client_config){
      .size = sizeof(client_config),
      .ca_file = CNET_TLS_BENCH_CA};
  status = cnet_tls_client_context_create(
      &client_config, &client_context);
  if (status != SALTS_OK) return status;
  status = cnet_tls_state_init(
      &pair->client, client_context, false, "localhost",
      CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) {
    cnet_tls_context_release(client_context);
    return status;
  }

  server_context = cnet_tls_server_context(
      &pair->server_context);
  cnet_tls_context_retain(server_context);
  status = cnet_tls_state_init(
      &pair->server, server_context, true, NULL,
      CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) {
    cnet_tls_context_release(server_context);
    return status;
  }
  status = tls_parallel_handshake(pair);
  if (status == SALTS_OK) pair->initialized = true;
  return status;
}

static void tls_parallel_pair_destroy(tls_parallel_pair *pair) {
  if (pair == NULL) return;
  if (pair->initialized) {
    cnet_tls_state_destroy(&pair->server);
    cnet_tls_state_destroy(&pair->client);
    (void)cnet_tls_server_destroy(&pair->server_context);
  }
  free(pair->received);
  memset(pair, 0, sizeof(*pair));
}

static int tls_parallel_message(
    cnet_tls_state *source, cnet_tls_state *target,
    unsigned char *received, size_t received_capacity,
    const unsigned char *payload, size_t payload_size,
    uint64_t *out_records, uint64_t *out_cipher_bytes) {
  tls_parallel_record_counter counter = {0};
  size_t received_size = 0u;
  uint64_t cipher_bytes = 0u;
  bool write_complete = false;
  size_t iteration;

  if (source == NULL || target == NULL || received == NULL ||
      payload == NULL || payload_size == 0u ||
      received_capacity < payload_size)
    return SALTS_EINVAL;

  for (iteration = 0u; iteration < 65536u; ++iteration) {
    bool progress = false;
    int status;

    while (received_size < payload_size) {
      size_t size = 0u;
      bool peer_closed = false;
      status = cnet_tls_read(
          target, received + received_size,
          payload_size - received_size, &size, &peer_closed);
      if (status != SALTS_OK) return status;
      if (peer_closed) return SALTS_ECONNABORTED;
      if (size == 0u) break;
      received_size += size;
      progress = true;
    }

    if (!write_complete) {
      status = cnet_tls_write(
          source, payload, payload_size, &write_complete);
      if (status != SALTS_OK) return status;
    }

    {
      bool transferred = false;
      status = tls_parallel_transfer(
          source, target, &counter, &cipher_bytes, &transferred);
      if (status != SALTS_OK) return status;
      if (transferred) progress = true;
    }

    if (write_complete && received_size == payload_size) {
      if (counter.header_size != 0u ||
          counter.payload_remaining != 0u ||
          counter.records == 0u)
        return SALTS_EPROTO;
      if (memcmp(received, payload, payload_size) != 0)
        return SALTS_EPROTO;
      if (out_records != NULL) *out_records += counter.records;
      if (out_cipher_bytes != NULL) *out_cipher_bytes += cipher_bytes;
      return SALTS_OK;
    }

    if (!progress && write_complete && received_size < payload_size)
      continue;
    if (!progress && !write_complete)
      return SALTS_EPROTO;
  }
  return SALTS_ETIMEDOUT;
}

static int tls_parallel_operation(
    tls_parallel_pair *pair, tls_parallel_mode mode,
    const unsigned char *payload, size_t payload_size,
    uint64_t *out_records, uint64_t *out_cipher_bytes) {
  int status;
  status = tls_parallel_message(
      &pair->server, &pair->client,
      pair->received, pair->received_capacity,
      payload, payload_size, out_records, out_cipher_bytes);
  if (status != SALTS_OK || mode == TLS_PARALLEL_PUSH)
    return status;
  return tls_parallel_message(
      &pair->client, &pair->server,
      pair->received, pair->received_capacity,
      payload, payload_size, out_records, out_cipher_bytes);
}

static void tls_parallel_worker_run(void *user) {
  tls_parallel_worker *worker =
      (tls_parallel_worker *)user;
  tls_parallel_shared *shared;
  size_t index;
  int status = SALTS_OK;

  if (worker == NULL || worker->shared == NULL) return;
  shared = worker->shared;

  for (index = 0u; index < worker->pair_count; ++index) {
    status = tls_parallel_pair_init(
        &worker->pairs[index], shared->payload_size);
    if (status != SALTS_OK) break;
  }

  if (status == SALTS_OK) {
    for (size_t warm = 0u; warm < shared->warmup_ops; ++warm) {
      tls_parallel_pair *pair =
          &worker->pairs[warm % worker->pair_count];
      status = tls_parallel_operation(
          pair, shared->mode, shared->payload,
          shared->payload_size, NULL, NULL);
      if (status != SALTS_OK) break;
    }
  }
  tls_parallel_set_error(shared, status);
  atomic_fetch_add_explicit(
      &shared->ready, 1u, memory_order_release);

  while (!atomic_load_explicit(
      &shared->start, memory_order_acquire))
    salts_thread_yield();

  if (status == SALTS_OK) {
    for (index = 0u;
         index < shared->ops_per_owner; ++index) {
      tls_parallel_pair *pair =
          &worker->pairs[index % worker->pair_count];
      const uint64_t started_ns = salts_hrtime();
      status = tls_parallel_operation(
          pair, shared->mode, shared->payload,
          shared->payload_size, &worker->tls_records,
          &worker->cipher_bytes);
      if (status != SALTS_OK) {
        tls_parallel_set_error(shared, status);
        break;
      }
      worker->latencies_ns[index] =
          salts_hrtime() - started_ns;
    }
  }

  atomic_fetch_add_explicit(
      &shared->measured_done, 1u, memory_order_release);
  while (!atomic_load_explicit(
      &shared->cleanup, memory_order_acquire))
    salts_thread_yield();

  for (index = 0u; index < worker->pair_count; ++index)
    tls_parallel_pair_destroy(&worker->pairs[index]);
}

static const char *tls_parallel_mode_name(tls_parallel_mode mode) {
  return mode == TLS_PARALLEL_ECHO ? "echo" : "push";
}

int main(int argc, char **argv) {
  tls_parallel_shared shared;
  tls_parallel_worker workers[TLS_PARALLEL_MAX_OWNERS];
  salts_thread_t threads[TLS_PARALLEL_MAX_OWNERS] = {0};
  bool thread_started[TLS_PARALLEL_MAX_OWNERS] = {false};
  unsigned char *payload = NULL;
  uint64_t *latencies = NULL;
  size_t payload_size;
  size_t owner_count;
  size_t total_ops;
  size_t warmup_ops;
  size_t ops_per_owner;
  size_t samples = 0u;
  uint64_t total_records = 0u;
  uint64_t total_cipher_bytes = 0u;
  uint64_t started_ns;
  uint64_t wall_ns;
  clock_t cpu_started;
  clock_t cpu_elapsed;
  tls_parallel_mode mode;
  const char *temperature;
  int status = SALTS_OK;

  if (argc != 5) {
    fprintf(stderr,
            "usage: %s <push|echo> <payload-bytes> <owners> <fresh|warm>\n",
            argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "push") == 0)
    mode = TLS_PARALLEL_PUSH;
  else if (strcmp(argv[1], "echo") == 0)
    mode = TLS_PARALLEL_ECHO;
  else
    return 2;
  if (tls_parallel_parse_size(
          argv[2], 1u, 128u * 1024u, &payload_size) != SALTS_OK ||
      tls_parallel_parse_size(
          argv[3], 1u, TLS_PARALLEL_MAX_OWNERS,
          &owner_count) != SALTS_OK ||
      (owner_count != 1u && owner_count != 2u &&
       owner_count != 4u) ||
      TLS_PARALLEL_PAIRS % owner_count != 0u)
    return 2;

  if (strcmp(argv[4], "fresh") == 0) {
    temperature = "fresh";
    warmup_ops = 0u;
  } else if (strcmp(argv[4], "warm") == 0) {
    temperature = "warm";
    warmup_ops = TLS_PARALLEL_WARMUP_OPS;
  } else {
    return 2;
  }

  total_ops = tls_parallel_env_count(
      "CNET_TLS_PROVIDER_PARALLEL_OPS",
      TLS_PARALLEL_DEFAULT_OPS);
  if (total_ops < owner_count)
    total_ops = owner_count;
  total_ops -= total_ops % owner_count;
  ops_per_owner = total_ops / owner_count;

  payload = (unsigned char *)malloc(payload_size);
  latencies = (uint64_t *)calloc(
      total_ops, sizeof(*latencies));
  if (payload == NULL || latencies == NULL) {
    status = SALTS_ENOMEM;
    goto cleanup;
  }
  for (size_t index = 0u; index < payload_size; ++index)
    payload[index] =
        (unsigned char)((index * 37u + payload_size) & 0xffu);

  memset(&shared, 0, sizeof(shared));
  shared.payload = payload;
  shared.payload_size = payload_size;
  shared.owner_count = owner_count;
  shared.ops_per_owner = ops_per_owner;
  shared.warmup_ops = warmup_ops;
  shared.mode = mode;
  atomic_init(&shared.ready, 0u);
  atomic_init(&shared.measured_done, 0u);
  atomic_init(&shared.start, false);
  atomic_init(&shared.cleanup, false);
  atomic_init(&shared.first_error, SALTS_OK);

  memset(workers, 0, sizeof(workers));
  for (size_t owner = 0u; owner < owner_count; ++owner) {
    workers[owner].shared = &shared;
    workers[owner].first_pair =
        owner * (TLS_PARALLEL_PAIRS / owner_count);
    workers[owner].pair_count =
        TLS_PARALLEL_PAIRS / owner_count;
    workers[owner].latencies_ns =
        latencies + owner * ops_per_owner;
    status = salts_thread_create(
        &threads[owner], tls_parallel_worker_run,
        &workers[owner]);
    if (status != SALTS_OK) {
      tls_parallel_set_error(&shared, status);
      for (size_t missing = owner;
           missing < owner_count; ++missing) {
        atomic_fetch_add_explicit(
            &shared.ready, 1u, memory_order_release);
        atomic_fetch_add_explicit(
            &shared.measured_done, 1u, memory_order_release);
      }
      owner_count = owner;
      break;
    }
    thread_started[owner] = true;
  }

  while (atomic_load_explicit(
             &shared.ready, memory_order_acquire) <
         owner_count)
    salts_thread_yield();

  status = atomic_load_explicit(
      &shared.first_error, memory_order_acquire);
  cpu_started = clock();
  started_ns = salts_hrtime();
  atomic_store_explicit(
      &shared.start, true, memory_order_release);

  while (atomic_load_explicit(
             &shared.measured_done, memory_order_acquire) <
         owner_count)
    salts_thread_yield();
  wall_ns = salts_hrtime() - started_ns;
  cpu_elapsed = clock() - cpu_started;

  if (status == SALTS_OK)
    status = atomic_load_explicit(
        &shared.first_error, memory_order_acquire);

  atomic_store_explicit(
      &shared.cleanup, true, memory_order_release);
  for (size_t owner = 0u; owner < owner_count; ++owner) {
    if (!thread_started[owner]) continue;
    if (salts_thread_join(&threads[owner]) != SALTS_OK &&
        status == SALTS_OK)
      status = SALTS_EIO;
    salts_thread_destroy(&threads[owner]);
    for (size_t index = 0u; index < ops_per_owner; ++index)
      if (workers[owner].latencies_ns[index] != 0u)
        ++samples;
    total_records += workers[owner].tls_records;
    total_cipher_bytes += workers[owner].cipher_bytes;
  }

  if (status == SALTS_OK && samples != total_ops)
    status = SALTS_EPROTO;

  if (status == SALTS_OK) {
    const double operations_per_second =
        wall_ns != 0u
            ? (double)total_ops * 1.0e9 /
                  (double)wall_ns
            : 0.0;
    const double cpu_ns_per_op =
        total_ops != 0u
            ? ((double)cpu_elapsed * 1.0e9 /
               (double)CLOCKS_PER_SEC) /
                  (double)total_ops
            : 0.0;
    printf(
        "{\"benchmark\":\"cnet_tls_provider_parallel\","
        "\"mode\":\"%s\","
        "\"temperature\":\"%s\","
        "\"payload_bytes\":%zu,"
        "\"owners\":%zu,"
        "\"pairs\":%u,"
        "\"operations\":%zu,"
        "\"samples\":%zu,"
        "\"ops_per_second\":%.3f,"
        "\"cpu_ns_per_op\":%.3f,"
        "\"p50_ns\":%llu,"
        "\"p95_ns\":%llu,"
        "\"p99_ns\":%llu,"
        "\"tls_records_per_op\":%.3f,"
        "\"cipher_bytes_per_op\":%.3f,"
        "\"errors\":0}\n",
        tls_parallel_mode_name(mode), temperature,
        payload_size, owner_count, TLS_PARALLEL_PAIRS,
        total_ops, samples, operations_per_second,
        cpu_ns_per_op,
        (unsigned long long)tls_parallel_percentile(
            latencies, samples, 50u),
        (unsigned long long)tls_parallel_percentile(
            latencies, samples, 95u),
        (unsigned long long)tls_parallel_percentile(
            latencies, samples, 99u),
        (double)total_records / (double)total_ops,
        (double)total_cipher_bytes / (double)total_ops);
  }

cleanup:
  free(latencies);
  free(payload);
  if (status != SALTS_OK) {
    fprintf(stderr,
            "TLS provider parallel benchmark failed: status=%d\n",
            status);
    return 1;
  }
  return 0;
}
