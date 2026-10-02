cmake_minimum_required(VERSION 3.29)

set(LUDUS_ENGINE_PROJECT_NAME "Ludus")
set(LUDUS_ENGINE_EXPORT_NAMESPACE "Ludus::")
set(LUDUS_ENGINE_PACKAGE_NAME "Ludus")

project(${LUDUS_ENGINE_PROJECT_NAME} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(EMSCRIPTEN)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(EngineOptions)
include(EngineCompilerWarnings)
include(EngineSanitizers)
include(EngineTargets)
include(EnginePch)
include(LudusShaders)

option(LUDUS_BUILD_TESTS "Build Ludus tests" ON)
option(LUDUS_BUILD_SMOKE_APP "Build the smoke executable" ON)
option(LUDUS_BUILD_SHADER_PROBE "Build the isolated offline shader feasibility probe" OFF)
# The Editor workspace (apps/editor) is an OPTIONAL developer tool, OFF by
# default. When OFF, configure/build/install and browser targets must not search
# for, link, or export Qt. Qt discovery happens only inside the enabled app
# subdirectory (see apps/editor/CMakeLists.txt), never globally. See
# .kiro/specs/editor-workspace/{requirements,design}.md (E01/E02).
option(LUDUS_BUILD_EDITOR "Build the optional native Qt editor workspace (apps/editor)" OFF)
if(EMSCRIPTEN)
    # Native Catch2 test targets are not browser executables.
    if(LUDUS_BUILD_TESTS)
        message(FATAL_ERROR "Use a web-emscripten preset; native tests are not browser targets")
    endif()
endif()
option(LUDUS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(LUDUS_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

# ccache defaults on when the environment requests it (e.g. CI exports
# LUDUS_ENABLE_CCACHE=1), otherwise off for plain local builds.
if(DEFINED ENV{LUDUS_ENABLE_CCACHE} AND NOT "$ENV{LUDUS_ENABLE_CCACHE}" STREQUAL "" AND NOT "$ENV{LUDUS_ENABLE_CCACHE}" STREQUAL "0")
    set(LUDUS_ENABLE_CCACHE_DEFAULT ON)
else()
    set(LUDUS_ENABLE_CCACHE_DEFAULT OFF)
endif()

option(LUDUS_ENABLE_CCACHE "Use ccache when available" ${LUDUS_ENABLE_CCACHE_DEFAULT})
option(LUDUS_ENABLE_TIME_TRACE "Emit Clang -ftime-trace per-TU compile profiles" OFF)

if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
    set(LUDUS_WARNINGS_AS_ERRORS_DEFAULT ON)
else()
    set(LUDUS_WARNINGS_AS_ERRORS_DEFAULT OFF)
endif()

option(LUDUS_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" ${LUDUS_WARNINGS_AS_ERRORS_DEFAULT})
option(LUDUS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LUDUS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

include(EngineBuildFlavor)
ludus_configure_project_options(ludus_project_options)
ludus_configure_project_warnings(ludus_project_warnings)
ludus_configure_project_sanitizers(ludus_project_options)

if(LUDUS_ENABLE_CCACHE)
    find_program(LUDUS_CCACHE_PROGRAM ccache)
    if(LUDUS_CCACHE_PROGRAM)
        message(STATUS "Using ccache: ${LUDUS_CCACHE_PROGRAM}")
        # Wrap compilation only. ccache is a compiler cache, not a linker, so it
        # must not be attached to the link rule (doing so corrupts link commands).
        set(CMAKE_C_COMPILER_LAUNCHER "${LUDUS_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${LUDUS_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
    else()
        message(WARNING "LUDUS_ENABLE_CCACHE is ON, but ccache was not found")
    endif()
endif()

configure_file(cmake/CheckSdkVariant.cmake.in "${PROJECT_BINARY_DIR}/cmake/CheckSdkVariant.cmake" @ONLY)
install(SCRIPT "${PROJECT_BINARY_DIR}/cmake/CheckSdkVariant.cmake" ALL_COMPONENTS)

add_subdirectory(modules/foundation/base)
add_subdirectory(modules/foundation/containers)
# FoundationMath depends only on FoundationBase. It is added before any consumer
# and outside the native-only branch so native and browser targets share it.
add_subdirectory(modules/foundation/math)
add_subdirectory(modules/foundation/logging)
add_subdirectory(modules/foundation/profiling)
# Input is configured before Platform: Platform depends on Input for the
# normalized keyboard record/sink vocabulary (one-way dependency).
add_subdirectory(modules/input)
if(EMSCRIPTEN)
    add_subdirectory(modules/platform)
    add_subdirectory(modules/graphics/rhi)
    # NOTE: Ludus::Text is intentionally NOT built for the Emscripten target yet.
    # Its FreeType/HarfBuzz dependencies are resolved natively through Conan; the
    # separate pinned web source-bootstrap for those libraries is still pending
    # (see .kiro/specs/text-font-rendering/tasks.md F0 "web source-bootstrap
    # pending"). Add it back here once that web dependency path exists, so the
    # browser configure does not fail finding freetype/harfbuzz.
    add_subdirectory(tools/web-rhi-probe)
    add_subdirectory(tools/web-frame-probe)
    add_subdirectory(tools/web-platform-probe)
    add_subdirectory(tools/web-foundation-probe)
    add_subdirectory(tools/web-math-probe)
    add_subdirectory(tools/web-observability-probe)
    if(LUDUS_BUILD_SMOKE_APP)
        add_subdirectory(apps/smoke)
    endif()
else()
    add_subdirectory(modules/platform)
    add_subdirectory(modules/graphics/rhi)
    add_subdirectory(modules/text)

    # Startup-only development diagnostic integration (depends on Base; Base does
    # not depend on it). Provides InitializeDiagnosticSession and the Python helper.
    add_subdirectory(tools/diagnostics)

    if(LUDUS_BUILD_SMOKE_APP)
        add_subdirectory(apps/smoke)
        add_subdirectory(apps/input_demo)
    endif()

    # Optional native editor workspace. Guarded so a default (OFF) native
    # configure never calls find_package(Qt6); the subdirectory owns all Qt
    # discovery and the unsupported-host diagnostic.
    if(LUDUS_BUILD_EDITOR)
        add_subdirectory(apps/editor)
    endif()

    if(LUDUS_BUILD_TESTS)
        enable_testing()
    endif()

    add_subdirectory(tests)
endif()

if(LUDUS_BUILD_SHADER_PROBE)
    add_subdirectory(tools/shader-probe)
endif()

install(
    EXPORT LudusTargets
    NAMESPACE ${LUDUS_ENGINE_EXPORT_NAMESPACE}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LUDUS_ENGINE_PACKAGE_NAME}"
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/LudusConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/cmake/LudusConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LUDUS_ENGINE_PACKAGE_NAME}"
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/cmake/LudusConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/LudusSdkManifest.json.in"
    "${PROJECT_BINARY_DIR}/cmake/LudusSdkManifest.json"
    @ONLY
    NEWLINE_STYLE UNIX
)

install(
    FILES
        "${PROJECT_BINARY_DIR}/cmake/LudusConfig.cmake"
        "${PROJECT_BINARY_DIR}/cmake/LudusConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LUDUS_ENGINE_PACKAGE_NAME}"
)

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/${LUDUS_ENGINE_PACKAGE_NAME}/licenses")
install(FILES "${PROJECT_BINARY_DIR}/cmake/LudusSdkManifest.json" DESTINATION "${CMAKE_INSTALL_DATADIR}/${LUDUS_ENGINE_PACKAGE_NAME}")

install(FILES cmake/LudusShaders.cmake DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Ludus")
install(FILES cmake/shaders/compile_shader.py config/shader_toolchain.json DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Ludus/shaders")
