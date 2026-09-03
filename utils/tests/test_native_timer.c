#include "platform.h"
#include "tinytest.h"
#include "salts_thread.h"
#include <stdatomic.h>
static int count = 0;
static atomic_int destroy_race_count = 0;

static void on_timer_tick(salts_timer_t *timer) {
  int *c = (int *)salts_timer_get_data(timer);
  if (c) {
    (*c)++;
    if (*c >= 5) {
      salts_timer_stop(timer);
    }
  }
}

static void on_oneshot_timer(salts_timer_t *timer) {
  int *c = (int *)salts_timer_get_data(timer);
  if (c) {
    (*c)++;
  }
}

static void on_destroy_race_timer(salts_timer_t *timer) {
  atomic_int *counter = (atomic_int *)salts_timer_get_data(timer);

  if (counter != NULL) {
    atomic_fetch_add(counter, 1);
  }
  salts_sleep_ms(1);
}

spec("Native Timer Tests") {

  it("should handle repeating native timers") {
    count = 0;
    salts_timer_t *repeating = salts_timer_create(NULL);
    check_not_null(repeating);

    salts_timer_set_data(repeating, &count);
    salts_timer_start(repeating, on_timer_tick, 100, 100);

    salts_sleep_ms(1000); // Wait for ticks

    check_greater_equal(count, 5);
    salts_timer_destroy(repeating);
  }

  it("should handle one-shot native timers") {
    count = 0;
    salts_timer_t *oneshot = salts_timer_create(NULL);
    check_not_null(oneshot);

    salts_timer_set_data(oneshot, &count);
    salts_timer_start(oneshot, on_oneshot_timer, 100, 0);

    salts_sleep_ms(300); // Wait for firing

    check_equal(count, 1);
    salts_timer_destroy(oneshot);
  }

  it("should tolerate destroy racing with one-shot expiry") {
    atomic_store(&destroy_race_count, 0);

    for (int i = 0; i < 256; ++i) {
      salts_timer_t *timer = salts_timer_create(NULL);

      check_not_null(timer);
      salts_timer_set_data(timer, &destroy_race_count);
      check_equal(salts_timer_start(timer, on_destroy_race_timer, 1, 0), 0);
      salts_sleep_ms(1);
      salts_timer_destroy(timer);
    }

    check_greater_equal(atomic_load(&destroy_race_count), 0);
  }
}
