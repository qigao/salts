#ifndef CFLOW_RUNTIME_INTERNAL_H
#define CFLOW_RUNTIME_INTERNAL_H

#include <cflow/reactive.h>

#include <stdbool.h>

/* True only while the calling thread is inside this Run's pump. Control
 * facades use it to reject self-wait and deferred self-destruction. */
bool cflow_subscription_active_on_current_thread(const cflow_subscription *run);

#endif /* CFLOW_RUNTIME_INTERNAL_H */
