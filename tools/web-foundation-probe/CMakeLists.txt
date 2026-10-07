add_executable(ludus_web_foundation_probe main.cpp "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/strings.cpp" "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/containers.cpp" "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/pack.cpp" "${PROJECT_SOURCE_DIR}/tests/sdk_consumer/async.cpp")
target_link_libraries(ludus_web_foundation_probe PRIVATE Ludus::FoundationContainers Ludus::FoundationBase Ludus::FoundationStrings Ludus::FoundationFilesystem)
ludus_apply_project_defaults(ludus_web_foundation_probe)
target_link_options(ludus_web_foundation_probe PRIVATE
    -fno-exceptions -g0 -g2 -sENVIRONMENT=web,node -sEXIT_RUNTIME=1
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
)
set_target_properties(ludus_web_foundation_probe PROPERTIES OUTPUT_NAME index SUFFIX ".js")
configure_file(shell.html index.html COPYONLY)

# These probes run wasm through the SDK's Node runtime; never execute a wasm
# binary as a native assertion child. Browser execution is a separate check.
enable_testing()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME ludus_web_foundation_semantics
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_semantics.py"
        "${CMAKE_CROSSCOMPILING_EMULATOR}" "$<TARGET_FILE:ludus_web_foundation_probe>" "${LUDUS_ENABLE_ASSERTS}")
add_test(NAME ludus_web_compile_policy
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/build_contract/verify_compile_policy.py"
        "${PROJECT_BINARY_DIR}/compile_commands.json")
add_test(NAME ludus_web_foundational_includes
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_foundational_includes.py" "${PROJECT_SOURCE_DIR}")

add_test(NAME ludus_web_header_self_sufficiency
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/build_contract/verify_header_self_sufficiency.py"
        "${PROJECT_BINARY_DIR}" "foundation/base")
add_test(NAME ludus_web_filesystem_header_self_sufficiency
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/build_contract/verify_header_self_sufficiency.py"
        "${PROJECT_BINARY_DIR}" "foundation/filesystem")
set_tests_properties(ludus_web_foundation_semantics ludus_web_compile_policy
    ludus_web_foundational_includes ludus_web_header_self_sufficiency
    ludus_web_filesystem_header_self_sufficiency
    PROPERTIES LABELS "web;foundation;build-contract" TIMEOUT 120)

foreach(module memory hash strings)
    add_test(NAME ludus_web_${module}_headers
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/build_contract/verify_header_self_sufficiency.py"
            "${PROJECT_BINARY_DIR}" "foundation/${module}")
    set_tests_properties(ludus_web_${module}_headers PROPERTIES LABELS "web;foundation;build-contract" TIMEOUT 120)
endforeach()
