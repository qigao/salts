cmake_add_test(test_salts_idna
  SOURCES test_salts_idna.c
  LIBS Salts::IDNA Salts::TinyTest
  FOLDER "idna/tests")
cmake_add_test(test_salts_idna_conformance
  SOURCES test_salts_idna_conformance.c
  LIBS Salts::IDNA Salts::TinyTest
  DEFS SALTS_IDNA_TEST_DATA="${PROJECT_SOURCE_DIR}/unicode/data/17.0.0/IdnaTestV2.txt"
  FOLDER "idna/tests")
set_tests_properties(test_salts_idna test_salts_idna_conformance
  PROPERTIES LABELS "unicode;idna")
