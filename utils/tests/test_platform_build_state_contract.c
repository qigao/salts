#include "salts_api.h"
#include "platform.h"
#include "tinytest.h"

#ifdef SALTS_BUILD_SHARED
#error "SALTS_BUILD_SHARED must not be required by Salts consumers"
#endif

#ifdef SALTS_USE_SHARED
#error "SALTS_USE_SHARED must not leak to Salts consumers"
#endif

#ifdef salts_EXPORTS
#error "salts_EXPORTS is a CMake implementation detail and must not reach consumers"
#endif

#ifndef SALTS_API
#error "Salts::Core consumers must receive SALTS_API through the target contract"
#endif

#ifndef SALTS_C_API
#error "Salts::Core consumers must receive SALTS_C_API through the target contract"
#endif

spec("platform build-state contract") {
  it("keeps shared-library build state out of consumer source") {
    check(true);
  }
}
