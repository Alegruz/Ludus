add_library(ludus_platform STATIC)
add_library(Ludus::Platform ALIAS ludus_platform)

set_target_properties(
    ludus_platform
    PROPERTIES
        OUTPUT_NAME ludus_platform
        EXPORT_NAME Platform
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)

pkg_get_variable(WAYLAND_PROTOCOLS_PKGDATADIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

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

target_sources(
    ludus_platform
    PRIVATE
        src/window.cpp
        src/window_wayland.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            include
        FILES
            include/ludus/platform/config.h
            include/ludus/platform/base/window.h
            include/ludus/platform/wayland/window.h
)

target_include_directories(
    ludus_platform
    PRIVATE
    "${LUDUS_PLATFORM_WAYLAND_GENERATED_DIR}"
        src
)

add_dependencies(ludus_platform ludus_platform_wayland_protocols)

target_link_libraries(
    ludus_platform
    PUBLIC
        Ludus::FoundationBase
    PRIVATE
        PkgConfig::WAYLAND_CLIENT
)

ludus_apply_project_defaults(ludus_platform)

install(
    TARGETS ludus_platform
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
