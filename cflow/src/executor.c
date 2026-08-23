#include <cflow/executor.h>
#include <turbo/thread_pool.h>

#include <limits.h>
#include <stdint.h>
#include <stdatomic.h>
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
    size_t peak_pending;
    size_t rejected_full;
    size_t rejected_closed;
    bool closed;
} cflow_manual_executor_state;

typedef struct cflow_pool_executor_state {
    turbo_threadpool_t *pool;
    _Atomic size_t rejected_full;
    _Atomic size_t rejected_closed;
} cflow_pool_executor_state;

static cflow_admission_status manual_try_post(void *self, cflow_task_fn fn,
                                              void *user) {
    cflow_manual_executor_state *state = (cflow_manual_executor_state *)self;
    if (!state || !fn) return CFLOW_ADMISSION_INVALID_ARGUMENT;
    if (state->closed) {
        ++state->rejected_closed;
        return CFLOW_ADMISSION_CLOSED;
    }
    if (state->count >= state->capacity) {
        ++state->rejected_full;
        return CFLOW_ADMISSION_FULL;
    }
    state->tasks[state->count++] = (cflow_manual_task){fn, user};
    if (state->count > state->peak_pending)
        state->peak_pending = state->count;
    return CFLOW_ADMISSION_ACCEPTED;
}

static bool manual_post(void *self, cflow_task_fn fn, void *user) {
    return manual_try_post(self, fn, user) == CFLOW_ADMISSION_ACCEPTED;
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
    const cflow_manual_executor_state *state =
        (const cflow_manual_executor_state *)self;
    return state && state->count == 0u;
}

static size_t manual_pending(void *self) {
    const cflow_manual_executor_state *state =
        (const cflow_manual_executor_state *)self;
    return state ? state->count : 0u;
}

static bool manual_shutdown(void *self) {
    cflow_manual_executor_state *state = (cflow_manual_executor_state *)self;
    if (!state) return false;
    state->closed = true;
    return true;
}

static bool manual_get_stats(void *self, cflow_executor_stats *out) {
    const cflow_manual_executor_state *state =
        (const cflow_manual_executor_state *)self;
    if (!state || !out) return false;
    *out = (cflow_executor_stats){
        .capacity = state->capacity,
        .pending = state->count,
        .peak_pending = state->peak_pending,
        .rejected_full = state->rejected_full,
        .rejected_closed = state->rejected_closed
    };
    return true;
}

static void manual_destroy(void *self) {
    cflow_manual_executor_state *state = (cflow_manual_executor_state *)self;
    if (!state) return;
    free(state->tasks);
    free(state);
}

CMETA_IMPLEMENTS(cflow_executor, manual_executor,
    CMETA_EXEC_CAP_MANUAL | CMETA_EXEC_CAP_SERIAL,
    .try_post = manual_try_post,
    .post = manual_post,
    .run_one = manual_run_one,
    .run_ready = manual_run_ready,
    .wait_idle = manual_wait_idle,
    .pending = manual_pending,
    .shutdown = manual_shutdown,
    .get_stats = manual_get_stats,
    .destroy = manual_destroy
);

static cflow_admission_status pool_admission_status(int status) {
    switch (status) {
    case TURBO_OK: return CFLOW_ADMISSION_ACCEPTED;
    case TURBO_EINVAL: return CFLOW_ADMISSION_INVALID_ARGUMENT;
    case TURBO_ENOBUFS: return CFLOW_ADMISSION_FULL;
    case TURBO_ESHUTDOWN: return CFLOW_ADMISSION_CLOSED;
    default: return CFLOW_ADMISSION_CLOSED;
    }
}

static size_t pool_pending(void *self);

static cflow_admission_status pool_try_post(void *self, cflow_task_fn fn,
                                            void *user) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    cflow_admission_status status;
    if (!state || !state->pool || !fn)
        return CFLOW_ADMISSION_INVALID_ARGUMENT;
    status = pool_admission_status(
        turbo_threadpool_try_submit(state->pool, fn, user));
    if (status == CFLOW_ADMISSION_FULL)
        atomic_fetch_add(&state->rejected_full, 1u);
    else if (status == CFLOW_ADMISSION_CLOSED)
        atomic_fetch_add(&state->rejected_closed, 1u);
    return status;
}

static bool pool_post(void *self, cflow_task_fn fn, void *user) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    cflow_admission_status status;
    if (!state || !state->pool || !fn) return false;
    status = pool_admission_status(
        turbo_threadpool_submit(state->pool, fn, user));
    if (status == CFLOW_ADMISSION_CLOSED)
        atomic_fetch_add(&state->rejected_closed, 1u);
    return status == CFLOW_ADMISSION_ACCEPTED;
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

static bool pool_shutdown(void *self) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    if (!state || !state->pool) return false;
    turbo_threadpool_shutdown(state->pool);
    return true;
}

static bool pool_get_stats(void *self, cflow_executor_stats *out) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    turbo_threadpool_stats_t pool_stats;
    if (!state || !state->pool || !out) return false;
    turbo_threadpool_get_stats(state->pool, &pool_stats);
    *out = (cflow_executor_stats){
        .capacity = pool_stats.queue_capacity,
        .pending = pool_stats.pending_tasks > 0
                       ? (size_t)pool_stats.pending_tasks
                       : 0u,
        .peak_pending = pool_stats.peak_pending_tasks > 0
                            ? (size_t)pool_stats.peak_pending_tasks
                            : 0u,
        .rejected_full = atomic_load(&state->rejected_full),
        .rejected_closed = atomic_load(&state->rejected_closed)
    };
    return true;
}

static void pool_destroy(void *self) {
    cflow_pool_executor_state *state = (cflow_pool_executor_state *)self;
    if (!state) return;
    turbo_threadpool_destroy(state->pool);
    free(state);
}

CMETA_IMPLEMENTS(cflow_executor, serial_executor,
    CMETA_EXEC_CAP_SERIAL,
    .try_post = pool_try_post,
    .post = pool_post,
    .run_one = pool_run_one,
    .run_ready = pool_run_ready,
    .wait_idle = pool_wait_idle,
    .pending = pool_pending,
    .shutdown = pool_shutdown,
    .get_stats = pool_get_stats,
    .destroy = pool_destroy
);

CMETA_IMPLEMENTS(cflow_executor, worker_executor,
    CMETA_EXEC_CAP_CONCURRENT,
    .try_post = pool_try_post,
    .post = pool_post,
    .run_one = pool_run_one,
    .run_ready = pool_run_ready,
    .wait_idle = pool_wait_idle,
    .pending = pool_pending,
    .shutdown = pool_shutdown,
    .get_stats = pool_get_stats,
    .destroy = pool_destroy
);

bool cflow_executor_manual_init_with_capacity(cflow_executor *executor,
                                              size_t capacity) {
    cflow_manual_executor_state *state;
    if (!executor) return false;
    memset(executor, 0, sizeof(*executor));
    if (capacity == 0u || capacity > SIZE_MAX / sizeof(cflow_manual_task))
        return false;
    state = (cflow_manual_executor_state *)calloc(1, sizeof(*state));
    if (!state) return false;
    state->tasks = (cflow_manual_task *)calloc(capacity, sizeof(*state->tasks));
    if (!state->tasks) {
        free(state);
        return false;
    }
    state->capacity = capacity;
    *executor = manual_executor_as_cflow_executor(state);
    return true;
}

bool cflow_executor_manual_init(cflow_executor *executor) {
    return cflow_executor_manual_init_with_capacity(
        executor, CFLOW_EXECUTOR_DEFAULT_CAPACITY);
}

static bool pool_executor_init(cflow_executor *executor, size_t workers,
                               size_t capacity, bool serial) {
    cflow_pool_executor_state *state;
    turbo_threadpool_config_t config;
    if (!executor) return false;
    memset(executor, 0, sizeof(*executor));
    if (workers == 0u || workers > (size_t)INT_MAX || capacity == 0u)
        return false;
    state = (cflow_pool_executor_state *)calloc(1, sizeof(*state));
    if (!state) return false;
    config.num_threads = (int)workers;
    config.queue_capacity = capacity;
    state->pool = turbo_threadpool_create_with_config(&config);
    if (!state->pool) {
        free(state);
        return false;
    }
    *executor = serial ? serial_executor_as_cflow_executor(state)
                       : worker_executor_as_cflow_executor(state);
    return true;
}

bool cflow_executor_serial_init(cflow_executor *executor) {
    return cflow_executor_serial_init_with_capacity(
        executor, CFLOW_EXECUTOR_DEFAULT_CAPACITY);
}

bool cflow_executor_serial_init_with_capacity(cflow_executor *executor,
                                              size_t capacity) {
    return pool_executor_init(executor, 1u, capacity, true);
}

bool cflow_executor_worker_init(cflow_executor *executor, size_t workers) {
    return cflow_executor_worker_init_with_capacity(
        executor, workers, CFLOW_EXECUTOR_DEFAULT_CAPACITY);
}

bool cflow_executor_worker_init_with_capacity(cflow_executor *executor,
                                              size_t workers,
                                              size_t capacity) {
    return pool_executor_init(executor, workers, capacity, false);
}
