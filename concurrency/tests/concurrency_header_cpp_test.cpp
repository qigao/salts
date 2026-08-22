#include <turbo/disruptor.h>
#include <turbo/thread_pool.h>
#include <type_traits>

static_assert(std::is_same_v<disruptor_stage_t, uint32_t>);
static_assert(std::is_same_v<decltype(disruptor_capacity(nullptr)), uint64_t>);
static_assert(std::is_same_v<decltype(turbo_threadpool_size(nullptr)), int>);

int main() { return 0; }
