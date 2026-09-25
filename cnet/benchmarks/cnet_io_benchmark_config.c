#include "cnet_io_benchmark_config.h"

#include <salts/error_codes.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int cnet_io_benchmark_backend_from_name(const char *name,
                                               cnet_io_benchmark_backend *selected) {
  if (strcmp(name, "iocp") == 0) {
#ifdef _WIN32
    *selected = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_IOCP, "iocp"};
    return SALTS_OK;
#else
    return SALTS_ENOTSUP;
#endif
  }
  if (strcmp(name, "epoll") == 0) {
#if defined(__linux__)
    *selected = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_EPOLL, "epoll"};
    return SALTS_OK;
#else
    return SALTS_ENOTSUP;
#endif
  }
  if (strcmp(name, "io_uring") == 0) {
#if defined(__linux__)
    *selected = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_IO_URING, "io_uring"};
    return SALTS_OK;
#else
    return SALTS_ENOTSUP;
#endif
  }
  if (strcmp(name, "kqueue") == 0) {
#if !defined(_WIN32) && !defined(__linux__)
    *selected = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_KQUEUE, "kqueue"};
    return SALTS_OK;
#else
    return SALTS_ENOTSUP;
#endif
  }
  return SALTS_EINVAL;
}

int cnet_io_benchmark_select_backend(const char *requested, cnet_io_benchmark_backend *selected) {
  cnet_io_benchmark_backend candidate;
  int status;
  if (selected == NULL) return SALTS_EINVAL;
  if (requested != NULL) {
    if (requested[0] == '\0') return SALTS_EINVAL;
    status = cnet_io_benchmark_backend_from_name(requested, &candidate);
    if (status != SALTS_OK) return status;
  } else {
#ifdef _WIN32
    candidate = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_IOCP, "iocp"};
#elif defined(__linux__)
    candidate = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_EPOLL, "epoll"};
#else
    candidate = (cnet_io_benchmark_backend){NATIVE_IO_BACKEND_KQUEUE, "kqueue"};
#endif
  }
  if (!native_io_backend_kind_supported(candidate.kind)) return SALTS_ENOTSUP;
  *selected = candidate;
  return SALTS_OK;
}

int cnet_io_benchmark_select_trace(const char *requested, cnet_io_benchmark_trace *selected) {
  static const char *const drivers[IO_BENCH_DRIVER_COUNT] = {
      [IO_BENCH_LIBUV] = "libuv:", [IO_BENCH_NATIVE_IO] = "native:",
      [IO_BENCH_NATIVE_IO_COROUTINE] = "coroutine:", [IO_BENCH_CNET] = "cnet:"};
  cnet_io_benchmark_trace result = {0};
  const char *size_text = NULL;
  char *end = NULL;
  unsigned long bytes;
  if (selected == NULL) return SALTS_EINVAL;
  if (requested == NULL) { *selected = result; return SALTS_OK; }
  for (unsigned driver = 0u; driver < sizeof(drivers) / sizeof(drivers[0]); ++driver) {
    const size_t length = strlen(drivers[driver]);
    if (strncmp(requested, drivers[driver], length) != 0) continue;
    result.driver = (io_bench_driver)driver;
    if (strncmp(requested + length, "tcp:", 4u) == 0) result.udp = false;
    else if (strncmp(requested + length, "udp:", 4u) == 0) result.udp = true;
    else return SALTS_EINVAL;
    size_text = requested + length + 4u;
    break;
  }
  if (size_text == NULL || *size_text < '0' || *size_text > '9') return SALTS_EINVAL;
  errno = 0;
  bytes = strtoul(size_text, &end, 10);
  if (errno != 0 || *end != '\0' || bytes == 0u ||
      bytes > CNET_IO_BENCHMARK_MAX_PAYLOAD ||
      (result.udp && bytes > CNET_IO_BENCHMARK_MAX_DATAGRAM))
    return SALTS_ERANGE;
  result.enabled = true;
  result.payload_size = (size_t)bytes;
  *selected = result;
  return SALTS_OK;
}
