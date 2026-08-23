#ifndef CFLOW_GRAPH_INTERNAL_H
#define CFLOW_GRAPH_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

/* Reserve a process-unique nonzero Graph mutation token. Tokens are skipped
 * when a later allocation fails, but are never reused within the process. */
bool cflow_graph_version_acquire(uint64_t *version);

#endif
