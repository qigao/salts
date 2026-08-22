#include <turbo/clock.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(turbo_hrtime()), uint64_t>);

int main() { return turbo_hrtime() > 0 ? 0 : 1; }
