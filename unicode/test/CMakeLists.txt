cmake_add_test(test_salts_unicode
  SOURCES test_salts_unicode.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")

cmake_add_test(test_salts_unicode_header_cpp
  SOURCES test_salts_unicode_header_cpp.cpp
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")

set_tests_properties(test_salts_unicode PROPERTIES LABELS "unicode;unit")
set_tests_properties(test_salts_unicode_header_cpp PROPERTIES LABELS "unicode;headers")

cmake_add_test(test_salts_unicode_grapheme
  SOURCES test_salts_unicode_grapheme.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
set_tests_properties(test_salts_unicode_grapheme PROPERTIES LABELS "unicode;grapheme")

cmake_add_test(test_salts_unicode_grapheme_conformance
  SOURCES test_salts_unicode_grapheme_conformance.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
target_compile_definitions(test_salts_unicode_grapheme_conformance
  PRIVATE SALTS_UNICODE_GRAPHEME_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data/17.0.0/GraphemeBreakTest.txt")
set_tests_properties(test_salts_unicode_grapheme_conformance
  PROPERTIES LABELS "unicode;grapheme;conformance")


cmake_add_test(test_salts_unicode_word
  SOURCES test_salts_unicode_word.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
set_tests_properties(test_salts_unicode_word PROPERTIES LABELS "unicode;word")

cmake_add_test(test_salts_unicode_word_conformance
  SOURCES test_salts_unicode_word_conformance.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
target_compile_definitions(test_salts_unicode_word_conformance
  PRIVATE SALTS_UNICODE_WORD_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data/17.0.0/WordBreakTest.txt")
set_tests_properties(test_salts_unicode_word_conformance
  PROPERTIES LABELS "unicode;word;conformance")
cmake_add_test(test_salts_unicode_line_break_conformance
  SOURCES test_salts_unicode_line_break_conformance.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
target_compile_definitions(test_salts_unicode_line_break_conformance
  PRIVATE SALTS_UNICODE_LINE_BREAK_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data/17.0.0/LineBreakTest.txt")
set_tests_properties(test_salts_unicode_line_break_conformance
  PROPERTIES LABELS "unicode;line-break;conformance")
cmake_add_test(test_salts_unicode_bidi
  SOURCES test_salts_unicode_bidi.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
set_tests_properties(test_salts_unicode_bidi
  PROPERTIES LABELS "unicode;bidi")


cmake_add_test(test_salts_unicode_bidi_conformance
  SOURCES test_salts_unicode_bidi_conformance.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
target_compile_definitions(test_salts_unicode_bidi_conformance
  PRIVATE SALTS_UNICODE_BIDI_PARAGRAPH_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data/17.0.0/BidiParagraphTest.txt")
set_tests_properties(test_salts_unicode_bidi_conformance
  PROPERTIES LABELS "unicode;bidi;conformance")


cmake_add_test(test_salts_unicode_emoji
  SOURCES test_salts_unicode_emoji.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
set_tests_properties(test_salts_unicode_emoji
  PROPERTIES LABELS "unicode;emoji")

cmake_add_test(test_salts_unicode_emoji_conformance
  SOURCES test_salts_unicode_emoji_conformance.c
  LIBS Salts::Unicode Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "unicode/tests")
target_compile_definitions(test_salts_unicode_emoji_conformance
  PRIVATE SALTS_UNICODE_EMOJI_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data/17.0.0/emoji-data.txt")
set_tests_properties(test_salts_unicode_emoji_conformance
  PROPERTIES LABELS "unicode;emoji;conformance")

# Validate the installed SDK, not just in-tree link aliases. The same consumers
# must compile as independent C11 and C++17 projects against Salts::Unicode.
# Cross-compiled Android/iOS builds do not execute hosted CTest.
add_test(
  NAME test_salts_unicode_installed_sdk
  COMMAND "${CMAKE_COMMAND}"
    "-DSALTS_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install_consumer"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-unicode"
    "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
    "-DSALTS_BUILD_GENERATOR=${CMAKE_GENERATOR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_installed_unicode.cmake")
set_tests_properties(test_salts_unicode_installed_sdk PROPERTIES
  LABELS "unicode;installed;sdk"
  TIMEOUT 300
  RUN_SERIAL TRUE)
