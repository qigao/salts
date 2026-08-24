#ifndef TURBO_READINESS_INTERNAL_H
#define TURBO_READINESS_INTERNAL_H

#include <turbo/readiness.h>

typedef struct turbo_readiness_backend_ops {
  /* A failing control hook must leave its native effect uncommitted and retryable.
   * TURBO_OK commits the requested effect.  shutdown returning TURBO_OK additionally
   * guarantees that backend callbacks and native access to the reactor are quiescent. */
  int (*register_resource)(void *user, intptr_t native_resource, uint64_t token);
  int (*arm)(void *user, uint64_t token, uint64_t arm_token,
             turbo_readiness_events events);
  int (*unarm)(void *user, uint64_t token);
  int (*close)(void *user, uint64_t token);
  int (*shutdown)(void *user);
  void (*destroy)(void *user);
} turbo_readiness_backend_ops;

typedef struct turbo_readiness_backend_event {
  uint64_t token;
  turbo_readiness_events events;
  int status;
} turbo_readiness_backend_event;

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

#if defined(TURBO_ENABLE_EPOLL_READINESS)
int turbo_readiness_epoll_init(turbo_readiness_reactor *reactor,
                               const turbo_readiness_config *config);
#endif

#endif /* TURBO_READINESS_INTERNAL_H */
