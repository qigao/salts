#ifndef CFLOW_READINESS_H
#define CFLOW_READINESS_H

#include <cflow/sources.h>
#include <turbo/readiness.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Move-only external cleanup owner for a reactor-backed Source. Initialize to
 * zero and do not copy impl. The caller must keep the owner alive until the
 * Source has been destroyed, then call cflow_reactor_source_owner_close().
 */
typedef struct cflow_reactor_source_owner {
    void *impl;
} cflow_reactor_source_owner;

/*
 * Adapt an already registered Platform resource to the CFlow WAIT protocol.
 * read remains the only source of values and terminal data semantics.
 *
 * out and owner are zero-state outputs. Success moves registration into shared
 * Source/owner state and clears the caller registration. Any failure clears
 * out and owner while leaving registration and user ownership with the caller.
 * Only trivial-copy, trivial-destroy value types are admitted.
 *
 * Source cancellation is terminal: it quiescently closes the registration,
 * then invokes close(user) once. Source destroy releases its state reference
 * even if Platform close fails; owner keeps registration/user reachable.
 *
 * owner is not thread-safe and must not be moved or closed concurrently.
 * owner_close returns TURBO_EBUSY without side effects while the Source still
 * exists. After Source destroy, it returns the exact Platform close error and
 * retains owner for retry. TURBO_OK clears owner and releases its final ref;
 * callers must close owner even when Source cleanup already succeeded.
 */
int cflow_source_from_reactor_registration(
    cflow_source *out,
    cflow_reactor_source_owner *owner,
    turbo_readiness_registration *registration,
    turbo_readiness_events events,
    const char *name,
    const cmeta_type_desc *type,
    cflow_read_fn read,
    cflow_resource_close_fn close,
    void *user);

int cflow_reactor_source_owner_close(cflow_reactor_source_owner *owner);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_READINESS_H */
