#include <ludus/runtime/game_host/host.h>

#include "internal/host_services.h"
#include "internal/identity.h"
#include "internal/module_loader.h"

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/logging/log_format.hpp>

namespace ludus::runtime::game_host
{
namespace
{
LUDUS_DEFINE_LOG_CATEGORY(LOG_HOST_RUN, "GameHost");

using game_api::CreateInfo;
using game_api::FrameInput;
using game_api::GameInstance;
using game_api::RenderParams;
using game_api::Status;
} // namespace

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
    // L0/L1 headless run path: owns the gameplay module lifecycle and the
    // mandatory Query/Create/Update/Destroy sequence without a surface. The
    // windowed path (platform window + RHI session) and the full protocol loop
    // are layered on in L1; this proves the loader and ABI end to end where no
    // GPU/display exists (a headless run is NOT proof of rendered behavior).
    if (config.Source != GameplaySource::DynamicModule)
    {
        // Static dispatch is wired by the shipping executable in L1.
        return RunResult::BadArguments;
    }
    if (config.ModulePath.empty())
    {
        return RunResult::BadArguments;
    }

    LoadedModule module;
    const LoadStatus loadStatus =
        LoadModule(config.ModulePath, 1, CurrentHostIdentity(), CurrentAbiMajor(), CurrentAbiMinor(), module);
    if (loadStatus != LoadStatus::Ok)
    {
        LUDUS_LOG_ERROR(LOG_HOST_RUN, "module load rejected: {}", LoadStatusName(loadStatus));
        return loadStatus == LoadStatus::IdentityRejected ? RunResult::IncompatibleModule : RunResult::ModuleLoadFailed;
    }

    HostServiceProvider services;

    CreateInfo info = {};
    info.StructSize = static_cast<uint32>(sizeof(CreateInfo));
    info.Services = &services.Services();
    info.ProjectId = config.ProjectId;
    info.GameId = config.GameId;
    info.ModuleGeneration = module.Generation();

    GameInstance* instance = nullptr;
    const Status createStatus = module.Table().Create(&info, &instance);
    if (createStatus != Status::Ok || instance == nullptr)
    {
        LUDUS_LOG_ERROR(LOG_HOST_RUN, "module Create failed: {}", static_cast<uint32>(createStatus));
        return RunResult::Internal;
    }

    const uint64 frames = config.MaxFrames == 0 ? 1 : config.MaxFrames;
    for (uint64 i = 0; i < frames; ++i)
    {
        FrameInput input = {};
        input.FrameIndex = i;
        input.DeltaSeconds = 1.0 / 60.0;
        input.ElapsedSeconds = static_cast<ludus::foundation::float64>(i) / 60.0;
        input.Width = 800;
        input.Height = 600;

        RenderParams render = {};
        const Status updateStatus = module.Table().Update(instance, &input, &render);
        if (updateStatus != Status::Ok)
        {
            LUDUS_LOG_ERROR(LOG_HOST_RUN, "module Update failed at frame {}: {}", i, static_cast<uint32>(updateStatus));
            module.Table().Destroy(instance);
            return RunResult::Internal;
        }
    }

    // Retire the instance before releasing the loader reference (design 8).
    module.Table().Destroy(instance);
    instance = nullptr;

    if (services.OutstandingAllocations() != 0)
    {
        LUDUS_LOG_ERROR(LOG_HOST_RUN, "module leaked {} host allocations", services.OutstandingAllocations());
        return RunResult::Internal;
    }

    module.Close();
    return RunResult::Ok;
}
} // namespace ludus::runtime::game_host
