#include <ludus/foundation/base/version.hpp>
#include <ludus/foundation/logging/log.hpp>

using namespace ludus::foundation::logging;

int main()
{
    // Console-only synchronous logging is enough for the smoke app; there is no
    // platform layer on this branch to resolve a log directory, so file logging
    // is left disabled (spec sections 21, 31: the directory is injected by the
    // platform layer when one exists).
    LogConfig config{};
    config.global_level = LogLevel::Trace;
    config.enable_console = true;
    config.enable_debugger = true;
    config.enable_file = false;
    LogSystem::initialize(config);

    set_current_thread_name("Main");

    LUDUS_LOG_INFO(LogCore, "Ludus {} starting", ludus::foundation::version_string());
    LUDUS_LOG_INFO(LogCore, "Revision: {}", ludus::foundation::git_revision());
    LUDUS_LOG_INFO(LogCore, "Compiler: {}", ludus::foundation::compiler_identity());

    LogSystem::shutdown();
    return 0;
}
