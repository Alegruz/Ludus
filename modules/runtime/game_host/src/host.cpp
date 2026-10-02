#include <ludus/runtime/game_host/host.h>

#include "internal/host_session.h"
#include "internal/identity.h"

namespace ludus::runtime::game_host
{
std::string_view HostIdentity() noexcept
{
    return CurrentHostIdentity();
}

uint32 HostAbiMajor() noexcept
{
    return CurrentAbiMajor();
}

uint32 HostAbiMinor() noexcept
{
    return CurrentAbiMinor();
}

RunResult Run(const HostConfig& config) noexcept
{
    // The host owns the gameplay module lifecycle, the frame loop and (when a
    // ControlFd is set) the play protocol. The windowed path drives a platform
    // window + RHI session; where no GPU/display exists the host runs the full
    // lifecycle headlessly (a headless run is NOT proof of rendered behavior).
    if (config.Source != GameplaySource::DynamicModule)
    {
        // Static dispatch is wired by the shipping executable (handled there).
        return RunResult::BadArguments;
    }
    if (config.ModulePath.empty())
    {
        return RunResult::BadArguments;
    }

    HostSession session(config.ProjectId, config.GameId, config.ControlFd);
    if (!session.LoadInitial(config.ModulePath))
    {
        // Distinguish an incompatible module from an ordinary load failure.
        return session.State() == PlayState::Failed ? RunResult::IncompatibleModule : RunResult::ModuleLoadFailed;
    }
    return session.RunLoop(config.MaxFrames);
}

RunResult RunStatic(const HostConfig& config, StaticEntryFn entry) noexcept
{
    if (entry == nullptr)
    {
        return RunResult::BadArguments;
    }
    return RunStaticEntry(config, entry);
}
} // namespace ludus::runtime::game_host
