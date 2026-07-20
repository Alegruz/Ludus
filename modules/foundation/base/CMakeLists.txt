add_library(ludus_foundation_base STATIC)
add_library(Ludus::FoundationBase ALIAS ludus_foundation_base)

set_target_properties(
    ludus_foundation_base
    PROPERTIES
        OUTPUT_NAME ludus_foundation_base
        EXPORT_NAME FoundationBase
)

find_package(Git QUIET)
set(LUDUS_GIT_REVISION "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE LUDUS_GIT_REVISION_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(LUDUS_GIT_REVISION_OUTPUT)
        set(LUDUS_GIT_REVISION "${LUDUS_GIT_REVISION_OUTPUT}")
    endif()
endif()

set(LUDUS_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
set(LUDUS_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
set(LUDUS_FOUNDATION_BASE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/build_metadata.hpp.in"
    "${LUDUS_FOUNDATION_BASE_GENERATED_INCLUDE_DIR}/ludus/foundation/base/build_metadata.hpp"
    @ONLY
    NEWLINE_STYLE UNIX
)

target_sources(
    ludus_foundation_base
    PRIVATE
        src/version.cpp
        src/internal/version_format.hpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            include
            "${LUDUS_FOUNDATION_BASE_GENERATED_INCLUDE_DIR}"
        FILES
            include/ludus/foundation/base/version.hpp
            "${LUDUS_FOUNDATION_BASE_GENERATED_INCLUDE_DIR}/ludus/foundation/base/build_metadata.hpp"
)

target_include_directories(
    ludus_foundation_base
    PRIVATE
        src
)

ludus_apply_project_defaults(ludus_foundation_base)

install(
    TARGETS ludus_foundation_base
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)

    add_executable(ludus_foundation_base_tests)
    target_sources(ludus_foundation_base_tests PRIVATE tests/version_tests.cpp)
    target_link_libraries(
        ludus_foundation_base_tests
        PRIVATE
            Ludus::FoundationBase
            Catch2::Catch2WithMain
    )
    ludus_apply_project_defaults(ludus_foundation_base_tests)

    add_test(NAME ludus_foundation_base_tests COMMAND ludus_foundation_base_tests)
    set_tests_properties(ludus_foundation_base_tests PROPERTIES LABELS "unit;foundation")
endif()
