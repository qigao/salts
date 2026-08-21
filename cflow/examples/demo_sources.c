#include <cflow/runtime.h>
#include <cflow/sources.h>
#include <cflow/stream.h>
#include "ops.h"

#include <stdio.h>
#include <string.h>

typedef struct obs_state {
    long values[16];
    size_t count;
    bool done;
    const char *error;
} obs_state;

static bool on_long(void *user, const cmeta_type_desc *type, const void *value) {
    obs_state *s = (obs_state *)user;
    if (!cmeta_type_equal(type, &cmeta_type_long) || s->count >= 16) return false;
    s->values[s->count++] = *(const long *)value;
    return true;
}
static void on_error(void *user, const char *msg) { ((obs_state *)user)->error = msg; }
static void on_done(void *user) { ((obs_state *)user)->done = true; }

static bool on_size(void *user, const cmeta_type_desc *type, const void *value) {
    obs_state *s = (obs_state *)user;
    if (!cmeta_type_equal(type, &cmeta_type_size) || s->count >= 16) return false;
    s->values[s->count++] = (long)*(const size_t *)value;
    return true;
}

typedef struct mock_io {
    cflow_scheduler *scheduler;
    cflow_task_id task;
    cflow_waker waker;
    int values[3];
    size_t index;
    bool ready;
} mock_io;

static void io_ready_task(void *user) {
    mock_io *io = (mock_io *)user;
    io->task = 0; io->ready = true;
    cflow_waker w = io->waker; io->waker = (cflow_waker){0};
    if (w.wake) w.wake(w.user);
}
static cflow_read_status io_read(void *user, void *out, const char **error) {
    (void)error;
    mock_io *io = (mock_io *)user;
    if (io->index >= 3) return CFLOW_READ_DONE;
    if (!io->ready) return CFLOW_READ_WOULD_BLOCK;
    io->ready = false;
    *(int *)out = io->values[io->index++];
    return io->index == 3 ? CFLOW_READ_VALUE_AND_DONE : CFLOW_READ_VALUE;
}
static bool io_arm(void *user, cflow_waker w) {
    mock_io *io = (mock_io *)user;
    if (io->task) return false;
    io->waker = w;
    io->task = cflow_scheduler_post_after(io->scheduler, 3, io_ready_task, io);
    return io->task != 0;
}
static void io_unwatch(void *user) {
    mock_io *io = (mock_io *)user;
    if (io->task) { (void)cflow_scheduler_cancel(io->scheduler, io->task); io->task = 0; }
    io->waker = (cflow_waker){0};
}

int main(void) {
    cflow_sink_callbacks sink_cb;
    cflow_sink sink;
    cflow_scheduler sched;
    cflow_source source;
    cflow_run run = {0};
    obs_state st = {0};

    /* Timer is just another source: WAIT(timer) -> wake -> VALUE. */
    cflow_graph timer_graph;
    cflow_graph_init(&timer_graph, &cmeta_type_size);
    cflow_scheduler_test_init(&sched);
    cflow_source_from_timer(&source, 3, 5);
    sink_cb = (cflow_sink_callbacks){ on_size, on_error, on_done, &st };
    sink = cflow_sink_from_callbacks(&sink_cb);
    if (!cflow_run_open(&run, &timer_graph, &source, &sched, &sink)) return 1;
    if (!cflow_run_request(&run, 3)) return 2;
    (void)cflow_scheduler_run_ready(&sched); /* arms first timer */
    (void)cflow_scheduler_advance(&sched, 5);
    (void)cflow_scheduler_advance(&sched, 5);
    (void)cflow_scheduler_advance(&sched, 5);
    (void)cflow_scheduler_run_ready(&sched);
    if (!st.done || st.error || st.count != 3 || st.values[0] != 0 || st.values[2] != 2) return 3;
    printf("timer source: %ld %ld %ld\n", st.values[0], st.values[1], st.values[2]);
    cflow_run_close(&run); cflow_scheduler_destroy(&sched); cflow_graph_destroy(&timer_graph);

    /* Channel resource lives outside the Graph; its receive side is a source. */
    memset(&st, 0, sizeof st);
    cflow_channel ch = {0};
    cflow_stream stream;
    cflow_stream_init(&stream, &cmeta_type_int);
    stream.map(&stream, square);
    cflow_scheduler_test_init(&sched);
    if (!cflow_channel_init(&ch, &cmeta_type_int, 4)) return 4;
    if (!cflow_source_from_channel(&source, &ch)) return 5;
    sink_cb = (cflow_sink_callbacks){ on_long, on_error, on_done, &st };
    sink = cflow_sink_from_callbacks(&sink_cb);
    if (!cflow_run_open(&run, &stream.graph, &source, &sched, &sink)) return 6;
    cflow_run_request(&run, 2); (void)cflow_scheduler_run_ready(&sched);
    int a = 3, b = 4;
    cflow_channel_push(&ch, &a); cflow_channel_push(&ch, &b); cflow_channel_close(&ch);
    (void)cflow_scheduler_run_until_idle(&sched, 0);
    if (!st.done || st.error || st.count != 2 || st.values[0] != 9 || st.values[1] != 16) return 7;
    printf("channel source -> graph: %ld %ld\n", st.values[0], st.values[1]);
    cflow_run_close(&run); cflow_channel_destroy(&ch); cflow_scheduler_destroy(&sched); cflow_stream_destroy(&stream);

    /* Terminal signals are not backpressured: close after exact demand still completes. */
    memset(&st, 0, sizeof st);
    cflow_stream hot_stream;
    cflow_stream_init(&hot_stream, &cmeta_type_int);
    hot_stream.map(&hot_stream, square);
    cflow_scheduler_test_init(&sched);
    cflow_channel_init(&ch, &cmeta_type_int, 2);
    cflow_source_from_channel(&source, &ch);
    sink_cb = (cflow_sink_callbacks){ on_long, on_error, on_done, &st };
    sink = cflow_sink_from_callbacks(&sink_cb);
    if (!cflow_run_open(&run, &hot_stream.graph, &source, &sched, &sink)) return 71;
    cflow_run_request(&run, 1); (void)cflow_scheduler_run_ready(&sched);
    int only = 6; cflow_channel_push(&ch, &only);
    (void)cflow_scheduler_run_until_idle(&sched, 0);
    if (st.count != 1 || st.done) return 72;
    cflow_channel_close(&ch);
    (void)cflow_scheduler_run_until_idle(&sched, 0);
    if (!st.done || st.values[0] != 36) return 73;
    printf("hot channel terminal: complete is not demand-gated\n");
    cflow_run_close(&run); cflow_channel_destroy(&ch); cflow_scheduler_destroy(&sched); cflow_stream_destroy(&hot_stream);

    /* Generic readiness adapter is the IO/UI boundary: runtime sees only WAIT. */
    memset(&st, 0, sizeof st);
    cflow_stream io_stream;
    cflow_stream_init(&io_stream, &cmeta_type_int);
    io_stream.map(&io_stream, square);
    cflow_scheduler_test_init(&sched);
    mock_io io = { &sched, 0, {0}, {2,3,5}, 0, false };
    if (!cflow_source_from_readiness(&source, "mock-fd", &cmeta_type_int,
                                     io_read, io_arm, io_unwatch, NULL, &io)) return 8;
    sink_cb = (cflow_sink_callbacks){ on_long, on_error, on_done, &st };
    sink = cflow_sink_from_callbacks(&sink_cb);
    if (!cflow_run_open(&run, &io_stream.graph, &source, &sched, &sink)) return 9;
    cflow_run_request(&run, 3);
    (void)cflow_scheduler_run_ready(&sched);
    (void)cflow_scheduler_advance(&sched, 3);
    (void)cflow_scheduler_advance(&sched, 3);
    (void)cflow_scheduler_advance(&sched, 3);
    (void)cflow_scheduler_run_ready(&sched);
    if (!st.done || st.error || st.count != 3 || st.values[0] != 4 || st.values[2] != 25) return 10;
    printf("readiness(IO/UI) source -> graph: %ld %ld %ld\n",
           st.values[0], st.values[1], st.values[2]);
    cflow_run_close(&run); cflow_scheduler_destroy(&sched); cflow_stream_destroy(&io_stream);
    return 0;
}
