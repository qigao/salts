#include "cnet_io_benchmark_config.h"
#include "tinytest.h"

#include <salts/error_codes.h>
#include <string.h>

spec("CNet I/O benchmark backend selection") {
  it("uses the platform backend only when no override is requested") {
    cnet_io_benchmark_backend selected = {0};

    check_equal(cnet_io_benchmark_select_backend(NULL, &selected), SALTS_OK);
#ifdef _WIN32
    check_equal(selected.kind, NATIVE_IO_BACKEND_IOCP);
    check_equal(strcmp(selected.name, "iocp"), 0);
#elif defined(__linux__)
    check_equal(selected.kind, NATIVE_IO_BACKEND_EPOLL);
    check_equal(strcmp(selected.name, "epoll"), 0);
#else
    check_equal(selected.kind, NATIVE_IO_BACKEND_KQUEUE);
    check_equal(strcmp(selected.name, "kqueue"), 0);
#endif
  }

  it("accepts every backend supported by the benchmark host") {
    cnet_io_benchmark_backend selected = {0};

#ifdef _WIN32
    check_equal(cnet_io_benchmark_select_backend("iocp", &selected), SALTS_OK);
    check_equal(selected.kind, NATIVE_IO_BACKEND_IOCP);
#elif defined(__linux__)
    check_equal(cnet_io_benchmark_select_backend("epoll", &selected), SALTS_OK);
    check_equal(selected.kind, NATIVE_IO_BACKEND_EPOLL);
    check_equal(cnet_io_benchmark_select_backend("io_uring", &selected), SALTS_OK);
    check_equal(selected.kind, NATIVE_IO_BACKEND_IO_URING);
#else
    check_equal(cnet_io_benchmark_select_backend("kqueue", &selected), SALTS_OK);
    check_equal(selected.kind, NATIVE_IO_BACKEND_KQUEUE);
#endif
  }

  it("rejects invalid and platform-incompatible overrides") {
    cnet_io_benchmark_backend selected = {0};

    check_equal(cnet_io_benchmark_select_backend(NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_io_benchmark_select_backend("", &selected), SALTS_EINVAL);
    check_equal(cnet_io_benchmark_select_backend("unknown", &selected), SALTS_EINVAL);
#ifdef _WIN32
    check_equal(cnet_io_benchmark_select_backend("epoll", &selected), SALTS_ENOTSUP);
    check_equal(cnet_io_benchmark_select_backend("io_uring", &selected), SALTS_ENOTSUP);
    check_equal(cnet_io_benchmark_select_backend("kqueue", &selected), SALTS_ENOTSUP);
#elif defined(__linux__)
    check_equal(cnet_io_benchmark_select_backend("iocp", &selected), SALTS_ENOTSUP);
    check_equal(cnet_io_benchmark_select_backend("kqueue", &selected), SALTS_ENOTSUP);
#else
    check_equal(cnet_io_benchmark_select_backend("iocp", &selected), SALTS_ENOTSUP);
    check_equal(cnet_io_benchmark_select_backend("epoll", &selected), SALTS_ENOTSUP);
    check_equal(cnet_io_benchmark_select_backend("io_uring", &selected), SALTS_ENOTSUP);
#endif
  }
}
