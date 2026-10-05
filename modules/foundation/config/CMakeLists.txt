add_library(ludus_foundation_config STATIC)
add_library(Ludus::FoundationConfig ALIAS ludus_foundation_config)
set_target_properties(ludus_foundation_config PROPERTIES EXPORT_NAME FoundationConfig)
target_sources(ludus_foundation_config PRIVATE src/config.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/config/config.hpp)
target_link_libraries(ludus_foundation_config PUBLIC Ludus::FoundationMemory Ludus::FoundationStrings
    PRIVATE Ludus::FoundationMath)
ludus_apply_project_defaults(ludus_foundation_config)
install(TARGETS ludus_foundation_config EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

add_library(ludus_foundation_config_json STATIC)
add_library(Ludus::FoundationConfigJson ALIAS ludus_foundation_config_json)
set_target_properties(ludus_foundation_config_json PROPERTIES EXPORT_NAME FoundationConfigJson)
target_sources(ludus_foundation_config_json PRIVATE src/json.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/config/json.hpp)
target_link_libraries(ludus_foundation_config_json PUBLIC Ludus::FoundationConfig Ludus::FoundationParsingJson)
ludus_apply_project_defaults(ludus_foundation_config_json)
install(TARGETS ludus_foundation_config_json EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_foundation_config_tests tests/config_tests.cpp tests/json_tests.cpp)
    target_link_libraries(ludus_foundation_config_tests PRIVATE Ludus::FoundationConfigJson Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_foundation_config_tests)
    ludus_enable_test_exceptions(ludus_foundation_config_tests)
    add_test(NAME ludus_foundation_config_tests COMMAND ludus_foundation_config_tests)
    set_tests_properties(ludus_foundation_config_tests PROPERTIES LABELS "unit;foundation;configuration" TIMEOUT 60)
endif()
if(EMSCRIPTEN AND LUDUS_BUILD_WEB_PROBES)
    add_executable(ludus_config_web_smoke tests/web_smoke.cpp)
    target_link_libraries(ludus_config_web_smoke PRIVATE Ludus::FoundationConfigJson)
    ludus_apply_project_defaults(ludus_config_web_smoke)
    target_link_options(ludus_config_web_smoke PRIVATE -fno-exceptions -sENVIRONMENT=web,node -sEXIT_RUNTIME=1
        -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
    set_target_properties(ludus_config_web_smoke PROPERTIES OUTPUT_NAME config-smoke SUFFIX ".js")
    enable_testing()
    add_test(NAME ludus_config_web_smoke COMMAND ludus_config_web_smoke)
    set_tests_properties(ludus_config_web_smoke PROPERTIES LABELS "web;foundation;configuration" TIMEOUT 60)
endif()
