#include <turbo/error_codes.h>
#include <turbo/readiness.h>

#include "readiness_backend_contract.h"
#include "tinytest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

struct readiness_backend_contract_fixture {
  int (*pipes)[2];
  size_t resource_count;
};

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
  if (status == TURBO_OK)
    status = poll_test_set_nonblocking_cloexec(fds[1]);
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
  return written == (ssize_t)sizeof(value)
             ? TURBO_OK
             : written < 0 ? -errno : TURBO_EIO;
}

static int poll_test_drain(int fd) {
  uint8_t bytes[32];
  for (;;) {
    ssize_t count = read(fd, bytes, sizeof(bytes));
    if (count > 0) continue;
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return TURBO_OK;
    return count == 0 ? TURBO_EIO : -errno;
  }
}

static void poll_contract_destroy(
    readiness_backend_contract_fixture *fixture) {
  if (fixture == NULL) return;
  for (size_t i = 0; i < fixture->resource_count; ++i) {
    (void)close(fixture->pipes[i][0]);
    (void)close(fixture->pipes[i][1]);
  }
  free(fixture->pipes);
  free(fixture);
}

static readiness_backend_contract_fixture *poll_contract_create(
    turbo_readiness_config config, turbo_readiness_reactor *reactor,
    int *status) {
  readiness_backend_contract_fixture *fixture = NULL;
  size_t created = 0u;
  if (status == NULL) return NULL;
  *status = turbo_readiness_reactor_init_kind(
      reactor, &config, TURBO_READINESS_BACKEND_POLL);
  if (*status != TURBO_OK) return NULL;

  fixture = (readiness_backend_contract_fixture *)calloc(1u,
                                                          sizeof(*fixture));
  if (fixture != NULL) {
    fixture->resource_count = config.registration_capacity + 1u;
    fixture->pipes = (int(*)[2])calloc(fixture->resource_count,
                                       sizeof(*fixture->pipes));
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

static intptr_t poll_contract_resource(
    readiness_backend_contract_fixture *fixture, size_t index) {
  return (intptr_t)fixture->pipes[index][0];
}

static int poll_contract_make_readable(
    readiness_backend_contract_fixture *fixture, size_t index) {
  return poll_test_write_byte(fixture->pipes[index][1],
                              (uint8_t)(index + 1u));
}

static int poll_contract_drain_readable(
    readiness_backend_contract_fixture *fixture, size_t index) {
  return poll_test_drain(fixture->pipes[index][0]);
}

const readiness_backend_contract_factory *
readiness_backend_contract_factory_get(void) {
  static const readiness_backend_contract_factory factory = {
      poll_contract_create, poll_contract_destroy, poll_contract_resource,
      poll_contract_make_readable, poll_contract_drain_readable};
  return &factory;
}

spec("Platform poll readiness selector") {
  it("reports explicit compile-time support") {
    check_true(turbo_readiness_backend_supported(
        TURBO_READINESS_BACKEND_POLL));
  }
}
