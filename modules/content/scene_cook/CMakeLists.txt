add_library(ludus_scene_cook STATIC src/scene_cook.cpp src/python_compat.cpp src/native_files.cpp)
add_library(Ludus::SceneCook ALIAS ludus_scene_cook)
set_target_properties(ludus_scene_cook PROPERTIES EXPORT_NAME SceneCook)
target_sources(ludus_scene_cook PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/content/scene_cook/scene_cook.h)
target_link_libraries(ludus_scene_cook
    PUBLIC Ludus::Content Ludus::FoundationContainers
    PRIVATE Ludus::FoundationMath Ludus::FoundationParsingJson Ludus::FoundationStrings)
# Byte identity with the float64 reference requires unfused IEEE arithmetic.
target_compile_options(ludus_scene_cook PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
ludus_apply_project_defaults(ludus_scene_cook)
install(TARGETS ludus_scene_cook EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(NOT EMSCRIPTEN)
    add_executable(ludus_cook tool/ludus_cook.cpp)
    set_target_properties(ludus_cook PROPERTIES OUTPUT_NAME ludus-cook)
    target_link_libraries(ludus_cook PRIVATE Ludus::SceneCook)
    ludus_apply_project_defaults(ludus_cook)
    install(TARGETS ludus_cook RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(LUDUS_BUILD_TESTS AND NOT EMSCRIPTEN)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_scene_cook_tests tests/scene_cook_tests.cpp)
    target_include_directories(ludus_scene_cook_tests PRIVATE src)
    target_compile_definitions(ludus_scene_cook_tests PRIVATE
        LUDUS_SCENE_GOLDENS_DIR="${PROJECT_SOURCE_DIR}/tests/goldens/scene_pipeline")
    target_link_libraries(ludus_scene_cook_tests PRIVATE
        Ludus::SceneCook Ludus::FoundationParsingJson Ludus::FoundationStrings Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_scene_cook_tests)
    ludus_enable_test_exceptions(ludus_scene_cook_tests)
    add_test(NAME ludus_scene_cook_tests COMMAND ludus_scene_cook_tests)
    set_tests_properties(ludus_scene_cook_tests PROPERTIES LABELS "unit;content;goldens")
endif()
