add_library(ludus_foundation_parsing STATIC)
add_library(Ludus::FoundationParsing ALIAS ludus_foundation_parsing)
set_target_properties(ludus_foundation_parsing PROPERTIES EXPORT_NAME FoundationParsing)
target_sources(ludus_foundation_parsing PRIVATE src/parsing.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/parsing/parsing.hpp)
target_link_libraries(ludus_foundation_parsing PUBLIC Ludus::FoundationBase)
ludus_apply_project_defaults(ludus_foundation_parsing)
install(TARGETS ludus_foundation_parsing EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

add_library(ludus_foundation_parsing_json STATIC)
add_library(Ludus::FoundationParsingJson ALIAS ludus_foundation_parsing_json)
set_target_properties(ludus_foundation_parsing_json PROPERTIES EXPORT_NAME FoundationParsingJson)
target_sources(ludus_foundation_parsing_json PRIVATE src/json.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/parsing/json.hpp)
target_link_libraries(ludus_foundation_parsing_json PUBLIC
    Ludus::FoundationParsing Ludus::FoundationMemory PRIVATE ludus_yyjson)
ludus_apply_project_defaults(ludus_foundation_parsing_json)
install(TARGETS ludus_foundation_parsing_json EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_foundation_parsing_tests tests/parsing_tests.cpp tests/json_tests.cpp)
    target_link_libraries(ludus_foundation_parsing_tests PRIVATE
        Ludus::FoundationParsingJson Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_foundation_parsing_tests)
    ludus_enable_test_exceptions(ludus_foundation_parsing_tests)
    add_test(NAME ludus_foundation_parsing_tests COMMAND ludus_foundation_parsing_tests)
    set_tests_properties(ludus_foundation_parsing_tests PROPERTIES LABELS "unit;foundation;parsing" TIMEOUT 60)
endif()

if(EMSCRIPTEN AND LUDUS_BUILD_WEB_PROBES)
    add_executable(ludus_parsing_web_smoke tests/web_smoke.cpp)
    target_link_libraries(ludus_parsing_web_smoke PRIVATE Ludus::FoundationParsingJson)
    ludus_apply_project_defaults(ludus_parsing_web_smoke)
    target_link_options(ludus_parsing_web_smoke PRIVATE
        -fno-exceptions -sENVIRONMENT=web,node -sEXIT_RUNTIME=1
        -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
    set_target_properties(ludus_parsing_web_smoke PROPERTIES OUTPUT_NAME parsing-smoke SUFFIX ".js")
    enable_testing()
    add_test(NAME ludus_parsing_web_smoke COMMAND ludus_parsing_web_smoke)
    set_tests_properties(ludus_parsing_web_smoke PROPERTIES LABELS "web;foundation;parsing" TIMEOUT 60)
endif()
