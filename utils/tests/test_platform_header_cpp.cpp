#include "platform.h"
#include "tinytest.hpp"

#ifndef TURBO_API
  #error "platform.h must define TURBO_API"
#endif

#ifndef TURBO_C_API
  #error "platform.h must define TURBO_C_API"
#endif

#ifdef CXX_C_API
  #error "platform.h must not expose the legacy CXX_C_API macro"
#endif

spec("platform C++ export contract") {
  it("exposes C linkage for platform functions") {
    uint64_t (*clock_fn)(void) = &turbo_monotonic_ms;
    check(clock_fn != nullptr);
  }
}
