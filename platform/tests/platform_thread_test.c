#include <salts/clock.h>
#include <salts/thread.h>
#include "tinytest.h"
#include <errno.h>
#include <stdatomic.h>

static void set_flag(void *arg) { *(int *)arg = 1; }

enum {
  PLATFORM_TEST_THREAD_COUNT = 4,
  PLATFORM_TEST_YIELD_LIMIT = 100000
};
static const uint64_t PLATFORM_TEST_WAIT_NS =
    5ULL * 1000ULL * 1000ULL * 1000ULL;

typedef struct thread_gate {
  salts_mutex_t mutex;
  salts_cond_t changed;
  int entered;
  int released;
  int completed;
} thread_gate;

typedef struct rwlock_probe {
  salts_rwlock_t lock;
  thread_gate gate;
  atomic_int reader_started;
  atomic_int reader_completed;
  int value;
  int observed;
} rwlock_probe;

typedef struct tls_probe {
  int assigned;
  int observed;
} tls_probe;

static atomic_int once_count;
static salts_once_t once_guard = SALTS_ONCE_INIT;
static SALTS_THREAD_LOCAL int tls_value;

static int thread_gate_init(thread_gate *gate) {
  salts_mutex_init(&gate->mutex);
  salts_cond_init(&gate->changed);
  gate->entered = 0;
  gate->released = 0;
  gate->completed = 0;
  return gate->mutex != NULL && gate->changed != NULL;
}

static void thread_gate_destroy(thread_gate *gate) {
  salts_cond_destroy(&gate->changed);
  salts_mutex_destroy(&gate->mutex);
}

static int thread_gate_wait(thread_gate *gate, int *flag) {
  int rc = 0;
  salts_mutex_lock(&gate->mutex);
  while (!*flag && rc == 0)
    rc = salts_cond_timedwait(&gate->changed, &gate->mutex,
                              PLATFORM_TEST_WAIT_NS);
  salts_mutex_unlock(&gate->mutex);
  return rc;
}

static void detached_gate_worker(void *arg) {
  thread_gate *gate = (thread_gate *)arg;
  salts_mutex_lock(&gate->mutex);
  gate->entered = 1;
  salts_cond_broadcast(&gate->changed);
  while (!gate->released) salts_cond_wait(&gate->changed, &gate->mutex);
  gate->completed = 1;
  salts_cond_broadcast(&gate->changed);
  salts_mutex_unlock(&gate->mutex);
}

static void rwlock_writer(void *arg) {
  rwlock_probe *probe = (rwlock_probe *)arg;
  salts_rwlock_wrlock(&probe->lock);
  probe->value = 42;
  salts_mutex_lock(&probe->gate.mutex);
  probe->gate.entered = 1;
  salts_cond_broadcast(&probe->gate.changed);
  while (!probe->gate.released)
    salts_cond_wait(&probe->gate.changed, &probe->gate.mutex);
  salts_mutex_unlock(&probe->gate.mutex);
  salts_rwlock_wrunlock(&probe->lock);
}

static void rwlock_reader(void *arg) {
  rwlock_probe *probe = (rwlock_probe *)arg;
  atomic_store(&probe->reader_started, 1);
  salts_rwlock_rdlock(&probe->lock);
  probe->observed = probe->value;
  salts_rwlock_rdunlock(&probe->lock);
  atomic_store(&probe->reader_completed, 1);
}

static void count_once(void) { atomic_fetch_add(&once_count, 1); }

static void once_worker(void *arg) {
  (void)arg;
  salts_once(&once_guard, count_once);
}

static void tls_worker(void *arg) {
  tls_probe *probe = (tls_probe *)arg;
  tls_value = probe->assigned;
  salts_thread_yield();
  probe->observed = tls_value;
}

spec("Platform thread primitives") {
  it("creates and joins a thread") {
    salts_thread_t thread;
    int flag = 0;
    check_equal(salts_thread_create(&thread, set_flag, &flag), 0);
    check_equal(salts_thread_join(&thread), 0);
    check_equal(flag, 1);
  }

  it("times condition waits using elapsed duration") {
    salts_mutex_t mutex;
    salts_cond_t cond;
    salts_mutex_init(&mutex);
    salts_cond_init(&cond);
    salts_mutex_lock(&mutex);
    uint64_t before = salts_hrtime();
    int rc = salts_cond_timedwait(&cond, &mutex, 20ULL * 1000000ULL);
    uint64_t elapsed = salts_hrtime() - before;
    salts_mutex_unlock(&mutex);
    check_equal(rc, -ETIMEDOUT);
    check(elapsed >= 10ULL * 1000000ULL);
    check(elapsed < 1000ULL * 1000000ULL);
    salts_cond_destroy(&cond);
    salts_mutex_destroy(&mutex);
  }

  it("keeps detached work alive after destroying its handle") {
    thread_gate gate;
    salts_thread_t thread;
    int rc;

    check_true(thread_gate_init(&gate));
    check_equal(salts_thread_create(&thread, detached_gate_worker, &gate), 0);
    check_equal(thread_gate_wait(&gate, &gate.entered), 0);

    salts_thread_destroy(&thread);
    check_null(thread);

    salts_mutex_lock(&gate.mutex);
    gate.released = 1;
    salts_cond_broadcast(&gate.changed);
    rc = 0;
    while (!gate.completed && rc == 0)
      rc = salts_cond_timedwait(&gate.changed, &gate.mutex,
                                PLATFORM_TEST_WAIT_NS);
    salts_mutex_unlock(&gate.mutex);

    check_equal(rc, 0);
    check_equal(gate.completed, 1);
    thread_gate_destroy(&gate);
  }

  it("serializes a write against a read lock") {
    rwlock_probe probe;
    salts_thread_t writer;
    salts_thread_t reader;
    int reader_started = 0;

    check_equal(salts_rwlock_init(&probe.lock), 0);
    check_true(thread_gate_init(&probe.gate));
    atomic_init(&probe.reader_started, 0);
    atomic_init(&probe.reader_completed, 0);
    probe.value = 0;
    probe.observed = 0;

    check_equal(salts_thread_create(&writer, rwlock_writer, &probe), 0);
    check_equal(thread_gate_wait(&probe.gate, &probe.gate.entered), 0);
    check_equal(salts_thread_create(&reader, rwlock_reader, &probe), 0);
    for (int i = 0; i < PLATFORM_TEST_YIELD_LIMIT && !reader_started; ++i) {
      reader_started = atomic_load(&probe.reader_started);
      salts_thread_yield();
    }

    check_equal(reader_started, 1);
    check_equal(atomic_load(&probe.reader_completed), 0);
    salts_mutex_lock(&probe.gate.mutex);
    probe.gate.released = 1;
    salts_cond_broadcast(&probe.gate.changed);
    salts_mutex_unlock(&probe.gate.mutex);

    check_equal(salts_thread_join(&writer), 0);
    check_equal(salts_thread_join(&reader), 0);
    check_equal(probe.observed, 42);

    thread_gate_destroy(&probe.gate);
    salts_rwlock_destroy(&probe.lock);
  }

  it("runs once initialization exactly once across threads") {
    salts_thread_t threads[PLATFORM_TEST_THREAD_COUNT];

    atomic_store(&once_count, 0);
    for (int i = 0; i < PLATFORM_TEST_THREAD_COUNT; ++i)
      check_equal(salts_thread_create(&threads[i], once_worker, NULL), 0);
    for (int i = 0; i < PLATFORM_TEST_THREAD_COUNT; ++i)
      check_equal(salts_thread_join(&threads[i]), 0);

    check_equal(atomic_load(&once_count), 1);
  }

  it("keeps thread-local values isolated") {
    salts_thread_t first;
    salts_thread_t second;
    tls_probe first_probe = {17, 0};
    tls_probe second_probe = {29, 0};

    tls_value = 0;
    check_equal(salts_thread_create(&first, tls_worker, &first_probe), 0);
    check_equal(salts_thread_create(&second, tls_worker, &second_probe), 0);
    check_equal(salts_thread_join(&first), 0);
    check_equal(salts_thread_join(&second), 0);

    check_equal(first_probe.observed, 17);
    check_equal(second_probe.observed, 29);
    check_equal(tls_value, 0);
  }

  it("reports at least one CPU and accepts yield and sleep") {
    check(salts_cpu_count() > 0);
    salts_thread_yield();
    salts_sleep_ms(0);
  }
}
