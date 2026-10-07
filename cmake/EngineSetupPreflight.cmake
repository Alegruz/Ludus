include("${CMAKE_CURRENT_LIST_DIR}/LudusDependencies.cmake")
# Run before project(): otherwise CMake's compiler setup reports only a missing
# toolchain path, without explaining how to prepare this checkout/preset.
function(ludus_check_setup)
    if(NOT CMAKE_TOOLCHAIN_FILE)
        return()
    endif()

    # Match CMake's relative toolchain lookup: binary directory, then source.
    set(toolchain "${CMAKE_TOOLCHAIN_FILE}")
    cmake_path(ABSOLUTE_PATH toolchain BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE
        OUTPUT_VARIABLE binary_toolchain)
    cmake_path(ABSOLUTE_PATH toolchain BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)
    if(EXISTS "${binary_toolchain}" OR EXISTS "${toolchain}")
        return()
    endif()

    set(conan_directory "${CMAKE_SOURCE_DIR}/out/conan")
    cmake_path(IS_PREFIX conan_directory "${toolchain}" NORMALIZE is_conan_toolchain)
    get_filename_component(filename "${toolchain}" NAME)
    if(is_conan_toolchain AND filename STREQUAL "conan_toolchain.cmake")
        get_filename_component(directory "${toolchain}" DIRECTORY)
        get_filename_component(preset "${directory}" NAME)
        if(NOT preset MATCHES "^(linux|macos)-clang-(debug|development|profile|release|asan-ubsan)$")
            return()
        endif()
    elseif(toolchain STREQUAL "${CMAKE_SOURCE_DIR}/out/host-tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
        get_filename_component(preset "${CMAKE_BINARY_DIR}" NAME)
        if(NOT preset MATCHES "^web-emscripten-(development|release)$")
            set(preset "web-emscripten-development")
        endif()
    else()
        # A custom toolchain belongs to its caller; init cannot repair it.
        return()
    endif()

    set(command "./init.sh --cli --preset ${preset} --preset-only --locked")
    if(LUDUS_BUILD_TESTS AND NOT preset MATCHES "^web-")
        string(APPEND command " --with-tests")
    endif()
    set(scope_hint "")
    if(NOT preset MATCHES "^web-")
        set(scope_hint "--preset-only prepares only the selected profile; omit it to prepare all native profiles.\n")
    endif()
    message(FATAL_ERROR
        "Ludus setup is missing for ${preset}.\n"
        "Run init before configuring CMake. From the repository root, run:\n\n"
        "  ${command}\n\n"
        "Then rerun CMake Configure.\n${scope_hint}"
        "Missing generated toolchain: ${toolchain}")
endfunction()

ludus_check_setup()

# A cached compiler/Ninja path can survive after tools move or are removed.
# Check explicit selections before project() obscures the setup problem.
foreach(variable CMAKE_MAKE_PROGRAM CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
    if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
        ludus_require_tool(${variable} "Build configuration"
            "Restore the selected tool or set ${variable} to its executable. For project-managed tools run ./init.sh from the repository root.")
    endif()
endforeach()
