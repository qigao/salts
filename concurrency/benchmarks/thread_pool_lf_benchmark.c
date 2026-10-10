#include <salts/thread_pool.h>
#include <salts/thread.h>
#include <tinytest.h>
#include <fmt.h>
#include <tstr.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

enum { MESSAGES = 1024, SAMPLES = 8, MAX_PRODUCERS = 4, CAPACITY = 256 };
typedef struct job {
  const unsigned char *payload;
  size_t size;
  uint64_t expected, actual;
  atomic_int runs, finals;
} job;
typedef struct producer {
  cmeta_thread_t thread;
  size_t id;
} producer;
static cmeta_threadpool_t *pool;
static cmeta_mutex_t lock;
static cmeta_cond_t changed;
static unsigned char *payloads;
static job jobs[MESSAGES];
static producer senders[MAX_PRODUCERS];
static size_t producer_count, created, epoch, done;
static bool stopping;
static atomic_int errors;
static tstr title;

/* Read every byte. This measures borrowed CPU input, not a payload copy path. */
static uint64_t checksum(const unsigned char *bytes, size_t size) {
  uint64_t value = UINT64_C(14695981039346656037);
  for (size_t n = 0; n < size; ++n)
    value = (value ^ bytes[n]) * UINT64_C(1099511628211);
  return value;
}
static void run(void *arg) {
  job *j = arg;
  j->actual = checksum(j->payload, j->size);
  if (j->actual != j->expected) atomic_fetch_add(&errors, 1);
  atomic_fetch_add(&j->runs, 1);
}
static void finalize(void *arg) {
  job *j = arg;
  atomic_fetch_add(&j->finals, 1);
}
static void admit(size_t id) {
  for (size_t n = id; n < MESSAGES; n += producer_count) {
    const cmeta_threadpool_task_t task = {run, NULL, finalize, &jobs[n]};
    if (cmeta_threadpool_submit_task(pool, &task) != SALTS_OK)
      atomic_fetch_add(&errors, 1);
  }
}
static void produce(void *arg) {
  producer *p = arg;
  size_t seen = 0;
  cmeta_mutex_lock(&lock);
  for (;;) {
    while (!stopping && epoch == seen) cmeta_cond_wait(&changed, &lock);
    if (stopping) break;
    seen = epoch;
    cmeta_mutex_unlock(&lock);
    admit(p->id);
    cmeta_mutex_lock(&lock);
    ++done;
    cmeta_cond_broadcast(&changed);
  }
  cmeta_mutex_unlock(&lock);
}
static void sample(void) {
  if (producer_count == 1) admit(0);
  else {
    cmeta_mutex_lock(&lock);
    done = 0;
    ++epoch;
    cmeta_cond_broadcast(&changed);
    while (done != producer_count) cmeta_cond_wait(&changed, &lock);
    cmeta_mutex_unlock(&lock);
  }
  if (cmeta_threadpool_wait_status(pool) != SALTS_OK)
    atomic_fetch_add(&errors, 1);
}
static void cleanup(void) {
  if (lock != NULL) {
    cmeta_mutex_lock(&lock);
    stopping = true;
    cmeta_cond_broadcast(&changed);
    cmeta_mutex_unlock(&lock);
  }
  for (size_t n = 0; n < created; ++n)
    if (cmeta_thread_join(&senders[n].thread) != 0) abort();
  created = 0;
  cmeta_threadpool_destroy(pool);
  pool = NULL;
  cmeta_cond_destroy(&changed);
  cmeta_mutex_destroy(&lock);
  free(payloads);
  payloads = NULL;
  tstr_free(title);
  title = NULL;
}
static void setup(bool lf, int workers, size_t batch, size_t size, size_t producers) {
  producer_count = producers;
  epoch = done = 0;
  stopping = false;
  atomic_store(&errors, 0);
  payloads = malloc(MESSAGES * size);
  check_not_null(payloads);
  for (size_t n = 0; n < MESSAGES; ++n) {
    unsigned char *bytes = payloads + n * size;
    for (size_t b = 0; b < size; ++b) bytes[b] = (unsigned char)(n * 37 + b * 13);
    jobs[n].payload = bytes;
    jobs[n].size = size;
    jobs[n].expected = checksum(bytes, size);
    jobs[n].actual = 0;
    atomic_store(&jobs[n].runs, 0);
    atomic_store(&jobs[n].finals, 0);
  }
  if (lf) {
    const cmeta_threadpool_lf_config_t config = {
        sizeof(config), SALTS_THREADPOOL_LF_VERSION, workers, CAPACITY, batch};
    check_equal(cmeta_threadpool_create_leader_followers(&config, &pool), SALTS_OK);
  } else {
    const cmeta_threadpool_config_t config = {workers, CAPACITY};
    pool = cmeta_threadpool_create_with_config(&config);
    check_not_null(pool);
  }
  cmeta_mutex_init(&lock);
  cmeta_cond_init(&changed);
  check_not_null(lock);
  check_not_null(changed);
  if (producers != 1)
    for (size_t n = 0; n < producers; ++n) {
      senders[n].id = n;
      check_equal(cmeta_thread_create(&senders[n].thread, produce, &senders[n]), 0);
      ++created;
    }
  title = tstr_format("{} p={} w={} batch={} bytes={}",
      lf ? "LF pool" : "Default pool", producers, workers, batch, size);
  check_not_null(title);
}

suite("Production CPU pool bounded admission through finalize") {
  after_each() { cleanup(); }
  bench("compares the public default and LF backends with a small dataset") {
    const size_t sizes[] = {16, 1024, 65536};
    for (size_t s = 0; s < 3; ++s)
      for (size_t producers = 1; producers <= 4; producers *= 4)
        for (int workers = 1; workers <= 4; workers *= 4)
          for (size_t mode = 0; mode < 3; ++mode) {
            setup(mode != 0, workers, mode == 2 ? 32 : 1, sizes[s], producers);
            sample(); /* One warmup; construction and payload preparation are untimed. */
            benchmark_io(title, SAMPLES, MESSAGES, MESSAGES * sizes[s]) { sample(); }
            check_equal(atomic_load(&errors), 0);
            for (size_t n = 0; n < MESSAGES; ++n) {
              check_equal(atomic_load(&jobs[n].runs), SAMPLES + 1);
              check_equal(atomic_load(&jobs[n].finals), SAMPLES + 1);
              check_equal(jobs[n].actual, jobs[n].expected);
            }
            cmeta_threadpool_stats_t stats;
            cmeta_threadpool_get_stats(pool, &stats);
            check_equal(stats.completed_tasks, (int64_t)((SAMPLES + 1) * MESSAGES));
            check_equal(stats.pending_tasks, (int64_t)0);
            cleanup();
          }
  }
}
