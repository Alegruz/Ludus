#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/platform/base/window.h>

#include <exception>
#include <iostream>

using namespace ludus::foundation::logging;

int main()
try
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
catch (const std::exception& exception)
{
    std::cerr << "Unhandled exception in smoke app: " << exception.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unhandled non-standard exception in smoke app\n";
    return 1;
}
