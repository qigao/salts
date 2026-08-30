#ifndef TURBO_NATIVE_IO_READINESS_H
#define TURBO_NATIVE_IO_READINESS_H

#include "native_io_internal.h"

enum { TURBO_IO_READY_READ = 1u, TURBO_IO_READY_WRITE = 2u, TURBO_IO_READY_ERROR = 4u };

typedef struct turbo_io_ready_event {
  uint64_t token;
  uint32_t interests;
  uint32_t native_status;
} turbo_io_ready_event;

typedef struct turbo_io_readiness_driver_ops {
  int (*init)(void *state, size_t batch_capacity);
  int (*update)(void *state, int fd, uint64_t token, uint32_t old_interests,
                uint32_t new_interests);
  int (*wait)(void *state, turbo_io_ready_event *events, size_t event_capacity, uint32_t timeout_ms,
              size_t *out_count);
  void (*destroy)(void *state);
} turbo_io_readiness_driver_ops;

int turbo_io_readiness_backend_init(turbo_io_backend *backend,
                                    const turbo_io_backend_config *config,
                                    const turbo_io_readiness_driver_ops *driver_ops,
                                    size_t driver_state_size);

#endif /* TURBO_NATIVE_IO_READINESS_H */
