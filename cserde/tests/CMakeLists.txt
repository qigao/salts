cmake_add_test(cserde_token_test
  SOURCES cserde_token_test.c
  LIBS TurboUtils::CSerde TurboUtils::TinyTest
  FOLDER "cserde/tests")

set_target_properties(cserde_token_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
