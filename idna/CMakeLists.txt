add_library(salts_idna STATIC include/salts_idna.h src/salts_idna.c src/idna_data.h)
cmake_config_target(salts_idna ALIAS Salts::IDNA FOLDER "idna" EXPORT_NAME IDNA)
target_include_directories(salts_idna PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/idna>)
target_link_libraries(salts_idna PUBLIC Salts::Unicode)
target_compile_features(salts_idna PUBLIC c_std_11)
install(TARGETS salts_idna EXPORT SaltsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/salts_idna.h DESTINATION include/idna)
if(BUILD_TESTS)
  add_subdirectory(test)
endif()
