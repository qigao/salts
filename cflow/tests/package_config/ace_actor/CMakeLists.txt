cmake_minimum_required(VERSION 3.20)
project(cflow_ace_actor_installed_consumer LANGUAGES C)

if(NOT DEFINED SALTS_TEST_PREFIX OR NOT DEFINED SALTS_TEST_VERSION)
  message(FATAL_ERROR "installed SDK prefix and expected version required")
endif()

# Installed CFlow and transitive CMeta only: no source-tree includes, no
# implicit Plugin runtime and no alternate stable SDK/fallback.
find_package(Salts ${SALTS_TEST_VERSION} EXACT CONFIG REQUIRED
  PATHS "${SALTS_TEST_PREFIX}" NO_DEFAULT_PATH)
if(NOT TARGET Salts::CFlow)
  message(FATAL_ERROR "installed package is missing Salts::CFlow")
endif()
if(NOT TARGET Salts::CMeta)
  message(FATAL_ERROR "installed package is missing Salts::CMeta")
endif()

add_executable(cflow_ace_actor_installed_consumer main.c)
target_link_libraries(cflow_ace_actor_installed_consumer PRIVATE Salts::CFlow)
target_compile_features(cflow_ace_actor_installed_consumer PRIVATE c_std_11)

enable_testing()
add_test(NAME cflow_ace_actor_installed_consumer
  COMMAND cflow_ace_actor_installed_consumer)
