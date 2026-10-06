add_library(cmeta_wsparser OBJECT websocket_frame_parser.c)
target_compile_features(cmeta_wsparser PUBLIC c_std_11)
target_include_directories(cmeta_wsparser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(cmeta_wsparser PROPERTIES
  FOLDER "tools/wsparser"
  POSITION_INDEPENDENT_CODE ON)
