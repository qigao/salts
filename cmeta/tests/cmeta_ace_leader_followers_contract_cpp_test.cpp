#include <tinytest.hpp>
#include "cmeta_ace_leader_followers_contract_cases.h"
#include <type_traits>

static_assert(std::is_same_v<decltype(&lf_ace_cpu_handler), lf_handler_fn>);

suite("CMeta ACE Leader Followers exact C++17 callable metadata") {
  it("preserves the ABI and borrower roles without a scheduler") {
    lf_ace_check_callable_metadata();
  }
}
