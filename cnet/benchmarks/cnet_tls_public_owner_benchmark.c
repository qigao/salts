#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cnet/cnet.h>
#include "cnet_client_internal.h"
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
  TLS_PUBLIC_PAIRS = 8,
  TLS_PUBLIC_MAX_OWNERS = 4,
  TLS_PUBLIC_TIMEOUT_MS = 10000,
  TLS_PUBLIC_DEFAULT_OPS = 48,
  TLS_PUBLIC_WARMUP_ROUNDS_PER_PAIR = 2
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

typedef enum tls_public_mode {
  TLS_PUBLIC_PUSH = 0,
  TLS_PUBLIC_ECHO = 1
} tls_public_mode;

typedef struct tls_public_probe {
  cnet_client *client;
  cnet_connection connection;
  unsigned char *received;
  size_t capacity;
  size_t received_size;
  size_t sent_count;
  int connected;
  int terminal;
  int failed;
  int failure_status;
} tls_public_probe;

typedef struct tls_public_pair {
  cnet_client client;
  cnet_client server;
  cnet_listener listener;
  cnet_tls_client tls_client;
  cnet_tls_server tls_server;
  tls_public_probe client_probe;
  tls_public_probe server_probe;
  mem_buffer_t *payload;
  bool accepted;
  bool client_initialized;
  bool server_initialized;
  bool listener_initialized;
  bool tls_client_initialized;
  bool tls_server_initialized;
  bool client_profile_active;
  bool server_profile_active;
} tls_public_pair;

typedef struct tls_public_shared {
  const unsigned char *payload_bytes;
  size_t payload_size;
  size_t ops_per_owner;
  size_t warmup_rounds_per_pair;
  int nodelay;
  tls_public_mode mode;
  atomic_size_t ready;
  atomic_size_t measured_done;
  atomic_bool start;
  atomic_bool cleanup;
  atomic_int first_error;
} tls_public_shared;

typedef struct tls_public_worker {
  tls_public_shared *shared;
  tls_public_pair pairs[TLS_PUBLIC_PAIRS];
  uint64_t *latencies_ns;
  size_t pair_count;
  uint64_t cpu_ns;
  cnet_owner_profile client_profile;
  cnet_owner_profile server_profile;
} tls_public_worker;

static uint64_t tls_public_thread_cpu_ns(void) {
  struct timespec value = {0};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
    return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
}

static uint64_t tls_public_add_u64(uint64_t left, uint64_t right) {
  return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

static void tls_public_profile_add(
    cnet_owner_profile *target, const cnet_owner_profile *source) {
  if (target == NULL || source == NULL) return;
#define TLS_PUBLIC_PROFILE_ADD(FIELD) \
  target->FIELD = tls_public_add_u64(target->FIELD, source->FIELD)
  TLS_PUBLIC_PROFILE_ADD(tls_pump_ns);
  TLS_PUBLIC_PROFILE_ADD(tls_pump_calls);
  TLS_PUBLIC_PROFILE_ADD(tls_ciphertext_bytes);
  TLS_PUBLIC_PROFILE_ADD(tls_write_submit_calls);
  TLS_PUBLIC_PROFILE_ADD(tls_write_submit_bytes);
  TLS_PUBLIC_PROFILE_ADD(tls_write_completion_calls);
  TLS_PUBLIC_PROFILE_ADD(tls_write_completion_bytes);
  TLS_PUBLIC_PROFILE_ADD(tls_read_completion_calls);
  TLS_PUBLIC_PROFILE_ADD(tls_read_completion_bytes);
  TLS_PUBLIC_PROFILE_ADD(tls_plaintext_receive_calls);
  TLS_PUBLIC_PROFILE_ADD(tls_plaintext_receive_bytes);
#undef TLS_PUBLIC_PROFILE_ADD
}

static int tls_public_u64_compare(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t tls_public_percentile(
    const uint64_t *values, size_t count, unsigned int percent) {
  uint64_t *ordered;
  size_t rank;
  uint64_t result;
  if (values == NULL || count == 0u) return 0u;
  ordered = (uint64_t *)malloc(count * sizeof(*ordered));
  if (ordered == NULL) return 0u;
  memcpy(ordered, values, count * sizeof(*ordered));
  qsort(ordered, count, sizeof(*ordered), tls_public_u64_compare);
  rank = ((size_t)percent * count + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  if (rank > count) rank = count;
  result = ordered[rank - 1u];
  free(ordered);
  return result;
}

static size_t tls_public_env_count(const char *name, size_t fallback) {
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

static int tls_public_parse_size(
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

static void tls_public_set_error(tls_public_shared *shared, int status) {
  int expected = SALTS_OK;
  if (shared == NULL || status == SALTS_OK) return;
  (void)atomic_compare_exchange_strong_explicit(
      &shared->first_error, &expected, status,
      memory_order_acq_rel, memory_order_acquire);
}

static void tls_public_on_state(
    void *user, cnet_connection connection,
    cnet_connection_state state, const cnet_error *error) {
  tls_public_probe *probe = (tls_public_probe *)user;
  if (probe == NULL) return;
  probe->connection = connection;
  if (state == CNET_CONNECTION_CONNECTED) {
    char version[16] = {0};
    size_t version_size = 0u;
    const int status = cnet_tls_negotiated_version(
        probe->client, connection, version, sizeof(version),
        &version_size);
    if (status != SALTS_OK ||
        strcmp(version, "TLSv1.3") != 0 ||
        version_size != strlen("TLSv1.3")) {
      probe->failed = 1;
      probe->failure_status =
          status == SALTS_OK ? SALTS_EPROTO : status;
      return;
    }
    probe->connected = 1;
  } else if (state == CNET_CONNECTION_CLOSED ||
             state == CNET_CONNECTION_FAILED) {
    probe->terminal = 1;
    if (state == CNET_CONNECTION_FAILED || error != NULL) {
      probe->failed = 1;
      probe->failure_status =
          error != NULL ? error->status : SALTS_EIO;
    }
  }
}

static void tls_public_on_receive(
    void *user, cnet_connection connection,
    const cnet_receive_view *view) {
  tls_public_probe *probe = (tls_public_probe *)user;
  (void)connection;
  if (probe == NULL || view == NULL ||
      view->kind != CNET_MESSAGE_BYTES ||
      view->data == NULL) {
    if (probe != NULL) {
      probe->failed = 1;
      probe->failure_status = SALTS_EPROTO;
    }
    return;
  }
  if (probe->received_size > probe->capacity ||
      view->size > probe->capacity - probe->received_size) {
    probe->failed = 1;
    probe->failure_status = SALTS_ENOBUFS;
    return;
  }
  memcpy(probe->received + probe->received_size,
         view->data, view->size);
  probe->received_size += view->size;
}

static void tls_public_on_send(
    void *user, cnet_connection connection, size_t size) {
  tls_public_probe *probe = (tls_public_probe *)user;
  (void)connection;
  if (probe == NULL) return;
  if (size == 0u) {
    probe->failed = 1;
    probe->failure_status = SALTS_EPROTO;
    return;
  }
  ++probe->sent_count;
}

static cnet_client_config tls_public_config(size_t payload_size) {
  cnet_client_config config = {
      .backend = NATIVE_IO_BACKEND_EPOLL,
      .connection_capacity = 1u,
      .command_capacity = 64u,
      .request_capacity = 64u,
      .completion_batch_capacity = 32u,
      .event_capacity = 128u,
      .max_send_bytes = payload_size,
      .receive_buffer_bytes = 32u * 1024u,
      .connect_timeout_ms = TLS_PUBLIC_TIMEOUT_MS,
      .read_timeout_ms = TLS_PUBLIC_TIMEOUT_MS,
      .write_timeout_ms = TLS_PUBLIC_TIMEOUT_MS,
      .tls_io_buffer_bytes = CNET_TLS_MIN_IO_BUFFER_BYTES,
      .tls_handshake_timeout_ms = TLS_PUBLIC_TIMEOUT_MS};
  if (config.max_send_bytes < 64u * 1024u)
    config.max_send_bytes = 64u * 1024u;
  return config;
}

static int tls_public_drive(tls_public_pair *pair, uint32_t timeout_ms) {
  size_t events = 0u;
  int ready = 0;
  int status;
  if (pair == NULL) return SALTS_EINVAL;

  status = cnet_client_poll(&pair->client, timeout_ms, &events);
  if (status != SALTS_OK) return status;

  if (!pair->accepted) {
    status = cnet_listener_wait(&pair->listener, 0u, &ready);
    if (status != SALTS_OK) return status;
    if (ready != 0) {
      const cnet_observer observer = {
          .on_state = tls_public_on_state,
          .on_receive = tls_public_on_receive,
          .user = &pair->server_probe,
          .on_send = tls_public_on_send};
      status = cnet_listener_accept_tls(
          &pair->listener, &pair->server,
          &pair->tls_server, &observer,
          &pair->server_probe.connection);
      if (status != SALTS_OK) return status;
      pair->accepted = true;
    }
  }

  return cnet_client_poll(&pair->server, timeout_ms, &events);
}

static int tls_public_pair_init(
    tls_public_pair *pair,
    const unsigned char *payload_bytes,
    size_t payload_size,
    int nodelay) {
  static const char *alpn[] = {"http/1.1"};
  cnet_client_config client_config =
      tls_public_config(payload_size);
  cnet_client_config server_config =
      tls_public_config(payload_size);
  cnet_listener_config listener_config = {
      .backend = NATIVE_IO_BACKEND_EPOLL,
      .host = "127.0.0.1",
      .port = 0u,
      .backlog = 1u};
  cnet_tls_server_config server_tls_config = {
      .size = sizeof(server_tls_config),
      .cert_file = CNET_TLS_BENCH_CERT,
      .key_file = CNET_TLS_BENCH_KEY,
      .client_auth = CNET_TLS_CLIENT_AUTH_NONE,
      .alpn_protocols = alpn,
      .alpn_protocol_count = 1u};
  cnet_tls_client_config client_tls_config = {
      .size = sizeof(client_tls_config),
      .ca_file = CNET_TLS_BENCH_CA,
      .server_name = "localhost",
      .alpn_protocols = alpn,
      .alpn_protocol_count = 1u};
  cnet_connect_options options;
  cnet_stream_socket_options socket_options =
      CNET_STREAM_SOCKET_OPTIONS_INIT;
  cnet_connection client_connection = {0};
  uint16_t port = 0u;
  char uri[64];
  uint64_t deadline;
  int status;

  memset(pair, 0, sizeof(*pair));
  pair->client_probe.client = &pair->client;
  pair->server_probe.client = &pair->server;
  pair->client_probe.received =
      (unsigned char *)malloc(payload_size);
  pair->server_probe.received =
      (unsigned char *)malloc(payload_size);
  if (pair->client_probe.received == NULL ||
      pair->server_probe.received == NULL)
    return SALTS_ENOMEM;
  pair->client_probe.capacity = payload_size;
  pair->server_probe.capacity = payload_size;

  pair->payload = mem_get_buffer(mem_global(), payload_size);
  if (pair->payload == NULL) return SALTS_ENOMEM;
  memcpy(mem_buffer_data(pair->payload),
         payload_bytes, payload_size);
  mem_set_used(pair->payload, payload_size);

  status = cnet_tls_server_init(
      &pair->tls_server, &server_tls_config);
  if (status != SALTS_OK) return status;
  pair->tls_server_initialized = true;

  status = cnet_tls_client_init(
      &pair->tls_client, &client_tls_config);
  if (status != SALTS_OK) return status;
  pair->tls_client_initialized = true;

  status = cnet_client_init(&pair->client, &client_config);
  if (status != SALTS_OK) return status;
  pair->client_initialized = true;
  status = cnet_client_init(&pair->server, &server_config);
  if (status != SALTS_OK) return status;
  pair->server_initialized = true;
  socket_options.nodelay = nodelay;
  status = cnet_client_set_stream_socket_options(
      &pair->client, &socket_options);
  if (status != SALTS_OK) return status;
  status = cnet_client_set_stream_socket_options(
      &pair->server, &socket_options);
  if (status != SALTS_OK) return status;
  status = cnet_listener_init(&pair->listener, &listener_config);
  if (status != SALTS_OK) return status;
  pair->listener_initialized = true;
  status = cnet_listener_port(&pair->listener, &port);
  if (status != SALTS_OK || port == 0u)
    return status == SALTS_OK ? SALTS_EPROTO : status;
  if (snprintf(uri, sizeof(uri), "tls://127.0.0.1:%u",
               (unsigned int)port) <= 0)
    return SALTS_EIO;

  options = (cnet_connect_options){
      .uri = uri,
      .observer = {
          .on_state = tls_public_on_state,
          .on_receive = tls_public_on_receive,
          .user = &pair->client_probe,
          .on_send = tls_public_on_send},
      .tls_client = &pair->tls_client};
  status = cnet_connect(
      &pair->client, &options, &client_connection);
  if (status != SALTS_OK) return status;
  pair->client_probe.connection = client_connection;

  status = cnet_tls_client_destroy(&pair->tls_client);
  if (status != SALTS_OK) return status;
  pair->tls_client_initialized = false;

  deadline = salts_monotonic_ms() + TLS_PUBLIC_TIMEOUT_MS;
  while ((!pair->client_probe.connected ||
          !pair->server_probe.connected) &&
         salts_monotonic_ms() < deadline) {
    status = tls_public_drive(pair, 1u);
    if (status != SALTS_OK) return status;
    if (pair->client_probe.failed)
      return pair->client_probe.failure_status;
    if (pair->server_probe.failed)
      return pair->server_probe.failure_status;
  }
  if (!pair->accepted ||
      !pair->client_probe.connected ||
      !pair->server_probe.connected)
    return SALTS_ETIMEDOUT;
  return SALTS_OK;
}

static void tls_public_pair_destroy(tls_public_pair *pair) {
  if (pair == NULL) return;

  if (pair->client_initialized && pair->client_probe.connected &&
      !pair->client_probe.terminal)
    (void)cnet_close(
        &pair->client, pair->client_probe.connection);
  if (pair->server_initialized && pair->server_probe.connected &&
      !pair->server_probe.terminal)
    (void)cnet_close(
        &pair->server, pair->server_probe.connection);
  if (pair->client_initialized && pair->server_initialized) {
    const uint64_t deadline =
        salts_monotonic_ms() + TLS_PUBLIC_TIMEOUT_MS;
    while ((!pair->client_probe.terminal ||
            !pair->server_probe.terminal) &&
           salts_monotonic_ms() < deadline) {
      if (tls_public_drive(pair, 0u) != SALTS_OK) break;
      salts_thread_yield();
    }
  }

  if (pair->listener_initialized) {
    (void)cnet_listener_close(&pair->listener);
    (void)cnet_listener_destroy(&pair->listener);
  }
  if (pair->client_initialized) {
    (void)cnet_client_stop(&pair->client, TLS_PUBLIC_TIMEOUT_MS);
    (void)cnet_client_destroy(&pair->client);
  }
  if (pair->server_initialized) {
    (void)cnet_client_stop(&pair->server, TLS_PUBLIC_TIMEOUT_MS);
    (void)cnet_client_destroy(&pair->server);
  }
  if (pair->tls_client_initialized)
    (void)cnet_tls_client_destroy(&pair->tls_client);
  if (pair->tls_server_initialized)
    (void)cnet_tls_server_destroy(&pair->tls_server);
  if (pair->payload != NULL)
    mem_buffer_release(pair->payload);
  free(pair->client_probe.received);
  free(pair->server_probe.received);
  memset(pair, 0, sizeof(*pair));
}

static size_t tls_public_receive_demand(size_t payload_size) {
  size_t records = (payload_size + 16383u) / 16384u;
  if (records > SIZE_MAX - 4u) return 0u;
  return records + 4u;
}

static int tls_public_one_way(
    tls_public_pair *pair, bool server_to_client) {
  tls_public_probe *source_probe;
  tls_public_probe *target_probe;
  cnet_client *source;
  cnet_client *target;
  size_t sent_before;
  size_t demand;
  uint64_t deadline;
  int status;

  if (pair == NULL) return SALTS_EINVAL;
  source = server_to_client ? &pair->server : &pair->client;
  target = server_to_client ? &pair->client : &pair->server;
  source_probe =
      server_to_client ? &pair->server_probe : &pair->client_probe;
  target_probe =
      server_to_client ? &pair->client_probe : &pair->server_probe;

  target_probe->received_size = 0u;
  demand = tls_public_receive_demand(
      mem_buffer_used(pair->payload));
  if (demand == 0u) return SALTS_ERANGE;
  status = cnet_receive(
      target, target_probe->connection, demand);
  if (status != SALTS_OK) return status;

  sent_before = source_probe->sent_count;
  status = cnet_send_buffer(
      source, source_probe->connection, pair->payload);
  if (status != SALTS_OK) return status;

  deadline = salts_monotonic_ms() + TLS_PUBLIC_TIMEOUT_MS;
  while ((target_probe->received_size <
              mem_buffer_used(pair->payload) ||
          source_probe->sent_count < sent_before + 1u) &&
         salts_monotonic_ms() < deadline) {
    status = tls_public_drive(pair, 0u);
    if (status != SALTS_OK) return status;
    if (pair->client_probe.failed)
      return pair->client_probe.failure_status;
    if (pair->server_probe.failed)
      return pair->server_probe.failure_status;
    salts_thread_yield();
  }

  if (target_probe->received_size !=
          mem_buffer_used(pair->payload) ||
      source_probe->sent_count != sent_before + 1u)
    return SALTS_ETIMEDOUT;
  if (memcmp(target_probe->received,
             mem_buffer_const_data(pair->payload),
             target_probe->received_size) != 0)
    return SALTS_EPROTO;
  return SALTS_OK;
}

static int tls_public_operation(
    tls_public_pair *pair, tls_public_mode mode) {
  int status;
  if (mode == TLS_PUBLIC_PUSH)
    return tls_public_one_way(pair, true);
  status = tls_public_one_way(pair, false);
  if (status != SALTS_OK) return status;
  return tls_public_one_way(pair, true);
}

static void tls_public_worker_run(void *user) {
  tls_public_worker *worker = (tls_public_worker *)user;
  tls_public_shared *shared;
  size_t index;
  int status = SALTS_OK;

  if (worker == NULL || worker->shared == NULL) return;
  shared = worker->shared;

  for (index = 0u; index < worker->pair_count; ++index) {
    status = tls_public_pair_init(
        &worker->pairs[index],
        shared->payload_bytes, shared->payload_size,
        shared->nodelay);
    if (status != SALTS_OK) break;
  }

  if (status == SALTS_OK) {
    for (size_t warm = 0u;
         warm < shared->warmup_rounds_per_pair; ++warm) {
      for (size_t pair_index = 0u;
           pair_index < worker->pair_count; ++pair_index) {
        status = tls_public_operation(
            &worker->pairs[pair_index], shared->mode);
        if (status != SALTS_OK) break;
      }
      if (status != SALTS_OK) break;
    }
  }

  if (status == SALTS_OK) {
    for (index = 0u; index < worker->pair_count; ++index) {
      status = cnet_client_profile_begin(&worker->pairs[index].client);
      if (status != SALTS_OK) break;
      worker->pairs[index].client_profile_active = true;
      status = cnet_client_profile_begin(&worker->pairs[index].server);
      if (status != SALTS_OK) break;
      worker->pairs[index].server_profile_active = true;
    }
  }

  tls_public_set_error(shared, status);
  atomic_fetch_add_explicit(
      &shared->ready, 1u, memory_order_release);
  while (!atomic_load_explicit(
      &shared->start, memory_order_acquire))
    salts_thread_yield();

  if (status == SALTS_OK) {
    const uint64_t cpu_started_ns =
        tls_public_thread_cpu_ns();
    for (index = 0u;
         index < shared->ops_per_owner; ++index) {
      tls_public_pair *pair =
          &worker->pairs[index % worker->pair_count];
      const uint64_t started_ns = salts_hrtime();
      status = tls_public_operation(pair, shared->mode);
      if (status != SALTS_OK) {
        tls_public_set_error(shared, status);
        break;
      }
      worker->latencies_ns[index] =
          salts_hrtime() - started_ns;
    }
    {
      const uint64_t cpu_finished_ns =
          tls_public_thread_cpu_ns();
      worker->cpu_ns =
          cpu_finished_ns >= cpu_started_ns
              ? cpu_finished_ns - cpu_started_ns
              : 0u;
    }
  }

  for (index = 0u; index < worker->pair_count; ++index) {
    cnet_client_poll_profile observed = {0};
    if (worker->pairs[index].client_profile_active) {
      const int profile_status =
          cnet_client_profile_take(&worker->pairs[index].client, &observed);
      worker->pairs[index].client_profile_active = false;
      if (profile_status == SALTS_OK)
        tls_public_profile_add(&worker->client_profile, &observed.owner);
      else
        tls_public_set_error(shared, profile_status);
    }
    observed = (cnet_client_poll_profile){0};
    if (worker->pairs[index].server_profile_active) {
      const int profile_status =
          cnet_client_profile_take(&worker->pairs[index].server, &observed);
      worker->pairs[index].server_profile_active = false;
      if (profile_status == SALTS_OK)
        tls_public_profile_add(&worker->server_profile, &observed.owner);
      else
        tls_public_set_error(shared, profile_status);
    }
  }

  atomic_fetch_add_explicit(
      &shared->measured_done, 1u, memory_order_release);
  while (!atomic_load_explicit(
      &shared->cleanup, memory_order_acquire))
    salts_thread_yield();

  for (index = 0u; index < worker->pair_count; ++index)
    tls_public_pair_destroy(&worker->pairs[index]);
}

static const char *tls_public_mode_name(tls_public_mode mode) {
  return mode == TLS_PUBLIC_ECHO ? "echo" : "push";
}

int main(int argc, char **argv) {
  tls_public_shared shared;
  tls_public_worker workers[TLS_PUBLIC_MAX_OWNERS];
  salts_thread_t threads[TLS_PUBLIC_MAX_OWNERS] = {0};
  bool thread_started[TLS_PUBLIC_MAX_OWNERS] = {false};
  unsigned char *payload = NULL;
  uint64_t *latencies = NULL;
  size_t payload_size;
  size_t owner_count;
  size_t total_ops;
  size_t ops_per_owner;
  size_t warmup_rounds_per_pair;
  size_t samples = 0u;
  uint64_t total_cpu_ns = 0u;
  cnet_owner_profile client_profile = {0};
  cnet_owner_profile server_profile = {0};
  uint64_t started_ns;
  uint64_t wall_ns;
  tls_public_mode mode;
  const char *temperature;
  const char *nodelay_text;
  int nodelay = 0;
  int status = SALTS_OK;

  if (argc != 5) {
    fprintf(stderr,
            "usage: %s <push|echo> <payload-bytes> <owners> <fresh|warm>\n",
            argv[0]);
    return 2;
  }

  if (strcmp(argv[1], "push") == 0)
    mode = TLS_PUBLIC_PUSH;
  else if (strcmp(argv[1], "echo") == 0)
    mode = TLS_PUBLIC_ECHO;
  else
    return 2;

  if (tls_public_parse_size(
          argv[2], 1u, 64u * 1024u, &payload_size) != SALTS_OK ||
      tls_public_parse_size(
          argv[3], 1u, TLS_PUBLIC_MAX_OWNERS,
          &owner_count) != SALTS_OK ||
      (owner_count != 1u && owner_count != 2u &&
       owner_count != 4u) ||
      TLS_PUBLIC_PAIRS % owner_count != 0u)
    return 2;

  if (strcmp(argv[4], "fresh") == 0) {
    temperature = "fresh";
    warmup_rounds_per_pair = 0u;
  } else if (strcmp(argv[4], "warm") == 0) {
    temperature = "warm";
    warmup_rounds_per_pair =
        TLS_PUBLIC_WARMUP_ROUNDS_PER_PAIR;
  } else {
    return 2;
  }

  nodelay_text = getenv("CNET_TLS_PUBLIC_NODELAY");
  if (nodelay_text != NULL && nodelay_text[0] != '\0') {
    if (strcmp(nodelay_text, "0") == 0)
      nodelay = 0;
    else if (strcmp(nodelay_text, "1") == 0)
      nodelay = 1;
    else
      return 2;
  }

  total_ops = tls_public_env_count(
      "CNET_TLS_PUBLIC_OWNER_OPS",
      TLS_PUBLIC_DEFAULT_OPS);
  if (total_ops < owner_count) total_ops = owner_count;
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
        (unsigned char)((index * 53u + payload_size) & 0xffu);

  memset(&shared, 0, sizeof(shared));
  shared.payload_bytes = payload;
  shared.payload_size = payload_size;
  shared.ops_per_owner = ops_per_owner;
  shared.warmup_rounds_per_pair =
      warmup_rounds_per_pair;
  shared.nodelay = nodelay;
  shared.mode = mode;
  atomic_init(&shared.ready, 0u);
  atomic_init(&shared.measured_done, 0u);
  atomic_init(&shared.start, false);
  atomic_init(&shared.cleanup, false);
  atomic_init(&shared.first_error, SALTS_OK);

  memset(workers, 0, sizeof(workers));
  {
    size_t created_count = 0u;
    for (size_t owner = 0u; owner < owner_count; ++owner) {
      workers[owner].shared = &shared;
      workers[owner].pair_count =
          TLS_PUBLIC_PAIRS / owner_count;
      workers[owner].latencies_ns =
          latencies + owner * ops_per_owner;
      status = salts_thread_create(
          &threads[owner], tls_public_worker_run,
          &workers[owner]);
      if (status != SALTS_OK) {
        tls_public_set_error(&shared, status);
        break;
      }
      thread_started[owner] = true;
      ++created_count;
    }
    if (created_count != owner_count) {
      atomic_store_explicit(
          &shared.start, true, memory_order_release);
      atomic_store_explicit(
          &shared.cleanup, true, memory_order_release);
      for (size_t owner = 0u;
           owner < created_count; ++owner) {
        (void)salts_thread_join(&threads[owner]);
        salts_thread_destroy(&threads[owner]);
      }
      status = status == SALTS_OK ? SALTS_EIO : status;
      goto cleanup;
    }
  }

  while (atomic_load_explicit(
             &shared.ready, memory_order_acquire) <
         owner_count)
    salts_thread_yield();

  status = atomic_load_explicit(
      &shared.first_error, memory_order_acquire);
  started_ns = salts_hrtime();
  atomic_store_explicit(
      &shared.start, true, memory_order_release);

  while (atomic_load_explicit(
             &shared.measured_done, memory_order_acquire) <
         owner_count)
    salts_thread_yield();
  wall_ns = salts_hrtime() - started_ns;

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
    total_cpu_ns += workers[owner].cpu_ns;
    tls_public_profile_add(&client_profile, &workers[owner].client_profile);
    tls_public_profile_add(&server_profile, &workers[owner].server_profile);
    for (size_t index = 0u;
         index < ops_per_owner; ++index)
      if (workers[owner].latencies_ns[index] != 0u)
        ++samples;
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
            ? (double)total_cpu_ns / (double)total_ops
            : 0.0;
    printf(
        "{\"benchmark\":\"cnet_tls_public_loopback_parallel\","
        "\"mode\":\"%s\","
        "\"temperature\":\"%s\","
        "\"nodelay\":%d,"
        "\"payload_bytes\":%zu,"
        "\"owners\":%zu,"
        "\"pairs\":%u,"
        "\"operations\":%zu,"
        "\"samples\":%zu,"
        "\"ops_per_second\":%.3f,"
        "\"owner_cpu_ns_per_op\":%.3f,"
        "\"server_tls_pump_ns_per_op\":%.3f,"
        "\"client_tls_pump_ns_per_op\":%.3f,"
        "\"server_tls_write_submits\":%llu,"
        "\"server_tls_write_submit_bytes\":%llu,"
        "\"server_tls_write_completions\":%llu,"
        "\"server_tls_write_completion_bytes\":%llu,"
        "\"client_tls_read_completions\":%llu,"
        "\"client_tls_read_completion_bytes\":%llu,"
        "\"client_tls_plaintext_receive_calls\":%llu,"
        "\"client_tls_plaintext_receive_bytes\":%llu,"
        "\"p50_ns\":%llu,"
        "\"p95_ns\":%llu,"
        "\"p99_ns\":%llu,"
        "\"errors\":0}\n",
        tls_public_mode_name(mode), temperature,
        nodelay, payload_size, owner_count, TLS_PUBLIC_PAIRS,
        total_ops, samples, operations_per_second,
        cpu_ns_per_op,
        total_ops != 0u
            ? (double)server_profile.tls_pump_ns / (double)total_ops
            : 0.0,
        total_ops != 0u
            ? (double)client_profile.tls_pump_ns / (double)total_ops
            : 0.0,
        (unsigned long long)server_profile.tls_write_submit_calls,
        (unsigned long long)server_profile.tls_write_submit_bytes,
        (unsigned long long)server_profile.tls_write_completion_calls,
        (unsigned long long)server_profile.tls_write_completion_bytes,
        (unsigned long long)client_profile.tls_read_completion_calls,
        (unsigned long long)client_profile.tls_read_completion_bytes,
        (unsigned long long)client_profile.tls_plaintext_receive_calls,
        (unsigned long long)client_profile.tls_plaintext_receive_bytes,
        (unsigned long long)tls_public_percentile(
            latencies, samples, 50u),
        (unsigned long long)tls_public_percentile(
            latencies, samples, 95u),
        (unsigned long long)tls_public_percentile(
            latencies, samples, 99u));
  }

cleanup:
  free(latencies);
  free(payload);
  if (status != SALTS_OK) {
    fprintf(stderr,
            "public CNet TLS loopback benchmark failed: status=%d\n",
            status);
    return 1;
  }
  return 0;
}
