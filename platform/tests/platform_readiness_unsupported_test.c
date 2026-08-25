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

#if defined(TURBO_TEST_EXPECT_POLL_UNSUPPORTED)
  it("rejects an explicit poll backend without fallback") {
    turbo_readiness_reactor reactor = {(void *)(uintptr_t)1};
    turbo_readiness_config config = {1, 1};

    check_false(turbo_readiness_backend_supported(
        TURBO_READINESS_BACKEND_POLL));
    check_equal(turbo_readiness_reactor_init_kind(
                    &reactor, &config, TURBO_READINESS_BACKEND_POLL),
                TURBO_ENOTSUP);
    check_null(reactor.impl);
  }
#endif
}
