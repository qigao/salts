#include <salts/clock.h>
#include <salts/random.h>
#include <salts/readiness.h>
#include <salts/thread.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(salts_hrtime()), uint64_t>);
static_assert(std::is_same_v<decltype(salts_platform_secure_random(nullptr, 0)), int>);
static_assert(std::is_same_v<salts_mutex_t, void *>);
static_assert(std::is_same_v<salts_rwlock_t, void *>);
static_assert(std::is_same_v<decltype(salts_readiness_reactor_init(
                                 static_cast<salts_readiness_reactor *>(nullptr),
                                 static_cast<const salts_readiness_config *>(nullptr))),
                             int>);
static_assert(std::is_same_v<decltype(salts_readiness_reactor_init_kind(
                                 static_cast<salts_readiness_reactor *>(nullptr),
                                 static_cast<const salts_readiness_config *>(nullptr),
                                 SALTS_READINESS_BACKEND_POLL)),
                             int>);
static_assert(std::is_same_v<
              decltype(salts_readiness_backend_supported(SALTS_READINESS_BACKEND_POLL)), bool>);
static_assert(std::is_same_v<decltype(&salts_readiness_arm),
                             int (*)(salts_readiness_registration *, salts_readiness_events,
                                     salts_readiness_callback, void *)>);

int main() {
  salts_readiness_reactor reactor{};
  salts_readiness_registration registration{};
  salts_readiness_config config{1, 1};
  salts_readiness_backend_kind backend_kind = SALTS_READINESS_BACKEND_POLL;
  return salts_hrtime() > 0 && reactor.impl == nullptr && registration.impl == nullptr &&
                 registration._admission == 0 && config.registration_capacity == 1 &&
                 backend_kind == SALTS_READINESS_BACKEND_POLL
             ? 0
             : 1;
}
