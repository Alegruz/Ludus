#include <ludus/foundation/config/json.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/runtime/configuration/host_options.hpp>

#include <cerrno>
#include <string_view>

#include <unistd.h>

namespace
{
using namespace ludus::foundation;
constexpr logging::LogCategory kConfigurationLog{"Configuration"};
bool Write(std::span<const uint8> bytes) noexcept
{
    usize offset = 0;
    while (offset < bytes.size())
    {
        const auto count = ::write(STDOUT_FILENO, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            return false;
        }
        offset += static_cast<usize>(count);
    }
    return true;
}
} // namespace
int main(int argc, char** argv)
{
    using namespace ludus::foundation;
    using namespace config;
    logging::LogConfig logConfig;
    logConfig.EnableConsole = true;
    logConfig.EnableFile = false;
    logConfig.Mode = logging::LogMode::Synchronous;
    (void)logging::LogSystem::Initialize(logConfig);
    struct Shutdown final
    {
        ~Shutdown() noexcept
        {
            (void)logging::LogSystem::Shutdown();
        }
    } shutdown;
    if (argc < 2 || argc > 3)
    {
        LUDUS_LOG_ERROR(kConfigurationLog, "Usage: ludus-config --schema | --validate [project|preference]");
        return 2;
    }
    Context context;
    Diagnostic error;
    const auto& domain = GetSystemAllocationDomain();
    if (context.Initialize(ludus::runtime::configuration::HostSchema(), domain, error) != Status::Ok)
    {
        return 1;
    }
    uint8 bytes[MAX_BUNDLE_BYTES + 1]{};
    std::span<const uint8> output;
    const std::string_view action = argv[1];
    Status status = Status::InvalidState;
    if (action == "--schema" && argc == 2)
    {
        status = WriteSchema(context, ludus::runtime::configuration::HOST_SCHEMA, bytes, output);
    }
    else if (action == "--validate")
    {
        Layer layer = Layer::Project;
        if (argc == 3)
        {
            if (std::string_view(argv[2]) == "preference")
            {
                layer = Layer::Preference;
            }
            else if (std::string_view(argv[2]) != "project")
            {
                return 2;
            }
        }
        usize size = 0;
        while (size < sizeof(bytes))
        {
            const auto count = ::read(STDIN_FILENO, bytes + size, sizeof(bytes) - size);
            if (count < 0 && errno == EINTR)
            {
                continue;
            }
            if (count < 0)
            {
                return 1;
            }
            if (count == 0)
            {
                break;
            }
            size += static_cast<usize>(count);
        }
        if (size > MAX_BUNDLE_BYTES)
        {
            return 1;
        }
        status = PrepareJson(context,
                             {reinterpret_cast<const char*>(bytes), size},
                             layer,
                             ludus::runtime::configuration::HOST_SCHEMA,
                             context.Revision(),
                             domain,
                             error);
        if (status == Status::Ok)
        {
            status = context.Commit(context.Revision(), error);
        }
        if (status == Status::Ok)
        {
            status = WriteLayer(context, layer, ludus::runtime::configuration::HOST_SCHEMA, bytes, output);
        }
    }
    if (status != Status::Ok)
    {
        LUDUS_LOG_ERROR(kConfigurationLog,
                        "Configuration {} at key '{}' record {}",
                        StatusName(status),
                        error.Key.GetView(),
                        error.Record);
        return 1;
    }
    return Write(output) ? 0 : 1;
}
