add_library(turbo_minicoro_vendor INTERFACE)
add_library(TurboUtils::MinicoroVendor ALIAS turbo_minicoro_vendor)

target_include_directories(
  turbo_minicoro_vendor
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

set_target_properties(turbo_minicoro_vendor PROPERTIES FOLDER "vendor/minicoro")
