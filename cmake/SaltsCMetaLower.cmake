# CMeta source lowering helper.
#
# salts_cmeta_lower_source(
#   INPUT  path/to/source.cmeta.c
#   OUTPUT path/to/generated.c)
#
# The lowerer is always a host executable. Installed target SDKs do not need to
# contain target-architecture parser/runtime code.

function(salts_cmeta_lower_source)
  set(options)
  set(oneValueArgs INPUT OUTPUT)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT ARG_INPUT OR NOT ARG_OUTPUT)
    message(FATAL_ERROR
            "salts_cmeta_lower_source requires INPUT and OUTPUT")
  endif()

  get_filename_component(input "${ARG_INPUT}" ABSOLUTE
                         BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(output "${ARG_OUTPUT}" ABSOLUTE
                         BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")

  if(TARGET Salts::CMetaLower)
    set(lower_command "$<TARGET_FILE:Salts::CMetaLower>")
    set(lower_depends Salts::CMetaLower)
  elseif(TARGET cmeta_lower)
    set(lower_command "$<TARGET_FILE:cmeta_lower>")
    set(lower_depends cmeta_lower)
  else()
    if(NOT SALTS_CMETA_LOWER_EXECUTABLE)
      set(_salts_cmeta_lower_hints
          "${PACKAGE_PREFIX_DIR}/bin"
          "${CMAKE_CURRENT_LIST_DIR}/../../../bin")

      # Salts.Native keeps target SDKs as siblings. When an Android target
      # package is selected, resolve the lowerer from the matching host SDK.
      if(PACKAGE_PREFIX_DIR)
        get_filename_component(_salts_sdk_root "${PACKAGE_PREFIX_DIR}" DIRECTORY)
        if(CMAKE_HOST_WIN32)
          list(APPEND _salts_cmeta_lower_hints
               "${_salts_sdk_root}/windows-x64/bin")
        elseif(CMAKE_HOST_APPLE)
          list(APPEND _salts_cmeta_lower_hints
               "${_salts_sdk_root}/macos-arm64/bin"
               "${_salts_sdk_root}/macos-x64/bin")
        else()
          list(APPEND _salts_cmeta_lower_hints
               "${_salts_sdk_root}/linux-x64/bin")
        endif()
      endif()

      find_program(SALTS_CMETA_LOWER_EXECUTABLE
        NAMES cmeta-lower
        HINTS ${_salts_cmeta_lower_hints})
      unset(_salts_cmeta_lower_hints)
      unset(_salts_sdk_root)
    endif()
    if(NOT SALTS_CMETA_LOWER_EXECUTABLE)
      message(FATAL_ERROR
              "cmeta-lower host tool was not found; set SALTS_CMETA_LOWER_EXECUTABLE")
    endif()
    set(lower_command "${SALTS_CMETA_LOWER_EXECUTABLE}")
    set(lower_depends)
  endif()

  get_filename_component(output_dir "${output}" DIRECTORY)
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
    COMMAND ${lower_command} "${input}" "${output}"
    DEPENDS "${input}" ${lower_depends}
    COMMENT "Lowering CMeta source ${ARG_INPUT}"
    VERBATIM)

  set_source_files_properties("${output}" PROPERTIES GENERATED TRUE)
endfunction()
