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

# One installed SDK and one out-of-tree CMake run now qualify both ACE
# Active Object and Pipes/Filters; do not duplicate the install harness.
add_executable(cflow_ace_pipes_installed_consumer pipes.c)
target_link_libraries(cflow_ace_pipes_installed_consumer PRIVATE Salts::CFlow)
target_compile_features(cflow_ace_pipes_installed_consumer PRIVATE c_std_11)
add_test(NAME cflow_ace_pipes_installed_consumer
  COMMAND cflow_ace_pipes_installed_consumer)

# The retired CNet x Actor adapter is NOT part of the 2.3 SDK. This
# executable uses the canonical public CNet and independent CFlow APIs:
# real TCP receive callback -> bounded Channel -> Subscription. It must not
# rely on (or resurrect) an obsolete CFlowCNet DSO.
if(TARGET Salts::CFlowCNet OR NOT TARGET Salts::CNet)
  message(FATAL_ERROR
    "installed ACE half-sync requires canonical Salts::CNet, not legacy Salts::CFlowCNet")
endif()
add_executable(cflow_ace_half_sync_installed_consumer half_sync.c)
target_link_libraries(cflow_ace_half_sync_installed_consumer
  PRIVATE Salts::CNet Salts::CFlow)
target_compile_features(cflow_ace_half_sync_installed_consumer PRIVATE c_std_11)
if(WIN32)
  target_link_libraries(cflow_ace_half_sync_installed_consumer PRIVATE ws2_32)
endif()
add_test(NAME cflow_ace_half_sync_installed_consumer
  COMMAND cflow_ace_half_sync_installed_consumer)
# Fail early on undeclared C APIs on every supported GCC/Clang host.
# The installed SDK consumer must include each API's declaring public header;
# GNU C11 warning permissiveness must not hide macOS Clang errors.
if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  foreach(ace_consumer IN ITEMS
      cflow_ace_actor_installed_consumer
      cflow_ace_pipes_installed_consumer
      cflow_ace_half_sync_installed_consumer)
    target_compile_options(${ace_consumer} PRIVATE
      -Werror=implicit-function-declaration)
  endforeach()
endif()
