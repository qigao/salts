#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <errno.h>
#include <inttypes.h>
#include <linux/io_uring.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum {
  ENTER_BENCH_DEFAULT_ITERATIONS = 5000000,
  ENTER_BENCH_DEFAULT_WARMUP = 10000
};

static uint64_t bench_now_ns(clockid_t clock_id) {
  struct timespec ts;
  if (clock_gettime(clock_id, &ts) != 0) return 0u;
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static size_t bench_count(const char *name, size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u || parsed > 100000000ull)
    return fallback;
  return (size_t)parsed;
}

#if defined(__NR_io_uring_setup) && defined(__NR_io_uring_enter) && \
    defined(__NR_io_uring_register) && defined(IORING_FEAT_REG_REG_RING) && \
    defined(IORING_ENTER_REGISTERED_RING)

typedef struct enter_measurement {
  const char *name;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  double wall_ns_per_call;
  double cpu_ns_per_call;
  double calls_per_second;
} enter_measurement;

static int enter_once(int fd, unsigned flags) {
  const int result = (int)syscall(__NR_io_uring_enter, fd, 0u, 0u, flags, NULL, 0u);
  return result < 0 ? -errno : result;
}

static int measure_mode(const char *name, int fd, unsigned flags, size_t iterations,
                        size_t warmup, enter_measurement *out) {
  uint64_t wall_started;
  uint64_t cpu_started;
  int status;

  for (size_t i = 0u; i < warmup; ++i) {
    status = enter_once(fd, flags);
    if (status != 0) return status;
  }

  fprintf(stderr, "NATIVE_IO_REGISTERED_ENTER_BEGIN mode=%s iterations=%zu\n", name, iterations);
  fflush(stderr);
  wall_started = bench_now_ns(CLOCK_MONOTONIC);
  cpu_started = bench_now_ns(CLOCK_THREAD_CPUTIME_ID);

  for (size_t i = 0u; i < iterations; ++i) {
    status = enter_once(fd, flags);
    if (status != 0) return status == 0 ? -EIO : status;
  }

  out->name = name;
  out->cpu_ns = bench_now_ns(CLOCK_THREAD_CPUTIME_ID) - cpu_started;
  out->wall_ns = bench_now_ns(CLOCK_MONOTONIC) - wall_started;
  out->cpu_ns_per_call = (double)out->cpu_ns / (double)iterations;
  out->wall_ns_per_call = (double)out->wall_ns / (double)iterations;
  out->calls_per_second =
      out->wall_ns == 0u ? 0.0 : (double)iterations * 1.0e9 / (double)out->wall_ns;
  fprintf(stderr, "NATIVE_IO_REGISTERED_ENTER_END mode=%s iterations=%zu\n", name, iterations);
  fflush(stderr);
  return 0;
}

static FILE *open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_REGISTERED_ENTER_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

int main(void) {
  const size_t iterations =
      bench_count("NATIVE_IO_REGISTERED_ENTER_ITERATIONS", ENTER_BENCH_DEFAULT_ITERATIONS);
  const size_t warmup =
      bench_count("NATIVE_IO_REGISTERED_ENTER_WARMUP", ENTER_BENCH_DEFAULT_WARMUP);
  struct io_uring_params params;
  struct io_uring_rsrc_update update;
  enter_measurement rows[4] = {0};
  const char *names[4] = {"normal-a", "registered-a", "registered-b", "normal-b"};
  int fds[4];
  unsigned flags[4];
  FILE *csv;
  int ring_fd;
  int registered_index;
  int result;

  memset(&params, 0, sizeof(params));
  ring_fd = (int)syscall(__NR_io_uring_setup, 8u, &params);
  if (ring_fd < 0) {
    fprintf(stderr, "io_uring_setup failed: %d\n", errno);
    return 2;
  }

  memset(&update, 0, sizeof(update));
  update.offset = UINT32_MAX;
  update.data = (uint64_t)(unsigned)ring_fd;
  result = (int)syscall(__NR_io_uring_register, ring_fd, IORING_REGISTER_RING_FDS, &update, 1u);
  if (result != 1) {
    fprintf(stderr, "IORING_REGISTER_RING_FDS failed: result=%d errno=%d\n", result, errno);
    (void)close(ring_fd);
    return 2;
  }
  registered_index = (int)update.offset;

  fds[0] = ring_fd;
  fds[1] = registered_index;
  fds[2] = registered_index;
  fds[3] = ring_fd;
  flags[0] = 0u;
  flags[1] = IORING_ENTER_REGISTERED_RING;
  flags[2] = IORING_ENTER_REGISTERED_RING;
  flags[3] = 0u;

  for (size_t i = 0u; i < 4u; ++i) {
    result = measure_mode(names[i], fds[i], flags[i], iterations, warmup, &rows[i]);
    if (result != 0) {
      fprintf(stderr, "measure %s failed: %d\n", names[i], result);
      goto fail;
    }
  }

  printf("# io_uring registered-ring enter benchmark\n\n");
  printf("| mode | CPU ns/call | wall ns/call | calls/s |\n");
  printf("| --- | ---: | ---: | ---: |\n");
  for (size_t i = 0u; i < 4u; ++i)
    printf("| %s | %.3f | %.3f | %.0f |\n",
           rows[i].name, rows[i].cpu_ns_per_call, rows[i].wall_ns_per_call,
           rows[i].calls_per_second);

  csv = open_csv();
  if (csv != NULL) {
    fprintf(csv, "mode,iterations,cpu_ns,wall_ns,cpu_ns_per_call,wall_ns_per_call,calls_per_second\n");
    for (size_t i = 0u; i < 4u; ++i)
      fprintf(csv, "%s,%zu,%" PRIu64 ",%" PRIu64 ",%.6f,%.6f,%.6f\n",
              rows[i].name, iterations, rows[i].cpu_ns, rows[i].wall_ns,
              rows[i].cpu_ns_per_call, rows[i].wall_ns_per_call,
              rows[i].calls_per_second);
    fclose(csv);
  }

  memset(&update, 0, sizeof(update));
  update.offset = (uint32_t)registered_index;
  result = (int)syscall(__NR_io_uring_register, ring_fd, IORING_UNREGISTER_RING_FDS, &update, 1u);
  if (result != 1) {
    fprintf(stderr, "IORING_UNREGISTER_RING_FDS failed: result=%d errno=%d\n", result, errno);
    (void)close(ring_fd);
    return 1;
  }
  (void)close(ring_fd);
  return 0;

fail:
  memset(&update, 0, sizeof(update));
  update.offset = (uint32_t)registered_index;
  (void)syscall(__NR_io_uring_register, ring_fd, IORING_UNREGISTER_RING_FDS, &update, 1u);
  (void)close(ring_fd);
  return 1;
}

#else

int main(void) {
  fprintf(stderr, "registered-ring enter benchmark is not supported by build headers\n");
  return 2;
}

#endif
