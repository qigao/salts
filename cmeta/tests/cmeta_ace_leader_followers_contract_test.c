#include "tinytest.h"
#include "cmeta_ace_leader_followers_contract_cases.h"

spec("CMeta ACE Leader Followers exact C11 callable metadata") {
  it("describes borrowed callbacks, typed leader roles and value terminals") {
    lf_ace_check_callable_metadata();
  }
}
