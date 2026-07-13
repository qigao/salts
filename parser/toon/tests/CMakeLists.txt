
# =============================================================================
# TOON Tests
# =============================================================================

function(add_toon_test test_name source_file)
    cmake_add_executable(${test_name}
      TEST
      SOURCES ${source_file}
      LIBS toon TurboUtils::TinyTest
      FOLDER "parser/toon/tests")
    
    if(UNIX)
        target_link_libraries(${test_name} PRIVATE m)
    endif()
endfunction()

add_toon_test(toon_debug_test debug_test.c)

add_toon_test(test_toonc test_toonc.c)
