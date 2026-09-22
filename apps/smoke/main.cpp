#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/platform/base/window.h>

#include <string>

using namespace ludus::foundation::logging;

// Ludus does not use C++ exceptions (see AGENTS.md); engine code is compiled
// with -fno-exceptions, so there is nothing to catch here. Fatal conditions are
// reported through LUDUS_LOG_FATAL and surfaced via the return code.
int main()
{
    LogConfig config{};
    config.GlobalLevel = LogLevel::Trace;
    config.EnableConsole = true;
    config.EnableDebugger = true;
    config.EnableFile = false;
    LogSystem::Initialize(config);

    SetCurrentThreadName("Main");

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
