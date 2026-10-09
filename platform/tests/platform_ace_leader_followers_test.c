/* #1064: test-local, opt-in Leader/Followers over caller-owned CPU events.
 * This intentionally does not export an ABI or move CNet SG/NativeIO ownership.
 * All event values are copies; the caller must join producers before destroy.
 */
#include <salts/thread.h>
#include "tinytest.h"
#include "platform_ace_leader_followers_types.h"
#include <string.h>

enum { LF_MAX_WORKERS = 4, LF_CAPACITY = 2, LF_EVENTS = 96 };
typedef struct lf_pool lf_pool;
typedef struct lf_worker {
  lf_pool *pool;
  int id;
  cmeta_thread_t thread;
  enum lf_role role;
  enum lf_stop stop;
} lf_worker;

struct lf_pool {
  cmeta_mutex_t mu;
  cmeta_cond_t changed;
  lf_worker workers[LF_MAX_WORKERS];
  lf_handler_fn handler;
  void *handler_context; /* Borrowed: valid until all workers are joined. */
  int queue[LF_CAPACITY];
  int accepted_order[LF_EVENTS], dequeued_order[LF_EVENTS];
  int observed[LF_EVENTS], worker_of[LF_EVENTS], successor_of[LF_EVENTS];
  enum lf_result result[LF_EVENTS];
  int head, tail, queued, active, peak_active;
  int worker_count, started, ready, stopped, joined;
  int leader, last_leader, promotions, handoffs, invariant_failures;
  int accepted, dequeued, settled, rejected, space_waiters;
  int cancelled_workers, failed_workers;
  int closing, dispatch_paused;
  int hold_event, hold_entered, hold_release;
  int fail_event, cancel_event;
};

/* The sole election authority. Call with mu held. A leader is a thread
 * actually waiting for/claiming a source event, never a callback executor. */
static void lf_elect_locked(lf_pool *p) {
  if (p->leader >= 0) return;
  for (int step = 1; step <= p->worker_count; ++step) {
    const int id = (p->last_leader + step) % p->worker_count;
    lf_worker *candidate = &p->workers[id];
    if (candidate->role != LF_FOLLOWER || candidate->stop != LF_RUN) continue;
    candidate->role = LF_LEADER;
    p->leader = id;
    p->last_leader = id;
    ++p->promotions;
    cmeta_cond_broadcast(&p->changed);
    return;
  }
}

static void lf_verify_locked(lf_pool *p) {
  int leaders = 0, processing = 0;
  for (int i = 0; i < p->worker_count; ++i) {
    leaders += p->workers[i].role == LF_LEADER;
    processing += p->workers[i].role == LF_PROCESSING;
  }
  if (leaders > 1 || leaders != (p->leader >= 0) ||
      (p->leader >= 0 && p->workers[p->leader].role != LF_LEADER) ||
      processing != p->active ||
      p->queued < 0 || p->queued > LF_CAPACITY ||
      p->accepted - p->dequeued != p->queued ||
      p->dequeued - p->settled != p->active)
    ++p->invariant_failures;
}

/* The callback is deliberately invoked outside the election lock. This
 * callback reacquires mu, so accidentally calling it under mu deadlocks. */
static enum lf_result lf_record(void *context, int event) {
  lf_pool *p = (lf_pool *)context;
  cmeta_mutex_lock(&p->mu);
  if (event == p->hold_event) {
    p->hold_entered = 1;
    cmeta_cond_broadcast(&p->changed);
    while (!p->hold_release) cmeta_cond_wait(&p->changed, &p->mu);
  }
  cmeta_mutex_unlock(&p->mu);
  if (event == p->fail_event) return LF_ERROR;
  if (event == p->cancel_event) return LF_CANCELLED;
  return LF_OK;
}

/* Compile-negative native callable checks: incompatible ABI is not implicit. */
typedef int (*lf_wrong_result_fn)(void *, int);
_Static_assert(_Generic(&lf_record, lf_handler_fn: 1, default: 0),
               "Leader/Followers handler must carry exact result signature");
_Static_assert(!_Generic((lf_wrong_result_fn)0, lf_handler_fn: 1, default: 0),
               "Mismatched handler result must be rejected");

static void lf_run(void *context) {
  lf_worker *w = (lf_worker *)context;
  lf_pool *p = w->pool;
  cmeta_mutex_lock(&p->mu);
  w->role = LF_FOLLOWER;
  ++p->ready;
  lf_elect_locked(p);
  lf_verify_locked(p);
  cmeta_cond_broadcast(&p->changed);
  for (;;) {
    /* Cooperative worker stop: never cancel a thread inside the mutex or
     * abandon an accepted callback. A promoted follower takes over first. */
    if (w->stop != LF_RUN || (p->closing && p->queued == 0)) {
      if (p->leader == w->id) p->leader = -1;
      w->role = LF_STOPPED;
      ++p->stopped;
      if (w->stop == LF_STOP_CANCEL) ++p->cancelled_workers;
      if (w->stop == LF_STOP_FAILURE) ++p->failed_workers;
      if (p->queued != 0 || !p->closing) lf_elect_locked(p);
      lf_verify_locked(p);
      cmeta_cond_broadcast(&p->changed);
      cmeta_mutex_unlock(&p->mu);
      return;
    }
    if (w->role == LF_LEADER && p->queued > 0 && !p->dispatch_paused) {
      const int event = p->queue[p->head];
      p->head = (p->head + 1) % LF_CAPACITY;
      --p->queued;
      p->dequeued_order[p->dequeued++] = event;
      w->role = LF_PROCESSING;
      p->leader = -1;
      ++p->active;
      if (p->active > p->peak_active) p->peak_active = p->active;

      /* The baton is explicitly assigned BEFORE this worker enters its
       * callback, rather than releasing the leader slot for a lock race. */
      lf_elect_locked(p);
      p->worker_of[event] = w->id;
      p->successor_of[event] = p->leader;
      if (p->leader >= 0) ++p->handoffs;
      lf_verify_locked(p);
      cmeta_cond_broadcast(&p->changed);
      cmeta_mutex_unlock(&p->mu);

      const enum lf_result status = p->handler(p->handler_context, event);

      cmeta_mutex_lock(&p->mu);
      if (event < 0 || event >= LF_EVENTS || p->observed[event] != 0 ||
          status == LF_PENDING)
        ++p->invariant_failures;
      else {
        ++p->observed[event];
        p->result[event] = status;
      }
      ++p->settled;
      --p->active;
      w->role = LF_FOLLOWER;
      lf_elect_locked(p);
      lf_verify_locked(p);
      cmeta_cond_broadcast(&p->changed);
      continue;
    }
    cmeta_cond_wait(&p->changed, &p->mu);
  }
}

/* Bounded wait for an observable state, never a scheduling sleep. */
static int lf_wait_at_least(lf_pool *p, const int *field, int expected) {
  int ok;
  cmeta_mutex_lock(&p->mu);
  for (int retry = 0; retry < 50 && *field < expected; ++retry)
    (void)cmeta_cond_timedwait(&p->changed, &p->mu, 100000000ULL);
  ok = *field >= expected;
  cmeta_mutex_unlock(&p->mu);
  return ok;
}

static int lf_init(lf_pool *p, int worker_count) {
  if (p == NULL || (worker_count != 1 && worker_count != 2 && worker_count != 4)) return -1;
  memset(p, 0, sizeof(*p));
  p->worker_count = worker_count;
  p->leader = p->last_leader = -1;
  p->hold_event = p->fail_event = p->cancel_event = -1;
  p->handler = lf_record;
  p->handler_context = p;
  cmeta_mutex_init(&p->mu);
  if (p->mu == NULL) return -1;
  cmeta_cond_init(&p->changed);
  if (p->changed == NULL) { cmeta_mutex_destroy(&p->mu); return -1; }
  for (int i = 0; i < worker_count; ++i) {
    p->workers[i].pool = p;
    p->workers[i].id = i;
    if (cmeta_thread_create(&p->workers[i].thread, lf_run, &p->workers[i]) != 0) {
      cmeta_mutex_lock(&p->mu);
      p->closing = 1;
      cmeta_cond_broadcast(&p->changed);
      cmeta_mutex_unlock(&p->mu);
      for (int j = 0; j < p->started; ++j)
        (void)cmeta_thread_join(&p->workers[j].thread);
      cmeta_cond_destroy(&p->changed);
      cmeta_mutex_destroy(&p->mu);
      return -1;
    }
    ++p->started;
  }
  if (!lf_wait_at_least(p, &p->ready, worker_count)) {
    cmeta_mutex_lock(&p->mu);
    p->closing = 1;
    cmeta_cond_broadcast(&p->changed);
    cmeta_mutex_unlock(&p->mu);
    for (int i = 0; i < p->started; ++i)
      (void)cmeta_thread_join(&p->workers[i].thread);
    cmeta_cond_destroy(&p->changed);
    cmeta_mutex_destroy(&p->mu);
    return -1;
  }
  return 0;
}

/* Non-blocking bounded admission. False means caller retains its value.
 * Every accepted value is copied and must settle exactly once. */
static int lf_submit(lf_pool *p, int event, int wait_for_space) {
  int ok = 0;
  cmeta_mutex_lock(&p->mu);
  if (event >= 0 && event < LF_EVENTS) {
    while (wait_for_space && !p->closing && p->queued == LF_CAPACITY) {
      ++p->space_waiters;
      cmeta_cond_broadcast(&p->changed);
      cmeta_cond_wait(&p->changed, &p->mu);
      --p->space_waiters;
    }
    if (!p->closing && p->queued < LF_CAPACITY && p->accepted < LF_EVENTS) {
      p->queue[p->tail] = event;
      p->tail = (p->tail + 1) % LF_CAPACITY;
      ++p->queued;
      p->accepted_order[p->accepted++] = event;
      ok = 1;
    }
  }
  if (!ok) ++p->rejected;
  lf_verify_locked(p);
  cmeta_cond_broadcast(&p->changed);
  cmeta_mutex_unlock(&p->mu);
  return ok;
}

static void lf_pause(lf_pool *p, int paused) {
  cmeta_mutex_lock(&p->mu);
  p->dispatch_paused = paused;
  cmeta_cond_broadcast(&p->changed);
  cmeta_mutex_unlock(&p->mu);
}

static void lf_release_hold(lf_pool *p) {
  cmeta_mutex_lock(&p->mu);
  p->hold_release = 1;
  cmeta_cond_broadcast(&p->changed);
  cmeta_mutex_unlock(&p->mu);
}

/* Cancellation/failure injection acts at safe points. Refuse removing the
 * final service worker while the source is still open or work is unsettled. */
static int lf_request_stop(lf_pool *p, int worker_id, enum lf_stop reason) {
  int other = 0;
  cmeta_mutex_lock(&p->mu);
  if (worker_id < 0 || worker_id >= p->worker_count || reason == LF_RUN ||
      p->workers[worker_id].role == LF_STOPPED ||
      p->workers[worker_id].stop != LF_RUN) {
    cmeta_mutex_unlock(&p->mu);
    return 0;
  }
  for (int i = 0; i < p->worker_count; ++i)
    if (i != worker_id && p->workers[i].role != LF_STOPPED &&
        p->workers[i].stop == LF_RUN) ++other;
  if (other == 0 && (!p->closing || p->queued + p->active != 0)) {
    cmeta_mutex_unlock(&p->mu);
    return 0;
  }
  p->workers[worker_id].stop = reason;
  cmeta_cond_broadcast(&p->changed);
  cmeta_mutex_unlock(&p->mu);
  return 1;
}

static int lf_close(lf_pool *p) {
  cmeta_mutex_lock(&p->mu);
  p->closing = 1;
  p->dispatch_paused = 0; /* A paused test ingress must still drain. */
  cmeta_cond_broadcast(&p->changed);
  cmeta_mutex_unlock(&p->mu);
  for (int i = 0; i < p->started; ++i)
    if (cmeta_thread_join(&p->workers[i].thread) != 0) return -1;
  p->joined = 1;
  return 0;
}

static int lf_destroy(lf_pool *p) {
  if (!p->joined || p->queued != 0 || p->active != 0 || p->stopped != p->started)
    return -1;
  cmeta_cond_destroy(&p->changed);
  cmeta_mutex_destroy(&p->mu);
  return 0;
}

/* Each producer admits disjoint event identifiers using explicit bounded
 * backpressure, with no executor-internal retry or dropped accepted event. */
typedef struct lf_producer {
  lf_pool *pool;
  int first, count, admitted, rejected;
} lf_producer;
static void lf_produce(void *context) {
  lf_producer *producer = (lf_producer *)context;
  for (int i = 0; i < producer->count; ++i) {
    const int accepted = lf_submit(producer->pool, producer->first + i, 1);
    producer->admitted += accepted;
    producer->rejected += !accepted;
  }
}
typedef struct lf_closer { lf_pool *pool; int status; } lf_closer;
static void lf_close_thread(void *context) {
  lf_closer *closer = (lf_closer *)context;
  closer->status = lf_close(closer->pool);
}

static int lf_assert_settled(lf_pool *p, int expected) {
  if (p->accepted != expected || p->dequeued != expected ||
      p->settled != expected || p->queued != 0 || p->active != 0 ||
      p->leader != -1 || p->stopped != p->started ||
      p->invariant_failures != 0) return 0;
  for (int i = 0; i < expected; ++i)
    if (p->accepted_order[i] != p->dequeued_order[i] ||
        p->observed[p->accepted_order[i]] != 1) return 0;
  return 1;
}

spec("ACE genuine optional Leader Followers: bounded CPU source") {
  it("rejects invalid topology and preserves caller-owned close preconditions") {
    lf_pool p;
    check_equal(lf_init(&p, 0), -1);
    check_equal(lf_init(&p, 3), -1);
    check_equal(lf_init(&p, 1), 0);
    check_equal(lf_destroy(&p), -1);
    check_equal(lf_request_stop(&p, 0, LF_STOP_CANCEL), 0);
    check_equal(lf_close(&p), 0);
    check_equal(lf_destroy(&p), 0);
  }
  it("drains 32 admitted events in FIFO order with 1 2 and 4 workers") {
    const int counts[] = { 1, 2, 4 };
    for (int n = 0; n < 3; ++n) {
      lf_pool p;
      check_equal(lf_init(&p, counts[n]), 0);
      for (int i = 0; i < 32; ++i) check_equal(lf_submit(&p, i, 1), 1);
      check_equal(lf_close(&p), 0);
      check_equal(lf_submit(&p, 33, 0), 0);
      check_equal(lf_assert_settled(&p, 32), 1);
      check_equal(p.rejected, 1);
      check_equal(lf_destroy(&p), 0);
    }
  }
  it("elects an explicit successor before a leader enters its blocked handler") {
    const int counts[] = { 2, 4 };
    for (int n = 0; n < 2; ++n) {
      lf_pool p;
      check_equal(lf_init(&p, counts[n]), 0);
      p.hold_event = 0;
      lf_pause(&p, 1);
      check_equal(lf_submit(&p, 0, 0), 1);
      check_equal(lf_submit(&p, 1, 0), 1);
      check_equal(lf_submit(&p, 2, 0), 0); /* capacity is bounded */
      lf_pause(&p, 0);
      check_equal(lf_wait_at_least(&p, &p.hold_entered, 1), 1);
      check_equal(lf_wait_at_least(&p, &p.settled, 1), 1);
      cmeta_mutex_lock(&p.mu);
      const int handed_off = p.successor_of[0] >= 0 &&
          p.successor_of[0] != p.worker_of[0] &&
          p.worker_of[0] != p.worker_of[1] &&
          p.handoffs >= 1 && p.observed[0] == 0 && p.observed[1] == 1;
      cmeta_mutex_unlock(&p.mu);
      check_equal(handed_off, 1);
      lf_release_hold(&p);
      check_equal(lf_close(&p), 0);
      check_equal(lf_assert_settled(&p, 2), 1);
      check_equal(lf_destroy(&p), 0);
    }
  }
  it("settles 3 concurrent producers without lost or duplicate events") {
    lf_pool p;
    lf_producer producers[3];
    cmeta_thread_t threads[3] = { NULL, NULL, NULL };
    check_equal(lf_init(&p, 4), 0);
    for (int i = 0; i < 3; ++i) {
      producers[i] = (lf_producer){ &p, i * 24, 24, 0, 0 };
      check_equal(cmeta_thread_create(&threads[i], lf_produce, &producers[i]), 0);
    }
    for (int i = 0; i < 3; ++i) check_equal(cmeta_thread_join(&threads[i]), 0);
    check_equal(lf_close(&p), 0);
    check_equal(lf_assert_settled(&p, 72), 1);
    for (int i = 0; i < 3; ++i) {
      check_equal(producers[i].admitted, 24);
      check_equal(producers[i].rejected, 0);
    }
    check_equal(lf_destroy(&p), 0);
  }
  it("rejects waiting producer atomically against close and drains accepted work") {
    lf_pool p;
    lf_producer producer;
    cmeta_thread_t producing = NULL, closing = NULL;
    lf_closer closer;
    check_equal(lf_init(&p, 2), 0);
    lf_pause(&p, 1);
    check_equal(lf_submit(&p, 0, 0), 1);
    check_equal(lf_submit(&p, 1, 0), 1);
    producer = (lf_producer){ &p, 2, 1, 0, 0 };
    check_equal(cmeta_thread_create(&producing, lf_produce, &producer), 0);
    check_equal(lf_wait_at_least(&p, &p.space_waiters, 1), 1);
    closer = (lf_closer){ &p, -1 };
    check_equal(cmeta_thread_create(&closing, lf_close_thread, &closer), 0);
    check_equal(cmeta_thread_join(&producing), 0);
    check_equal(cmeta_thread_join(&closing), 0);
    check_equal(closer.status, 0);
    check_equal(producer.admitted, 0);
    check_equal(producer.rejected, 1);
    check_equal(lf_submit(&p, 3, 0), 0);
    check_equal(lf_assert_settled(&p, 2), 1);
    check_equal(lf_destroy(&p), 0);
  }
  it("hands off cancelled and failed leaders and settles handler error/cancel terminals") {
    lf_pool p;
    check_equal(lf_init(&p, 4), 0);
    p.fail_event = 10;
    p.cancel_event = 11;
    cmeta_mutex_lock(&p.mu);
    const int first_leader = p.leader;
    cmeta_mutex_unlock(&p.mu);
    check_equal(lf_request_stop(&p, first_leader, LF_STOP_CANCEL), 1);
    check_equal(lf_wait_at_least(&p, &p.cancelled_workers, 1), 1);
    cmeta_mutex_lock(&p.mu);
    const int second_leader = p.leader;
    cmeta_mutex_unlock(&p.mu);
    check_equal(lf_request_stop(&p, second_leader, LF_STOP_FAILURE), 1);
    check_equal(lf_wait_at_least(&p, &p.failed_workers, 1), 1);
    for (int i = 10; i < 14; ++i) check_equal(lf_submit(&p, i, 1), 1);
    check_equal(lf_close(&p), 0);
    check_equal(p.accepted, 4);
    check_equal(p.result[10], LF_ERROR);
    check_equal(p.result[11], LF_CANCELLED);
    check_equal(p.result[12], LF_OK);
    check_equal(p.result[13], LF_OK);
    check_equal(p.invariant_failures, 0);
    check_equal(p.stopped, 4);
    for (int i = 10; i < 14; ++i) check_equal(p.observed[i], 1);
    check_equal(lf_destroy(&p), 0);
  }
  it("defers an active worker fault until callback settlement while its successor proceeds") {
    lf_pool p;
    check_equal(lf_init(&p, 2), 0);
    p.hold_event = 0;
    check_equal(lf_submit(&p, 0, 0), 1);
    check_equal(lf_wait_at_least(&p, &p.hold_entered, 1), 1);
    cmeta_mutex_lock(&p.mu);
    const int worker = p.worker_of[0];
    cmeta_mutex_unlock(&p.mu);
    check_equal(lf_request_stop(&p, worker, LF_STOP_FAILURE), 1);
    check_equal(lf_submit(&p, 1, 1), 1);
    check_equal(lf_wait_at_least(&p, &p.settled, 1), 1);
    lf_release_hold(&p);
    check_equal(lf_close(&p), 0);
    check_equal(p.failed_workers, 1);
    check_equal(p.observed[0], 1);
    check_equal(p.observed[1], 1);
    check_equal(p.invariant_failures, 0);
    check_equal(lf_destroy(&p), 0);
  }
}
