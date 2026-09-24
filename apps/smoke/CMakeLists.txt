add_executable(ludus_smoke)
target_sources(ludus_smoke PRIVATE main.cpp)
target_link_libraries(
    ludus_smoke
    PRIVATE
        Ludus::FoundationBase
        Ludus::FoundationLogging
        Ludus::FoundationProfiling
        Ludus::Platform
        Ludus::DiagnosticsIntegration
        Ludus::GraphicsRhi
)
ludus_apply_project_defaults(ludus_smoke)
