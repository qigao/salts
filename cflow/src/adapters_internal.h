#ifndef CFLOW_ADAPTERS_INTERNAL_H
#define CFLOW_ADAPTERS_INTERNAL_H

#include <cflow/lower.h>
#include <cflow/sources.h>

/* Prepare the executable Graph for a synchronous adapter that owns source.
 * On success, ownership remains with the caller until Run admission. On any
 * failure after entry, source is destroyed exactly once and restored to zero.
 * normalized must be empty and remains owned by the caller. */
bool cflow_adapter_prepare_owned_source_graph(
    cflow_graph *normalized,
    const cflow_graph *graph,
    cflow_source *source,
    const cflow_graph **out_graph);

#endif /* CFLOW_ADAPTERS_INTERNAL_H */
