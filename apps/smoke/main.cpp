#include <ludus/diagnostics/session.hpp>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/platform/base/window.h>

#include <string>

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
    const ludus::diagnostics::SessionResult diagnostics = ludus::diagnostics::InitializeDiagnosticSession();

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
    LUDUS_LOG_INFO(LOG_CORE, "Ludus {} starting", ludus::foundation::version_string());
    LUDUS_LOG_INFO(LOG_CORE, "Revision: {}", ludus::foundation::git_revision());
    LUDUS_LOG_INFO(LOG_CORE, "Compiler: {}", ludus::foundation::compiler_identity());

    ludus::platform::WindowManager windowManager;
    constexpr ludus::platform::WindowManager::InitializeInfo info = {};
    if (!windowManager.Initialize(info))
    {
        LUDUS_LOG_FATAL(LOG_CORE, "Failed to initialize window manager");
        LogSystem::Shutdown();
        return 1;
    }

    const ludus::platform::Window::CreateInfo createInfo = {
        .Name = std::string("Test Window"),
    };

    ludus::foundation::core::UniquePtr<ludus::platform::Window> window = nullptr;
    if (!windowManager.CreateWindow(createInfo, window))
    {
        LUDUS_LOG_FATAL(LOG_CORE, "Failed to create window");
        LogSystem::Shutdown();
        return 1;
    }

    while (window->HandleEvent({}))
    {
        // Main loop
    }

    LogSystem::Shutdown();
    return 0;
}
