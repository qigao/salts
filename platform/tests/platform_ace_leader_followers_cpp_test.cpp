#include "platform_ace_leader_followers_types.h"
#include <tinytest.hpp>
#include <type_traits>

using lf_wrong_result_fn = int (*)(void *, int);
using lf_wrong_parameters_fn = lf_result (*)(int, void *);
static_assert(std::is_same_v<decltype(LF_LEADER), lf_role>);
static_assert(std::is_same_v<decltype(LF_CANCELLED), lf_result>);
static_assert(std::is_same_v<lf_handler_fn, lf_result (*)(void *, int)>);
static_assert(!std::is_convertible_v<lf_wrong_result_fn, lf_handler_fn>);
static_assert(!std::is_convertible_v<lf_wrong_parameters_fn, lf_handler_fn>);

suite("ACE Leader Followers exact C++17 callback role") {
  it("does not conflate cancellation result with worker lifetime") {
    check_false((std::is_same_v<lf_result, lf_stop>));
    check_equal(LF_LEADER, lf_role::LF_LEADER);
  }
}
