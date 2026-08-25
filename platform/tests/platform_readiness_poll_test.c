#include <turbo/error_codes.h>
#include <turbo/readiness.h>
#include <turbo/thread.h>

#include "../src/readiness_internal.h"
#include "readiness_backend_contract.h"
#include "tinytest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

struct readiness_backend_contract_fixture {
  int (*pipes)[2];
  size_t resource_count;
};

static const uint64_t POLL_TEST_TIMEOUT_NS = UINT64_C(2000000000);

typedef struct poll_fairness_probe {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  size_t first_calls;
  size_t second_calls;
} poll_fairness_probe;

typedef struct poll_callback_probe {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  size_t calls;
  turbo_readiness_events events;
  int status;
  int blocked;
  int entered;
} poll_callback_probe;

typedef struct poll_shutdown_args {
  turbo_readiness_reactor *reactor;
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  int completed;
  int status;
} poll_shutdown_args;

static void poll_probe_init(poll_callback_probe *probe) {
  *probe = (poll_callback_probe){0};
  turbo_mutex_init(&probe->mutex);
  turbo_cond_init(&probe->changed);
}

static void poll_probe_destroy(poll_callback_probe *probe) {
  turbo_cond_destroy(&probe->changed);
  turbo_mutex_destroy(&probe->mutex);
}

static void poll_record_callback(void *user, turbo_readiness_events events, int status) {
  poll_callback_probe *probe = (poll_callback_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  ++probe->calls;
  probe->events = events;
  probe->status = status;
  probe->entered = 1;
  turbo_cond_broadcast(&probe->changed);
  while (probe->blocked)
    turbo_cond_wait(&probe->changed, &probe->mutex);
  turbo_mutex_unlock(&probe->mutex);
}

static int poll_probe_wait_calls(poll_callback_probe *probe, size_t calls) {
  int status = TURBO_OK;
  turbo_mutex_lock(&probe->mutex);
  while (probe->calls < calls && status == TURBO_OK)
    status = turbo_cond_timedwait(&probe->changed, &probe->mutex, POLL_TEST_TIMEOUT_NS);
  turbo_mutex_unlock(&probe->mutex);
  return status;
}

static void poll_probe_release(poll_callback_probe *probe) {
  turbo_mutex_lock(&probe->mutex);
  probe->blocked = 0;
  turbo_cond_broadcast(&probe->changed);
  turbo_mutex_unlock(&probe->mutex);
}

static void poll_shutdown_entry(void *user) {
  poll_shutdown_args *args = (poll_shutdown_args *)user;
  int status = turbo_readiness_reactor_shutdown(args->reactor);
  turbo_mutex_lock(&args->mutex);
  args->status = status;
  args->completed = 1;
  turbo_cond_broadcast(&args->changed);
  turbo_mutex_unlock(&args->mutex);
}

static void poll_shutdown_args_init(poll_shutdown_args *args, turbo_readiness_reactor *reactor) {
  *args = (poll_shutdown_args){0};
  args->reactor = reactor;
  args->status = TURBO_EIO;
  turbo_mutex_init(&args->mutex);
  turbo_cond_init(&args->changed);
}

static void poll_shutdown_args_destroy(poll_shutdown_args *args) {
  turbo_cond_destroy(&args->changed);
  turbo_mutex_destroy(&args->mutex);
}

static turbo_readiness_callback_result
poll_test_rearm_first(void *user, turbo_readiness_events events, int status) {
  poll_fairness_probe *probe = (poll_fairness_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  ++probe->first_calls;
  turbo_mutex_unlock(&probe->mutex);
  return status == TURBO_OK && (events & TURBO_READINESS_EVENT_READ) != 0u
             ? (turbo_readiness_callback_result){TURBO_READINESS_REARM, TURBO_READINESS_EVENT_READ}
             : (turbo_readiness_callback_result){TURBO_READINESS_COMPLETE, 0u};
}

static turbo_readiness_callback_result
poll_test_complete_second(void *user, turbo_readiness_events events, int status) {
  poll_fairness_probe *probe = (poll_fairness_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  if (status == TURBO_OK && (events & TURBO_READINESS_EVENT_READ) != 0u) ++probe->second_calls;
  turbo_cond_broadcast(&probe->changed);
  turbo_mutex_unlock(&probe->mutex);
  return (turbo_readiness_callback_result){TURBO_READINESS_COMPLETE, 0u};
}

static int poll_test_set_nonblocking_cloexec(int fd) {
  int flags;
  do {
    flags = fcntl(fd, F_GETFL);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) return -errno;
  while (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    if (errno != EINTR) return -errno;
  }
  do {
    flags = fcntl(fd, F_GETFD);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) return -errno;
  while (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
    if (errno != EINTR) return -errno;
  }
  return TURBO_OK;
}

static int poll_test_make_pipe(int fds[2]) {
  int status;
  if (pipe(fds) != 0) return -errno;
  status = poll_test_set_nonblocking_cloexec(fds[0]);
  if (status == TURBO_OK) status = poll_test_set_nonblocking_cloexec(fds[1]);
  if (status != TURBO_OK) {
    (void)close(fds[0]);
    (void)close(fds[1]);
  }
  return status;
}

static int poll_test_make_socket_pair(int fds[2]) {
  int status;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return -errno;
  status = poll_test_set_nonblocking_cloexec(fds[0]);
  if (status == TURBO_OK) status = poll_test_set_nonblocking_cloexec(fds[1]);
  if (status != TURBO_OK) {
    (void)close(fds[0]);
    (void)close(fds[1]);
  }
  return status;
}

static int poll_test_write_byte(int fd, uint8_t value) {
  ssize_t written;
  do {
    written = write(fd, &value, sizeof(value));
  } while (written < 0 && errno == EINTR);
  return written == (ssize_t)sizeof(value) ? TURBO_OK : written < 0 ? -errno : TURBO_EIO;
}

static int poll_test_drain(int fd) {
  uint8_t bytes[32];
  for (;;) {
    ssize_t count = read(fd, bytes, sizeof(bytes));
    if (count > 0) continue;
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return TURBO_OK;
    return count == 0 ? TURBO_EIO : -errno;
  }
}

static void poll_contract_destroy(readiness_backend_contract_fixture *fixture) {
  if (fixture == NULL) return;
  for (size_t i = 0; i < fixture->resource_count; ++i) {
    (void)close(fixture->pipes[i][0]);
    (void)close(fixture->pipes[i][1]);
  }
  free(fixture->pipes);
  free(fixture);
}

static readiness_backend_contract_fixture *
poll_contract_create(turbo_readiness_config config, turbo_readiness_reactor *reactor, int *status) {
  readiness_backend_contract_fixture *fixture = NULL;
  size_t created = 0u;
  if (status == NULL) return NULL;
  *status = turbo_readiness_reactor_init_kind(reactor, &config, TURBO_READINESS_BACKEND_POLL);
  if (*status != TURBO_OK) return NULL;

  fixture = (readiness_backend_contract_fixture *)calloc(1u, sizeof(*fixture));
  if (fixture != NULL) {
    fixture->resource_count = config.registration_capacity + 1u;
    fixture->pipes = (int (*)[2])calloc(fixture->resource_count, sizeof(*fixture->pipes));
  }
  if (fixture == NULL || fixture->pipes == NULL) {
    *status = TURBO_ENOMEM;
    goto fail;
  }
  for (size_t i = 0u; i < fixture->resource_count; ++i) {
    fixture->pipes[i][0] = -1;
    fixture->pipes[i][1] = -1;
    *status = poll_test_make_pipe(fixture->pipes[i]);
    if (*status != TURBO_OK) goto fail;
    ++created;
  }
  return fixture;

fail:
  if (fixture != NULL && fixture->pipes != NULL) {
    fixture->resource_count = created;
    poll_contract_destroy(fixture);
  } else {
    free(fixture);
  }
  (void)turbo_readiness_reactor_shutdown(reactor);
  (void)turbo_readiness_reactor_destroy(reactor);
  return NULL;
}

static intptr_t poll_contract_resource(readiness_backend_contract_fixture *fixture, size_t index) {
  return (intptr_t)fixture->pipes[index][0];
}

static int poll_contract_make_readable(readiness_backend_contract_fixture *fixture, size_t index) {
  return poll_test_write_byte(fixture->pipes[index][1], (uint8_t)(index + 1u));
}

static int poll_contract_drain_readable(readiness_backend_contract_fixture *fixture, size_t index) {
  return poll_test_drain(fixture->pipes[index][0]);
}

const readiness_backend_contract_factory *readiness_backend_contract_factory_get(void) {
  static const readiness_backend_contract_factory factory = {
      poll_contract_create, poll_contract_destroy, poll_contract_resource,
      poll_contract_make_readable, poll_contract_drain_readable};
  return &factory;
}

spec("Platform poll readiness selector") {
  it("reports explicit compile-time support") {
    check_true(turbo_readiness_backend_supported(TURBO_READINESS_BACKEND_POLL));
  }

  it("reports socket write readiness and pipe hangup through the generic mask") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    const turbo_readiness_config config = {1u, 1u};
    poll_callback_probe probe;
    int fds[2] = {-1, -1};

    poll_probe_init(&probe);
    check_equal(poll_test_make_socket_pair(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init_kind(&reactor, &config, TURBO_READINESS_BACKEND_POLL),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_WRITE,
                                    poll_record_callback, &probe),
                TURBO_OK);
    check_equal(poll_probe_wait_calls(&probe, 1u), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_WRITE, TURBO_READINESS_EVENT_WRITE);
    check_equal(probe.status, TURBO_OK);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(close(fds[0]), 0);
    check_equal(close(fds[1]), 0);
    fds[0] = -1;
    fds[1] = -1;

    check_equal(poll_test_make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration,
                                    TURBO_READINESS_EVENT_READ |
                                        TURBO_READINESS_EVENT_HANGUP,
                                    poll_record_callback, &probe),
                TURBO_OK);
    check_equal(close(fds[1]), 0);
    fds[1] = -1;
    check_equal(poll_probe_wait_calls(&probe, 2u), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_HANGUP, TURBO_READINESS_EVENT_HANGUP);
    check_equal(probe.status, TURBO_OK);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    poll_probe_destroy(&probe);
  }

  it("maps an invalid borrowed descriptor to the generic error event") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    const turbo_readiness_config config = {1u, 1u};
    poll_callback_probe probe;
    int fds[2] = {-1, -1};

    poll_probe_init(&probe);
    check_equal(poll_test_make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init_kind(&reactor, &config, TURBO_READINESS_BACKEND_POLL),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(close(fds[0]), 0);
    fds[0] = -1;
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, poll_record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(poll_probe_wait_calls(&probe, 1u), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_ERROR, TURBO_READINESS_EVENT_ERROR);
    check_equal(probe.status, TURBO_OK);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[1]);
    poll_probe_destroy(&probe);
  }

  it("joins the worker only after an inflight callback returns") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration registration = {0};
    const turbo_readiness_config config = {1u, 1u};
    poll_callback_probe probe;
    poll_shutdown_args shutdown_args;
    turbo_thread_t shutdown_thread = NULL;
    int fds[2] = {-1, -1};

    poll_probe_init(&probe);
    check_equal(poll_test_make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init_kind(&reactor, &config, TURBO_READINESS_BACKEND_POLL),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    turbo_mutex_lock(&probe.mutex);
    probe.blocked = 1;
    turbo_mutex_unlock(&probe.mutex);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, poll_record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(poll_test_write_byte(fds[1], 3u), TURBO_OK);
    check_equal(poll_probe_wait_calls(&probe, 1u), TURBO_OK);

    poll_shutdown_args_init(&shutdown_args, &reactor);
    check_equal(turbo_thread_create(&shutdown_thread, poll_shutdown_entry, &shutdown_args),
                TURBO_OK);
    check_equal(turbo_readiness_backend_wait_admission_closed(&reactor), TURBO_OK);
    turbo_mutex_lock(&shutdown_args.mutex);
    check_false(shutdown_args.completed);
    turbo_mutex_unlock(&shutdown_args.mutex);

    poll_probe_release(&probe);
    check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);
    check_equal(shutdown_args.status, TURBO_OK);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    poll_shutdown_args_destroy(&shutdown_args);
    (void)close(fds[0]);
    (void)close(fds[1]);
    poll_probe_destroy(&probe);
  }

  it("rotates a bounded batch across continuously ready registrations") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_registration first = {0};
    turbo_readiness_registration second = {0};
    const turbo_readiness_config config = {2u, 1u};
    poll_fairness_probe probe = {0};
    int first_pipe[2] = {-1, -1};
    int second_pipe[2] = {-1, -1};
    int wait_status = TURBO_OK;
    size_t first_calls;
    size_t second_calls;

    turbo_mutex_init(&probe.mutex);
    turbo_cond_init(&probe.changed);
    check_equal(poll_test_make_pipe(first_pipe), TURBO_OK);
    check_equal(poll_test_make_pipe(second_pipe), TURBO_OK);
    check_equal(turbo_readiness_reactor_init_kind(&reactor, &config, TURBO_READINESS_BACKEND_POLL),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, first_pipe[0], &first), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, second_pipe[0], &second), TURBO_OK);
    check_equal(turbo_readiness_arm_continuation(&first, TURBO_READINESS_EVENT_READ,
                                                 poll_test_rearm_first, &probe),
                TURBO_OK);
    check_equal(turbo_readiness_arm_continuation(&second, TURBO_READINESS_EVENT_READ,
                                                 poll_test_complete_second, &probe),
                TURBO_OK);
    check_equal(poll_test_write_byte(first_pipe[1], 1u), TURBO_OK);
    check_equal(poll_test_write_byte(second_pipe[1], 2u), TURBO_OK);

    turbo_mutex_lock(&probe.mutex);
    while (probe.second_calls == 0u && wait_status == TURBO_OK)
      wait_status = turbo_cond_timedwait(&probe.changed, &probe.mutex, POLL_TEST_TIMEOUT_NS);
    first_calls = probe.first_calls;
    second_calls = probe.second_calls;
    turbo_mutex_unlock(&probe.mutex);
    check_equal(wait_status, TURBO_OK);
    check_greater(first_calls, (size_t)0u);
    check_equal(second_calls, (size_t)1u);

    check_equal(poll_test_drain(first_pipe[0]), TURBO_OK);
    check_equal(poll_test_drain(second_pipe[0]), TURBO_OK);
    check_equal(turbo_readiness_close(&first), TURBO_OK);
    check_equal(turbo_readiness_close(&second), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(first_pipe[0]);
    (void)close(first_pipe[1]);
    (void)close(second_pipe[0]);
    (void)close(second_pipe[1]);
    turbo_cond_destroy(&probe.changed);
    turbo_mutex_destroy(&probe.mutex);
  }
}
