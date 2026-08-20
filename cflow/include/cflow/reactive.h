#ifndef CFLOW_REACTIVE_H
#define CFLOW_REACTIVE_H

#include <cflow/runtime.h>

/* Reactive is intentionally a façade, not an execution engine. */
typedef struct cflow_subscription {
    cflow_run run;
} cflow_subscription;

bool cflow_subscribe(cflow_subscription *sub,
                     const cflow_graph *graph,
                     cflow_source *source,
                     cflow_scheduler *scheduler,
                     const cflow_sink *sink);
bool cflow_subscription_request(cflow_subscription *sub, size_t n);
void cflow_subscription_cancel(cflow_subscription *sub);
void cflow_subscription_close(cflow_subscription *sub);
bool cflow_subscription_is_done(const cflow_subscription *sub);
const char *cflow_subscription_error(const cflow_subscription *sub);

#endif
