#include "platform.h"
#include "tinytest.h"
#include "cmeta_thread.h"
#include <stdatomic.h>
static int count = 0;
static atomic_int destroy_race_count = 0;

static void on_timer_tick(cmeta_timer_t *timer) {
  int *c = (int *)cmeta_timer_get_data(timer);
  if (c) {
    (*c)++;
    if (*c >= 5) {
      cmeta_timer_stop(timer);
    }
  }
}

static void on_oneshot_timer(cmeta_timer_t *timer) {
  int *c = (int *)cmeta_timer_get_data(timer);
  if (c) {
    (*c)++;
  }
}

static void on_destroy_race_timer(cmeta_timer_t *timer) {
  atomic_int *counter = (atomic_int *)cmeta_timer_get_data(timer);

  if (counter != NULL) {
    atomic_fetch_add(counter, 1);
  }
  cmeta_sleep_ms(1);
}

spec("Native Timer Tests") {

  it("should handle repeating native timers") {
    count = 0;
    cmeta_timer_t *repeating = cmeta_timer_create(NULL);
    check_not_null(repeating);

    cmeta_timer_set_data(repeating, &count);
    cmeta_timer_start(repeating, on_timer_tick, 100, 100);

    cmeta_sleep_ms(1000); // Wait for ticks

    check_greater_equal(count, 5);
    cmeta_timer_destroy(repeating);
  }

  it("should handle one-shot native timers") {
    count = 0;
    cmeta_timer_t *oneshot = cmeta_timer_create(NULL);
    check_not_null(oneshot);

    cmeta_timer_set_data(oneshot, &count);
    cmeta_timer_start(oneshot, on_oneshot_timer, 100, 0);

    cmeta_sleep_ms(300); // Wait for firing

    check_equal(count, 1);
    cmeta_timer_destroy(oneshot);
  }

  it("should tolerate destroy racing with one-shot expiry") {
    atomic_store(&destroy_race_count, 0);

    for (int i = 0; i < 256; ++i) {
      cmeta_timer_t *timer = cmeta_timer_create(NULL);

      check_not_null(timer);
      cmeta_timer_set_data(timer, &destroy_race_count);
      check_equal(cmeta_timer_start(timer, on_destroy_race_timer, 1, 0), 0);
      cmeta_sleep_ms(1);
      cmeta_timer_destroy(timer);
    }

    check_greater_equal(atomic_load(&destroy_race_count), 0);
  }
}
