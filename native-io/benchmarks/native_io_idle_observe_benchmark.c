#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
  IDLE_OBSERVE_DEFAULT_ITERATIONS = 2000000,
  IDLE_OBSERVE_DEFAULT_WARMUP_ITERATIONS = 10000
};

static uint64_t idle_observe_thread_cpu_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static size_t idle_observe_count(const char *name, size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u || parsed > 100000000ull)
    return fallback;
  return (size_t)parsed;
}

static FILE *idle_observe_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_IDLE_OBSERVE_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

int main(void) {
  native_io_backend backend = {0};
  const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};
  native_io_completion event = {0};
  const size_t iterations =
      idle_observe_count("NATIVE_IO_IDLE_OBSERVE_ITERATIONS",
                         IDLE_OBSERVE_DEFAULT_ITERATIONS);
  const size_t warmup =
      idle_observe_count("NATIVE_IO_IDLE_OBSERVE_WARMUP",
                         IDLE_OBSERVE_DEFAULT_WARMUP_ITERATIONS);
  uint64_t wall_started;
  uint64_t cpu_started;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  FILE *csv;
  int status;

  if (!native_io_backend_kind_supported(NATIVE_IO_BACKEND_IO_URING)) {
    fprintf(stderr, "io_uring backend is not supported\n");
    return 2;
  }

  status = native_io_backend_init(&backend, &config);
  if (status != SALTS_OK) {
    fprintf(stderr, "native_io_backend_init failed: %d\n", status);
    return 1;
  }

  for (size_t i = 0u; i < warmup; ++i) {
    size_t count = SIZE_MAX;
    status = native_io_backend_observe(&backend, &event, 1u, 0u, &count);
    if (status != SALTS_ETIMEDOUT || count != 0u) {
      fprintf(stderr, "warmup observe failed: status=%d count=%zu\n", status, count);
      (void)native_io_backend_close(&backend);
      (void)native_io_backend_destroy(&backend);
      return 1;
    }
  }

  fprintf(stderr, "NATIVE_IO_IDLE_OBSERVE_BEGIN iterations=%zu\n", iterations);
  fflush(stderr);
  wall_started = salts_hrtime();
  cpu_started = idle_observe_thread_cpu_ns();

  for (size_t i = 0u; i < iterations; ++i) {
    size_t count = SIZE_MAX;
    status = native_io_backend_observe(&backend, &event, 1u, 0u, &count);
    if (status != SALTS_ETIMEDOUT || count != 0u) {
      fprintf(stderr, "measured observe failed: status=%d count=%zu at=%zu\n",
              status, count, i);
      (void)native_io_backend_close(&backend);
      (void)native_io_backend_destroy(&backend);
      return 1;
    }
  }

  cpu_ns = idle_observe_thread_cpu_ns() - cpu_started;
  wall_ns = salts_hrtime() - wall_started;
  fprintf(stderr, "NATIVE_IO_IDLE_OBSERVE_END iterations=%zu\n", iterations);
  fflush(stderr);

  printf("# NativeIO io_uring idle observe(0) benchmark\n\n");
  printf("| iterations | calls/s | wall ns/call | CPU ns/call |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  printf("| %zu | %.0f | %.3f | %.3f |\n",
         iterations,
         wall_ns == 0u ? 0.0 : (double)iterations * 1.0e9 / (double)wall_ns,
         iterations == 0u ? 0.0 : (double)wall_ns / (double)iterations,
         iterations == 0u ? 0.0 : (double)cpu_ns / (double)iterations);

  csv = idle_observe_open_csv();
  if (csv != NULL) {
    fprintf(csv, "iterations,wall_ns,cpu_ns,calls_per_second,wall_ns_per_call,cpu_ns_per_call\n");
    fprintf(csv, "%zu,%" PRIu64 ",%" PRIu64 ",%.6f,%.6f,%.6f\n",
            iterations, wall_ns, cpu_ns,
            wall_ns == 0u ? 0.0 : (double)iterations * 1.0e9 / (double)wall_ns,
            iterations == 0u ? 0.0 : (double)wall_ns / (double)iterations,
            iterations == 0u ? 0.0 : (double)cpu_ns / (double)iterations);
    fclose(csv);
  }

  status = native_io_backend_close(&backend);
  if (status != SALTS_OK) {
    fprintf(stderr, "native_io_backend_close failed: %d\n", status);
    (void)native_io_backend_destroy(&backend);
    return 1;
  }
  status = native_io_backend_destroy(&backend);
  if (status != SALTS_OK) {
    fprintf(stderr, "native_io_backend_destroy failed: %d\n", status);
    return 1;
  }
  return 0;
}
