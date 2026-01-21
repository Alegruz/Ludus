# CheckFormat.cmake - Verify code formatting and fail if issues found
# Usage: cmake -P CheckFormat.cmake

find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(NOT CLANG_FORMAT_EXE)
    message(FATAL_ERROR "clang-format not found")
endif()

file(GLOB_RECURSE ALL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/include/*.h"
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
)

execute_process(
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_SOURCE_FILES}
    RESULT_VARIABLE FORMAT_RESULT
    OUTPUT_VARIABLE FORMAT_OUTPUT
    ERROR_VARIABLE FORMAT_ERROR
)

if(NOT FORMAT_RESULT EQUAL 0)
    message(FATAL_ERROR "Code formatting check failed:\n${FORMAT_OUTPUT}\n${FORMAT_ERROR}")
endif()

message(STATUS "All files properly formatted ✓")
