#include "tinytest.h"

#include "../src/readiness_internal.h"
#include "readiness_backend_contract.h"

#include <turbo/error_codes.h>
#include <turbo/readiness.h>
#include <turbo/thread.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

enum {
  EPOLL_TEST_TIMEOUT_NS = 2000000000ULL,
  EPOLL_TEST_QUIESCENCE_YIELDS = 100000
};

typedef struct epoll_callback_probe {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  size_t calls;
  turbo_readiness_events events;
  int status;
  int blocked;
  int entered;
} epoll_callback_probe;

typedef struct epoll_shutdown_args {
  turbo_readiness_reactor *reactor;
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  int completed;
  int status;
} epoll_shutdown_args;

struct readiness_backend_contract_fixture {
  int (*pipes)[2];
  size_t resource_count;
};

static void probe_init(epoll_callback_probe *probe) {
  probe->mutex = NULL;
  probe->changed = NULL;
  probe->calls = 0;
  probe->events = 0;
  probe->status = TURBO_OK;
  probe->blocked = 0;
  probe->entered = 0;
  turbo_mutex_init(&probe->mutex);
  turbo_cond_init(&probe->changed);
}

static void probe_destroy(epoll_callback_probe *probe) {
  turbo_cond_destroy(&probe->changed);
  turbo_mutex_destroy(&probe->mutex);
}

static void record_callback(void *user, turbo_readiness_events events, int status) {
  epoll_callback_probe *probe = (epoll_callback_probe *)user;
  turbo_mutex_lock(&probe->mutex);
  probe->calls += 1u;
  probe->events = events;
  probe->status = status;
  probe->entered = 1;
  turbo_cond_broadcast(&probe->changed);
  while (probe->blocked)
    turbo_cond_wait(&probe->changed, &probe->mutex);
  turbo_mutex_unlock(&probe->mutex);
}

static int probe_wait_calls(epoll_callback_probe *probe, size_t calls) {
  int status = TURBO_OK;
  turbo_mutex_lock(&probe->mutex);
  while (probe->calls < calls && status == TURBO_OK)
    status = turbo_cond_timedwait(&probe->changed, &probe->mutex, EPOLL_TEST_TIMEOUT_NS);
  turbo_mutex_unlock(&probe->mutex);
  return status;
}

static int probe_wait_entered(epoll_callback_probe *probe) {
  int status = TURBO_OK;
  turbo_mutex_lock(&probe->mutex);
  while (!probe->entered && status == TURBO_OK)
    status = turbo_cond_timedwait(&probe->changed, &probe->mutex, EPOLL_TEST_TIMEOUT_NS);
  turbo_mutex_unlock(&probe->mutex);
  return status;
}

static size_t probe_calls(epoll_callback_probe *probe) {
  size_t calls;
  turbo_mutex_lock(&probe->mutex);
  calls = probe->calls;
  turbo_mutex_unlock(&probe->mutex);
  return calls;
}

static int wait_callbacks_quiescent(turbo_readiness_reactor *reactor) {
  for (size_t i = 0; i < EPOLL_TEST_QUIESCENCE_YIELDS; ++i) {
    turbo_readiness_stats stats;
    int status = turbo_readiness_reactor_stats(reactor, &stats);
    if (status != TURBO_OK) return status;
    if (stats.callbacks_inflight == 0) return TURBO_OK;
    turbo_thread_yield();
  }
  return TURBO_ETIMEDOUT;
}

static void probe_release(epoll_callback_probe *probe) {
  turbo_mutex_lock(&probe->mutex);
  probe->blocked = 0;
  turbo_cond_broadcast(&probe->changed);
  turbo_mutex_unlock(&probe->mutex);
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
  return TURBO_OK;
}

static int make_pipe(int fds[2]) {
  int status;
  if (pipe(fds) != 0) return -errno;
  status = set_nonblocking_cloexec(fds[0]);
  if (status == TURBO_OK) status = set_nonblocking_cloexec(fds[1]);
  if (status != TURBO_OK) {
    (void)close(fds[0]);
    (void)close(fds[1]);
  }
  return status;
}

static int make_socket_pair(int fds[2]) {
  int status;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return -errno;
  status = set_nonblocking_cloexec(fds[0]);
  if (status == TURBO_OK) status = set_nonblocking_cloexec(fds[1]);
  if (status != TURBO_OK) {
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
  return written == (ssize_t)sizeof(value) ? TURBO_OK : -errno;
}

static int drain_to_would_block(int fd) {
  uint8_t buffer[32];
  for (;;) {
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) continue;
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return TURBO_OK;
    return count == 0 ? TURBO_EIO : -errno;
  }
}

static readiness_backend_contract_fixture *epoll_backend_contract_create(
    turbo_readiness_config config, turbo_readiness_reactor *reactor, int *status) {
  readiness_backend_contract_fixture *fixture;
  size_t created = 0;
  if (status == NULL) return NULL;
  *status = turbo_readiness_reactor_init(reactor, &config);
  if (*status != TURBO_OK) return NULL;

  fixture = (readiness_backend_contract_fixture *)calloc(1, sizeof(*fixture));
  if (fixture != NULL) {
    fixture->resource_count = config.registration_capacity + 1u;
    fixture->pipes = (int(*)[2])calloc(fixture->resource_count, sizeof(*fixture->pipes));
  }
  if (fixture == NULL || fixture->pipes == NULL) {
    *status = TURBO_ENOMEM;
    goto fail;
  }
  for (size_t i = 0; i < fixture->resource_count; ++i) {
    fixture->pipes[i][0] = -1;
    fixture->pipes[i][1] = -1;
    *status = make_pipe(fixture->pipes[i]);
    if (*status != TURBO_OK) goto fail;
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
  (void)turbo_readiness_reactor_shutdown(reactor);
  (void)turbo_readiness_reactor_destroy(reactor);
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
  int status = turbo_readiness_reactor_shutdown(args->reactor);
  turbo_mutex_lock(&args->mutex);
  args->status = status;
  args->completed = 1;
  turbo_cond_broadcast(&args->changed);
  turbo_mutex_unlock(&args->mutex);
}

static void shutdown_args_init(epoll_shutdown_args *args,
                               turbo_readiness_reactor *reactor) {
  args->reactor = reactor;
  args->mutex = NULL;
  args->changed = NULL;
  args->completed = 0;
  args->status = TURBO_EIO;
  turbo_mutex_init(&args->mutex);
  turbo_cond_init(&args->changed);
}

static void shutdown_args_destroy(epoll_shutdown_args *args) {
  turbo_cond_destroy(&args->changed);
  turbo_mutex_destroy(&args->mutex);
}

spec("Platform epoll readiness") {
  it("does not subscribe read readiness for hangup-only or error-only arms") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    uint32_t hangup_interest;
    uint32_t error_interest;
    int fds[2];

    hangup_interest =
        turbo_readiness_epoll_interest_events(TURBO_READINESS_EVENT_HANGUP);
    error_interest =
        turbo_readiness_epoll_interest_events(TURBO_READINESS_EVENT_ERROR);
    check_equal(hangup_interest & (uint32_t)(EPOLLIN | EPOLLOUT), (uint32_t)0);
    check_equal(error_interest & (uint32_t)(EPOLLIN | EPOLLOUT), (uint32_t)0);

    probe_init(&probe);
    check_equal(make_socket_pair(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_HANGUP,
                                    record_callback, &probe),
                TURBO_OK);
    check_equal(write_byte(fds[1], 9), TURBO_OK);
    check_equal(shutdown(fds[1], SHUT_WR), 0);
    check_equal(probe_wait_calls(&probe, 1), TURBO_OK);
    check_equal(probe_calls(&probe), (size_t)1);
    check_equal(probe.events, TURBO_READINESS_EVENT_HANGUP);
    check_equal(probe.status, TURBO_OK);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("enforces bounded native registration capacity") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {2, 2};
    turbo_readiness_registration first = {0};
    turbo_readiness_registration second = {0};
    turbo_readiness_registration rejected = {(void *)(uintptr_t)1};
    turbo_readiness_stats stats;
    int first_pipe[2];
    int second_pipe[2];
    int third_pipe[2];

    check_equal(make_pipe(first_pipe), TURBO_OK);
    check_equal(make_pipe(second_pipe), TURBO_OK);
    check_equal(make_pipe(third_pipe), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, first_pipe[0], &first), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, second_pipe[0], &second), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, third_pipe[0], &rejected), TURBO_ENOBUFS);
    check_null(rejected.impl);
    check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
    check_equal(stats.registered_count, (size_t)2);
    check_equal(stats.rejected_full, (uint64_t)1);

    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_close(&first), TURBO_OK);
    check_equal(turbo_readiness_close(&second), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(first_pipe[0]);
    (void)close(first_pipe[1]);
    (void)close(second_pipe[0]);
    (void)close(second_pipe[1]);
    (void)close(third_pipe[0]);
    (void)close(third_pipe[1]);
  }

  it("delivers readable once per explicit arm") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(write_byte(fds[1], 1), TURBO_OK);
    check_equal(probe_wait_calls(&probe, 1), TURBO_OK);
    check_equal(wait_callbacks_quiescent(&reactor), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_READ, TURBO_READINESS_EVENT_READ);
    check_equal(probe.status, TURBO_OK);
    check_equal(drain_to_would_block(fds[0]), TURBO_OK);

    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(write_byte(fds[1], 2), TURBO_OK);
    check_equal(probe_wait_calls(&probe, 2), TURBO_OK);
    check_equal(probe_calls(&probe), (size_t)2);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("reports peer hangup through the generic event mask") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_socket_pair(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    (void)close(fds[1]);
    check_equal(probe_wait_calls(&probe, 1), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_HANGUP, TURBO_READINESS_EVENT_HANGUP);
    check_equal(probe.status, TURBO_OK);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    probe_destroy(&probe);
  }

  it("unarms without consuming later level readiness") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
    check_equal(write_byte(fds[1], 3), TURBO_OK);
    check_equal(probe_calls(&probe), (size_t)0);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(probe_wait_calls(&probe, 1), TURBO_OK);
    check_equal(probe.events & TURBO_READINESS_EVENT_READ, TURBO_READINESS_EVENT_READ);

    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("close unregisters without closing the borrowed descriptor") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(write_byte(fds[1], 4), TURBO_OK);
    check_equal(drain_to_would_block(fds[0]), TURBO_OK);
    check_equal(probe_calls(&probe), (size_t)0);

    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("preserves the registration when DEL reports EBADF") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int original_pipe[2];
    int replacement_pipe[2];
    int borrowed_fd;

    probe_init(&probe);
    check_equal(make_pipe(original_pipe), TURBO_OK);
    borrowed_fd = original_pipe[0];
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, borrowed_fd, &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ,
                                    record_callback, &probe),
                TURBO_OK);
    check_equal(close(original_pipe[0]), 0);
    original_pipe[0] = -1;

    check_equal(turbo_readiness_unarm(&registration), -EBADF);
    check_not_null(registration.impl);

    check_equal(make_pipe(replacement_pipe), TURBO_OK);
    if (replacement_pipe[0] != borrowed_fd) {
      check_equal(dup2(replacement_pipe[0], borrowed_fd), borrowed_fd);
      check_equal(close(replacement_pipe[0]), 0);
      replacement_pipe[0] = borrowed_fd;
      check_equal(set_nonblocking_cloexec(replacement_pipe[0]), TURBO_OK);
    }
    check_equal(turbo_readiness_unarm(&registration), TURBO_OK);
    check_not_null(registration.impl);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_null(registration.impl);

    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(original_pipe[1]);
    (void)close(replacement_pipe[0]);
    (void)close(replacement_pipe[1]);
    probe_destroy(&probe);
  }

  it("filters stale events when a descriptor number is reused") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {2, 2};
    turbo_readiness_registration blocker_registration = {0};
    turbo_readiness_registration old_registration = {0};
    turbo_readiness_registration new_registration = {0};
    epoll_callback_probe blocker_probe;
    epoll_callback_probe old_probe;
    epoll_callback_probe new_probe;
    turbo_readiness_stats stats;
    int blocker_pipe[2];
    int old_pipe[2];
    int new_pipe[2];
    int reused_fd;

    probe_init(&blocker_probe);
    probe_init(&old_probe);
    probe_init(&new_probe);
    check_equal(make_pipe(blocker_pipe), TURBO_OK);
    check_equal(make_pipe(old_pipe), TURBO_OK);
    reused_fd = old_pipe[0];
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, blocker_pipe[0], &blocker_registration),
                TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, reused_fd, &old_registration), TURBO_OK);
    turbo_mutex_lock(&blocker_probe.mutex);
    blocker_probe.blocked = 1;
    turbo_mutex_unlock(&blocker_probe.mutex);
    check_equal(turbo_readiness_arm(&blocker_registration, TURBO_READINESS_EVENT_READ,
                                    record_callback, &blocker_probe),
                TURBO_OK);
    check_equal(write_byte(blocker_pipe[1], 4), TURBO_OK);
    check_equal(probe_wait_entered(&blocker_probe), TURBO_OK);

    check_equal(turbo_readiness_arm(&old_registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &old_probe),
                TURBO_OK);
    check_equal(write_byte(old_pipe[1], 5), TURBO_OK);
    check_equal(turbo_readiness_close(&old_registration), TURBO_OK);
    (void)close(old_pipe[0]);
    (void)close(old_pipe[1]);

    check_equal(make_pipe(new_pipe), TURBO_OK);
    if (new_pipe[0] != reused_fd) {
      if (new_pipe[1] == reused_fd) {
        int moved_write_fd;
        do {
          moved_write_fd = fcntl(new_pipe[1], F_DUPFD, reused_fd + 1);
        } while (moved_write_fd < 0 && errno == EINTR);
        check_greater(moved_write_fd, reused_fd);
        check_equal(set_nonblocking_cloexec(moved_write_fd), TURBO_OK);
        (void)close(new_pipe[1]);
        new_pipe[1] = moved_write_fd;
      }
      check_equal(dup2(new_pipe[0], reused_fd), reused_fd);
      (void)close(new_pipe[0]);
      new_pipe[0] = reused_fd;
      check_equal(set_nonblocking_cloexec(new_pipe[0]), TURBO_OK);
    }
    check_equal(turbo_readiness_register(&reactor, new_pipe[0], &new_registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&new_registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &new_probe),
                TURBO_OK);
    probe_release(&blocker_probe);
    check_equal(write_byte(new_pipe[1], 6), TURBO_OK);
    check_equal(probe_wait_calls(&new_probe, 1), TURBO_OK);
    check_equal(probe_calls(&new_probe), (size_t)1);
    check_equal(probe_calls(&old_probe), (size_t)0);
    check_equal(turbo_readiness_reactor_stats(&reactor, &stats), TURBO_OK);
    check_equal(stats.stale_events, (uint64_t)0);

    check_equal(turbo_readiness_close(&new_registration), TURBO_OK);
    check_equal(turbo_readiness_close(&blocker_registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(new_pipe[0]);
    (void)close(new_pipe[1]);
    (void)close(blocker_pipe[0]);
    (void)close(blocker_pipe[1]);
    probe_destroy(&blocker_probe);
    probe_destroy(&old_probe);
    probe_destroy(&new_probe);
  }

  it("shutdown delivers the exact terminal status to an armed registration") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(turbo_readiness_reactor_shutdown(&reactor), TURBO_OK);
    check_equal(probe_calls(&probe), (size_t)1);
    check_equal(probe.events, (turbo_readiness_events)0);
    check_equal(probe.status, TURBO_ESHUTDOWN);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }

  it("shutdown joins the reactor thread after an inflight callback") {
    turbo_readiness_reactor reactor = {0};
    turbo_readiness_config config = {1, 1};
    turbo_readiness_registration registration = {0};
    epoll_callback_probe probe;
    epoll_shutdown_args shutdown_args;
    turbo_thread_t shutdown_thread = NULL;
    int fds[2];

    probe_init(&probe);
    check_equal(make_pipe(fds), TURBO_OK);
    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_OK);
    check_equal(turbo_readiness_register(&reactor, fds[0], &registration), TURBO_OK);
    turbo_mutex_lock(&probe.mutex);
    probe.blocked = 1;
    turbo_mutex_unlock(&probe.mutex);
    check_equal(turbo_readiness_arm(&registration, TURBO_READINESS_EVENT_READ, record_callback,
                                    &probe),
                TURBO_OK);
    check_equal(write_byte(fds[1], 6), TURBO_OK);
    check_equal(probe_wait_entered(&probe), TURBO_OK);

    shutdown_args_init(&shutdown_args, &reactor);
    check_equal(turbo_thread_create(&shutdown_thread, shutdown_entry, &shutdown_args), TURBO_OK);
    check_equal(turbo_readiness_backend_wait_admission_closed(&reactor), TURBO_OK);
    turbo_mutex_lock(&shutdown_args.mutex);
    check_false(shutdown_args.completed);
    turbo_mutex_unlock(&shutdown_args.mutex);

    probe_release(&probe);
    check_equal(turbo_thread_join(&shutdown_thread), TURBO_OK);
    check_equal(shutdown_args.status, TURBO_OK);
    check_equal(turbo_readiness_close(&registration), TURBO_OK);
    check_equal(turbo_readiness_reactor_destroy(&reactor), TURBO_OK);
    shutdown_args_destroy(&shutdown_args);
    (void)close(fds[0]);
    (void)close(fds[1]);
    probe_destroy(&probe);
  }
}
