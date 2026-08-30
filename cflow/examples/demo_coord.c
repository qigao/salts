#include <cflow/coord.h>
#include <cflow/publishers.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct wake_flag { bool fired; } wake_flag;
static void set_wake(void *user) { ((wake_flag *)user)->fired = true; }

typedef struct script_state {
    const cmeta_type_desc *type;
    unsigned char *values;
    size_t count;
    size_t index;
} script_state;

static cflow_step script_resume(void *state, cflow_publish_context *ctx, void *out) {
    (void)ctx;
    script_state *s = (script_state *)state;
    if (!s || !out) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "script invalid" };
    if (s->index >= s->count) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    memcpy(out, s->values + s->index * s->type->size, s->type->size);
    ++s->index;
    return (cflow_step){ s->index == s->count ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE,
                         {0}, NULL };
}

static void script_destroy(void *state) {
    script_state *s = (script_state *)state;
    if (!s) return;
    free(s->values);
    free(s);
}

static const cflow_resumable_ops script_ops = { script_resume, NULL, script_destroy };

static bool script_ints(cflow_resumable *out, const int *values, size_t count) {
    script_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->type = &cmeta_type_int;
    s->count = count;
    s->values = malloc(sizeof(*values) * count);
    if (!s->values) { free(s); return false; }
    memcpy(s->values, values, sizeof(*values) * count);
    *out = (cflow_resumable){ "script-int", &cmeta_type_int, &script_ops, s };
    return true;
}

static void destroy_machine(cflow_resumable *m) {
    if (m && m->ops && m->ops->destroy) m->ops->destroy(m->state);
    if (m) memset(m, 0, sizeof(*m));
}

int main(void) {
    cflow_scheduler loop;
    assert(cflow_scheduler_test_init(&loop));
    cflow_publish_context ctx = { &loop };

    /* ALL: heterogeneous children retain their own typed slots. */
    long lv = 7;
    double dv = 2.5;
    cflow_resumable all_children[2] = {{0}, {0}}, all = {0};
    assert(cflow_resumable_from_value(&all_children[0], &cmeta_type_long, &lv));
    assert(cflow_resumable_from_value(&all_children[1], &cmeta_type_double, &dv));
    assert(cflow_resumable_from_coordination(&all, CFLOW_COORD_ALL, all_children, 2));
    cflow_coord_event event = {0};
    cflow_step step = all.ops->resume(all.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE_AND_DONE && event.child_index == SIZE_MAX);
    const cmeta_type_desc *t0 = NULL, *t1 = NULL;
    const void *v0 = NULL, *v1 = NULL;
    assert(cflow_coord_value(&all, 0, &t0, &v0));
    assert(cflow_coord_value(&all, 1, &t1, &v1));
    assert(t0 == &cmeta_type_long && *(const long *)v0 == 7);
    assert(t1 == &cmeta_type_double && *(const double *)v1 == 2.5);
    destroy_machine(&all);
    puts("coord ALL: heterogeneous typed barrier");

    /* ANY: first available child wins and the rest are cancelled. */
    int a = 11, b = 22;
    cflow_resumable any_children[2] = {{0}, {0}}, any = {0};
    assert(cflow_resumable_from_value(&any_children[0], &cmeta_type_int, &a));
    assert(cflow_resumable_from_value(&any_children[1], &cmeta_type_int, &b));
    assert(cflow_resumable_from_coordination(&any, CFLOW_COORD_ANY, any_children, 2));
    step = any.ops->resume(any.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE_AND_DONE && event.child_index == 0);
    assert(cflow_coord_value(&any, 0, NULL, &v0) && *(const int *)v0 == 11);
    destroy_machine(&any);
    puts("coord ANY: first value wins");

    /* Aggregate WAIT: two independently scheduled children arm through one
     * coordinator waitable. The earlier timer wins ANY. */
    cflow_publisher timers[2];
    memset(timers, 0, sizeof timers);
    cflow_resumable wait_children[2] = {{0}, {0}}, wait_any = {0};
    assert(cflow_publisher_from_timer(&timers[0], 1, 10));
    assert(cflow_publisher_from_timer(&timers[1], 1, 5));
    assert(cflow_resumable_from_publisher(&wait_children[0], &timers[0]));
    assert(cflow_resumable_from_publisher(&wait_children[1], &timers[1]));
    assert(cflow_resumable_from_coordination(&wait_any, CFLOW_COORD_ANY, wait_children, 2));
    step = wait_any.ops->resume(wait_any.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_WAIT && cflow_waitable_valid(&step.waitable));
    wake_flag wf = { false };
    assert(cflow_waitable_arm(&step.waitable, (cflow_waker){ set_wake, &wf }));
    assert(cflow_scheduler_advance(&loop, 4) == 0 && !wf.fired);
    assert(cflow_scheduler_advance(&loop, 1) == 1 && wf.fired);
    step = wait_any.ops->resume(wait_any.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE_AND_DONE && event.child_index == 1);
    assert(cflow_coord_value(&wait_any, 1, NULL, &v1) && *(const size_t *)v1 == 0);
    destroy_machine(&wait_any);
    puts("coord WAIT: aggregate waker selects earlier scheduled child");

    /* SEQUENCE: one child result at a time, in declaration order. */
    int s0 = 3, s1 = 4;
    cflow_resumable seq_children[2] = {{0}, {0}}, seq = {0};
    assert(cflow_resumable_from_value(&seq_children[0], &cmeta_type_int, &s0));
    assert(cflow_resumable_from_value(&seq_children[1], &cmeta_type_int, &s1));
    assert(cflow_resumable_from_coordination(&seq, CFLOW_COORD_SEQUENCE, seq_children, 2));
    step = seq.ops->resume(seq.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE && event.child_index == 0);
    step = seq.ops->resume(seq.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE_AND_DONE && event.child_index == 1);
    destroy_machine(&seq);
    puts("coord SEQUENCE: ordered child handoff");

    /* LATEST: no event until every child has an initial value; after that,
     * every update yields a coordination event carrying the changed index. */
    const int l0[] = { 1, 2 };
    const int l1[] = { 10, 20 };
    cflow_resumable latest_children[2] = {{0}, {0}}, latest = {0};
    assert(script_ints(&latest_children[0], l0, 2));
    assert(script_ints(&latest_children[1], l1, 2));
    assert(cflow_resumable_from_coordination(&latest, CFLOW_COORD_LATEST, latest_children, 2));
    step = latest.ops->resume(latest.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE && event.child_index == 1 && event.generation == 1);
    assert(cflow_coord_value(&latest, 0, NULL, &v0) && *(const int *)v0 == 1);
    assert(cflow_coord_value(&latest, 1, NULL, &v1) && *(const int *)v1 == 10);
    step = latest.ops->resume(latest.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE && event.child_index == 0 && event.generation == 2);
    assert(cflow_coord_value(&latest, 0, NULL, &v0) && *(const int *)v0 == 2);
    step = latest.ops->resume(latest.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE_AND_DONE && event.child_index == 1 && event.generation == 3);
    assert(cflow_coord_value(&latest, 1, NULL, &v1) && *(const int *)v1 == 20);
    destroy_machine(&latest);
    puts("coord LATEST: initial barrier + latest updates");

    /* LATEST startup fairness: once the fast child has its first value it
     * pauses until the WAITing child establishes its initial value. */
    const int fast_values[] = { 1, 2, 3 };
    cflow_publisher slow_timer = {0};
    cflow_resumable fair_children[2] = {{0}, {0}}, fair = {0};
    assert(script_ints(&fair_children[0], fast_values, 3));
    assert(cflow_publisher_from_timer(&slow_timer, 1, 3));
    assert(cflow_resumable_from_publisher(&fair_children[1], &slow_timer));
    assert(cflow_resumable_from_coordination(&fair, CFLOW_COORD_LATEST, fair_children, 2));
    step = fair.ops->resume(fair.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_WAIT);
    assert(cflow_coord_value(&fair, 0, NULL, &v0) && *(const int *)v0 == 1);
    wf.fired = false;
    assert(cflow_waitable_arm(&step.waitable, (cflow_waker){ set_wake, &wf }));
    assert(cflow_scheduler_advance(&loop, 3) == 1 && wf.fired);
    step = fair.ops->resume(fair.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE && event.child_index == 1);
    assert(cflow_coord_value(&fair, 0, NULL, &v0) && *(const int *)v0 == 1);
    step = fair.ops->resume(fair.state, &ctx, &event);
    assert(step.kind == CFLOW_STEP_VALUE && event.child_index == 0);
    assert(cflow_coord_value(&fair, 0, NULL, &v0) && *(const int *)v0 == 2);
    destroy_machine(&fair);
    puts("coord LATEST: startup fairness across WAITing children");

    cflow_scheduler_destroy(&loop);
    return 0;
}
