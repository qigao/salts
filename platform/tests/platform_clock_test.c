#include <turbo/clock.h>
#include "tinytest.h"

spec("Platform clock") {
  it("keeps monotonic time nondecreasing") {
    uint64_t first = turbo_hrtime();
    uint64_t second = turbo_hrtime();
    check(second >= first);
    check(turbo_monotonic_ms() > 0);
  }

  it("keeps conversion helpers deterministic") {
    check_equal(turbo_ns_to_ms(1999999ULL), 1ULL);
    check_equal(turbo_ms_to_ns(7ULL), 7000000ULL);
  }

  it("keeps realtime separate from monotonic time") {
    check(turbo_realtime_ms() > 0);
  }
}
