#include "readiness_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <errno.h>
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
  uint32_t arm_generation;
  uint64_t arm_token;
  turbo_readiness_slot_state state;
  int inflight;
  int control_inflight;
  int native_registered;
  int terminal_pending;
  int terminal_status;
  int orphaned;
} turbo_readiness_slot;

struct turbo_readiness_impl {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  turbo_readiness_slot *slots;
  size_t capacity;
  turbo_readiness_backend_ops backend_ops;
  void *backend_user;
  uint64_t rejected_full;
  uint64_t stale_events;
  uint64_t duplicate_events;
  uint64_t backend_errors;
  int admission_open;
  int terminalizing;
  int shutdown_inflight;
  int backend_shutdown_inflight;
  int shutdown_complete;
};

static TURBO_THREAD_LOCAL turbo_readiness_slot *readiness_callback_slot;

static int readiness_callback_on_impl(const turbo_readiness_impl *impl) {
  return readiness_callback_slot != NULL && readiness_callback_slot->owner == impl;
}

static uint64_t readiness_slot_token(const turbo_readiness_slot *slot) {
  return ((uint64_t)slot->generation << 32) | (uint64_t)slot->index;
}

static void readiness_slot_reclaim(turbo_readiness_slot *slot) {
  slot->native_resource = 0;
  slot->callback = NULL;
  slot->callback_user = NULL;
  slot->inflight = 0;
  slot->control_inflight = 0;
  slot->native_registered = 0;
  slot->terminal_pending = 0;
  slot->terminal_status = TURBO_OK;
  slot->orphaned = 0;
  slot->arm_token = 0;
  slot->state = TURBO_READINESS_SLOT_FREE;
}

static int readiness_slot_can_reclaim(const turbo_readiness_slot *slot) {
  return slot->state == TURBO_READINESS_SLOT_CLOSING && !slot->inflight &&
         !slot->control_inflight && !slot->native_registered && !slot->terminal_pending &&
         !slot->orphaned;
}

static void readiness_slot_try_reclaim(turbo_readiness_slot *slot) {
  if (readiness_slot_can_reclaim(slot)) readiness_slot_reclaim(slot);
}

static void readiness_wait_slot_control(turbo_readiness_impl *impl, turbo_readiness_slot *slot) {
  while (slot->control_inflight)
    turbo_cond_wait(&impl->changed, &impl->mutex);
}

static int readiness_wait_public_gate(turbo_readiness_impl *impl) {
  while (impl->terminalizing) {
    if (readiness_callback_on_impl(impl)) return TURBO_EBUSY;
    turbo_cond_wait(&impl->changed, &impl->mutex);
  }
  return TURBO_OK;
}

static int readiness_wait_public_slot_control(turbo_readiness_impl *impl,
                                              turbo_readiness_slot *slot,
                                              uint32_t generation) {
  for (;;) {
    int status = readiness_wait_public_gate(impl);
    if (status != TURBO_OK) return status;
    if (slot->generation != generation || slot->state == TURBO_READINESS_SLOT_FREE)
      return TURBO_EBUSY;
    if (!slot->control_inflight) return TURBO_OK;
    turbo_cond_wait(&impl->changed, &impl->mutex);
  }
}

static int readiness_controls_inflight(const turbo_readiness_impl *impl) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].control_inflight) return 1;
  }
  return 0;
}

static void readiness_wait_controls(turbo_readiness_impl *impl) {
  while (readiness_controls_inflight(impl))
    turbo_cond_wait(&impl->changed, &impl->mutex);
}

static void readiness_snapshot_terminal(turbo_readiness_impl *impl, int status) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    turbo_readiness_slot *slot = &impl->slots[i];
    if (slot->state == TURBO_READINESS_SLOT_ARMED && !slot->terminal_pending) {
      slot->terminal_pending = 1;
      slot->terminal_status = status;
    }
  }
}

static turbo_readiness_impl *readiness_impl_from_reactor(turbo_readiness_reactor *reactor) {
  return reactor != NULL ? (turbo_readiness_impl *)reactor->impl : NULL;
}

static turbo_readiness_slot *
readiness_slot_from_registration(turbo_readiness_registration *registration) {
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
  if (address < begin || address >= end || (address - begin) % sizeof(*impl->slots) != 0)
    return NULL;
  return slot;
}

static int readiness_events_valid(turbo_readiness_events events) {
  const turbo_readiness_events valid = TURBO_READINESS_EVENT_READ | TURBO_READINESS_EVENT_WRITE |
                                       TURBO_READINESS_EVENT_ERROR | TURBO_READINESS_EVENT_HANGUP;
  return events != 0 && (events & ~valid) == 0;
}

static int readiness_config_validate(const turbo_readiness_config *config) {
  if (config == NULL || config->registration_capacity == 0 || config->event_batch_capacity == 0)
    return TURBO_EINVAL;
  if (config->registration_capacity > (size_t)UINT32_MAX - 1u) return TURBO_ERANGE;
  if (config->event_batch_capacity > config->registration_capacity + 1u) return TURBO_EINVAL;
  if (config->registration_capacity > SIZE_MAX / sizeof(turbo_readiness_slot))
    return TURBO_ERANGE;
  return TURBO_OK;
}

static int readiness_backend_ops_validate(const turbo_readiness_backend_ops *backend_ops) {
  return backend_ops != NULL && backend_ops->register_resource != NULL &&
         backend_ops->arm != NULL && backend_ops->unarm != NULL && backend_ops->close != NULL &&
         backend_ops->shutdown != NULL && backend_ops->destroy != NULL;
}

int turbo_readiness_reactor_init_backend(turbo_readiness_reactor *reactor,
                                         const turbo_readiness_config *config,
                                         const turbo_readiness_backend_ops *backend_ops,
                                         void *backend_user) {
  turbo_readiness_impl *impl;
  int status;

  if (reactor != NULL) reactor->impl = NULL;
  if (reactor == NULL || !readiness_backend_ops_validate(backend_ops)) return TURBO_EINVAL;
  status = readiness_config_validate(config);
  if (status != TURBO_OK) return status;

  impl = (turbo_readiness_impl *)calloc(1, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->slots = (turbo_readiness_slot *)calloc(config->registration_capacity, sizeof(*impl->slots));
  if (impl->slots == NULL) {
    free(impl);
    return TURBO_ENOMEM;
  }

  turbo_mutex_init(&impl->mutex);
  turbo_cond_init(&impl->changed);
  if (impl->mutex == NULL || impl->changed == NULL) {
    turbo_cond_destroy(&impl->changed);
    turbo_mutex_destroy(&impl->mutex);
    free(impl->slots);
    free(impl);
    return TURBO_ENOMEM;
  }

  impl->capacity = config->registration_capacity;
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
#if defined(TURBO_ENABLE_EPOLL_READINESS)
  return turbo_readiness_epoll_init(reactor, config);
#else
  return TURBO_ENOTSUP;
#endif
}

int turbo_readiness_register(turbo_readiness_reactor *reactor, intptr_t native_resource,
                             turbo_readiness_registration *registration) {
  turbo_readiness_impl *impl;
  turbo_readiness_slot *slot = NULL;
  turbo_readiness_generation_step generation_step;
  uint64_t token;
  int exhausted_slot = 0;
  int status;

  if (registration != NULL) registration->impl = NULL;
  impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL || registration == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->mutex);
  status = readiness_wait_public_gate(impl);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_ESHUTDOWN;
  }
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].state == TURBO_READINESS_SLOT_FREE) {
      if (!turbo_readiness_generation_available(impl->slots[i].generation) ||
          !turbo_readiness_generation_available(impl->slots[i].arm_generation)) {
        exhausted_slot = 1;
        continue;
      }
      slot = &impl->slots[i];
      break;
    }
  }
  if (slot == NULL) {
    if (exhausted_slot) {
      turbo_mutex_unlock(&impl->mutex);
      return -EOVERFLOW;
    }
    impl->rejected_full += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_ENOBUFS;
  }

  status = turbo_readiness_generation_prepare(slot->generation, &generation_step);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->generation = turbo_readiness_generation_commit(&generation_step);
  slot->native_resource = native_resource;
  slot->state = TURBO_READINESS_SLOT_REGISTERED;
  slot->control_inflight = 1;
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.register_resource(impl->backend_user, native_resource, token);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK) {
    slot->control_inflight = 0;
    slot->generation = turbo_readiness_generation_rollback(&generation_step);
    readiness_slot_reclaim(slot);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->native_registered = 1;
  if (impl->admission_open) {
    slot->control_inflight = 0;
    registration->impl = slot;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }

  slot->state = TURBO_READINESS_SLOT_CLOSING;
  turbo_mutex_unlock(&impl->mutex);
  status = impl->backend_ops.close(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  if (status == TURBO_OK) {
    slot->native_registered = 0;
  } else {
    slot->orphaned = 1;
  }
  slot->control_inflight = 0;
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return status == TURBO_OK ? TURBO_ESHUTDOWN : status;
}

int turbo_readiness_arm(turbo_readiness_registration *registration, turbo_readiness_events events,
                        turbo_readiness_callback callback, void *user) {
  turbo_readiness_slot *slot = readiness_slot_from_registration(registration);
  turbo_readiness_impl *impl;
  turbo_readiness_generation_step arm_generation_step;
  uint32_t generation;
  uint64_t token;
  uint64_t arm_token;
  int status;

  if (slot == NULL || callback == NULL || !readiness_events_valid(events)) return TURBO_EINVAL;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  generation = slot->generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
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
  status = turbo_readiness_generation_prepare(slot->arm_generation, &arm_generation_step);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->callback = callback;
  slot->callback_user = user;
  slot->state = TURBO_READINESS_SLOT_ARMED;
  slot->control_inflight = 1;
  slot->arm_generation = turbo_readiness_generation_commit(&arm_generation_step);
  slot->arm_token = ((uint64_t)slot->arm_generation << 32) | (uint64_t)slot->index;
  token = readiness_slot_token(slot);
  arm_token = slot->arm_token;
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.arm(impl->backend_user, token, arm_token, events);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK && readiness_slot_token(slot) == token) {
    slot->arm_generation = turbo_readiness_generation_rollback(&arm_generation_step);
    slot->terminal_pending = 0;
    slot->terminal_status = TURBO_OK;
    if (slot->state == TURBO_READINESS_SLOT_ARMED) {
      slot->state = TURBO_READINESS_SLOT_REGISTERED;
      slot->callback = NULL;
      slot->callback_user = NULL;
      slot->arm_token = 0;
    }
  }
  slot->control_inflight = 0;
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return status;
}

int turbo_readiness_unarm(turbo_readiness_registration *registration) {
  turbo_readiness_slot *slot = readiness_slot_from_registration(registration);
  turbo_readiness_impl *impl;
  uint64_t token;
  uint32_t generation;
  int was_firing;
  int terminal_won;
  int status;

  if (slot == NULL) return TURBO_EINVAL;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (readiness_callback_slot == slot && slot->inflight) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  generation = slot->generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  if (slot->terminal_pending) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  if (slot->state != TURBO_READINESS_SLOT_ARMED && slot->state != TURBO_READINESS_SLOT_FIRING) {
    status = slot->state == TURBO_READINESS_SLOT_REGISTERED ? TURBO_EALREADY : TURBO_EBUSY;
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  was_firing = slot->state == TURBO_READINESS_SLOT_FIRING;
  token = readiness_slot_token(slot);
  slot->control_inflight = 1;
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.unarm(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  while (was_firing && slot->inflight)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  terminal_won = status == TURBO_OK && !was_firing && slot->terminal_pending;
  if (status == TURBO_OK && !was_firing && !slot->terminal_pending) {
    slot->state = TURBO_READINESS_SLOT_REGISTERED;
    slot->callback = NULL;
    slot->callback_user = NULL;
    slot->arm_token = 0;
  }
  slot->control_inflight = 0;
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return terminal_won ? TURBO_EBUSY : status;
}

int turbo_readiness_close(turbo_readiness_registration *registration) {
  turbo_readiness_slot *slot = readiness_slot_from_registration(registration);
  turbo_readiness_impl *impl;
  turbo_readiness_slot_state previous_state;
  uint64_t token;
  uint32_t generation;
  int self_close;
  int status;

  if (slot == NULL) return TURBO_EINVAL;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  self_close = readiness_callback_slot == slot && slot->inflight;
  if (self_close || slot->terminal_pending) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  generation = slot->generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  if (slot->terminal_pending) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  if (slot->state == TURBO_READINESS_SLOT_FREE || slot->state == TURBO_READINESS_SLOT_CLOSING) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  previous_state = slot->state;
  slot->state = TURBO_READINESS_SLOT_CLOSING;
  slot->control_inflight = 1;
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.close(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK) {
    slot->control_inflight = 0;
    if (slot->state == TURBO_READINESS_SLOT_CLOSING) {
      slot->state = previous_state == TURBO_READINESS_SLOT_FIRING && !slot->inflight
                        ? TURBO_READINESS_SLOT_REGISTERED
                        : previous_state;
    }
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->native_registered = 0;
  while (slot->inflight)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  slot->control_inflight = 0;
  readiness_slot_try_reclaim(slot);
  registration->impl = NULL;
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

static int readiness_dispatch_slot(turbo_readiness_impl *impl, turbo_readiness_slot *slot,
                                   turbo_readiness_events events, int status) {
  turbo_readiness_callback callback;
  turbo_readiness_slot *previous_callback_slot;
  void *callback_user;

  slot->state = TURBO_READINESS_SLOT_FIRING;
  slot->inflight = 1;
  slot->arm_token = 0;
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
  if (slot->state == TURBO_READINESS_SLOT_CLOSING) readiness_slot_try_reclaim(slot);
  else slot->state = TURBO_READINESS_SLOT_REGISTERED;
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

static int readiness_backend_dispatch_impl(turbo_readiness_reactor *reactor, uint64_t token,
                                           uint64_t arm_token, int validate_arm_token,
                                           turbo_readiness_events events, int status) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  turbo_readiness_slot *slot;
  uint32_t index = (uint32_t)token;
  uint32_t generation = (uint32_t)(token >> 32);

  if (impl == NULL || (status == TURBO_OK && !readiness_events_valid(events))) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->mutex);
  if ((size_t)index >= impl->capacity) {
    impl->stale_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  slot = &impl->slots[index];
  if (generation != 0 && slot->generation == generation && impl->terminalizing) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  while (slot->control_inflight && slot->generation == generation)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  if (generation != 0 && slot->generation == generation && impl->terminalizing) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (generation == 0 || slot->generation != generation ||
      slot->state == TURBO_READINESS_SLOT_FREE) {
    impl->stale_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (slot->terminal_pending) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (slot->state != TURBO_READINESS_SLOT_ARMED) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (validate_arm_token && (arm_token == 0 || slot->arm_token != arm_token)) {
    impl->stale_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  return readiness_dispatch_slot(impl, slot, events, status);
}

int turbo_readiness_backend_dispatch(turbo_readiness_reactor *reactor, uint64_t token,
                                     turbo_readiness_events events, int status) {
  return readiness_backend_dispatch_impl(reactor, token, 0, 0, events, status);
}

int turbo_readiness_backend_dispatch_generation(turbo_readiness_reactor *reactor,
                                                uint64_t token, uint64_t arm_token,
                                                turbo_readiness_events events, int status) {
  return readiness_backend_dispatch_impl(reactor, token, arm_token, 1, events, status);
}

static void readiness_fanout(turbo_readiness_impl *impl, int status) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    turbo_readiness_callback callback;
    turbo_readiness_slot *previous_callback_slot;
    turbo_readiness_slot *slot;
    void *callback_user;
    int terminal_status;

    turbo_mutex_lock(&impl->mutex);
    slot = &impl->slots[i];
    readiness_wait_slot_control(impl, slot);
    if (!slot->terminal_pending) {
      turbo_mutex_unlock(&impl->mutex);
      continue;
    }
    slot->terminal_pending = 0;
    slot->inflight = 1;
    if (slot->state != TURBO_READINESS_SLOT_CLOSING) slot->state = TURBO_READINESS_SLOT_FIRING;
    callback = slot->callback;
    callback_user = slot->callback_user;
    terminal_status = slot->terminal_status;
    turbo_mutex_unlock(&impl->mutex);

    previous_callback_slot = readiness_callback_slot;
    readiness_callback_slot = slot;
    callback(callback_user, 0, terminal_status != TURBO_OK ? terminal_status : status);
    readiness_callback_slot = previous_callback_slot;

    turbo_mutex_lock(&impl->mutex);
    slot->inflight = 0;
    slot->callback = NULL;
    slot->callback_user = NULL;
    slot->arm_token = 0;
    slot->terminal_status = TURBO_OK;
    if (slot->state == TURBO_READINESS_SLOT_CLOSING) readiness_slot_try_reclaim(slot);
    else slot->state = TURBO_READINESS_SLOT_REGISTERED;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
  }
}

static int readiness_retry_orphan_closes(turbo_readiness_impl *impl) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    turbo_readiness_slot *slot = &impl->slots[i];
    uint64_t token;
    int status;
    if (!slot->orphaned) continue;

    readiness_wait_slot_control(impl, slot);
    slot->control_inflight = 1;
    token = readiness_slot_token(slot);
    turbo_mutex_unlock(&impl->mutex);
    status = impl->backend_ops.close(impl->backend_user, token);
    turbo_mutex_lock(&impl->mutex);
    if (status != TURBO_OK) {
      slot->control_inflight = 0;
      turbo_cond_broadcast(&impl->changed);
      return status;
    }
    slot->native_registered = 0;
    slot->orphaned = 0;
    slot->control_inflight = 0;
    readiness_slot_try_reclaim(slot);
    turbo_cond_broadcast(&impl->changed);
  }
  return TURBO_OK;
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
  impl->terminalizing = 1;
  impl->backend_errors += 1u;
  turbo_cond_broadcast(&impl->changed);
  readiness_wait_controls(impl);
  readiness_snapshot_terminal(impl, status);
  impl->terminalizing = 0;
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  readiness_fanout(impl, status);
  return TURBO_OK;
}

int turbo_readiness_backend_wait_admission_closed(turbo_readiness_reactor *reactor) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->mutex);
  while (impl->admission_open)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

int turbo_readiness_reactor_shutdown(turbo_readiness_reactor *reactor) {
  turbo_readiness_impl *impl = readiness_impl_from_reactor(reactor);
  int orphan_status;
  int backend_status;
  if (impl == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->mutex);
  while (impl->terminalizing) {
    if (readiness_callback_on_impl(impl)) {
      turbo_mutex_unlock(&impl->mutex);
      return TURBO_EBUSY;
    }
    turbo_cond_wait(&impl->changed, &impl->mutex);
  }
  if (impl->shutdown_complete || impl->shutdown_inflight) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EALREADY;
  }
  impl->shutdown_inflight = 1;
  impl->admission_open = 0;
  impl->terminalizing = 1;
  turbo_cond_broadcast(&impl->changed);
  readiness_wait_controls(impl);
  readiness_snapshot_terminal(impl, TURBO_ESHUTDOWN);
  orphan_status = readiness_retry_orphan_closes(impl);
  if (orphan_status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    readiness_fanout(impl, TURBO_ESHUTDOWN);
    turbo_mutex_lock(&impl->mutex);
    impl->shutdown_inflight = 0;
    impl->terminalizing = 0;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return orphan_status;
  }
  readiness_wait_controls(impl);
  impl->backend_shutdown_inflight = 1;
  turbo_mutex_unlock(&impl->mutex);

  backend_status = impl->backend_ops.shutdown(impl->backend_user);
  turbo_mutex_lock(&impl->mutex);
  impl->backend_shutdown_inflight = 0;
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  readiness_fanout(impl, TURBO_ESHUTDOWN);

  turbo_mutex_lock(&impl->mutex);
  for (;;) {
    size_t inflight = 0;
    for (size_t i = 0; i < impl->capacity; ++i)
      inflight += impl->slots[i].inflight != 0 || impl->slots[i].control_inflight != 0;
    if (inflight == 0) break;
    turbo_cond_wait(&impl->changed, &impl->mutex);
  }
  impl->shutdown_inflight = 0;
  if (backend_status == TURBO_OK) impl->shutdown_complete = 1;
  impl->terminalizing = 0;
  turbo_cond_broadcast(&impl->changed);
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
  while (impl->backend_shutdown_inflight || readiness_controls_inflight(impl))
    turbo_cond_wait(&impl->changed, &impl->mutex);
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].state != TURBO_READINESS_SLOT_FREE) {
      turbo_mutex_unlock(&impl->mutex);
      return TURBO_EBUSY;
    }
  }
  turbo_mutex_unlock(&impl->mutex);

  turbo_cond_destroy(&impl->changed);
  turbo_mutex_destroy(&impl->mutex);
  impl->backend_ops.destroy(impl->backend_user);
  free(impl->slots);
  free(impl);
  reactor->impl = NULL;
  return TURBO_OK;
}

int turbo_readiness_reactor_stats(turbo_readiness_reactor *reactor, turbo_readiness_stats *stats) {
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
    if (slot->state != TURBO_READINESS_SLOT_FREE) snapshot.registered_count += 1u;
    if (slot->state == TURBO_READINESS_SLOT_ARMED) snapshot.armed_count += 1u;
    if (slot->inflight) snapshot.callbacks_inflight += 1u;
  }
  turbo_mutex_unlock(&impl->mutex);
  *stats = snapshot;
  return TURBO_OK;
}
