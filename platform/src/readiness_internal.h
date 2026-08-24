#ifndef TURBO_READINESS_INTERNAL_H
#define TURBO_READINESS_INTERNAL_H

#include <turbo/error_codes.h>
#include <turbo/readiness.h>

#include <errno.h>

typedef struct turbo_readiness_generation_step {
  uint32_t previous;
  uint32_t next;
} turbo_readiness_generation_step;

static inline int turbo_readiness_generation_available(uint32_t generation) {
  return generation != UINT32_MAX;
}

static inline int turbo_readiness_generation_prepare(
    uint32_t generation, turbo_readiness_generation_step *step) {
  if (step == NULL) return TURBO_EINVAL;
  if (!turbo_readiness_generation_available(generation)) return -EOVERFLOW;
  step->previous = generation;
  step->next = generation + 1u;
  return TURBO_OK;
}

static inline uint32_t turbo_readiness_generation_commit(
    const turbo_readiness_generation_step *step) {
  return step->next;
}

static inline uint32_t turbo_readiness_generation_rollback(
    const turbo_readiness_generation_step *step) {
  return step->previous;
}

typedef struct turbo_readiness_backend_ops {
  /* A failing register/arm/unarm/close hook leaves its native effect uncommitted and
   * retryable.  shutdown may make monotonic partial progress on failure, but retains
   * backend ownership for a later retry.  Only shutdown TURBO_OK proves backend callbacks,
   * its thread, and all native reactor access are quiescent. */
  int (*register_resource)(void *user, intptr_t native_resource, uint64_t token);
  /* The arm hook must not call turbo_readiness_backend_dispatch() or
   * turbo_readiness_backend_dispatch_generation() inline on its own execution
   * thread, nor wait for a dispatch it triggered: dispatch waits for this arm's
   * control gate and such a hook would wait for itself. The hook may notify an
   * independent reactor/producer thread; its queued dispatch is released after
   * the hook returns and the state engine commits or rolls back the arm control
   * operation and clears that gate. */
  int (*arm)(void *user, uint64_t token, uint64_t arm_token,
             turbo_readiness_events events);
  int (*unarm)(void *user, uint64_t token);
  int (*close)(void *user, uint64_t token);
  int (*shutdown)(void *user);
  void (*destroy)(void *user);
} turbo_readiness_backend_ops;

int turbo_readiness_reactor_init_backend(turbo_readiness_reactor *reactor,
                                         const turbo_readiness_config *config,
                                         const turbo_readiness_backend_ops *backend_ops,
                                         void *backend_user);
int turbo_readiness_backend_dispatch(turbo_readiness_reactor *reactor, uint64_t token,
                                     turbo_readiness_events events, int status);
int turbo_readiness_backend_dispatch_generation(turbo_readiness_reactor *reactor,
                                                uint64_t token, uint64_t arm_token,
                                                turbo_readiness_events events, int status);
int turbo_readiness_backend_fail(turbo_readiness_reactor *reactor, int status);
int turbo_readiness_backend_wait_admission_closed(turbo_readiness_reactor *reactor);

#if defined(__linux__)
uint32_t turbo_readiness_epoll_interest_events(turbo_readiness_events events);
#endif

#if defined(TURBO_ENABLE_EPOLL_READINESS)
int turbo_readiness_epoll_init(turbo_readiness_reactor *reactor,
                               const turbo_readiness_config *config);
#endif

#endif /* TURBO_READINESS_INTERNAL_H */
