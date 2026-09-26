cmake_add_test(salts_cpp_member_thunk_test
  SOURCES cpp_member_thunk_test.cpp
  LIBS Salts::BindingsCpp Salts::TinyTest
  FOLDER "bindings/cpp/tests")

set_target_properties(salts_cpp_member_thunk_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
