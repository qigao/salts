#include <cflow/reactive.h>
#include <cflow/lower.h>
#include <cflow/publishers.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct obs_state {
    double values[8];
    atomic_size_t count;
    atomic_int done;
    atomic_int errors;
    atomic_int callbacks;
    atomic_int overlap;
} obs_state;

static bool on_value(void *user, const cmeta_type_desc *type, const void *value) {
    obs_state *s = (obs_state *)user;
    if (!cmeta_type_equal(type, &cmeta_type_double)) return false;
    if (atomic_fetch_add(&s->callbacks, 1) != 0) atomic_fetch_add(&s->overlap, 1);
    size_t i = atomic_fetch_add(&s->count, 1);
    if (i < 8) s->values[i] = *(const double *)value;
    atomic_fetch_sub(&s->callbacks, 1);
    return true;
}
static void on_error(void *user, const char *message) {
    (void)message;
    atomic_fetch_add(&((obs_state *)user)->errors, 1);
}
static void on_done(void *user) { atomic_store(&((obs_state *)user)->done, 1); }
static int near(double a, double b) { return fabs(a - b) < 1e-9; }

static void build_zip(cflow_stream *left, cflow_stream *right) {
    cflow_stream_init(left, &cmeta_type_int);
    cflow_stream_init(right, &cmeta_type_int);
    left->map(left, square);
    right->map(right, as_double);
    left->zip(left, right, merge_long_double);
}

int main(void) {
    cflow_stream left, right;
    build_zip(&left, &right);
    if (!cflow_stream_ok(&left) || !cflow_stream_ok(&right)) return 1;

    /* Deterministic scheduler: parent -> child -> parent handoff is visible. */
    cflow_scheduler loop = {0};
    cflow_publisher src = {0};
    cflow_subscription run = {0};
    cflow_graph exec = {0}; exec.root = CMETA_INVALID_ID;
    int one = 2;
    obs_state st = {0};
    cflow_subscriber_callbacks obs_cb = { on_value, on_error, on_done, &st };
    cflow_subscriber obs = cflow_subscriber_from_callbacks(&obs_cb);
    if (!cflow_graph_normalize(&exec, &left.graph)) return 37;
    if (!cflow_scheduler_test_init(&loop)) return 2;
    if (!cflow_publisher_from_array(&src, &cmeta_type_int, &one, 1)) return 3;
    if (!cflow_subscribe(&run, &exec, &src, &loop, &obs)) return 4;
    if (!cflow_subscription_request(&run, 1)) return 5;

    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (atomic_load(&st.count) != 1 || !near(st.values[0], 6.25) ||
        atomic_load(&st.errors) || !atomic_load(&st.done)) return 11;
    printf("subflow ZIP: parent -> child -> parent scheduler handoff, value=%.2f\n", st.values[0]);
    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&loop);
    cflow_graph_destroy(&exec); exec = (cflow_graph){0}; exec.root = CMETA_INVALID_ID;
    cflow_stream_destroy(&left); cflow_stream_destroy(&right);

    /* Cancelling while the parent waits propagates into the child run. */
    build_zip(&left, &right);
    if (!cflow_graph_normalize(&exec, &left.graph)) return 38;
    memset(&st, 0, sizeof(st));
    if (!cflow_scheduler_test_init(&loop)) return 19;
    if (!cflow_publisher_from_array(&src, &cmeta_type_int, &one, 1)) return 20;
    if (!cflow_subscribe(&run, &exec, &src, &loop, &obs)) return 21;
    if (!cflow_subscription_request(&run, 1)) return 22;
    if (!cflow_scheduler_run_one(&loop)) return 23; /* waiting on child */
    cflow_subscription_cancel(&run);
    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (!cflow_subscription_is_cancelled(&run) || atomic_load(&st.count) != 0 ||
        atomic_load(&st.done) || atomic_load(&st.errors)) return 24;
    printf("subflow cancellation: parent cancel propagates into child\n");
    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&loop);
    cflow_graph_destroy(&exec); exec = (cflow_graph){0}; exec.root = CMETA_INVALID_ID;
    cflow_stream_destroy(&left); cflow_stream_destroy(&right);

    /* Child graph capture is immutable: later branch mutation is isolated. */
    build_zip(&left, &right);
    right.map(&right, to_int);
    if (!cflow_graph_normalize(&exec, &left.graph)) return 39;
    memset(&st, 0, sizeof(st));
    if (!cflow_scheduler_test_init(&loop)) return 25;
    if (!cflow_publisher_from_array(&src, &cmeta_type_int, &one, 1)) return 26;
    if (!cflow_subscribe(&run, &exec, &src, &loop, &obs)) return 27;
    if (!cflow_subscription_request(&run, 1)) return 28;
    (void)cflow_scheduler_run_until_idle(&loop, 0);
    if (cflow_subscription_error(&run) || atomic_load(&st.count) != 1 ||
        atomic_load(&st.errors) || !atomic_load(&st.done) || !near(st.values[0], 6.25)) return 29;
    printf("subgraph snapshot: later branch mutation does not affect captured IR\n");
    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&loop);
    cflow_graph_destroy(&exec); exec = (cflow_graph){0}; exec.root = CMETA_INVALID_ID;
    cflow_stream_destroy(&left); cflow_stream_destroy(&right);

    /* Same subflow composition on a concurrent scheduler. */
    build_zip(&left, &right);
    if (!cflow_graph_normalize(&exec, &left.graph)) return 40;
    cflow_scheduler workers = {0};
    int many[] = {1,2,3};
    memset(&st, 0, sizeof(st));
    if (!cflow_scheduler_worker_init(&workers, 4)) return 12;
    if (!cflow_publisher_from_array(&src, &cmeta_type_int, many, 3)) return 13;
    if (!cflow_subscribe(&run, &exec, &src, &workers, &obs)) return 14;
    if (!cflow_subscription_request(&run, 3)) return 15;
    if (!cflow_scheduler_wait_idle(&workers)) return 16;
    if (atomic_load(&st.count) != 3 || atomic_load(&st.errors) ||
        atomic_load(&st.overlap) || !atomic_load(&st.done)) return 17;
    if (!near(st.values[0],2.25) || !near(st.values[1],6.25) || !near(st.values[2],12.25)) return 18;
    printf("subflow ZIP: concurrent scheduler, serialized outputs %.2f %.2f %.2f\n",
           st.values[0], st.values[1], st.values[2]);
    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&workers);
    cflow_graph_destroy(&exec); exec = (cflow_graph){0}; exec.root = CMETA_INVALID_ID;
    cflow_stream_destroy(&left); cflow_stream_destroy(&right);

    /* Subgraphs may recursively contain subgraphs. */
    cflow_stream outer, child, grandchild;
    cflow_stream_init(&outer, &cmeta_type_int);
    cflow_stream_init(&child, &cmeta_type_int);
    cflow_stream_init(&grandchild, &cmeta_type_int);
    outer.map(&outer, square);
    child.map(&child, square);
    grandchild.map(&grandchild, as_double);
    child.zip(&child, &grandchild, merge_long_double);
    outer.zip(&outer, &child, merge_long_double);
    if (!cflow_graph_normalize(&exec, &outer.graph)) return 41;
    int nested_input[] = {1,2,3};
    memset(&st, 0, sizeof(st));
    if (!cflow_scheduler_worker_init(&workers, 4)) return 30;
    if (!cflow_publisher_from_array(&src, &cmeta_type_int, nested_input, 3)) return 31;
    if (!cflow_subscribe(&run, &exec, &src, &workers, &obs)) return 32;
    if (!cflow_subscription_request(&run, 3)) return 33;
    if (!cflow_scheduler_wait_idle(&workers)) return 34;
    if (atomic_load(&st.count) != 3 || atomic_load(&st.errors) ||
        atomic_load(&st.overlap) || !atomic_load(&st.done)) return 35;
    if (!near(st.values[0],3.25) || !near(st.values[1],10.25) ||
        !near(st.values[2],21.25)) return 36;
    printf("nested subflows: parent -> subflow -> nested subflow composition %.2f %.2f %.2f\n",
           st.values[0], st.values[1], st.values[2]);
    cflow_subscription_close(&run);
    cflow_scheduler_destroy(&workers);
    cflow_graph_destroy(&exec);
    cflow_stream_destroy(&outer);
    cflow_stream_destroy(&child);
    cflow_stream_destroy(&grandchild);
    return 0;
}
