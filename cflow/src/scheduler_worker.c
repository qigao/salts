#include <cflow/scheduler.h>

#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

typedef struct worker_task {
    cflow_task_id id;
    uint64_t due_ms;
    uint64_t order;
    cflow_task_fn fn;
    void *user;
    bool cancelled;
} worker_task;

typedef struct worker_state {
    mtx_t mutex;
    cnd_t cv;
    cnd_t idle_cv;
    thrd_t *threads;
    size_t worker_count;
    worker_task *tasks;
    size_t count;
    size_t capacity;
    size_t active;
    cflow_task_id next_id;
    uint64_t next_order;
    bool stopping;
} worker_state;

static uint64_t now_ms(void) {
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static struct timespec ms_to_timespec(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    return ts;
}

static bool ensure_capacity(worker_state *s, size_t need) {
    if (need <= s->capacity) return true;
    size_t cap = s->capacity ? s->capacity * 2u : 16u;
    while (cap < need) {
        if (cap > SIZE_MAX / 2u) { cap = need; break; }
        cap *= 2u;
    }
    worker_task *p = realloc(s->tasks, cap * sizeof(*p));
    if (!p) return false;
    s->tasks = p;
    s->capacity = cap;
    return true;
}

static void compact_cancelled(worker_state *s) {
    size_t w = 0;
    for (size_t i = 0; i < s->count; ++i) {
        if (!s->tasks[i].cancelled) {
            if (w != i) s->tasks[w] = s->tasks[i];
            ++w;
        }
    }
    s->count = w;
}

static size_t earliest_task(const worker_state *s) {
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < s->count; ++i) {
        const worker_task *t = &s->tasks[i];
        if (t->cancelled) continue;
        if (best == SIZE_MAX || t->due_ms < s->tasks[best].due_ms ||
            (t->due_ms == s->tasks[best].due_ms &&
             t->order < s->tasks[best].order))
            best = i;
    }
    return best;
}

static bool is_idle(const worker_state *s) {
    if (s->active != 0) return false;
    for (size_t i = 0; i < s->count; ++i)
        if (!s->tasks[i].cancelled) return false;
    return true;
}

static int worker_main(void *arg) {
    worker_state *s = (worker_state *)arg;
    if (!s) return -1;

    if (mtx_lock(&s->mutex) != thrd_success) return -1;
    for (;;) {
        compact_cancelled(s);
        if (s->stopping) break;

        size_t idx = earliest_task(s);
        if (idx == SIZE_MAX) {
            (void)cnd_wait(&s->cv, &s->mutex);
            continue;
        }

        uint64_t now = now_ms();
        if (s->tasks[idx].due_ms > now) {
            struct timespec deadline = ms_to_timespec(s->tasks[idx].due_ms);
            (void)cnd_timedwait(&s->cv, &s->mutex, &deadline);
            continue;
        }

        worker_task task = s->tasks[idx];
        memmove(&s->tasks[idx], &s->tasks[idx + 1],
                (s->count - idx - 1u) * sizeof(s->tasks[0]));
        --s->count;
        ++s->active;
        (void)mtx_unlock(&s->mutex);

        task.fn(task.user);

        (void)mtx_lock(&s->mutex);
        --s->active;
        if (is_idle(s)) cnd_broadcast(&s->idle_cv);
    }
    (void)mtx_unlock(&s->mutex);
    return 0;
}

static cflow_task_id worker_post_after(void *state,
                                       uint64_t delay_ms,
                                       cflow_task_fn fn,
                                       void *user) {
    worker_state *s = (worker_state *)state;
    if (!s || !fn) return 0;
    if (mtx_lock(&s->mutex) != thrd_success) return 0;
    if (s->stopping || !ensure_capacity(s, s->count + 1u)) {
        (void)mtx_unlock(&s->mutex);
        return 0;
    }
    uint64_t now = now_ms();
    uint64_t due = UINT64_MAX - now < delay_ms ? UINT64_MAX : now + delay_ms;
    cflow_task_id id = s->next_id++;
    if (id == 0) id = s->next_id++;
    s->tasks[s->count++] = (worker_task){
        .id = id,
        .due_ms = due,
        .order = s->next_order++,
        .fn = fn,
        .user = user,
        .cancelled = false
    };
    cnd_broadcast(&s->cv);
    (void)mtx_unlock(&s->mutex);
    return id;
}

static bool worker_cancel(void *state, cflow_task_id id) {
    worker_state *s = (worker_state *)state;
    if (!s || !id || mtx_lock(&s->mutex) != thrd_success) return false;
    bool found = false;
    for (size_t i = 0; i < s->count; ++i) {
        if (s->tasks[i].id == id && !s->tasks[i].cancelled) {
            s->tasks[i].cancelled = true;
            found = true;
            break;
        }
    }
    compact_cancelled(s);
    if (is_idle(s)) cnd_broadcast(&s->idle_cv);
    cnd_broadcast(&s->cv);
    (void)mtx_unlock(&s->mutex);
    return found;
}

static bool worker_wait_idle(void *state) {
    worker_state *s = (worker_state *)state;
    if (!s || mtx_lock(&s->mutex) != thrd_success) return false;
    while (!s->stopping && !is_idle(s))
        (void)cnd_wait(&s->idle_cv, &s->mutex);
    bool ok = !s->stopping || is_idle(s);
    (void)mtx_unlock(&s->mutex);
    return ok;
}

static uint64_t worker_now(void *state) {
    (void)state;
    return now_ms();
}

static size_t worker_pending(void *state) {
    worker_state *s = (worker_state *)state;
    if (!s || mtx_lock(&s->mutex) != thrd_success) return 0;
    size_t count = 0;
    for (size_t i = 0; i < s->count; ++i)
        if (!s->tasks[i].cancelled) ++count;
    (void)mtx_unlock(&s->mutex);
    return count;
}

static void worker_destroy(void *state) {
    worker_state *s = (worker_state *)state;
    if (!s) return;
    if (mtx_lock(&s->mutex) == thrd_success) {
        s->stopping = true;
        s->count = 0;
        cnd_broadcast(&s->cv);
        cnd_broadcast(&s->idle_cv);
        (void)mtx_unlock(&s->mutex);
    }
    for (size_t i = 0; i < s->worker_count; ++i) {
        int result = 0;
        (void)thrd_join(s->threads[i], &result);
    }
    free(s->tasks);
    free(s->threads);
    cnd_destroy(&s->idle_cv);
    cnd_destroy(&s->cv);
    mtx_destroy(&s->mutex);
    free(s);
}

static bool worker_run_one(void *state) { (void)state; return false; }
static size_t worker_run_ready(void *state) { (void)state; return 0; }
static size_t worker_advance(void *state, uint64_t ticks) { (void)state; (void)ticks; return 0; }
static size_t worker_run_until_idle(void *state, size_t max_steps) { (void)state; (void)max_steps; return 0; }

CMETA_IMPLEMENTS(cflow_scheduler, c11_worker,
    CMETA_SCHED_CAP_DELAYED | CMETA_SCHED_CAP_CONCURRENT,
    .post_after = worker_post_after,
    .cancel = worker_cancel,
    .run_one = worker_run_one,
    .run_ready = worker_run_ready,
    .advance = worker_advance,
    .run_until_idle = worker_run_until_idle,
    .wait_idle = worker_wait_idle,
    .now = worker_now,
    .pending = worker_pending,
    .destroy = worker_destroy
);

bool cflow_scheduler_worker_init(cflow_scheduler *scheduler, size_t workers) {
    if (!scheduler || workers == 0) return false;
    memset(scheduler, 0, sizeof(*scheduler));
    worker_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    if (mtx_init(&s->mutex, mtx_plain) != thrd_success) {
        free(s); return false;
    }
    if (cnd_init(&s->cv) != thrd_success) {
        mtx_destroy(&s->mutex); free(s); return false;
    }
    if (cnd_init(&s->idle_cv) != thrd_success) {
        cnd_destroy(&s->cv); mtx_destroy(&s->mutex); free(s); return false;
    }
    s->threads = calloc(workers, sizeof(*s->threads));
    if (!s->threads) {
        cnd_destroy(&s->idle_cv); cnd_destroy(&s->cv); mtx_destroy(&s->mutex);
        free(s); return false;
    }
    s->worker_count = workers;
    s->next_id = 1;
    size_t created = 0;
    for (; created < workers; ++created) {
        if (thrd_create(&s->threads[created], worker_main, s) != thrd_success)
            break;
    }
    if (created != workers) {
        if (mtx_lock(&s->mutex) == thrd_success) {
            s->stopping = true;
            cnd_broadcast(&s->cv);
            (void)mtx_unlock(&s->mutex);
        }
        for (size_t i = 0; i < created; ++i) {
            int result = 0;
            (void)thrd_join(s->threads[i], &result);
        }
        free(s->threads);
        cnd_destroy(&s->idle_cv); cnd_destroy(&s->cv); mtx_destroy(&s->mutex);
        free(s); return false;
    }
    *scheduler = c11_worker_as_cflow_scheduler(s);
    return true;
}
