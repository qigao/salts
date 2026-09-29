#include "cnet_tls.h"
#include "cnet_write_queue.h"

#define TINYTEST_NO_MAIN
#include "tinytest.h"

#include <salts/clock.h>
#include <salts/error_codes.h>

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  TLS_BENCH_REPLICATES = 11,
  TLS_BENCH_DEFAULT_ITERATIONS = 50,
  TLS_BENCH_DEFAULT_WARMUP = 8,
  TLS_BENCH_MAX_SEGMENTS = 16,
  TLS_BENCH_CIPHER_BUFFER = 65536
};

static const char CNET_TLS_TEST_CERTIFICATE[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC7TCCAdWgAwIBAgIUT4pOT+qAkLpsC1bUF3bYRrTHssQwDQYJKoZIhvcNAQEL\n"
    "BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDMyMzA4MDcwMloXDTM2MDMy\n"
    "MDA4MDcwMlowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF\n"
    "AAOCAQ8AMIIBCgKCAQEAtNuutQlZVrXBW97HX5HfMXbMkES9n2eglXQRzU7Qg4Mm\n"
    "KtAprpkBVSFHeAti0NyPgasoaoJTBi1xBhDsGTWTto0TJVHhW5QcYSPRc8x/acWQ\n"
    "NxBSMdWf8Rp9QxbaECyQbWr+QDb/c1a9QU0fGFntQBnLfk9lLJG7MRTwg38ufnSk\n"
    "OqqyAtbT4V5ZwImkOo9MdECcZMvRDYnvH1atIUvGRI7O3M466jGe+5WN4E42h8VN\n"
    "PSJw2IBvbFxePZ3yMWpiVRkbsWlq1hJIHGvnGD+4IPGr2nB/FmR+P969KFm/gSvG\n"
    "L9tYFRw36Cfa+cnwWAYNpLspwOaaAQcpeMN8tAGIPwIDAQABozcwNTAUBgNVHREE\n"
    "DTALgglsb2NhbGhvc3QwHQYDVR0OBBYEFHSKGrYW6d59EU5htbnpgVhPLaQiMA0G\n"
    "CSqGSIb3DQEBCwUAA4IBAQBhIzu8IJ7Pm30nKOfvwgQRKbJDWIBKZz/NYoIP5Ljm\n"
    "fZG+ZZT0BnuCObKTvPwAWERwbIn5cIDNCkVKhQoJc4+KqR9fXptxML+Q3e4lCVo3\n"
    "5jjQpG/r18aZxhfroinp6iCfGcECw/JAXPxC8jOhEgVOPQd/LybM9vO8vraH/dIR\n"
    "YRmIoBvGw+wQMt/PcV0GxYLo6LsYJFs0FuJyiufJ2auNtmW5h8qOdtnagmeo0ehp\n"
    "g5VqPlB3EMa/01r9WmfNQJcBbEF8ONhhPXZCV4uplsXGtN8+Xxrzb3SAYQR9xFry\n"
    "x9YTzT8UMLc26vY1RiF6uwODUJzmSaqmefmapVsWrgi3\n"
    "-----END CERTIFICATE-----\n";

static const char CNET_TLS_TEST_KEY[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQC02661CVlWtcFb\n"
    "3sdfkd8xdsyQRL2fZ6CVdBHNTtCDgyYq0CmumQFVIUd4C2LQ3I+BqyhqglMGLXEG\n"
    "EOwZNZO2jRMlUeFblBxhI9FzzH9pxZA3EFIx1Z/xGn1DFtoQLJBtav5ANv9zVr1B\n"
    "TR8YWe1AGct+T2UskbsxFPCDfy5+dKQ6qrIC1tPhXlnAiaQ6j0x0QJxky9ENie8f\n"
    "Vq0hS8ZEjs7czjrqMZ77lY3gTjaHxU09InDYgG9sXF49nfIxamJVGRuxaWrWEkgc\n"
    "a+cYP7gg8avacH8WZH4/3r0oWb+BK8Yv21gVHDfoJ9r5yfBYBg2kuynA5poBByl4\n"
    "w3y0AYg/AgMBAAECggEAEJkoy4yexQp2mHaLAwZhiX9G/uaQJepeHoPsg6nRZoB0\n"
    "JvG7zD5WlPgyQEjV5NKZM7lVmDt7Cydt0V9e4QwTERSZcToL3gUV0FnNMJIlZLuw\n"
    "+fIRg76rUyFZ5aevPlTDXIdj64N1+6E2SqFH/UrOL1fZXoTthXhKdGgLkBtCqnA6\n"
    "DlHQX3lehrnV+MG5fTxPc8lro/s4UVAoBMhc4dP5U1W5Xt5c6RsdcWYytidRYj8t\n"
    "XMkyjST/F2NV80+8WGp/YFE0dHyxGWvLGNmkOUuI4EMwzzSadsIM+PQO/YP1KwHA\n"
    "0DYHuEFvPCLjPsD+7IUnZgifQe45/FJoJMp5hSmzgQKBgQD7XEl2mLR3iqup2dF+\n"
    "PD3zA2J48jdiJdbK7vRLXpdV5WP2/s90GZFKLadg7UWmx9zWkC4B92atNJV0/+8o\n"
    "wE4Zd8PG62QZ3o1T4QpYMem9PAq5OxqwYBxMZ2Y5Mf+54Gp0SXB+AbXPlYI/LIwP\n"
    "i/2Iq+bAjGmuGuloJNWD3Wl3DwKBgQC4MkMYvf5aSqbL8GE5ndKY06HzbxwcMoh3\n"
    "Hia5LRMw5dG3J2JwdruiE4V3gQyqz0NzYrrqqkyYxh3aJW934qj6JVMVw/xWx2n5\n"
    "xB4X4hcCKrO2piROmOuXBEt1T36C+fShNb8g+RNY0edoiw+OKTa3rzlQhggTkoGs\n"
    "Iy7oyxtb0QKBgGKkgfP304LCOcHrSCppC8qtflyGebObs+Jpyhc15OABqKxKrTEb\n"
    "w4e/yNrh4p6j+od9h4CgDXxVkX2b3sg4R6348SzEPcFlNENBomSgGeF4iaDNkBi9\n"
    "bv2Q6m3xsDDK4BwIogvhMe9n9fhCzChhwLp8846GzAZWa1jCc8RPBM+DAoGAQxRy\n"
    "4QDYL5O+OMka7zutpWB1O008hHxWvGKroYZr1cPsYvIh5GkpHfZUBdhmf5Ips0zC\n"
    "W5GXgY+s8XPuq09NUIPlRSjxrbzDuGUWvIXm8TAR8LOCx2jja0TyIg/IN/TFhSwo\n"
    "pd5vkEopJyZ1jMUvmydiDRQyvsX9GW5auAa3uPECgYBxuBJ6Vji7pxlqjG3aB0je\n"
    "+JexLyzdckU7EKTxpTSU1o/p17QpT26KF+DPMc2kg+PBK+Sjm0m4Uxdzq/OXNMMA\n"
    "zhR6Vjo1nPWsKgzK03hGzaJVMkHekgCidY9R+MZEeDAhHDIia9XyAS1qCoGAJ6WC\n"
    "oYB4EuDLFhurWiLO+diuMg==\n"
    "-----END PRIVATE KEY-----\n";

typedef struct tls_bench_pair {
  cnet_tls_server server_context;
  cnet_tls_state client;
  cnet_tls_state server;
  char *cert_path;
  char *key_path;
} tls_bench_pair;

typedef struct tls_bench_sample {
  double ns_per_op;
  double bytes_per_second;
  double tls_write_calls_per_op;
  double cipher_bytes_per_op;
} tls_bench_sample;

typedef struct tls_bench_summary {
  const char *style;
  size_t payload_bytes;
  size_t segment_count;
  size_t iterations;
  size_t replicates;
  double p50_ns_per_op;
  double p95_ns_per_op;
  double median_bytes_per_second;
  double median_tls_write_calls_per_op;
  double median_cipher_bytes_per_op;
} tls_bench_summary;

static size_t tls_bench_env_count(const char *name, size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u || parsed > 1000000ull) return fallback;
  return (size_t)parsed;
}

static int tls_bench_transfer(cnet_tls_state *source, cnet_tls_state *target) {
  unsigned char buffer[TLS_BENCH_CIPHER_BUFFER];
  for (;;) {
    size_t size = 0u;
    int status = cnet_tls_take_cipher(source, buffer, sizeof(buffer), &size);
    if (status == SALTS_ENOENT) return SALTS_OK;
    if (status != SALTS_OK) return status;
    if (size == 0u) return SALTS_EPROTO;
    status = cnet_tls_feed_cipher(target, buffer, size);
    if (status != SALTS_OK) return status;
  }
}

static int tls_bench_drain_cipher(cnet_tls_state *state, uint64_t *out_bytes) {
  unsigned char buffer[TLS_BENCH_CIPHER_BUFFER];
  uint64_t total = 0u;
  for (;;) {
    size_t size = 0u;
    int status = cnet_tls_take_cipher(state, buffer, sizeof(buffer), &size);
    if (status == SALTS_ENOENT) {
      if (out_bytes != NULL) *out_bytes = total;
      return SALTS_OK;
    }
    if (status != SALTS_OK) return status;
    if (size == 0u) return SALTS_EPROTO;
    if (UINT64_MAX - total < size) return SALTS_ERANGE;
    total += size;
  }
}

static int tls_bench_pair_init(tls_bench_pair *pair) {
  cnet_tls_server_config server_config;
  cnet_tls_client_config client_config;
  cnet_tls_context *client_context = NULL;
  cnet_tls_context *server_context;
  int status;

  memset(pair, 0, sizeof(*pair));
  pair->cert_path = tt_make_temp_file("cnet-tls-bench-cert", ".pem");
  pair->key_path = tt_make_temp_file("cnet-tls-bench-key", ".pem");
  if (pair->cert_path == NULL || pair->key_path == NULL) return SALTS_ENOMEM;
  if (tt_write_file(pair->cert_path, CNET_TLS_TEST_CERTIFICATE,
                    sizeof(CNET_TLS_TEST_CERTIFICATE) - 1u) != 0 ||
      tt_write_file(pair->key_path, CNET_TLS_TEST_KEY,
                    sizeof(CNET_TLS_TEST_KEY) - 1u) != 0)
    return SALTS_EIO;

  server_config = (cnet_tls_server_config){.size = sizeof(server_config),
                                           .cert_file = pair->cert_path,
                                           .key_file = pair->key_path,
                                           .client_auth = CNET_TLS_CLIENT_AUTH_NONE};
  status = cnet_tls_server_init(&pair->server_context, &server_config);
  if (status != SALTS_OK) return status;

  client_config = (cnet_tls_client_config){.size = sizeof(client_config),
                                           .ca_file = pair->cert_path};
  status = cnet_tls_client_context_create(&client_config, &client_context);
  if (status != SALTS_OK) return status;
  status = cnet_tls_state_init(&pair->client, client_context, false, "localhost",
                               CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) {
    cnet_tls_context_release(client_context);
    return status;
  }

  server_context = cnet_tls_server_context(&pair->server_context);
  cnet_tls_context_retain(server_context);
  status = cnet_tls_state_init(&pair->server, server_context, true, NULL,
                               CNET_TLS_MIN_IO_BUFFER_BYTES);
  if (status != SALTS_OK) cnet_tls_context_release(server_context);
  return status;
}

static int tls_bench_handshake(tls_bench_pair *pair) {
  for (size_t iteration = 0u; iteration < 256u; ++iteration) {
    bool client_complete = false;
    bool server_complete = false;
    int status = cnet_tls_handshake(&pair->client, &client_complete);
    if (status != SALTS_OK) return status;
    status = tls_bench_transfer(&pair->client, &pair->server);
    if (status != SALTS_OK) return status;
    status = cnet_tls_handshake(&pair->server, &server_complete);
    if (status != SALTS_OK) return status;
    status = tls_bench_transfer(&pair->server, &pair->client);
    if (status != SALTS_OK) return status;
    if (client_complete && server_complete) return SALTS_OK;
  }
  return SALTS_ETIMEDOUT;
}

static void tls_bench_pair_destroy(tls_bench_pair *pair) {
  cnet_tls_state_destroy(&pair->server);
  cnet_tls_state_destroy(&pair->client);
  (void)cnet_tls_server_destroy(&pair->server_context);
  if (pair->cert_path != NULL) {
    (void)tt_remove_file(pair->cert_path);
    free(pair->cert_path);
  }
  if (pair->key_path != NULL) {
    (void)tt_remove_file(pair->key_path);
    free(pair->key_path);
  }
  memset(pair, 0, sizeof(*pair));
}

static int tls_bench_write_complete(cnet_tls_state *tls, const void *data, size_t size,
                                    uint64_t *io_calls, uint64_t *cipher_bytes) {
  bool complete = false;
  while (!complete) {
    uint64_t drained = 0u;
    int status = cnet_tls_write(tls, data, size, &complete);
    if (io_calls != NULL) ++*io_calls;
    if (status != SALTS_OK) return status;
    status = tls_bench_drain_cipher(tls, &drained);
    if (status != SALTS_OK) return status;
    if (cipher_bytes != NULL) {
      if (UINT64_MAX - *cipher_bytes < drained) return SALTS_ERANGE;
      *cipher_bytes += drained;
    }
  }
  return SALTS_OK;
}

static int tls_bench_queue_init(cnet_write_queue *queue) {
  const cnet_write_queue_config config = {
      .connection_capacity = 1u,
      .capacity = 4u,
      .max_payload_bytes = 65536u};
  return cnet_write_queue_init(queue, &config);
}

static int tls_bench_iteration(tls_bench_pair *pair, cnet_write_queue *queue,
                               cnet_session_handle connection, mem_buffer_t *contiguous,
                               const mem_slice_t *segments, size_t segment_count,
                               uint64_t *io_calls, uint64_t *cipher_bytes) {
  cnet_write_handle handle = {0};
  cnet_write_view view = {0};
  int status;

  if (segment_count == 1u) {
    status = cnet_write_queue_enqueue_buffer(queue, connection, contiguous, false, &handle);
  } else {
    status = cnet_write_queue_enqueue_slicev(queue, connection, segments, segment_count,
                                             false, &handle);
  }
  if (status != SALTS_OK) return status;
  status = cnet_write_queue_peek(queue, connection, &view);
  if (status != SALTS_OK) return status;

  if (!view.vector_write) {
    status = tls_bench_write_complete(&pair->client, view.data, view.remaining,
                                      io_calls, cipher_bytes);
    if (status != SALTS_OK) return status;
  } else {
    while (view.remaining != 0u) {
      native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX];
      size_t span_count = 0u;
      size_t span_bytes = 0u;
      status = cnet_write_queue_build_vector(queue, &view, (size_t)INT_MAX,
                                             spans, &span_count, &span_bytes);
      if (status != SALTS_OK) return status;
      if (span_count == 0u || span_bytes == 0u) return SALTS_EPROTO;
      for (size_t index = 0u; index < span_count; ++index) {
        status = tls_bench_write_complete(&pair->client, spans[index].data,
                                          spans[index].length, io_calls, cipher_bytes);
        if (status != SALTS_OK) return status;
        status = cnet_write_queue_advance(queue, &view, spans[index].length);
        if (status != SALTS_OK) return status;
      }
    }
  }

  return cnet_write_queue_settle(queue, &view);
}

static int tls_bench_compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double tls_bench_percentile(double *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), tls_bench_compare_double);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int tls_bench_run(size_t payload_bytes, size_t segment_count,
                         size_t warmup, size_t iterations, tls_bench_summary *out) {
  tls_bench_pair pair;
  cnet_write_queue queue = {0};
  cnet_session_handle connection = {1u, 1u};
  mem_buffer_t *contiguous = NULL;
  mem_buffer_t *segment_buffers[TLS_BENCH_MAX_SEGMENTS] = {0};
  mem_slice_t slices[TLS_BENCH_MAX_SEGMENTS] = {{0}};
  tls_bench_sample samples[TLS_BENCH_REPLICATES] = {{0}};
  double latency[TLS_BENCH_REPLICATES];
  double throughput[TLS_BENCH_REPLICATES];
  double calls[TLS_BENCH_REPLICATES];
  double cipher[TLS_BENCH_REPLICATES];
  int status = SALTS_OK;

  memset(&pair, 0, sizeof(pair));
  if (segment_count == 0u || segment_count > TLS_BENCH_MAX_SEGMENTS ||
      payload_bytes == 0u || payload_bytes % segment_count != 0u)
    return SALTS_EINVAL;

  status = tls_bench_pair_init(&pair);
  if (status != SALTS_OK) goto cleanup;
  status = tls_bench_handshake(&pair);
  if (status != SALTS_OK) goto cleanup;
  status = tls_bench_queue_init(&queue);
  if (status != SALTS_OK) goto cleanup;

  if (segment_count == 1u) {
    contiguous = mem_get_buffer(mem_global(), payload_bytes);
    if (contiguous == NULL) { status = SALTS_ENOMEM; goto cleanup; }
    memset(mem_buffer_data(contiguous), 0x5a, payload_bytes);
    mem_set_used(contiguous, payload_bytes);
  } else {
    const size_t segment_bytes = payload_bytes / segment_count;
    for (size_t index = 0u; index < segment_count; ++index) {
      segment_buffers[index] = mem_get_buffer(mem_global(), segment_bytes);
      if (segment_buffers[index] == NULL) { status = SALTS_ENOMEM; goto cleanup; }
      memset(mem_buffer_data(segment_buffers[index]), (int)(0x41u + (index % 23u)),
             segment_bytes);
      mem_set_used(segment_buffers[index], segment_bytes);
      slices[index] = mem_slice(segment_buffers[index], 0u, segment_bytes);
      if (slices[index].buffer == NULL) { status = SALTS_ENOMEM; goto cleanup; }
    }
  }

  for (size_t index = 0u; index < warmup; ++index) {
    uint64_t io_calls = 0u;
    uint64_t cipher_bytes = 0u;
    status = tls_bench_iteration(&pair, &queue, connection, contiguous,
                                 slices, segment_count, &io_calls, &cipher_bytes);
    if (status != SALTS_OK) goto cleanup;
  }

  for (size_t replicate = 0u; replicate < TLS_BENCH_REPLICATES; ++replicate) {
    uint64_t io_calls = 0u;
    uint64_t cipher_bytes = 0u;
    const uint64_t started = salts_hrtime();
    for (size_t index = 0u; index < iterations; ++index) {
      status = tls_bench_iteration(&pair, &queue, connection, contiguous,
                                   slices, segment_count, &io_calls, &cipher_bytes);
      if (status != SALTS_OK) goto cleanup;
    }
    {
      const uint64_t elapsed = salts_hrtime() - started;
      samples[replicate].ns_per_op = (double)elapsed / (double)iterations;
      samples[replicate].bytes_per_second =
          elapsed == 0u ? 0.0 : (double)payload_bytes * (double)iterations * 1.0e9 /
                                      (double)elapsed;
      samples[replicate].tls_write_calls_per_op = (double)io_calls / (double)iterations;
      samples[replicate].cipher_bytes_per_op = (double)cipher_bytes / (double)iterations;
      latency[replicate] = samples[replicate].ns_per_op;
      throughput[replicate] = samples[replicate].bytes_per_second;
      calls[replicate] = samples[replicate].tls_write_calls_per_op;
      cipher[replicate] = samples[replicate].cipher_bytes_per_op;
    }
  }

  {
    cnet_write_queue_stats stats = {0};
    if (!cnet_write_queue_get_stats(&queue, &stats) || stats.live_writes != 0u) {
      status = SALTS_EPROTO;
      goto cleanup;
    }
  }

  *out = (tls_bench_summary){
      segment_count == 1u ? "retained_contiguous" : "retained_slicev",
      payload_bytes,
      segment_count,
      iterations,
      TLS_BENCH_REPLICATES,
      tls_bench_percentile(latency, TLS_BENCH_REPLICATES, 50u),
      tls_bench_percentile(latency, TLS_BENCH_REPLICATES, 95u),
      tls_bench_percentile(throughput, TLS_BENCH_REPLICATES, 50u),
      tls_bench_percentile(calls, TLS_BENCH_REPLICATES, 50u),
      tls_bench_percentile(cipher, TLS_BENCH_REPLICATES, 50u)};

cleanup:
  for (size_t index = 0u; index < TLS_BENCH_MAX_SEGMENTS; ++index) {
    mem_slice_release(&slices[index]);
    if (segment_buffers[index] != NULL) mem_buffer_release(segment_buffers[index]);
  }
  if (contiguous != NULL) mem_buffer_release(contiguous);
  if (queue.impl != NULL) {
    const int close_status = cnet_write_queue_close(&queue);
    const int destroy_status = close_status == SALTS_OK ? cnet_write_queue_destroy(&queue)
                                                        : close_status;
    if (status == SALTS_OK) status = destroy_status;
  }
  tls_bench_pair_destroy(&pair);
  return status;
}

static FILE *tls_bench_open_csv(void) {
  const char *prefix = getenv("CNET_TLS_SG_BENCH_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void tls_bench_print(FILE *stream, const char *backend,
                            const tls_bench_summary *row) {
  fprintf(stream, "%s,%s,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f\n",
          backend, row->style, row->payload_bytes, row->segment_count,
          row->iterations, row->replicates, row->p50_ns_per_op, row->p95_ns_per_op,
          row->median_bytes_per_second, row->median_tls_write_calls_per_op,
          row->median_cipher_bytes_per_op);
}

int main(void) {
  static const size_t payloads[] = {1024u, 8192u, 32768u, 65536u};
  static const size_t segment_counts[] = {1u, 2u, 4u, 8u, 16u};
  const char *backend = getenv("CNET_TLS_SG_BENCH_BACKEND");
  const size_t warmup = tls_bench_env_count("CNET_TLS_SG_BENCH_WARMUP",
                                             TLS_BENCH_DEFAULT_WARMUP);
  const size_t iterations = tls_bench_env_count("CNET_TLS_SG_BENCH_ITERATIONS",
                                                 TLS_BENCH_DEFAULT_ITERATIONS);
  FILE *csv = tls_bench_open_csv();

  if (backend == NULL || *backend == '\0') backend = "unknown";
  if (csv != NULL) {
    fprintf(csv,
            "backend,style,payload_bytes,segment_count,iterations_per_replicate,replicates,"
            "p50_ns_per_op,p95_ns_per_op,median_bytes_per_second,"
            "median_tls_write_calls_per_op,median_cipher_bytes_per_op\n");
  }

  printf("| backend | style | payload | segments | p50 ns/op | p95 ns/op | median MiB/s | TLS writes/op | cipher bytes/op |\n");
  printf("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");

  for (size_t p = 0u; p < sizeof(payloads) / sizeof(payloads[0]); ++p) {
    for (size_t sidx = 0u; sidx < sizeof(segment_counts) / sizeof(segment_counts[0]); ++sidx) {
      tls_bench_summary row = {0};
      const int status = tls_bench_run(payloads[p], segment_counts[sidx],
                                       warmup, iterations, &row);
      if (status != SALTS_OK) {
        if (csv != NULL) fclose(csv);
        fprintf(stderr, "TLS SG benchmark failed payload=%zu segments=%zu status=%d\n",
                payloads[p], segment_counts[sidx], status);
        return 1;
      }
      printf("| %s | %s | %zu | %zu | %.3f | %.3f | %.2f | %.3f | %.1f |\n",
             backend, row.style, row.payload_bytes, row.segment_count,
             row.p50_ns_per_op, row.p95_ns_per_op,
             row.median_bytes_per_second / (1024.0 * 1024.0),
             row.median_tls_write_calls_per_op, row.median_cipher_bytes_per_op);
      if (csv != NULL) tls_bench_print(csv, backend, &row);
    }
  }

  if (csv != NULL) fclose(csv);
  return 0;
}
