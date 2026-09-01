#include <turbo/clock.h>
#include <turbo/random.h>
#include <turbo/readiness.h>
#include <turbo/thread.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(turbo_hrtime()), uint64_t>);
static_assert(std::is_same_v<decltype(turbo_platform_secure_random(nullptr, 0)), int>);
static_assert(std::is_same_v<turbo_mutex_t, void *>);
static_assert(std::is_same_v<turbo_rwlock_t, void *>);
static_assert(std::is_same_v<decltype(turbo_readiness_reactor_init(
                                 static_cast<turbo_readiness_reactor *>(nullptr),
                                 static_cast<const turbo_readiness_config *>(nullptr))),
                             int>);
static_assert(std::is_same_v<decltype(turbo_readiness_reactor_init_kind(
                                 static_cast<turbo_readiness_reactor *>(nullptr),
                                 static_cast<const turbo_readiness_config *>(nullptr),
                                 TURBO_READINESS_BACKEND_POLL)),
                             int>);
static_assert(std::is_same_v<
              decltype(turbo_readiness_backend_supported(TURBO_READINESS_BACKEND_POLL)), bool>);
static_assert(std::is_same_v<decltype(&turbo_readiness_arm),
                             int (*)(turbo_readiness_registration *, turbo_readiness_events,
                                     turbo_readiness_callback, void *)>);

int main() {
  turbo_readiness_reactor reactor{};
  turbo_readiness_registration registration{};
  turbo_readiness_config config{1, 1};
  turbo_readiness_backend_kind backend_kind = TURBO_READINESS_BACKEND_POLL;
  return turbo_hrtime() > 0 && reactor.impl == nullptr && registration.impl == nullptr &&
                 registration._admission == 0 && config.registration_capacity == 1 &&
                 backend_kind == TURBO_READINESS_BACKEND_POLL
             ? 0
             : 1;
}
