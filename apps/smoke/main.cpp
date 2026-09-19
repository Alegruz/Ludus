#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/logging/log.hpp>

using namespace ludus::foundation::logging;

int main()
{
#if 0
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

    LogSystem::Shutdown();
#endif
    return 0;
}
