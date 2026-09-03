#include "platform.h"
#include "tinytest.hpp"

#ifndef SALTS_API
  #error "platform.h must define SALTS_API"
#endif

#ifndef SALTS_C_API
  #error "platform.h must define SALTS_C_API"
#endif

#ifdef CXX_C_API
  #error "platform.h must not expose the legacy CXX_C_API macro"
#endif

spec("platform C++ export contract") {
  it("exposes C linkage for platform functions") {
    uint64_t (*clock_fn)(void) = &salts_monotonic_ms;
    check(clock_fn != nullptr);
  }
}
