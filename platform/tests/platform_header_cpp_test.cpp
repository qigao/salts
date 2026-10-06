#include <salts/clock.h>
#include <salts/random.h>
#include <salts/readiness.h>
#include <salts/thread.h>
#include <tinytest.hpp>
#include <type_traits>

static_assert(std::is_same_v<decltype(cmeta_hrtime()), uint64_t>);
static_assert(std::is_same_v<decltype(cmeta_platform_secure_random(nullptr, 0)), int>);
static_assert(std::is_same_v<cmeta_mutex_t, void *>);
static_assert(std::is_same_v<cmeta_rwlock_t, void *>);
static_assert(std::is_same_v<decltype(cmeta_readiness_reactor_init(
                                 static_cast<cmeta_readiness_reactor *>(nullptr),
                                 static_cast<const cmeta_readiness_config *>(nullptr))),
                             int>);
static_assert(std::is_same_v<decltype(cmeta_readiness_reactor_init_kind(
                                 static_cast<cmeta_readiness_reactor *>(nullptr),
                                 static_cast<const cmeta_readiness_config *>(nullptr),
                                 SALTS_READINESS_BACKEND_POLL)),
                             int>);
static_assert(std::is_same_v<
              decltype(cmeta_readiness_backend_supported(SALTS_READINESS_BACKEND_POLL)), bool>);
static_assert(std::is_same_v<decltype(&cmeta_readiness_arm),
                             int (*)(cmeta_readiness_registration *, cmeta_readiness_events,
                                     cmeta_readiness_callback, void *)>);

suite("Platform C++ headers") {
  group("clock") {
    it("exposes a positive high-resolution timestamp") {
      check_greater(cmeta_hrtime(), uint64_t{0});
    }
  }

  group("readiness values") {
    it("value-initializes empty reactor and registration handles") {
      const cmeta_readiness_reactor reactor{};
      const cmeta_readiness_registration registration{};
      check_equal(reactor.impl, nullptr);
      check_equal(registration.impl, nullptr);
      check_equal(registration._admission, 0u);
    }

    it("accepts aggregate configuration and backend selection") {
      constexpr size_t capacity = 1;
      const cmeta_readiness_config config{capacity, capacity};
      const cmeta_readiness_backend_kind backend_kind = SALTS_READINESS_BACKEND_POLL;
      check_equal(config.registration_capacity, capacity);
      check_equal(backend_kind, SALTS_READINESS_BACKEND_POLL);
    }
  }
}
