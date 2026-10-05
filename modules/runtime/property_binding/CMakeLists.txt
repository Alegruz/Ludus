add_library(ludus_runtime_property_binding STATIC src/scalars.cpp)
add_library(Ludus::GamePropertyBinding ALIAS ludus_runtime_property_binding)
set_target_properties(ludus_runtime_property_binding PROPERTIES EXPORT_NAME GamePropertyBinding)
target_sources(ludus_runtime_property_binding PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/runtime/property_binding/scalars.hpp)
target_link_libraries(ludus_runtime_property_binding PUBLIC Ludus::FoundationReflection Ludus::GameApi)
set_target_properties(ludus_runtime_property_binding PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
ludus_apply_project_defaults(ludus_runtime_property_binding)
install(TARGETS ludus_runtime_property_binding EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(LUDUS_BUILD_TESTS OR (EMSCRIPTEN AND LUDUS_BUILD_WEB_PROBES))
    ludus_generate_reflection(TARGET sample_game_reflection SCHEMA "${PROJECT_SOURCE_DIR}/examples/live-edit-game/body.schema.json"
        BASELINE "${PROJECT_SOURCE_DIR}/examples/live-edit-game/body.schema.baseline.json")
    ludus_generate_reflection(TARGET reflection_test_record SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/tests/record.schema.json")
endif()
if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_reflection_tests tests/scalars_tests.cpp tests/game_tests.cpp
        "${PROJECT_SOURCE_DIR}/examples/live-edit-game/src/game.cpp")
    target_link_libraries(ludus_reflection_tests PRIVATE Ludus::GamePropertyBinding sample_game_reflection
        reflection_test_record Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_reflection_tests)
    ludus_enable_test_exceptions(ludus_reflection_tests)
    add_test(NAME ludus_reflection_tests COMMAND ludus_reflection_tests)
    set_tests_properties(ludus_reflection_tests PROPERTIES LABELS "unit;reflection;serialization;editor-binding" TIMEOUT 60)
endif()
if(EMSCRIPTEN AND LUDUS_BUILD_WEB_PROBES)
    add_executable(ludus_reflection_web_smoke tests/web_smoke.cpp)
    target_link_libraries(ludus_reflection_web_smoke PRIVATE Ludus::GamePropertyBinding
        sample_game_reflection reflection_test_record)
    ludus_apply_project_defaults(ludus_reflection_web_smoke)
    target_link_options(ludus_reflection_web_smoke PRIVATE -fno-exceptions -sENVIRONMENT=web,node
        -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
    set_target_properties(ludus_reflection_web_smoke PROPERTIES OUTPUT_NAME reflection-smoke SUFFIX ".js")
    enable_testing()
    add_test(NAME ludus_reflection_web_smoke COMMAND ludus_reflection_web_smoke)
    set_tests_properties(ludus_reflection_web_smoke PROPERTIES LABELS "web;reflection;serialization" TIMEOUT 60)
endif()
