add_library(salts_reed STATIC gf256.c gf256.h)

target_include_directories(
  salts_reed PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

find_path(SIMDE_INCLUDE_DIRS "simde/simde-common.h" REQUIRED)
target_include_directories(salts_reed PRIVATE ${SIMDE_INCLUDE_DIRS})

set_target_properties(
  salts_reed
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF
             FOLDER "vendor/reed")
