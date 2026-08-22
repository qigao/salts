#include <cflow/scheduler.h>
#include "tinytest.h"

static int scheduler_order[4];
static size_t scheduler_order_count;

static void record_order(void *user) {
  const int *value = (const int *)user;
  if (scheduler_order_count < sizeof(scheduler_order) / sizeof(scheduler_order[0]))
    scheduler_order[scheduler_order_count++] = *value;
}

spec("CFlow scheduler compatibility") {
  it("keeps legacy ticks in milliseconds") {
    cflow_scheduler scheduler = {0};

    check_true(cflow_scheduler_test_init(&scheduler));
    check_equal(cflow_scheduler_now(&scheduler), 0u);
    check_equal(cflow_scheduler_advance(&scheduler, 25u), (size_t)0u);
    check_equal(cflow_scheduler_now(&scheduler), 25u);
    cflow_scheduler_destroy(&scheduler);
  }

  it("preserves FIFO order for equal deadlines") {
    cflow_scheduler scheduler = {0};
    int first = 1;
    int second = 2;
    int later = 3;

    scheduler_order_count = 0u;
    check_true(cflow_scheduler_test_init(&scheduler));
    check(cflow_scheduler_post_after(&scheduler, 10u, record_order, &first) != 0u);
    check(cflow_scheduler_post_after(&scheduler, 10u, record_order, &second) != 0u);
    check(cflow_scheduler_post_after(&scheduler, 20u, record_order, &later) != 0u);

    check_equal(cflow_scheduler_advance(&scheduler, 10u), (size_t)2u);
    check_equal(scheduler_order_count, (size_t)2u);
    check_equal(scheduler_order[0], 1);
    check_equal(scheduler_order[1], 2);

    check_equal(cflow_scheduler_run_until_idle(&scheduler, 0u), (size_t)1u);
    check_equal(scheduler_order_count, (size_t)3u);
    check_equal(scheduler_order[2], 3);
    check_equal(cflow_scheduler_now(&scheduler), 20u);
    cflow_scheduler_destroy(&scheduler);
  }

  it("cancels only pending delayed work") {
    cflow_scheduler scheduler = {0};
    int value = 7;
    cflow_task_id id;

    scheduler_order_count = 0u;
    check_true(cflow_scheduler_test_init(&scheduler));
    id = cflow_scheduler_post_after(&scheduler, 10u, record_order, &value);
    check(id != 0u);
    check_true(cflow_scheduler_cancel(&scheduler, id));
    check_false(cflow_scheduler_cancel(&scheduler, id));
    check_equal(cflow_scheduler_run_until_idle(&scheduler, 0u), (size_t)0u);
    check_equal(scheduler_order_count, (size_t)0u);
    cflow_scheduler_destroy(&scheduler);
  }
}
