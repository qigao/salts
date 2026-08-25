#include "readiness_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

enum { KQUEUE_CONTROL_IDENT = 1 };

_Static_assert(sizeof(uintptr_t) >= sizeof(uint64_t),
               "kqueue readiness tokens require 64-bit udata");

typedef struct turbo_readiness_kqueue_record {
  int fd;
  uint64_t registration_token;
  uint64_t event_token;
  turbo_readiness_events watched_events;
  int active;
  int armed;
} turbo_readiness_kqueue_record;

typedef struct turbo_readiness_kqueue_backend {
  turbo_readiness_reactor *reactor;
  turbo_readiness_kqueue_record *records;
  struct kevent *event_batch;
  size_t capacity;
  size_t event_batch_capacity;
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  turbo_thread_t thread;
  int kqueue_fd;
  int thread_started;
  int thread_exited;
  atomic_int stopping;
} turbo_readiness_kqueue_backend;

static uint32_t kqueue_token_index(uint64_t token) { return (uint32_t)token; }

static turbo_readiness_events kqueue_native_events(
    turbo_readiness_events events) {
  turbo_readiness_events native_events =
      events & (TURBO_READINESS_EVENT_READ | TURBO_READINESS_EVENT_WRITE);
  if (native_events == 0u &&
      (events & (TURBO_READINESS_EVENT_ERROR |
                 TURBO_READINESS_EVENT_HANGUP)) != 0u)
    native_events = TURBO_READINESS_EVENT_READ;
  return native_events;
}

static int kqueue_fd_valid(int fd) {
  int status;
  do {
    status = fcntl(fd, F_GETFD);
  } while (status < 0 && errno == EINTR);
  return status >= 0 ? TURBO_OK : -errno;
}

static int kqueue_change(turbo_readiness_kqueue_backend *backend,
                         uintptr_t ident, int16_t filter, uint16_t flags,
                         uint32_t fflags, intptr_t data, void *user) {
  struct kevent change;
  int status;
  EV_SET(&change, ident, filter, flags, fflags, data, user);
  do {
    status = kevent(backend->kqueue_fd, &change, 1, NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  return status == 0 ? TURBO_OK : -errno;
}

static int kqueue_delete_filter(turbo_readiness_kqueue_backend *backend,
                                int fd, int16_t filter) {
  int status = kqueue_change(backend, (uintptr_t)fd, filter, EV_DELETE,
                             0u, 0, NULL);
  return status == -ENOENT ? TURBO_OK : status;
}

static int kqueue_remove_filters(turbo_readiness_kqueue_backend *backend,
                                 int fd,
                                 turbo_readiness_events events) {
  const turbo_readiness_events native_events = kqueue_native_events(events);
  int read_status = TURBO_OK;
  int write_status = TURBO_OK;
  if ((native_events & TURBO_READINESS_EVENT_READ) != 0)
    read_status = kqueue_delete_filter(backend, fd, EVFILT_READ);
  if ((native_events & TURBO_READINESS_EVENT_WRITE) != 0)
    write_status = kqueue_delete_filter(backend, fd, EVFILT_WRITE);
  return read_status != TURBO_OK ? read_status : write_status;
}

static int kqueue_register_resource(void *user, intptr_t native_resource,
                                    uint64_t token) {
  turbo_readiness_kqueue_backend *backend =
      (turbo_readiness_kqueue_backend *)user;
  uint32_t index = kqueue_token_index(token);
  int fd;
  int status;

  if (native_resource < 0 || native_resource > INT_MAX ||
      (uint32_t)(token >> 32) == 0u || (size_t)index >= backend->capacity)
    return TURBO_EINVAL;
  fd = (int)native_resource;
  status = kqueue_fd_valid(fd);
  if (status != TURBO_OK) return status;

  turbo_mutex_lock(&backend->mutex);
  if (backend->records[index].active) {
    turbo_mutex_unlock(&backend->mutex);
    return TURBO_EALREADY;
  }
  for (size_t i = 0u; i < backend->capacity; ++i) {
    if (backend->records[i].active && backend->records[i].fd == fd) {
      turbo_mutex_unlock(&backend->mutex);
      return TURBO_EALREADY;
    }
  }
  backend->records[index].fd = fd;
  backend->records[index].registration_token = token;
  backend->records[index].active = 1;
  turbo_mutex_unlock(&backend->mutex);
  return TURBO_OK;
}

static int kqueue_arm(void *user, uint64_t token, uint64_t arm_token,
                      turbo_readiness_events events) {
  turbo_readiness_kqueue_backend *backend =
      (turbo_readiness_kqueue_backend *)user;
  uint32_t index = kqueue_token_index(token);
  int fd;
  struct kevent changes[2];
  int change_count = 0;
  int status;
  uint64_t previous_event_token;
  turbo_readiness_events previous_watched_events;
  int previous_armed;
  const turbo_readiness_events native_events = kqueue_native_events(events);

  turbo_mutex_lock(&backend->mutex);
  if ((size_t)index >= backend->capacity ||
      !backend->records[index].active ||
      backend->records[index].registration_token != token) {
    turbo_mutex_unlock(&backend->mutex);
    return TURBO_EINVAL;
  }
  fd = backend->records[index].fd;
  previous_event_token = backend->records[index].event_token;
  previous_watched_events = backend->records[index].watched_events;
  previous_armed = backend->records[index].armed;
  backend->records[index].event_token = arm_token;
  backend->records[index].watched_events = events;
  backend->records[index].armed = 1;
  turbo_mutex_unlock(&backend->mutex);

  if ((native_events & TURBO_READINESS_EVENT_READ) != 0) {
    const int synthetic_read =
        (events & TURBO_READINESS_EVENT_READ) == 0u;
    EV_SET(&changes[change_count++], (uintptr_t)fd, EVFILT_READ,
           EV_ADD | EV_ENABLE | EV_ONESHOT,
           synthetic_read ? NOTE_LOWAT : 0u,
           synthetic_read ? INTPTR_MAX : 0,
           (void *)(uintptr_t)arm_token);
  }
  if ((native_events & TURBO_READINESS_EVENT_WRITE) != 0) {
    EV_SET(&changes[change_count++], (uintptr_t)fd, EVFILT_WRITE,
           EV_ADD | EV_ENABLE | EV_ONESHOT, 0u, 0,
           (void *)(uintptr_t)arm_token);
  }
  do {
    status = kevent(backend->kqueue_fd, changes, change_count,
                    NULL, 0, NULL);
  } while (status < 0 && errno == EINTR);
  status = status == 0 ? TURBO_OK : -errno;
  if (status != TURBO_OK) {
    (void)kqueue_remove_filters(backend, fd, events);
    turbo_mutex_lock(&backend->mutex);
    if (backend->records[index].active &&
        backend->records[index].registration_token == token &&
        backend->records[index].event_token == arm_token) {
      backend->records[index].event_token = previous_event_token;
      backend->records[index].watched_events = previous_watched_events;
      backend->records[index].armed = previous_armed;
    }
    turbo_mutex_unlock(&backend->mutex);
    return status;
  }
  return TURBO_OK;
}

static int kqueue_remove_watch(turbo_readiness_kqueue_backend *backend,
                               uint64_t token, int close_record) {
  uint32_t index = kqueue_token_index(token);
  int fd;
  turbo_readiness_events watched;
  int status;

  turbo_mutex_lock(&backend->mutex);
  if ((size_t)index >= backend->capacity ||
      !backend->records[index].active ||
      backend->records[index].registration_token != token) {
    turbo_mutex_unlock(&backend->mutex);
    return TURBO_EINVAL;
  }
  fd = backend->records[index].fd;
  watched = backend->records[index].watched_events;
  turbo_mutex_unlock(&backend->mutex);

  status = kqueue_remove_filters(backend, fd, watched);
  if (status != TURBO_OK) return status;

  turbo_mutex_lock(&backend->mutex);
  backend->records[index].watched_events = 0u;
  backend->records[index].event_token = 0u;
  backend->records[index].armed = 0;
  if (close_record) {
    backend->records[index].fd = -1;
    backend->records[index].registration_token = 0u;
    backend->records[index].active = 0;
  }
  turbo_mutex_unlock(&backend->mutex);
  return TURBO_OK;
}

static int kqueue_unarm(void *user, uint64_t token) {
  return kqueue_remove_watch((turbo_readiness_kqueue_backend *)user,
                             token, 0);
}

static int kqueue_close_registration(void *user, uint64_t token) {
  return kqueue_remove_watch((turbo_readiness_kqueue_backend *)user,
                             token, 1);
}

static int kqueue_control_trigger(turbo_readiness_kqueue_backend *backend) {
  return kqueue_change(backend, KQUEUE_CONTROL_IDENT, EVFILT_USER, 0u,
                       NOTE_TRIGGER, 0, NULL);
}

static turbo_readiness_events kqueue_translate_events(
    const struct kevent *event) {
  turbo_readiness_events events = 0u;
  if (event->filter == EVFILT_READ) events |= TURBO_READINESS_EVENT_READ;
  if (event->filter == EVFILT_WRITE) events |= TURBO_READINESS_EVENT_WRITE;
  if ((event->flags & EV_EOF) != 0) events |= TURBO_READINESS_EVENT_HANGUP;
  if ((event->flags & EV_EOF) != 0 && event->fflags != 0u)
    events |= TURBO_READINESS_EVENT_ERROR;
  if ((event->flags & EV_ERROR) != 0) events |= TURBO_READINESS_EVENT_ERROR;
  return events;
}

static void kqueue_thread_mark_exited(
    turbo_readiness_kqueue_backend *backend) {
  turbo_mutex_lock(&backend->mutex);
  backend->thread_exited = 1;
  turbo_cond_broadcast(&backend->changed);
  turbo_mutex_unlock(&backend->mutex);
}

static void kqueue_thread_entry(void *user) {
  turbo_readiness_kqueue_backend *backend =
      (turbo_readiness_kqueue_backend *)user;
  int terminal_status = TURBO_OK;
  for (;;) {
    int ready;
    int control_seen = 0;
    do {
      ready = kevent(backend->kqueue_fd, NULL, 0, backend->event_batch,
                     (int)backend->event_batch_capacity, NULL);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) {
      terminal_status = -errno;
      break;
    }

    for (int i = 0; i < ready; ++i) {
      const struct kevent *event = &backend->event_batch[i];
      uint64_t event_token;
      uint64_t registration_token = 0u;
      turbo_readiness_events watched = 0u;
      uint32_t index;
      int event_status = TURBO_OK;
      turbo_readiness_events delivered;

      if (event->filter == EVFILT_USER &&
          event->ident == KQUEUE_CONTROL_IDENT) {
        control_seen = 1;
        continue;
      }
      event_token = (uint64_t)(uintptr_t)event->udata;
      index = kqueue_token_index(event_token);
      turbo_mutex_lock(&backend->mutex);
      if ((size_t)index < backend->capacity &&
          backend->records[index].active &&
          backend->records[index].armed &&
          backend->records[index].event_token == event_token) {
        registration_token = backend->records[index].registration_token;
        watched = backend->records[index].watched_events;
        backend->records[index].watched_events = 0u;
        backend->records[index].armed = 0;
      }
      turbo_mutex_unlock(&backend->mutex);
      if (registration_token == 0u) continue;
      (void)kqueue_remove_filters(backend, (int)event->ident, watched);
      if ((event->flags & EV_ERROR) != 0 && event->data != 0)
        event_status = -(int)event->data;
      delivered = kqueue_translate_events(event);
      if ((watched & TURBO_READINESS_EVENT_READ) == 0u)
        delivered &= ~TURBO_READINESS_EVENT_READ;
      if ((watched & TURBO_READINESS_EVENT_WRITE) == 0u)
        delivered &= ~TURBO_READINESS_EVENT_WRITE;
      (void)turbo_readiness_backend_dispatch_generation(
          backend->reactor, registration_token, event_token,
          delivered, event_status);
    }
    if (control_seen &&
        atomic_load_explicit(&backend->stopping, memory_order_acquire))
      break;
  }
  if (terminal_status != TURBO_OK)
    (void)turbo_readiness_backend_fail(backend->reactor, terminal_status);
  kqueue_thread_mark_exited(backend);
}

static int kqueue_shutdown(void *user) {
  turbo_readiness_kqueue_backend *backend =
      (turbo_readiness_kqueue_backend *)user;
  int status;
  if (!backend->thread_started) return TURBO_OK;
  atomic_store_explicit(&backend->stopping, 1, memory_order_release);
  status = kqueue_control_trigger(backend);
  if (status != TURBO_OK) {
    atomic_store_explicit(&backend->stopping, 0, memory_order_release);
    return status;
  }
  turbo_mutex_lock(&backend->mutex);
  while (!backend->thread_exited)
    turbo_cond_wait(&backend->changed, &backend->mutex);
  turbo_mutex_unlock(&backend->mutex);
  status = turbo_thread_join(&backend->thread);
  if (status != TURBO_OK) return status;
  backend->thread_started = 0;
  return TURBO_OK;
}

static void kqueue_backend_destroy(void *user) {
  turbo_readiness_kqueue_backend *backend =
      (turbo_readiness_kqueue_backend *)user;
  if (backend == NULL) return;
  if (backend->kqueue_fd >= 0) (void)close(backend->kqueue_fd);
  turbo_cond_destroy(&backend->changed);
  turbo_mutex_destroy(&backend->mutex);
  free(backend->event_batch);
  free(backend->records);
  free(backend);
}

static const turbo_readiness_backend_ops kqueue_backend_ops = {
    kqueue_register_resource, kqueue_arm, kqueue_unarm,
    kqueue_close_registration, kqueue_shutdown, kqueue_backend_destroy};

int turbo_readiness_kqueue_init(turbo_readiness_reactor *reactor,
                                const turbo_readiness_config *config) {
  turbo_readiness_kqueue_backend *backend;
  int status;
  if (config == NULL || config->registration_capacity == 0u ||
      config->event_batch_capacity == 0u)
    return TURBO_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u ||
      config->event_batch_capacity > (size_t)INT_MAX)
    return TURBO_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u)
    return TURBO_EINVAL;
  if (config->registration_capacity >
          SIZE_MAX / sizeof(turbo_readiness_kqueue_record) ||
      config->event_batch_capacity > SIZE_MAX / sizeof(struct kevent))
    return TURBO_ERANGE;
  backend = (turbo_readiness_kqueue_backend *)calloc(1u, sizeof(*backend));
  if (backend == NULL) return TURBO_ENOMEM;
  backend->kqueue_fd = -1;
  backend->capacity = config->registration_capacity;
  backend->event_batch_capacity = config->event_batch_capacity;
  atomic_init(&backend->stopping, 0);
  backend->records = (turbo_readiness_kqueue_record *)calloc(
      backend->capacity, sizeof(*backend->records));
  backend->event_batch = (struct kevent *)calloc(
      backend->event_batch_capacity, sizeof(*backend->event_batch));
  turbo_mutex_init(&backend->mutex);
  turbo_cond_init(&backend->changed);
  if (backend->records == NULL || backend->event_batch == NULL ||
      backend->mutex == NULL || backend->changed == NULL) {
    kqueue_backend_destroy(backend);
    return TURBO_ENOMEM;
  }
  backend->kqueue_fd = kqueue();
  if (backend->kqueue_fd < 0) {
    status = -errno;
    kqueue_backend_destroy(backend);
    return status;
  }
  status = kqueue_change(backend, KQUEUE_CONTROL_IDENT, EVFILT_USER,
                         EV_ADD | EV_CLEAR, 0u, 0, NULL);
  if (status != TURBO_OK) {
    kqueue_backend_destroy(backend);
    return status;
  }
  status = turbo_readiness_reactor_init_backend(
      reactor, config, &kqueue_backend_ops, backend);
  if (status != TURBO_OK) {
    kqueue_backend_destroy(backend);
    return status;
  }
  backend->reactor = reactor;
  status = turbo_thread_create(&backend->thread, kqueue_thread_entry, backend);
  if (status != TURBO_OK) {
    (void)turbo_readiness_reactor_shutdown(reactor);
    (void)turbo_readiness_reactor_destroy(reactor);
    return status;
  }
  backend->thread_started = 1;
  return TURBO_OK;
}
