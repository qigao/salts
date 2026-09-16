from pathlib import Path

path = Path("cnet/tests/CMakeLists.txt")
text = path.read_text()
marker = '''set_target_properties(cnet_api_profile_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX)
'''
insert = marker + '''
cmake_add_test(cnet_direct_dispatch_contract_test
  SOURCES cnet_direct_dispatch_contract_test.c
  LIBS salts_cnet_profile Salts::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_direct_dispatch_contract_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX)
# Release CI explicitly builds cnet_api_profile_test; keep the A1 diagnostic
# contract in the same build closure so RED/GREEN is exercised on every backend.
add_dependencies(cnet_api_profile_test cnet_direct_dispatch_contract_test)
'''
if text.count(marker) != 1:
    raise SystemExit(f"expected one cnet_api_profile_test marker, got {text.count(marker)}")
path.write_text(text.replace(marker, insert, 1))
