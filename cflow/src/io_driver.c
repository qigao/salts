#include "io_driver_internal.h"

#include <turbo/error_codes.h>

#include <string.h>

static int completion_callback_submit(void *self,
                                      cflow_io_actor *actor,
                                      cflow_io_request_id request_id,
                                      cflow_io_lease_id lease_id,
                                      void *operation_user) {
    cflow_io_driver *driver = (cflow_io_driver *)self;
    return driver->completion_callbacks.submit(
        driver->completion_callback_user, actor, request_id, lease_id,
        operation_user);
}

static int completion_callback_cancel(void *self,
                                      cflow_io_request_id request_id) {
    cflow_io_driver *driver = (cflow_io_driver *)self;
    return driver->completion_callbacks.cancel != NULL
               ? driver->completion_callbacks.cancel(
                     driver->completion_callback_user, request_id)
               : TURBO_OK;
}

static const cflow_io_driver_ops completion_callback_driver_ops = {
    completion_callback_submit,
    completion_callback_cancel
};

bool cflow_io_driver_init_adapter(cflow_io_driver *driver,
                                  cflow_io_driver_kind kind,
                                  const cflow_io_driver_ops *ops,
                                  void *self) {
    if (driver == NULL || ops == NULL || ops->submit == NULL ||
        ops->cancel == NULL || kind < CFLOW_IO_DRIVER_READINESS ||
        kind > CFLOW_IO_DRIVER_BLOCKING)
        return false;
    memset(driver, 0, sizeof(*driver));
    driver->ops = ops;
    driver->self = self;
    driver->kind = kind;
    return true;
}

bool cflow_io_driver_init_completion_callbacks(
    cflow_io_driver *driver,
    cflow_io_backend_ops backend,
    void *backend_user) {
    if (driver == NULL || backend.submit == NULL)
        return false;
    memset(driver, 0, sizeof(*driver));
    driver->ops = &completion_callback_driver_ops;
    driver->self = driver;
    driver->kind = CFLOW_IO_DRIVER_COMPLETION;
    driver->completion_callbacks = backend;
    driver->completion_callback_user = backend_user;
    return true;
}

cflow_io_driver_kind cflow_io_driver_kind_of(
    const cflow_io_driver *driver) {
    return driver != NULL && driver->ops != NULL
               ? driver->kind : CFLOW_IO_DRIVER_INVALID;
}

int cflow_io_driver_submit(cflow_io_driver *driver,
                           cflow_io_actor *actor,
                           cflow_io_request_id request_id,
                           cflow_io_lease_id lease_id,
                           void *operation_user) {
    if (driver == NULL || driver->ops == NULL ||
        driver->ops->submit == NULL)
        return TURBO_EINVAL;
    return driver->ops->submit(
        driver->self, actor, request_id, lease_id, operation_user);
}

int cflow_io_driver_cancel(cflow_io_driver *driver,
                           cflow_io_request_id request_id) {
    if (driver == NULL || driver->ops == NULL ||
        driver->ops->cancel == NULL)
        return TURBO_EINVAL;
    return driver->ops->cancel(driver->self, request_id);
}
