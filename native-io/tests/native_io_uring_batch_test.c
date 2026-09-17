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

  it("flushes a FIFO lane successor before returning its predecessor completion") {
    static const unsigned char payload[] = {0x41u, 0x42u};
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 2u, 1u};
    int descriptors[2] = {-1, -1};
    native_io_endpoint endpoint = {0};
    native_io_request requests[2] = {0};
    native_io_completion events[2] = {0};
    native_io_operation operations[2];
    native_io_uring_profile after_first = {0};
    native_io_uring_profile after_all = {0};
    unsigned char received[2] = {0};
    size_t first_count = 0u;
    size_t second_count = 0u;
    bool successor_flushed;

    check_equal(native_io_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_uring_batch_make_pipe(descriptors), 0);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                             NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                SALTS_OK);
    check_equal(write(descriptors[1], payload, sizeof(payload)), (ssize_t)sizeof(payload));

    operations[0] = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                         .endpoint = endpoint,
                                         .buffer = &received[0],
                                         .length = 1u,
                                         .user_data = 11u};
    operations[1] = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                         .endpoint = endpoint,
                                         .buffer = &received[1],
                                         .length = 1u,
                                         .user_data = 12u};

    check_equal(native_io_backend_submit(&backend, &operations[0], &requests[0]), SALTS_OK);
    check_equal(native_io_backend_submit(&backend, &operations[1], &requests[1]), SALTS_OK);
    check_equal(native_io_backend_observe(&backend, &events[0], 1u, 5000u, &first_count), SALTS_OK);
    check_equal(first_count, (size_t)1u);
    check_equal(events[0].user_data, (uintptr_t)11u);
    check_true(native_io_io_uring_profile_take(&backend, &after_first));
    successor_flushed = after_first.enter_submitted == after_first.sqes_published;

    check_equal(native_io_backend_observe(&backend, &events[1], 1u, 5000u, &second_count), SALTS_OK);
    check_equal(second_count, (size_t)1u);
    check_equal(events[1].user_data, (uintptr_t)12u);
    check_equal(memcmp(received, payload, sizeof(payload)), 0);
    check_true(native_io_io_uring_profile_take(&backend, &after_all));
    check_equal(after_all.enter_submitted, after_all.sqes_published);

    (void)close(descriptors[0]);
    (void)close(descriptors[1]);
    check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    check_true(successor_flushed);
  }

  it("flushes a published in-flight cancel on the next observe boundary") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};
    int descriptors[2] = {-1, -1};
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    native_io_operation operation;
    native_io_uring_profile before_cancel = {0};
    native_io_uring_profile pending_cancel = {0};
    native_io_uring_profile after_cancel = {0};
    unsigned char received = 0u;
    size_t count = 0u;

    check_equal(native_io_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_uring_batch_make_pipe(descriptors), 0);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                             NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                SALTS_OK);
    operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                      .endpoint = endpoint,
                                      .buffer = &received,
                                      .length = 1u,
                                      .user_data = 21u};

    check_equal(native_io_backend_submit(&backend, &operation, &request), SALTS_OK);
    check_equal(native_io_backend_observe(&backend, &event, 1u, 1u, &count), SALTS_ETIMEDOUT);
    check_equal(count, (size_t)0u);
    check_true(native_io_io_uring_profile_take(&backend, &before_cancel));
    check_equal(before_cancel.enter_submitted, before_cancel.sqes_published);

    check_equal(native_io_backend_cancel(&backend, request), SALTS_OK);
    check_true(native_io_io_uring_profile_take(&backend, &pending_cancel));
    check_equal(pending_cancel.sqes_published, before_cancel.sqes_published + (uint64_t)1u);
    check_equal(pending_cancel.enter_submitted, before_cancel.enter_submitted);

    count = 0u;
    check_equal(native_io_backend_observe(&backend, &event, 1u, 5000u, &count), SALTS_OK);
    check_equal(count, (size_t)1u);
    check_equal(event.kind, NATIVE_IO_COMPLETION_CANCELLED);
    check_true(native_io_io_uring_profile_take(&backend, &after_cancel));
    check_equal(after_cancel.enter_submitted, after_cancel.sqes_published);

    (void)close(descriptors[0]);
    (void)close(descriptors[1]);
    check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

  it("flushes a full SQ ring under pressure without dropping published entries") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};
    native_io_uring_profile before_final_flush = {0};
    native_io_uring_profile after_final_flush = {0};

    check_equal(native_io_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_io_uring_test_pressure(&backend, &before_final_flush,
                                                &after_final_flush),
                SALTS_OK);
    check_true(before_final_flush.pressure_flushes > (uint64_t)0u);
    check_true(before_final_flush.enter_submitted <= before_final_flush.sqes_published);
    check_true(before_final_flush.enter_submitted < before_final_flush.sqes_published);
    check_equal(after_final_flush.enter_submitted, after_final_flush.sqes_published);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
}
