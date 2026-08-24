#ifndef CFLOW_READINESS_INTERNAL_H
#define CFLOW_READINESS_INTERNAL_H

#include <cflow/readiness.h>

/* Test-only observer; the owner and Source must remain live for the call. */
turbo_readiness_registration *
cflow_reactor_source_owner_observe_registration(
    cflow_reactor_source_owner *owner);

#endif /* CFLOW_READINESS_INTERNAL_H */
