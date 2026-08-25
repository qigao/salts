#include "readiness_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

typedef struct turbo_readiness_impl turbo_readiness_impl;

typedef struct turbo_readiness_slot {
  turbo_readiness_impl *owner;
  intptr_t native_resource;
  turbo_readiness_callback callback;
  turbo_readiness_continuation continuation;
  void *callback_user;
  uint32_t index;
  _Atomic uint32_t generation;
  _Atomic uint32_t arm_generation;
  uint64_t arm_token;
  turbo_readiness_lifecycle lifecycle;
  turbo_readiness_interest interest;
  turbo_readiness_delivery delivery;
  turbo_readiness_terminal terminal;
  turbo_readiness_control control;
  uint32_t arm_waiters;
  uint32_t api_borrows;
  int native_registered;
  int terminal_status;
  int orphaned;
} turbo_readiness_slot;

typedef struct turbo_readiness_api_borrow {
  turbo_readiness_slot *slot;
  turbo_readiness_registration *registration;
  uint32_t generation;
} turbo_readiness_api_borrow;

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

static void readiness_slot_clear_callback(turbo_readiness_slot *slot) {
  slot->callback = NULL;
  slot->continuation = NULL;
  slot->callback_user = NULL;
}

static void *readiness_handle_load(
    const turbo_readiness_registration *registration) {
#if defined(_MSC_VER)
  return _InterlockedCompareExchangePointer(
      (void *volatile *)&registration->impl, NULL, NULL);
#else
  return __atomic_load_n(&registration->impl, __ATOMIC_ACQUIRE);
#endif
}

static void readiness_handle_store(turbo_readiness_registration *registration,
                                   void *value) {
#if defined(_MSC_VER)
  (void)_InterlockedExchangePointer((void *volatile *)&registration->impl,
                                    value);
#else
  __atomic_store_n(&registration->impl, value, __ATOMIC_RELEASE);
#endif
}

static int readiness_handle_clear_if(
    turbo_readiness_registration *registration, void *expected) {
#if defined(_MSC_VER)
  return _InterlockedCompareExchangePointer(
             (void *volatile *)&registration->impl, NULL, expected) == expected;
#else
  return __atomic_compare_exchange_n(&registration->impl, &expected, NULL, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
}

#define TURBO_READINESS_ADMISSION_CLOSED \
  ((uintptr_t)1u << (sizeof(uintptr_t) * CHAR_BIT - 1u))
#define TURBO_READINESS_ADMISSION_COUNT_MASK \
  (TURBO_READINESS_ADMISSION_CLOSED - (uintptr_t)1u)

uintptr_t turbo_readiness_registration_admission_max_entrants(void) {
  const uintptr_t slot_counter_limit = (uintptr_t)UINT32_MAX;
  return TURBO_READINESS_ADMISSION_COUNT_MASK < slot_counter_limit
             ? TURBO_READINESS_ADMISSION_COUNT_MASK
             : slot_counter_limit;
}

static uintptr_t readiness_admission_load(const uintptr_t *admission) {
#if defined(_MSC_VER) && UINTPTR_MAX == UINT64_MAX
  return (uintptr_t)_InterlockedCompareExchange64(
      (volatile long long *)admission, 0, 0);
#elif defined(_MSC_VER)
  return (uintptr_t)_InterlockedCompareExchange(
      (volatile long *)admission, 0, 0);
#else
  return __atomic_load_n(admission, __ATOMIC_ACQUIRE);
#endif
}

static int readiness_admission_compare_exchange(
    uintptr_t *admission, uintptr_t *expected, uintptr_t desired) {
#if defined(_MSC_VER) && UINTPTR_MAX == UINT64_MAX
  uintptr_t observed = (uintptr_t)_InterlockedCompareExchange64(
      (volatile long long *)admission, (long long)desired,
      (long long)*expected);
#elif defined(_MSC_VER)
  uintptr_t observed = (uintptr_t)_InterlockedCompareExchange(
      (volatile long *)admission, (long)desired, (long)*expected);
#else
  return __atomic_compare_exchange_n(admission, expected, desired, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
#if defined(_MSC_VER)
  if (observed == *expected) return 1;
  *expected = observed;
  return 0;
#endif
}

int turbo_readiness_registration_admission_enter(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return TURBO_EINVAL;
  observed = readiness_admission_load(admission);
  for (;;) {
    uintptr_t entrants = observed & TURBO_READINESS_ADMISSION_COUNT_MASK;
    if ((observed & TURBO_READINESS_ADMISSION_CLOSED) != 0u)
      return TURBO_EBUSY;
    if (entrants >=
        turbo_readiness_registration_admission_max_entrants())
      return -EOVERFLOW;
    if (readiness_admission_compare_exchange(admission, &observed,
                                             observed + 1u))
      return TURBO_OK;
  }
}

int turbo_readiness_registration_admission_reserve_register(
    uintptr_t *admission) {
  uintptr_t expected = 0u;
  if (admission == NULL) return TURBO_EINVAL;
  return readiness_admission_compare_exchange(
             admission, &expected, TURBO_READINESS_ADMISSION_CLOSED)
             ? TURBO_OK
             : TURBO_EBUSY;
}

int turbo_readiness_registration_admission_close(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return TURBO_EINVAL;
  observed = readiness_admission_load(admission);
  for (;;) {
    if ((observed & TURBO_READINESS_ADMISSION_CLOSED) != 0u)
      return TURBO_EALREADY;
    if (readiness_admission_compare_exchange(
            admission, &observed,
            observed | TURBO_READINESS_ADMISSION_CLOSED))
      return TURBO_OK;
  }
}

static void readiness_registration_admission_reopen(uintptr_t *admission) {
  uintptr_t observed = readiness_admission_load(admission);
  while ((observed & TURBO_READINESS_ADMISSION_CLOSED) != 0u) {
    if (readiness_admission_compare_exchange(
            admission, &observed,
            observed & ~TURBO_READINESS_ADMISSION_CLOSED))
      return;
  }
}

void turbo_readiness_registration_admission_leave(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return;
  observed = readiness_admission_load(admission);
  while ((observed & TURBO_READINESS_ADMISSION_COUNT_MASK) != 0u) {
    if (readiness_admission_compare_exchange(admission, &observed,
                                             observed - 1u))
      return;
  }
}

int turbo_readiness_registration_admission_reset(uintptr_t *admission) {
  uintptr_t observed;
  if (admission == NULL) return TURBO_EINVAL;
  observed = readiness_admission_load(admission);
  for (;;) {
    if ((observed & TURBO_READINESS_ADMISSION_COUNT_MASK) != 0u)
      return TURBO_EBUSY;
    if (readiness_admission_compare_exchange(admission, &observed, 0u))
      return TURBO_OK;
  }
}

uint32_t turbo_readiness_registration_admission_entrants(
    const uintptr_t *admission) {
  if (admission == NULL) return 0u;
  return (uint32_t)(readiness_admission_load(admission) &
                    TURBO_READINESS_ADMISSION_COUNT_MASK);
}

int turbo_readiness_state_model_valid(
    const turbo_readiness_state_view *view) {
  int inactive;
  if (view == NULL ||
      view->lifecycle > TURBO_READINESS_LIFECYCLE_RETIRED ||
      view->interest > TURBO_READINESS_INTEREST_UNARMING ||
      view->delivery > TURBO_READINESS_DELIVERY_CALLBACK ||
      view->terminal > TURBO_READINESS_TERMINAL_DELIVERING ||
      view->control > TURBO_READINESS_CONTROL_CLOSE)
    return 0;
  inactive = view->lifecycle == TURBO_READINESS_LIFECYCLE_FREE ||
             view->lifecycle == TURBO_READINESS_LIFECYCLE_RETIRED;
  if (inactive)
    return view->interest == TURBO_READINESS_INTEREST_IDLE &&
           view->delivery == TURBO_READINESS_DELIVERY_IDLE &&
           view->terminal == TURBO_READINESS_TERMINAL_NONE &&
           view->control == TURBO_READINESS_CONTROL_NONE &&
           view->callback == NULL && view->arm_token == 0u &&
           view->arm_waiters == 0u && view->api_borrows == 0u &&
           !view->native_registered && !view->orphaned;
  if ((view->interest == TURBO_READINESS_INTEREST_ARMED ||
       view->interest == TURBO_READINESS_INTEREST_ARMING) &&
      (view->callback == NULL || view->arm_token == 0u))
    return 0;
  if (view->delivery == TURBO_READINESS_DELIVERY_CALLBACK &&
      view->callback == NULL)
    return 0;
  if (view->terminal == TURBO_READINESS_TERMINAL_RESERVED &&
      (view->lifecycle != TURBO_READINESS_LIFECYCLE_OPEN ||
       view->interest != TURBO_READINESS_INTEREST_ARMED ||
       view->delivery != TURBO_READINESS_DELIVERY_IDLE))
    return 0;
  if (view->terminal == TURBO_READINESS_TERMINAL_DELIVERING &&
      (view->interest != TURBO_READINESS_INTEREST_IDLE ||
       view->delivery != TURBO_READINESS_DELIVERY_CALLBACK))
    return 0;
  if ((view->interest == TURBO_READINESS_INTEREST_ARMING) !=
      (view->control == TURBO_READINESS_CONTROL_ARM))
    return 0;
  if ((view->interest == TURBO_READINESS_INTEREST_UNARMING) !=
      (view->control == TURBO_READINESS_CONTROL_UNARM))
    return 0;
  if (view->control == TURBO_READINESS_CONTROL_REGISTER &&
      (view->lifecycle != TURBO_READINESS_LIFECYCLE_OPEN ||
       view->interest != TURBO_READINESS_INTEREST_IDLE))
    return 0;
  if (view->control == TURBO_READINESS_CONTROL_CLOSE &&
      view->lifecycle != TURBO_READINESS_LIFECYCLE_CLOSING)
    return 0;
  if (view->orphaned &&
      (view->lifecycle != TURBO_READINESS_LIFECYCLE_CLOSING ||
       !view->native_registered))
    return 0;
  return 1;
}

int turbo_readiness_callback_forms_valid(
    turbo_readiness_callback callback,
    turbo_readiness_continuation continuation) {
  return (callback == NULL) != (continuation == NULL);
}

static int readiness_callback_on_impl(const turbo_readiness_impl *impl) {
  return readiness_callback_slot != NULL && readiness_callback_slot->owner == impl;
}

static uint64_t readiness_slot_token(const turbo_readiness_slot *slot) {
  return ((uint64_t)slot->generation << 32) | (uint64_t)slot->index;
}

static void readiness_slot_reclaim(turbo_readiness_slot *slot) {
  turbo_readiness_lifecycle inactive_lifecycle =
      turbo_readiness_generation_available(slot->generation) &&
              turbo_readiness_generation_available(slot->arm_generation)
          ? TURBO_READINESS_LIFECYCLE_FREE
          : TURBO_READINESS_LIFECYCLE_RETIRED;
  slot->native_resource = 0;
  readiness_slot_clear_callback(slot);
  slot->interest = TURBO_READINESS_INTEREST_IDLE;
  slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
  slot->terminal = TURBO_READINESS_TERMINAL_NONE;
  slot->control = TURBO_READINESS_CONTROL_NONE;
  slot->native_registered = 0;
  slot->terminal_status = TURBO_OK;
  slot->orphaned = 0;
  slot->arm_token = 0;
  slot->lifecycle = inactive_lifecycle;
}

static int readiness_slot_can_reclaim(const turbo_readiness_slot *slot) {
  return slot->lifecycle == TURBO_READINESS_LIFECYCLE_CLOSING &&
         slot->interest == TURBO_READINESS_INTEREST_IDLE &&
         slot->delivery == TURBO_READINESS_DELIVERY_IDLE &&
         slot->terminal == TURBO_READINESS_TERMINAL_NONE &&
         slot->control == TURBO_READINESS_CONTROL_NONE &&
         slot->arm_waiters == 0u && slot->api_borrows == 0u &&
         !slot->native_registered && !slot->orphaned;
}

static void readiness_slot_try_reclaim(turbo_readiness_slot *slot) {
  if (readiness_slot_can_reclaim(slot)) readiness_slot_reclaim(slot);
}

static void readiness_wait_slot_control(turbo_readiness_impl *impl, turbo_readiness_slot *slot) {
  while (slot->control != TURBO_READINESS_CONTROL_NONE)
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
    if (slot->generation != generation ||
        slot->lifecycle == TURBO_READINESS_LIFECYCLE_FREE ||
        slot->lifecycle == TURBO_READINESS_LIFECYCLE_RETIRED)
      return TURBO_EBUSY;
    if (slot->lifecycle == TURBO_READINESS_LIFECYCLE_CLOSING)
      return TURBO_EBUSY;
    if (slot->control == TURBO_READINESS_CONTROL_NONE) return TURBO_OK;
    turbo_cond_wait(&impl->changed, &impl->mutex);
  }
}

static int readiness_controls_inflight(const turbo_readiness_impl *impl) {
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].control != TURBO_READINESS_CONTROL_NONE) return 1;
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
    if (slot->lifecycle == TURBO_READINESS_LIFECYCLE_OPEN &&
        slot->interest == TURBO_READINESS_INTEREST_ARMED &&
        slot->terminal == TURBO_READINESS_TERMINAL_NONE) {
      slot->terminal = TURBO_READINESS_TERMINAL_RESERVED;
      slot->terminal_status = status;
    }
  }
}

static turbo_readiness_impl *readiness_impl_from_reactor(turbo_readiness_reactor *reactor) {
  return reactor != NULL ? (turbo_readiness_impl *)reactor->impl : NULL;
}

static turbo_readiness_slot *readiness_slot_from_pointer(void *pointer) {
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  uintptr_t address;
  uintptr_t begin;
  uintptr_t end;

  if (pointer == NULL) return NULL;
  slot = (turbo_readiness_slot *)pointer;
  impl = slot->owner;
  if (impl == NULL || impl->slots == NULL) return NULL;
  address = (uintptr_t)slot;
  begin = (uintptr_t)impl->slots;
  end = begin + impl->capacity * sizeof(*impl->slots);
  if (address < begin || address >= end || (address - begin) % sizeof(*impl->slots) != 0)
    return NULL;
  return slot;
}

static int readiness_api_borrow_acquire(
    turbo_readiness_registration *registration,
    turbo_readiness_api_borrow *borrow) {
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  void *pointer;

  if (registration == NULL || borrow == NULL)
    return TURBO_EINVAL;
  borrow->slot = NULL;
  borrow->registration = NULL;
  borrow->generation = 0u;
  {
    int status = turbo_readiness_registration_admission_enter(
        &registration->_admission);
    if (status != TURBO_OK) return status;
  }
  pointer = readiness_handle_load(registration);
  slot = readiness_slot_from_pointer(pointer);
  if (slot == NULL) {
    turbo_readiness_registration_admission_leave(&registration->_admission);
    return TURBO_EINVAL;
  }
  impl = slot->owner;
  borrow->generation = atomic_load_explicit(&slot->generation,
                                             memory_order_acquire);
  turbo_mutex_lock(&impl->mutex);
  if (readiness_handle_load(registration) != pointer ||
      slot->generation != borrow->generation ||
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_FREE ||
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_RETIRED) {
    turbo_readiness_registration_admission_leave(&registration->_admission);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  if (slot->api_borrows == UINT32_MAX) {
    turbo_readiness_registration_admission_leave(&registration->_admission);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return -EOVERFLOW;
  }
  ++slot->api_borrows;
  borrow->slot = slot;
  borrow->registration = registration;
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

static void readiness_api_borrow_release(
    turbo_readiness_api_borrow *borrow) {
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  if (borrow == NULL || borrow->slot == NULL) return;
  slot = borrow->slot;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (slot->api_borrows != 0u) --slot->api_borrows;
  turbo_readiness_registration_admission_leave(
      &borrow->registration->_admission);
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  borrow->slot = NULL;
  borrow->registration = NULL;
}

static int readiness_api_borrow_return(
    turbo_readiness_api_borrow *borrow, int status) {
  readiness_api_borrow_release(borrow);
  return status;
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
#elif defined(TURBO_ENABLE_KQUEUE_READINESS)
  return turbo_readiness_kqueue_init(reactor, config);
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

  if (registration != NULL) {
    status = turbo_readiness_registration_admission_reserve_register(
        &registration->_admission);
    if (status != TURBO_OK) return status;
    readiness_handle_store(registration, NULL);
  }
  impl = readiness_impl_from_reactor(reactor);
  if (impl == NULL || registration == NULL) {
    if (registration != NULL)
      (void)turbo_readiness_registration_admission_reset(
          &registration->_admission);
    return TURBO_EINVAL;
  }

  turbo_mutex_lock(&impl->mutex);
  status = readiness_wait_public_gate(impl);
  if (status != TURBO_OK) {
    (void)turbo_readiness_registration_admission_reset(
        &registration->_admission);
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  if (!impl->admission_open) {
    (void)turbo_readiness_registration_admission_reset(
        &registration->_admission);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_ESHUTDOWN;
  }
  for (size_t i = 0; i < impl->capacity; ++i) {
    if (impl->slots[i].lifecycle == TURBO_READINESS_LIFECYCLE_RETIRED) {
      exhausted_slot = 1;
      continue;
    }
    if (impl->slots[i].lifecycle == TURBO_READINESS_LIFECYCLE_FREE) {
      if (!turbo_readiness_generation_available(impl->slots[i].generation) ||
          !turbo_readiness_generation_available(impl->slots[i].arm_generation)) {
        impl->slots[i].lifecycle = TURBO_READINESS_LIFECYCLE_RETIRED;
        exhausted_slot = 1;
        continue;
      }
      slot = &impl->slots[i];
      break;
    }
  }
  if (slot == NULL) {
    (void)turbo_readiness_registration_admission_reset(
        &registration->_admission);
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
    (void)turbo_readiness_registration_admission_reset(
        &registration->_admission);
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->generation = turbo_readiness_generation_commit(&generation_step);
  slot->native_resource = native_resource;
  slot->lifecycle = TURBO_READINESS_LIFECYCLE_OPEN;
  slot->interest = TURBO_READINESS_INTEREST_IDLE;
  slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
  slot->terminal = TURBO_READINESS_TERMINAL_NONE;
  slot->control = TURBO_READINESS_CONTROL_REGISTER;
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.register_resource(impl->backend_user, native_resource, token);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK) {
    slot->control = TURBO_READINESS_CONTROL_NONE;
    slot->generation = turbo_readiness_generation_rollback(&generation_step);
    slot->lifecycle = TURBO_READINESS_LIFECYCLE_CLOSING;
    readiness_slot_reclaim(slot);
    (void)turbo_readiness_registration_admission_reset(
        &registration->_admission);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return status;
  }
  slot->native_registered = 1;
  if (impl->admission_open) {
    slot->control = TURBO_READINESS_CONTROL_NONE;
    readiness_handle_store(registration, slot);
    (void)turbo_readiness_registration_admission_reset(
        &registration->_admission);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }

  slot->lifecycle = TURBO_READINESS_LIFECYCLE_CLOSING;
  slot->control = TURBO_READINESS_CONTROL_CLOSE;
  turbo_mutex_unlock(&impl->mutex);
  status = impl->backend_ops.close(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  if (status == TURBO_OK) {
    slot->native_registered = 0;
  } else {
    slot->orphaned = 1;
  }
  slot->control = TURBO_READINESS_CONTROL_NONE;
  readiness_slot_try_reclaim(slot);
  (void)turbo_readiness_registration_admission_reset(
      &registration->_admission);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return status == TURBO_OK ? TURBO_ESHUTDOWN : status;
}

static int readiness_arm_impl(
    turbo_readiness_registration *registration,
    turbo_readiness_events events,
    turbo_readiness_callback callback,
    turbo_readiness_continuation continuation,
    void *user) {
  turbo_readiness_api_borrow borrow = {0};
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  turbo_readiness_generation_step arm_generation_step;
  uint32_t generation;
  uint64_t token;
  uint64_t arm_token;
  int status;

  if (!turbo_readiness_callback_forms_valid(callback, continuation) ||
      !readiness_events_valid(events))
    return TURBO_EINVAL;
  status = readiness_api_borrow_acquire(registration, &borrow);
  if (status != TURBO_OK) return status;
  slot = borrow.slot;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (readiness_callback_slot == slot &&
      slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
  }
  generation = borrow.generation;
  for (;;) {
    status = readiness_wait_public_slot_control(impl, slot, generation);
    if (status != TURBO_OK) {
      turbo_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, status);
    }
    if (!impl->admission_open) {
      turbo_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, TURBO_ESHUTDOWN);
    }
    if (slot->terminal != TURBO_READINESS_TERMINAL_NONE) {
      turbo_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
    }
    if (slot->delivery == TURBO_READINESS_DELIVERY_IDLE) {
      if (slot->interest == TURBO_READINESS_INTEREST_ARMED) {
        turbo_mutex_unlock(&impl->mutex);
        return readiness_api_borrow_return(&borrow, TURBO_EALREADY);
      }
      if (slot->interest == TURBO_READINESS_INTEREST_IDLE) break;
      turbo_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
    }
    if (slot->arm_waiters == UINT32_MAX) {
      turbo_mutex_unlock(&impl->mutex);
      return readiness_api_borrow_return(&borrow, -EOVERFLOW);
    }
    ++slot->arm_waiters;
    turbo_cond_broadcast(&impl->changed);
    while (slot->generation == generation &&
           slot->lifecycle == TURBO_READINESS_LIFECYCLE_OPEN &&
           slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK &&
           slot->terminal == TURBO_READINESS_TERMINAL_NONE &&
           slot->control == TURBO_READINESS_CONTROL_NONE &&
           impl->admission_open && !impl->terminalizing)
      turbo_cond_wait(&impl->changed, &impl->mutex);
    --slot->arm_waiters;
    readiness_slot_try_reclaim(slot);
    turbo_cond_broadcast(&impl->changed);
  }
  status = turbo_readiness_generation_prepare(slot->arm_generation, &arm_generation_step);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  slot->callback = callback;
  slot->continuation = continuation;
  slot->callback_user = user;
  slot->interest = TURBO_READINESS_INTEREST_ARMING;
  slot->control = TURBO_READINESS_CONTROL_ARM;
  slot->arm_generation = turbo_readiness_generation_commit(&arm_generation_step);
  slot->arm_token = ((uint64_t)slot->arm_generation << 32) | (uint64_t)slot->index;
  token = readiness_slot_token(slot);
  arm_token = slot->arm_token;
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.arm(impl->backend_user, token, arm_token, events);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK && readiness_slot_token(slot) == token) {
    slot->arm_generation = turbo_readiness_generation_rollback(&arm_generation_step);
    slot->terminal_status = TURBO_OK;
    if (slot->interest == TURBO_READINESS_INTEREST_ARMING &&
        slot->control == TURBO_READINESS_CONTROL_ARM) {
      slot->interest = TURBO_READINESS_INTEREST_IDLE;
      readiness_slot_clear_callback(slot);
      slot->arm_token = 0;
    }
  } else if (status == TURBO_OK && readiness_slot_token(slot) == token &&
             slot->interest == TURBO_READINESS_INTEREST_ARMING &&
             slot->control == TURBO_READINESS_CONTROL_ARM) {
    slot->interest = TURBO_READINESS_INTEREST_ARMED;
  }
  slot->control = TURBO_READINESS_CONTROL_NONE;
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return readiness_api_borrow_return(&borrow, status);
}

int turbo_readiness_arm(turbo_readiness_registration *registration,
                        turbo_readiness_events events,
                        turbo_readiness_callback callback, void *user) {
  return readiness_arm_impl(registration, events, callback, NULL, user);
}

int turbo_readiness_arm_continuation(
    turbo_readiness_registration *registration,
    turbo_readiness_events events,
    turbo_readiness_continuation continuation, void *user) {
  return readiness_arm_impl(registration, events, NULL, continuation, user);
}

int turbo_readiness_backend_wait_arm_waiter(
    turbo_readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns) {
  return turbo_readiness_backend_wait_arm_waiter_observe(
      registration, waiters, timeout_ns, NULL);
}

int turbo_readiness_backend_wait_arm_waiter_observe(
    turbo_readiness_registration *registration, uint32_t waiters,
    uint64_t timeout_ns, uint32_t *api_borrows) {
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  void *pointer;
  uint32_t generation;
  int status = TURBO_OK;

  if (api_borrows != NULL) *api_borrows = UINT32_MAX;
  if (waiters == 0u || timeout_ns == 0u) return TURBO_EINVAL;
  if (registration == NULL) return TURBO_EINVAL;
  status = turbo_readiness_registration_admission_enter(
      &registration->_admission);
  if (status != TURBO_OK) return status;
  pointer = readiness_handle_load(registration);
  slot = readiness_slot_from_pointer(pointer);
  if (slot == NULL) {
    turbo_readiness_registration_admission_leave(&registration->_admission);
    return TURBO_EINVAL;
  }
  impl = slot->owner;
  generation = atomic_load_explicit(&slot->generation, memory_order_acquire);
  turbo_mutex_lock(&impl->mutex);
  if (readiness_handle_load(registration) != pointer ||
      slot->generation != generation ||
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_FREE ||
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_RETIRED) {
    status = TURBO_EBUSY;
  }
  while (slot->generation == generation &&
         slot->lifecycle == TURBO_READINESS_LIFECYCLE_OPEN &&
         slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK &&
         slot->arm_waiters < waiters && status == TURBO_OK)
    status = turbo_cond_timedwait(&impl->changed, &impl->mutex, timeout_ns);
  if (status == TURBO_OK &&
      (slot->generation != generation ||
       slot->lifecycle != TURBO_READINESS_LIFECYCLE_OPEN ||
       slot->delivery != TURBO_READINESS_DELIVERY_CALLBACK ||
       slot->arm_waiters < waiters))
    status = TURBO_EBUSY;
  if (api_borrows != NULL) *api_borrows = slot->api_borrows;
  turbo_readiness_registration_admission_leave(&registration->_admission);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return status;
}

int turbo_readiness_unarm(turbo_readiness_registration *registration) {
  turbo_readiness_api_borrow borrow = {0};
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  turbo_readiness_interest previous_interest;
  uint64_t token;
  uint32_t generation;
  int status;

  status = readiness_api_borrow_acquire(registration, &borrow);
  if (status != TURBO_OK) return status;
  slot = borrow.slot;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  if (readiness_callback_slot == slot &&
      slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
  }
  generation = borrow.generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  if (slot->terminal != TURBO_READINESS_TERMINAL_NONE) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
  }
  if (slot->delivery == TURBO_READINESS_DELIVERY_IDLE &&
      slot->interest != TURBO_READINESS_INTEREST_ARMED) {
    status = slot->interest == TURBO_READINESS_INTEREST_IDLE
                 ? TURBO_EALREADY
                 : TURBO_EBUSY;
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  previous_interest = slot->interest;
  token = readiness_slot_token(slot);
  slot->interest = TURBO_READINESS_INTEREST_UNARMING;
  slot->control = TURBO_READINESS_CONTROL_UNARM;
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.unarm(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  while (slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  if (status == TURBO_OK) {
    slot->interest = TURBO_READINESS_INTEREST_IDLE;
    readiness_slot_clear_callback(slot);
    slot->arm_token = 0;
  } else {
    slot->interest = previous_interest;
  }
  slot->control = TURBO_READINESS_CONTROL_NONE;
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return readiness_api_borrow_return(&borrow, status);
}

int turbo_readiness_close(turbo_readiness_registration *registration) {
  turbo_readiness_api_borrow borrow = {0};
  turbo_readiness_slot *slot;
  turbo_readiness_impl *impl;
  uint64_t token;
  uint32_t generation;
  int self_close;
  int status;

  status = readiness_api_borrow_acquire(registration, &borrow);
  if (status != TURBO_OK) return status;
  slot = borrow.slot;
  impl = slot->owner;
  turbo_mutex_lock(&impl->mutex);
  self_close = readiness_callback_slot == slot &&
               slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK;
  if (self_close || slot->terminal != TURBO_READINESS_TERMINAL_NONE) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
  }
  generation = borrow.generation;
  status = readiness_wait_public_slot_control(impl, slot, generation);
  if (status != TURBO_OK) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  if (slot->terminal != TURBO_READINESS_TERMINAL_NONE) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
  }
  if (slot->lifecycle != TURBO_READINESS_LIFECYCLE_OPEN) {
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, TURBO_EBUSY);
  }
  slot->lifecycle = TURBO_READINESS_LIFECYCLE_CLOSING;
  slot->control = TURBO_READINESS_CONTROL_CLOSE;
  status = turbo_readiness_registration_admission_close(
      &registration->_admission);
  if (status != TURBO_OK) {
    slot->lifecycle = TURBO_READINESS_LIFECYCLE_OPEN;
    slot->control = TURBO_READINESS_CONTROL_NONE;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  token = readiness_slot_token(slot);
  turbo_mutex_unlock(&impl->mutex);

  status = impl->backend_ops.close(impl->backend_user, token);
  turbo_mutex_lock(&impl->mutex);
  if (status != TURBO_OK) {
    slot->control = TURBO_READINESS_CONTROL_NONE;
    slot->lifecycle = TURBO_READINESS_LIFECYCLE_OPEN;
    readiness_registration_admission_reopen(&registration->_admission);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return readiness_api_borrow_return(&borrow, status);
  }
  slot->native_registered = 0;
  if (slot->delivery == TURBO_READINESS_DELIVERY_IDLE) {
    readiness_slot_clear_callback(slot);
  }
  slot->interest = TURBO_READINESS_INTEREST_IDLE;
  slot->arm_token = 0;
  while (slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK ||
         slot->arm_waiters != 0u || slot->api_borrows != 1u ||
         turbo_readiness_registration_admission_entrants(
             &registration->_admission) != 1u)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  (void)readiness_handle_clear_if(registration, slot);
  slot->control = TURBO_READINESS_CONTROL_NONE;
  --slot->api_borrows;
  turbo_readiness_registration_admission_leave(&registration->_admission);
  (void)turbo_readiness_registration_admission_reset(
      &registration->_admission);
  borrow.slot = NULL;
  borrow.registration = NULL;
  readiness_slot_try_reclaim(slot);
  turbo_cond_broadcast(&impl->changed);
  turbo_mutex_unlock(&impl->mutex);
  return TURBO_OK;
}

static int readiness_dispatch_slot(turbo_readiness_impl *impl, turbo_readiness_slot *slot,
                                   turbo_readiness_events events, int status) {
  turbo_readiness_callback callback;
  turbo_readiness_continuation continuation;
  turbo_readiness_callback_result result = {
      TURBO_READINESS_COMPLETE, 0u};
  turbo_readiness_generation_step arm_generation_step;
  turbo_readiness_slot *previous_callback_slot;
  void *callback_user;
  uint64_t token;
  uint64_t arm_token;
  int rearm_status;

  slot->interest = TURBO_READINESS_INTEREST_IDLE;
  slot->delivery = TURBO_READINESS_DELIVERY_CALLBACK;
  slot->arm_token = 0;
  callback = slot->callback;
  continuation = slot->continuation;
  callback_user = slot->callback_user;
  turbo_mutex_unlock(&impl->mutex);

  previous_callback_slot = readiness_callback_slot;
  readiness_callback_slot = slot;
  if (continuation != NULL)
    result = continuation(callback_user, events, status);
  else
    callback(callback_user, events, status);
  readiness_callback_slot = previous_callback_slot;

  turbo_mutex_lock(&impl->mutex);
  if (continuation != NULL && status == TURBO_OK &&
      result.action != TURBO_READINESS_COMPLETE &&
      result.action != TURBO_READINESS_REARM) {
    slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
    readiness_slot_clear_callback(slot);
    readiness_slot_try_reclaim(slot);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EINVAL;
  }
  if (continuation != NULL && status == TURBO_OK &&
      result.action == TURBO_READINESS_REARM &&
      !readiness_events_valid(result.interests)) {
    slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
    readiness_slot_clear_callback(slot);
    readiness_slot_try_reclaim(slot);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EINVAL;
  }
  if (continuation != NULL && status == TURBO_OK &&
      result.action == TURBO_READINESS_REARM &&
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_OPEN &&
      slot->terminal == TURBO_READINESS_TERMINAL_NONE &&
      slot->control == TURBO_READINESS_CONTROL_NONE &&
      impl->admission_open && !impl->terminalizing) {
    rearm_status = turbo_readiness_generation_prepare(
        slot->arm_generation, &arm_generation_step);
    if (rearm_status == TURBO_OK) {
      slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
      slot->interest = TURBO_READINESS_INTEREST_ARMING;
      slot->control = TURBO_READINESS_CONTROL_ARM;
      slot->arm_generation =
          turbo_readiness_generation_commit(&arm_generation_step);
      slot->arm_token =
          ((uint64_t)slot->arm_generation << 32) | (uint64_t)slot->index;
      token = readiness_slot_token(slot);
      arm_token = slot->arm_token;
      turbo_mutex_unlock(&impl->mutex);

      rearm_status = impl->backend_ops.arm(
          impl->backend_user, token, arm_token, result.interests);

      turbo_mutex_lock(&impl->mutex);
      if (rearm_status == TURBO_OK &&
          readiness_slot_token(slot) == token &&
          slot->interest == TURBO_READINESS_INTEREST_ARMING &&
          slot->control == TURBO_READINESS_CONTROL_ARM) {
        slot->interest = TURBO_READINESS_INTEREST_ARMED;
        slot->control = TURBO_READINESS_CONTROL_NONE;
        turbo_cond_broadcast(&impl->changed);
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_OK;
      }
      if (readiness_slot_token(slot) == token &&
          slot->interest == TURBO_READINESS_INTEREST_ARMING &&
          slot->control == TURBO_READINESS_CONTROL_ARM) {
        slot->arm_generation =
            turbo_readiness_generation_rollback(&arm_generation_step);
        slot->interest = TURBO_READINESS_INTEREST_IDLE;
        slot->arm_token = 0u;
      }
    }
    if (slot->control == TURBO_READINESS_CONTROL_ARM)
      slot->control = TURBO_READINESS_CONTROL_NONE;
    slot->terminal = TURBO_READINESS_TERMINAL_DELIVERING;
    slot->delivery = TURBO_READINESS_DELIVERY_CALLBACK;
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);

    previous_callback_slot = readiness_callback_slot;
    readiness_callback_slot = slot;
    (void)continuation(callback_user, 0u, rearm_status);
    readiness_callback_slot = previous_callback_slot;

    turbo_mutex_lock(&impl->mutex);
    slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
    slot->terminal = TURBO_READINESS_TERMINAL_NONE;
    readiness_slot_clear_callback(slot);
    readiness_slot_try_reclaim(slot);
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
  readiness_slot_clear_callback(slot);
  readiness_slot_try_reclaim(slot);
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
  while (slot->control != TURBO_READINESS_CONTROL_NONE &&
         slot->generation == generation)
    turbo_cond_wait(&impl->changed, &impl->mutex);
  if (generation != 0 && slot->generation == generation && impl->terminalizing) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (generation == 0 || slot->generation != generation ||
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_FREE ||
      slot->lifecycle == TURBO_READINESS_LIFECYCLE_RETIRED) {
    impl->stale_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (slot->terminal != TURBO_READINESS_TERMINAL_NONE) {
    impl->duplicate_events += 1u;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
  }
  if (slot->lifecycle != TURBO_READINESS_LIFECYCLE_OPEN ||
      slot->interest != TURBO_READINESS_INTEREST_ARMED ||
      slot->delivery != TURBO_READINESS_DELIVERY_IDLE) {
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
    turbo_readiness_continuation continuation;
    turbo_readiness_slot *previous_callback_slot;
    turbo_readiness_slot *slot;
    void *callback_user;
    int terminal_status;

    turbo_mutex_lock(&impl->mutex);
    slot = &impl->slots[i];
    readiness_wait_slot_control(impl, slot);
    if (slot->terminal != TURBO_READINESS_TERMINAL_RESERVED) {
      turbo_mutex_unlock(&impl->mutex);
      continue;
    }
    slot->terminal = TURBO_READINESS_TERMINAL_DELIVERING;
    slot->interest = TURBO_READINESS_INTEREST_IDLE;
    slot->delivery = TURBO_READINESS_DELIVERY_CALLBACK;
    slot->arm_token = 0;
    callback = slot->callback;
    continuation = slot->continuation;
    callback_user = slot->callback_user;
    terminal_status = slot->terminal_status;
    turbo_mutex_unlock(&impl->mutex);

    previous_callback_slot = readiness_callback_slot;
    readiness_callback_slot = slot;
    if (continuation != NULL)
      (void)continuation(
          callback_user, 0,
          terminal_status != TURBO_OK ? terminal_status : status);
    else
      callback(callback_user, 0,
               terminal_status != TURBO_OK ? terminal_status : status);
    readiness_callback_slot = previous_callback_slot;

    turbo_mutex_lock(&impl->mutex);
    slot->delivery = TURBO_READINESS_DELIVERY_IDLE;
    slot->terminal = TURBO_READINESS_TERMINAL_NONE;
    readiness_slot_clear_callback(slot);
    slot->terminal_status = TURBO_OK;
    readiness_slot_try_reclaim(slot);
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
    slot->control = TURBO_READINESS_CONTROL_CLOSE;
    token = readiness_slot_token(slot);
    turbo_mutex_unlock(&impl->mutex);
    status = impl->backend_ops.close(impl->backend_user, token);
    turbo_mutex_lock(&impl->mutex);
    if (status != TURBO_OK) {
      slot->control = TURBO_READINESS_CONTROL_NONE;
      turbo_cond_broadcast(&impl->changed);
      return status;
    }
    slot->native_registered = 0;
    slot->orphaned = 0;
    slot->control = TURBO_READINESS_CONTROL_NONE;
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
  if (readiness_callback_on_impl(impl)) {
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_EBUSY;
  }
  while (impl->terminalizing) {
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
      inflight +=
          impl->slots[i].delivery == TURBO_READINESS_DELIVERY_CALLBACK ||
          impl->slots[i].control != TURBO_READINESS_CONTROL_NONE;
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
    if (impl->slots[i].lifecycle != TURBO_READINESS_LIFECYCLE_FREE &&
        impl->slots[i].lifecycle != TURBO_READINESS_LIFECYCLE_RETIRED) {
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
    if (slot->lifecycle == TURBO_READINESS_LIFECYCLE_OPEN ||
        slot->lifecycle == TURBO_READINESS_LIFECYCLE_CLOSING)
      snapshot.registered_count += 1u;
    if (slot->interest == TURBO_READINESS_INTEREST_ARMED)
      snapshot.armed_count += 1u;
    if (slot->delivery == TURBO_READINESS_DELIVERY_CALLBACK)
      snapshot.callbacks_inflight += 1u;
  }
  turbo_mutex_unlock(&impl->mutex);
  *stats = snapshot;
  return TURBO_OK;
}
