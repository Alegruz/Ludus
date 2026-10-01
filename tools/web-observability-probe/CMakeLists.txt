add_executable(ludus_web_observability_probe main.cpp)
target_link_libraries(ludus_web_observability_probe PRIVATE Ludus::FoundationLogging Ludus::FoundationProfiling)
target_include_directories(ludus_web_observability_probe PRIVATE "${PROJECT_SOURCE_DIR}/modules/foundation/logging/src")
ludus_apply_project_defaults(ludus_web_observability_probe)
target_link_options(ludus_web_observability_probe PRIVATE
    -fno-exceptions -g0 -g2 -sENVIRONMENT=web,node -sEXIT_RUNTIME=1
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1 -sFORCE_FILESYSTEM=1 "-sEXPORTED_RUNTIME_METHODS=['FS']"
)
set_target_properties(ludus_web_observability_probe PROPERTIES OUTPUT_NAME index SUFFIX ".js")
configure_file(shell.html index.html COPYONLY)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME ludus_web_observability_semantics
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_semantics.py"
        "${CMAKE_CROSSCOMPILING_EMULATOR}" "$<TARGET_FILE:ludus_web_observability_probe>" "${LUDUS_PROFILING_ENABLED}")
foreach(module containers logging profiling)
    add_test(NAME ludus_web_${module}_headers
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/build_contract/verify_header_self_sufficiency.py"
            "${PROJECT_BINARY_DIR}" "foundation/${module}")
    set_tests_properties(ludus_web_${module}_headers PROPERTIES LABELS "web;build-contract" TIMEOUT 120)
endforeach()
set_tests_properties(ludus_web_observability_semantics PROPERTIES LABELS "web;foundation" TIMEOUT 30)
