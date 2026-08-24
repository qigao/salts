#include <turbo/clock.h>
#include <turbo/readiness.h>
#include <turbo/thread.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(turbo_hrtime()), uint64_t>);
static_assert(std::is_same_v<turbo_mutex_t, void *>);
static_assert(std::is_same_v<turbo_rwlock_t, void *>);
static_assert(std::is_same_v<decltype(turbo_readiness_reactor_init(
                                 static_cast<turbo_readiness_reactor *>(nullptr),
                                 static_cast<const turbo_readiness_config *>(nullptr))),
                             int>);
static_assert(std::is_same_v<decltype(&turbo_readiness_arm),
                             int (*)(turbo_readiness_registration *, turbo_readiness_events,
                                     turbo_readiness_callback, void *)>);

int main() {
  turbo_readiness_reactor reactor{};
  turbo_readiness_config config{1, 1};
  return turbo_hrtime() > 0 && reactor.impl == nullptr && config.registration_capacity == 1 ? 0 : 1;
}
