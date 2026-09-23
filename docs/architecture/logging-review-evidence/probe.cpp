#include <cstdlib>
#include <ludus/foundation/logging/log.hpp>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace ludus::foundation;
using namespace ludus::foundation::logging;
int main(int argc, char** argv)
{
    if (argc < 3)
        return 2;
    LogConfig config{};
    config.Directory = argv[2];
    config.EnableConsole = false;
    config.EnableDebugger = false;
    config.GlobalLevel = LogLevel::Warning;
    config.FlushIntervalMilliseconds = 10;
    LogSystem::Initialize(config);
    const std::string_view mode{argv[1]};
    if (mode == "race")
    {
        std::vector<std::thread> workers;
        for (uint32 t = 0; t < 8; ++t)
            workers.emplace_back([t] {
                for (uint32 i = 0; i < 500; ++i)
                    LUDUS_LOG_WARN(LOG_CORE,
                                   "producer={} record={} payload=abcdefghijklmnopqrstuvwxyz0123456789",
                                   t,
                                   i);
            });
        for (auto& worker : workers)
            worker.join();
    }
    else if (mode == "abrupt")
    {
        LUDUS_LOG_WARN(LOG_CORE, "ABRUPT_MARKER");
        ::usleep(100000);
        std::_Exit(0);
    }
    else if (mode == "error")
    {
        LUDUS_LOG_ERROR(LOG_CORE, "ERROR_MARKER");
        std::_Exit(0);
    }
    else
    {
        LUDUS_LOG_WARN(LOG_CORE, "escaped {{braces}}");
        Log(LogLevel::Info, LOG_CORE, std::source_location::current(), "DIRECT_FILTER_BYPASS");
    }
    LogSystem::Shutdown();
}
