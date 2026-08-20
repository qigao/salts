#include <cflow/scheduler.h>

#include <stdlib.h>
#include <string.h>

typedef struct cflow_scheduled_task {
    cflow_task_id id;
    uint64_t due;
    uint64_t order;
    cflow_task_fn fn;
    void *user;
    bool cancelled;
} cflow_scheduled_task;

typedef struct cflow_test_loop_state {
    cflow_scheduled_task *tasks;
    size_t count;
    size_t capacity;
    uint64_t now;
    uint64_t next_order;
    cflow_task_id next_id;
    bool running;
} cflow_test_loop_state;

static bool ensure_capacity(cflow_test_loop_state *s, size_t need) {
    if (need <= s->capacity) return true;
    size_t cap = s->capacity ? s->capacity * 2u : 16u;
    while (cap < need) {
        if (cap > SIZE_MAX / 2u) { cap = need; break; }
        cap *= 2u;
    }
    cflow_scheduled_task *p = realloc(s->tasks, cap * sizeof(*p));
    if (!p) return false;
    s->tasks = p;
    s->capacity = cap;
    return true;
}

static cflow_task_id test_post_after(void *state,
                                     uint64_t delay,
                                     cflow_task_fn fn,
                                     void *user) {
    cflow_test_loop_state *s = (cflow_test_loop_state *)state;
    if (!s || !fn || !ensure_capacity(s, s->count + 1u)) return 0;
    uint64_t due = UINT64_MAX - s->now < delay ? UINT64_MAX : s->now + delay;
    cflow_task_id id = s->next_id++;
    if (id == 0) id = s->next_id++;
    s->tasks[s->count++] = (cflow_scheduled_task){
        .id = id,
        .due = due,
        .order = s->next_order++,
        .fn = fn,
        .user = user,
        .cancelled = false
    };
    return id;
}

static bool test_cancel(void *state, cflow_task_id id) {
    cflow_test_loop_state *s = (cflow_test_loop_state *)state;
    if (!s || !id) return false;
    for (size_t i = 0; i < s->count; ++i) {
        if (s->tasks[i].id == id && !s->tasks[i].cancelled) {
            s->tasks[i].cancelled = true;
            return true;
        }
    }
    return false;
}

static void compact_cancelled(cflow_test_loop_state *s) {
    size_t w = 0;
    for (size_t i = 0; i < s->count; ++i) {
        if (!s->tasks[i].cancelled) {
            if (w != i) s->tasks[w] = s->tasks[i];
            ++w;
        }
    }
    s->count = w;
}

static size_t best_ready(const cflow_test_loop_state *s) {
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < s->count; ++i) {
        const cflow_scheduled_task *t = &s->tasks[i];
        if (t->cancelled || t->due > s->now) continue;
        if (best == SIZE_MAX || t->due < s->tasks[best].due ||
            (t->due == s->tasks[best].due && t->order < s->tasks[best].order))
            best = i;
    }
    return best;
}

static bool test_run_one(void *state) {
    cflow_test_loop_state *s = (cflow_test_loop_state *)state;
    if (!s) return false;
    compact_cancelled(s);
    size_t i = best_ready(s);
    if (i == SIZE_MAX) return false;
    cflow_scheduled_task task = s->tasks[i];
    memmove(&s->tasks[i], &s->tasks[i + 1],
            (s->count - i - 1u) * sizeof(s->tasks[0]));
    --s->count;
    bool outer = s->running;
    s->running = true;
    task.fn(task.user);
    s->running = outer;
    return true;
}

static size_t test_run_ready(void *state) {
    size_t n = 0;
    while (test_run_one(state)) ++n;
    return n;
}

static size_t test_advance(void *state, uint64_t ticks) {
    cflow_test_loop_state *s = (cflow_test_loop_state *)state;
    if (!s) return 0;
    s->now = UINT64_MAX - s->now < ticks ? UINT64_MAX : s->now + ticks;
    return test_run_ready(s);
}

static bool next_due(const cflow_test_loop_state *s, uint64_t *due) {
    bool found = false;
    uint64_t best = 0;
    for (size_t i = 0; i < s->count; ++i) {
        if (s->tasks[i].cancelled) continue;
        if (!found || s->tasks[i].due < best) {
            best = s->tasks[i].due;
            found = true;
        }
    }
    if (found && due) *due = best;
    return found;
}

static size_t test_run_until_idle(void *state, size_t max_steps) {
    cflow_test_loop_state *s = (cflow_test_loop_state *)state;
    if (!s) return 0;
    size_t ran = 0;
    for (;;) {
        compact_cancelled(s);
        if (s->count == 0 || (max_steps && ran >= max_steps)) break;
        uint64_t due = 0;
        if (!next_due(s, &due)) break;
        if (due > s->now) s->now = due;
        if (!test_run_one(s)) break;
        ++ran;
    }
    return ran;
}

static bool test_wait_idle(void *state) {
    (void)test_run_until_idle(state, 0);
    return true;
}

static uint64_t test_now(void *state) {
    const cflow_test_loop_state *s = (const cflow_test_loop_state *)state;
    return s ? s->now : 0;
}

static size_t test_pending(void *state) {
    const cflow_test_loop_state *s = (const cflow_test_loop_state *)state;
    if (!s) return 0;
    size_t n = 0;
    for (size_t i = 0; i < s->count; ++i)
        if (!s->tasks[i].cancelled) ++n;
    return n;
}

static void test_destroy(void *state) {
    cflow_test_loop_state *s = (cflow_test_loop_state *)state;
    if (!s) return;
    free(s->tasks);
    free(s);
}

CMETA_IMPLEMENTS(cflow_scheduler, test_loop,
    CMETA_SCHED_CAP_DELAYED | CMETA_SCHED_CAP_MANUAL_CLOCK,
    .post_after = test_post_after,
    .cancel = test_cancel,
    .run_one = test_run_one,
    .run_ready = test_run_ready,
    .advance = test_advance,
    .run_until_idle = test_run_until_idle,
    .wait_idle = test_wait_idle,
    .now = test_now,
    .pending = test_pending,
    .destroy = test_destroy
);

bool cflow_scheduler_test_init(cflow_scheduler *scheduler) {
    if (!scheduler) return false;
    memset(scheduler, 0, sizeof(*scheduler));
    cflow_test_loop_state *state = calloc(1, sizeof(*state));
    if (!state) return false;
    state->next_id = 1;
    *scheduler = test_loop_as_cflow_scheduler(state);
    return true;
}

CMETA_INTERFACE_IMPL(cflow_scheduler, CMETA_SCHEDULER_METHODS)

cflow_task_id cflow_scheduler_post(cflow_scheduler *scheduler,
                                   cflow_task_fn fn,
                                   void *user) {
    return cflow_scheduler_post_after(scheduler, 0, fn, user);
}

const char *cflow_scheduler_name(const cflow_scheduler *scheduler) {
    return cflow_scheduler_implementation(scheduler);
}
