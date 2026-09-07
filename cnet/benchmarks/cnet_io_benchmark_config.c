#include "cnet_io_benchmark_config.h"

#include <salts/error_codes.h>
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
