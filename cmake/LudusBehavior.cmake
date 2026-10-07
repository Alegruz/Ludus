# Project-owned script assets, explicit paired host tools, no configure-time cook.
function(ludus_cook_behaviors)
    cmake_parse_arguments(BEHAVIOR "" "NAME;CONTRACT;PACKAGE;PROFILE;COMPILER;ANALYZER" "" ${ARGN})
    if(BEHAVIOR_UNPARSED_ARGUMENTS OR NOT BEHAVIOR_NAME MATCHES "^[A-Za-z][A-Za-z0-9_]*$")
        message(FATAL_ERROR "ludus_cook_behaviors: use a target identifier and declared arguments")
    endif()
    foreach(argument NAME CONTRACT PACKAGE)
        if(NOT BEHAVIOR_${argument})
            message(FATAL_ERROR "ludus_cook_behaviors: ${argument} is required")
        endif()
    endforeach()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(resource "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/Ludus/behavior")
    foreach(pair "PROFILE;profile.json" "COMPILER;bin/luau-compile" "ANALYZER;bin/luau-analyze")
        list(GET pair 0 argument)
        list(GET pair 1 filename)
        if(NOT BEHAVIOR_${argument})
            set(BEHAVIOR_${argument} "${resource}/${filename}")
        endif()
    endforeach()
    foreach(argument CONTRACT PACKAGE PROFILE COMPILER ANALYZER)
        get_filename_component(BEHAVIOR_${argument} "${BEHAVIOR_${argument}}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${BEHAVIOR_NAME}")
    # The cooker validates the bounded source closure/content on every build.
    add_custom_target(${BEHAVIOR_NAME}_cook
        COMMAND "${Python3_EXECUTABLE}" "${resource}/behavior_cook.py"
            --contract "${BEHAVIOR_CONTRACT}" --package "${BEHAVIOR_PACKAGE}" --output "${output}"
            --profile "${BEHAVIOR_PROFILE}" --compiler "${BEHAVIOR_COMPILER}" --analyzer "${BEHAVIOR_ANALYZER}"
        BYPRODUCTS "${output}/contract.h" "${output}/package.h" "${output}/current.json"
        VERBATIM)
    set(${BEHAVIOR_NAME}_DIRECTORY "${output}" PARENT_SCOPE)
endfunction()
