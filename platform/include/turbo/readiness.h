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
  uintptr_t _admission;
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

typedef enum turbo_readiness_action {
  TURBO_READINESS_COMPLETE = 0,
  TURBO_READINESS_REARM = 1
} turbo_readiness_action;

typedef struct turbo_readiness_callback_result {
  turbo_readiness_action action;
  turbo_readiness_events interests;
} turbo_readiness_callback_result;

typedef turbo_readiness_callback_result (*turbo_readiness_continuation)(
    void *user, turbo_readiness_events events, int status);

/*
 * Reactors and registrations are zero-state handles. A successful register
 * borrows native_resource until turbo_readiness_close() returns TURBO_OK.
 * Registration handles are move-only: copying the complete handle is allowed
 * only at a quiescent ownership-transfer boundary. Controls on one handle are
 * concurrency-safe; register, close, or reuse of that handle must not race its
 * ownership transfer. Reactor destroy requires every registration handle and
 * every public registration API call associated with the reactor to be
 * quiescent.
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
 * Shutdown invoked from any callback owned by this reactor returns
 * TURBO_EBUSY without committing a state transition. Repeated shutdown after
 * success returns TURBO_EALREADY.
 */
TURBO_PLATFORM_C_API int turbo_readiness_reactor_shutdown(turbo_readiness_reactor *reactor);
TURBO_PLATFORM_C_API int turbo_readiness_reactor_destroy(turbo_readiness_reactor *reactor);
TURBO_PLATFORM_C_API int turbo_readiness_register(turbo_readiness_reactor *reactor,
                                                  intptr_t native_resource,
                                                  turbo_readiness_registration *registration);

/*
 * Arm is one-shot. callback and user must already be valid before this call.
 * Backend events are serialized behind the arm control operation; delivery is
 * released after its commit clears the control gate, and may race the
 * function's final return and the caller's observation of that return. Keep
 * callback and user valid until callback completion or successful unarm/close.
 * An external arm of the same registration waits for an inflight callback to
 * return, then re-evaluates terminal, admission, generation, and slot state.
 * Arm from inside that registration's callback fails fast with TURBO_EBUSY.
 * The backend event and user callback are dispatched without a Platform lock
 * held. status is TURBO_OK for readiness or an exact negative terminal backend
 * code; terminal delivery uses events == 0.
 */
TURBO_PLATFORM_C_API int turbo_readiness_arm(turbo_readiness_registration *registration,
                                             turbo_readiness_events events,
                                             turbo_readiness_callback callback, void *user);

/*
 * Continuation arm is the additive persistent-reactor form of the one-shot
 * contract. For ordinary readiness, the callback returns COMPLETE or REARM
 * with the next nonzero interest set. Platform serializes and commits REARM
 * only after the callback returns; callers must not invoke registration
 * controls recursively from the callback. Terminal callbacks use events == 0,
 * ignore the returned action, and never rearm. An invalid action or invalid
 * REARM interests completes the arm and makes dispatch return TURBO_EINVAL.
 * If the backend rejects a continuation rearm, Platform invokes the same
 * continuation exactly once more with events == 0 and the backend error.
 *
 * @param registration Open registration owned by the caller.
 * @param events Initial nonzero interest set.
 * @param continuation Bounded nonblocking callback kept valid through its
 *        terminal invocation or successful unarm/close.
 * @param user Borrowed callback context with the same lifetime as continuation.
 * @return TURBO_OK after the initial arm commits; TURBO_EINVAL for invalid
 *         arguments; TURBO_EALREADY if already armed; TURBO_EBUSY for callback
 *         reentry or conflicting control; TURBO_ESHUTDOWN after admission
 *         closes; otherwise the exact initial backend arm error.
 *
 * A continuing callback returns (turbo_readiness_callback_result){
 *     work_remains ? TURBO_READINESS_REARM : TURBO_READINESS_COMPLETE,
 *     work_remains ? TURBO_READINESS_EVENT_READ : 0u};
 */
TURBO_PLATFORM_C_API int turbo_readiness_arm_continuation(
    turbo_readiness_registration *registration,
    turbo_readiness_events events,
    turbo_readiness_continuation continuation, void *user);

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
