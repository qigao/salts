#ifndef CFLOW_IO_BLOCKING_DRIVER_H
#define CFLOW_IO_BLOCKING_DRIVER_H

#include <cflow/io_actor.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_io_blocking_driver {
    void *impl;
} cflow_io_blocking_driver;

/**
 * Executes one blocking operation and writes its terminal result.
 *
 * The callback borrows user and out for its duration. TURBO_OK requires a
 * valid completion in out; any nonzero status becomes a FAILED completion.
 * The callback may block and runs on one of the driver's private workers.
 */
typedef int (*cflow_io_blocking_execute_fn)(
    void *user, cflow_io_completion *out);

/**
 * Borrowed descriptor. Pass its address as cflow_io_operation.user; the Actor
 * retains that operation and its release callback until acknowledgement.
 */
typedef struct cflow_io_blocking_job {
    cflow_io_blocking_execute_fn execute;
    void *user;
} cflow_io_blocking_job;

typedef struct cflow_io_blocking_driver_config {
    size_t workers;
    size_t capacity;
} cflow_io_blocking_driver_config;

typedef enum cflow_io_blocking_driver_lifecycle {
    CFLOW_IO_BLOCKING_DRIVER_OPEN = 0,
    CFLOW_IO_BLOCKING_DRIVER_CLOSING,
    CFLOW_IO_BLOCKING_DRIVER_CLOSED
} cflow_io_blocking_driver_lifecycle;

typedef struct cflow_io_blocking_driver_stats {
    size_t capacity;
    size_t active;
    size_t queued;
    size_t running;
    uint64_t accepted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t rejected_closed;
    uint64_t publication_errors;
    cflow_io_blocking_driver_lifecycle lifecycle;
} cflow_io_blocking_driver_stats;

/**
 * Initializes an explicit bounded blocking driver with private worker threads.
 * Both workers and capacity must be nonzero. Storage is fixed after init.
 * Returns TURBO_OK, TURBO_EINVAL, or TURBO_ENOMEM.
 */
int cflow_io_blocking_driver_init(
    cflow_io_blocking_driver *driver,
    const cflow_io_blocking_driver_config *config);

/**
 * Exposes the driver as an Actor backend without transferring ownership.
 * driver must outlive the Actor and remain alive through Actor destruction.
 */
bool cflow_io_blocking_driver_as_backend(
    cflow_io_blocking_driver *driver,
    cflow_io_backend_ops *out_ops,
    void **out_user);

/**
 * Stops admission and cancels tasks that have not started. Running callbacks
 * are not forcefully interrupted and publish their actual terminal result.
 * Returns TURBO_OK, TURBO_EALREADY, or TURBO_EINVAL.
 */
int cflow_io_blocking_driver_close(cflow_io_blocking_driver *driver);

/** Copies a consistent bounded-driver snapshot. */
bool cflow_io_blocking_driver_get_stats(
    const cflow_io_blocking_driver *driver,
    cflow_io_blocking_driver_stats *out);

/**
 * Destroys a closed, quiescent driver. TURBO_EBUSY leaves ownership unchanged.
 * Close and destroy the Actor before destroying its borrowed driver.
 */
int cflow_io_blocking_driver_destroy(cflow_io_blocking_driver *driver);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_BLOCKING_DRIVER_H */
