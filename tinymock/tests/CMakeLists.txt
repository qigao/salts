cmake_add_test(tinymock_reflect_test
  SOURCES tinymock_reflect_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")
cmake_add_test(tinymock_reflect_cpp_test
  SOURCES tinymock_reflect_cpp_test.cpp
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")
