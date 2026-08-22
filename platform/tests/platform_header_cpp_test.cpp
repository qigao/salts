#include <turbo/clock.h>
#include <turbo/thread.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(turbo_hrtime()), uint64_t>);
static_assert(std::is_same_v<turbo_mutex_t, void *>);
static_assert(std::is_same_v<turbo_rwlock_t, void *>);

int main() { return turbo_hrtime() > 0 ? 0 : 1; }
