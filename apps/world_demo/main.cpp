#include "internal/application.h"
#include <ludus/foundation/base/config.h>
#include <ludus/foundation/base/parse_number.hpp>
#include <ludus/foundation/filesystem/filesystem.hpp>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>
#include <ludus/graphics/rhi/rhi.h>
#include <string_view>
#if LUDUS_TARGET_OS == LUDUS_OS_WEB
#    include <emscripten.h>
namespace
{
ludus::world_demo::Application gApplication;
// clang-format off
EM_JS(bool, FollowCameraRequested, (), {
    return new URLSearchParams(location.search).get('camera') === 'follow';
});
EM_JS(void, Present, (ludus::foundation::uint32 state, ludus::foundation::uint32 mode, ludus::foundation::uint32 backend, ludus::foundation::uint32 tick, ludus::foundation::float32 x, ludus::foundation::float32 y), {
    const status = document.getElementById('status');
    status.dataset.state = ['stopped','loading','playing','failed'][state];
    status.dataset.mode = ['playing','paused','faulted'][mode];
    status.dataset.backend = ['vulkan','webgpu','webgl2'][backend];
    status.dataset.tick = tick; status.dataset.x = x; status.dataset.y = y;
    status.textContent = state == 3 ? 'Graphics could not start. Reload to retry.' : mode == 2 ? 'Simulation stopped. Press R to restart.' : mode == 1 ? 'Paused. N steps one tick; P resumes.' : 'WASD moves. Space attacks. P pauses. R restarts.';
});
// clang-format on
void Frame() noexcept
{
    const auto state = gApplication.Frame();
    const auto& session = gApplication.GetSession();
    const auto player = session.World().FindAuthored("player-start");
    const auto* transform = session.World().Transforms().Find(player);
    Present(static_cast<ludus::foundation::uint32>(state),
            static_cast<ludus::foundation::uint32>(session.Driver.GetMode()),
            static_cast<ludus::foundation::uint32>(ludus::graphics::rhi::GetStartup().SelectedBackend),
            static_cast<ludus::foundation::uint32>(session.World().GetTick()),
            transform != nullptr ? transform->Current.X : 0,
            transform != nullptr ? transform->Current.Y : 0);
}
} // namespace
#endif
int main(int argc, char** argv)
{
    using namespace ludus::world_demo;
    ludus::foundation::logging::LogConfig config;
    config.EnableConsole = true;
    config.EnableFile = false;
    ludus::foundation::logging::LogSystem::Initialize(config);
    // --level is an explicit bounded read. The same codec validates Editor and
    // runtime; loading never rewrites, builds or repairs the source project.
    char levelBytes[65536] = {};
    usize levelSize = 0;
    bool headless = false;
    bool follow = false;
    uint64 frameLimit = 0;
    for (int argument = 1; argument < argc; ++argument)
    {
        const std::string_view flag(argv[argument]);
        if (flag == "--headless")
        {
            headless = true;
        }
        else if (flag == "--follow-camera")
        {
            follow = true;
        }
        else if (flag == "--frames" && argument + 1 < argc)
        {
            const std::string_view text(argv[++argument]);
            if (ParseUint64(text.data(), text.size(), frameLimit) != NumberParseStatus::Success || frameLimit == 0)
            {
                return 1;
            }
        }
        else if (flag == "--level" && argument + 1 < argc)
        {
            const std::string_view path(argv[++argument]);
            if (levelSize != 0 || path.empty() || path.size() > 4096)
            {
                return 1;
            }
            // Split only at this existing filesystem compatibility boundary;
            // Directory owns the root and refuses child symlinks.
            auto slash = std::string_view::npos;
            for (usize i = 0; i < path.size(); ++i)
            {
                if (path[i] == '/')
                {
                    slash = i;
                }
            }
            const auto root = slash == std::string_view::npos ? std::string_view(".")
                              : slash == 0                    ? path.substr(0, 1)
                                                              : path.substr(0, slash);
            const auto leaf = slash == std::string_view::npos ? path : path.substr(slash + 1);
            ludus::foundation::filesystem::Directory directory;
            ludus::foundation::filesystem::File file;
            if (!directory.Open(root).Succeeded() || !directory.OpenRead(leaf, file).Succeeded() || file.Size() == 0 ||
                file.Size() > sizeof(levelBytes))
            {
                return 1;
            }
            levelSize = static_cast<usize>(file.Size());
            const auto read = file.ReadAt(0, {reinterpret_cast<uint8*>(levelBytes), levelSize});
            if (!read.Outcome.Succeeded() || read.BytesRead != levelSize)
            {
                return 1;
            }
            Level checked;
            if (ReadLevel({levelBytes, levelSize}, checked).Error != LevelError::None)
            {
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }
    const auto source = levelSize != 0 ? std::string_view(levelBytes, levelSize) : ExampleLevel();
    if (headless)
    {
        Session session;
        if (session.RequestLoad(source).Error != LevelError::None)
        {
            return 1;
        }
        for (usize step = 0; step < 40 && session.GetLoadStage() != LoadStage::Ready; ++step)
        {
            if (session.PollLoad() != Status::Success)
            {
                return 1;
            }
        }
        if (!session.Activate())
        {
            return 1;
        }
        for (usize tick = 0; tick < 120; ++tick)
        {
            if (session.World().RunTick({ .Axis = {0.5F, 0} }) != Status::Success)
            {
                return 1;
            }
        }
        LUDUS_LOG_INFO(ludus::foundation::logging::LOG_CORE,
                       "Headless reference: ticks={} hash={}",
                       session.World().GetTick(),
                       session.World().GetReplayHash());
        ludus::foundation::logging::LogSystem::Shutdown();
        return 0;
    }
#if LUDUS_TARGET_OS == LUDUS_OS_WEB
    (void)follow;
    (void)frameLimit;
    gApplication.SetCameraFollow(FollowCameraRequested());
    (void)gApplication.Start();
    emscripten_set_main_loop(Frame, 0, true);
#else
    Application application;
    application.SetCameraFollow(follow);
    if (!application.Start(ludus::graphics::rhi::BackendSelection::Auto,
                           levelSize != 0 ? levelBytes : nullptr,
                           levelSize))
    {
        return 1;
    }
    while (application.Frame() != AppState::Stopped && application.GetState() != AppState::Failed)
    {
        if (frameLimit != 0 && application.GetPresentedFrames() >= frameLimit)
        {
            break;
        }
    }
    LUDUS_LOG_INFO(ludus::foundation::logging::LOG_CORE,
                   "Reference presented {} frames",
                   application.GetPresentedFrames());
    const auto state = application.GetState();
    application.Shutdown();
    ludus::foundation::logging::LogSystem::Shutdown();
    return state == AppState::Failed ? 1 : 0;
#endif
}
