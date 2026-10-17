add_library(ludus_scene_cook STATIC
    src/scene_cook.cpp src/scene_assets.cpp src/python_compat.cpp src/python_json.cpp src/native_files.cpp src/uuid.cpp
    src/importers/pbrt/import_diagnostics.cpp src/importers/pbrt/pbrt_tokenizer.cpp src/importers/pbrt/ply_reader.cpp
    src/importers/pbrt/pbrt_parser.cpp src/importers/pbrt/shape_tessellation.cpp src/importers/pbrt/spectrum_tables.cpp
    src/importers/pbrt/transform_utilities.cpp src/importers/budget_planner.cpp src/importers/best_effort_import.cpp src/importers/authored_lighting.cpp
    src/importers/image/image_reader.cpp)
add_library(Ludus::SceneCook ALIAS ludus_scene_cook)
set_target_properties(ludus_scene_cook PROPERTIES EXPORT_NAME SceneCook)
target_sources(ludus_scene_cook PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/content/scene_cook/scene_cook.h include/ludus/content/scene_cook/scene_assets.h
    include/ludus/content/scene_cook/best_effort_import.h)
target_link_libraries(ludus_scene_cook
    PUBLIC Ludus::Content Ludus::FoundationContainers Ludus::FoundationIdentity Ludus::FoundationStrings
    PRIVATE Ludus::SceneFormat Ludus::FoundationMath Ludus::FoundationParsingJson ludus_stb_image)
# Byte identity with the float64 reference requires unfused IEEE arithmetic.
target_compile_options(ludus_scene_cook PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
ludus_apply_project_defaults(ludus_scene_cook)
install(TARGETS ludus_scene_cook EXPORT LudusTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# The CLI and golden tests need native file access (Linux and macOS hosts).
if(EMSCRIPTEN OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
    return()
endif()

# Developer tool; built in the tree, not part of the installed SDK.
add_executable(ludus_cook tool/ludus_cook.cpp)
set_target_properties(ludus_cook PROPERTIES OUTPUT_NAME ludus-cook)
# The CLI supplies operating-system entropy for new UUIDs; the entropy target
# avoids linking Platform's window backends into a command-line tool.
target_link_libraries(ludus_cook PRIVATE Ludus::SceneCook Ludus::PlatformEntropy)
ludus_apply_project_defaults(ludus_cook)

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_scene_cook_tests tests/scene_cook_tests.cpp)
    target_include_directories(ludus_scene_cook_tests PRIVATE src)
    target_compile_definitions(ludus_scene_cook_tests PRIVATE
        LUDUS_SCENE_GOLDENS_DIR="${PROJECT_SOURCE_DIR}/tests/goldens/scene_pipeline")
    target_link_libraries(ludus_scene_cook_tests PRIVATE
        Ludus::SceneCook Ludus::FoundationMath Ludus::FoundationParsingJson Ludus::FoundationStrings Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_scene_cook_tests)
    ludus_enable_test_exceptions(ludus_scene_cook_tests)
    add_test(NAME ludus_scene_cook_tests COMMAND ludus_scene_cook_tests)
    set_tests_properties(ludus_scene_cook_tests PROPERTIES LABELS "unit;content;goldens")

    add_executable(ludus_scene_assets_tests tests/scene_assets_tests.cpp)
    target_include_directories(ludus_scene_assets_tests PRIVATE src)
    target_compile_definitions(ludus_scene_assets_tests PRIVATE
        LUDUS_SCENE_GOLDENS_DIR="${PROJECT_SOURCE_DIR}/tests/goldens/scene_pipeline")
    target_link_libraries(ludus_scene_assets_tests PRIVATE
        Ludus::SceneCook Ludus::Resources Ludus::FoundationParsingJson Ludus::FoundationStrings Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_scene_assets_tests)
    ludus_enable_test_exceptions(ludus_scene_assets_tests)
    add_test(NAME ludus_scene_assets_tests COMMAND ludus_scene_assets_tests)
    set_tests_properties(ludus_scene_assets_tests PROPERTIES LABELS "unit;content;goldens")
endif()

if(LUDUS_BUILD_TESTS)
    add_executable(ludus_best_effort_import_tests tests/best_effort_import_tests.cpp)
    target_include_directories(ludus_best_effort_import_tests PRIVATE src)
    target_link_libraries(ludus_best_effort_import_tests PRIVATE
        Ludus::SceneCook Ludus::SceneFormat Ludus::Resources Ludus::FoundationMath Ludus::FoundationStrings
        Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_best_effort_import_tests)
    ludus_enable_test_exceptions(ludus_best_effort_import_tests)
    add_test(NAME ludus_best_effort_import_tests COMMAND ludus_best_effort_import_tests)
    set_tests_properties(ludus_best_effort_import_tests PROPERTIES LABELS "unit;content")
endif()

# libFuzzer target over the pbrt reader, PLY reader and image decoders (native Clang only).
option(LUDUS_BUILD_SCENE_COOK_FUZZERS "Build the instrumented best-effort importer fuzz target" OFF)
if(LUDUS_BUILD_SCENE_COOK_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Scene cook fuzzing requires native Clang/libFuzzer")
    endif()
    add_executable(ludus_scene_cook_pbrt_fuzzer tests/pbrt_fuzzer.cpp)
    target_include_directories(ludus_scene_cook_pbrt_fuzzer PRIVATE src)
    target_link_libraries(ludus_scene_cook_pbrt_fuzzer PRIVATE Ludus::SceneCook)
    ludus_apply_project_defaults(ludus_scene_cook_pbrt_fuzzer)
    target_compile_options(ludus_scene_cook_pbrt_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(ludus_scene_cook_pbrt_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
