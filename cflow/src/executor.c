#include <cflow/executor.h>
#include <turbo/thread_pool.h>

#include <stdlib.h>
#include <string.h>

typedef struct cflow_manual_task {
    cflow_task_fn fn;
    void *user;
} cflow_manual_task;

typedef struct cflow_manual_executor_state {
    cflow_manual_task *tasks;
    size_t count;
    size_t capacity;
} cflow_manual_executor_state;

typedef struct cflow_pool_executor_state {
    turbo_threadpool_t *pool;
} cflow_pool_executor_state;

static bool manual_ensure_capacity(cflow_manual_executor_state *state, size_t need) {
    size_t capacity;
    cflow_manual_task *tasks;

    if (need <= state->capacity) return true;
    capacity = state->capacity ? state->capacity * 2u : 16u;
    while (capacity < need) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = need;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*state->tasks)) return false;
    tasks = (cflow_manual_task *)realloc(state->tasks,
                                         capacity * sizeof(*state->tasks));
    if (!tasks) return false;
    state->tasks = tasks;
    state->capacity = capacity;
    return true;
}

static bool manual_post(void *self, cflow_task_fn fn, void *user) {
    cflow_manual_executor_state *state = (cflow_manual_executor_state *)self;
    if (!state || !fn || !manual_ensure_capacity(state, state->count + 1u))
        return false;
    state->tasks[state->count++] = (cflow_manual_task){fn, user};
    return true;
}

static bool manual_run_one(void *self) {
    cflow_manual_executor_state *state = (cflow_manual_executor_state *)self;
    cflow_manual_task task;

    if (!state || state->count == 0u) return false;
    task = state->tasks[0];
    if (state->count > 1u) {
        memmove(&state->tasks[0], &state->tasks[1],
                (state->count - 1u) * sizeof(state->tasks[0]));
    }
    --state->count;
    task.fn(task.user);
    return true;
}

static size_t manual_run_ready(void *self) {
    size_t count = 0u;
    while (manual_run_one(self)) ++count;
    return count;
}

static bool manual_wait_idle(void *self) {
    (void)manual_run_ready(self);
    return true;
}

static size_t manual_pending(void *self) {
    const cflow_manual_executor_state *state =
        (const cflow_manual_executor_state *)self;
    return state ? state->count : 0u;
}

static void manual_destroy(void *self) {
    cflow_manual_executor_state *state = (cflow_manual_executor_state *)self;
    if (!state) return;
    free(state->tasks);
    free(state);
}

CMETA_IMPLEMENTS(cflow_executor, manual_executor,
    CMETA_EXEC_CAP_MANUAL | CMETA_EXEC_CAP_SERIAL,
    .post = manual_post,
    .run_one = manual_run_one,
    .run_ready = manual_run_ready,
    .wait_idle = manual_wait_idle,
    .pending = manual_pending,
    .destroy = manual_destroy
);

static bool pool_post(void *self, cflow_task_fn fn, void *user) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    return state && state->pool && fn &&
           turbo_threadpool_submit(state->pool, fn, user) == 0;
}

static bool pool_run_one(void *self) {
    (void)self;
    return false;
}

static size_t pool_run_ready(void *self) {
    (void)self;
    return 0u;
}

static bool pool_wait_idle(void *self) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    if (!state || !state->pool) return false;
    turbo_threadpool_wait(state->pool);
    return true;
}

static size_t pool_pending(void *self) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    int pending;
    if (!state || !state->pool) return 0u;
    pending = turbo_threadpool_pending(state->pool);
    return pending > 0 ? (size_t)pending : 0u;
}

static void pool_destroy(void *self) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    if (!state) return;
    turbo_threadpool_destroy(state->pool);
    free(state);
}

CMETA_IMPLEMENTS(cflow_executor, serial_executor,
    CMETA_EXEC_CAP_SERIAL,
    .post = pool_post,
    .run_one = pool_run_one,
    .run_ready = pool_run_ready,
    .wait_idle = pool_wait_idle,
    .pending = pool_pending,
    .destroy = pool_destroy
);

CMETA_IMPLEMENTS(cflow_executor, worker_executor,
    CMETA_EXEC_CAP_CONCURRENT,
    .post = pool_post,
    .run_one = pool_run_one,
    .run_ready = pool_run_ready,
    .wait_idle = pool_wait_idle,
    .pending = pool_pending,
    .destroy = pool_destroy
);

bool cflow_executor_manual_init(cflow_executor *executor) {
    cflow_manual_executor_state *state;
    if (!executor) return false;
    memset(executor, 0, sizeof(*executor));
    state = (cflow_manual_executor_state *)calloc(1, sizeof(*state));
    if (!state) return false;
    *executor = manual_executor_as_cflow_executor(state);
    return true;
}

static bool pool_executor_init(cflow_executor *executor, size_t workers,
                               bool serial) {
    cflow_pool_executor_state *state;
    if (!executor || workers == 0u) return false;
    memset(executor, 0, sizeof(*executor));
    state = (cflow_pool_executor_state *)calloc(1, sizeof(*state));
    if (!state) return false;
    state->pool = turbo_threadpool_create((int)workers);
    if (!state->pool) {
        free(state);
        return false;
    }
    *executor = serial ? serial_executor_as_cflow_executor(state)
                       : worker_executor_as_cflow_executor(state);
    return true;
}

bool cflow_executor_serial_init(cflow_executor *executor) {
    return pool_executor_init(executor, 1u, true);
}

bool cflow_executor_worker_init(cflow_executor *executor, size_t workers) {
    if (workers > (size_t)INT_MAX) return false;
    return pool_executor_init(executor, workers, false);
}
