#include "native_io_readiness.h"

#include <turbo/error_codes.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(uintptr_t) >= sizeof(uint64_t),
               "NativeIO kqueue endpoint tokens require a 64-bit target");

#define TURBO_IO_KQUEUE_WAKE_IDENT ((uintptr_t)UINTPTR_MAX)

typedef struct turbo_io_kqueue_state {
  int kqueue_fd;
  struct kevent *events;
  size_t event_capacity;
} turbo_io_kqueue_state;

typedef struct turbo_io_kqueue_change {
  int16_t filter;
  bool add;
} turbo_io_kqueue_change;

static int kqueue_apply_change(turbo_io_kqueue_state *state, int fd, uint64_t token,
                               turbo_io_kqueue_change change) {
  struct kevent event;
  int status;
  EV_SET(&event, (uintptr_t)fd, change.filter, change.add ? (EV_ADD | EV_ENABLE) : EV_DELETE, 0u, 0,
         change.add ? (void *)(uintptr_t)token : NULL);
  do {
    status = kevent(state->kqueue_fd, &event, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  if (status == 0) return TURBO_OK;
  if (!change.add && errno == ENOENT) return TURBO_OK;
  return -errno;
}

static int kqueue_driver_init(void *driver_state, size_t batch_capacity) {
  turbo_io_kqueue_state *state = (turbo_io_kqueue_state *)driver_state;
  if (batch_capacity > (size_t)INT_MAX || batch_capacity > SIZE_MAX / sizeof(struct kevent))
    return TURBO_ERANGE;
  state->kqueue_fd = kqueue();
  if (state->kqueue_fd < 0) return -errno;
  {
    struct kevent wake_event;
    EV_SET(&wake_event, TURBO_IO_KQUEUE_WAKE_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0u, 0, NULL);
    if (kevent(state->kqueue_fd, &wake_event, 1, NULL, 0, NULL) != 0) {
      const int saved_error = errno;
      (void)close(state->kqueue_fd);
      state->kqueue_fd = -1;
      return -saved_error;
    }
  }
  state->events = (struct kevent *)calloc(batch_capacity, sizeof(*state->events));
  if (state->events == NULL) {
    (void)close(state->kqueue_fd);
    state->kqueue_fd = -1;
    return TURBO_ENOMEM;
  }
  state->event_capacity = batch_capacity;
  return TURBO_OK;
}

static int kqueue_driver_update(void *driver_state, int fd, uint64_t token, uint32_t old_interests,
                                uint32_t new_interests) {
  turbo_io_kqueue_state *state = (turbo_io_kqueue_state *)driver_state;
  turbo_io_kqueue_change changes[2];
  size_t count = 0u;
  size_t applied = 0u;
  int status;
  if ((old_interests & TURBO_IO_READY_READ) != (new_interests & TURBO_IO_READY_READ))
    changes[count++] =
        (turbo_io_kqueue_change){EVFILT_READ, (new_interests & TURBO_IO_READY_READ) != 0u};
  if ((old_interests & TURBO_IO_READY_WRITE) != (new_interests & TURBO_IO_READY_WRITE))
    changes[count++] =
        (turbo_io_kqueue_change){EVFILT_WRITE, (new_interests & TURBO_IO_READY_WRITE) != 0u};
  while (applied < count) {
    status = kqueue_apply_change(state, fd, token, changes[applied]);
    if (status != TURBO_OK) {
      while (applied != 0u) {
        turbo_io_kqueue_change rollback;
        --applied;
        rollback = changes[applied];
        rollback.add = !rollback.add;
        (void)kqueue_apply_change(state, fd, token, rollback);
      }
      return status;
    }
    ++applied;
  }
  return TURBO_OK;
}

static int kqueue_driver_wait(void *driver_state, turbo_io_ready_event *events,
                              size_t event_capacity, uint32_t timeout_ms, size_t *out_count) {
  turbo_io_kqueue_state *state = (turbo_io_kqueue_state *)driver_state;
  const size_t limit =
      event_capacity < state->event_capacity ? event_capacity : state->event_capacity;
  struct timespec timeout;
  const struct timespec *timeout_pointer = NULL;
  int count;
  if (timeout_ms != UINT32_MAX) {
    timeout.tv_sec = (time_t)(timeout_ms / 1000u);
    timeout.tv_nsec = (long)(timeout_ms % 1000u) * 1000000L;
    timeout_pointer = &timeout;
  }
  do {
    count = kevent(state->kqueue_fd, NULL, 0, state->events, (int)limit, timeout_pointer);
  } while (count < 0 && errno == EINTR);
  if (count < 0) return -errno;
  if (count == 0) return TURBO_ETIMEDOUT;
  for (int index = 0; index < count; ++index) {
    const struct kevent *native = &state->events[index];
    uint32_t interests = 0u;
    uint32_t native_status = 0u;
    if (native->filter == EVFILT_USER && native->ident == TURBO_IO_KQUEUE_WAKE_IDENT) {
      events[index] = (turbo_io_ready_event){0u, TURBO_IO_READY_WAKE, 0u};
      continue;
    }
    if (native->filter == EVFILT_READ) interests |= TURBO_IO_READY_READ;
    if (native->filter == EVFILT_WRITE) interests |= TURBO_IO_READY_WRITE;
    if ((native->flags & EV_EOF) != 0u) interests |= TURBO_IO_READY_ERROR;
    if ((native->flags & EV_ERROR) != 0u) {
      interests |= TURBO_IO_READY_ERROR;
      if (native->data > 0 && (uintmax_t)native->data <= UINT32_MAX)
        native_status = (uint32_t)native->data;
    }
    events[index] =
        (turbo_io_ready_event){(uint64_t)(uintptr_t)native->udata, interests, native_status};
  }
  *out_count = (size_t)count;
  return TURBO_OK;
}

static int kqueue_driver_wake(void *driver_state) {
  turbo_io_kqueue_state *state = (turbo_io_kqueue_state *)driver_state;
  struct kevent event;
  int status;
  EV_SET(&event, TURBO_IO_KQUEUE_WAKE_IDENT, EVFILT_USER, 0u, NOTE_TRIGGER, 0, NULL);
  do {
    status = kevent(state->kqueue_fd, &event, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  return status == 0 ? TURBO_OK : -errno;
}

static void kqueue_driver_destroy(void *driver_state) {
  turbo_io_kqueue_state *state = (turbo_io_kqueue_state *)driver_state;
  free(state->events);
  state->events = NULL;
  if (state->kqueue_fd >= 0) (void)close(state->kqueue_fd);
  state->kqueue_fd = -1;
}

static const turbo_io_readiness_driver_ops kqueue_driver_ops = {
    kqueue_driver_init, kqueue_driver_update, kqueue_driver_wait, kqueue_driver_wake,
    kqueue_driver_destroy};

int turbo_io_kqueue_backend_init(native_io_backend *backend, const native_io_backend_config *config) {
  if (config->kind != NATIVE_IO_BACKEND_KQUEUE) return TURBO_ENOTSUP;
  return turbo_io_readiness_backend_init(backend, config, &kqueue_driver_ops,
                                         sizeof(turbo_io_kqueue_state));
}
