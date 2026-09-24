# External diagnostic helper: a standalone launcher/collector that runs OUTSIDE
# the engine process. It links Ludus::FoundationBase only to reuse the public
# control-protocol header (ControlFrame + version constants); it depends on no
# higher module and pulls in neither Logging nor Platform. The linker drops the
# engine objects it does not reference. Keeping the protocol in one public header
# guarantees the helper and the engine agree on the wire format.
add_executable(ludus_diagnostic_helper)
target_sources(ludus_diagnostic_helper PRIVATE main.cpp)
target_link_libraries(ludus_diagnostic_helper PRIVATE Ludus::FoundationBase)
ludus_apply_project_defaults(ludus_diagnostic_helper)

install(
    TARGETS ludus_diagnostic_helper
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
