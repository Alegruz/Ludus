# Catch2 3.4.0 exposes stream/chrono formatting in every assertion TU. Preserve
# its ABI and evaluation, but compile stream insertion at one test-only boundary.
# The local Conan recipe rebuilds the runtime with these same licensed headers.
if(LUDUS_BUILD_TESTS AND NOT EMSCRIPTEN)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_package(Catch2 3.4.0 EXACT REQUIRED CONFIG)
    get_target_property(catch_includes Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
    # Recompute after dependency relocation or a recipe update; never retain an old package path.
    unset(LUDUS_CATCH_HEADER_ROOT CACHE)
    find_path(LUDUS_CATCH_HEADER_ROOT catch2/catch_tostring.hpp
        PATHS ${Catch2_INCLUDE_DIRS} ${catch_includes} NO_DEFAULT_PATH REQUIRED)
    if(NOT EXISTS "${LUDUS_CATCH_HEADER_ROOT}/catch2/internal/catch_stream.hpp")
        message(FATAL_ERROR "Catch2 is stale: initialize the pinned Ludus diagnostic recipe with ./init.sh --with-tests")
    endif()

    add_library(ludus_catch_header_boundary_tests OBJECT tests/support/catch_header_boundary.cpp)
    target_link_libraries(ludus_catch_header_boundary_tests PRIVATE Catch2::Catch2)
    ludus_apply_project_defaults(ludus_catch_header_boundary_tests)
    ludus_enable_test_exceptions(ludus_catch_header_boundary_tests)

    add_executable(ludus_test_diagnostics_tests tests/support/catch_diagnostics_tests.cpp)
    target_link_libraries(ludus_test_diagnostics_tests PRIVATE Ludus::FoundationBase Ludus::FoundationContainers Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_test_diagnostics_tests)
    ludus_enable_test_exceptions(ludus_test_diagnostics_tests)
    add_test(NAME ludus_test_diagnostics_values COMMAND ludus_test_diagnostics_tests)
    add_test(NAME ludus_test_diagnostics_failures
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/support/check_catch_diagnostics.py"
            "$<TARGET_FILE:ludus_test_diagnostics_tests>")
endif()
