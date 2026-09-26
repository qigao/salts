#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <errno.h>
#include <linux/io_uring.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { BATCH_TEST_COUNT = 4, BATCH_TEST_CALLS = 16, BATCH_TEST_TIMEOUT_MS = 5000 };
static unsigned enter_calls;
static unsigned enter_sizes[BATCH_TEST_CALLS];
static int enter_fds[BATCH_TEST_CALLS];
static unsigned enter_flags[BATCH_TEST_CALLS];
static unsigned consume_limit;
static unsigned consume_before_error;
static int enter_error;
static bool no_progress;
static bool enable_ring_wait;
static unsigned poll_calls;
static unsigned setup_calls;
static unsigned setup_flags[BATCH_TEST_CALLS];
static unsigned reject_setup_flags;
static unsigned reject_nonzero_setups;
static unsigned register_calls;
static unsigned register_opcodes[BATCH_TEST_CALLS];
static int register_fds[BATCH_TEST_CALLS];
static bool reject_ring_registration;
static bool reject_ring_unregistration;

static int batch_test_poll(struct pollfd *fds, nfds_t count, int timeout) {
  ++poll_calls;
  return poll(fds, count, timeout);
}

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
    const size_t mask_size = va_arg(arguments, size_t);
    if (enter_calls < BATCH_TEST_CALLS) {
      enter_sizes[enter_calls] = submit;
      enter_fds[enter_calls] = fd;
      enter_flags[enter_calls] = flags;
    }
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
    if (setup_calls < BATCH_TEST_CALLS) setup_flags[setup_calls] = parameters->flags;
    ++setup_calls;
    if (reject_setup_flags != 0u &&
        (parameters->flags & reject_setup_flags) != 0u) {
      errno = EINVAL;
      result = -1;
    } else if (reject_nonzero_setups != 0u && parameters->flags != 0u) {
      --reject_nonzero_setups;
      errno = EINVAL;
      result = -1;
    } else {
      result = syscall(number, entries, parameters);
    }
#if defined(IORING_FEAT_EXT_ARG)
    if (result >= 0 && !enable_ring_wait) parameters->features &= ~IORING_FEAT_EXT_ARG;
#endif
#if defined(__NR_io_uring_register)
  } else if (number == __NR_io_uring_register) {
    const int fd = va_arg(arguments, int);
    const unsigned opcode = va_arg(arguments, unsigned);
    void *arg = va_arg(arguments, void *);
    const unsigned nr_args = va_arg(arguments, unsigned);
    if (register_calls < BATCH_TEST_CALLS) {
      register_opcodes[register_calls] = opcode;
      register_fds[register_calls] = fd;
    }
    ++register_calls;
#if defined(IORING_REGISTER_RING_FDS)
    if (reject_ring_registration && opcode == IORING_REGISTER_RING_FDS) {
      errno = EINVAL;
      result = -1;
    } else
#endif
#if defined(IORING_UNREGISTER_RING_FDS)
    if (reject_ring_unregistration && opcode == IORING_UNREGISTER_RING_FDS) {
      errno = EINVAL;
      result = -1;
    } else
#endif
    {
      result = syscall(number, fd, opcode, arg, nr_args);
    }
#endif
  } else {
    errno = ENOSYS;
    result = -1;
  }
  va_end(arguments);
  return result;
}

#define syscall batch_test_syscall
#define poll batch_test_poll
#define salts_io_uring_backend_init batch_test_backend_init
#include "../src/native_io_io_uring.c"
#undef salts_io_uring_backend_init
#undef poll
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
    memset(enter_fds, 0, sizeof(enter_fds));
    memset(enter_flags, 0, sizeof(enter_flags));
    consume_limit = 0u;
    consume_before_error = 0u;
    enter_error = 0;
    no_progress = false;
    enable_ring_wait = false;
    poll_calls = 0u;
    setup_calls = 0u;
    memset(setup_flags, 0, sizeof(setup_flags));
    reject_setup_flags = 0u;
    reject_nonzero_setups = 0u;
    register_calls = 0u;
    memset(register_opcodes, 0, sizeof(register_opcodes));
    memset(register_fds, 0, sizeof(register_fds));
    reject_ring_registration = false;
    reject_ring_unregistration = false;
  }
#if defined(__NR_io_uring_register) && defined(IORING_REGISTER_RING_FDS) && \
    defined(IORING_UNREGISTER_RING_FDS) && defined(IORING_ENTER_REGISTERED_RING)
  it("registers the ring fd and uses the returned index for enter") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    salts_io_uring_impl *impl = backend.impl;
    check_true(impl->ring_fd_registered);
    check_greater_equal(register_calls, 1u);
    check_equal(register_opcodes[0], (unsigned)IORING_REGISTER_RING_FDS);
    check_equal(register_fds[0], impl->ring_fd);

    enter_calls = 0u;
    memset(enter_fds, 0, sizeof(enter_fds));
    memset(enter_flags, 0, sizeof(enter_flags));
    check_equal(uring_enter(impl, 0u, 0u, 0u), 0);
    check_equal(enter_calls, 1u);
    check_equal(enter_fds[0], impl->enter_ring_fd);
    check_true((enter_flags[0] & IORING_ENTER_REGISTERED_RING) != 0u);

    check_equal(native_io_backend_close(&backend), SALTS_OK);
    const unsigned before_destroy = register_calls;
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    check_equal(register_calls, before_destroy + 1u);
    check_equal(register_opcodes[before_destroy], (unsigned)IORING_UNREGISTER_RING_FDS);
  }

  it("keeps normal ring enters when registration is unavailable") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
    reject_ring_registration = true;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    salts_io_uring_impl *impl = backend.impl;
    check_false(impl->ring_fd_registered);
    check_equal(impl->enter_ring_fd, impl->ring_fd);

    enter_calls = 0u;
    memset(enter_fds, 0, sizeof(enter_fds));
    memset(enter_flags, 0, sizeof(enter_flags));
    check_equal(uring_enter(impl, 0u, 0u, 0u), 0);
    check_equal(enter_calls, 1u);
    check_equal(enter_fds[0], impl->ring_fd);
    check_equal(enter_flags[0] & IORING_ENTER_REGISTERED_RING, 0u);

    check_equal(native_io_backend_close(&backend), SALTS_OK);
    const unsigned before_destroy = register_calls;
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    check_equal(register_calls, before_destroy);
  }

  it("retains destroy ownership when ring-fd unregister fails") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    salts_io_uring_impl *impl = backend.impl;
    check_true(impl->ring_fd_registered);
    check_equal(native_io_backend_close(&backend), SALTS_OK);

    reject_ring_unregistration = true;
    check_equal(native_io_backend_destroy(&backend), -EINVAL);
    check_true(backend.impl == (void *)impl);
    check_true(impl->ring_fd_registered);

    reject_ring_unregistration = false;
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    check_equal(backend.impl, NULL);
  }
#endif

#if defined(IORING_SETUP_SINGLE_ISSUER)
  it("requests the strongest single-owner setup supported by the build") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_greater_equal(setup_calls, 1u);
    check_true((setup_flags[0] & IORING_SETUP_SINGLE_ISSUER) != 0u);
#if defined(IORING_SETUP_DEFER_TASKRUN) && defined(IORING_FEAT_EXT_ARG) && \
    defined(IORING_ENTER_EXT_ARG)
    check_true((setup_flags[0] & IORING_SETUP_DEFER_TASKRUN) != 0u);
#endif
#if defined(IORING_SETUP_TASKRUN_FLAG) && defined(IORING_SQ_TASKRUN) && \
    defined(IORING_SQ_CQ_OVERFLOW) && defined(IORING_SETUP_DEFER_TASKRUN) && \
    defined(IORING_FEAT_EXT_ARG) && defined(IORING_ENTER_EXT_ARG)
    check_true((setup_flags[0] & IORING_SETUP_TASKRUN_FLAG) != 0u);
#endif
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }

#if defined(IORING_SETUP_TASKRUN_FLAG) && defined(IORING_SQ_TASKRUN) && \
    defined(IORING_SQ_CQ_OVERFLOW) && defined(IORING_SETUP_DEFER_TASKRUN) && \
    defined(IORING_FEAT_EXT_ARG) && defined(IORING_ENTER_EXT_ARG)
  it("falls back from unsupported taskrun flag to the accepted defer policy") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
    reject_setup_flags = IORING_SETUP_TASKRUN_FLAG;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    check_equal(setup_calls, 2u);
    check_true((setup_flags[0] & IORING_SETUP_TASKRUN_FLAG) != 0u);
    check_equal(setup_flags[1],
                (unsigned)(IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN));
    salts_io_uring_impl *impl = backend.impl;
    check_true(impl->defer_taskrun);
    check_false(impl->taskrun_flag);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
#endif

#if defined(IORING_SETUP_DEFER_TASKRUN) && defined(IORING_FEAT_EXT_ARG) && \
    defined(IORING_ENTER_EXT_ARG)
  it("falls back from unsupported defer-taskrun to single issuer") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
    reject_setup_flags = IORING_SETUP_DEFER_TASKRUN;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
#if defined(IORING_SETUP_TASKRUN_FLAG) && defined(IORING_SQ_TASKRUN) && \
    defined(IORING_SQ_CQ_OVERFLOW)
    check_equal(setup_calls, 3u);
    check_true((setup_flags[0] & IORING_SETUP_TASKRUN_FLAG) != 0u);
    check_true((setup_flags[1] & IORING_SETUP_DEFER_TASKRUN) != 0u);
    check_equal(setup_flags[2], (unsigned)IORING_SETUP_SINGLE_ISSUER);
#else
    check_equal(setup_calls, 2u);
    check_true((setup_flags[0] & IORING_SETUP_DEFER_TASKRUN) != 0u);
    check_equal(setup_flags[1], (unsigned)IORING_SETUP_SINGLE_ISSUER);
#endif
    salts_io_uring_impl *impl = backend.impl;
    check_false(impl->defer_taskrun);
    check_false(impl->taskrun_flag);
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
#endif

  it("falls back from unsupported single-owner flags to conservative io_uring") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};

    enable_ring_wait = true;
#if defined(IORING_SETUP_TASKRUN_FLAG) && defined(IORING_SQ_TASKRUN) && \
    defined(IORING_SQ_CQ_OVERFLOW) && defined(IORING_SETUP_DEFER_TASKRUN) && \
    defined(IORING_FEAT_EXT_ARG) && defined(IORING_ENTER_EXT_ARG)
    reject_nonzero_setups = 3u;
#elif defined(IORING_SETUP_DEFER_TASKRUN) && defined(IORING_FEAT_EXT_ARG) && \
      defined(IORING_ENTER_EXT_ARG)
    reject_nonzero_setups = 2u;
#else
    reject_nonzero_setups = 1u;
#endif
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
#if defined(IORING_SETUP_TASKRUN_FLAG) && defined(IORING_SQ_TASKRUN) && \
    defined(IORING_SQ_CQ_OVERFLOW) && defined(IORING_SETUP_DEFER_TASKRUN) && \
    defined(IORING_FEAT_EXT_ARG) && defined(IORING_ENTER_EXT_ARG)
    check_equal(setup_calls, 4u);
    check_true((setup_flags[0] & IORING_SETUP_TASKRUN_FLAG) != 0u);
    check_equal(setup_flags[1],
                (unsigned)(IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN));
    check_equal(setup_flags[2], (unsigned)IORING_SETUP_SINGLE_ISSUER);
    check_equal(setup_flags[3], 0u);
#elif defined(IORING_SETUP_DEFER_TASKRUN) && defined(IORING_FEAT_EXT_ARG) && \
      defined(IORING_ENTER_EXT_ARG)
    check_equal(setup_calls, 3u);
    check_true((setup_flags[0] & IORING_SETUP_DEFER_TASKRUN) != 0u);
    check_equal(setup_flags[1], (unsigned)IORING_SETUP_SINGLE_ISSUER);
    check_equal(setup_flags[2], 0u);
#else
    check_equal(setup_calls, 2u);
    check_true((setup_flags[0] & IORING_SETUP_SINGLE_ISSUER) != 0u);
    check_equal(setup_flags[1], 0u);
#endif
    check_equal(native_io_backend_close(&backend), SALTS_OK);
    check_equal(native_io_backend_destroy(&backend), SALTS_OK);
  }
#endif

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
#if defined(IORING_SETUP_DEFER_TASKRUN) && defined(IORING_FEAT_EXT_ARG) && \
    defined(IORING_ENTER_EXT_ARG)
  it("runs deferred task work during zero-timeout observe") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};
    int descriptors[2] = {-1, -1};
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    unsigned char received = 0u;
    const unsigned char sent = 0x71u;
    size_t count = 0u;

    enable_ring_wait = true;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    salts_io_uring_impl *impl = backend.impl;
    if (!impl->defer_taskrun) {
      check_equal(native_io_backend_close(&backend), SALTS_OK);
      check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    } else {
      check_equal(pipe(descriptors), 0);
      check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                                NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                  SALTS_OK);
      check_equal(write(descriptors[1], &sent, 1u), (ssize_t)1);
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                             .endpoint = endpoint,
                                             .buffer = &received,
                                             .length = 1u,
                                             .user_data = 31u};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
      check_equal(native_io_backend_flush(&backend), SALTS_OK);

      enter_calls = 0u;
      poll_calls = 0u;
      check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count), SALTS_OK);
      check_equal(count, 1u);
      check_equal(event.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(event.user_data, (uintptr_t)31u);
      check_equal(event.bytes, 1u);
      check_equal(received, sent);
      /* A ready CQE may already be visible after flush. Do not force an
       * unnecessary GETEVENTS when the completion can be returned directly. */
      check_equal(poll_calls, 0u);

      count = SIZE_MAX;
      enter_calls = 0u;
      check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count), SALTS_ETIMEDOUT);
      check_equal(count, 0u);
      check_equal(enter_calls, impl->taskrun_flag ? 0u : 1u);
      check_equal(poll_calls, 0u);

#if defined(IORING_SQ_TASKRUN) && defined(IORING_SQ_CQ_OVERFLOW)
      if (impl->taskrun_flag) {
        _Atomic unsigned synthetic_flags;
        unsigned *const real_flags = impl->sq_flags;
        atomic_init(&synthetic_flags, IORING_SQ_TASKRUN);

        /* Deterministically exercise the userspace progress decision without
         * mutating the kernel-owned SQ flag word. */
        impl->sq_flags = (unsigned *)&synthetic_flags;
        enter_calls = 0u;
        count = SIZE_MAX;
        check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count),
                    SALTS_ETIMEDOUT);
        check_equal(count, 0u);
        check_equal(enter_calls, 1u);

        atomic_store_explicit(&synthetic_flags, IORING_SQ_CQ_OVERFLOW,
                              memory_order_release);
        enter_calls = 0u;
        count = SIZE_MAX;
        check_equal(native_io_backend_observe(&backend, &event, 1u, 0u, &count),
                    SALTS_ETIMEDOUT);
        check_equal(count, 0u);
        check_equal(enter_calls, 1u);
        impl->sq_flags = real_flags;
      }
#endif

      check_equal(close(descriptors[0]), 0);
      check_equal(close(descriptors[1]), 0);
      check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_OK);
      check_equal(native_io_backend_close(&backend), SALTS_OK);
      check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    }
  }
#endif

  it("uses one ring enter and no outer poll for a finite prepared wait") {
    native_io_backend backend = {0};
    const native_io_backend_config config = {NATIVE_IO_BACKEND_IO_URING, 1u, 1u, 1u};
    int descriptors[2] = {-1, -1};
    native_io_endpoint endpoint = {0};
    native_io_request request = {0};
    native_io_completion event = {0};
    unsigned char received = 0u;
    const unsigned char sent = 0x6du;
    size_t count = 0u;
    enable_ring_wait = true;
    check_equal(batch_test_backend_init(&backend, &config), SALTS_OK);
    salts_io_uring_impl *impl = backend.impl;
    if (!impl->ring_native_wait) {
      check_equal(native_io_backend_close(&backend), SALTS_OK);
      check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    } else {
      /* Exclude the one-time internal wake-poll arm from the operation wait count. */
      enter_calls = 0u;
      memset(enter_sizes, 0, sizeof(enter_sizes));
      poll_calls = 0u;
      check_equal(pipe(descriptors), 0);
      check_equal(native_io_backend_attach_pipe(&backend, (uintptr_t)descriptors[0],
                                                NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
                  SALTS_OK);
      check_equal(write(descriptors[1], &sent, 1u), (ssize_t)1);
      const native_io_operation operation = {.kind = NATIVE_IO_OPERATION_PIPE_READ,
                                             .endpoint = endpoint,
                                             .buffer = &received,
                                             .length = 1u,
                                             .user_data = 17u};
      check_equal(native_io_backend_prepare(&backend, &operation, &request), SALTS_OK);
      check_equal(native_io_backend_observe(&backend, &event, 1u, BATCH_TEST_TIMEOUT_MS, &count),
                  SALTS_OK);
      check_equal(count, 1u);
      check_equal(event.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(event.bytes, 1u);
      check_equal(event.user_data, (uintptr_t)17u);
      check_equal(received, sent);
      check_equal(enter_calls, 1u);
      check_equal(enter_sizes[0], 1u);
      check_equal(poll_calls, 0u);

      enter_calls = 0u;
      poll_calls = 0u;
      count = SIZE_MAX;
      check_equal(native_io_backend_observe(&backend, &event, 1u, 10u, &count), SALTS_ETIMEDOUT);
      check_equal(count, 0u);
      check_equal(enter_calls, 1u);
      check_equal(enter_sizes[0], 0u);
      check_equal(poll_calls, 0u);

      check_equal(close(descriptors[0]), 0);
      check_equal(close(descriptors[1]), 0);
      check_equal(native_io_backend_release_pipe(&backend, endpoint), SALTS_OK);
      check_equal(native_io_backend_close(&backend), SALTS_OK);
      check_equal(native_io_backend_destroy(&backend), SALTS_OK);
    }
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
