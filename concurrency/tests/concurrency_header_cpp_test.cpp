#include <salts/deadline_queue.h>
#include <salts/disruptor.h>
#include <salts/spsc_ring.h>
#include <salts/thread_pool.h>
#include <salts/rcu.h>
#include <tinytest.hpp>
#include <type_traits>

suite("Concurrency C++ headers") {
    group("compile-time API contracts") {
        it("preserves public identifier and return types") {
            static_assert(std::is_same_v<disruptor_stage_t, uint32_t>);
            static_assert(std::is_same_v<decltype(disruptor_capacity(nullptr)), uint64_t>);
            static_assert(std::is_same_v<decltype(cmeta_threadpool_size(nullptr)), int>);
            static_assert(std::is_same_v<cmeta_deadline_id, uint64_t>);
            static_assert(std::is_same_v<decltype(cmeta_spsc_ring_read_available(nullptr)), size_t>);
            static_assert(std::is_same_v<decltype(cmeta_rcu_read_unlock(nullptr)), int>);
        }
    }
}
