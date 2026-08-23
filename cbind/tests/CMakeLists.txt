cmake_add_test(cbind_scalar_decode_test
  SOURCES cbind_scalar_decode_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

set_target_properties(cbind_scalar_decode_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
