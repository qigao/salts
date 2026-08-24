#include "tinytest.h"

#include <turbo/error_codes.h>
#include <turbo/readiness.h>

#include <stdint.h>

spec("Platform unsupported native readiness") {
  it("returns ENOTSUP and clears the reactor output") {
    turbo_readiness_reactor reactor = {(void *)(uintptr_t)1};
    turbo_readiness_config config = {1, 1};

    check_equal(turbo_readiness_reactor_init(&reactor, &config), TURBO_ENOTSUP);
    check_null(reactor.impl);
  }
}
