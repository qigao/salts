cmake_add_test(
  test_cmeta_core
  SOURCES test_cmeta_core.c
  FOLDER "cmeta/tests"
  LIBS TurboUtils::TinyTest TurboUtils::CMeta)
