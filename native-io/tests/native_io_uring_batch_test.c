#if !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include "../src/native_io_internal.h"

#include <salts/error_codes.h>

#define TINYTEST_NO_MAIN 1
#include "tinytest.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static int native_io_uring_batch_make_pipe(int descriptors[2]) {
  int flags;
  if (pipe(descriptors) != 0) return -1;
  flags = fcntl(descriptors[0], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) goto failed;
  flags = fcntl(descriptors[1], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK) != 0) goto failed;
  return 0;

failed:
  (void)close(descriptors[0]);
  (void)close(descriptors[1]);
  descriptors[0] = -1;
  descriptors[1] = -1;
  return -1;
}

static int native_io_uring_batch_observe_all(native_io_backend *backend,
                                             native_io_completion *events,
                                             size_t expected) {
  size_t total = 0u;
  while (total < expected) {
    size_t count = 0u;
    const int status = native_io_backend_observe(backend, events + total, expected - total,
                                                 5000u, &count);
    if (status != SALTS_OK) return status;
    total += count;
  }
  return SALTS_OK;
}

spec("NativeIO io_uring submission batching") {
  it("publishes independent lane heads before entering the kernel") {
    static const unsigned char payload[] = {0x41u, 0x42u, 0x43u, 0x44u};
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 2u, 2u, 2u};
    int descriptors[2] = {-1, -1};
    native_io_endpoint endpoints[2] = {0};
    native_io_request requests[2] = {0};
    native_io_completion events[2] = {0};
    native_io_operation operations[2];
    native_io_uring_profile before = {0};
    native_io_uring_profile after = {0};
    unsigned char received[sizeof(payload)] = {0};

    check_equal(native_io_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_uring_batch_make_pipe(descriptors), 0);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                             NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                             &endpoints[0]),
                SALTS_OK);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[1],
                                             NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                             &endpoints[1]),
                SALTS_OK);

    operations[0] = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                         .endpoint = endpoints[0],
                                         .buffer = received,
                                         .length = sizeof(received),
                                         .user_data = 1u};
    operations[1] = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_WRITE,
                                         .endpoint = endpoints[1],
                                         .buffer = (void *)payload,
                                         .length = sizeof(payload),
                                         .user_data = 2u};

    check_equal(native_io_backend_submit(&backend, &operations[0], &requests[0]), SALTS_OK);
    check_equal(native_io_backend_submit(&backend, &operations[1], &requests[1]), SALTS_OK);
    check_true(native_io_io_uring_profile_take(&backend, &before));
    check_equal(before.sqes_published, (uint64_t)2u);
    check_equal(before.enter_calls, (uint64_t)0u);
    check_equal(before.enter_submitted, (uint64_t)0u);

    check_equal(native_io_uring_batch_observe_all(&backend, events, 2u), SALTS_OK);
    check_equal(memcmp(received, payload, sizeof(payload)), 0);
    check_true(native_io_io_uring_profile_take(&backend, &after));
    check_equal(after.sqes_published, (uint64_t)2u);
    check_equal(after.enter_calls, (uint64_t)1u);
    check_equal(after.enter_submitted, (uint64_t)2u);
    check_equal(after.pressure_flushes, (uint64_t)0u);

    (void)close(descriptors[0]);
    (void)close(descriptors[1]);
    check_equal(native_io_backend_release_pipe(&backend, endpoints[0]), SALTS_OK);
    check_equal(native_io_backend_release_pipe(&backend, endpoints[1]), SALTS_OK);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
}
