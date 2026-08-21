#include <cflow/sources.h>

#include <stdlib.h>
#include <string.h>
#include <threads.h>

static void source_no_cancel(void *state) { (void)state; }
static void source_no_terminal_bind(void *state, cflow_waker waker) { (void)state; (void)waker; }
static cflow_source_terminal source_open_terminal(void *state, const char **error) {
    (void)state; (void)error; return CFLOW_SOURCE_OPEN;
}

/* ---------------- array ---------------- */
typedef struct array_state {
    const unsigned char *data;
    const cmeta_type_desc *type;
    size_t count;
    size_t index;
} array_state;

static cflow_step array_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    (void)ctx;
    array_state *s = (array_state *)state;
    if (!s || s->index >= s->count) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    memcpy(out, s->data + s->index * s->type->size, s->type->size);
    ++s->index;
    return (cflow_step){ s->index == s->count ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL };
}
static void simple_free(void *state) { free(state); }
static const char *array_name(void *state) { (void)state; return "array"; }
static const cmeta_type_desc *array_type(void *state) {
    array_state *s = (array_state *)state; return s ? s->type : NULL;
}
CMETA_IMPLEMENTS(cflow_source, array_source, 0,
    .name = array_name,
    .output_type = array_type,
    .resume = array_resume,
    .cancel = source_no_cancel,
    .destroy = simple_free,
    .bind_terminal_waker = source_no_terminal_bind,
    .poll_terminal = source_open_terminal
);

bool cflow_source_from_array(cflow_source *out,
                             const cmeta_type_desc *type,
                             const void *data,
                             size_t count) {
    if (!out || !type || (count && !data)) return false;
    array_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->data = (const unsigned char *)data; s->type = type; s->count = count;
    *out = array_source_as_cflow_source(s);
    return true;
}

/* ---------------- CMeta Range ---------------- */
typedef struct range_state {
    cmeta_range range;
    cmeta_range_cursor cursor;
} range_state;

static cflow_step range_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    range_state *s = (range_state *)state;
    cmeta_gen_status status;
    (void)ctx;
    if (!s) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "range source unavailable" };
    status = cmeta_range_next(&s->range, &s->cursor, out);
    switch (status) {
        case CMETA_GEN_VALUE:
            return (cflow_step){ CFLOW_STEP_VALUE, {0}, NULL };
        case CMETA_GEN_VALUE_AND_DONE:
            return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
        case CMETA_GEN_DONE:
            return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
        case CMETA_GEN_MUTATED:
            return (cflow_step){ CFLOW_STEP_ERROR, {0}, "range owner mutated" };
        case CMETA_GEN_ERROR:
            return (cflow_step){ CFLOW_STEP_ERROR, {0}, "range iteration failed" };
    }
    return (cflow_step){ CFLOW_STEP_ERROR, {0}, "invalid range status" };
}
static const char *range_name(void *state) { (void)state; return "range"; }
static const cmeta_type_desc *range_type(void *state) {
    range_state *s = (range_state *)state;
    return s ? s->range.element_type : NULL;
}
CMETA_IMPLEMENTS(cflow_source, range_source, 0,
    .name = range_name,
    .output_type = range_type,
    .resume = range_resume,
    .cancel = source_no_cancel,
    .destroy = simple_free,
    .bind_terminal_waker = source_no_terminal_bind,
    .poll_terminal = source_open_terminal
);

bool cflow_source_from_range(cflow_source *out, cmeta_range range) {
    range_state *s;
    if (!out || !range.object || !range.element_type || !range.next) return false;
    s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->range = range;
    *out = range_source_as_cflow_source(s);
    return true;
}

/* ---------------- timer ---------------- */
typedef struct timer_state {
    size_t count;
    size_t emitted;
    uint64_t interval;
    bool ready;
    cflow_scheduler *scheduler;
    cflow_task_id task;
    cflow_waker waker;
} timer_state;

static void timer_fire(void *user) {
    timer_state *s = (timer_state *)user;
    s->task = 0; s->ready = true;
    cflow_waker w = s->waker; s->waker = (cflow_waker){0};
    if (w.wake) w.wake(w.user);
}
static bool timer_arm(void *state, cflow_waker w) {
    timer_state *s = (timer_state *)state;
    if (!s || !s->scheduler || s->task) return false;
    s->waker = w;
    s->task = cflow_scheduler_post_after(s->scheduler, s->interval, timer_fire, s);
    return s->task != 0;
}
static void timer_wait_cancel(void *state) {
    timer_state *s = (timer_state *)state;
    if (s && s->task) { (void)cflow_scheduler_cancel(s->scheduler, s->task); s->task = 0; }
    if (s) s->waker = (cflow_waker){0};
}
CMETA_IMPLEMENTS(cflow_waitable, timer_waitable, 0,
    .arm = timer_arm,
    .cancel = timer_wait_cancel
);
static cflow_step timer_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    timer_state *s = (timer_state *)state;
    if (!s || !ctx || !ctx->scheduler) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "timer has no scheduler" };
    s->scheduler = ctx->scheduler;
    if (s->emitted >= s->count) return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
    if (!s->ready) return (cflow_step){ CFLOW_STEP_WAIT, timer_waitable_as_cflow_waitable(s), NULL };
    s->ready = false;
    size_t v = s->emitted++;
    memcpy(out, &v, sizeof v);
    return (cflow_step){ s->emitted == s->count ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL };
}
static void timer_cancel(void *state) { timer_wait_cancel(state); }
static void timer_destroy(void *state) { timer_wait_cancel(state); free(state); }
static const char *timer_name(void *state) { (void)state; return "timer"; }
static const cmeta_type_desc *timer_type(void *state) { (void)state; return &cmeta_type_size; }
CMETA_IMPLEMENTS(cflow_source, timer_source, 0,
    .name = timer_name,
    .output_type = timer_type,
    .resume = timer_resume,
    .cancel = timer_cancel,
    .destroy = timer_destroy,
    .bind_terminal_waker = source_no_terminal_bind,
    .poll_terminal = source_open_terminal
);

bool cflow_source_from_timer(cflow_source *out,
                             size_t count,
                             uint64_t interval_ticks) {
    if (!out) return false;
    timer_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    s->count = count; s->interval = interval_ticks;
    *out = timer_source_as_cflow_source(s);
    return true;
}

/* ---------------- channel ---------------- */
typedef struct channel_impl {
    const cmeta_type_desc *type;
    unsigned char *data;
    size_t capacity;
    size_t head, count;
    bool closed;
    mtx_t lock;
    cflow_waker waiter;
    cflow_waker terminal_waker;
} channel_impl;

typedef struct channel_source_state { channel_impl *ch; } channel_source_state;

static channel_impl *channel_of(cflow_channel *ch) { return ch ? (channel_impl *)ch->impl : NULL; }

bool cflow_channel_init(cflow_channel *ch,
                        const cmeta_type_desc *type,
                        size_t capacity) {
    if (!ch || !type || capacity == 0) return false;
    channel_impl *c = calloc(1, sizeof(*c));
    if (!c) return false;
    c->data = malloc(type->size * capacity);
    if (!c->data || mtx_init(&c->lock, mtx_plain) != thrd_success) { free(c->data); free(c); return false; }
    c->type = type; c->capacity = capacity; ch->impl = c; return true;
}

bool cflow_channel_push(cflow_channel *ch, const void *value) {
    channel_impl *c = channel_of(ch);
    if (!c || !value || mtx_lock(&c->lock) != thrd_success) return false;
    if (c->closed || c->count == c->capacity) { (void)mtx_unlock(&c->lock); return false; }
    size_t tail = (c->head + c->count) % c->capacity;
    memcpy(c->data + tail * c->type->size, value, c->type->size); ++c->count;
    cflow_waker w = c->waiter; c->waiter = (cflow_waker){0};
    (void)mtx_unlock(&c->lock);
    if (w.wake) w.wake(w.user);
    return true;
}

void cflow_channel_close(cflow_channel *ch) {
    channel_impl *c = channel_of(ch); if (!c) return;
    if (mtx_lock(&c->lock) != thrd_success) return;
    c->closed = true;
    cflow_waker w = c->waiter; c->waiter = (cflow_waker){0};
    cflow_waker tw = c->terminal_waker;
    (void)mtx_unlock(&c->lock);
    if (w.wake) w.wake(w.user);
    if (tw.wake) tw.wake(tw.user);
}

void cflow_channel_destroy(cflow_channel *ch) {
    channel_impl *c = channel_of(ch); if (!c) return;
    cflow_channel_close(ch); mtx_destroy(&c->lock); free(c->data); free(c); ch->impl = NULL;
}

static bool channel_arm(void *state, cflow_waker w) {
    channel_source_state *ss = (channel_source_state *)state;
    channel_impl *c = ss ? ss->ch : NULL; if (!c) return false;
    if (mtx_lock(&c->lock) != thrd_success) return false;
    bool ready = c->count || c->closed;
    if (!ready) c->waiter = w;
    (void)mtx_unlock(&c->lock);
    if (ready && w.wake) w.wake(w.user);
    return true;
}
static void channel_unarm(void *state) {
    channel_source_state *ss = (channel_source_state *)state;
    channel_impl *c = ss ? ss->ch : NULL; if (!c) return;
    if (mtx_lock(&c->lock) == thrd_success) { c->waiter = (cflow_waker){0}; (void)mtx_unlock(&c->lock); }
}
CMETA_IMPLEMENTS(cflow_waitable, channel_waitable, 0,
    .arm = channel_arm,
    .cancel = channel_unarm
);
static cflow_step channel_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    (void)ctx;
    channel_source_state *ss = (channel_source_state *)state;
    channel_impl *c = ss ? ss->ch : NULL;
    if (!c || mtx_lock(&c->lock) != thrd_success) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "channel unavailable" };
    if (c->count) {
        memcpy(out, c->data + c->head * c->type->size, c->type->size);
        c->head = (c->head + 1) % c->capacity; --c->count;
        bool final = c->closed && c->count == 0;
        (void)mtx_unlock(&c->lock);
        return (cflow_step){ final ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL };
    }
    if (c->closed) { (void)mtx_unlock(&c->lock); return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL }; }
    (void)mtx_unlock(&c->lock);
    return (cflow_step){ CFLOW_STEP_WAIT, channel_waitable_as_cflow_waitable(ss), NULL };
}
static void channel_source_cancel(void *state) { channel_unarm(state); }
static const char *channel_source_name(void *state) { (void)state; return "channel"; }
static const cmeta_type_desc *channel_source_type(void *state) {
    channel_source_state *ss = (channel_source_state *)state;
    return ss && ss->ch ? ss->ch->type : NULL;
}

static void channel_bind_terminal(void *state, cflow_waker w) {
    channel_source_state *ss = (channel_source_state *)state;
    channel_impl *c = ss ? ss->ch : NULL; if (!c) return;
    if (mtx_lock(&c->lock) == thrd_success) { c->terminal_waker = w; (void)mtx_unlock(&c->lock); }
}
static cflow_source_terminal channel_poll_terminal(void *state, const char **error) {
    (void)error;
    channel_source_state *ss = (channel_source_state *)state;
    channel_impl *c = ss ? ss->ch : NULL; if (!c) return CFLOW_SOURCE_ERROR;
    if (mtx_lock(&c->lock) != thrd_success) return CFLOW_SOURCE_ERROR;
    bool done = c->closed && c->count == 0;
    (void)mtx_unlock(&c->lock);
    return done ? CFLOW_SOURCE_DONE : CFLOW_SOURCE_OPEN;
}
CMETA_IMPLEMENTS(cflow_source, channel_source, 0,
    .name = channel_source_name,
    .output_type = channel_source_type,
    .resume = channel_resume,
    .cancel = channel_source_cancel,
    .destroy = simple_free,
    .bind_terminal_waker = channel_bind_terminal,
    .poll_terminal = channel_poll_terminal
);

bool cflow_source_from_channel(cflow_source *out, cflow_channel *ch) {
    channel_impl *c = channel_of(ch); if (!out || !c) return false;
    channel_source_state *s = calloc(1, sizeof(*s)); if (!s) return false;
    s->ch = c; *out = channel_source_as_cflow_source(s); return true;
}

/* ---------------- generic readiness/completion source ---------------- */
typedef struct readiness_state {
    const char *name;
    const cmeta_type_desc *type;
    cflow_read_fn read;
    cflow_watch_fn arm;
    cflow_unwatch_fn cancel;
    cflow_resource_close_fn close;
    void *user;
} readiness_state;

static bool readiness_arm(void *state, cflow_waker w) {
    readiness_state *s = (readiness_state *)state;
    return s && s->arm && s->arm(s->user, w);
}
static void readiness_unarm(void *state) {
    readiness_state *s = (readiness_state *)state;
    if (s && s->cancel) s->cancel(s->user);
}
CMETA_IMPLEMENTS(cflow_waitable, readiness_waitable, 0,
    .arm = readiness_arm,
    .cancel = readiness_unarm
);
static cflow_step readiness_resume(void *state, cflow_resume_ctx *ctx, void *out) {
    (void)ctx;
    readiness_state *s = (readiness_state *)state; const char *err = NULL;
    if (!s || !s->read) return (cflow_step){ CFLOW_STEP_ERROR, {0}, "readiness source unavailable" };
    switch (s->read(s->user, out, &err)) {
        case CFLOW_READ_VALUE: return (cflow_step){ CFLOW_STEP_VALUE, {0}, NULL };
        case CFLOW_READ_VALUE_AND_DONE: return (cflow_step){ CFLOW_STEP_VALUE_AND_DONE, {0}, NULL };
        case CFLOW_READ_WOULD_BLOCK: return (cflow_step){ CFLOW_STEP_WAIT, readiness_waitable_as_cflow_waitable(s), NULL };
        case CFLOW_READ_DONE: return (cflow_step){ CFLOW_STEP_DONE, {0}, NULL };
        case CFLOW_READ_ERROR: return (cflow_step){ CFLOW_STEP_ERROR, {0}, err ? err : "readiness source error" };
    }
    return (cflow_step){ CFLOW_STEP_ERROR, {0}, "invalid readiness status" };
}
static void readiness_cancel(void *state) { readiness_unarm(state); }
static void readiness_destroy(void *state) {
    readiness_state *s = (readiness_state *)state;
    if (s) { readiness_unarm(s); if (s->close) s->close(s->user); free(s); }
}
static const char *readiness_name(void *state) {
    readiness_state *s = (readiness_state *)state; return s && s->name ? s->name : "readiness";
}
static const cmeta_type_desc *readiness_type(void *state) {
    readiness_state *s = (readiness_state *)state; return s ? s->type : NULL;
}
CMETA_IMPLEMENTS(cflow_source, readiness_source, 0,
    .name = readiness_name,
    .output_type = readiness_type,
    .resume = readiness_resume,
    .cancel = readiness_cancel,
    .destroy = readiness_destroy,
    .bind_terminal_waker = source_no_terminal_bind,
    .poll_terminal = source_open_terminal
);

bool cflow_source_from_readiness(cflow_source *out,
                                 const char *name,
                                 const cmeta_type_desc *type,
                                 cflow_read_fn read,
                                 cflow_watch_fn arm,
                                 cflow_unwatch_fn cancel,
                                 cflow_resource_close_fn close,
                                 void *user) {
    if (!out || !type || !read || !arm) return false;
    readiness_state *s = calloc(1, sizeof(*s)); if (!s) return false;
    s->name = name ? name : "readiness"; s->type = type;
    s->read = read; s->arm = arm; s->cancel = cancel; s->close = close; s->user = user;
    *out = readiness_source_as_cflow_source(s);
    return true;
}
