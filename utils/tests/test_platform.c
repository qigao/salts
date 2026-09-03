#include "platform.h"
#include "tinytest.h"

#include <string.h>

#ifndef SALTS_API
  #error "platform.h must define SALTS_API"
#endif

#ifndef SALTS_C_API
  #error "platform.h must define SALTS_C_API"
#endif

#ifdef CXX_C_API
  #error "platform.h must not expose the legacy CXX_C_API macro"
#endif

spec("platform_datetime") {
  it("should convert UTC broken-down time to epoch seconds") {
    struct tm tm_value;
    time_t ts;

    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = 124;
    tm_value.tm_mon = 0;
    tm_value.tm_mday = 1;
    tm_value.tm_hour = 12;

    ts = salts_timegm(&tm_value);
    check(ts != (time_t)-1);
    check_equal((int)ts, 1704110400);
  }

  it("should decompose UTC time safely") {
    struct tm tm_value;

    check_equal(salts_gmtime((time_t)1704110400, &tm_value), 0);
    check_equal(tm_value.tm_year + 1900, 2024);
    check_equal(tm_value.tm_mon + 1, 1);
    check_equal(tm_value.tm_mday, 1);
    check_equal(tm_value.tm_hour, 12);
  }

  it("should format UTC time with strftime") {
    char buf[32];
    int rc = salts_strftime_utc((time_t)1704110400, "%Y-%m-%dT%H:%M:%SZ",
                                buf, sizeof(buf));

    check(rc > 0);
    check_equal(buf, "2024-01-01T12:00:00Z");
  }
}
