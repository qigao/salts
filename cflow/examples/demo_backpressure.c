#include <cflow/reactive.h>
#include <cflow/publishers.h>
#include <cflow/stream.h>

#include <stdio.h>

#define N 10000u

typed(flatMap, value, cmeta_gen_status, generate_many,
      (int x, long *out, size_t *cursor)) {
    if (*cursor >= N) return CMETA_GEN_DONE;
    *out = (long)x * 100000L + (long)*cursor;
    ++*cursor;
    return *cursor == N ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

typedef struct state { size_t count; long first, last; bool done; } state;
static bool on_value(void *user, const cmeta_type_desc *type, const void *value) {
    state *s = (state *)user;
    if (!cmeta_type_equal(type, &cmeta_type_long)) return false;
    long v = *(const long *)value;
    if (!s->count) s->first = v;
    s->last = v; ++s->count; return true;
}
static void on_error(void *user, const char *m) { (void)user; (void)m; }
static void on_done(void *user) { ((state *)user)->done = true; }

int main(void) {
    cflow_stream s; cflow_stream_init(&s, &cmeta_type_int); s.flatMap(&s, generate_many);
    int input = 7; cflow_publisher source; cflow_publisher_from_array(&source, &cmeta_type_int, &input, 1);
    cflow_scheduler loop; cflow_scheduler_test_init(&loop);
    state st = {0}; cflow_subscriber_callbacks obs_cb = { on_value, on_error, on_done, &st };
    cflow_subscriber obs = cflow_subscriber_from_callbacks(&obs_cb);
    cflow_subscription sub; if (!cflow_subscribe(&sub, &s.graph, &source, &loop, &obs)) return 1;
    if (!cflow_subscription_request(&sub, 1)) return 2;
    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (st.count != 1 || st.done) return 3;
    if (!cflow_subscription_request(&sub, 17)) return 4;
    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (st.count != 18 || st.done) return 5;
    if (!cflow_subscription_request(&sub, N - 18)) return 6;
    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (st.count != N || !st.done || st.first != 700000L || st.last != 709999L) { fprintf(stderr,"count=%zu done=%d first=%ld last=%ld err=%s demand=%zu\n", st.count, st.done, st.first, st.last, cflow_subscription_error(&sub), cflow_subscription_outstanding_demand(&sub.run)); return 7; }
    printf("resumable backpressure: %zu outputs, no sink pending queue\n", st.count);
    cflow_subscription_close(&sub); cflow_scheduler_destroy(&loop); cflow_stream_destroy(&s); return 0;
}
