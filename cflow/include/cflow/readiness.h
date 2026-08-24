#ifndef CFLOW_READINESS_H
#define CFLOW_READINESS_H

#include <cflow/sources.h>
#include <turbo/readiness.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Adapt an already registered Platform resource to the CFlow WAIT protocol.
 * read remains the only source of values and terminal data semantics.
 *
 * Success moves registration into the Source and clears the caller handle.
 * Failure leaves both registration and user ownership with the caller.
 * Only trivial-copy, trivial-destroy value types are admitted.
 *
 * Source cancellation is terminal: it quiescently closes the registration,
 * then invokes close(user) once. A retryable Platform close failure preserves
 * the registration and borrowed user for a later destroy retry.
 */
int cflow_source_from_reactor_registration(
    cflow_source *out,
    turbo_readiness_registration *registration,
    turbo_readiness_events events,
    const char *name,
    const cmeta_type_desc *type,
    cflow_read_fn read,
    cflow_resource_close_fn close,
    void *user);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_READINESS_H */
