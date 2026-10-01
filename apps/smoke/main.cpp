#include "internal/application.h"
#include <ludus/diagnostics/session.hpp>
#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/foundation/profiling/profiling.hpp>
#include <ludus/foundation/profiling/trace_system.hpp>

using namespace ludus::foundation::logging;

// Ludus does not use C++ exceptions (see AGENTS.md); engine code is compiled
// with -fno-exceptions, so there is nothing to catch here. Fatal conditions are
// reported through LUDUS_LOG_FATAL and surfaced via the return code.
int main()
{
    // Initialize the diagnostic transport BEFORE any engine worker or the logger
    // is set up, so an assertion during startup/logger init is still captured and
    // (in an eligible local non-CI Debug run with a helper) the control channel is
    // ready. Interactive presentation is auto-resolved: CI and headless runs stay
    // report-only, require no display/dialog helper, and never block. This never
    // launches a helper/UI and never changes any assertion's fatal action.
    [[maybe_unused]] const ludus::diagnostics::SessionResult diagnostics =
        ludus::diagnostics::InitializeDiagnosticSession();

    LogConfig config{};
    config.GlobalLevel = LogLevel::Trace;
    config.EnableConsole = true;
    config.EnableDebugger = true;
    config.EnableFile = false;
    LogSystem::Initialize(config);

    SetCurrentThreadName("Main");

    LUDUS_LOG_INFO(LOG_CORE,
                   "Diagnostics: report={} control={} mode={} ci={}",
                   diagnostics.ReportTransportReady,
                   diagnostics.ControlEndpointReady,
                   diagnostics.Mode == ludus::diagnostics::SessionMode::Interactive ? "interactive" : "report-only",
                   diagnostics.DetectedCi);

    // Profiling demo: register the main thread and capture the startup phase.
    // In an enabled build this records CPU scopes and writes a Perfetto/Chrome
    // trace on shutdown; in a Release (profiling-disabled) build every macro
    // compiles to nothing (see docs/architecture/profiling-final.md).
    ludus::foundation::profiling::RegisterThreadForTrace("Main");
    const bool profilingCapture = ludus::foundation::profiling::BeginCapture();

    LUDUS_LOG_INFO(LOG_CORE, "Ludus {} starting", ludus::foundation::version_string());
    LUDUS_LOG_INFO(LOG_CORE, "Revision: {}", ludus::foundation::git_revision());
    LUDUS_LOG_INFO(LOG_CORE, "Compiler: {}", ludus::foundation::compiler_identity());

    ludus::smoke::Application application;
    const bool started = application.Start();
    auto state = application.GetState();
    while (started && (state == ludus::smoke::State::Loading || state == ludus::smoke::State::Playing))
    {
        state = application.Tick();
    }

    // Close the capture and write a trace next to the executable. Guarded so a
    // profiling-disabled build (where BeginCapture returned false) does nothing.
    if (profilingCapture)
    {
        ludus::foundation::profiling::EndCapture();
        if (ludus::foundation::profiling::ExportPerfettoTrace("ludus_smoke_trace.json"))
        {
            LUDUS_LOG_INFO(LOG_CORE, "Wrote profiling trace: ludus_smoke_trace.json");
        }
    }

    application.Shutdown();
    LogSystem::Shutdown();
    return state == ludus::smoke::State::Failed || state == ludus::smoke::State::DeviceLost ? 1 : 0;
}
