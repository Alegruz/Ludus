cmake_minimum_required(VERSION 3.29)

set(LUDUS_ENGINE_PROJECT_NAME "Ludus")
set(LUDUS_ENGINE_EXPORT_NAMESPACE "Ludus::")
set(LUDUS_ENGINE_PACKAGE_NAME "Ludus")

project(${LUDUS_ENGINE_PROJECT_NAME} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

include(EngineOptions)
include(EngineCompilerWarnings)
include(EngineSanitizers)
include(EngineTargets)

option(LUDUS_BUILD_TESTS "Build Ludus tests" ON)
option(LUDUS_BUILD_SMOKE_APP "Build the smoke executable" ON)
option(LUDUS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(LUDUS_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)
option(LUDUS_ENABLE_CCACHE "Use ccache when available" OFF)

if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
    set(LUDUS_WARNINGS_AS_ERRORS_DEFAULT ON)
else()
    set(LUDUS_WARNINGS_AS_ERRORS_DEFAULT OFF)
endif()

option(LUDUS_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" ${LUDUS_WARNINGS_AS_ERRORS_DEFAULT})
option(LUDUS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LUDUS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

ludus_configure_project_options(ludus_project_options)
ludus_configure_project_warnings(ludus_project_warnings)
ludus_configure_project_sanitizers(ludus_project_options)

if(LUDUS_ENABLE_CCACHE)
    find_program(LUDUS_CCACHE_PROGRAM ccache)
    if(LUDUS_CCACHE_PROGRAM)
        message(STATUS "Using ccache: ${LUDUS_CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${LUDUS_CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${LUDUS_CCACHE_PROGRAM}")
    else()
        message(WARNING "LUDUS_ENABLE_CCACHE is ON, but ccache was not found")
    endif()
endif()

add_subdirectory(modules/foundation/base)

if(LUDUS_BUILD_SMOKE_APP)
    add_subdirectory(apps/smoke)
endif()

if(LUDUS_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(tests)

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
