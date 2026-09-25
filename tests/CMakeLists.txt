# SDK consumer checks are intentionally configured by scripts/install-sdk
# against the installed package, not through this source-tree build.

if(LUDUS_BUILD_TESTS)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_build_contract_tests build_contract/exception_tests.cpp)
    target_link_libraries(ludus_build_contract_tests PRIVATE Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_build_contract_tests)
    ludus_enable_test_exceptions(ludus_build_contract_tests)
    add_test(NAME ludus_build_contract_tests COMMAND ludus_build_contract_tests)
    add_test(NAME ludus_compile_policy
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_contract/verify_compile_policy.py"
            "${PROJECT_BINARY_DIR}/compile_commands.json")
    add_test(NAME ludus_assert_policy
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_contract/verify_assert_policy.py"
            "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}" "${CMAKE_COMMAND}"
            "${LUDUS_BUILD_FLAVOR_ID}" "${LUDUS_ENABLE_ASSERTS}" "${LUDUS_BREAK_ON_CHECK}")
    add_test(NAME ludus_assert_headers
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/assertions/verify_headers.py"
            "${PROJECT_BINARY_DIR}" "${LUDUS_ENABLE_ASSERTS}")
    # Foundational-header architecture gates (ADR 0007):
    #  - every public header compiles standalone (self-sufficiency);
    #  - the foundational headers / public headers respect the include boundary.
    add_test(NAME ludus_header_self_sufficiency
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_contract/verify_header_self_sufficiency.py"
            "${PROJECT_BINARY_DIR}")
    add_test(NAME ludus_foundational_includes
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_foundational_includes.py"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(ludus_build_contract_tests ludus_compile_policy ludus_assert_policy ludus_assert_headers
        ludus_header_self_sufficiency ludus_foundational_includes
        PROPERTIES LABELS "build-contract" TIMEOUT 120)
endif()
