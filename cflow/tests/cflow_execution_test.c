#include <cflow/clock.h>
#include <cflow/time.h>
#include "tinytest.h"

spec("CFlow execution time") {
  it("saturates deadline arithmetic") {
    cflow_instant now = {UINT64_MAX - 5u};
    cflow_duration delay = cflow_duration_from_ns(10u);
    cflow_deadline deadline = cflow_deadline_after(now, delay);
    check_equal(deadline.ns, UINT64_MAX);
  }

  it("saturates duration unit conversion") {
    check_equal(cflow_duration_from_s(UINT64_MAX).ns, UINT64_MAX);
  }

  it("advances virtual time exactly") {
    cflow_clock clock = {0};
    check_true(cflow_clock_virtual_init(&clock, (cflow_instant){100u}));
    check_equal(cflow_clock_now(&clock).ns, 100u);
    check_true(cflow_clock_advance(&clock, cflow_duration_from_ns(25u)));
    check_equal(cflow_clock_now(&clock).ns, 125u);
    cflow_clock_destroy(&clock);
  }

  it("does not manually advance system time") {
    cflow_clock clock = {0};
    check_true(cflow_clock_system_init(&clock));
    check(cflow_clock_now(&clock).ns > 0u);
    check_false(cflow_clock_advance(&clock, cflow_duration_from_ns(1u)));
    cflow_clock_destroy(&clock);
  }
}
