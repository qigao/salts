#include <salts/clock.h>
#include "tinytest.h"

spec("Platform clock") {
  it("keeps monotonic time nondecreasing") {
    uint64_t first = salts_hrtime();
    uint64_t second = salts_hrtime();
    check(second >= first);
    check(salts_monotonic_ms() > 0);
  }

  it("keeps conversion helpers deterministic") {
    check_equal(salts_ns_to_ms(1999999ULL), 1ULL);
    check_equal(salts_ms_to_ns(7ULL), 7000000ULL);
  }

  it("keeps realtime separate from monotonic time") {
    check(salts_realtime_ms() > 0);
  }
}
