#ifndef TURBO_READINESS_H
#define TURBO_READINESS_H

#include <turbo/platform.h>

#include <stddef.h>
#include <stdint.h>

typedef struct turbo_readiness_reactor {
  void *impl;
} turbo_readiness_reactor;

typedef struct turbo_readiness_registration {
  void *impl;
} turbo_readiness_registration;

typedef uint32_t turbo_readiness_events;

enum {
  TURBO_READINESS_EVENT_READ = 1u << 0,
  TURBO_READINESS_EVENT_WRITE = 1u << 1,
  TURBO_READINESS_EVENT_ERROR = 1u << 2,
  TURBO_READINESS_EVENT_HANGUP = 1u << 3
};

typedef struct turbo_readiness_config {
  size_t registration_capacity;
  size_t event_batch_capacity;
} turbo_readiness_config;

typedef struct turbo_readiness_stats {
  size_t capacity;
  size_t registered_count;
  size_t armed_count;
  size_t callbacks_inflight;
  uint64_t rejected_full;
  uint64_t stale_events;
  uint64_t duplicate_events;
  uint64_t backend_errors;
} turbo_readiness_stats;

typedef void (*turbo_readiness_callback)(void *user, turbo_readiness_events events, int status);

/*
 * Reactors and registrations are zero-state handles. A successful register
 * borrows native_resource until turbo_readiness_close() returns TURBO_OK.
 * Registration handles are move-only: copying registration.impl is outside
 * the contract.
 *
 * Capacity is fixed at initialization. registration_capacity and
 * event_batch_capacity must be nonzero, the batch may not exceed capacity + 1,
 * and registration_capacity may not exceed UINT32_MAX - 1. The native factory
 * returns TURBO_ENOTSUP when no configured backend exists; it never falls back.
 */
TURBO_PLATFORM_C_API int turbo_readiness_reactor_init(turbo_readiness_reactor *reactor,
                                                      const turbo_readiness_config *config);

/*
 * Shutdown atomically closes admission and reserves one TURBO_ESHUTDOWN
 * delivery for every armed registration; ordinary readiness cannot overtake
 * that terminal delivery. Registrations remain owned by their handles and must
 * be closed before destroy. A failed shutdown preserves the reactor and may be
 * retried; destroy returns TURBO_EBUSY until backend shutdown succeeds.
 * Repeated shutdown after success returns TURBO_EALREADY.
 */
TURBO_PLATFORM_C_API int turbo_readiness_reactor_shutdown(turbo_readiness_reactor *reactor);
TURBO_PLATFORM_C_API int turbo_readiness_reactor_destroy(turbo_readiness_reactor *reactor);
TURBO_PLATFORM_C_API int turbo_readiness_register(turbo_readiness_reactor *reactor,
                                                  intptr_t native_resource,
                                                  turbo_readiness_registration *registration);

/*
 * Arm is one-shot. Callback delivery for this arm begins only after
 * turbo_readiness_arm() returns; backend events are serialized behind the arm
 * control operation. The backend event and user callback are dispatched
 * without a Platform lock held. status is TURBO_OK for readiness or an exact
 * negative terminal backend code; terminal delivery uses events == 0.
 */
TURBO_PLATFORM_C_API int turbo_readiness_arm(turbo_readiness_registration *registration,
                                             turbo_readiness_events events,
                                             turbo_readiness_callback callback, void *user);

/*
 * External unarm/close calls return only after an inflight callback is
 * quiescent. Unarm or close from inside that registration's callback fails
 * fast with TURBO_EBUSY. Close also returns TURBO_EBUSY while terminal
 * delivery is reserved. Only TURBO_OK consumes and clears the registration;
 * TURBO_EBUSY and backend errors preserve the handle and cleanup ownership so
 * the caller can retry after quiescence. While fatal or shutdown terminalization
 * is in progress, registration controls invoked by any callback on that reactor
 * fail fast with TURBO_EBUSY; external callers wait and then re-evaluate state.
 */
TURBO_PLATFORM_C_API int turbo_readiness_unarm(turbo_readiness_registration *registration);
TURBO_PLATFORM_C_API int turbo_readiness_close(turbo_readiness_registration *registration);
TURBO_PLATFORM_C_API int turbo_readiness_reactor_stats(turbo_readiness_reactor *reactor,
                                                       turbo_readiness_stats *stats);

#endif /* TURBO_READINESS_H */
