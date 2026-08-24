#include "readiness_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum turbo_readiness_slot_state {
  TURBO_READINESS_SLOT_FREE = 0,
  TURBO_READINESS_SLOT_REGISTERED,
  TURBO_READINESS_SLOT_ARMED,
  TURBO_READINESS_SLOT_FIRING,
  TURBO_READINESS_SLOT_CLOSING
} turbo_readiness_slot_state;

typedef struct turbo_readiness_impl turbo_readiness_impl;

typedef struct turbo_readiness_slot {
  turbo_readiness_impl *owner;
  intptr_t native_resource;
  turbo_readiness_callback callback;
  void *callback_user;
  uint32_t index;
  uint32_t generation;
  turbo_readiness_slot_state state;
  int inflight;
} turbo_readiness_slot;

struct turbo_readiness_impl {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  turbo_readiness_slot *slots;
  turbo_readiness_backend_event *event_batch;
  size_t capacity;
  size_t event_batch_capacity;
  turbo_readiness_backend_ops backend_ops;
  void *backend_user;
  uint64_t rejected_full;
  uint64_t stale_events;
  uint64_t duplicate_events;
  uint64_t backend_errors;
  int admission_open;
  int shutdown_started;
  int shutdown_complete;
};

static TURBO_THREAD_LOCAL turbo_readiness_slot *readiness_callback_slot;

static uint64_t readiness_slot_token(const turbo_readiness_slot *slot) {
  return ((uint64_t)slot->generation << 32) | (uint64_t)slot->index;
}

static uint32_t readiness_next_generation(uint32_t generation) {
  generation += 1u;
  return generation == 0u ? 1u : generation;
}

static void readiness_slot_reclaim(turbo_readiness_slot *slot) {
  slot->native_resource = 0;
  slot->callback = NULL;
  slot->callback_user = NULL;
  slot->inflight = 0;
  slot->state = TURBO_READINESS_SLOT_FREE;
}

static turbo_readiness_impl *readiness_impl_from_reactor(
    turbo_readiness_reactor *reactor) {
  return reactor != NULL ? (turbo_readiness_impl *)reactor->impl : NULL;
}

static turbo_readiness_slot *readiness_slot_from_registration(
    turbo_readiness_registration *registration) {
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  uintptr_t address;
  uintptr_t begin;
  uintptr_t end;

  if (registration == NULL || registration->impl == NULL) return NULL;
  slot = (turbo_readiness_slot *)registration->impl;
  impl = slot->owner;
  if (impl == NULL || impl->slots == NULL) return NULL;
  address = (uintptr_t)slot;
  begin = (uintptr_t)impl->slots;
  end = begin + impl->capacity * sizeof(*impl->slots);
  if (address < begin || address >= end ||
      (address - begin) % sizeof(*impl->slots) != 0)
    return NULL;
  return slot;
}

static int readiness_events_valid(turbo_readiness_events events) {
  const turbo_readiness_events valid =
      TURBO_READINESS_EVENT_READ | TURBO_READINESS_EVENT_WRITE |
      TURBO_READINESS_EVENT_ERROR | TURBO_READINESS_EVENT_HANGUP;
  return events != 0 && (events & ~valid) == 0;
}

static int readiness_config_validate(const turbo_readiness_config *config) {
  if (config == NULL || config->registration_capacity == 0 ||
      config->event_batch_capacity == 0)
    return TURBO_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u)
    return TURBO_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u)
    return TURBO_EINVAL;
  if (config->registration_capacity > SIZE_MAX / sizeof(turbo_readiness_slot) ||
      config->event_batch_capacity >
          SIZE_MAX / sizeof(turbo_readiness_backend_event))
    return TURBO_ERANGE;
  return TURBO_OK;
}

static int readiness_backend_ops_validate(
    const turbo_readiness_backend_ops *backend_ops) {
  return backend_ops != NULL && backend_ops->register_resource != NULL &&
         backend_ops->arm != NULL && backend_ops->unarm != NULL &&
         backend_ops->close != NULL && backend_ops->shutdown != NULL;
}

int turbo_readiness_reactor_init_backend(
    turbo_readiness_reactor *reactor, const turbo_readiness_config *config,
    const turbo_readiness_backend_ops *backend_ops, void *backend_user) {
  turbo_readiness_impl *impl;
  int status;

  if (reactor != NULL) reactor->impl = NULL;
  if (reactor == NULL || !readiness_backend_ops_validate(backend_ops))
    return TURBO_EINVAL;
  status = readiness_config_validate(config);
  if (status != TURBO_OK) return status;

  impl = (turbo_readiness_impl *)calloc(1, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->slots = (turbo_readiness_slot *)calloc(
      config->registration_capacity, sizeof(*impl->slots));
  impl->event_batch = (turbo_readiness_backend_event *)calloc(
      config->event_batch_capacity, sizeof(*impl->event_batch));
  if (impl->slots == NULL || impl->event_batch == NULL) {
    free(impl->event_batch);
    free(impl->slots);
    free(impl);
    return TURBO_ENOMEM;
  }

  turbo_mutex_init(&impl->mutex);
  turbo_cond_init(&impl->changed);
  if (impl->mutex == NULL || impl->changed == NULL) {
    turbo_cond_destroy(&impl->changed);
    turbo_mutex_destroy(&impl->mutex);
    free(impl->event_batch);
    free(impl->slots);
    free(impl);
    return TURBO_ENOMEM;
  }

  impl->capacity = config->registration_capacity;
  impl->event_batch_capacity = config->event_batch_capacity;
  impl->backend_ops = *backend_ops;
  impl->backend_user = backend_user;
  impl->admission_open = 1;
  for (size_t i = 0; i < impl->capacity; ++i) {
    impl->slots[i].owner = impl;
    impl->slots[i].index = (uint32_t)i;
  }
  reactor->impl = impl;
  return TURBO_OK;
}

int turbo_readiness_reactor_init(turbo_readiness_reactor *reactor,
                                 const turbo_readiness_config *config) {
  if (reactor != NULL) reactor->impl = NULL;
  if (reactor == NULL || config == NULL) return TURBO_EINVAL;
  return TURBO_ENOTSUP;
}

int turbo_readiness_register(turbo_readiness_reactor *reactor,
                             intptr_t native_resource,
                             turbo_readiness_registration *registration) {
  turbo_readiness_impl *impl;
  turbo_readiness_slot *slot = NULL;
  uint64_t token;
  int status;

  if (registration != NULL) registration->impl = NULL;
  impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL || registration == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->mutex);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_ESHUTDOWN;
  }
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].state == TURBO_READINESS_SLOT_FREE) {
      slot = &impl->slots[i];
      break;
    }
  }
  if (slot == NULL) {
    impl->rejected_full += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_ENOBUFS;
  }

  slot->generation = readiness_next_generation(slot->generation);
  slot->native_resource = native_resource;
  slot->state = TURBO_READINESS_SLOT_REGISTERED;
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.register_resource(impl->backend_user,
                                               native_resource, token);
  if (status != TURBO_OK) {
    turbo_mutex_lock(&impl->mutex);
    if (slot->state == TURBO_READINESS_SLOT_REGISTERED &&
        slot->generation == (uint32_t)(token >> 32))
      readiness_slot_reclaim(slot);
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  registration->impl = slot;
  return TURBO_OK;
}

int turbo_readiness_arm(turbo_readiness_registration *registration,
                        turbo_readiness_events events,
                        turbo_readiness_callback callback, void *user) {
  turbo_readiness_slot *slot = readiness_slot_from_registration(registration);
  turbo_readiness_impl *impl;
  uint64_t token;
  int status;

  if (slot == NULL || callback == NULL || !readiness_events_valid(events))
    return TURBO_EINVAL;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_ESHUTDOWN;
  }
  if (slot->state == TURBO_READINESS_SLOT_ARMED) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EALREADY;
  }
  if (slot->state != TURBO_READINESS_SLOT_REGISTERED) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  slot->callback = callback;
  slot->callback_user = user;
  slot->state = TURBO_READINESS_SLOT_ARMED;
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.arm(impl->backend_user, token, events);
  if (status != TURBO_OK) {
    turbo_mutex_lock(&impl->mutex);
    if (slot->state == TURBO_READINESS_SLOT_ARMED &&
        readiness_slot_token(slot) == token) {
      slot->state = TURBO_READINESS_SLOT_REGISTERED;
      slot->callback = NULL;
      slot->callback_user = NULL;
    }
    turbo_mutex_unlock(&impl->mutex);
  }
  return status;
}

int turbo_readiness_unarm(turbo_readiness_registration *registration) {
  turbo_readiness_slot *slot = readiness_slot_from_registration(registration);
  turbo_readiness_impl *impl;
  uint64_t token;
  int was_firing;
  int status;

  if (slot == NULL) return TURBO_EINVAL;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (slot->state != TURBO_READINESS_SLOT_ARMED &&
      slot->state != TURBO_READINESS_SLOT_FIRING) {
    status = slot->state == TURBO_READINESS_SLOT_REGISTERED
                 ? TURBO_EALREADY
                 : TURBO_EBUSY;
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  was_firing = slot->state == TURBO_READINESS_SLOT_FIRING;
  if (!was_firing) {
    slot->state = TURBO_READINESS_SLOT_REGISTERED;
    slot->callback = NULL;
    slot->callback_user = NULL;
  }
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.unarm(impl->backend_user, token);
  if (status != TURBO_OK) return status;
  if (!was_firing) return TURBO_OK;

  turbo_mutex_lock(&impl->mutex);
  while (slot->inflight)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

int turbo_readiness_close(turbo_readiness_registration *registration) {
  turbo_readiness_slot *slot = readiness_slot_from_registration(registration);
  turbo_readiness_impl *impl;
  turbo_readiness_slot_state previous_state;
  uint64_t token;
  int self_close;
  int status;

  if (slot == NULL) return TURBO_EINVAL;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (slot->state == TURBO_READINESS_SLOT_FREE ||
      slot->state == TURBO_READINESS_SLOT_CLOSING) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  previous_state = slot->state;
  self_close = previous_state == TURBO_READINESS_SLOT_FIRING &&
               readiness_callback_slot == slot;
  slot->state = TURBO_READINESS_SLOT_CLOSING;
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.close(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK) {
    if (slot->state == TURBO_READINESS_SLOT_CLOSING)
      slot->state = previous_state;
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  registration->impl = NULL;
  if (self_close) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  while (slot->inflight)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  if (slot->state != TURBO_READINESS_SLOT_FREE)
    readiness_slot_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

static int readiness_dispatch_slot(turbo_readiness_impl *impl,
                                   turbo_readiness_slot *slot,
                                   turbo_readiness_events events, int status) {
  turbo_readiness_callback callback;
  turbo_readiness_slot *previous_callback_slot;
  void *callback_user;

  slot->state = TURBO_READINESS_SLOT_FIRING;
  slot->inflight = 1;
  callback = slot->callback;
  callback_user = slot->callback_user;
  turbo_mutex_unlock(&impl->mutex);

  previous_callback_slot = readiness_callback_slot;
  readiness_callback_slot = slot;
  callback(callback_user, events, status);
  readiness_callback_slot = previous_callback_slot;

  turbo_mutex_lock(&impl->mutex);
  slot->inflight = 0;
  slot->callback = NULL;
  slot->callback_user = NULL;
  if (slot->state == TURBO_READINESS_SLOT_CLOSING)
    readiness_slot_reclaim(slot);
  else
    slot->state = TURBO_READINESS_SLOT_REGISTERED;
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

int turbo_readiness_backend_dispatch(turbo_readiness_reactor *reactor,
                                     uint64_t token,
                                     turbo_readiness_events events, int status) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  turbo_readiness_slot *slot;
  uint32_t index = (uint32_t)token;
  uint32_t generation = (uint32_t)(token >> 32);

  if (impl == NULL || (status == TURBO_OK && !readiness_events_valid(events)))
    return TURBO_EINVAL;
  turbo_mutex_lock(&impl->mutex);
  if ((size_t)index >= impl->capacity) {
    impl->stale_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  slot = &impl->slots[index];
  if (generation == 0 || slot->generation != generation ||
      slot->state == TURBO_READINESS_SLOT_FREE) {
    impl->stale_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (slot->state != TURBO_READINESS_SLOT_ARMED) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  return readiness_dispatch_slot(impl, slot, events, status);
}

static void readiness_fanout(turbo_readiness_impl *impl, int status) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    turbo_mutex_lock(&impl->mutex);
    if (impl->slots[i].state == TURBO_READINESS_SLOT_ARMED) {
      (void)readiness_dispatch_slot(impl, &impl->slots[i], 0, status);
    } else {
      turbo_mutex_unlock(&impl->mutex);
    }
  }
}

int turbo_readiness_backend_fail(turbo_readiness_reactor *reactor, int status) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL || status >= TURBO_OK) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->mutex);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EALREADY;
  }
  impl->admission_open = 0;
  impl->backend_errors += 1u;
  turbo_mutex_unlock(&impl->mutex);
  readiness_fanout(impl, status);
  return TURBO_OK;
}

int turbo_readiness_reactor_shutdown(turbo_readiness_reactor *reactor) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  int backend_status;
  if (impl == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->mutex);
  if (impl->shutdown_started) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EALREADY;
  }
  impl->shutdown_started = 1;
  impl->admission_open = 0;
  turbo_mutex_unlock(&impl->mutex);

  backend_status = impl->backend_ops.shutdown(impl->backend_user);
  readiness_fanout(impl, TURBO_ESHUTDOWN);

  turbo_mutex_lock(&impl->mutex);
  for (;;) {
    size_t inflight = 0;
    for (size_t i = 0; i < impl->capacity; ++i)
      inflight += impl->slots[i].inflight != 0;
    if (inflight == 0) break;
    turbo_cond_wait(&impl->changed, &impl->mutex);
  }
  impl->shutdown_complete = 1;
  turbo_mutex_unlock(&impl->mutex);
  return backend_status;
}

int turbo_readiness_reactor_destroy(turbo_readiness_reactor *reactor) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->mutex);
  if (!impl->shutdown_complete) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].state != TURBO_READINESS_SLOT_FREE) {
      turbo_mutex_unlock(&impl->mutex);
      return TURBO_EBUSY;
    }
  }
  turbo_mutex_unlock(&impl->mutex);

  turbo_cond_destroy(&impl->changed);
  turbo_mutex_destroy(&impl->mutex);
  free(impl->event_batch);
  free(impl->slots);
  free(impl);
  reactor->impl = NULL;
  return TURBO_OK;
}

int turbo_readiness_reactor_stats(turbo_readiness_reactor *reactor,
                                  turbo_readiness_stats *stats) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  turbo_readiness_stats snapshot = {0};
  if (impl == NULL || stats == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->mutex);
  snapshot.capacity = impl->capacity;
  snapshot.rejected_full = impl->rejected_full;
  snapshot.stale_events = impl->stale_events;
  snapshot.duplicate_events = impl->duplicate_events;
  snapshot.backend_errors = impl->backend_errors;
  for (size_t i = 0; i < impl->capacity; ++i) {
    const turbo_readiness_slot *slot = &impl->slots[i];
    if (slot->state != TURBO_READINESS_SLOT_FREE)
      snapshot.registered_count += 1u;
    if (slot->state == TURBO_READINESS_SLOT_ARMED)
      snapshot.armed_count += 1u;
    if (slot->inflight)
      snapshot.callbacks_inflight += 1u;
  }
  turbo_mutex_unlock(&impl->mutex);
  *stats = snapshot;
  return TURBO_OK;
}
