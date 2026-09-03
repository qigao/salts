#include "tinytest.h"

#include <salts/error_codes.h>
#include <salts/readiness.h>

#include <stdint.h>

spec("Platform unsupported native readiness") {
  it("returns ENOTSUP and clears the reactor output") {
    salts_readiness_reactor reactor = {(void *)(uintptr_t)1};
    salts_readiness_config config = {1, 1};

    check_equal(salts_readiness_reactor_init(&reactor, &config), SALTS_ENOTSUP);
    check_null(reactor.impl);
  }

#if defined(SALTS_TEST_EXPECT_POLL_UNSUPPORTED)
  it("rejects an explicit poll backend without fallback") {
    salts_readiness_reactor reactor = {(void *)(uintptr_t)1};
    salts_readiness_config config = {1, 1};

    check_false(salts_readiness_backend_supported(
        SALTS_READINESS_BACKEND_POLL));
    check_equal(salts_readiness_reactor_init_kind(
                    &reactor, &config, SALTS_READINESS_BACKEND_POLL),
                SALTS_ENOTSUP);
    check_null(reactor.impl);
  }
#endif
}
