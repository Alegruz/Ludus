add_executable(ludus_web_platform_probe main.cpp)
target_link_libraries(ludus_web_platform_probe PRIVATE Ludus::Platform)
ludus_apply_project_defaults(ludus_web_platform_probe)
target_link_options(ludus_web_platform_probe PRIVATE
    -fno-exceptions -g0 -g2 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1
    -sASSERTIONS=1 "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/node_dom_fixture.js")
set_target_properties(ludus_web_platform_probe PROPERTIES OUTPUT_NAME index SUFFIX ".js")
configure_file(shell.html index.html COPYONLY)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME ludus_web_platform_semantics
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_semantics.py"
        "${CMAKE_CROSSCOMPILING_EMULATOR}" "$<TARGET_FILE:ludus_web_platform_probe>")
add_test(NAME ludus_web_platform_headers
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/build_contract/verify_header_self_sufficiency.py"
        "${PROJECT_BINARY_DIR}" "platform")
set_tests_properties(ludus_web_platform_semantics ludus_web_platform_headers PROPERTIES LABELS "web;platform" TIMEOUT 120)
