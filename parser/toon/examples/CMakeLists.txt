# Examples for toon library

cmake_add_source(EXAMPLE_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS "*.c")

foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    target_link_libraries(${EXAMPLE_NAME} PRIVATE toon)
    
    if(UNIX)
        target_link_libraries(${EXAMPLE_NAME} PRIVATE m)
    endif()
    
    cmake_config_target(${EXAMPLE_NAME} FOLDER "parser/toon/examples")
endforeach()
