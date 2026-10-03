set(BUILD_ID "unknown")
if(GIT_EXECUTABLE AND EXISTS "${GIT_EXECUTABLE}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE git_result
    OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(git_result EQUAL 0 AND commit MATCHES "^[0-9a-f]+$")
    string(SUBSTRING "${commit}" 0 6 BUILD_ID)
    # Updating the distributed executable alone does not change its sources.
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD -- . ":(exclude)bin/"
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE dirty_result
      ERROR_QUIET)
    if(NOT dirty_result EQUAL 0)
      string(APPEND BUILD_ID "-dirty")
    endif()
  endif()
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
configure_file("${SOURCE_DIR}/cmake/build_info.h.in"
               "${OUTPUT_DIR}/build_info.h" @ONLY)
