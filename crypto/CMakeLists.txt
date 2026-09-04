set(TARGET_NAME salts_crypto)
set(LIBECC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libecc")

file(
  GLOB LIBECC_ARITHMETIC_SOURCES CONFIGURE_DEPENDS
  "${LIBECC_ROOT}/src/fp/fp*.c"
  "${LIBECC_ROOT}/src/nn/n*.c")
file(
  GLOB LIBECC_CURVE_SOURCES CONFIGURE_DEPENDS
  "${LIBECC_ROOT}/src/curves/*.c")

set(
  LIBECC_ED448_SOURCES
  ${LIBECC_ARITHMETIC_SOURCES}
  ${LIBECC_CURVE_SOURCES}
  "${LIBECC_ROOT}/src/hash/hash_algs.c"
  "${LIBECC_ROOT}/src/hash/shake.c"
  "${LIBECC_ROOT}/src/hash/shake256.c"
  "${LIBECC_ROOT}/src/sig/ec_key.c"
  "${LIBECC_ROOT}/src/sig/eddsa.c"
  "${LIBECC_ROOT}/src/sig/sig_algs.c"
  "${LIBECC_ROOT}/src/utils/utils.c"
  "${LIBECC_ROOT}/src/utils/utils_rand.c")

add_library(${TARGET_NAME} SHARED src/crypto_ed448.c src/crypto_sha256.c
            ${LIBECC_ED448_SOURCES} "${LIBECC_ROOT}/src/hash/sha256.c")
cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::Crypto
  FOLDER "crypto"
  EXPORT_NAME Crypto)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_compile_definitions(${TARGET_NAME} PRIVATE SALTS_CRYPTO_BUILDING)
set_target_properties(${TARGET_NAME} PROPERTIES C_VISIBILITY_PRESET hidden)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE "${LIBECC_ROOT}/include")

# Keep libecc private and compile only the RFC 8032 Ed448 feature set. The
# explicit word size is required by MSVC and matches supported 64-bit targets.
set_source_files_properties(
  src/crypto_ed448.c
  src/crypto_sha256.c
  ${LIBECC_ED448_SOURCES}
  "${LIBECC_ROOT}/src/hash/sha256.c"
  PROPERTIES COMPILE_DEFINITIONS
             "WITH_STDLIB;WITH_LIBECC_CONFIG_OVERRIDE;WITH_CURVE_WEI448;WITH_HASH_SHA256;WITH_HASH_SHAKE256;WITH_SIG_EDDSA448;WORDSIZE=64")

target_link_libraries(${TARGET_NAME} PRIVATE Salts::Platform monocypher)

install(TARGETS ${TARGET_NAME} EXPORT SaltsTargets)
install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")
install(FILES "${LIBECC_ROOT}/LICENSE"
        DESTINATION share/licenses/Salts/libecc)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
