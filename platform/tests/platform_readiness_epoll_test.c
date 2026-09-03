#include "tinytest.h"

#include "../src/readiness_internal.h"
#include "readiness_backend_contract.h"

#include <salts/error_codes.h>
#include <salts/readiness.h>
#include <salts/thread.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

enum {
  EPOLL_TEST_TIMEOUT_NS = 2000000000ULL,
  EPOLL_TEST_QUIESCENCE_YIELDS = 100000,
  EPOLL_STRESS_CAPACITY = 3,
  EPOLL_STRESS_ITERATIONS = 32
};

typedef struct epoll_callback_probe {
  salts_readiness_reactor *reactor;
  salts_mutex_t mutex;
  salts_cond_t changed;
  size_t calls;
  salts_readiness_events events;
  int status;
  int blocked;
  int entered;
  int shutdown_completed;
  int shutdown_status;
} epoll_callback_probe;

typedef struct epoll_shutdown_args {
  salts_readiness_reactor *reactor;
  salts_mutex_t mutex;
  salts_cond_t changed;
  int completed;
  int status;
} epoll_shutdown_args;

struct readiness_backend_contract_fixture {
  int (*pipes)[2];
  size_t resource_count;
};

static void probe_init(epoll_callback_probe *probe) {
  probe->reactor = NULL;
  probe->mutex = NULL;
  probe->changed = NULL;
  probe->calls = 0;
  probe->events = 0;
  probe->status = SALTS_OK;
  probe->blocked = 0;
  probe->entered = 0;
  probe->shutdown_completed = 0;
  probe->shutdown_status = SALTS_EIO;
  salts_mutex_init(&probe->mutex);
  salts_cond_init(&probe->changed);
}

static void probe_destroy(epoll_callback_probe *probe) {
  salts_cond_destroy(&probe->changed);
  salts_mutex_destroy(&probe->mutex);
}

static void record_callback(void *user, salts_readiness_events events, int status) {
  epoll_callback_probe *probe = (epoll_callback_probe *)user;
  salts_mutex_lock(&probe->mutex);
  probe->calls += 1u;
  probe->events = events;
  probe->status = status;
  probe->entered = 1;
  salts_cond_broadcast(&probe->changed);
  while (probe->blocked)
    salts_cond_wait(&probe->changed, &probe->mutex);
  salts_mutex_unlock(&probe->mutex);
}

static void shutdown_from_callback(void *user, salts_readiness_events events,
                                   int status) {
  epoll_callback_probe *probe = (epoll_callback_probe *)user;
  int shutdown_status;
  record_callback(user, events, status);
  shutdown_status = salts_readiness_reactor_shutdown(probe->reactor);
  salts_mutex_lock(&probe->mutex);
  probe->shutdown_status = shutdown_status;
  probe->shutdown_completed = 1;
  salts_cond_broadcast(&probe->changed);
  salts_mutex_unlock(&probe->mutex);
}

static int probe_wait_calls(epoll_callback_probe *probe, size_t calls) {
  int status = SALTS_OK;
  salts_mutex_lock(&probe->mutex);
  while (probe->calls < calls && status == SALTS_OK)
    status = salts_cond_timedwait(&probe->changed, &probe->mutex, EPOLL_TEST_TIMEOUT_NS);
  salts_mutex_unlock(&probe->mutex);
  return status;
}

static int probe_wait_entered(epoll_callback_probe *probe) {
  int status = SALTS_OK;
  salts_mutex_lock(&probe->mutex);
  while (!probe->entered && status == SALTS_OK)
    status = salts_cond_timedwait(&probe->changed, &probe->mutex, EPOLL_TEST_TIMEOUT_NS);
  salts_mutex_unlock(&probe->mutex);
  return status;
}

static int probe_wait_shutdown(epoll_callback_probe *probe) {
  int status = SALTS_OK;
  salts_mutex_lock(&probe->mutex);
  while (!probe->shutdown_completed && status == SALTS_OK)
    status = salts_cond_timedwait(&probe->changed, &probe->mutex,
                                  EPOLL_TEST_TIMEOUT_NS);
  salts_mutex_unlock(&probe->mutex);
  return status;
}

static size_t probe_calls(epoll_callback_probe *probe) {
  size_t calls;
  salts_mutex_lock(&probe->mutex);
  calls = probe->calls;
  salts_mutex_unlock(&probe->mutex);
  return calls;
}

static int wait_callbacks_quiescent(salts_readiness_reactor *reactor) {
  for (size_t i = 0; i < EPOLL_TEST_QUIESCENCE_YIELDS; ++i) {
    salts_readiness_stats stats;
    int status = salts_readiness_reactor_stats(reactor, &stats);
    if (status != SALTS_OK) return status;
    if (stats.callbacks_inflight == 0) return SALTS_OK;
    salts_thread_yield();
  }
  return SALTS_ETIMEDOUT;
}

static void probe_release(epoll_callback_probe *probe) {
  salts_mutex_lock(&probe->mutex);
  probe->blocked = 0;
  salts_cond_broadcast(&probe->changed);
  salts_mutex_unlock(&probe->mutex);
}

static int set_nonblocking_cloexec(int fd) {
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
  return SALTS_OK;
}

static int make_pipe(int fds[2]) {
  int status;
  if (pipe(fds) != 0) return -errno;
  status = set_nonblocking_cloexec(fds[0]);
  if (status == SALTS_OK) status = set_nonblocking_cloexec(fds[1]);
  if (status != SALTS_OK) {
    (void)close(fds[0]);
    (void)close(fds[1]);
  }
  return status;
}

static int make_socket_pair(int fds[2]) {
  int status;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return -errno;
  status = set_nonblocking_cloexec(fds[0]);
  if (status == SALTS_OK) status = set_nonblocking_cloexec(fds[1]);
  if (status != SALTS_OK) {
    (void)close(fds[0]);
    (void)close(fds[1]);
  }
  return status;
}

static int write_byte(int fd, uint8_t value) {
  ssize_t written;
  do {
    written = write(fd, &value, sizeof(value));
  } while (written < 0 && errno == EINTR);
  return written == (ssize_t)sizeof(value) ? SALTS_OK : -errno;
}

static int drain_to_would_block(int fd) {
  uint8_t buffer[32];
  for (;;) {
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) continue;
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return SALTS_OK;
    return count == 0 ? SALTS_EIO : -errno;
  }
}

static readiness_backend_contract_fixture *epoll_backend_contract_create(
    salts_readiness_config config, salts_readiness_reactor *reactor, int *status) {
  readiness_backend_contract_fixture *fixture;
  size_t created = 0;
  if (status == NULL) return NULL;
  *status = salts_readiness_reactor_init(reactor, &config);
  if (*status != SALTS_OK) return NULL;

  fixture = (readiness_backend_contract_fixture *)calloc(1, sizeof(*fixture));
  if (fixture != NULL) {
    fixture->resource_count = config.registration_capacity + 1u;
    fixture->pipes = (int(*)[2])calloc(fixture->resource_count, sizeof(*fixture->pipes));
  }
  if (fixture == NULL || fixture->pipes == NULL) {
    *status = SALTS_ENOMEM;
    goto fail;
  }
  for (size_t i = 0; i < fixture->resource_count; ++i) {
    fixture->pipes[i][0] = -1;
    fixture->pipes[i][1] = -1;
    *status = make_pipe(fixture->pipes[i]);
    if (*status != SALTS_OK) goto fail;
    created += 1u;
  }
  return fixture;

fail:
  if (fixture != NULL && fixture->pipes != NULL) {
    for (size_t i = 0; i < created; ++i) {
      (void)close(fixture->pipes[i][0]);
      (void)close(fixture->pipes[i][1]);
    }
  }
  free(fixture != NULL ? fixture->pipes : NULL);
  free(fixture);
  (void)salts_readiness_reactor_shutdown(reactor);
  (void)salts_readiness_reactor_destroy(reactor);
  return NULL;
}

static void epoll_backend_contract_destroy(readiness_backend_contract_fixture *fixture) {
  for (size_t i = 0; i < fixture->resource_count; ++i) {
    (void)close(fixture->pipes[i][0]);
    (void)close(fixture->pipes[i][1]);
  }
  free(fixture->pipes);
  free(fixture);
}

static intptr_t epoll_backend_contract_resource(readiness_backend_contract_fixture *fixture,
                                                size_t index) {
  return (intptr_t)fixture->pipes[index][0];
}

static int epoll_backend_contract_make_readable(readiness_backend_contract_fixture *fixture,
                                                size_t index) {
  return write_byte(fixture->pipes[index][1], (uint8_t)(index + 1u));
}

static int epoll_backend_contract_drain_readable(readiness_backend_contract_fixture *fixture,
                                                 size_t index) {
  return drain_to_would_block(fixture->pipes[index][0]);
}

const readiness_backend_contract_factory *readiness_backend_contract_factory_get(void) {
  static const readiness_backend_contract_factory factory = {
      epoll_backend_contract_create, epoll_backend_contract_destroy,
      epoll_backend_contract_resource, epoll_backend_contract_make_readable,
      epoll_backend_contract_drain_readable};
  return &factory;
}

static void shutdown_entry(void *user) {
  epoll_shutdown_args *args = (epoll_shutdown_args *)user;
  int status = salts_readiness_reactor_shutdown(args->reactor);
  salts_mutex_lock(&args->mutex);
  args->status = status;
  args->completed = 1;
  salts_cond_broadcast(&args->changed);
  salts_mutex_unlock(&args->mutex);
}

static void shutdown_args_init(epoll_shutdown_args *args,
                               salts_readiness_reactor *reactor) {
  args->reactor = reactor;
  args->mutex = NULL;
  args->changed = NULL;
  args->completed = 0;
  args->status = SALTS_EIO;
  salts_mutex_init(&args->mutex);
  salts_cond_init(&args->changed);
}

static void shutdown_args_destroy(epoll_shutdown_args *args) {
  salts_cond_destroy(&args->changed);
  salts_mutex_destroy(&args->mutex);
}

spec("Platform epoll readiness") {
  it("rejects the first shutdown invoked from the reactor callback") {
    const salts_readiness_config config = {1u, 1u};
    salts_readiness_reactor reactor = {0};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    probe.reactor = &reactor;
    check_equal(make_pipe(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration),
                SALTS_OK);
    check_equal(salts_readiness_arm(&registration,
                                    SALTS_READINESS_EVENT_READ,
                                    shutdown_from_callback, &probe),
                SALTS_OK);
    check_equal(write_byte(fds[1], 1u), SALTS_OK);
    check_equal(probe_wait_shutdown(&probe), SALTS_OK);
    check_equal(probe.shutdown_status, SALTS_EBUSY);
    check_not_null(registration.impl);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("does not subscribe read readiness for hangup-only or error-only arms") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    uint32_t hangup_interest;
    uint32_t error_interest;
    int fds[2];

    hangup_interest =
        salts_readiness_epoll_interest_events(SALTS_READINESS_EVENT_HANGUP);
    error_interest =
        salts_readiness_epoll_interest_events(SALTS_READINESS_EVENT_ERROR);
    check_equal(hangup_interest & (uint32_t)(EPOLLIN | EPOLLOUT), (uint32_t)0);
    check_equal(error_interest & (uint32_t)(EPOLLIN | EPOLLOUT), (uint32_t)0);

    probe_init(&probe);
    check_equal(make_socket_pair(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_HANGUP,
                                    record_callback, &probe),
                SALTS_OK);
    check_equal(write_byte(fds[1], 9), SALTS_OK);
    check_equal(shutdown(fds[1], SHUT_WR), 0);
    check_equal(probe_wait_calls(&probe, 1), SALTS_OK);
    check_equal(probe_calls(&probe), (size_t)1);
    check_equal(probe.events, SALTS_READINESS_EVENT_HANGUP);
    check_equal(probe.status, SALTS_OK);

    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("enforces bounded native registration capacity") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {2, 2};
    salts_readiness_registration first = {0};
    salts_readiness_registration second = {0};
    salts_readiness_registration rejected = {(void *)(uintptr_t)1, 0u};
    salts_readiness_stats stats;
    int first_pipe[2];
    int second_pipe[2];
    int third_pipe[2];

    check_equal(make_pipe(first_pipe), SALTS_OK);
    check_equal(make_pipe(second_pipe), SALTS_OK);
    check_equal(make_pipe(third_pipe), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, first_pipe[0], &first), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, second_pipe[0], &second), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, third_pipe[0], &rejected), SALTS_ENOBUFS);
    check_null(rejected.impl);
    check_equal(salts_readiness_reactor_stats(&reactor, &stats), SALTS_OK);
    check_equal(stats.registered_count, (size_t)2);
    check_equal(stats.rejected_full, (uint64_t)1);

    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_close(&first), SALTS_OK);
    check_equal(salts_readiness_close(&second), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(first_pipe[0]);
    (void)close(first_pipe[1]);
    (void)close(second_pipe[0]);
    (void)close(second_pipe[1]);
    (void)close(third_pipe[0]);
    (void)close(third_pipe[1]);
  }

  it("delivers readable once per explicit arm") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(write_byte(fds[1], 1), SALTS_OK);
    check_equal(probe_wait_calls(&probe, 1), SALTS_OK);
    check_equal(wait_callbacks_quiescent(&reactor), SALTS_OK);
    check_equal(probe.events & SALTS_READINESS_EVENT_READ, SALTS_READINESS_EVENT_READ);
    check_equal(probe.status, SALTS_OK);
    check_equal(drain_to_would_block(fds[0]), SALTS_OK);

    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(write_byte(fds[1], 2), SALTS_OK);
    check_equal(probe_wait_calls(&probe, 2), SALTS_OK);
    check_equal(probe_calls(&probe), (size_t)2);

    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("reports peer hangup through the generic event mask") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_socket_pair(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    (void)close(fds[1]);
    check_equal(probe_wait_calls(&probe, 1), SALTS_OK);
    check_equal(probe.events & SALTS_READINESS_EVENT_HANGUP, SALTS_READINESS_EVENT_HANGUP);
    check_equal(probe.status, SALTS_OK);

    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    probe_destroy(&probe);
  }

  it("unarms without consuming later level readiness") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(salts_readiness_unarm(&registration), SALTS_OK);
    check_equal(write_byte(fds[1], 3), SALTS_OK);
    check_equal(probe_calls(&probe), (size_t)0);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(probe_wait_calls(&probe, 1), SALTS_OK);
    check_equal(probe.events & SALTS_READINESS_EVENT_READ, SALTS_READINESS_EVENT_READ);

    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("close unregisters without closing the borrowed descriptor") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(write_byte(fds[1], 4), SALTS_OK);
    check_equal(drain_to_would_block(fds[0]), SALTS_OK);
    check_equal(probe_calls(&probe), (size_t)0);

    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("preserves the registration when DEL reports EBADF") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int original_pipe[2];
    int borrowed_fd;
    int saved_fd = -1;

    probe_init(&probe);
    check_equal(make_pipe(original_pipe), SALTS_OK);
    borrowed_fd = original_pipe[0];
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, borrowed_fd, &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    record_callback, &probe),
                SALTS_OK);
    saved_fd = dup(borrowed_fd);
    check_greater(saved_fd, -1);
    check_not_equal(saved_fd, borrowed_fd);
    check_equal(fcntl(saved_fd, F_SETFD, FD_CLOEXEC), 0);
    check_equal(close(original_pipe[0]), 0);
    original_pipe[0] = -1;

    check_equal(salts_readiness_unarm(&registration), -EBADF);
    check_not_null(registration.impl);

    check_equal(dup2(saved_fd, borrowed_fd), borrowed_fd);
    original_pipe[0] = borrowed_fd;
    check_equal(close(saved_fd), 0);
    saved_fd = -1;
    check_equal(salts_readiness_unarm(&registration), SALTS_OK);
    check_not_null(registration.impl);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ,
                                    record_callback, &probe),
                SALTS_OK);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_null(registration.impl);

    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(original_pipe[0]);
    (void)close(original_pipe[1]);
    if (saved_fd >= 0) (void)close(saved_fd);
    probe_destroy(&probe);
  }

  it("filters stale events when a descriptor number is reused") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {2, 2};
    salts_readiness_registration blocker_registration = {0};
    salts_readiness_registration old_registration = {0};
    salts_readiness_registration new_registration = {0};
    epoll_callback_probe blocker_probe;
    epoll_callback_probe old_probe;
    epoll_callback_probe new_probe;
    salts_readiness_stats stats;
    int blocker_pipe[2];
    int old_pipe[2];
    int new_pipe[2];
    int reused_fd;

    probe_init(&blocker_probe);
    probe_init(&old_probe);
    probe_init(&new_probe);
    check_equal(make_pipe(blocker_pipe), SALTS_OK);
    check_equal(make_pipe(old_pipe), SALTS_OK);
    reused_fd = old_pipe[0];
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, blocker_pipe[0], &blocker_registration),
                SALTS_OK);
    check_equal(salts_readiness_register(&reactor, reused_fd, &old_registration), SALTS_OK);
    salts_mutex_lock(&blocker_probe.mutex);
    blocker_probe.blocked = 1;
    salts_mutex_unlock(&blocker_probe.mutex);
    check_equal(salts_readiness_arm(&blocker_registration, SALTS_READINESS_EVENT_READ,
                                    record_callback, &blocker_probe),
                SALTS_OK);
    check_equal(write_byte(blocker_pipe[1], 4), SALTS_OK);
    check_equal(probe_wait_entered(&blocker_probe), SALTS_OK);

    check_equal(salts_readiness_arm(&old_registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &old_probe),
                SALTS_OK);
    check_equal(write_byte(old_pipe[1], 5), SALTS_OK);
    check_equal(salts_readiness_close(&old_registration), SALTS_OK);
    (void)close(old_pipe[0]);
    (void)close(old_pipe[1]);

    check_equal(make_pipe(new_pipe), SALTS_OK);
    if (new_pipe[0] != reused_fd) {
      if (new_pipe[1] == reused_fd) {
        int moved_write_fd;
        do {
          moved_write_fd = fcntl(new_pipe[1], F_DUPFD, reused_fd + 1);
        } while (moved_write_fd < 0 && errno == EINTR);
        check_greater(moved_write_fd, reused_fd);
        check_equal(set_nonblocking_cloexec(moved_write_fd), SALTS_OK);
        (void)close(new_pipe[1]);
        new_pipe[1] = moved_write_fd;
      }
      check_equal(dup2(new_pipe[0], reused_fd), reused_fd);
      (void)close(new_pipe[0]);
      new_pipe[0] = reused_fd;
      check_equal(set_nonblocking_cloexec(new_pipe[0]), SALTS_OK);
    }
    check_equal(salts_readiness_register(&reactor, new_pipe[0], &new_registration), SALTS_OK);
    check_equal(salts_readiness_arm(&new_registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &new_probe),
                SALTS_OK);
    probe_release(&blocker_probe);
    check_equal(write_byte(new_pipe[1], 6), SALTS_OK);
    check_equal(probe_wait_calls(&new_probe, 1), SALTS_OK);
    check_equal(probe_calls(&new_probe), (size_t)1);
    check_equal(probe_calls(&old_probe), (size_t)0);
    check_equal(salts_readiness_reactor_stats(&reactor, &stats), SALTS_OK);
    check_equal(stats.stale_events, (uint64_t)0);

    check_equal(salts_readiness_close(&new_registration), SALTS_OK);
    check_equal(salts_readiness_close(&blocker_registration), SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(new_pipe[0]);
    (void)close(new_pipe[1]);
    (void)close(blocker_pipe[0]);
    (void)close(blocker_pipe[1]);
    probe_destroy(&blocker_probe);
    probe_destroy(&old_probe);
    probe_destroy(&new_probe);
  }

  it("shutdown delivers the exact terminal status to an armed registration") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(probe_calls(&probe), (size_t)1);
    check_equal(probe.events, (salts_readiness_events)0);
    check_equal(probe.status, SALTS_ESHUTDOWN);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("shutdown joins the reactor thread after an inflight callback") {
    salts_readiness_reactor reactor = {0};
    salts_readiness_config config = {1, 1};
    salts_readiness_registration registration = {0};
    epoll_callback_probe probe;
    epoll_shutdown_args shutdown_args;
    salts_thread_t shutdown_thread = NULL;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), SALTS_OK);
    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_OK);
    check_equal(salts_readiness_register(&reactor, fds[0], &registration), SALTS_OK);
    salts_mutex_lock(&probe.mutex);
    probe.blocked = 1;
    salts_mutex_unlock(&probe.mutex);
    check_equal(salts_readiness_arm(&registration, SALTS_READINESS_EVENT_READ, record_callback,
                                    &probe),
                SALTS_OK);
    check_equal(write_byte(fds[1], 6), SALTS_OK);
    check_equal(probe_wait_entered(&probe), SALTS_OK);

    shutdown_args_init(&shutdown_args, &reactor);
    check_equal(salts_thread_create(&shutdown_thread, shutdown_entry, &shutdown_args), SALTS_OK);
    check_equal(salts_readiness_backend_wait_admission_closed(&reactor), SALTS_OK);
    salts_mutex_lock(&shutdown_args.mutex);
    check_false(shutdown_args.completed);
    salts_mutex_unlock(&shutdown_args.mutex);

    probe_release(&probe);
    check_equal(salts_thread_join(&shutdown_thread), SALTS_OK);
    check_equal(shutdown_args.status, SALTS_OK);
    check_equal(salts_readiness_close(&registration), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    shutdown_args_destroy(&shutdown_args);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("keeps bounded native registrations quiescent across repeated reuse") {
    const salts_readiness_config config = {EPOLL_STRESS_CAPACITY, 2u};
    salts_readiness_reactor reactor = {0};
    readiness_backend_contract_fixture *fixture;
    epoll_callback_probe probes[EPOLL_STRESS_CAPACITY];
    int init_status = SALTS_EINVAL;

    fixture = epoll_backend_contract_create(config, &reactor, &init_status);
    check_equal(init_status, SALTS_OK);
    check_not_null(fixture);
    for (size_t index = 0; index < EPOLL_STRESS_CAPACITY; ++index)
      probe_init(&probes[index]);

    for (size_t iteration = 0; iteration < EPOLL_STRESS_ITERATIONS; ++iteration) {
      salts_readiness_registration registrations[EPOLL_STRESS_CAPACITY] = {{0}};
      salts_readiness_registration rejected = {(void *)(uintptr_t)1u, 0u};
      salts_readiness_stats stats = {0};

      for (size_t index = 0; index < EPOLL_STRESS_CAPACITY; ++index) {
        check_equal(salts_readiness_register(
                        &reactor, epoll_backend_contract_resource(fixture, index),
                        &registrations[index]),
                    SALTS_OK);
      }
      check_equal(salts_readiness_register(
                      &reactor,
                      epoll_backend_contract_resource(fixture, EPOLL_STRESS_CAPACITY),
                      &rejected),
                  SALTS_ENOBUFS);
      check_null(rejected.impl);

      for (size_t index = 0; index < EPOLL_STRESS_CAPACITY; ++index) {
        size_t calls_before = probe_calls(&probes[index]);

        check_equal(salts_readiness_arm(&registrations[index], SALTS_READINESS_EVENT_READ,
                                        record_callback, &probes[index]),
                    SALTS_OK);
        check_equal(epoll_backend_contract_make_readable(fixture, index), SALTS_OK);
        check_equal(probe_wait_calls(&probes[index], calls_before + 1u), SALTS_OK);
        check_equal(salts_readiness_close(&registrations[index]), SALTS_OK);
        check_null(registrations[index].impl);
        check_equal(epoll_backend_contract_drain_readable(fixture, index), SALTS_OK);
        check_equal(salts_readiness_register(
                        &reactor, epoll_backend_contract_resource(fixture, index),
                        &registrations[index]),
                    SALTS_OK);

        check_equal(salts_readiness_arm(&registrations[index], SALTS_READINESS_EVENT_READ,
                                        record_callback, &probes[index]),
                    SALTS_OK);
        check_equal(salts_readiness_unarm(&registrations[index]), SALTS_OK);
        check_equal(epoll_backend_contract_make_readable(fixture, index), SALTS_OK);
        check_equal(salts_readiness_arm(&registrations[index], SALTS_READINESS_EVENT_READ,
                                        record_callback, &probes[index]),
                    SALTS_OK);
        check_equal(probe_wait_calls(&probes[index], calls_before + 2u), SALTS_OK);
        check_equal(salts_readiness_close(&registrations[index]), SALTS_OK);
        check_null(registrations[index].impl);
        check_equal(probe_calls(&probes[index]), calls_before + 2u);
        check_equal(epoll_backend_contract_drain_readable(fixture, index), SALTS_OK);
      }

      check_equal(salts_readiness_reactor_stats(&reactor, &stats), SALTS_OK);
      check_equal(stats.registered_count, (size_t)0u);
      check_equal(stats.armed_count, (size_t)0u);
      check_equal(stats.callbacks_inflight, (size_t)0u);
      check_equal(stats.rejected_full, (uint64_t)(iteration + 1u));
    }

    check_equal(salts_readiness_reactor_shutdown(&reactor), SALTS_OK);
    check_equal(salts_readiness_reactor_destroy(&reactor), SALTS_OK);
    for (size_t index = 0; index < EPOLL_STRESS_CAPACITY; ++index)
      probe_destroy(&probes[index]);
    epoll_backend_contract_destroy(fixture);
  }
}
