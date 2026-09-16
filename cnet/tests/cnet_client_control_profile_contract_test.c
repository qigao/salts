#include "cnet_client_internal.h"
#include "tinytest.h"

#include <stdint.h>

spec("CNet client fixed-control profile contract") {
  it("composes owner, client-poll, and dispatcher timing boundaries") {
    cnet_client_poll_profile profile = {0};

    check_equal(profile.owner.owner_drive_calls, (uint64_t)0u);
    check_equal(profile.client_poll_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_prepare_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_invoke_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_observer_calls, (uint64_t)0u);
    check_equal(profile.dispatcher_release_calls, (uint64_t)0u);
  }
}
