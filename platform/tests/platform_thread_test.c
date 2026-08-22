#include <turbo/clock.h>
#include <turbo/thread.h>
#include "tinytest.h"
#include <errno.h>

static void set_flag(void *arg) { *(int *)arg = 1; }

spec("Platform thread primitives") {
  it("creates and joins a thread") {
    turbo_thread_t thread;
    int flag = 0;
    check_equal(turbo_thread_create(&thread, set_flag, &flag), 0);
    check_equal(turbo_thread_join(&thread), 0);
    check_equal(flag, 1);
  }

  it("times condition waits using elapsed duration") {
    turbo_mutex_t mutex;
    turbo_cond_t cond;
    turbo_mutex_init(&mutex);
    turbo_cond_init(&cond);
    turbo_mutex_lock(&mutex);
    uint64_t before = turbo_hrtime();
    int rc = turbo_cond_timedwait(&cond, &mutex, 20ULL * 1000000ULL);
    uint64_t elapsed = turbo_hrtime() - before;
    turbo_mutex_unlock(&mutex);
    check_equal(rc, -ETIMEDOUT);
    check(elapsed >= 10ULL * 1000000ULL);
    check(elapsed < 1000ULL * 1000000ULL);
    turbo_cond_destroy(&cond);
    turbo_mutex_destroy(&mutex);
  }
}
