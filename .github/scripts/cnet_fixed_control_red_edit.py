from pathlib import Path

cmake = Path("cnet/tests/CMakeLists.txt")
text = cmake.read_text()
old = '''cmake_add_test(cnet_api_profile_test\n  SOURCES cnet_api_test.c\n'''
new = '''cmake_add_test(cnet_api_profile_test\n  SOURCES cnet_api_test.c cnet_client_control_profile_contract_test.c\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one cnet_api_profile_test source stanza, found {text.count(old)}")
cmake.write_text(text.replace(old, new, 1))

Path("cnet/tests/cnet_client_control_profile_contract_test.c").write_text(r'''#include "cnet_client_internal.h"
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
''')
