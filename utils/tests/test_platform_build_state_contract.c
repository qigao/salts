#include "turbo_api.h"
#include "platform.h"
#include "tinytest.h"

#ifdef TURBO_BUILD_SHARED
#error "TURBO_BUILD_SHARED must not be required by TurboUtils consumers"
#endif

#ifdef TURBO_USE_SHARED
#error "TURBO_USE_SHARED must not leak to TurboUtils consumers"
#endif

#ifdef turbo_utils_EXPORTS
#error "turbo_utils_EXPORTS is a CMake implementation detail and must not reach consumers"
#endif

#ifndef TURBO_API
#error "Rocida::Core consumers must receive TURBO_API through the target contract"
#endif

#ifndef TURBO_C_API
#error "Rocida::Core consumers must receive TURBO_C_API through the target contract"
#endif

spec("platform build-state contract") {
  it("keeps shared-library build state out of consumer source") {
    check(true);
  }
}
