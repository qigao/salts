#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <errno.h>
#include <linux/io_uring.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { BATCH_TEST_COUNT = 4, BATCH_TEST_CALLS = 16, BATCH_TEST_TIMEOUT_MS = 5000 };
static unsigned enter_calls;
static unsigned enter_sizes[BATCH_TEST_CALLS];
static unsigned consume_limit;
static unsigned consume_before_error;
static int enter_error;
static bool no_progress;

/* Compile a private copy of the driver. Faults never enter the production ABI,
 * and short submissions still consume real SQEs through the kernel. */
static long batch_test_syscall(long number, ...) {
  va_list arguments;
  long result;
  va_start(arguments, number);
  if (number == __NR_io_uring_enter) {
    const int fd = va_arg(arguments, int);
    unsigned submit = va_arg(arguments, unsigned);
    const unsigned minimum = va_arg(arguments, unsigned);
    const unsigned flags = va_arg(arguments, unsigned);
    void *mask = va_arg(arguments, void *);
    const unsigned mask_size = va_arg(arguments, unsigned);
    if (enter_calls < BATCH_TEST_CALLS) enter_sizes[enter_calls] = submit;
    ++enter_calls;
    if (enter_error != 0) {
      if (consume_before_error != 0u) {
        result = syscall(number, fd, consume_before_error, minimum, flags, mask, mask_size);
        if (result != (long)consume_before_error) {
          va_end(arguments);
          return result;
        }
      }
      errno = enter_error;
      enter_error = 0;
      result = -1;
    } else if (no_progress) {
      result = 0;
    } else {
      if (consume_limit != 0u && submit > consume_limit) submit = consume_limit;
      result = syscall(number, fd, submit, minimum, flags, mask, mask_size);
    }
  } else if (number == __NR_io_uring_setup) {
    const unsigned entries = va_arg(arguments, unsigned);
    struct io_uring_params *parameters = va_arg(arguments, struct io_uring_params *);
    result = syscall(number, entries, parameters);
  } else {
    errno = ENOSYS;
    result = -1;
  }
  va_end(arguments);
  return result;
}

#define syscall batch_test_syscall
#define salts_io_uring_backend_init batch_test_backend_init
#include "../src/native_io_io_uring.c"
#undef salts_io_uring_backend_init
#undef syscall

#include "tinytest.h"

static void batch_test_requests(bool prepared, unsigned accepted, int expected_status) {
  native_io_backend backend = {0};
  const native_io_backend_config config = {
      NATIVE_IO_BACKEND_IO_URING, BATCH_TEST_COUNT, BATCH_TEST_COUNT, BATCH_TEST_COUNT};
  int descriptors[BATCH_TEST_COUNT][2];
  native_io_endpoint endpoints[BATCH_TEST_COUNT] = {0};
  native_io_request requests[BATCH_TEST_COUNT] = {0};
  native_io_completion events[BATCH_TEST_COUNT] = {0};
  unsigned char bytes[BATCH_TEST_COUNT] = {0};
  const unsigned char sent = 0x61u;
  size_t total = 0u;
  unsigned seen = 0u;
  check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
  for (size_t i = 0u; i < BATCH_TEST_COUNT; ++i) {
    check_equal(pipe(descriptors[i]), 0);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[i][0],
                                              NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                              &endpoints[i]), SALTS_OK);
    check_equal(write(descriptors[i][1], &sent, 1u), (ssize_t)1);
    const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                           .endpoint = endpoints[i], .buffer = &bytes[i],
                                           .length = 1u, .user_data = i};
    check_equal(prepared ? native_io_backend_prepare(&backend, &operation, &requests[i])
                         : native_io_backend_submit(&backend, &operation, &requests[i]), SALTS_OK);
  }
  check_equal(enter_calls, prepared ? 0u : (unsigned)BATCH_TEST_COUNT);
  check_equal(native_io_backend_close(&backend), SALTS_OK);
  check_equal(native_io_backend_flush(&backend), expected_status);
  check_equal(native_io_backend_release_pipe(&backend, endpoints[0]), SALTS_EBUSY);
  salts_io_uring_impl *impl = backend.impl;
  check_equal(*impl->sq_head, *impl->sq_tail);
  check_equal(impl->staged_head, SALTS_IO_URING_INDEX_NONE);
  check_equal(impl->active_requests, (size_t)BATCH_TEST_COUNT);
  while (total < BATCH_TEST_COUNT) {
    size_t count = 0u;
    check_equal(native_io_backend_observe(&backend, events + total, BATCH_TEST_COUNT - total,
                                          BATCH_TEST_TIMEOUT_MS, &count), SALTS_OK);
    total += count;
  }
  for (size_t i = 0u; i < BATCH_TEST_COUNT; ++i) {
    const size_t tag = events[i].user_data;
    check_less(tag, (size_t)BATCH_TEST_COUNT);
    check_equal(seen & (1u << tag), 0u);
    seen |= 1u << tag;
    check_equal(events[i].kind, tag < accepted ? NATIVE_IO_COMPLETION_OK
                                             : NATIVE_IO_COMPLETION_FAILED);
    check_equal(events[i].status, tag < accepted ? SALTS_OK : expected_status);
    check_equal(bytes[tag], tag < accepted ? sent : 0u);
    check_equal(native_io_backend_cancel(&backend, requests[tag]), SALTS_ENOENT);
    check_equal(close(descriptors[tag][0]), 0);
    check_equal(close(descriptors[tag][1]), 0);
    check_equal(native_io_backend_release_pipe(&backend, endpoints[tag]), SALTS_OK);
  }
  check_equal(seen, (1u << BATCH_TEST_COUNT) - 1u);
  check_equal(native_io_backend_flush(&backend), SALTS_OK);
  check_equal(native_io_backend_destroy(&backend), SALTS_OK);
}

spec("io_uring explicit batch submission") {
  before_each() {
    enter_calls = 0u;
    memset(enter_sizes, 0, sizeof(enter_sizes));
    consume_limit = 0u;
    consume_before_error = 0u;
    enter_error = 0;
    no_progress = false;
  }
  it("preserves immediate submit without observe or flush") {
    batch_test_requests(false, BATCH_TEST_COUNT, SALTS_OK);
    check_equal(enter_calls, (unsigned)BATCH_TEST_COUNT);
  }
  it("flushes independent endpoints in one enter after close") {
    batch_test_requests(true, BATCH_TEST_COUNT, SALTS_OK);
    check_equal(enter_calls, 1u);
    check_equal(enter_sizes[0], (unsigned)BATCH_TEST_COUNT);
  }
  it("continues only the unconsumed suffix after short submission") {
    consume_limit = 1u;
    batch_test_requests(true, BATCH_TEST_COUNT, SALTS_OK);
    check_equal(enter_calls, (unsigned)BATCH_TEST_COUNT);
    for (unsigned i = 0u; i < BATCH_TEST_COUNT; ++i)
      check_equal(enter_sizes[i], (unsigned)BATCH_TEST_COUNT - i);
  }
  it("publishes one failure per unsubmitted request on enter error") {
    enter_error = EIO;
    batch_test_requests(true, 0u, -EIO);
    check_equal(enter_calls, 1u);
  }
  it("retains the consumed prefix when an enter error follows consumption") {
    enter_error = EIO;
    consume_before_error = 1u;
    batch_test_requests(true, 1u, -EIO);
    check_equal(enter_calls, 1u);
  }
  it("does not replay consumed entries when an interrupted enter is retried") {
    enter_error = EINTR;
    consume_before_error = 1u;
    batch_test_requests(true, BATCH_TEST_COUNT, SALTS_OK);
    check_equal(enter_calls, 2u);
  }
  it("does not spin or release borrowed requests on zero progress") {
    no_progress = true;
    batch_test_requests(true, 0u, SALTS_EIO);
    check_equal(enter_calls, 1u);
  }
  it("completes a ready prepared stream send without entering the ring") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 2u, 2u};
    int sockets[2];
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    const unsigned char sent[] = {0x31u, 0x32u, 0x33u};
    unsigned char received[sizeof(sent)] = {0};
    size_t count = 0u;
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_socket(&backend, (uintptr_t)sockets[0], &endpoint),
                SALTS_OK);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_SEND,
                                             .endpoint = endpoint,
                                             .buffer = (void *)sent,
                                             .length = sizeof(sent)};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
    }
    check_equal(enter_calls, 0u);
    check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count), SALTS_OK);
    check_equal(count, 1u);
    check_equal(event.kind, NATIVE_IO_COMPLETION_OK);
    check_equal(event.bytes, sizeof(sent));
    check_equal(recv(sockets[1], received, sizeof(received), 0), (ssize_t)sizeof(received));
    check_equal(memcmp(received, sent, sizeof(sent)), 0);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(close(sockets[0]), 0);
    check_equal(close(sockets[1]), 0);
    check_equal(native_io_backend_release_socket(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

  it("completes a ready prepared stream receive without entering the ring") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 2u, 2u};
    int sockets[2];
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    const unsigned char sent = 0x41u;
    unsigned char received = 0u;
    size_t count = 0u;
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_socket(&backend, (uintptr_t)sockets[0], &endpoint),
                SALTS_OK);
    check_equal(send(sockets[1], &sent, 1u, 0), (ssize_t)1);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
                                             .endpoint = endpoint,
                                             .buffer = &received,
                                             .length = 1u};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
    }
    check_equal(enter_calls, 0u);
    check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count), SALTS_OK);
    check_equal(count, 1u);
    check_equal(event.kind, NATIVE_IO_COMPLETION_OK);
    check_equal(event.bytes, 1u);
    check_equal(received, sent);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(close(sockets[0]), 0);
    check_equal(close(sockets[1]), 0);
    check_equal(native_io_backend_release_socket(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

  it("stages a prepared stream receive only after direct EAGAIN") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 2u, 2u};
    int sockets[2];
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    const unsigned char sent = 0x51u;
    unsigned char received = 0u;
    size_t count = 0u;
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_socket(&backend, (uintptr_t)sockets[0], &endpoint),
                SALTS_OK);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
                                             .endpoint = endpoint,
                                             .buffer = &received,
                                             .length = 1u};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
    }
    check_equal(enter_calls, 0u);
    check_not_equal(((salts_io_uring_impl *)backend.impl)->staged_head,
                    SALTS_IO_URING_INDEX_NONE);
    check_equal(send(sockets[1], &sent, 1u, 0), (ssize_t)1);
    check_equal(native_io_backend_observe(&backend, &event, 1u, BATCH_TEST_TIMEOUT_MS, &count),
                SALTS_OK);
    check_equal(count, 1u);
    check_equal(event.kind, NATIVE_IO_COMPLETION_OK);
    check_equal(received, sent);
    check_equal(enter_calls, 1u);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(close(sockets[0]), 0);
    check_equal(close(sockets[1]), 0);
    check_equal(native_io_backend_release_socket(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

  it("does not speculate a newer stream receive ahead of a queued lane head") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 3u, 3u};
    int sockets[2];
    native_io_endpoint endpoint = {0};
    native_io_request requests[2] = {0};
    native_io_completion events[2] = {0};
    unsigned char received[2] = {0};
    const unsigned char first = 0x61u;
    const unsigned char second = 0x62u;
    size_t count = 0u;
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_socket(&backend, (uintptr_t)sockets[0], &endpoint),
                SALTS_OK);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
                                             .endpoint = endpoint,
                                             .buffer = &received[0],
                                             .length = 1u,
                                             .user_data = 0u};
      check_equal(native_io_backend_prepare(&backend, &operation, &requests[0]), SALTS_OK);
    }
    check_equal(send(sockets[1], &first, 1u, 0), (ssize_t)1);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
                                             .endpoint = endpoint,
                                             .buffer = &received[1],
                                             .length = 1u,
                                             .user_data = 1u};
      check_equal(native_io_backend_prepare(&backend, &operation, &requests[1]), SALTS_OK);
    }
    check_equal(received[0], 0u);
    check_equal(received[1], 0u);
    check_equal(enter_calls, 0u);
    check_equal(native_io_backend_observe(&backend, events, 2u, BATCH_TEST_TIMEOUT_MS, &count),
                SALTS_OK);
    check_equal(count, 1u);
    check_equal(events[0].user_data, (uintptr_t)0u);
    check_equal(received[0], first);
    check_equal(received[1], 0u);
    check_equal(send(sockets[1], &second, 1u, 0), (ssize_t)1);
    count = 0u;
    check_equal(native_io_backend_observe(&backend, events + 1u, 1u, BATCH_TEST_TIMEOUT_MS, &count),
                SALTS_OK);
    check_equal(count, 1u);
    check_equal(events[1].user_data, (uintptr_t)1u);
    check_equal(received[1], second);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(close(sockets[0]), 0);
    check_equal(close(sockets[1]), 0);
    check_equal(native_io_backend_release_socket(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

  it("publishes a speculative stream send error as a terminal completion") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 2u, 2u};
    int sockets[2];
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    unsigned char sent = 0x71u;
    size_t count = 0u;
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(native_io_backend_attach_socket(&backend, (uintptr_t)sockets[0], &endpoint),
                SALTS_OK);
    check_equal(close(sockets[1]), 0);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_SEND,
                                             .endpoint = endpoint,
                                             .buffer = &sent,
                                             .length = 1u};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
    }
    check_equal(enter_calls, 0u);
    check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count), SALTS_OK);
    check_equal(count, 1u);
    check_equal(event.kind, NATIVE_IO_COMPLETION_FAILED);
    check_not_equal(event.status, SALTS_OK);
    check_equal(native_io_backend_cancel(&backend, request), SALTS_ENOENT);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(close(sockets[0]), 0);
    check_equal(native_io_backend_release_socket(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

  it("submits prepared work and the wake poll in one wait enter") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};
    int descriptors[2];
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    unsigned char byte = 0u;
    const unsigned char sent = 0x6bu;
    size_t count = 0u;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(pipe(descriptors), 0);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                              NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                SALTS_OK);
    check_equal(write(descriptors[1], &sent, 1u), (ssize_t)1);
    {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                             .endpoint = endpoint,
                                             .buffer = &byte,
                                             .length = 1u};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
    }
    check_equal(enter_calls, 0u);
    check_equal(native_io_backend_observe(&backend, &event, 1u, BATCH_TEST_TIMEOUT_MS, &count),
                SALTS_OK);
    check_equal(count, 1u);
    check_equal(event.kind, NATIVE_IO_COMPLETION_OK);
    check_equal(event.bytes, 1u);
    check_equal(byte, sent);
    check_equal(enter_calls, 1u);
    check_equal(enter_sizes[0], 2u);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(close(descriptors[0]), 0);
    check_equal(close(descriptors[1]), 0);
    check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
  it("retains queued followers and failure terminals after observe reports a flush error") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 2u, 2u};
    int descriptors[2];
    native_io_endpoint endpoint = {0};
    native_io_request requests[2] = {0};
    native_io_completion events[2] = {0};
    unsigned char bytes[2] = {0};
    size_t count = 0u;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(pipe(descriptors), 0);
    check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                              NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                SALTS_OK);
    for (size_t i = 0u; i < 2u; ++i) {
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                             .endpoint = endpoint, .buffer = &bytes[i],
                                             .length = 1u, .user_data = i};
      check_equal(native_io_backend_prepare(&backend, &operation, &requests[i]), SALTS_OK);
    }
    enter_error = EIO;
    check_equal(native_io_backend_observe(&backend, events, 2u, 0u, &count), -EIO);
    check_equal(count, 0u);
    check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_EBUSY);
    check_equal(native_io_backend_observe(&backend, events, 2u, 0u, &count), SALTS_OK);
    check_equal(count, 2u);
    for (size_t i = 0u; i < 2u; ++i) {
      check_equal(events[i].kind, NATIVE_IO_COMPLETION_FAILED);
      check_equal(events[i].status, -EIO);
      check_equal(events[i].user_data, i);
      check_equal(bytes[i], 0u);
      check_equal(native_io_backend_cancel(&backend, requests[i]), SALTS_ENOENT);
    }
    check_equal(enter_calls, 1u);
    check_equal(close(descriptors[0]), 0);
    check_equal(close(descriptors[1]), 0);
    check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_OK);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
}
