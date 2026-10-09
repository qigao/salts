# Cross-DSO and C++17 conformance using only the canonical CNet library.
add_library(cnet_manager_link_probe SHARED probe.c)
target_compile_definitions(cnet_manager_link_probe PRIVATE CNET_DSO_PROBE_BUILD=1)
target_link_libraries(cnet_manager_link_probe PRIVATE Salts::CNet)
set_target_properties(cnet_manager_link_probe PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden FOLDER "cnet/tests")
add_executable(cnet_manager_link_test main.c)
add_executable(cnet_manager_link_cpp_test main.cpp)
set_target_properties(cnet_manager_link_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "cnet/tests")
set_target_properties(cnet_manager_link_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF FOLDER "cnet/tests")
foreach(_t IN ITEMS cnet_manager_link_test cnet_manager_link_cpp_test)
  target_link_libraries(${_t} PRIVATE Salts::CNet cnet_manager_link_probe)
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME ${_t} COMMAND ${_t})
    set_tests_properties(${_t} PROPERTIES TIMEOUT 30)
  endif()
endforeach()
