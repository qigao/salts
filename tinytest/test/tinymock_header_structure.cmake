if(NOT DEFINED TINYMOCK_HEADER)
  message(FATAL_ERROR "TINYMOCK_HEADER is required")
endif()
if(NOT DEFINED TRAITS_HEADER)
  message(FATAL_ERROR "TRAITS_HEADER is required")
endif()

file(READ "${TINYMOCK_HEADER}" tinymock_header)
file(READ "${TRAITS_HEADER}" traits_header)

foreach(forbidden
    "tinymock_value_eq_"
    "tinymock_value_dump_"
    "tinymock_value_int("
    "tinymock_value_uint("
    "tinymock_value_size("
    "tinymock_value_signed("
    "tinymock_value_unsigned("
    "tinymock_value_as_"
    "TINYMOCk_RESULT_AS__("
    "TINYMOCk_MOCK1("
    "TINYMOCk_MOCK2("
    "TINYMOCk_MOCK3("
    "TINYMOCk_MOCK_DEFINE1("
    "TINYMOCk_MOCK_DEFINE2("
    "TINYMOCk_PARAMS_1("
    "TINYMOCk_EXPECT_PARAMS_1("
    "TINYMOCk_EXPECT_VALUES_1("
    "TINYMOCk_ACTUAL_VALUES_1("
    "__TINYMOCk_HAS_C11_GENERIC__"
    "C89/C99 fallback")
  string(FIND "${tinymock_header}" "${forbidden}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "tinymock.h retains redundant typed implementation: ${forbidden}")
  endif()
endforeach()

string(FIND "${tinymock_header}" "TTEST_C11_EQUAL_ASSOCIATIONS__" traits_position)
if(traits_position EQUAL -1)
  message(FATAL_ERROR "tinymock.h does not use the shared strict-C11 trait map")
endif()

string(FIND "${tinymock_header}" "TTEST_PP_REPEAT__" repeat_position)
if(repeat_position EQUAL -1)
  message(FATAL_ERROR "tinymock.h does not use the shared PP repeat kernel")
endif()

foreach(forbidden "FECI" "TTEST_TRAIT_PP_")
  string(FIND "${traits_header}" "${forbidden}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "traits.h retains non-trait PP machinery: ${forbidden}")
  endif()
endforeach()

string(REPLACE
  "static inline void tinymock_failure_adapter__("
  "void tinymock_failure_adapter__("
  header_without_language_adapter
  "${tinymock_header}")
string(REGEX MATCH
  "(^|\n)[ \t]*static[ \t]+(inline[ \t]+)?[^;\n{]*tinymock_[A-Za-z0-9_]+[ \t]*\\("
  private_static_function
  "${header_without_language_adapter}")
if(private_static_function)
  string(STRIP "${private_static_function}" private_static_function)
  message(FATAL_ERROR
    "tinymock.h still defines a backend static function: ${private_static_function}")
endif()
