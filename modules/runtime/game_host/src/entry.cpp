// Project-owned GameHost executable (project-live-reload design 1/2/3).
//
// A thin front end over Ludus::GameHost::Run. It parses argv/cwd exactly as the
// v1 executable Run contract requires (positional args and cwd are preserved and
// not reinterpreted), resolves the control fd inherited from the supervisor, and
// runs one play session. The same binary runs headless (no control fd, bounded
// frames) for local smoke and acceptance.
//
// Usage:
//   game_host --module <abs-path> [--control-fd N] [--max-frames N]
//             [--project-id HEX] [--game-id HEX] [--headless]
//
// This executable intentionally links no Qt. The editor supervises it over the
// protocol; it never embeds the editor.

#include <ludus/runtime/game_host/host.h>

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log_system.hpp>

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <unistd.h>

namespace
{
using ludus::foundation::int32;
using ludus::foundation::uint64;

[[nodiscard]] bool ParseU64(std::string_view text, uint64& out) noexcept
{
    if (text.empty())
    {
        return false;
    }
    uint64 value = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        const uint64 digit = static_cast<uint64>(c - '0');
        if (value > (~uint64{0} - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

[[nodiscard]] bool ParseHex(std::string_view text, uint64& out) noexcept
{
    if (text.empty() || text.size() > 16)
    {
        return false;
    }
    uint64 value = 0;
    for (const char c : text)
    {
        value <<= 4;
        if (c >= '0' && c <= '9')
        {
            value |= static_cast<uint64>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            value |= static_cast<uint64>(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            value |= static_cast<uint64>(c - 'A' + 10);
        }
        else
        {
            return false;
        }
    }
    out = value;
    return true;
}
} // namespace

ludus::foundation::int32
ludus::runtime::game_host::RunMain(ludus::foundation::int32 argc, const char* const* argv, StaticEntryFn entry) noexcept
{
    ludus::foundation::logging::LogConfig logConfig;
    logConfig.EnableConsole = true;
    logConfig.EnableFile = false;
    logConfig.Mode = ludus::foundation::logging::LogMode::Synchronous;
    (void)ludus::foundation::logging::LogSystem::Initialize(logConfig);
    struct LogShutdown final
    {
        ~LogShutdown() noexcept
        {
            (void)ludus::foundation::logging::LogSystem::Shutdown();
        }
    } shutdown;

    ludus::runtime::game_host::HostConfig config;
    config.Source = entry == nullptr ? GameplaySource::DynamicModule : GameplaySource::Static;
    config.Mode = ludus::runtime::game_host::Presentation::Windowed;

    std::string modulePath;
    bool inspect = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        auto nextValue = [&](std::string_view& out) -> bool {
            if (i + 1 >= argc)
            {
                return false;
            }
            out = argv[++i];
            return true;
        };

        if (arg == "--module" || arg == "--inspect-module")
        {
            std::string_view value;
            if (!nextValue(value))
            {
                return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
            }
            modulePath = std::string(value);
            inspect = arg == "--inspect-module";
        }
        else if (arg == "--control-fd")
        {
            std::string_view value;
            uint64 fd = 0;
            if (!nextValue(value) || !ParseU64(value, fd) || fd > 0x7fffffffU)
            {
                return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
            }
            config.ControlFd = static_cast<int32>(fd);
        }
        else if (arg == "--max-frames")
        {
            std::string_view value;
            uint64 frames = 0;
            if (!nextValue(value) || !ParseU64(value, frames))
            {
                return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
            }
            config.MaxFrames = frames;
        }
        else if (arg == "--project-id")
        {
            std::string_view value;
            if (!nextValue(value) || !ParseHex(value, config.ProjectId))
            {
                return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
            }
        }
        else if (arg == "--game-id")
        {
            std::string_view value;
            if (!nextValue(value) || !ParseHex(value, config.GameId))
            {
                return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
            }
        }
        else if (arg == "--project-epoch")
        {
            std::string_view value;
            if (!nextValue(value) || !ParseHex(value, config.ProjectEpoch) || config.ProjectEpoch == 0)
            {
                return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
            }
        }
        else if (arg == "--headless")
        {
            config.Mode = ludus::runtime::game_host::Presentation::Headless;
        }
        else if (arg == "--identity")
        {
            // Report the host ABI identity and exit (acceptance/debug helper).
            const std::string_view id = ludus::runtime::game_host::HostIdentity();
            (void)::write(STDOUT_FILENO, id.data(), id.size());
            (void)::write(STDOUT_FILENO, "\n", 1);
            return 0;
        }
    }

    if (entry == nullptr && modulePath.empty())
    {
        return static_cast<int>(ludus::runtime::game_host::RunResult::BadArguments);
    }
    config.ModulePath = modulePath;
    if (inspect)
    {
        ludus::runtime::game_api::GameMetadata metadata;
        const auto result = ludus::runtime::game_host::InspectModule(modulePath, metadata);
        if (result == ludus::runtime::game_host::RunResult::Ok)
        {
            // The loader has already checked this identity against the host.
            std::string identity;
            for (const char c : std::string_view(metadata.Identity, metadata.IdentityLength))
            {
                if (c == '\\' || c == '\"')
                {
                    identity.push_back('\\');
                }
                if (static_cast<ludus::foundation::uint8>(c) < 0x20)
                {
                    return static_cast<int>(ludus::runtime::game_host::RunResult::Internal);
                }
                identity.push_back(c);
            }
            const std::string record =
                "{\"sdk_identity\":\"" + identity + "\",\"abi_major\":" + std::to_string(metadata.AbiMajor) +
                ",\"abi_minor\":" + std::to_string(metadata.AbiMinor) +
                ",\"capabilities\":" + std::to_string(metadata.Capabilities) +
                ",\"property_schema\":" + std::to_string(metadata.PropertySchemaVersion) +
                ",\"checkpoint_schema\":" + std::to_string(metadata.CheckpointSchemaVersion) + "}\n";
            (void)::write(STDOUT_FILENO, record.data(), record.size());
        }
        return static_cast<int>(result);
    }

    const RunResult result = entry == nullptr ? Run(config) : RunStatic(config, entry);
    return static_cast<int>(result);
}
