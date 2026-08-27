#ifndef CFLOW_RUNTIME_INTERNAL_H
#define CFLOW_RUNTIME_INTERNAL_H

#include <cflow/runtime.h>

#include <stdbool.h>

/* True only while the calling thread is inside this Run's pump. Control
 * facades use it to reject self-wait and deferred self-destruction. */
bool cflow_run_active_on_current_thread(const cflow_run *run);

#endif /* CFLOW_RUNTIME_INTERNAL_H */
