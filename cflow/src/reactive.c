#include <cflow/reactive.h>

#include <string.h>

bool cflow_subscribe(cflow_subscription *sub,
                     const cflow_graph *graph,
                     cflow_source *source,
                     cflow_scheduler *scheduler,
                     const cflow_sink *sink) {
    if (!sub) return false;
    memset(sub, 0, sizeof(*sub));
    return cflow_run_open(&sub->run, graph, source, scheduler, sink);
}
bool cflow_subscription_request(cflow_subscription *sub, size_t n) {
    return sub && cflow_run_request(&sub->run, n);
}
void cflow_subscription_cancel(cflow_subscription *sub) {
    if (sub) cflow_run_cancel(&sub->run);
}
void cflow_subscription_close(cflow_subscription *sub) {
    if (sub) cflow_run_close(&sub->run);
}
bool cflow_subscription_is_done(const cflow_subscription *sub) {
    return sub && cflow_run_is_done(&sub->run);
}
const char *cflow_subscription_error(const cflow_subscription *sub) {
    return sub ? cflow_run_error(&sub->run) : "subscription is null";
}
