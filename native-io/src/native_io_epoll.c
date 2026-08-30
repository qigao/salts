#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include "native_io_readiness.h"

#include <turbo/error_codes.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

typedef struct turbo_io_epoll_state {
  int epoll_fd;
  struct epoll_event *events;
  size_t event_capacity;
} turbo_io_epoll_state;

static uint32_t epoll_native_interests(uint32_t interests) {
  uint32_t native = EPOLLERR | EPOLLHUP;
  if ((interests & TURBO_IO_READY_READ) != 0u) native |= EPOLLIN | EPOLLPRI | EPOLLRDHUP;
  if ((interests & TURBO_IO_READY_WRITE) != 0u) native |= EPOLLOUT;
  return native;
}

static int epoll_driver_init(void *driver_state, size_t batch_capacity) {
  turbo_io_epoll_state *state = (turbo_io_epoll_state *)driver_state;
  if (batch_capacity > (size_t)INT_MAX || batch_capacity > SIZE_MAX / sizeof(struct epoll_event))
    return TURBO_ERANGE;
  state->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (state->epoll_fd < 0) return -errno;
  state->events = (struct epoll_event *)calloc(batch_capacity, sizeof(*state->events));
  if (state->events == NULL) {
    const int saved_error = errno;
    (void)close(state->epoll_fd);
    state->epoll_fd = -1;
    return saved_error == 0 ? TURBO_ENOMEM : -saved_error;
  }
  state->event_capacity = batch_capacity;
  return TURBO_OK;
}

static int epoll_driver_update(void *driver_state, int fd, uint64_t token, uint32_t old_interests,
                               uint32_t new_interests) {
  turbo_io_epoll_state *state = (turbo_io_epoll_state *)driver_state;
  struct epoll_event event;
  int operation;
  int status;
  if (old_interests == new_interests) return TURBO_OK;
  if (old_interests == 0u) operation = EPOLL_CTL_ADD;
  else if (new_interests == 0u) operation = EPOLL_CTL_DEL;
  else operation = EPOLL_CTL_MOD;
  event.events = epoll_native_interests(new_interests);
  event.data.u64 = token;
  do {
    status = epoll_ctl(state->epoll_fd, operation, fd, operation == EPOLL_CTL_DEL ? NULL : &event);
  } while (status < 0 && errno == EINTR);
  if (status == 0) return TURBO_OK;
  if (operation == EPOLL_CTL_DEL && errno == ENOENT) return TURBO_OK;
  return -errno;
}

static int epoll_driver_wait(void *driver_state, turbo_io_ready_event *events,
                             size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  turbo_io_epoll_state *state = (turbo_io_epoll_state *)driver_state;
  const size_t limit =
      event_capacity < state->event_capacity ? event_capacity : state->event_capacity;
  const int native_timeout = timeout_ms == UINT32_MAX         ? -1
                             : timeout_ms > (uint32_t)INT_MAX ? INT_MAX
                                                              : (int)timeout_ms;
  int count;
  do {
    count = epoll_wait(state->epoll_fd, state->events, (int)limit, native_timeout);
  } while (count < 0 && errno == EINTR);
  if (count < 0) return -errno;
  if (count == 0) return TURBO_ETIMEDOUT;
  for (int index = 0; index < count; ++index) {
    const uint32_t native = state->events[index].events;
    uint32_t interests = 0u;
    if ((native & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0u) interests |= TURBO_IO_READY_READ;
    if ((native & EPOLLOUT) != 0u) interests |= TURBO_IO_READY_WRITE;
    if ((native & (EPOLLERR | EPOLLHUP)) != 0u) interests |= TURBO_IO_READY_ERROR;
    events[index] = (turbo_io_ready_event){state->events[index].data.u64, interests, native};
  }
  *out_count = (size_t)count;
  return TURBO_OK;
}

static void epoll_driver_destroy(void *driver_state) {
  turbo_io_epoll_state *state = (turbo_io_epoll_state *)driver_state;
  free(state->events);
  state->events = NULL;
  if (state->epoll_fd >= 0) (void)close(state->epoll_fd);
  state->epoll_fd = -1;
}

static const turbo_io_readiness_driver_ops epoll_driver_ops = {
    epoll_driver_init, epoll_driver_update, epoll_driver_wait, epoll_driver_destroy};

int turbo_io_epoll_backend_init(turbo_io_backend *backend, const turbo_io_backend_config *config) {
  if (config->kind != TURBO_IO_BACKEND_EPOLL) return TURBO_ENOTSUP;
  return turbo_io_readiness_backend_init(backend, config, &epoll_driver_ops,
                                         sizeof(turbo_io_epoll_state));
}
