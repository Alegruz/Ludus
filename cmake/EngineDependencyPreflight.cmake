# Optional dependencies are checked only for enabled features.
include("${CMAKE_CURRENT_LIST_DIR}/LudusDependencies.cmake")
get_filename_component(setup_preset "${CMAKE_BINARY_DIR}" NAME)
set(setup_recovery "From the repository root: ./init.sh --cli --preset ${setup_preset} --preset-only --locked")
if(NOT setup_preset MATCHES "^(linux|macos)-clang-(debug|development|profile|release|asan-ubsan|tsan)$")
    set(setup_recovery "From the repository root: ./init.sh --cli --locked")
endif()
if(LUDUS_BUILD_TESTS)
    ludus_require_python("Engine tests")
    find_package(Catch2 3 QUIET CONFIG)
    if(NOT Catch2_FOUND)
        ludus_dependency_error("Engine tests" "Catch2 3"
            "Conan package metadata is missing or incompatible. CMAKE_PREFIX_PATH='${CMAKE_PREFIX_PATH}'."
            "${setup_recovery} --with-tests\n  For custom builds, set Catch2_DIR to its package configuration directory.")
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(volk QUIET CONFIG)
    if(NOT volk_FOUND)
        ludus_dependency_error("Vulkan rendering" "volk"
            "Conan package metadata is missing. CMAKE_PREFIX_PATH='${CMAKE_PREFIX_PATH}'."
            "${setup_recovery}\n  For custom builds, set volk_DIR to its package configuration directory.")
    endif()
endif()

if(NOT EMSCRIPTEN)
    foreach(package freetype harfbuzz)
        find_package(${package} QUIET CONFIG)
        if(NOT ${package}_FOUND)
            ludus_dependency_error("Text rendering" "${package}"
                "Conan package metadata is missing. CMAKE_PREFIX_PATH='${CMAKE_PREFIX_PATH}'."
                "${setup_recovery}\n  For custom builds, set ${package}_DIR to its package configuration directory.")
        endif()
    endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(CheckCXXSourceCompiles)
    # Recheck after SDK/compiler changes, even when their cache paths are unchanged.
    unset(LUDUS_MACOS_SDK_MATH_WORKS CACHE)
    check_cxx_source_compiles("#include <cmath>
#ifndef NAN
#error LudusSDK_missing_NAN
#endif
#ifndef INFINITY
#error LudusSDK_missing_INFINITY
#endif
int main() { volatile double value = NAN; return std::isnan(value) ? 0 : 1; }"
        LUDUS_MACOS_SDK_MATH_WORKS)
    if(NOT LUDUS_MACOS_SDK_MATH_WORKS)
        ludus_dependency_error("macOS dependencies" "compatible compiler, libc++ and SDK headers"
            "The NAN/INFINITY compile-link probe failed (the same contract HarfBuzz needs). Compiler: '${CMAKE_CXX_COMPILER}'; SDK: '${CMAKE_OSX_SYSROOT}'. See CMakeFiles/CMakeConfigureLog.yaml for compiler output."
            "${setup_recovery}\n  Init tests installed SDKs before building dependencies. If SDKROOT is explicitly set, unset it or select a compatible installed SDK. A deployment-target change alone does not change SDK headers.")
    endif()
endif()
