add_library(ludus_platform STATIC)
add_library(Ludus::Platform ALIAS ludus_platform)

set_target_properties(
    ludus_platform
    PROPERTIES
        OUTPUT_NAME ludus_platform
        EXPORT_NAME Platform
)

# Wayland is the preferred Linux windowing backend, but it is optional at build
# time: if the development packages, protocol definitions, and wayland-scanner
# are all present we compile the Wayland backend; otherwise the module still
# builds against a headless null backend (see src/window.cpp). This keeps a
# clean clone buildable on headless CI runners and hosts without Wayland.
set(LUDUS_PLATFORM_WAYLAND_AVAILABLE OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
        pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
        find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)

        if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_FOUND AND WAYLAND_SCANNER_EXECUTABLE)
            set(LUDUS_PLATFORM_WAYLAND_AVAILABLE ON)
        endif()
    endif()
endif()

option(LUDUS_PLATFORM_ENABLE_WAYLAND "Build the Wayland windowing backend" ${LUDUS_PLATFORM_WAYLAND_AVAILABLE})

if(LUDUS_PLATFORM_ENABLE_WAYLAND AND NOT LUDUS_PLATFORM_WAYLAND_AVAILABLE)
    message(FATAL_ERROR
        "LUDUS_PLATFORM_ENABLE_WAYLAND is ON but the Wayland build prerequisites were not found. "
        "Install the Wayland client dev headers, wayland-protocols, and wayland-scanner "
        "(see AGENTS.md / ./init.sh), or leave the option unset to build the headless backend.")
endif()

target_sources(
    ludus_platform
    PRIVATE
        src/window.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            include
        FILES
            include/ludus/platform/config.h
            include/ludus/platform/log_categories.h
            include/ludus/platform/base/window.h
            include/ludus/platform/wayland/window.h
            include/ludus/platform/headless/window.h
)

target_include_directories(
    ludus_platform
    PRIVATE
        src
)

target_link_libraries(
    ludus_platform
    PUBLIC
        Ludus::FoundationBase
        Ludus::FoundationLogging
)

if(LUDUS_PLATFORM_ENABLE_WAYLAND)
    message(STATUS "Ludus platform: Wayland backend enabled")

    pkg_get_variable(WAYLAND_PROTOCOLS_PKGDATADIR wayland-protocols pkgdatadir)

    set(LUDUS_PLATFORM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(LUDUS_PLATFORM_WAYLAND_GENERATED_DIR "${LUDUS_PLATFORM_GENERATED_DIR}/wayland")
    set(LUDUS_PLATFORM_XDG_SHELL_XML "${WAYLAND_PROTOCOLS_PKGDATADIR}/stable/xdg-shell/xdg-shell.xml")
    set(LUDUS_PLATFORM_XDG_SHELL_HEADER "${LUDUS_PLATFORM_WAYLAND_GENERATED_DIR}/xdg-shell-client-protocol.h")
    set(LUDUS_PLATFORM_XDG_SHELL_CODE "${LUDUS_PLATFORM_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.cpp")

    add_custom_command(
        OUTPUT
            "${LUDUS_PLATFORM_XDG_SHELL_HEADER}"
            "${LUDUS_PLATFORM_XDG_SHELL_CODE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LUDUS_PLATFORM_WAYLAND_GENERATED_DIR}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${LUDUS_PLATFORM_XDG_SHELL_XML}" "${LUDUS_PLATFORM_XDG_SHELL_HEADER}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${LUDUS_PLATFORM_XDG_SHELL_XML}" "${LUDUS_PLATFORM_XDG_SHELL_CODE}"
        DEPENDS
            "${LUDUS_PLATFORM_XDG_SHELL_XML}"
        VERBATIM
    )

    add_custom_target(
        ludus_platform_wayland_protocols
        DEPENDS
            "${LUDUS_PLATFORM_XDG_SHELL_HEADER}"
            "${LUDUS_PLATFORM_XDG_SHELL_CODE}"
    )

    target_sources(ludus_platform PRIVATE src/window_wayland.cpp)
    target_include_directories(ludus_platform PRIVATE "${LUDUS_PLATFORM_WAYLAND_GENERATED_DIR}")
    add_dependencies(ludus_platform ludus_platform_wayland_protocols)
    target_link_libraries(ludus_platform PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(ludus_platform PRIVATE LUDUS_PLATFORM_WAYLAND=1)
else()
    message(STATUS "Ludus platform: Wayland not available, building headless window backend")
    target_sources(ludus_platform PRIVATE src/window_headless.cpp)
    target_compile_definitions(ludus_platform PRIVATE LUDUS_PLATFORM_HEADLESS=1)
endif()

ludus_apply_project_defaults(ludus_platform)

install(
    TARGETS ludus_platform
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
