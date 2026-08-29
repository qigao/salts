#ifndef CFLOW_IO_DRIVER_INTERNAL_H
#define CFLOW_IO_DRIVER_INTERNAL_H

#include <cflow/io_actor.h>

#include <stdbool.h>

typedef enum cflow_io_driver_kind {
    CFLOW_IO_DRIVER_INVALID = 0,
    CFLOW_IO_DRIVER_READINESS,
    CFLOW_IO_DRIVER_COMPLETION,
    CFLOW_IO_DRIVER_BLOCKING
} cflow_io_driver_kind;

typedef struct cflow_io_driver cflow_io_driver;

typedef struct cflow_io_driver_ops {
    int (*submit)(void *self,
                  cflow_io_actor *actor,
                  cflow_io_request_id request_id,
                  cflow_io_lease_id lease_id,
                  void *operation_user);
    int (*cancel)(void *self, cflow_io_request_id request_id);
} cflow_io_driver_ops;

struct cflow_io_driver {
    const cflow_io_driver_ops *ops;
    void *self;
    cflow_io_driver_kind kind;
    cflow_io_backend_ops completion_callbacks;
    void *completion_callback_user;
};

/* Internal injection point for model-specific adapters. The vtable and self
 * object must outlive the driver; callbacks retain their exact status codes. */
bool cflow_io_driver_init_adapter(cflow_io_driver *driver,
                                  cflow_io_driver_kind kind,
                                  const cflow_io_driver_ops *ops,
                                  void *self);

/* The public Actor backend contract is completion-shaped even when its native
 * implementation uses readiness internally: submit/cancel produce exactly
 * one terminal callback. This adapter preserves that boundary explicitly. */
bool cflow_io_driver_init_completion_callbacks(
    cflow_io_driver *driver,
    cflow_io_backend_ops backend,
    void *backend_user);

cflow_io_driver_kind cflow_io_driver_kind_of(
    const cflow_io_driver *driver);

int cflow_io_driver_submit(cflow_io_driver *driver,
                           cflow_io_actor *actor,
                           cflow_io_request_id request_id,
                           cflow_io_lease_id lease_id,
                           void *operation_user);

int cflow_io_driver_cancel(cflow_io_driver *driver,
                           cflow_io_request_id request_id);

#endif /* CFLOW_IO_DRIVER_INTERNAL_H */
