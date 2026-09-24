# Startup-only development diagnostic integration.
#
# Lives ABOVE FoundationBase: it depends on Base to reuse the report/control
# transports and the shared CI-detection primitive, but Base does NOT depend on
# this target. It is not part of the engine's public module graph and pulls in
# neither Logging nor Platform. Applications/tools link it to establish a
# diagnostic session at healthy startup.
add_library(ludus_diagnostics_integration STATIC)
add_library(Ludus::DiagnosticsIntegration ALIAS ludus_diagnostics_integration)

target_sources(
    ludus_diagnostics_integration
    PRIVATE
        src/session.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES
            include/ludus/diagnostics/session.hpp
)

target_link_libraries(ludus_diagnostics_integration PUBLIC Ludus::FoundationBase)
ludus_apply_project_defaults(ludus_diagnostics_integration)

# The Python development helper is a standalone tool, not linked into anything.
# Install it next to the SDK tools; do not embed this checkout's absolute path
# into the installed SDK (the smoke integration locates it via env/PATH at run
# time, not a baked-in path).
install(
    PROGRAMS ludus_diagnostic_helper.py
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME ludus_diagnostic_helper
)

if(LUDUS_BUILD_TESTS)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

    # Startup/report-delivery test child. Links the integration layer (and thus
    # Base) and calls InitializeDiagnosticSession; it emits reports independently
    # of Logging and never triggers a fatal action.
    add_executable(ludus_diagnostic_startup_child tests/diagnostic_startup_child.cpp)
    target_link_libraries(ludus_diagnostic_startup_child PRIVATE Ludus::DiagnosticsIntegration)
    ludus_apply_project_defaults(ludus_diagnostic_startup_child)

    add_test(NAME ludus_diagnostic_startup
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_tests.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/ludus_diagnostic_helper.py"
            "$<TARGET_FILE:ludus_diagnostic_startup_child>")
    set_tests_properties(ludus_diagnostic_startup PROPERTIES LABELS "tools;diagnostic-startup" TIMEOUT 60)
endif()
