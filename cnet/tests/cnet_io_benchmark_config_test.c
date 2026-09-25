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

  it("selects one bounded trace workload without changing the comparison defaults") {
    cnet_io_benchmark_trace trace = {0};
    check_equal(cnet_io_benchmark_select_trace(NULL, &trace), SALTS_OK);
    check_false(trace.enabled);
    check_equal(cnet_io_benchmark_select_trace("libuv:tcp:32768", &trace), SALTS_OK);
    check_true(trace.enabled);
    check_false(trace.udp);
    check_equal(trace.driver, 0u);
    check_equal(trace.payload_size, (size_t)32768u);
    check_equal(cnet_io_benchmark_select_trace("native:tcp:1024", &trace), SALTS_OK);
    check_equal(trace.driver, 1u);
    check_equal(cnet_io_benchmark_select_trace("coroutine:udp:8192", &trace), SALTS_OK);
    check_equal(trace.driver, 2u);
    check_true(trace.udp);
    check_equal(cnet_io_benchmark_select_trace("cnet:tcp:65536", &trace), SALTS_OK);
    check_equal(trace.driver, 3u);
  }

  it("rejects malformed and oversized trace requests without selecting another workload") {
    const char *invalid[] = {"", "unknown:tcp:1024", "native:pipe:1024", "native:tcp:-1",
                            "native:tcp:+1", "native:tcp: 1", "native:tcp:0",
                            "native:tcp:65537", "libuv:udp:8193", "cnet:tcp:12junk",
                            "cnet:tcp:999999999999999999999999999999"};
    cnet_io_benchmark_trace trace = {.payload_size = 42u};
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      check_not_equal(cnet_io_benchmark_select_trace(invalid[i], &trace), SALTS_OK);
      check_equal(trace.payload_size, (size_t)42u);
    }
    check_equal(cnet_io_benchmark_select_trace(NULL, NULL), SALTS_EINVAL);
  }
}
