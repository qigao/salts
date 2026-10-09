/* #1064: opt-in, test-local Leader/Followers CPU event executor.
 * Events are owned int values; never carry SG endpoints or borrowed pointers.
 * Deliberately no installed/public ABI until consumer and DSO gates qualify. */
#include <salts/thread.h>
#include "tinytest.h"
#include <string.h>

enum { LF_MAX_WORKERS = 4, LF_CAPACITY = 2, LF_EVENTS = 32 };
typedef struct lf_pool lf_pool;
typedef struct lf_worker { lf_pool *pool; int index; cmeta_thread_t thread; } lf_worker;
struct lf_pool {
  cmeta_mutex_t mu;
  cmeta_cond_t ready;
  lf_worker worker[LF_MAX_WORKERS];
  int buffer[LF_CAPACITY];
  int observed[LF_EVENTS];
  int head, tail, queued, active, leader, finished, rejected;
  int worker_count, started, closing, failures, handoffs, peak_active;
};
static void lf_run(void *arg) {
  lf_worker *w = (lf_worker *)arg;
  lf_pool *p = w->pool;
  for (;;) {
    int value;
    cmeta_mutex_lock(&p->mu);
    while (!p->closing && (p->queued == 0 || (p->leader != -1 && p->leader != w->index)))
      cmeta_cond_wait(&p->ready, &p->mu);
    if (p->queued == 0 && p->closing) {
      if (p->leader == w->index) p->leader = -1;
      cmeta_cond_broadcast(&p->ready);
      cmeta_mutex_unlock(&p->mu);
      return;
    }
    if (p->leader == -1) p->leader = w->index;
    if (p->leader != w->index) {
      cmeta_mutex_unlock(&p->mu);
      continue;
    }
    value = p->buffer[p->head];
    p->head = (p->head + 1) % LF_CAPACITY;
    --p->queued;
    ++p->active;
    if (p->active > p->peak_active) p->peak_active = p->active;
    /* Baton: successor elected by exactly one thread under the mutex,
       BEFORE callback processing; the next contender claims vacant leader. */
    p->leader = -1;
    ++p->handoffs;
    cmeta_cond_broadcast(&p->ready);
    cmeta_mutex_unlock(&p->mu);

    /* Simulate an owned CPU-only event callback, never holding election mutex. */
    cmeta_thread_yield();

    cmeta_mutex_lock(&p->mu);
    if (value < 0 || value >= LF_EVENTS || p->observed[value] != 0)
      ++p->failures;
    else
      ++p->observed[value];
    ++p->finished;
    --p->active;
    cmeta_cond_broadcast(&p->ready);
    cmeta_mutex_unlock(&p->mu);
  }
}
static int lf_init(lf_pool *p, int workers) {
  if (p == NULL || (workers != 1 && workers != 2 && workers != 4)) return -1;
  memset(p, 0, sizeof(*p));
  p->leader = -1;
  p->worker_count = workers;
  cmeta_mutex_init(&p->mu);
  if (p->mu == NULL) return -1;
  cmeta_cond_init(&p->ready);
  if (p->ready == NULL) { cmeta_mutex_destroy(&p->mu); return -1; }
  for (int i = 0; i < workers; ++i) {
    p->worker[i].pool = p;
    p->worker[i].index = i;
    if (cmeta_thread_create(&p->worker[i].thread, lf_run, &p->worker[i]) != 0) {
      cmeta_mutex_lock(&p->mu);
      p->closing = 1;
      cmeta_cond_broadcast(&p->ready);
      cmeta_mutex_unlock(&p->mu);
      for (int j = 0; j < p->started; ++j) (void)cmeta_thread_join(&p->worker[j].thread);
      cmeta_cond_destroy(&p->ready);
      cmeta_mutex_destroy(&p->mu);
      return -1;
    }
    ++p->started;
  }
  return 0;
}
static int lf_submit(lf_pool *p, int event) {
  int ok = 0;
  cmeta_mutex_lock(&p->mu);
  if (!p->closing && p->queued < LF_CAPACITY) {
    p->buffer[p->tail] = event;
    p->tail = (p->tail + 1) % LF_CAPACITY;
    ++p->queued;
    ok = 1;
    cmeta_cond_broadcast(&p->ready);
  } else ++p->rejected;
  cmeta_mutex_unlock(&p->mu);
  return ok;
}
/* Finish-accepted close: stop admission and allow all queued/active work to
 * settle before joining and reclaiming caller-owned pool storage. */
static void lf_close(lf_pool *p) {
  cmeta_mutex_lock(&p->mu);
  p->closing = 1;
  cmeta_cond_broadcast(&p->ready);
  cmeta_mutex_unlock(&p->mu);
  for (int i = 0; i < p->started; ++i) (void)cmeta_thread_join(&p->worker[i].thread);
}
static void lf_destroy(lf_pool *p) {
  cmeta_cond_destroy(&p->ready);
  cmeta_mutex_destroy(&p->mu);
}
spec("ACE opt-in Leader Followers CPU topology") {
  it("rejects invalid worker topology") {
    lf_pool p;
    check_equal(lf_init(&p, 0), -1);
    check_equal(lf_init(&p, 3), -1);
  }
  it("settles accepted bounded events for 1 2 and 4 workers") {
    const int counts[] = {1, 2, 4};
    for (int n = 0; n < 3; ++n) {
      lf_pool p;
      int accepted[LF_EVENTS] = {0};
      int total = 0;
      check_equal(lf_init(&p, counts[n]), 0);
      for (int i = 0; i < LF_EVENTS; ++i) {
        accepted[i] = lf_submit(&p, i);
        total += accepted[i];
      }
      lf_close(&p);
      check_equal(lf_submit(&p, 999), 0);
      check_equal(p.finished, total);
      check_equal(p.queued, 0);
      check_equal(p.active, 0);
      check_equal(p.leader, -1);
      check_equal(p.failures, 0);
      check_equal(p.handoffs, total);
      for (int i = 0; i < LF_EVENTS; ++i)
        check_equal(p.observed[i], accepted[i]);
      lf_destroy(&p);
    }
  }
}
